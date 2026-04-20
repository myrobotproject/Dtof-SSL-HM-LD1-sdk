# hm_ld1_sdk

[English README](README_EN.md)

`hm_ld1_sdk` 是一个面向 HM-LD1 ToF 模组的轻量级 C++17 SDK，统一封装了串口、UDP 和 UVC 三种接入方式，并将底层协议数据整理为统一的 `FrameSet` 输出。

## 特性

- 单一公共头文件：`include/hm_ld1_sdk/hm_ld1_sdk.hpp`
- 三种传输统一 API：`Serial`、`Udp`、`Uvc`
- 自动缓存设备信息与标定参数
- 在标定可用时自动补全点云或深度图
- 支持 CMake 安装、导出和 `find_package`
- Linux 下支持 UVC/V4L2 和 UDP 网卡自动补 IP

## 目录结构

- `include/hm_ld1_sdk/`：公共 API
- `src/protocol/`：协议解析
- `src/transport/`：串口、UDP、UVC 传输层
- `src/internal/`：内部数据整理与几何转换
- `cmake/`：CMake 包配置模板

## 平台支持

| 传输方式 | Windows | Linux | 说明 |
| --- | --- | --- | --- |
| Serial | 支持 | 支持 | 默认波特率 `921600` |
| Udp | 不支持 | 支持 | 依赖 IPv4 UDP socket |
| Uvc | 不支持 | 支持 | 依赖 V4L2，像素格式为 `YUYV` |

> `UdpConfig::autoConfig` 依赖原始套接字和网卡配置能力，通常需要 `root`、`CAP_NET_RAW` 或 `CAP_NET_ADMIN`。

## 构建

### 构建动态库

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

### 安装

```bash
cmake --install build --prefix /usr/local
```

安装后会导出：

- 头文件：`include/hm_ld1_sdk/hm_ld1_sdk.hpp`
- 动态库：`libhm_ld1_sdk.so` / `hm_ld1_sdk.dll`
- CMake 包：`hm_ld1_sdkConfig.cmake`

### 作为 CMake 依赖使用

```cmake
find_package(hm_ld1_sdk CONFIG REQUIRED)
target_link_libraries(your_app PRIVATE hm_ld1::sdk)
```

## 快速开始

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

运行示例：

```bash
./example uart /dev/ttyUSB0
./example udp 0.0.0.0 eth0
./example uvc /dev/video0
```

## 配置说明

### 串口

```cpp
hm_ld1::CameraConfig config;
config.transportType = hm_ld1::TransportType::Serial;
config.serial.port = "/dev/ttyUSB0";
config.serial.baud = 921600;
config.serial.crcMode = "auto";
```

`crcMode` 可选值：

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

UVC profile 含义：

- `Auto`：默认解析为 `Mixed120x90`
- `Depth40x30`：深度数据
- `Mixed120x90`：点云/深度/信息混合流
- `PointCloud160x120`：点云流
- `Raw480x360`：原始流，附带 histogram

> profile 名称表示 UVC 传输流格式；SDK 输出的测量结果仍统一为 `40x30` 深度/点云网格。

## 输出数据模型

### `FrameSet`

- `sequence`：SDK 侧递增序号
- `transportType`：当前帧来源传输方式
- `activeUvcProfile`：当前 UVC profile
- `clock`：主机单调时钟 + 设备时间戳
- `calibration`：标定参数
- `infoSnapshot`：设备信息快照
- `depth`：`ImageFrame<uint16_t>`
- `pointCloud`：`PointCloudFrame`
- `confidence`：置信度图
- `histogram`：仅原始 UVC 流可能携带

### 自动补全逻辑

- 当设备直接输出点云但未携带深度图时，SDK 会从点云 `z` 值反推深度图
- 当设备输出深度图且标定参数有效时，SDK 会自动补生成点云
- `PointCloudFrame::source` 可区分点云是设备直出还是由深度图推导

## 运行时行为

- `Open()` 成功后才会提交内部状态；失败不会遗留半初始化的缓存状态
- `Poll()` 返回 `true` 且 `frame.empty()` 时，表示本次轮询未拿到完整测量帧，不是错误
- `LatestDeviceInfo()` / `LatestCalibration()` 返回 SDK 当前缓存快照
- `Stats()` 可读取成功包数、解析失败数、CRC 错误和最近错误

## 故障排查

- `UVC transport is only supported on Linux`：当前平台不支持 V4L2
- `UDP transport is not implemented on Windows`：UDP 仅支持 Linux
- `Unexpected UDP payload size`：收到的 UDP 报文长度与协议不匹配
- `UVC device negotiated a different stream size`：设备没有按请求 profile 输出
- `Timed out while bootstrapping HM-LD1 UVC calibration`：引导标定超时，可尝试调整 `bootstrapProfile` 或 `bootstrapTimeoutMs`

## 许可证

本项目基于 MIT License 开源，详见 `LICENSE`。
