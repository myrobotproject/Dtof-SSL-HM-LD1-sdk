#include "internal/frame_event.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <limits>

namespace hm_ld1::internal {
namespace {

float SafeReciprocal(float value) {
    if (std::fabs(value) <= 1e-12f) {
        return 0.0f;
    }
    return 1.0f / value;
}

bool HasCalibrationNumbers(const CameraCalibration& calibration) {
    return std::isfinite(calibration.fx) &&
        std::isfinite(calibration.fy) &&
        std::fabs(calibration.fx) > 1e-6f &&
        std::fabs(calibration.fy) > 1e-6f;
}

bool UndistortPixelToRay(
    const CameraCalibration& calibration,
    float pixelX,
    float pixelY,
    float* rayX,
    float* rayY) {
    if (!calibration.valid || rayX == nullptr || rayY == nullptr) {
        return false;
    }

    const float xd = (pixelX - calibration.cx) * SafeReciprocal(calibration.fx);
    const float yd = (pixelY - calibration.cy) * SafeReciprocal(calibration.fy);

    float x = xd;
    float y = yd;
    for (int iteration = 0; iteration < 8; ++iteration) {
        const float r2 = x * x + y * y;
        const float r4 = r2 * r2;
        const float r6 = r4 * r2;

        const float radialNumerator = 1.0f + calibration.k1 * r2 + calibration.k2 * r4 + calibration.k3 * r6;
        const float radialDenominator = 1.0f + calibration.k4 * r2 + calibration.k5 * r4 + calibration.k6 * r6;
        if (std::fabs(radialNumerator) <= 1e-12f || std::fabs(radialDenominator) <= 1e-12f) {
            return false;
        }

        const float radial = radialNumerator / radialDenominator;
        const float deltaX = 2.0f * calibration.p1 * x * y + calibration.p2 * (r2 + 2.0f * x * x);
        const float deltaY = calibration.p1 * (r2 + 2.0f * y * y) + 2.0f * calibration.p2 * x * y;

        x = (xd - deltaX) / radial;
        y = (yd - deltaY) / radial;
    }

    if (!std::isfinite(x) || !std::isfinite(y)) {
        return false;
    }

    *rayX = x;
    *rayY = y;
    return true;
}

template <typename T>
void MergeOptional(const std::optional<T>& update, std::optional<T>* target) {
    if (update.has_value()) {
        *target = update;
    }
}

}  // namespace

bool HasAnyDeviceInfo(const DeviceInfo& info) {
    return info.protocolVersion.has_value() ||
        info.socVersion.has_value() ||
        info.socSerialNumber.has_value() ||
        info.sensorVersion.has_value() ||
        info.sensorSerialNumber.has_value() ||
        info.stateCode.has_value() ||
        info.sensorTemperatureCelsius.has_value() ||
        info.versionText.has_value() ||
        !info.vendorBlob.empty() ||
        !info.eeprom.empty();
}

void MergeDeviceInfo(const DeviceInfo& update, DeviceInfo* target) {
    if (target == nullptr) {
        return;
    }

    MergeOptional(update.protocolVersion, &target->protocolVersion);
    MergeOptional(update.socVersion, &target->socVersion);
    MergeOptional(update.socSerialNumber, &target->socSerialNumber);
    MergeOptional(update.sensorVersion, &target->sensorVersion);
    MergeOptional(update.sensorSerialNumber, &target->sensorSerialNumber);
    MergeOptional(update.stateCode, &target->stateCode);
    MergeOptional(update.sensorTemperatureCelsius, &target->sensorTemperatureCelsius);
    MergeOptional(update.versionText, &target->versionText);

    if (!update.vendorBlob.empty()) {
        target->vendorBlob = update.vendorBlob;
    }
    if (!update.eeprom.empty()) {
        target->eeprom = update.eeprom;
    }
}

uint64_t HostMonotonicNowNs() {
    const auto now = std::chrono::steady_clock::now().time_since_epoch();
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(now).count());
}

CameraCalibration CalibrationFromRaw(const std::array<float, kCalibrationParameterCount>& raw) {
    CameraCalibration calibration;
    calibration.raw = raw;
    calibration.fx = raw[0];
    calibration.fy = raw[1];
    calibration.cx = raw[2];
    calibration.cy = raw[3];
    calibration.k1 = raw[4];
    calibration.k2 = raw[5];
    calibration.p1 = raw[6];
    calibration.p2 = raw[7];
    calibration.k3 = raw[8];
    calibration.k4 = raw[9];
    calibration.k5 = raw[10];
    calibration.k6 = raw[11];
    calibration.valid = HasCalibrationNumbers(calibration);
    return calibration;
}

bool BuildPointCloudFromDepth(
    const ImageFrame<uint16_t>& depth,
    const CameraCalibration& calibration,
    PointCloudFrame* pointCloud) {
    if (pointCloud == nullptr ||
        !calibration.valid ||
        depth.width == 0 ||
        depth.height == 0 ||
        depth.data.size() != static_cast<size_t>(depth.width) * static_cast<size_t>(depth.height)) {
        return false;
    }

    pointCloud->width = depth.width;
    pointCloud->height = depth.height;
    pointCloud->source = PointCloudSource::DerivedFromDepth;
    pointCloud->points.assign(depth.data.size(), Point3f {});

    for (uint32_t y = 0; y < depth.height; ++y) {
        for (uint32_t x = 0; x < depth.width; ++x) {
            const size_t index = static_cast<size_t>(y) * static_cast<size_t>(depth.width) + static_cast<size_t>(x);
            const float depthValue = static_cast<float>(depth.data[index]);
            if (depthValue <= 0.0f) {
                continue;
            }

            float rayX = 0.0f;
            float rayY = 0.0f;
            if (!UndistortPixelToRay(calibration, static_cast<float>(x), static_cast<float>(y), &rayX, &rayY)) {
                pointCloud->points.clear();
                pointCloud->width = 0;
                pointCloud->height = 0;
                return false;
            }

            pointCloud->points[index] = Point3f {rayX * depthValue, rayY * depthValue, depthValue};
        }
    }

    return true;
}

void BuildDepthFromPointCloud(const PointCloudFrame& pointCloud, ImageFrame<uint16_t>* depth) {
    if (depth == nullptr) {
        return;
    }

    const size_t expectedSize = static_cast<size_t>(pointCloud.width) * static_cast<size_t>(pointCloud.height);
    if (pointCloud.width == 0 || pointCloud.height == 0 || pointCloud.points.size() != expectedSize) {
        *depth = ImageFrame<uint16_t>();
        return;
    }

    depth->width = pointCloud.width;
    depth->height = pointCloud.height;
    depth->data.assign(pointCloud.points.size(), 0);

    for (size_t index = 0; index < pointCloud.points.size(); ++index) {
        const float z = pointCloud.points[index].z;
        if (!std::isfinite(z) || z <= 0.0f) {
            continue;
        }
        const float clamped = std::clamp(z, 0.0f, static_cast<float>(std::numeric_limits<uint16_t>::max()));
        depth->data[index] = static_cast<uint16_t>(std::lround(clamped));
    }
}

}  // namespace hm_ld1::internal
