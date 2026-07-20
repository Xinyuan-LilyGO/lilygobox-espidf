<h1 align="center">lilygobox-espidf</h1>

## **English | [Chinese](./README_CN.md)**

[![Release](https://img.shields.io/github/v/release/Xinyuan-LilyGO/lilygobox-espidf?style=flat-square)](https://github.com/Xinyuan-LilyGO/lilygobox-espidf/releases)
[![License](https://img.shields.io/github/license/Xinyuan-LilyGO/lilygobox-espidf?style=flat-square)](./LICENSE)
[![ESP-IDF](https://img.shields.io/badge/ESP--IDF-v5.5.4%2B-ff6f00?style=flat-square)](https://github.com/espressif/esp-idf)

**lilygobox-espidf** is a standalone ESP-IDF application for LILYGO smart-display devices. It provides a touch-oriented LVGL interface and uses `lilygo_device_driver` to access the device hardware.

## Table of Contents

- [Supported Devices](#supported-devices)
- [Quick Start](#quick-start)

## Supported Devices

| Device | SoC | Status | Description |
| --- | --- | --- | --- |
| T-Display-P4 | ESP32-P4 | Supported | Current default and primary target |

## Quick Start

This repository is distributed as a complete ESP-IDF application. Clone, build, and flash it directly rather than adding it to another project as a component.

### Requirement

- ESP-IDF v5.5.4 or later

### Clone the Application

```bash
git clone --recursive https://github.com/Xinyuan-LilyGO/lilygobox-espidf.git
```

If the repository has already been cloned without its submodules, run:

```bash
git submodule update --init --recursive
```

### Configure

Open an ESP-IDF terminal in the project directory, then select the ESP32-P4 target:

```bash
idf.py set-target esp32p4
```

The default configuration already selects T-Display-P4. To review the device, camera, screen color, or log settings, run:

```bash
idf.py menuconfig
```

Device-related options are under `lilygo_device_driver configuration`, and application log options are under `LilygoBox Configuration`.

### Build

```bash
idf.py build
```

### Flash and Monitor

```bash
idf.py -p COMx flash monitor
```

Replace `COMx` with the serial port connected to the board. On Linux or macOS, use the corresponding device path, such as `/dev/ttyUSB0`.
