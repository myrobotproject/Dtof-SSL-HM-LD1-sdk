#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace hm_ld1 {

constexpr uint32_t kDepthWidth = 40;
constexpr uint32_t kDepthHeight = 30;
constexpr size_t kDepthPointCount = static_cast<size_t>(kDepthWidth) * static_cast<size_t>(kDepthHeight);
constexpr size_t kCalibrationParameterCount = 12;
constexpr uint32_t kHistogramWidth = 2560;
constexpr uint32_t kHistogramHeight = 30;

enum class TransportType {
    Serial,
    Udp,
    Uvc,
};

enum class UvcStreamProfile {
    Auto,
    Depth40x30,
    Mixed120x90,
    PointCloud160x120,
    Raw480x360,
};

enum class TimestampUnit {
    Unknown,
    Milliseconds,
    Microseconds,
    Nanoseconds,
};

enum class PointCloudSource {
    Direct,
    DerivedFromDepth,
};

struct SerialConfig {
    std::string port;
    int baud = 921600;
    std::string crcMode = "auto";
};

struct UdpConfig {
    std::string bindAddress = "0.0.0.0";
    int port = 2368;
    std::string interfaceName;
    bool autoConfig = false;
    int autoConfigTimeoutMs = 5000;
};

struct UvcConfig {
    std::string device = "/dev/video0";
    UvcStreamProfile workingProfile = UvcStreamProfile::Auto;
    bool bootstrapCalibration = true;
    UvcStreamProfile bootstrapProfile = UvcStreamProfile::Mixed120x90;
    int bootstrapTimeoutMs = 1500;
};

struct CameraConfig {
    TransportType transportType = TransportType::Serial;
    SerialConfig serial;
    UdpConfig udp;
    UvcConfig uvc;
};

struct Point3f {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

struct CameraCalibration {
    float fx = 0.0f;
    float fy = 0.0f;
    float cx = 0.0f;
    float cy = 0.0f;
    float k1 = 0.0f;
    float k2 = 0.0f;
    float p1 = 0.0f;
    float p2 = 0.0f;
    float k3 = 0.0f;
    float k4 = 0.0f;
    float k5 = 0.0f;
    float k6 = 0.0f;
    std::array<float, kCalibrationParameterCount> raw {};
    bool valid = false;
};

struct DeviceTimestamp {
    bool valid = false;
    uint64_t value = 0;
    TimestampUnit unit = TimestampUnit::Unknown;
    uint32_t raw0 = 0;
    uint32_t raw1 = 0;
};

struct FrameClock {
    uint64_t hostMonotonicTimeNs = 0;
    DeviceTimestamp device;
};

struct DeviceInfo {
    std::optional<uint8_t> protocolVersion;
    std::optional<std::string> socVersion;
    std::optional<std::string> socSerialNumber;
    std::optional<std::string> sensorVersion;
    std::optional<std::string> sensorSerialNumber;
    std::optional<uint8_t> stateCode;
    std::optional<float> sensorTemperatureCelsius;
    std::optional<std::string> versionText;
    std::vector<uint8_t> vendorBlob;
    std::vector<uint8_t> eeprom;
};

template <typename T>
struct ImageFrame {
    uint32_t width = 0;
    uint32_t height = 0;
    std::vector<T> data;

    bool empty() const {
        return data.empty();
    }

    size_t size() const {
        return data.size();
    }
};

struct PointCloudFrame {
    uint32_t width = 0;
    uint32_t height = 0;
    PointCloudSource source = PointCloudSource::DerivedFromDepth;
    std::vector<Point3f> points;

    bool empty() const {
        return points.empty();
    }

    size_t size() const {
        return points.size();
    }
};

struct ConfidenceFrame {
    bool valid = false;
    uint32_t width = 0;
    uint32_t height = 0;
    uint16_t bitDepth = 0;
    std::vector<uint16_t> values;

    bool empty() const {
        return !valid || values.empty();
    }
};

struct HistogramFrame {
    bool valid = false;
    uint32_t width = 0;
    uint32_t height = 0;
    std::vector<uint16_t> bins;

    bool empty() const {
        return !valid || bins.empty();
    }
};

struct FrameSet {
    uint64_t sequence = 0;
    TransportType transportType = TransportType::Serial;
    UvcStreamProfile activeUvcProfile = UvcStreamProfile::Auto;
    FrameClock clock;
    CameraCalibration calibration;
    DeviceInfo infoSnapshot;
    ImageFrame<uint16_t> depth;
    PointCloudFrame pointCloud;
    ConfidenceFrame confidence;
    HistogramFrame histogram;

    bool empty() const {
        return depth.empty() && pointCloud.empty() && confidence.empty() && histogram.empty();
    }
};

struct StreamCapabilities {
    bool depthAlwaysAvailable = true;
    bool pointCloudAlwaysAvailable = true;
    bool calibrationAlwaysAvailableAfterOpen = false;
    bool canBootstrapCalibration = false;
    bool mayProvideConfidence = false;
    bool mayProvideHistogram = false;
    bool mayProvideDeviceInfo = false;
    bool mayProvideTemperature = false;
    bool mayProvideStateCode = false;
    bool mayProvideDeviceTimestamp = false;
};

struct CameraStats {
    size_t okPackets = 0;
    size_t parseFailures = 0;
    size_t crcFailures = 0;
    size_t badLengths = 0;
    size_t discardedBytes = 0;
    std::string lastError;
};

class Camera {
public:
    Camera();
    ~Camera();

    Camera(const Camera&) = delete;
    Camera& operator=(const Camera&) = delete;
    Camera(Camera&&) noexcept;
    Camera& operator=(Camera&&) noexcept;

    bool Open(const CameraConfig& config, std::string* error);
    bool Poll(FrameSet* frame, std::string* error);
    void Close();

    StreamCapabilities Capabilities() const;
    std::optional<DeviceInfo> LatestDeviceInfo() const;
    std::optional<CameraCalibration> LatestCalibration() const;
    CameraStats Stats() const;
    std::string Describe() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace hm_ld1
