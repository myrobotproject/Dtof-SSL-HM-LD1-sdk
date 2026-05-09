#include "hm_ld1_sdk/hm_ld1_sdk.hpp"

#include <chrono>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "internal/error_utils.hpp"
#include "internal/frame_event.hpp"
#include "internal/uvc_profile_utils.hpp"
#include "transport/packet_source.hpp"

namespace hm_ld1 {
namespace {

constexpr int kDefaultBootstrapTimeoutMs = 1500;

StreamCapabilities BuildCapabilities(const CameraConfig& config, bool hasBootstrappedCalibration) {
    StreamCapabilities capabilities;
    capabilities.calibrationAlwaysAvailableAfterOpen = hasBootstrappedCalibration;

    if (config.transportType == TransportType::Serial) {
        capabilities.mayProvideDeviceInfo = true;
        capabilities.mayProvideTemperature = true;
        capabilities.mayProvideStateCode = true;
        capabilities.mayProvideDeviceTimestamp = true;
        return capabilities;
    }

    if (config.transportType == TransportType::Udp) {
        capabilities.mayProvideConfidence = true;
        capabilities.mayProvideDeviceTimestamp = true;
        return capabilities;
    }

    const UvcStreamProfile profile = internal::ResolveUvcProfile(config.uvc.workingProfile);
    capabilities.canBootstrapCalibration = internal::UvcProfileNeedsBootstrap(profile);
    capabilities.calibrationAlwaysAvailableAfterOpen = hasBootstrappedCalibration;
    capabilities.mayProvideDeviceInfo = profile != UvcStreamProfile::Depth40x30;
    capabilities.mayProvideTemperature = profile != UvcStreamProfile::Depth40x30;
    capabilities.mayProvideDeviceTimestamp =
        profile == UvcStreamProfile::Mixed120x90 || profile == UvcStreamProfile::PointCloud160x120;
    capabilities.mayProvideConfidence = profile != UvcStreamProfile::Depth40x30;
    capabilities.mayProvideHistogram = profile == UvcStreamProfile::Raw480x360;
    return capabilities;
}

void MergeInfoIfPresent(const DeviceInfo& update, std::optional<DeviceInfo>* cachedInfo) {
    if (!internal::HasAnyDeviceInfo(update)) {
        return;
    }
    if (!cachedInfo->has_value()) {
        *cachedInfo = DeviceInfo();
    }
    internal::MergeDeviceInfo(update, &cachedInfo->value());
}

void MergeCalibrationIfPresent(const std::optional<CameraCalibration>& update, std::optional<CameraCalibration>* cachedCalibration) {
    if (!update.has_value() || !update->valid) {
        return;
    }
    *cachedCalibration = update;
}

void NormalizeEventOrientation(internal::SourceEvent* event) {
    if (event == nullptr) {
        return;
    }
    if (event->calibrationUpdate.has_value()) {
        event->calibrationUpdate =
            internal::NormalizeHorizontalMirrorCalibration(*event->calibrationUpdate, kDepthWidth);
    }
    internal::NormalizeMeasurementOrientation(&event->measurement);
}

bool BootstrapCalibrationForProfile(
    const CameraConfig& baseConfig,
    UvcStreamProfile profile,
    std::optional<DeviceInfo>* cachedInfo,
    std::optional<CameraCalibration>* cachedCalibration,
    std::string* error) {
    CameraConfig bootstrapConfig = baseConfig;
    bootstrapConfig.uvc.workingProfile = profile;

    std::unique_ptr<PacketSource> source = CreatePacketSource(bootstrapConfig);
    if (source == nullptr) {
        internal::SetError(error, "Failed to create an HM-LD1 UVC bootstrap source");
        return false;
    }
    if (!source->Open(bootstrapConfig, error)) {
        return false;
    }

    const int timeoutMs = baseConfig.uvc.bootstrapTimeoutMs > 0 ? baseConfig.uvc.bootstrapTimeoutMs : kDefaultBootstrapTimeoutMs;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    while (std::chrono::steady_clock::now() < deadline) {
        internal::SourceEvent event;
        if (!source->Poll(&event, error)) {
            source->Close();
            return false;
        }

        NormalizeEventOrientation(&event);

        MergeInfoIfPresent(event.infoUpdate, cachedInfo);
        MergeCalibrationIfPresent(event.calibrationUpdate, cachedCalibration);
        if (event.type == internal::SourceEventType::Measurement) {
            MergeCalibrationIfPresent(event.measurement.calibration, cachedCalibration);
        }
        if (cachedCalibration->has_value() && cachedCalibration->value().valid) {
            source->Close();
            return true;
        }
    }

    source->Close();
    return false;
}

bool BootstrapUvcCalibration(
    const CameraConfig& config,
    std::optional<DeviceInfo>* cachedInfo,
    std::optional<CameraCalibration>* cachedCalibration,
    std::string* error) {
    std::vector<UvcStreamProfile> candidates;
    const UvcStreamProfile preferred = internal::ResolveUvcProfile(config.uvc.bootstrapProfile);
    if (preferred != UvcStreamProfile::Depth40x30 && preferred != UvcStreamProfile::PointCloud160x120) {
        candidates.push_back(preferred);
    }
    if (preferred != UvcStreamProfile::Mixed120x90) {
        candidates.push_back(UvcStreamProfile::Mixed120x90);
    }
    if (preferred != UvcStreamProfile::Raw480x360) {
        candidates.push_back(UvcStreamProfile::Raw480x360);
    }

    std::string lastError;
    for (const UvcStreamProfile profile : candidates) {
        std::string attemptError;
        if (BootstrapCalibrationForProfile(
                config,
                profile,
                cachedInfo,
                cachedCalibration,
                &attemptError)) {
            return true;
        }
        lastError = attemptError;
    }

    if (!lastError.empty()) {
        internal::SetError(error, lastError);
    } else {
        internal::SetError(error, "Timed out while bootstrapping HM-LD1 UVC calibration");
    }
    return false;
}

}  // namespace

struct Camera::Impl {
    CameraConfig config {};
    std::unique_ptr<PacketSource> packetSource;
    std::optional<DeviceInfo> latestDeviceInfo;
    std::optional<CameraCalibration> latestCalibration;
    StreamCapabilities capabilities {};
    uint64_t nextSequence = 0;
};

Camera::Camera()
    : impl_(std::make_unique<Impl>()) {}

Camera::~Camera() {
    Close();
}

Camera::Camera(Camera&&) noexcept = default;

Camera& Camera::operator=(Camera&&) noexcept = default;

bool Camera::Open(const CameraConfig& config, std::string* error) {
    Close();

    std::optional<DeviceInfo> latestDeviceInfo;
    std::optional<CameraCalibration> latestCalibration;
    bool hasBootstrappedCalibration = false;
    if (config.transportType == TransportType::Uvc &&
        config.uvc.bootstrapCalibration &&
        internal::UvcProfileNeedsBootstrap(config.uvc.workingProfile)) {
        if (!BootstrapUvcCalibration(config, &latestDeviceInfo, &latestCalibration, error)) {
            return false;
        }
        hasBootstrappedCalibration = latestCalibration.has_value() && latestCalibration->valid;
    }

    std::unique_ptr<PacketSource> packetSource = CreatePacketSource(config);
    if (packetSource == nullptr) {
        internal::SetError(error, "Failed to create an HM-LD1 packet source");
        return false;
    }
    if (!packetSource->Open(config, error)) {
        return false;
    }

    impl_->config = config;
    impl_->packetSource = std::move(packetSource);
    impl_->latestDeviceInfo = std::move(latestDeviceInfo);
    impl_->latestCalibration = std::move(latestCalibration);
    impl_->capabilities = BuildCapabilities(impl_->config, hasBootstrappedCalibration);
    impl_->nextSequence = 0;
    internal::ClearError(error);
    return true;
}

bool Camera::Poll(FrameSet* frame, std::string* error) {
    if (frame == nullptr) {
        internal::SetError(error, "FrameSet output pointer is null");
        return false;
    }
    *frame = FrameSet();

    if (impl_->packetSource == nullptr) {
        internal::SetError(error, "HM-LD1 camera is not open");
        return false;
    }

    for (;;) {
        internal::SourceEvent event;
        if (!impl_->packetSource->Poll(&event, error)) {
            return false;
        }
        NormalizeEventOrientation(&event);
        if (event.type == internal::SourceEventType::None) {
            internal::ClearError(error);
            return true;
        }

        MergeInfoIfPresent(event.infoUpdate, &impl_->latestDeviceInfo);
        MergeCalibrationIfPresent(event.calibrationUpdate, &impl_->latestCalibration);
        if (event.type == internal::SourceEventType::InfoUpdate) {
            continue;
        }

        MergeCalibrationIfPresent(event.measurement.calibration, &impl_->latestCalibration);

        frame->sequence = ++impl_->nextSequence;
        frame->transportType = event.measurement.transportType;
        frame->activeUvcProfile = event.measurement.activeUvcProfile;
        frame->clock = event.measurement.clock;
        frame->clock.hostMonotonicTimeNs = internal::HostMonotonicNowNs();
        frame->depth = std::move(event.measurement.depth);
        frame->pointCloud = std::move(event.measurement.pointCloud);
        frame->confidence = std::move(event.measurement.confidence);
        frame->histogram = std::move(event.measurement.histogram);

        if (impl_->latestCalibration.has_value()) {
            frame->calibration = *impl_->latestCalibration;
        }
        if (impl_->latestDeviceInfo.has_value()) {
            frame->infoSnapshot = *impl_->latestDeviceInfo;
        }

        if (frame->depth.empty() && !frame->pointCloud.empty()) {
            internal::BuildDepthFromPointCloud(frame->pointCloud, &frame->depth);
        }
        if (frame->pointCloud.empty() && !frame->depth.empty() && frame->calibration.valid) {
            internal::BuildPointCloudFromDepth(frame->depth, frame->calibration, &frame->pointCloud);
        }

        internal::ClearError(error);
        return true;
    }
}

void Camera::Close() {
    if (impl_->packetSource != nullptr) {
        impl_->packetSource->Close();
        impl_->packetSource.reset();
    }
    impl_->latestDeviceInfo.reset();
    impl_->latestCalibration.reset();
    impl_->capabilities = StreamCapabilities();
    impl_->nextSequence = 0;
}

StreamCapabilities Camera::Capabilities() const {
    return impl_->capabilities;
}

std::optional<DeviceInfo> Camera::LatestDeviceInfo() const {
    return impl_->latestDeviceInfo;
}

std::optional<CameraCalibration> Camera::LatestCalibration() const {
    return impl_->latestCalibration;
}

CameraStats Camera::Stats() const {
    if (impl_->packetSource == nullptr) {
        return {};
    }
    return impl_->packetSource->Stats();
}

std::string Camera::Describe() const {
    if (impl_->packetSource == nullptr) {
        return "not-open";
    }
    return impl_->packetSource->Describe(impl_->config);
}

}  // namespace hm_ld1
