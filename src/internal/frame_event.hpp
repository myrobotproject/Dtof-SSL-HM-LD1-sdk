#pragma once

#include <optional>
#include <string>

#include "hm_ld1_sdk/hm_ld1_sdk.hpp"

namespace hm_ld1::internal {

enum class SourceEventType {
    None,
    InfoUpdate,
    Measurement,
};

struct Measurement {
    TransportType transportType = TransportType::Serial;
    UvcStreamProfile activeUvcProfile = UvcStreamProfile::Auto;
    FrameClock clock;
    std::optional<CameraCalibration> calibration;
    ImageFrame<uint16_t> depth;
    PointCloudFrame pointCloud;
    ConfidenceFrame confidence;
    HistogramFrame histogram;
};

struct SourceEvent {
    SourceEventType type = SourceEventType::None;
    DeviceInfo infoUpdate;
    std::optional<CameraCalibration> calibrationUpdate;
    Measurement measurement;
    std::string integrityName = "n/a";
};

bool HasAnyDeviceInfo(const DeviceInfo& info);
void MergeDeviceInfo(const DeviceInfo& update, DeviceInfo* target);
uint64_t HostMonotonicNowNs();

CameraCalibration CalibrationFromRaw(const std::array<float, kCalibrationParameterCount>& raw);
CameraCalibration NormalizeHorizontalMirrorCalibration(const CameraCalibration& calibration, uint32_t width);
void NormalizeHorizontalMirrorDepth(ImageFrame<uint16_t>* depth);
void NormalizeHorizontalMirrorConfidence(ConfidenceFrame* confidence);
void NormalizeHorizontalMirrorHistogram(HistogramFrame* histogram);
void NormalizeHorizontalMirrorPointCloud(PointCloudFrame* pointCloud);
void NormalizeMeasurementOrientation(Measurement* measurement);
bool BuildPointCloudFromDepth(
    const ImageFrame<uint16_t>& depth,
    const CameraCalibration& calibration,
    PointCloudFrame* pointCloud);
void BuildDepthFromPointCloud(const PointCloudFrame& pointCloud, ImageFrame<uint16_t>* depth);

}  // namespace hm_ld1::internal
