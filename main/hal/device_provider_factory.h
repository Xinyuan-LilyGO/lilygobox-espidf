/*
 * @Description: 设备 Provider 上下文与工厂接口
 * @Author: LILYGO_L
 * @Date: 2026-05-10 13:27:05
 * @LastEditTime: 2026-07-30 18:00:00
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
  DeviceInfoProvider* device_info = nullptr;
  GpsProvider* gps = nullptr;
  ImuProvider* imu = nullptr;
  AudioProvider* audio = nullptr;
  HapticProvider* haptic = nullptr;
  BatteryManagementProvider* battery_management = nullptr;
  CameraProvider* camera = nullptr;
  RtcProvider* rtc = nullptr;
  RadioProvider* radio = nullptr;
  // Air 板 NFC 读卡器接口，其他设备保持为空。
  NfcProvider* nfc = nullptr;
  // Air 板红外收发接口，其他设备保持为空。
  InfraredProvider* infrared = nullptr;
  // Air 板 nRF9151 蜂窝接口，其他设备保持为空。
  CellularProvider* cellular = nullptr;
  EthernetProvider* ethernet = nullptr;
  WifiProvider* wifi = nullptr;
  StorageProvider* storage = nullptr;
};

/**
 * @brief 创建 Kconfig 选择的设备 provider 上下文
 * @return 创建成功返回设备 provider 上下文，否则返回空上下文
 */
DeviceProviderContext CreateDeviceProviderContext();

}  // namespace lilygo_box::hal
