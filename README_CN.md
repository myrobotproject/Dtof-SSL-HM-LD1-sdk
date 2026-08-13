# hm_ld1_sdk

[English README](README.md)

`hm_ld1_sdk` 是一个面向 HM-LD1 ToF 模组的轻量级 C++17 SDK。它把串口、UDP、UVC 三种接入方式统一到同一个 `Camera` API 上，并将底层传输帧整理成一致的 `FrameSet` 输出。

## 特性

- 单一公共头文件：`include/hm_ld1_sdk/hm_ld1_sdk.hpp`
- 统一的 `Serial`、`Udp`、`Uvc` 三种 transport API
- 自动缓存设备信息和标定参数
- 在标定可用时自动补全深度图或点云
- 支持 CMake 安装、导出和 `find_package`

## 目录结构

- `include/hm_ld1_sdk/`：公共 API
- `src/protocol/`：协议解析和载荷解码
- `src/transport/`：串口、UDP、UVC 传输后端
- `src/internal/`：帧组装、标定处理、几何辅助逻辑
- `cmake/`：CMake 包配置模板

## 平台支持

| 传输方式   | Windows | Linux | 说明                   |
| ------ | ------- | ----- | -------------------- |
| Serial | 支持      | 支持    | 默认波特率为 `921600`      |
| Udp    | 不支持     | 支持    | 依赖 IPv4 UDP socket   |
| Uvc    | 不支持     | 支持    | 依赖 V4L2，采集格式为 `YUYV` |

> `UdpConfig::autoConfig` 依赖原始套接字和网卡重配置能力，通常需要 `root`、`CAP_NET_RAW` 或 `CAP_NET_ADMIN`。

## 构建

### 配置并编译

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
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

### CMake 选项

- `HM_LD1_SDK_ENABLE_WARNINGS`：开启常用编译告警

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

### 深度采样值

- SDK 不会过滤、丢弃或重映射 `FrameSet::depth.data` 中的设备深度采样值；传输帧中携带的每个采样都会输出给应用。
- 深度值 `0`、`1`、`2` 是设备端滤波标记值，不是普通测距值。
- `0`：飞点滤波滤除或标记的采样点。
- `1`：连通域滤波滤除或标记的采样点。
- `2`：探测概率滤波滤除或标记的采样点，常见原因包括目标超出量程，或深色/低反射物体导致回波信号弱。
- 设备直出点云在 `Point3f::z` 中使用同样的标记值；`z` 为 `0`、`1`、`2` 时不是有效测距值。
- SDK 由深度图生成点云时，会在同一网格 index 的 `Point3f::z` 中保留深度标记值 `0`、`1`、`2`，这些标记采样的 `x` 和 `y` 置为 `0`。
- 应用可以把 `z > 2` 的点云采样视为有效测距 3D 点。

### 设备时间戳

- 所有有效的 `clock.device.value` 都统一输出为 Unix 纪元微秒，`TimestampUnit::Microseconds` 对所有传输方式一致。
- 串口在 PPS 有效时使用 PPS 对齐后的主机时间；没有 PPS 时，以主机系统时间锚定设备递增计数。这样保持同一时间域，但无 PPS 时属于估算值，不代表设备时钟已同步。
- UDP 只有在打开后的第一帧有效数据构成且与主机系统时间相差不超过 5 秒的 Unix 纪元时间时才直接使用该时间域；同一次打开期间不会切换时间域，后续非法或回退字段会被单调钳制。因此未同步或过期的设备时钟会使用主机锚定的相对时间估算。
- UVC 的毫秒计数以主机时间为锚点，并处理 32 位回绕；没有设备时间戳字段的 UVC profile 使用帧接收时的主机系统时间。
- `clock.device.raw0` 和 `clock.device.raw1` 保留协议原始字段及其原生单位，便于诊断；`hostMonotonicTimeNs` 仍是独立的主机单调时钟值。

### 自动补全逻辑

- 当设备直接输出点云但没有深度图时，SDK 会根据 `z` 反推深度
- 当设备输出深度图且标定有效时，SDK 会自动生成点云，并在 `Point3f::z` 中保留设备标记深度 `0`、`1`、`2`
- `PointCloudFrame::source` 可区分点云是设备直出还是由深度推导

### 数据对齐与坐标约定

- `depth`、`pointCloud`、`confidence`、`histogram` 和 `calibration` 在 `FrameSet` 中使用同一套公开帧布局
- 当这些帧同时存在时，`pointCloud[index]`、`depth.data[index]` 和 `confidence.values[index]` 对应公开 `40x30` 网格中的同一个采样
- `PointCloudFrame::source` 可区分点云是设备直出还是由深度推导
- 点云坐标约定为 `x` 向右、`y` 向下、`z` 向前

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
