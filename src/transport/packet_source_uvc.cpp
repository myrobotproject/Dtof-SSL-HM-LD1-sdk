#include "transport/packet_source_factory.hpp"

#include <deque>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "internal/error_utils.hpp"
#include "internal/uvc_profile_utils.hpp"
#include "protocol/uvc_protocol.hpp"
#include "transport/uvc_device.hpp"

namespace hm_ld1 {
namespace {

class UvcPacketSource final : public PacketSource {
public:
    explicit UvcPacketSource(CameraConfig config)
        : config_(std::move(config)) {}

    bool Open(const CameraConfig& config, std::string* error) override {
        internal::ClearError(error);
        config_ = config;
        resolvedProfile_ = internal::ResolveUvcProfile(config_.uvc.workingProfile);
        if (!internal::GetUvcStreamDimensions(resolvedProfile_, &streamWidth_, &streamHeight_)) {
            internal::SetError(error, "Unsupported UVC stream profile");
            return false;
        }
        return device_.Open(config_.uvc.device, streamWidth_, streamHeight_, error);
    }

    bool Poll(internal::SourceEvent* event, std::string* error) override {
        *event = internal::SourceEvent();
        internal::ClearError(error);
        if (!readyEvents_.empty()) {
            *event = std::move(readyEvents_.front());
            readyEvents_.pop_front();
            return true;
        }

        if (!device_.ReadFrame(&frameBuffer_, error)) {
            return false;
        }
        if (frameBuffer_.empty()) {
            return true;
        }

        UvcParsedFrame parsedFrame;
        std::string parseError;
        if (!ParseUvcPayload(frameBuffer_, resolvedProfile_, &parsedFrame, &parseError)) {
            ++parseFailureCount_;
            lastError_ = std::move(parseError);
            return true;
        }

        ++okPacketCount_;
        lastError_.clear();
        return HandleParsedFrame(std::move(parsedFrame), event);
    }

    void Close() override {
        readyEvents_.clear();
        pendingPointCloud_.reset();
        device_.Close();
    }

    CameraStats Stats() const override {
        CameraStats stats;
        stats.okPackets = okPacketCount_;
        stats.parseFailures = parseFailureCount_;
        stats.lastError = lastError_;
        return stats;
    }

    std::string Describe(const CameraConfig& config) const override {
        std::ostringstream stream;
        stream << "uvc " << config.uvc.device
               << " " << internal::UvcProfileName(resolvedProfile_)
               << " yuyv";
        return stream.str();
    }

private:
    bool HandleParsedFrame(UvcParsedFrame parsedFrame, internal::SourceEvent* event) {
        if (resolvedProfile_ != UvcStreamProfile::Mixed120x90 || parsedFrame.type == UvcParsedFrameType::Info) {
            *event = std::move(parsedFrame.event);
            return true;
        }

        if (parsedFrame.type == UvcParsedFrameType::PointCloud) {
            if (pendingPointCloud_.has_value()) {
                readyEvents_.push_back(std::move(*pendingPointCloud_));
                pendingPointCloud_.reset();
            }
            pendingPointCloud_ = std::move(parsedFrame.event);
            if (!readyEvents_.empty()) {
                *event = std::move(readyEvents_.front());
                readyEvents_.pop_front();
            }
            return true;
        }

        if (parsedFrame.type == UvcParsedFrameType::Depth && pendingPointCloud_.has_value()) {
            internal::SourceEvent combined = std::move(*pendingPointCloud_);
            pendingPointCloud_.reset();
            internal::MergeDeviceInfo(parsedFrame.event.infoUpdate, &combined.infoUpdate);
            if (parsedFrame.event.measurement.clock.device.valid) {
                combined.measurement.clock.device = parsedFrame.event.measurement.clock.device;
            }
            if (parsedFrame.event.measurement.calibration.has_value()) {
                combined.measurement.calibration = parsedFrame.event.measurement.calibration;
            }
            combined.measurement.depth = std::move(parsedFrame.event.measurement.depth);
            combined.measurement.confidence = std::move(parsedFrame.event.measurement.confidence);
            combined.measurement.histogram = std::move(parsedFrame.event.measurement.histogram);
            combined.measurement.activeUvcProfile = UvcStreamProfile::Mixed120x90;
            *event = std::move(combined);
            return true;
        }

        *event = std::move(parsedFrame.event);
        return true;
    }

    CameraConfig config_;
    UvcStreamProfile resolvedProfile_ = UvcStreamProfile::Auto;
    uint32_t streamWidth_ = 0;
    uint32_t streamHeight_ = 0;
    UvcDevice device_;
    std::vector<uint8_t> frameBuffer_;
    std::deque<internal::SourceEvent> readyEvents_;
    std::optional<internal::SourceEvent> pendingPointCloud_;
    size_t okPacketCount_ = 0;
    size_t parseFailureCount_ = 0;
    std::string lastError_;
};

}  // namespace

std::unique_ptr<PacketSource> CreateUvcPacketSource(CameraConfig config) {
    return std::make_unique<UvcPacketSource>(std::move(config));
}

}  // namespace hm_ld1
