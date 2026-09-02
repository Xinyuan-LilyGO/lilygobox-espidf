/*
 * @Description: T-Display-P4-Air 板级初始化与设备信息实现
 * @Author: LILYGO_L
 * @Date: 2026-08-28 00:00:00
 * @LastEditTime: 2026-08-28 00:00:00
 * @License: GPL 3.0
 */
#include "hal/device/t_display_p4_air/device.h"

#include <memory>

#include "base/logger.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

namespace lilygo_box::hal {

TDisplayP4AirDevice::TDisplayP4AirDevice()
    : driver_(TDisplayP4AirBoardDriver::GetInstance()),
      tool_(std::make_unique<cpp_bus_driver::Tool>()) {
  wifi_.scan_results_mutex = xSemaphoreCreateMutex();
  radio_.mutex = xSemaphoreCreateMutex();
  otg_.mutex = xSemaphoreCreateMutex();
  nrf9151_mutex_ = xSemaphoreCreateMutex();
  imu_.mutex = xSemaphoreCreateMutex();
  nfc_.mutex = xSemaphoreCreateMutex();
  infrared_.mutex = xSemaphoreCreateMutex();
  cellular_.status_mutex = xSemaphoreCreateMutex();
}

bool TDisplayP4AirDevice::InitDevice() {
  if (wifi_.scan_results_mutex == nullptr || radio_.mutex == nullptr ||
      otg_.mutex == nullptr ||
      nrf9151_mutex_ == nullptr || imu_.mutex == nullptr ||
      nfc_.mutex == nullptr || infrared_.mutex == nullptr ||
      cellular_.status_mutex == nullptr) {
    LogMessage(LogLevel::kError, __FILE__, __LINE__,
        "Create T-Display-P4-Air synchronization resources failed\n");
    return false;
  }

  const bool result = driver_.Init(TDisplayP4AirBoardDriver::InitMode::kAsync);
  otg_.source_role_enabled = false;
  otg_.power_output_enabled = false;
  if (!result) {
    LogMessage(LogLevel::kWarning, __FILE__, __LINE__, "Init failed\n");
  }

  if (!WaitForScreenReady()) {
    LogMessage(
        LogLevel::kError, __FILE__, __LINE__, "WaitForScreenReady failed\n");
    return false;
  }
  if (!WaitForTouchReady()) {
    LogMessage(
        LogLevel::kError, __FILE__, __LINE__, "WaitForTouchReady failed\n");
    return false;
  }
  if (!driver_.SetScreenSleep(false)) {
    LogMessage(LogLevel::kError, __FILE__, __LINE__,
        "Activate screen failed\n");
    return false;
  }
  if (!InitializePowerButton()) {
    LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
        "Initialize power button failed\n");
  }
  if (!InitializeVolumeButtons()) {
    LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
        "Initialize volume buttons failed\n");
  }
  if (!InitializeTouchInterrupt()) {
    LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
        "Initialize touch interrupt failed; using polling fallback\n");
  }
  return true;
}

int TDisplayP4AirDevice::ScreenWidth() const {
  return driver_.screen_info().width;
}

int TDisplayP4AirDevice::ScreenHeight() const {
  return driver_.screen_info().height;
}

int TDisplayP4AirDevice::ScreenBitsPerPixel() const {
  return driver_.screen_info().bits_per_pixel;
}

bool TDisplayP4AirDevice::ReadDeviceInfo(DeviceInfo* info) {
  if (info == nullptr) {
    return false;
  }

  const auto device_info = driver_.device_info();
  info->device_model_name = device_info.model.name;
  info->device_model_version = device_info.model.version;
  info->screen_type = device_info.screen.name;
  info->screen_width = device_info.screen.width;
  info->screen_height = device_info.screen.height;
  info->screen_bits_per_pixel = device_info.screen.bits_per_pixel;
  info->screen_pixel_format = device_info.screen.pixel_format;
  info->camera_name = device_info.camera.name;
  info->camera_pixel_format = device_info.camera.pixel_format;
  info->camera_bits_per_pixel = device_info.camera.bits_per_pixel;
  info->camera_buffer_count = device_info.camera.buffer_count;
  info->battery_charger_chip_name = device_info.battery.charger_chip_name;
  info->battery_fuel_gauge_chip_name =
      device_info.battery.fuel_gauge_chip_name;
  info->battery_capacity_mah = device_info.battery.capacity_mah;
  return true;
}

bool TDisplayP4AirDevice::ReadDeviceDiagnostics(
    DeviceDiagnostics* diagnostics) {
  if (diagnostics == nullptr) {
    return false;
  }

  *diagnostics = DeviceDiagnostics();
  const bool battery_management_result =
      ReadBatteryManagementStatus(&diagnostics->battery_management);
  const bool imu_result = ReadImuStatus(&diagnostics->imu);
  return battery_management_result || imu_result;
}

}  // namespace lilygo_box::hal
