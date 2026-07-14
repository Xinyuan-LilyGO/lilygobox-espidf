/*
 * @Description: 设备型号、芯片与硬件基础信息接口
 * @Author: LILYGO_L
 * @Date: 2026-05-19 22:35:00
 * @LastEditTime: 2026-05-19 22:35:00
 * @License: GPL 3.0
 */
#pragma once

namespace lilygo_box::hal {

// 设备型号、屏幕和相机等基础信息
struct DeviceInfo {
  const char* device_name = "unknown";
  const char* device_model_name = "unknown";
  const char* device_model_version = "v1.0";
  const char* screen_type = "unknown";
  int screen_width = 0;
  int screen_height = 0;
  int screen_bits_per_pixel = 0;
  const char* screen_pixel_format = "unknown";
  const char* camera_name = "unknown";
  const char* camera_pixel_format = "unknown";
  int camera_bits_per_pixel = 0;
  int camera_buffer_count = 0;
  const char* battery_fuel_gauge_name = "unknown";
  int battery_capacity_mah = 0;
};

class DeviceInfoProvider {
 public:
  virtual ~DeviceInfoProvider() = default;

  /**
   * @brief 读取当前设备信息
   * @param info 设备信息输出地址
   * @return 读取成功返回 true，否则返回 false
   */
  virtual bool ReadDeviceInfo(DeviceInfo* info) = 0;
};

}  // namespace lilygo_box::hal
