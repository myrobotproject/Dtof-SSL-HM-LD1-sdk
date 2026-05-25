#include "transport/packet_source_factory.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <memory>
#include <sstream>
#include <string>
#include <utility>

#include "internal/error_utils.hpp"
#include "protocol/frame_parser.hpp"
#include "protocol/serial_protocol.hpp"
#include "transport/serial_port.hpp"

namespace hm_ld1 {
namespace {

constexpr uint32_t kPpsPeriodMs = 1000;
constexpr uint32_t kPpsWrapThresholdMs = 500;
constexpr uint64_t kMicrosecondsPerMillisecond = 1000;
constexpr uint64_t kMicrosecondsPerSecond = 1000000;

enum class SerialTimestampMode {
    Unknown,
    PpsCandidate,
    PpsActive,
    DeviceCounter,
};

uint64_t SystemTimeNowUs() {
    const auto now = std::chrono::system_clock::now().time_since_epoch();
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(now).count());
}

class SerialPacketSource final : public PacketSource {
public:
    explicit SerialPacketSource(std::string crcMode)
        : parser_(std::move(crcMode)) {}

    bool Open(const CameraConfig& config, std::string* error) override {
        if (!IsSupportedCrcMode(config.serial.crcMode)) {
            internal::SetError(
                error,
                "Unsupported serial crcMode '" + config.serial.crcMode +
                    "'. Supported values: auto, none, crc8, crc8_itu, maxim, rohc.");
            return false;
        }
        ResetTimestampState();
        return serialPort_.Open(config.serial.port, config.serial.baud, error);
    }

    bool Poll(internal::SourceEvent* event, std::string* error) override {
        event->type = internal::SourceEventType::None;
        internal::ClearError(error);

        ProtocolFrame frame;
        if (parser_.TryPop(&frame)) {
            return EmitFrame(frame, event);
        }

        const int bytesRead = serialPort_.ReadSome(readBuffer_.data(), readBuffer_.size(), error);
        if (bytesRead < 0) {
            return false;
        }
        if (bytesRead == 0) {
            return true;
        }

        parser_.Append(readBuffer_.data(), static_cast<size_t>(bytesRead));
        if (!parser_.TryPop(&frame)) {
            return true;
        }
        return EmitFrame(frame, event);
    }

    void Close() override {
        serialPort_.Close();
        ResetTimestampState();
    }

    CameraStats Stats() const override {
        CameraStats stats;
        stats.okPackets = parser_.okFrames();
        stats.parseFailures = parseFailureCount_;
        stats.crcFailures = parser_.crcFailures();
        stats.badLengths = parser_.badLengths();
        stats.discardedBytes = parser_.discardedBytes();
        stats.lastError = lastError_;
        return stats;
    }

    std::string Describe(const CameraConfig& config) const override {
        std::ostringstream stream;
        stream << "serial " << config.serial.port << " @ " << config.serial.baud;
        return stream.str();
    }

private:
    bool EmitFrame(const ProtocolFrame& frame, internal::SourceEvent* event) {
        event->integrityName = parser_.activeCrcName();
        if (frame.msgId == kInfoMsgId) {
            if (!ParseSerialInfoPacket(frame.msgData, &event->infoUpdate)) {
                ++parseFailureCount_;
                lastError_ = "Failed to parse serial info packet";
                event->type = internal::SourceEventType::None;
                return true;
            }
            lastError_.clear();
            event->type = internal::SourceEventType::InfoUpdate;
            return true;
        }
        if (frame.msgId == kDataMsgId) {
            if (!ParseSerialDataFrame(frame.msgData, &event->measurement)) {
                ++parseFailureCount_;
                lastError_ = "Failed to parse serial data packet";
                event->type = internal::SourceEventType::None;
                return true;
            }
            NormalizeSerialTimestamp(&event->measurement);
            lastError_.clear();
            event->type = internal::SourceEventType::Measurement;
            return true;
        }
        return true;
    }

    void ResetTimestampState() {
        timestampMode_ = SerialTimestampMode::Unknown;
        hasPreviousRawTimestamp_ = false;
        previousRawTimestampMs_ = 0;
        ppsBoundaryBaseUs_ = 0;
        ppsWrapCount_ = 0;
        lastPpsTimestampUs_ = 0;
    }

    void SetDeviceTimestamp(internal::Measurement* measurement, uint64_t valueUs, uint32_t rawTimestampMs) {
        measurement->clock.device.valid = true;
        measurement->clock.device.value = valueUs;
        measurement->clock.device.unit = TimestampUnit::Microseconds;
        measurement->clock.device.raw0 = rawTimestampMs;
        measurement->clock.device.raw1 = 0;
    }

    void NormalizeSerialTimestamp(internal::Measurement* measurement) {
        if (measurement == nullptr || !measurement->clock.device.valid) {
            return;
        }

        const uint32_t rawTimestampMs = measurement->clock.device.raw0;
        const uint64_t nowUs = SystemTimeNowUs();

        if (rawTimestampMs > kPpsPeriodMs) {
            timestampMode_ = SerialTimestampMode::DeviceCounter;
            ppsBoundaryBaseUs_ = 0;
            ppsWrapCount_ = 0;
            lastPpsTimestampUs_ = 0;
            hasPreviousRawTimestamp_ = true;
            previousRawTimestampMs_ = rawTimestampMs;
            SetDeviceTimestamp(measurement, static_cast<uint64_t>(rawTimestampMs) * kMicrosecondsPerMillisecond, rawTimestampMs);
            return;
        }

        const bool wrappedFromHighToLow =
            hasPreviousRawTimestamp_ &&
            previousRawTimestampMs_ <= kPpsPeriodMs &&
            previousRawTimestampMs_ > rawTimestampMs &&
            (previousRawTimestampMs_ - rawTimestampMs) > kPpsWrapThresholdMs;

        if (timestampMode_ != SerialTimestampMode::PpsCandidate &&
            timestampMode_ != SerialTimestampMode::PpsActive) {
            ppsBoundaryBaseUs_ = nowUs - static_cast<uint64_t>(rawTimestampMs) * kMicrosecondsPerMillisecond;
            ppsWrapCount_ = 0;
            lastPpsTimestampUs_ = 0;
            timestampMode_ = SerialTimestampMode::PpsCandidate;
        }

        if (wrappedFromHighToLow) {
            ++ppsWrapCount_;
            timestampMode_ = SerialTimestampMode::PpsActive;
        }

        const bool stalePpsSample =
            (timestampMode_ == SerialTimestampMode::PpsCandidate || timestampMode_ == SerialTimestampMode::PpsActive) &&
            !wrappedFromHighToLow &&
            hasPreviousRawTimestamp_ &&
            rawTimestampMs < previousRawTimestampMs_;
        if (stalePpsSample && lastPpsTimestampUs_ != 0) {
            const uint64_t staleTimestampUs = lastPpsTimestampUs_ + 1;
            lastPpsTimestampUs_ = staleTimestampUs;
            SetDeviceTimestamp(measurement, staleTimestampUs, rawTimestampMs);
            return;
        }

        uint64_t realTimestampUs =
            ppsBoundaryBaseUs_ +
            ppsWrapCount_ * kMicrosecondsPerSecond +
            static_cast<uint64_t>(rawTimestampMs) * kMicrosecondsPerMillisecond;
        if (lastPpsTimestampUs_ != 0 && realTimestampUs <= lastPpsTimestampUs_) {
            realTimestampUs = std::max(nowUs, lastPpsTimestampUs_ + 1);
        }

        hasPreviousRawTimestamp_ = true;
        previousRawTimestampMs_ = rawTimestampMs;
        lastPpsTimestampUs_ = realTimestampUs;
        SetDeviceTimestamp(measurement, realTimestampUs, rawTimestampMs);
    }

    SerialPort serialPort_;
    FrameParser parser_;
    std::array<uint8_t, 4096> readBuffer_ {};
    size_t parseFailureCount_ = 0;
    std::string lastError_;
    SerialTimestampMode timestampMode_ = SerialTimestampMode::Unknown;
    bool hasPreviousRawTimestamp_ = false;
    uint32_t previousRawTimestampMs_ = 0;
    uint64_t ppsBoundaryBaseUs_ = 0;
    uint64_t ppsWrapCount_ = 0;
    uint64_t lastPpsTimestampUs_ = 0;
};

}  // namespace

std::unique_ptr<PacketSource> CreateSerialPacketSource(std::string crcMode) {
    return std::make_unique<SerialPacketSource>(std::move(crcMode));
}

}  // namespace hm_ld1
