#include "hm_ld1_sdk/hm_ld1_sdk.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <string>

namespace hm_ld1 {
namespace {

const char* TimestampUnitName(TimestampUnit unit) {
    switch (unit) {
        case TimestampUnit::Milliseconds:
            return "milliseconds";
        case TimestampUnit::Microseconds:
            return "microseconds";
        case TimestampUnit::Nanoseconds:
            return "nanoseconds";
        case TimestampUnit::Unknown:
        default:
            return "unknown";
    }
}

void PrintUsage(const char* argv0) {
    std::cerr
        << "Usage: " << argv0
        << " --serial-port /dev/ttyUSB0 [--baud 921600] [--crc-mode auto] [--count 20] [--timeout-ms 5000]\n";
}

}  // namespace

int Main(int argc, char** argv) {
    std::string serialPort;
    int baud = 921600;
    std::string crcMode = "auto";
    int count = 20;
    int timeoutMs = 5000;

    for (int index = 1; index < argc; ++index) {
        const std::string argument = argv[index];
        if (argument == "--serial-port" && index + 1 < argc) {
            serialPort = argv[++index];
            continue;
        }
        if (argument == "--baud" && index + 1 < argc) {
            baud = std::stoi(argv[++index]);
            continue;
        }
        if (argument == "--crc-mode" && index + 1 < argc) {
            crcMode = argv[++index];
            continue;
        }
        if (argument == "--count" && index + 1 < argc) {
            count = std::max(1, std::stoi(argv[++index]));
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

    if (serialPort.empty()) {
        std::cerr << "--serial-port is required\n";
        PrintUsage(argv[0]);
        return 1;
    }

    Camera camera;
    CameraConfig config;
    config.transportType = TransportType::Serial;
    config.serial.port = serialPort;
    config.serial.baud = baud;
    config.serial.crcMode = crcMode;

    std::string error;
    if (!camera.Open(config, &error)) {
        std::cerr << "open failed: " << error << "\n";
        return 2;
    }

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    uint64_t previousValue = 0;
    uint32_t previousRaw0 = 0;
    bool hasPrevious = false;
    int printed = 0;

    while (printed < count && std::chrono::steady_clock::now() < deadline) {
        FrameSet frame;
        if (!camera.Poll(&frame, &error)) {
            std::cerr << "poll failed: " << error << "\n";
            return 3;
        }
        if (frame.empty()) {
            continue;
        }

        const DeviceTimestamp& timestamp = frame.clock.device;
        const int64_t deltaValue =
            hasPrevious ? static_cast<int64_t>(timestamp.value) - static_cast<int64_t>(previousValue) : 0;
        const int64_t deltaRaw0 =
            hasPrevious ? static_cast<int64_t>(timestamp.raw0) - static_cast<int64_t>(previousRaw0) : 0;

        std::cout << "seq=" << frame.sequence
                  << " valid=" << (timestamp.valid ? 1 : 0)
                  << " unit=" << TimestampUnitName(timestamp.unit)
                  << " value=" << timestamp.value
                  << " raw0=" << timestamp.raw0
                  << " raw1=" << timestamp.raw1
                  << " d_value=" << deltaValue
                  << " d_raw0=" << deltaRaw0
                  << "\n";

        previousValue = timestamp.value;
        previousRaw0 = timestamp.raw0;
        hasPrevious = true;
        ++printed;
    }

    if (printed == 0) {
        const CameraStats stats = camera.Stats();
        std::cerr << "timed out without serial measurement"
                  << " ok=" << stats.okPackets
                  << " parse_failures=" << stats.parseFailures
                  << " crc_failures=" << stats.crcFailures
                  << " last_error=" << stats.lastError << "\n";
        return 4;
    }

    return 0;
}

}  // namespace hm_ld1

int main(int argc, char** argv) {
    return hm_ld1::Main(argc, argv);
}
