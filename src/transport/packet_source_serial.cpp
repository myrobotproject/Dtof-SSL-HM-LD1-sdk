#include "transport/packet_source_factory.hpp"

#include <array>
#include <memory>
#include <sstream>
#include <string>
#include <utility>

#include "internal/error_utils.hpp"
#include "internal/timestamp_normalizer.hpp"
#include "protocol/frame_parser.hpp"
#include "protocol/serial_protocol.hpp"
#include "transport/serial_port.hpp"

namespace hm_ld1 {
namespace {

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
        timestampNormalizer_.Reset();
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
        timestampNormalizer_.Reset();
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
            if (!ParseSerialDataFrame(frame.msgData, &event->measurement, &event->infoUpdate)) {
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
        const uint64_t realTimestampUs = timestampNormalizer_.Normalize(rawTimestampMs, internal::SystemTimeNowUs());
        SetDeviceTimestamp(measurement, realTimestampUs, rawTimestampMs);
    }

    SerialPort serialPort_;
    FrameParser parser_;
    std::array<uint8_t, 4096> readBuffer_ {};
    size_t parseFailureCount_ = 0;
    std::string lastError_;
    internal::SerialTimestampNormalizer timestampNormalizer_;
};

}  // namespace

std::unique_ptr<PacketSource> CreateSerialPacketSource(std::string crcMode) {
    return std::make_unique<SerialPacketSource>(std::move(crcMode));
}

}  // namespace hm_ld1
