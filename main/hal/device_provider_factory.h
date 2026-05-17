/*
 * @Description: None
 * @Author: LILYGO_L
 * @Date: 2026-05-10 13:27:05
 * @LastEditTime: 2026-05-14 00:45:00
 * @License: GPL 3.0
 */
#pragma once

#include <memory>

#include "hal/providers/providers.h"

namespace lilygo_box::hal {

struct DeviceProviderContext {
  DeviceProvider* device = nullptr;
  std::unique_ptr<ScreenProvider> screen;
  DeviceDiagnosticsProvider* diagnostics = nullptr;
  GpsProvider* gps = nullptr;
  ImuProvider* imu = nullptr;
  AudioProvider* audio = nullptr;
  HapticProvider* haptic = nullptr;
  BmuProvider* bmu = nullptr;
  RtcProvider* rtc = nullptr;
  EthernetProvider* ethernet = nullptr;
  WifiProvider* wifi = nullptr;
};

/**
 * @brief 创建 Kconfig 选择的设备 provider 上下文
 * @return 创建成功返回设备 provider 上下文，否则返回空上下文
 */
DeviceProviderContext CreateDeviceProviderContext();

}  // namespace lilygo_box::hal
