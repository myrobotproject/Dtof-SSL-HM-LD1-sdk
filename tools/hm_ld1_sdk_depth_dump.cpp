#include "hm_ld1_sdk/hm_ld1_sdk.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <limits>
#include <string>

namespace hm_ld1 {
namespace {

enum class CaptureProfile {
    Depth,
    PointCloud,
};

struct DepthStats {
    uint16_t minValue = 0;
    uint16_t maxValue = 0;
    size_t validCount = 0;
};

const char* CaptureProfileName(CaptureProfile profile) {
    switch (profile) {
        case CaptureProfile::Depth:
            return "depth";
        case CaptureProfile::PointCloud:
            return "pointcloud";
        default:
            return "unknown";
    }
}

bool ParseCaptureProfile(const std::string& value, CaptureProfile* profile) {
    if (profile == nullptr) {
        return false;
    }
    if (value == "depth") {
        *profile = CaptureProfile::Depth;
        return true;
    }
    if (value == "pointcloud") {
        *profile = CaptureProfile::PointCloud;
        return true;
    }
    return false;
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

void DepthToRgb(uint16_t value, const DepthStats& stats, uint8_t* red, uint8_t* green, uint8_t* blue) {
    if (value == 0 || stats.validCount == 0 || stats.maxValue <= stats.minValue) {
        *red = 0;
        *green = 0;
        *blue = 0;
        return;
    }

    const float normalized =
        static_cast<float>(value - stats.minValue) / static_cast<float>(stats.maxValue - stats.minValue);
    const float nearBias = 1.0f - std::clamp(normalized, 0.0f, 1.0f);
    const float r = std::clamp(1.5f - std::fabs(4.0f * nearBias - 3.0f), 0.0f, 1.0f);
    const float g = std::clamp(1.5f - std::fabs(4.0f * nearBias - 2.0f), 0.0f, 1.0f);
    const float b = std::clamp(1.5f - std::fabs(4.0f * nearBias - 1.0f), 0.0f, 1.0f);
    *red = ClampColorChannel(r * 255.0f);
    *green = ClampColorChannel(g * 255.0f);
    *blue = ClampColorChannel(b * 255.0f);
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

bool WriteBmp(const ImageFrame<uint16_t>& depth, int scale, const std::string& path, std::string* error) {
    if (scale <= 0) {
        if (error != nullptr) {
            *error = "scale must be positive";
        }
        return false;
    }
    const DepthStats stats = ComputeDepthStats(depth);
    std::ofstream stream(path, std::ios::binary);
    if (!stream) {
        if (error != nullptr) {
            *error = "failed to open output file: " + path;
        }
        return false;
    }

    const uint32_t width = depth.width * static_cast<uint32_t>(scale);
    const uint32_t height = depth.height * static_cast<uint32_t>(scale);
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

    for (int sourceY = static_cast<int>(depth.height) - 1; sourceY >= 0; --sourceY) {
        for (int repeatY = 0; repeatY < scale; ++repeatY) {
            for (uint32_t sourceX = 0; sourceX < depth.width; ++sourceX) {
                const uint16_t value =
                    depth.data[static_cast<size_t>(sourceY) * static_cast<size_t>(depth.width) + static_cast<size_t>(sourceX)];
                uint8_t red = 0;
                uint8_t green = 0;
                uint8_t blue = 0;
                DepthToRgb(value, stats, &red, &green, &blue);
                for (int repeatX = 0; repeatX < scale; ++repeatX) {
                    stream.put(static_cast<char>(blue));
                    stream.put(static_cast<char>(green));
                    stream.put(static_cast<char>(red));
                }
            }
            for (uint32_t pad = 0; pad < rowPadding; ++pad) {
                stream.put(0);
            }
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

void PrintUsage(const char* argv0) {
    std::cerr
        << "Usage: " << argv0
        << " [--profile depth|pointcloud] [--uvc-device /dev/video0] [--timeout-ms 5000] [--scale 16] [--output depth_raw.bmp]\n"
        << "Writes frame.depth from hm_ld1_sdk with no display flip or rotation.\n"
        << "Use --profile pointcloud to export depth derived from direct point-cloud frames.\n";
}

}  // namespace

int Main(int argc, char** argv) {
    CaptureProfile profile = CaptureProfile::Depth;
    std::string device = "/dev/video0";
    std::string output = "depth_raw.bmp";
    int timeoutMs = 5000;
    int scale = 1;

    for (int index = 1; index < argc; ++index) {
        const std::string argument = argv[index];
        if (argument == "--profile" && index + 1 < argc) {
            if (!ParseCaptureProfile(argv[++index], &profile)) {
                std::cerr << "Unsupported profile: " << argv[index] << "\n";
                PrintUsage(argv[0]);
                return 1;
            }
            continue;
        }
        if (argument == "--uvc-device" && index + 1 < argc) {
            device = argv[++index];
            continue;
        }
        if (argument == "--output" && index + 1 < argc) {
            output = argv[++index];
            continue;
        }
        if (argument == "--timeout-ms" && index + 1 < argc) {
            timeoutMs = std::max(1, std::stoi(argv[++index]));
            continue;
        }
        if (argument == "--scale" && index + 1 < argc) {
            scale = std::max(1, std::stoi(argv[++index]));
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
    config.uvc.workingProfile =
        profile == CaptureProfile::PointCloud ? UvcStreamProfile::PointCloud160x120 : UvcStreamProfile::Depth40x30;
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
        if (!frame.depth.empty()) {
            break;
        }
    }

    if (frame.depth.empty()) {
        const CameraStats stats = camera.Stats();
        std::cerr << "timed out without depth frame"
                  << " ok=" << stats.okPackets
                  << " parse_failures=" << stats.parseFailures
                  << " last_error=" << stats.lastError << "\n";
        return 4;
    }

    if (!WriteBmp(frame.depth, scale, output, &error)) {
        std::cerr << error << "\n";
        return 5;
    }

    const DepthStats stats = ComputeDepthStats(frame.depth);
    std::cout << "saved=" << output
              << " profile=" << CaptureProfileName(profile)
              << " scale=" << scale
              << " width=" << frame.depth.width
              << " height=" << frame.depth.height
              << " valid=" << stats.validCount << "/" << frame.depth.data.size()
              << " min=" << stats.minValue
              << " max=" << stats.maxValue
              << " note=no-display-flip\n";
    return 0;
}

}  // namespace hm_ld1

int main(int argc, char** argv) {
    return hm_ld1::Main(argc, argv);
}
