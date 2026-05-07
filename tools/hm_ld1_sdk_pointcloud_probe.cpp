#include "hm_ld1_sdk/hm_ld1_sdk.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

namespace hm_ld1 {
namespace {

struct ErrorStats {
    size_t count = 0;
    double mean = 0.0;
    double median = 0.0;
    double p90 = 0.0;
};

ErrorStats Summarize(std::vector<double> values) {
    ErrorStats stats;
    stats.count = values.size();
    if (values.empty()) {
        return stats;
    }

    double sum = 0.0;
    for (double value : values) {
        sum += value;
    }
    std::sort(values.begin(), values.end());
    stats.mean = sum / static_cast<double>(values.size());
    stats.median = values[values.size() / 2];
    stats.p90 = values[static_cast<size_t>(std::floor(0.90 * static_cast<double>(values.size() - 1)))];
    return stats;
}

void PrintStats(const std::string& name, const ErrorStats& stats, const std::string& unit) {
    std::cout << name
              << " count=" << stats.count
              << " mean=" << stats.mean << unit
              << " median=" << stats.median << unit
              << " p90=" << stats.p90 << unit
              << "\n";
}

bool Project(const CameraCalibration& c, const Point3f& p, bool flipX, bool flipY, double* u, double* v) {
    if (p.z <= 0.0f || !std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z)) {
        return false;
    }

    double x = static_cast<double>(p.x) / static_cast<double>(p.z);
    double y = static_cast<double>(p.y) / static_cast<double>(p.z);
    if (flipX) {
        x = -x;
    }
    if (flipY) {
        y = -y;
    }

    const double r2 = x * x + y * y;
    const double r4 = r2 * r2;
    const double r6 = r4 * r2;
    const double radialNumerator = 1.0 + c.k1 * r2 + c.k2 * r4 + c.k3 * r6;
    const double radialDenominator = 1.0 + c.k4 * r2 + c.k5 * r4 + c.k6 * r6;
    if (std::abs(radialDenominator) < 1e-12) {
        return false;
    }

    const double radial = radialNumerator / radialDenominator;
    const double deltaX = 2.0 * c.p1 * x * y + c.p2 * (r2 + 2.0 * x * x);
    const double deltaY = c.p1 * (r2 + 2.0 * y * y) + 2.0 * c.p2 * x * y;
    *u = c.fx * (x * radial + deltaX) + c.cx;
    *v = c.fy * (y * radial + deltaY) + c.cy;
    return std::isfinite(*u) && std::isfinite(*v);
}

void PrintUsage(const char* argv0) {
    std::cerr << "Usage: " << argv0 << " [--uvc-device /dev/video0] [--timeout-ms 5000]\n";
}

}  // namespace

int Main(int argc, char** argv) {
    std::string device = "/dev/video0";
    int timeoutMs = 5000;

    for (int index = 1; index < argc; ++index) {
        const std::string argument = argv[index];
        if (argument == "--uvc-device" && index + 1 < argc) {
            device = argv[++index];
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
    config.uvc.workingProfile = UvcStreamProfile::Mixed120x90;

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
        if (!frame.depth.empty() && !frame.pointCloud.empty() && frame.calibration.valid) {
            break;
        }
    }

    if (frame.depth.empty() || frame.pointCloud.empty() || !frame.calibration.valid) {
        const CameraStats stats = camera.Stats();
        std::cerr << "timed out without comparable frame"
                  << " ok=" << stats.okPackets
                  << " parse_failures=" << stats.parseFailures
                  << " last_error=" << stats.lastError << "\n";
        return 4;
    }

    std::vector<double> depthSame;
    std::vector<double> depthXFlip;
    std::vector<double> depthYFlip;
    std::vector<double> depthXYFlip;
    std::vector<double> projSame;
    std::vector<double> projXNeg;
    std::vector<double> projYNeg;
    std::vector<double> projXYNeg;

    const uint32_t width = frame.depth.width;
    const uint32_t height = frame.depth.height;
    for (uint32_t y = 0; y < height; ++y) {
        for (uint32_t x = 0; x < width; ++x) {
            const size_t index = static_cast<size_t>(y) * width + x;
            const Point3f& point = frame.pointCloud.points[index];
            if (!std::isfinite(point.z) || point.z <= 0.0f) {
                continue;
            }

            const auto addDepthError = [&](uint32_t dx, uint32_t dy, std::vector<double>* out) {
                const uint16_t depthValue = frame.depth.data[static_cast<size_t>(dy) * width + dx];
                if (depthValue > 0) {
                    out->push_back(std::abs(static_cast<double>(point.z) - static_cast<double>(depthValue)));
                }
            };
            addDepthError(x, y, &depthSame);
            addDepthError(width - 1 - x, y, &depthXFlip);
            addDepthError(x, height - 1 - y, &depthYFlip);
            addDepthError(width - 1 - x, height - 1 - y, &depthXYFlip);

            const auto addProjectionError = [&](bool flipX, bool flipY, std::vector<double>* out) {
                double u = 0.0;
                double v = 0.0;
                if (Project(frame.calibration, point, flipX, flipY, &u, &v)) {
                    const double du = u - static_cast<double>(x);
                    const double dv = v - static_cast<double>(y);
                    out->push_back(std::sqrt(du * du + dv * dv));
                }
            };
            addProjectionError(false, false, &projSame);
            addProjectionError(true, false, &projXNeg);
            addProjectionError(false, true, &projYNeg);
            addProjectionError(true, true, &projXYNeg);
        }
    }

    std::cout << "frame seq=" << frame.sequence
              << " depth=" << width << "x" << height
              << " point_count=" << frame.pointCloud.points.size()
              << " point_source=" << (frame.pointCloud.source == PointCloudSource::Direct ? "direct" : "derived")
              << "\n";

    std::cout << "depth-z error:\n";
    PrintStats("same", Summarize(depthSame), "mm");
    PrintStats("x-flip", Summarize(depthXFlip), "mm");
    PrintStats("y-flip", Summarize(depthYFlip), "mm");
    PrintStats("xy-flip", Summarize(depthXYFlip), "mm");

    std::cout << "projection error:\n";
    PrintStats("same", Summarize(projSame), "px");
    PrintStats("x-neg", Summarize(projXNeg), "px");
    PrintStats("y-neg", Summarize(projYNeg), "px");
    PrintStats("xy-neg", Summarize(projXYNeg), "px");
    return 0;
}

}  // namespace hm_ld1

int main(int argc, char** argv) {
    return hm_ld1::Main(argc, argv);
}
