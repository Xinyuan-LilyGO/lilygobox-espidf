/*
 * @Description: T-Display-P4 SD 与 USB 存储实现
 * @Author: LILYGO_L
 * @Date: 2026-08-28 00:00:00
 * @LastEditTime: 2026-08-28 00:00:00
 * @License: GPL 3.0
 */
#include "hal/device/t_display_p4/device.h"

#include <sys/stat.h>

#include "base/logger.h"

namespace lilygo_box::hal {
namespace device = lilygo_device_driver::t_display_p4::device;

bool TDisplayP4Device::EnsureSdCardMounted() {
  if (IsSdCardMounted()) {
    return true;
  }

  const bool result = driver_.InitSdmmc(device::sd::kBasePath, SDMMC_FREQ_52M);
  if (!result) {
    LogMessage(LogLevel::kWarning, __FILE__, __LINE__, "InitSdmmc failed\n");
    return false;
  }
  return IsSdCardMounted();
}

bool TDisplayP4Device::UnmountSdCard() { return driver_.DeinitSdmmc(); }

bool TDisplayP4Device::IsSdCardMounted() const {
  if (!driver_.IsSdmmcReady()) {
    return false;
  }
  struct stat info = {};
  return stat(device::sd::kBasePath, &info) == 0 && S_ISDIR(info.st_mode);
}

const char* TDisplayP4Device::SdCardBasePath() const {
  return device::sd::kBasePath;
}

bool TDisplayP4Device::StartUsbStorage() {
  if (!driver_.SetUsbHostPowerEnabled(true)) {
    return false;
  }
  return usb_storage_manager_.Start();
}

bool TDisplayP4Device::StopUsbStorage() {
  // USB PHY 供电保持开启可避免 ESP32-P4 产生约 20 mA 的额外功耗。
  return usb_storage_manager_.Stop();
}

bool TDisplayP4Device::ReadUsbStorageSnapshot(
    UsbStorageSnapshot* snapshot) const {
  return usb_storage_manager_.ReadSnapshot(snapshot);
}

}  // namespace lilygo_box::hal
