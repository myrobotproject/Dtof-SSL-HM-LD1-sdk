#include "hm_ld1_sdk/hm_ld1_sdk.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

namespace hm_ld1 {
namespace {

struct Rgb {
    uint8_t red = 0;
    uint8_t green = 0;
    uint8_t blue = 0;
};

struct Canvas {
    int width = 0;
    int height = 0;
    std::vector<Rgb> pixels;
};

struct DepthStats {
    uint16_t minValue = 0;
    uint16_t maxValue = 0;
    size_t validCount = 0;
};

struct Vec3 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

struct Mat3 {
    float m[3][3] = {};
};

struct CameraView {
    Vec3 position;
    Vec3 right;
    Vec3 up;
    Vec3 forward;
    float focalLength = 1.0f;
    int centerX = 0;
    int centerY = 0;
    float distance = 1.0f;
};

struct ScreenPoint {
    int x = 0;
    int y = 0;
    float depthOrder = 0.0f;
    Rgb color;
};

constexpr float kNearClipDistanceMm = 10.0f;
constexpr float kCameraTargetDistanceScale = 1.0965856f;
constexpr float kDefaultBaseCameraDistanceMm = 400.0f;
constexpr int kCanvasWidth = 1280;
constexpr int kCanvasHeight = 720;
constexpr int kPointSize = 4;
constexpr float kInitialYawDeg = 25.0f;
constexpr float kInitialPitchDeg = -20.0f;
constexpr float kInitialZoom = 1.0f;

float DegreesToRadians(float degrees) {
    return degrees * 3.14159265358979323846f / 180.0f;
}

float Norm(const Vec3& value) {
    return std::sqrt(value.x * value.x + value.y * value.y + value.z * value.z);
}

Vec3 Add(const Vec3& lhs, const Vec3& rhs) {
    return Vec3 {lhs.x + rhs.x, lhs.y + rhs.y, lhs.z + rhs.z};
}

Vec3 Sub(const Vec3& lhs, const Vec3& rhs) {
    return Vec3 {lhs.x - rhs.x, lhs.y - rhs.y, lhs.z - rhs.z};
}

Vec3 Scale(const Vec3& value, float factor) {
    return Vec3 {value.x * factor, value.y * factor, value.z * factor};
}

float Dot(const Vec3& lhs, const Vec3& rhs) {
    return lhs.x * rhs.x + lhs.y * rhs.y + lhs.z * rhs.z;
}

Vec3 Cross(const Vec3& lhs, const Vec3& rhs) {
    return Vec3 {
        lhs.y * rhs.z - lhs.z * rhs.y,
        lhs.z * rhs.x - lhs.x * rhs.z,
        lhs.x * rhs.y - lhs.y * rhs.x,
    };
}

Vec3 Normalize(const Vec3& value, const Vec3& fallback) {
    const float norm = Norm(value);
    if (norm <= 1e-6f) {
        return fallback;
    }
    return Scale(value, 1.0f / norm);
}

Vec3 Multiply(const Mat3& matrix, const Vec3& value) {
    return Vec3 {
        matrix.m[0][0] * value.x + matrix.m[0][1] * value.y + matrix.m[0][2] * value.z,
        matrix.m[1][0] * value.x + matrix.m[1][1] * value.y + matrix.m[1][2] * value.z,
        matrix.m[2][0] * value.x + matrix.m[2][1] * value.y + matrix.m[2][2] * value.z,
    };
}

Mat3 Multiply(const Mat3& lhs, const Mat3& rhs) {
    Mat3 result;
    for (int row = 0; row < 3; ++row) {
        for (int column = 0; column < 3; ++column) {
            result.m[row][column] =
                lhs.m[row][0] * rhs.m[0][column] +
                lhs.m[row][1] * rhs.m[1][column] +
                lhs.m[row][2] * rhs.m[2][column];
        }
    }
    return result;
}

Mat3 MakeRotationMatrix(float yawDeg, float pitchDeg) {
    const float yaw = DegreesToRadians(yawDeg);
    const float pitch = DegreesToRadians(pitchDeg);
    const Mat3 yawMatrix {{
        {std::cos(yaw), 0.0f, std::sin(yaw)},
        {0.0f, 1.0f, 0.0f},
        {-std::sin(yaw), 0.0f, std::cos(yaw)},
    }};
    const Mat3 pitchMatrix {{
        {1.0f, 0.0f, 0.0f},
        {0.0f, std::cos(pitch), -std::sin(pitch)},
        {0.0f, std::sin(pitch), std::cos(pitch)},
    }};
    return Multiply(pitchMatrix, yawMatrix);
}

CameraView MakeCameraView(const Vec3& target, float distance, float focalLength, int centerX, int centerY) {
    const Vec3 cameraPosition = Add(target, Vec3 {0.0f, distance * 0.45f, -distance});
    const Vec3 worldUp {0.0f, 1.0f, 0.0f};
    const Vec3 forward = Normalize(Sub(target, cameraPosition), Vec3 {0.0f, 0.0f, 1.0f});
    const Vec3 right = Normalize(Cross(worldUp, forward), Vec3 {1.0f, 0.0f, 0.0f});
    const Vec3 up = Normalize(Cross(forward, right), Vec3 {0.0f, 1.0f, 0.0f});

    CameraView camera;
    camera.position = cameraPosition;
    camera.right = right;
    camera.up = up;
    camera.forward = forward;
    camera.focalLength = focalLength;
    camera.centerX = centerX;
    camera.centerY = centerY;
    camera.distance = distance;
    return camera;
}

bool ProjectPerspective(const Vec3& point, const CameraView& camera, int* x, int* y, float* viewDepth) {
    const Vec3 delta = Sub(point, camera.position);
    const float xView = Dot(delta, camera.right);
    const float yView = Dot(delta, camera.up);
    const float zView = Dot(delta, camera.forward);
    if (zView <= kNearClipDistanceMm) {
        return false;
    }

    *x = static_cast<int>(std::lround((xView * camera.focalLength) / zView + static_cast<float>(camera.centerX)));
    *y = static_cast<int>(std::lround((-yView * camera.focalLength) / zView + static_cast<float>(camera.centerY)));
    *viewDepth = zView;
    return true;
}

DepthStats ComputeDepthStats(const ImageFrame<uint16_t>& depth) {
    DepthStats stats;
    stats.minValue = std::numeric_limits<uint16_t>::max();
    for (uint16_t value : depth.data) {
        if (value == 0) {
            continue;
        }
        stats.minValue = std::min(stats.minValue, value);
        stats.maxValue = std::max(stats.maxValue, value);
        ++stats.validCount;
    }
    if (stats.validCount == 0) {
        stats.minValue = 0;
        stats.maxValue = 0;
    }
    return stats;
}

uint8_t ClampColorChannel(float value) {
    if (value <= 0.0f) {
        return 0;
    }
    if (value >= 255.0f) {
        return 255;
    }
    return static_cast<uint8_t>(value + 0.5f);
}

Rgb DepthToRgb(uint16_t value, const DepthStats& stats) {
    Rgb color;
    if (value == 0 || stats.validCount == 0 || stats.maxValue <= stats.minValue) {
        return color;
    }

    const float normalized =
        static_cast<float>(value - stats.minValue) / static_cast<float>(stats.maxValue - stats.minValue);
    const float nearBias = 1.0f - std::clamp(normalized, 0.0f, 1.0f);
    const float r = std::clamp(1.5f - std::fabs(4.0f * nearBias - 3.0f), 0.0f, 1.0f);
    const float g = std::clamp(1.5f - std::fabs(4.0f * nearBias - 2.0f), 0.0f, 1.0f);
    const float b = std::clamp(1.5f - std::fabs(4.0f * nearBias - 1.0f), 0.0f, 1.0f);
    color.red = ClampColorChannel(r * 255.0f);
    color.green = ClampColorChannel(g * 255.0f);
    color.blue = ClampColorChannel(b * 255.0f);
    return color;
}

void SetPixel(Canvas* canvas, int x, int y, const Rgb& color) {
    if (x < 0 || y < 0 || x >= canvas->width || y >= canvas->height) {
        return;
    }
    canvas->pixels[static_cast<size_t>(y) * static_cast<size_t>(canvas->width) + static_cast<size_t>(x)] = color;
}

void DrawSquare(Canvas* canvas, int centerX, int centerY, int pointSize, const Rgb& color) {
    const int halfExtent = std::max(0, pointSize / 2);
    for (int y = centerY - halfExtent; y <= centerY + halfExtent; ++y) {
        for (int x = centerX - halfExtent; x <= centerX + halfExtent; ++x) {
            SetPixel(canvas, x, y, color);
        }
    }
}

void WriteLe16(std::ofstream* stream, uint16_t value) {
    stream->put(static_cast<char>(value & 0xFFu));
    stream->put(static_cast<char>((value >> 8u) & 0xFFu));
}

void WriteLe32(std::ofstream* stream, uint32_t value) {
    stream->put(static_cast<char>(value & 0xFFu));
    stream->put(static_cast<char>((value >> 8u) & 0xFFu));
    stream->put(static_cast<char>((value >> 16u) & 0xFFu));
    stream->put(static_cast<char>((value >> 24u) & 0xFFu));
}

bool WriteBmp(const Canvas& canvas, const std::string& path, std::string* error) {
    std::ofstream stream(path, std::ios::binary);
    if (!stream) {
        if (error != nullptr) {
            *error = "failed to open output file: " + path;
        }
        return false;
    }

    const uint32_t width = static_cast<uint32_t>(canvas.width);
    const uint32_t height = static_cast<uint32_t>(canvas.height);
    const uint32_t rowStride = width * 3u;
    const uint32_t rowPadding = (4u - (rowStride % 4u)) % 4u;
    const uint32_t pixelDataSize = (rowStride + rowPadding) * height;
    const uint32_t headerSize = 14u + 40u;
    const uint32_t fileSize = headerSize + pixelDataSize;

    stream.put('B');
    stream.put('M');
    WriteLe32(&stream, fileSize);
    WriteLe16(&stream, 0);
    WriteLe16(&stream, 0);
    WriteLe32(&stream, headerSize);

    WriteLe32(&stream, 40u);
    WriteLe32(&stream, width);
    WriteLe32(&stream, height);
    WriteLe16(&stream, 1u);
    WriteLe16(&stream, 24u);
    WriteLe32(&stream, 0u);
    WriteLe32(&stream, pixelDataSize);
    WriteLe32(&stream, 2835u);
    WriteLe32(&stream, 2835u);
    WriteLe32(&stream, 0u);
    WriteLe32(&stream, 0u);

    for (int y = canvas.height - 1; y >= 0; --y) {
        for (int x = 0; x < canvas.width; ++x) {
            const Rgb& color = canvas.pixels[static_cast<size_t>(y) * static_cast<size_t>(canvas.width) + static_cast<size_t>(x)];
            stream.put(static_cast<char>(color.blue));
            stream.put(static_cast<char>(color.green));
            stream.put(static_cast<char>(color.red));
        }
        for (uint32_t pad = 0; pad < rowPadding; ++pad) {
            stream.put(0);
        }
    }

    if (!stream) {
        if (error != nullptr) {
            *error = "failed while writing output file: " + path;
        }
        return false;
    }
    return true;
}

ImageFrame<uint16_t> BuildDepthForColoring(const FrameSet& frame) {
    if (!frame.depth.empty() &&
        frame.depth.data.size() == frame.pointCloud.points.size() &&
        frame.depth.width == frame.pointCloud.width &&
        frame.depth.height == frame.pointCloud.height) {
        return frame.depth;
    }

    ImageFrame<uint16_t> depth;
    depth.width = frame.pointCloud.width;
    depth.height = frame.pointCloud.height;
    depth.data.assign(frame.pointCloud.points.size(), 0);
    for (size_t index = 0; index < frame.pointCloud.points.size(); ++index) {
        const float z = frame.pointCloud.points[index].z;
        if (!std::isfinite(z) || z <= 0.0f) {
            continue;
        }
        const float clamped = std::clamp(z, 0.0f, static_cast<float>(std::numeric_limits<uint16_t>::max()));
        depth.data[index] = static_cast<uint16_t>(std::lround(clamped));
    }
    return depth;
}

Canvas RenderPointCloud(const FrameSet& frame, bool negateY) {
    const ImageFrame<uint16_t> depthForColoring = BuildDepthForColoring(frame);
    const DepthStats stats = ComputeDepthStats(depthForColoring);
    Canvas canvas;
    canvas.width = kCanvasWidth;
    canvas.height = kCanvasHeight;
    canvas.pixels.assign(static_cast<size_t>(canvas.width) * static_cast<size_t>(canvas.height), Rgb {16, 16, 16});

    std::vector<Vec3> points;
    std::vector<uint16_t> colors;
    points.reserve(frame.pointCloud.points.size());
    colors.reserve(frame.pointCloud.points.size());

    Vec3 centroid;
    for (size_t index = 0; index < frame.pointCloud.points.size(); ++index) {
        const uint16_t depthValue = depthForColoring.data[index];
        if (depthValue == 0) {
            continue;
        }

        const Point3f& raw = frame.pointCloud.points[index];
        if (!std::isfinite(raw.x) || !std::isfinite(raw.y) || !std::isfinite(raw.z)) {
            continue;
        }

        Vec3 point {raw.x, negateY ? -raw.y : raw.y, raw.z};
        points.push_back(point);
        colors.push_back(depthValue);
        centroid = Add(centroid, point);
    }

    if (points.empty()) {
        return canvas;
    }

    centroid = Scale(centroid, 1.0f / static_cast<float>(points.size()));
    const Mat3 rotation = MakeRotationMatrix(kInitialYawDeg, kInitialPitchDeg);
    float maxRadiusFromCloudCenter = 1.0f;
    for (const Vec3& point : points) {
        maxRadiusFromCloudCenter = std::max(maxRadiusFromCloudCenter, Norm(Sub(point, centroid)));
    }

    const int viewportWidth = kCanvasWidth - 24 - (190 + 36);
    const int viewportHeight = kCanvasHeight - 84 - 76;
    const int projectionCenterX = 24 + viewportWidth / 2;
    const int projectionCenterY = 84 + viewportHeight / 2;
    const float focalLength = static_cast<float>(std::min(viewportWidth, viewportHeight)) * 0.95f;
    const float fitHalfSpan = static_cast<float>(std::min(viewportWidth, viewportHeight)) * 0.40f;
    const float requiredTargetDistance =
        maxRadiusFromCloudCenter * (1.0f + focalLength / std::max(80.0f, fitHalfSpan)) + 120.0f;
    const float baseCameraDistance =
        std::max(kDefaultBaseCameraDistanceMm, requiredTargetDistance / kCameraTargetDistanceScale);
    const float cameraDistance = baseCameraDistance * kInitialZoom;
    const Vec3 viewTarget {};
    const CameraView camera = MakeCameraView(viewTarget, cameraDistance, focalLength, projectionCenterX, projectionCenterY);

    std::vector<ScreenPoint> screenPoints;
    screenPoints.reserve(points.size());
    for (size_t index = 0; index < points.size(); ++index) {
        const Vec3 worldPoint = Multiply(rotation, points[index]);
        int screenX = 0;
        int screenY = 0;
        float viewDepth = 0.0f;
        if (!ProjectPerspective(worldPoint, camera, &screenX, &screenY, &viewDepth)) {
            continue;
        }
        if (screenX < 0 || screenY < 0 || screenX >= canvas.width || screenY >= canvas.height) {
            continue;
        }

        screenPoints.push_back(ScreenPoint {
            screenX,
            screenY,
            viewDepth,
            DepthToRgb(colors[index], stats),
        });
    }

    std::sort(screenPoints.begin(), screenPoints.end(), [](const ScreenPoint& lhs, const ScreenPoint& rhs) {
        return lhs.depthOrder > rhs.depthOrder;
    });
    for (const ScreenPoint& point : screenPoints) {
        DrawSquare(&canvas, point.x, point.y, kPointSize, point.color);
    }

    return canvas;
}

void PrintUsage(const char* argv0) {
    std::cerr
        << "Usage: " << argv0
        << " [--uvc-device /dev/video0] [--timeout-ms 5000] [--output-raw pointcloud_raw.bmp] [--output-yneg pointcloud_yneg.bmp]\n";
}

}  // namespace

int Main(int argc, char** argv) {
    std::string device = "/dev/video0";
    std::string outputRaw = "pointcloud_raw.bmp";
    std::string outputYNeg = "pointcloud_yneg.bmp";
    int timeoutMs = 5000;

    for (int index = 1; index < argc; ++index) {
        const std::string argument = argv[index];
        if (argument == "--uvc-device" && index + 1 < argc) {
            device = argv[++index];
            continue;
        }
        if (argument == "--output-raw" && index + 1 < argc) {
            outputRaw = argv[++index];
            continue;
        }
        if (argument == "--output-yneg" && index + 1 < argc) {
            outputYNeg = argv[++index];
            continue;
        }
        if (argument == "--timeout-ms" && index + 1 < argc) {
            timeoutMs = std::max(1, std::stoi(argv[++index]));
            continue;
        }
        if (argument == "--help" || argument == "-h") {
            PrintUsage(argv[0]);
            return 0;
        }

        std::cerr << "Unknown argument: " << argument << "\n";
        PrintUsage(argv[0]);
        return 1;
    }

    Camera camera;
    CameraConfig config;
    config.transportType = TransportType::Uvc;
    config.uvc.device = device;
    config.uvc.workingProfile = UvcStreamProfile::PointCloud160x120;
    config.uvc.bootstrapCalibration = false;

    std::string error;
    if (!camera.Open(config, &error)) {
        std::cerr << "open failed: " << error << "\n";
        return 2;
    }

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    FrameSet frame;
    while (std::chrono::steady_clock::now() < deadline) {
        if (!camera.Poll(&frame, &error)) {
            std::cerr << "poll failed: " << error << "\n";
            return 3;
        }
        if (!frame.pointCloud.empty()) {
            break;
        }
    }

    if (frame.pointCloud.empty()) {
        const CameraStats stats = camera.Stats();
        std::cerr << "timed out without point cloud"
                  << " ok=" << stats.okPackets
                  << " parse_failures=" << stats.parseFailures
                  << " last_error=" << stats.lastError << "\n";
        return 4;
    }

    const Canvas rawCanvas = RenderPointCloud(frame, false);
    const Canvas yNegCanvas = RenderPointCloud(frame, true);
    if (!WriteBmp(rawCanvas, outputRaw, &error)) {
        std::cerr << error << "\n";
        return 5;
    }
    if (!WriteBmp(yNegCanvas, outputYNeg, &error)) {
        std::cerr << error << "\n";
        return 6;
    }

    std::cout << "saved_raw=" << outputRaw
              << " saved_yneg=" << outputYNeg
              << " points=" << frame.pointCloud.points.size()
              << " source=" << (frame.pointCloud.source == PointCloudSource::Direct ? "direct" : "derived")
              << "\n";
    return 0;
}

}  // namespace hm_ld1

int main(int argc, char** argv) {
    return hm_ld1::Main(argc, argv);
}
