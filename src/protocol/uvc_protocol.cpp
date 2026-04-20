#include "protocol/uvc_protocol.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstring>

#include "internal/error_utils.hpp"
#include "protocol/byte_utils.hpp"
#include "protocol/text_utils.hpp"

namespace hm_ld1 {
namespace {

constexpr size_t kPointCloudPointCount = kDepthPointCount;
constexpr size_t kPointCloudBytes = kPointCloudPointCount * 3 * sizeof(float);
constexpr size_t kDepthBytes = kDepthPointCount * sizeof(uint16_t);
constexpr size_t kConfidenceBytes = kDepthPointCount * sizeof(uint16_t);
constexpr size_t kHistogramBinCount = static_cast<size_t>(kHistogramWidth) * static_cast<size_t>(kHistogramHeight);
constexpr size_t kHistogramBytes = kHistogramBinCount * sizeof(uint16_t);
constexpr size_t kMixedPcdDeclaredLength = 4 + 4 + 4 + 4 + kPointCloudBytes + kConfidenceBytes;
constexpr size_t kMixedDepDeclaredLength = 4 + 4 + 4 + 4 + (kCalibrationParameterCount * sizeof(float)) + kDepthBytes + kConfidenceBytes;
constexpr size_t kMixedInfDeclaredLength = 4 + 4 + 16 + 16 + 16 + 16 + 569;
constexpr size_t kRawMinimumDeclaredLength =
    4 + 4 + 32 + 16 + 4 + kHistogramBytes + kDepthBytes + kConfidenceBytes + (kCalibrationParameterCount * sizeof(float));

void ResetFrame(UvcParsedFrame* frame) {
    *frame = UvcParsedFrame();
}

internal::Measurement MakeUvcMeasurement(UvcStreamProfile profile) {
    internal::Measurement measurement;
    measurement.transportType = TransportType::Uvc;
    measurement.activeUvcProfile = profile;
    return measurement;
}

bool ReadCalibrationArray(const uint8_t* data, std::array<float, kCalibrationParameterCount>* parameters) {
    if (parameters == nullptr) {
        return false;
    }
    for (size_t index = 0; index < parameters->size(); ++index) {
        (*parameters)[index] = protocol_detail::ReadLeFloat(data + index * sizeof(float));
    }
    return true;
}

void ReadPointCloud(const uint8_t* data, PointCloudFrame* pointCloud) {
    pointCloud->width = kDepthWidth;
    pointCloud->height = kDepthHeight;
    pointCloud->source = PointCloudSource::Direct;
    pointCloud->points.resize(kPointCloudPointCount);

    size_t offset = 0;
    for (size_t index = 0; index < kPointCloudPointCount; ++index) {
        pointCloud->points[index].x = protocol_detail::ReadLeFloat(data + offset);
        offset += 4;
        pointCloud->points[index].y = protocol_detail::ReadLeFloat(data + offset);
        offset += 4;
        pointCloud->points[index].z = protocol_detail::ReadLeFloat(data + offset);
        offset += 4;
    }
}

void ReadConfidence(const uint8_t* data, ConfidenceFrame* confidence) {
    confidence->valid = true;
    confidence->width = kDepthWidth;
    confidence->height = kDepthHeight;
    confidence->bitDepth = 16;
    confidence->values.resize(kPointCloudPointCount);
    for (size_t index = 0; index < kPointCloudPointCount; ++index) {
        confidence->values[index] = protocol_detail::ReadLe16(data + index * sizeof(uint16_t));
    }
}

bool ParseDepth40x30(const std::vector<uint8_t>& payload, UvcParsedFrame* frame, std::string* error) {
    if (payload.size() < kDepthBytes) {
        internal::SetError(error, "UVC 40x30 depth payload is shorter than expected");
        return false;
    }

    ResetFrame(frame);
    frame->type = UvcParsedFrameType::Depth;
    frame->event.type = internal::SourceEventType::Measurement;
    frame->event.integrityName = "uvc";
    frame->event.measurement = MakeUvcMeasurement(UvcStreamProfile::Depth40x30);
    frame->event.measurement.depth.width = kDepthWidth;
    frame->event.measurement.depth.height = kDepthHeight;
    frame->event.measurement.depth.data.resize(kDepthPointCount);

    for (size_t index = 0; index < kDepthPointCount; ++index) {
        frame->event.measurement.depth.data[index] = protocol_detail::ReadLe16(payload.data() + index * sizeof(uint16_t));
    }
    return true;
}

bool ParsePointCloud160x120(const std::vector<uint8_t>& payload, UvcParsedFrame* frame, std::string* error) {
    constexpr size_t kMinimumPayload = kPointCloudBytes + kConfidenceBytes;
    constexpr size_t kObservedTailBytes = 72;
    if (payload.size() < kMinimumPayload) {
        internal::SetError(error, "UVC 160x120 point cloud payload is shorter than expected");
        return false;
    }

    ResetFrame(frame);
    frame->type = UvcParsedFrameType::PointCloud;
    frame->event.type = internal::SourceEventType::Measurement;
    frame->event.integrityName = "uvc";
    frame->event.measurement = MakeUvcMeasurement(UvcStreamProfile::PointCloud160x120);

    ReadPointCloud(payload.data(), &frame->event.measurement.pointCloud);
    ReadConfidence(payload.data() + kPointCloudBytes, &frame->event.measurement.confidence);
    internal::BuildDepthFromPointCloud(frame->event.measurement.pointCloud, &frame->event.measurement.depth);

    if (payload.size() >= kMinimumPayload + kObservedTailBytes) {
        size_t offset = kMinimumPayload;
        frame->event.infoUpdate.socVersion = protocol_detail::TrimFixedString(payload.data() + offset, 16);
        offset += 16;
        frame->event.infoUpdate.socSerialNumber = protocol_detail::TrimFixedString(payload.data() + offset, 16);
        offset += 16;
        frame->event.infoUpdate.sensorVersion = protocol_detail::TrimFixedString(payload.data() + offset, 16);
        offset += 16;
        frame->event.infoUpdate.sensorSerialNumber = protocol_detail::TrimFixedString(payload.data() + offset, 16);
        offset += 16;
        frame->event.infoUpdate.sensorTemperatureCelsius = protocol_detail::ReadLeFloat(payload.data() + offset);
        offset += 4;
        frame->event.measurement.clock.device.valid = true;
        frame->event.measurement.clock.device.value = protocol_detail::ReadLe32(payload.data() + offset);
        frame->event.measurement.clock.device.unit = TimestampUnit::Milliseconds;
        frame->event.measurement.clock.device.raw0 = protocol_detail::ReadLe32(payload.data() + offset);
        frame->event.measurement.clock.device.raw1 = 0;
    }
    return true;
}

bool ParseMixedPcd(const std::vector<uint8_t>& payload, UvcParsedFrame* frame, std::string* error) {
    if (payload.size() < 8) {
        internal::SetError(error, "UVC 120x90 pcd frame is missing the declared length");
        return false;
    }
    const uint32_t declaredLength = protocol_detail::ReadLe32(payload.data() + 4);
    if (declaredLength < kMixedPcdDeclaredLength) {
        internal::SetError(error, "UVC 120x90 pcd declared length is too small");
        return false;
    }
    if (payload.size() < declaredLength) {
        internal::SetError(error, "UVC 120x90 pcd payload is shorter than its declared length");
        return false;
    }

    ResetFrame(frame);
    frame->type = UvcParsedFrameType::PointCloud;
    frame->event.type = internal::SourceEventType::Measurement;
    frame->event.integrityName = "uvc";
    frame->event.measurement = MakeUvcMeasurement(UvcStreamProfile::Mixed120x90);

    const uint32_t timestamp = protocol_detail::ReadLe32(payload.data() + 8);
    const float temperature = protocol_detail::ReadLeFloat(payload.data() + 12);
    ReadPointCloud(payload.data() + 16, &frame->event.measurement.pointCloud);
    ReadConfidence(payload.data() + 16 + kPointCloudBytes, &frame->event.measurement.confidence);
    internal::BuildDepthFromPointCloud(frame->event.measurement.pointCloud, &frame->event.measurement.depth);

    frame->event.infoUpdate.sensorTemperatureCelsius = temperature;
    frame->event.measurement.clock.device.valid = true;
    frame->event.measurement.clock.device.value = timestamp;
    frame->event.measurement.clock.device.unit = TimestampUnit::Milliseconds;
    frame->event.measurement.clock.device.raw0 = timestamp;
    frame->event.measurement.clock.device.raw1 = 0;
    return true;
}

bool ParseMixedDep(const std::vector<uint8_t>& payload, UvcParsedFrame* frame, std::string* error) {
    if (payload.size() < 8) {
        internal::SetError(error, "UVC 120x90 dep frame is missing the declared length");
        return false;
    }
    const uint32_t declaredLength = protocol_detail::ReadLe32(payload.data() + 4);
    if (declaredLength < kMixedDepDeclaredLength) {
        internal::SetError(error, "UVC 120x90 dep declared length is too small");
        return false;
    }
    if (payload.size() < declaredLength) {
        internal::SetError(error, "UVC 120x90 dep payload is shorter than its declared length");
        return false;
    }

    ResetFrame(frame);
    frame->type = UvcParsedFrameType::Depth;
    frame->event.type = internal::SourceEventType::Measurement;
    frame->event.integrityName = "uvc";
    frame->event.measurement = MakeUvcMeasurement(UvcStreamProfile::Mixed120x90);

    size_t offset = 8;
    const uint32_t timestamp = protocol_detail::ReadLe32(payload.data() + offset);
    offset += 4;
    const float temperature = protocol_detail::ReadLeFloat(payload.data() + offset);
    offset += 4;

    std::array<float, kCalibrationParameterCount> parameters {};
    ReadCalibrationArray(payload.data() + offset, &parameters);
    offset += kCalibrationParameterCount * sizeof(float);
    frame->event.measurement.calibration = internal::CalibrationFromRaw(parameters);

    frame->event.measurement.depth.width = kDepthWidth;
    frame->event.measurement.depth.height = kDepthHeight;
    frame->event.measurement.depth.data.resize(kDepthPointCount);
    for (size_t index = 0; index < kDepthPointCount; ++index) {
        frame->event.measurement.depth.data[index] = protocol_detail::ReadLe16(payload.data() + offset);
        offset += sizeof(uint16_t);
    }

    frame->event.measurement.confidence.valid = true;
    frame->event.measurement.confidence.width = kDepthWidth;
    frame->event.measurement.confidence.height = kDepthHeight;
    frame->event.measurement.confidence.bitDepth = 16;
    frame->event.measurement.confidence.values.resize(kDepthPointCount);
    for (size_t index = 0; index < kDepthPointCount; ++index) {
        frame->event.measurement.confidence.values[index] = protocol_detail::ReadLe16(payload.data() + offset);
        offset += sizeof(uint16_t);
    }

    frame->event.infoUpdate.sensorTemperatureCelsius = temperature;
    frame->event.measurement.clock.device.valid = true;
    frame->event.measurement.clock.device.value = timestamp;
    frame->event.measurement.clock.device.unit = TimestampUnit::Milliseconds;
    frame->event.measurement.clock.device.raw0 = timestamp;
    frame->event.measurement.clock.device.raw1 = 0;
    return true;
}

bool ParseMixedInf(const std::vector<uint8_t>& payload, UvcParsedFrame* frame, std::string* error) {
    if (payload.size() < 8) {
        internal::SetError(error, "UVC 120x90 inf frame is missing the declared length");
        return false;
    }
    const uint32_t declaredLength = protocol_detail::ReadLe32(payload.data() + 4);
    if (declaredLength < kMixedInfDeclaredLength) {
        internal::SetError(error, "UVC 120x90 inf declared length is too small");
        return false;
    }
    if (payload.size() < declaredLength) {
        internal::SetError(error, "UVC 120x90 inf payload is shorter than its declared length");
        return false;
    }

    ResetFrame(frame);
    frame->type = UvcParsedFrameType::Info;
    frame->event.type = internal::SourceEventType::InfoUpdate;
    frame->event.integrityName = "uvc";

    size_t offset = 8;
    frame->event.infoUpdate.socVersion = protocol_detail::TrimFixedString(payload.data() + offset, 16);
    offset += 16;
    frame->event.infoUpdate.socSerialNumber = protocol_detail::TrimFixedString(payload.data() + offset, 16);
    offset += 16;
    frame->event.infoUpdate.sensorVersion = protocol_detail::TrimFixedString(payload.data() + offset, 16);
    offset += 16;
    frame->event.infoUpdate.sensorSerialNumber = protocol_detail::TrimFixedString(payload.data() + offset, 16);
    offset += 16;

    std::array<float, kCalibrationParameterCount> parameters {};
    ReadCalibrationArray(payload.data() + offset, &parameters);
    frame->event.calibrationUpdate = internal::CalibrationFromRaw(parameters);
    offset += kCalibrationParameterCount * sizeof(float);

    const size_t tailBytes = declaredLength - offset;
    if (tailBytes > 0) {
        frame->event.infoUpdate.eeprom.assign(payload.begin() + static_cast<std::ptrdiff_t>(offset), payload.begin() + static_cast<std::ptrdiff_t>(declaredLength));
    }
    return true;
}

bool ParseRaw480x360(const std::vector<uint8_t>& payload, UvcParsedFrame* frame, std::string* error) {
    if (payload.size() < 8) {
        internal::SetError(error, "UVC 480x360 raw frame is missing the declared length");
        return false;
    }
    if (std::memcmp(payload.data(), "raw\0", 4) != 0) {
        internal::SetError(error, "UVC 480x360 frame does not start with raw\\0");
        return false;
    }

    const uint32_t declaredLength = protocol_detail::ReadLe32(payload.data() + 4);
    if (declaredLength < kRawMinimumDeclaredLength) {
        internal::SetError(error, "UVC 480x360 declared length is too small");
        return false;
    }
    if (payload.size() < declaredLength) {
        internal::SetError(error, "UVC 480x360 payload is shorter than its declared length");
        return false;
    }

    ResetFrame(frame);
    frame->type = UvcParsedFrameType::Depth;
    frame->event.type = internal::SourceEventType::Measurement;
    frame->event.integrityName = "uvc";
    frame->event.measurement = MakeUvcMeasurement(UvcStreamProfile::Raw480x360);

    size_t offset = 8;
    frame->event.infoUpdate.versionText = protocol_detail::TrimFixedString(payload.data() + offset, 32);
    offset += 32;
    frame->event.infoUpdate.socSerialNumber = protocol_detail::TrimFixedString(payload.data() + offset, 16);
    offset += 16;
    frame->event.infoUpdate.sensorTemperatureCelsius = protocol_detail::ReadLeFloat(payload.data() + offset);
    offset += 4;

    frame->event.measurement.histogram.valid = true;
    frame->event.measurement.histogram.width = kHistogramWidth;
    frame->event.measurement.histogram.height = kHistogramHeight;
    frame->event.measurement.histogram.bins.resize(kHistogramBinCount);
    for (size_t index = 0; index < kHistogramBinCount; ++index) {
        frame->event.measurement.histogram.bins[index] = protocol_detail::ReadLe16(payload.data() + offset);
        offset += sizeof(uint16_t);
    }

    frame->event.measurement.depth.width = kDepthWidth;
    frame->event.measurement.depth.height = kDepthHeight;
    frame->event.measurement.depth.data.resize(kDepthPointCount);
    for (size_t index = 0; index < kDepthPointCount; ++index) {
        frame->event.measurement.depth.data[index] = protocol_detail::ReadLe16(payload.data() + offset);
        offset += sizeof(uint16_t);
    }

    frame->event.measurement.confidence.valid = true;
    frame->event.measurement.confidence.width = kDepthWidth;
    frame->event.measurement.confidence.height = kDepthHeight;
    frame->event.measurement.confidence.bitDepth = 16;
    frame->event.measurement.confidence.values.resize(kDepthPointCount);
    for (size_t index = 0; index < kDepthPointCount; ++index) {
        frame->event.measurement.confidence.values[index] = protocol_detail::ReadLe16(payload.data() + offset);
        offset += sizeof(uint16_t);
    }

    std::array<float, kCalibrationParameterCount> parameters {};
    ReadCalibrationArray(payload.data() + offset, &parameters);
    offset += kCalibrationParameterCount * sizeof(float);
    frame->event.measurement.calibration = internal::CalibrationFromRaw(parameters);

    if (declaredLength > offset) {
        frame->event.infoUpdate.eeprom.assign(payload.begin() + static_cast<std::ptrdiff_t>(offset), payload.begin() + static_cast<std::ptrdiff_t>(declaredLength));
    }
    return true;
}

}  // namespace

bool ParseUvcPayload(
    const std::vector<uint8_t>& payload,
    UvcStreamProfile profile,
    UvcParsedFrame* frame,
    std::string* error) {
    if (frame == nullptr) {
        internal::SetError(error, "UVC parsed frame output pointer is null");
        return false;
    }
    internal::ClearError(error);

    if (profile == UvcStreamProfile::Auto) {
        internal::SetError(error, "UVC auto profile must be resolved before parsing");
        return false;
    }
    if (profile == UvcStreamProfile::Depth40x30) {
        return ParseDepth40x30(payload, frame, error);
    }
    if (profile == UvcStreamProfile::PointCloud160x120) {
        return ParsePointCloud160x120(payload, frame, error);
    }
    if (profile == UvcStreamProfile::Raw480x360) {
        return ParseRaw480x360(payload, frame, error);
    }

    if (payload.size() < 4) {
        internal::SetError(error, "UVC 120x90 payload is missing the frame type header");
        return false;
    }
    if (std::memcmp(payload.data(), "pcd\0", 4) == 0) {
        return ParseMixedPcd(payload, frame, error);
    }
    if (std::memcmp(payload.data(), "dep\0", 4) == 0) {
        return ParseMixedDep(payload, frame, error);
    }
    if (std::memcmp(payload.data(), "inf\0", 4) == 0) {
        return ParseMixedInf(payload, frame, error);
    }

    internal::SetError(error, "Unknown UVC mixed frame type");
    return false;
}

}  // namespace hm_ld1
