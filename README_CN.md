<h1 align="center">lilygobox-espidf</h1>

## **[英文](./README.md) | 中文**

[![Release](https://img.shields.io/github/v/release/Xinyuan-LilyGO/lilygobox-espidf?style=flat-square)](https://github.com/Xinyuan-LilyGO/lilygobox-espidf/releases)
[![License](https://img.shields.io/github/license/Xinyuan-LilyGO/lilygobox-espidf?style=flat-square)](./LICENSE)
[![ESP-IDF](https://img.shields.io/badge/ESP--IDF-v5.5.4%2B-ff6f00?style=flat-square)](https://github.com/espressif/esp-idf)

**lilygobox-espidf** 是一个面向 LILYGO 智能显示设备的独立 ESP-IDF 应用。它提供适合触控操作的 LVGL 界面，并通过 `lilygo_device_driver` 访问设备硬件。

## 目录

- [支持的设备](#支持的设备)
- [快速开始](#快速开始)

## 支持的设备

| 设备 | 主控 | 无线协处理器 | 状态 | 说明 |
| --- | --- | --- | --- | --- |
| T-Display-P4-Air | ESP32-P4 | ESP32-C5 | 支持 | 当前默认配置；使用独立的 Air 硬件驱动 |
| T-Display-P4 | ESP32-P4 | ESP32-C6 | 支持 | 原版硬件；保留独立的原版驱动 |

T-Display-P4-Air 与 T-Display-P4 是两个不同的设备。两者的硬件驱动分别
放在独立目录中，不应同时选中：Air 使用
`main/hal/device/t_display_p4_air`，原版继续使用
`main/hal/device/t_display_p4`。

## 快速开始

本仓库以完整 ESP-IDF 应用的形式发布，请直接克隆、编译并烧录，无需把它作为 component 添加到其他工程。

### 环境要求

- ESP-IDF v5.5.4 或更高版本

### 克隆应用

```bash
git clone --recursive https://github.com/Xinyuan-LilyGO/lilygobox-espidf.git
```

如果克隆仓库时没有初始化子模块，请执行：

```bash
git submodule update --init --recursive
```

### 配置

在项目目录中打开 ESP-IDF 终端，然后选择 ESP32-P4 目标：

```bash
idf.py set-target esp32p4
```

默认配置已经选择 T-Display-P4-Air 和 ESP32-C5 无线协处理器。如需检查
设备、相机、屏幕色彩格式、无线协处理器或日志配置，请执行：

```bash
idf.py menuconfig
```

设备相关选项位于 `lilygo_device_driver configuration`，应用日志选项位于 `LilygoBox Configuration`。这些选项用于选择启动默认值，应用可以在运行时通过各自线程安全的 `SetMinimumLogLevel()` 接口动态调整最低日志等级。

切换到原版 T-Display-P4 时，需要选择其独立设备选项，并把 ESP-Hosted
从机目标改为 ESP32-C6；切换回 T-Display-P4-Air 时，则需要同时选择 Air
设备选项和 ESP32-C5。工程会在构建时拒绝不匹配的配置，避免为错误的无线
协处理器生成固件。

### 编译

```bash
idf.py build
```

### 烧录和监视

```bash
idf.py -p COMx flash monitor
```

请把 `COMx` 替换为开发板对应的串口。在 Linux 或 macOS 上，请使用相应的设备路径，例如 `/dev/ttyUSB0`。
