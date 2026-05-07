# hm_ld1_sdk

[English README](README.md)

`hm_ld1_sdk` 是一个面向 HM-LD1 ToF 模组的轻量级 C++17 SDK。它把串口、UDP、UVC 三种接入方式统一到同一个 `Camera` API 上，并将底层传输帧整理成一致的 `FrameSet` 输出。

## 特性

- 单一公共头文件：`include/hm_ld1_sdk/hm_ld1_sdk.hpp`
- 统一的 `Serial`、`Udp`、`Uvc` 三种 transport API
- 自动缓存设备信息和标定参数
- 在标定可用时自动补全深度图或点云
- 支持 CMake 安装、导出和 `find_package`
- 自带深度抓图、点云抓图、几何一致性检查工具

## 目录结构

- `include/hm_ld1_sdk/`：公共 API
- `src/protocol/`：协议解析和载荷解码
- `src/transport/`：串口、UDP、UVC 传输后端
- `src/internal/`：方向归一化、标定处理、几何辅助逻辑
- `tools/`：命令行诊断和抓图工具
- `cmake/`：CMake 包配置模板

## 平台支持

| 传输方式 | Windows | Linux | 说明 |
| --- | --- | --- | --- |
| Serial | 支持 | 支持 | 默认波特率为 `921600` |
| Udp | 不支持 | 支持 | 依赖 IPv4 UDP socket |
| Uvc | 不支持 | 支持 | 依赖 V4L2，采集格式为 `YUYV` |

> `UdpConfig::autoConfig` 依赖原始套接字和网卡重配置能力，通常需要 `root`、`CAP_NET_RAW` 或 `CAP_NET_ADMIN`。

## 构建

### 配置并编译

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DHM_LD1_SDK_BUILD_TOOLS=ON
cmake --build build --config Release
```

### 安装

```bash
cmake --install build --prefix /usr/local
```

安装产物包括：

- 头文件：`include/hm_ld1_sdk/hm_ld1_sdk.hpp`
- 动态库：`libhm_ld1_sdk.so` 或 `hm_ld1_sdk.dll`
- CMake 包：`hm_ld1_sdkConfig.cmake`
- 可选工具：`hm_ld1_sdk_depth_dump`、`hm_ld1_sdk_pointcloud_probe`、`hm_ld1_sdk_pointcloud_dump`

### CMake 选项

- `HM_LD1_SDK_ENABLE_WARNINGS`：开启常用编译告警
- `HM_LD1_SDK_BUILD_TOOLS`：构建命令行工具

## 自带工具

当 `HM_LD1_SDK_BUILD_TOOLS=ON` 时，会生成三个小工具：

- `hm_ld1_sdk_depth_dump`：通过 UVC `Depth40x30` 抓取 SDK 深度图并写成 BMP
- `hm_ld1_sdk_pointcloud_probe`：检查深度、点云和标定之间的几何一致性
- `hm_ld1_sdk_pointcloud_dump`：把 SDK 输出的点云渲染成两张 BMP，便于检查方向是否正确

示例命令：

```bash
./hm_ld1_sdk_depth_dump --uvc-device /dev/video0 --output depth_raw.bmp
./hm_ld1_sdk_pointcloud_probe --uvc-device /dev/video0 --timeout-ms 5000
./hm_ld1_sdk_pointcloud_dump --uvc-device /dev/video0 \
  --output-raw pointcloud_raw.bmp \
  --output-yneg pointcloud_yneg.bmp
```

这些工具主要用于 Linux/UVC 场景验证，不会在 SDK 已做方向归一化之外再额外做显示层翻转。

## 在其他 CMake 工程中使用

```cmake
find_package(hm_ld1_sdk CONFIG REQUIRED)

add_executable(example main.cpp)
target_link_libraries(example PRIVATE hm_ld1::sdk)
```

## 快速开始

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

如果要切换到串口或 UDP，只需要把 `config.transportType` 改成 `Serial` 或 `Udp`，并填写对应的 `config.serial` 或 `config.udp`。

## 传输配置

### 串口

```cpp
hm_ld1::CameraConfig config;
config.transportType = hm_ld1::TransportType::Serial;
config.serial.port = "/dev/ttyUSB0";
config.serial.baud = 921600;
config.serial.crcMode = "auto";
```

支持的 `crcMode` 取值：

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

- `Auto`：解析为 `Mixed120x90`
- `Depth40x30`：仅深度流
- `Mixed120x90`：点云、深度、信息混合流
- `PointCloud160x120`：直接点云流
- `Raw480x360`：带 histogram 的原始流

`Depth40x30` 和 `PointCloud160x120` 本身可能不携带标定。如果 `bootstrapCalibration=true`，SDK 会先短暂打开一个信息更完整的 UVC profile，把标定读出来后再进入最终工作流。

## 输出模型

### `FrameSet`

- `sequence`：SDK 侧单调递增帧号
- `transportType`：当前帧使用的传输方式
- `activeUvcProfile`：当前生效的 UVC profile
- `clock`：主机单调时钟加设备时间戳
- `calibration`：最近一次有效标定快照
- `infoSnapshot`：最近一次缓存的设备信息快照
- `depth`：`ImageFrame<uint16_t>`
- `pointCloud`：`PointCloudFrame`
- `confidence`：置信度图
- `histogram`：仅原始 UVC 流可能携带

### 自动补全逻辑

- 当设备直接输出点云但没有深度图时，SDK 会根据 `z` 反推深度
- 当设备输出深度图且标定有效时，SDK 会自动生成点云
- `PointCloudFrame::source` 可区分点云是设备直出还是由深度推导

### 方向与坐标约定

- `depth`、`pointCloud`、`confidence`、`histogram` 和 `calibration` 会在对外暴露前由 SDK 统一做水平归一化
- `pointCloud[index]`、`depth.data[index]`、`confidence.values[index]` 始终对应公开 `40x30` 网格中的同一个像素
- SDK 会同步修正镜像相关标定项（`cx`、`p2`）和设备直出点云的 `x` 坐标，保证重投影关系在归一化后仍然一致
- SDK 不会对点云 `y` 轴做取反；显示层是否使用 Y-up 或 Y-down 由上层应用决定

## 运行时说明

- `Open()` 只有在目标 transport 真正打开成功后才会提交内部状态
- `Poll()` 返回 `true` 且 `frame.empty()` 时，表示暂时还没有拿到完整测量帧，不表示错误
- `LatestDeviceInfo()` 和 `LatestCalibration()` 可读取 SDK 当前缓存快照
- `Stats()` 会返回包计数、解析失败、CRC 失败、丢弃字节数和最近一次错误字符串

## 故障排查

- `UVC transport is only supported on Linux`：当前平台不提供 V4L2
- `UDP transport is not implemented on Windows`：UDP 传输当前仅支持 Linux
- `Unexpected UDP payload size`：收到的 UDP 报文长度与 HM-LD1 协议帧不匹配
- `UVC device negotiated a different stream size`：设备没有接受请求的 profile 分辨率
- `Timed out while bootstrapping HM-LD1 UVC calibration`：引导标定超时，可尝试更换 `bootstrapProfile` 或增大超时
- `Unsupported serial crcMode ...`：串口 `crcMode` 配置值不在文档支持列表内

## 许可

本项目采用 MIT License，详见 `LICENSE`。
