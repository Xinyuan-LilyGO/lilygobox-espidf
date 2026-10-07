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

| Device | Main SoC | Wireless Co-processor | Status | Description |
| --- | --- | --- | --- | --- |
| T-Display-P4 V1 | ESP32-P4 | ESP32-C6 | Supported | Repository default device and version |
| T-Display-P4 V2 | ESP32-P4 | ESP32-C5 | Supported | Select V2 in the device version options |
| T-Display-P4-Air | ESP32-P4 | ESP32-C5 | Supported | Independent Air hardware driver |

T-Display-P4-Air and T-Display-P4 are different devices. Their hardware
drivers are kept in separate directories and must not be selected at the same
time: Air uses `main/hal/device/t_display_p4_air`, while the original board
continues to use `main/hal/device/t_display_p4`.

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

The repository defaults to T-Display-P4 V1 with the ESP32-C6 wireless
co-processor. To review the device, camera, screen color, wireless
co-processor, or log settings, run:

```bash
idf.py menuconfig
```

Device-related options are under `lilygo_device_driver configuration`, and application log options are under `LilygoBox Configuration`. These options select the startup defaults; the application can change both minimum log levels at runtime through their thread-safe `SetMinimumLogLevel()` APIs.

Select the matching hardware version: T-Display-P4 V1 uses ESP32-C6, while
T-Display-P4 V2 and T-Display-P4-Air use ESP32-C5. The ESP-Hosted slave target
must match the hardware.

### Build

```bash
idf.py build
```

### Flash and Monitor

```bash
idf.py -p COMx flash monitor
```

Replace `COMx` with the serial port connected to the board. On Linux or macOS, use the corresponding device path, such as `/dev/ttyUSB0`.
