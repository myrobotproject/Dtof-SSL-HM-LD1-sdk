#include "transport/packet_source_factory.hpp"

#include <array>
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

class SerialPacketSource final : public PacketSource {
public:
    explicit SerialPacketSource(std::string crcMode)
        : parser_(std::move(crcMode)) {}

    bool Open(const CameraConfig& config, std::string* error) override {
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
            lastError_.clear();
            event->type = internal::SourceEventType::Measurement;
            return true;
        }
        return true;
    }

    SerialPort serialPort_;
    FrameParser parser_;
    std::array<uint8_t, 4096> readBuffer_ {};
    size_t parseFailureCount_ = 0;
    std::string lastError_;
};

}  // namespace

std::unique_ptr<PacketSource> CreateSerialPacketSource(std::string crcMode) {
    return std::make_unique<SerialPacketSource>(std::move(crcMode));
}

}  // namespace hm_ld1
