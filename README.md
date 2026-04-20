# Dtof-SSL-HM-LD1-sdk


`hm_ld1_sdk` is a lightweight C++17 SDK for HM-LD1 ToF modules. It provides one unified API over serial, UDP, and UVC transports, and converts device-specific packets into a consistent `FrameSet`.

## Features

- Single public header: `include/hm_ld1_sdk/hm_ld1_sdk.hpp`
- Unified API for `Serial`, `Udp`, and `Uvc`
- Automatic caching of device info and calibration
- Automatic depth/point-cloud completion when calibration is available
- CMake install/export support with `find_package`
- Linux support for UVC/V4L2 and UDP interface auto-configuration

## Repository Layout

- `include/hm_ld1_sdk/`: public API
- `src/protocol/`: protocol parsing
- `src/transport/`: serial, UDP, and UVC transports
- `src/internal/`: internal frame normalization and geometry helpers
- `cmake/`: CMake package config template

## Platform Support

| Transport | Windows | Linux | Notes |
| --- | --- | --- | --- |
| Serial | Yes | Yes | Default baud rate is `921600` |
| Udp | No | Yes | Requires IPv4 UDP sockets |
| Uvc | No | Yes | Requires V4L2 with `YUYV` capture |

> `UdpConfig::autoConfig` relies on raw sockets and interface reconfiguration, which usually requires `root`, `CAP_NET_RAW`, or `CAP_NET_ADMIN`.

## Build

### Build the shared library

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

### Install

```bash
cmake --install build --prefix /usr/local
```

Installed artifacts include:

- Header: `include/hm_ld1_sdk/hm_ld1_sdk.hpp`
- Shared library: `libhm_ld1_sdk.so` / `hm_ld1_sdk.dll`
- CMake package: `hm_ld1_sdkConfig.cmake`

### Use from another CMake project

```cmake
find_package(hm_ld1_sdk CONFIG REQUIRED)
target_link_libraries(your_app PRIVATE hm_ld1::sdk)
```

## Quick Start

```cpp
#include <hm_ld1_sdk/hm_ld1_sdk.hpp>

#include <iostream>
#include <optional>
#include <string>

enum class SelectedTransport {
    Uart,
    Udp,
    Uvc,
};

std::optional<SelectedTransport> ParseTransport(const std::string& name) {
    if (name == "uart" || name == "serial") {
        return SelectedTransport::Uart;
    }
    if (name == "udp") {
        return SelectedTransport::Udp;
    }
    if (name == "uvc") {
        return SelectedTransport::Uvc;
    }
    return std::nullopt;
}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "usage: " << argv[0] << " <uart|udp|uvc> [device-or-address] [udp-interface]\n";
        return 1;
    }

    const std::optional<SelectedTransport> selected = ParseTransport(argv[1]);
    if (!selected.has_value()) {
        std::cerr << "unsupported transport: " << argv[1] << '\n';
        return 1;
    }

    hm_ld1::Camera camera;
    hm_ld1::CameraConfig config;

    switch (*selected) {
        case SelectedTransport::Uart:
            config.transportType = hm_ld1::TransportType::Serial;
            config.serial.port = argc > 2 ? argv[2] : "/dev/ttyUSB0";
            config.serial.baud = 921600;
            config.serial.crcMode = "auto";
            break;
        case SelectedTransport::Udp:
            config.transportType = hm_ld1::TransportType::Udp;
            config.udp.bindAddress = argc > 2 ? argv[2] : "0.0.0.0";
            config.udp.port = 2368;
            config.udp.interfaceName = argc > 3 ? argv[3] : "";
            break;
        case SelectedTransport::Uvc:
            config.transportType = hm_ld1::TransportType::Uvc;
            config.uvc.device = argc > 2 ? argv[2] : "/dev/video0";
            config.uvc.workingProfile = hm_ld1::UvcStreamProfile::Auto;
            break;
    }

    std::string error;
    if (!camera.Open(config, &error)) {
        std::cerr << "open failed: " << error << '\n';
        return 1;
    }

    for (;;) {
        hm_ld1::FrameSet frame;
        if (!camera.Poll(&frame, &error)) {
            std::cerr << "poll failed: " << error << '\n';
            break;
        }
        if (frame.empty()) {
            continue;
        }

        std::cout << "seq=" << frame.sequence
                  << " depth=" << frame.depth.width << "x" << frame.depth.height
                  << " points=" << frame.pointCloud.size()
                  << " confidence=" << frame.confidence.values.size()
                  << '\n';
    }

    camera.Close();
    return 0;
}
```

Run examples:

```bash
./example uart /dev/ttyUSB0
./example udp 0.0.0.0 eth0
./example uvc /dev/video0
```

## Configuration

### Serial

```cpp
hm_ld1::CameraConfig config;
config.transportType = hm_ld1::TransportType::Serial;
config.serial.port = "/dev/ttyUSB0";
config.serial.baud = 921600;
config.serial.crcMode = "auto";
```

Supported `crcMode` values:

- `auto`
- `none`
- `crc8`
- `crc8_itu`
- `maxim`
- `rohc`

### UDP

```cpp
hm_ld1::CameraConfig config;
config.transportType = hm_ld1::TransportType::Udp;
config.udp.bindAddress = "0.0.0.0";
config.udp.port = 2368;
config.udp.interfaceName = "eth0";
config.udp.autoConfig = false;
config.udp.autoConfigTimeoutMs = 5000;
```

### UVC

```cpp
hm_ld1::CameraConfig config;
config.transportType = hm_ld1::TransportType::Uvc;
config.uvc.device = "/dev/video0";
config.uvc.workingProfile = hm_ld1::UvcStreamProfile::Auto;
config.uvc.bootstrapCalibration = true;
config.uvc.bootstrapProfile = hm_ld1::UvcStreamProfile::Mixed120x90;
config.uvc.bootstrapTimeoutMs = 1500;
```

UVC profile meanings:

- `Auto`: resolves to `Mixed120x90`
- `Depth40x30`: depth-only stream
- `Mixed120x90`: mixed point-cloud / depth / info stream
- `PointCloud160x120`: point-cloud stream
- `Raw480x360`: raw stream with histogram

> Profile names describe the UVC transport format. The normalized measurement output from the SDK still uses a `40x30` depth/point-cloud grid.

## Output Model

### `FrameSet`

- `sequence`: SDK-side monotonically increasing frame id
- `transportType`: transport used by the current frame
- `activeUvcProfile`: active UVC profile
- `clock`: host monotonic clock and device timestamp
- `calibration`: camera intrinsics/distortion
- `infoSnapshot`: cached device info snapshot
- `depth`: `ImageFrame<uint16_t>`
- `pointCloud`: `PointCloudFrame`
- `confidence`: confidence map
- `histogram`: only present for raw UVC streams

### Automatic completion

- If the device provides a point cloud without a depth frame, the SDK reconstructs depth from `z`
- If the device provides depth and valid calibration, the SDK derives a point cloud automatically
- `PointCloudFrame::source` tells whether the point cloud is direct device output or derived from depth

## Runtime Notes

- `Open()` only commits internal state after the transport is fully opened
- `Poll()` returning `true` with `frame.empty()` means no complete measurement was available yet, not an error
- `LatestDeviceInfo()` and `LatestCalibration()` expose the latest cached snapshots
- `Stats()` reports successful packets, parse failures, CRC failures, and the last error string

## Troubleshooting

- `UVC transport is only supported on Linux`: current platform does not provide V4L2 support
- `UDP transport is not implemented on Windows`: UDP transport is Linux-only
- `Unexpected UDP payload size`: the incoming datagram does not match the expected protocol size
- `UVC device negotiated a different stream size`: the device did not accept the requested profile dimensions
- `Timed out while bootstrapping HM-LD1 UVC calibration`: calibration bootstrap timed out; try another `bootstrapProfile` or a longer timeout

## License

This project is released under the MIT License. See `LICENSE` for details.
