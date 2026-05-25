# hm_ld1_sdk


`hm_ld1_sdk` is a lightweight C++17 SDK for HM-LD1 ToF modules. It exposes one `Camera` API across serial, UDP, and UVC transports and normalizes transport-specific packets into a consistent `FrameSet`.

## Highlights

- Single public header: `include/hm_ld1_sdk/hm_ld1_sdk.hpp`
- Unified API for `Serial`, `Udp`, and `Uvc`
- Automatic caching of device info and calibration
- Automatic depth or point-cloud completion when calibration is available
- CMake install/export support with `find_package`
- Small diagnostic tools for depth dumps, point-cloud dumps, timestamp checks, and geometry checks

## Repository Layout

- `include/hm_ld1_sdk/`: public API
- `src/protocol/`: frame parsing and payload decoding
- `src/transport/`: serial, UDP, and UVC transport backends
- `src/internal/`: frame assembly, calibration handling, and geometry helpers
- `tools/`: command-line diagnostics and capture utilities
- `cmake/`: package config template

## Platform Support

| Transport | Windows | Linux | Notes |
| --- | --- | --- | --- |
| Serial | Yes | Yes | Default baud rate is `921600` |
| Udp | No | Yes | Requires IPv4 UDP sockets |
| Uvc | No | Yes | Requires V4L2 with `YUYV` capture |

> `UdpConfig::autoConfig` uses raw sockets and interface reconfiguration. It usually requires `root`, `CAP_NET_RAW`, or `CAP_NET_ADMIN`.

## Build

### Configure and build

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DHM_LD1_SDK_BUILD_TOOLS=ON
cmake --build build --config Release
```

### Install

```bash
cmake --install build --prefix /usr/local
```

Installed artifacts include:

- Header: `include/hm_ld1_sdk/hm_ld1_sdk.hpp`
- Shared library: `libhm_ld1_sdk.so` or `hm_ld1_sdk.dll`
- CMake package: `hm_ld1_sdkConfig.cmake`
- Optional tools: `hm_ld1_sdk_depth_dump`, `hm_ld1_sdk_timestamp_probe`, `hm_ld1_sdk_pointcloud_probe`, `hm_ld1_sdk_pointcloud_dump`

### CMake options

- `HM_LD1_SDK_ENABLE_WARNINGS`: enable common compiler warnings
- `HM_LD1_SDK_BUILD_TOOLS`: build the command-line tools

## Included Tools

When `HM_LD1_SDK_BUILD_TOOLS=ON`, the project builds four small utilities:

- `hm_ld1_sdk_depth_dump`: opens the sensor through UVC `Depth40x30` and writes the SDK depth frame to a BMP file
- `hm_ld1_sdk_timestamp_probe`: prints serial SDK timestamp fields for PPS and device-counter checks
- `hm_ld1_sdk_pointcloud_probe`: checks whether depth, point cloud, and calibration agree geometrically
- `hm_ld1_sdk_pointcloud_dump`: renders direct or depth-derived SDK point-cloud output into BMP snapshots for visual inspection

Example commands:

```bash
./hm_ld1_sdk_depth_dump --uvc-device /dev/video0 --output depth_raw.bmp
./hm_ld1_sdk_timestamp_probe --serial-port /dev/ttyUSB0 --count 50
./hm_ld1_sdk_pointcloud_probe --uvc-device /dev/video0 --timeout-ms 5000
./hm_ld1_sdk_pointcloud_dump --uvc-device /dev/video0 \
  --profile pointcloud \
  --output-raw pointcloud_raw.bmp \
  --output-yneg pointcloud_yneg.bmp
./hm_ld1_sdk_pointcloud_dump --uvc-device /dev/video0 \
  --profile depth \
  --output-raw pointcloud_from_depth_raw.bmp \
  --output-yneg pointcloud_from_depth_yneg.bmp
```

The UVC tools write SDK outputs in a format that is easy to inspect. The timestamp probe is intended for serial validation.

## Use From Another CMake Project

```cmake
find_package(hm_ld1_sdk CONFIG REQUIRED)

add_executable(example main.cpp)
target_link_libraries(example PRIVATE hm_ld1::sdk)
```

## Quick Start

```cpp
#include <hm_ld1_sdk/hm_ld1_sdk.hpp>

#include <iostream>
#include <string>

int main() {
    hm_ld1::Camera camera;
    hm_ld1::CameraConfig config;
    config.transportType = hm_ld1::TransportType::Uvc;
    config.uvc.device = "/dev/video0";
    config.uvc.workingProfile = hm_ld1::UvcStreamProfile::Mixed120x90;

    std::string error;
    if (!camera.Open(config, &error)) {
        std::cerr << "open failed: " << error << '\n';
        return 1;
    }

    for (;;) {
        hm_ld1::FrameSet frame;
        if (!camera.Poll(&frame, &error)) {
            std::cerr << "poll failed: " << error << '\n';
            return 2;
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
}
```

To switch transports, set `config.transportType` to `Serial` or `Udp` and fill `config.serial` or `config.udp`.

## Transport Configuration

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
- `Mixed120x90`: mixed point-cloud, depth, and info stream
- `PointCloud160x120`: direct point-cloud stream
- `Raw480x360`: raw stream with histogram

`Depth40x30` and `PointCloud160x120` may not carry calibration by themselves. When `bootstrapCalibration=true`, the SDK briefly opens a richer UVC profile first to cache calibration before the final stream starts.

## Output Model

### `FrameSet`

- `sequence`: SDK-side monotonically increasing frame id
- `transportType`: transport used by the current frame
- `activeUvcProfile`: active UVC profile
- `clock`: host monotonic clock plus device timestamp
- `calibration`: latest valid calibration snapshot
- `infoSnapshot`: latest cached device info snapshot
- `depth`: `ImageFrame<uint16_t>`
- `pointCloud`: `PointCloudFrame`
- `confidence`: confidence map
- `histogram`: only present for raw UVC streams

### Device timestamps

- Serial timestamps are exposed in `clock.device.value` as microseconds.
- When a valid PPS signal is detected on serial, `clock.device.value` is converted to a host-system-time based timestamp.
- Without a valid PPS signal, serial uses the device's increasing timestamp converted from milliseconds to microseconds.
- `clock.device.raw0` keeps the raw serial `TimeStamp` field for diagnostics.
- UDP and UVC timestamp units follow the corresponding transport packet definitions.

### Automatic completion

- If the device provides point cloud without depth, the SDK reconstructs depth from `z`
- If the device provides depth and valid calibration, the SDK derives point cloud automatically
- `PointCloudFrame::source` tells whether the point cloud is direct device output or derived from depth

### Data alignment and coordinates

- `depth`, `pointCloud`, `confidence`, `histogram`, and `calibration` use the same public frame layout when present in `FrameSet`
- `pointCloud[index]`, `depth.data[index]`, and `confidence.values[index]` refer to the same public `40x30` grid sample when those frames are present
- `PointCloudFrame::source` indicates whether a point cloud is direct device output or derived from depth
- Point-cloud coordinates use `x` right, `y` down, and `z` forward

## Runtime Notes

- `Open()` only commits internal state after the selected transport opens successfully
- `Poll()` returning `true` with `frame.empty()` means no complete measurement is ready yet, not an error
- `LatestDeviceInfo()` and `LatestCalibration()` expose the latest cached snapshots
- `Stats()` reports packet counts, parse failures, CRC failures, discarded bytes, and the last error string

## Troubleshooting

- `UVC transport is only supported on Linux`: current platform does not provide V4L2 support
- `UDP transport is not implemented on Windows`: UDP transport is Linux-only
- `Unexpected UDP payload size`: the incoming datagram does not match the HM-LD1 UDP frame size
- `UVC device negotiated a different stream size`: the device did not accept the requested profile dimensions
- `Timed out while bootstrapping HM-LD1 UVC calibration`: bootstrap did not receive calibration in time; try another `bootstrapProfile` or a longer timeout
- `Unsupported serial crcMode ...`: the configured serial CRC mode is not one of the documented values

## License

This project is released under the MIT License. See `LICENSE` for details.
