/*
 * @Description: 当前设备信息快照数据模型与采集接口
 * @Author: LILYGO_L
 * @Date: 2026-05-19 23:20:00
 * @LastEditTime: 2026-05-20 00:10:00
 * @License: GPL 3.0
 */
#pragma once

#include <cstddef>
#include <cstdint>

namespace lilygo_box::hal {
class DeviceInfoProvider;
}  // namespace lilygo_box::hal

namespace lilygo_box::app {

// 当前芯片信息快照
struct CurrentDeviceChipInfo {
  const char* model = "unknown";
  char efuse_mac[18] = "unknown";
  int revision_major = 0;
  int revision_minor = 0;
  int cores = 0;
  uint32_t flash_total_bytes = 0;
  size_t running_image_bytes = 0;
  bool running_image_size_valid = false;
  const char* flash_features = "unknown";
};

// 当前内存信息快照
struct CurrentDeviceMemoryInfo {
  uint32_t free_heap_bytes = 0;
  size_t internal_free_bytes = 0;
  size_t internal_total_bytes = 0;
  size_t psram_free_bytes = 0;
  size_t psram_total_bytes = 0;
};

// 当前软件和设备名称信息快照
struct CurrentDeviceSoftwareInfo {
  const char* company = "lilygo";
  const char* device_name = "unknown";
  const char* device_model_name = "unknown";
  const char* device_model_version = "v1.0";
  const char* software_name = "unknown";
  const char* software_version = "unknown";
  const char* software_build_date = "unknown";
  const char* software_build_time = "unknown";
  const char* esp_idf_version = "unknown";
  const char* target_arch = "unknown";
};

// 当前屏幕信息快照
struct CurrentDeviceScreenInfo {
  const char* type = "unknown";
  int width = 0;
  int height = 0;
  int bits_per_pixel = 0;
  const char* pixel_format = "unknown";
};

// 当前相机信息快照
struct CurrentDeviceCameraInfo {
  const char* type = "unknown";
  const char* pixel_format = "unknown";
  int bits_per_pixel = 0;
  int buffer_count = 0;
};

// 当前电池信息快照
struct CurrentDeviceBatteryInfo {
  const char* charger_chip_name = "unknown";
  const char* fuel_gauge_chip_name = "unknown";
  int capacity_mah = 0;
};

// 当前 LVGL 版本信息快照
struct CurrentDeviceLvglInfo {
  int major = 0;
  int minor = 0;
  int patch = 0;
  const char* extra_info = "";
};

// 当前设备完整信息快照
struct CurrentDeviceInfoSnapshot {
  CurrentDeviceChipInfo chip;
  CurrentDeviceMemoryInfo memory;
  CurrentDeviceSoftwareInfo software;
  CurrentDeviceScreenInfo screen;
  CurrentDeviceCameraInfo camera;
  CurrentDeviceBatteryInfo battery;
  CurrentDeviceLvglInfo lvgl;
};

/**
 * @brief 读取当前设备信息快照
 * @param provider 设备信息提供者
 * @param info 设备信息快照输出地址
 * @return 读取成功返回 true，否则返回 false
 */
bool ReadCurrentDeviceInfoSnapshot(
    hal::DeviceInfoProvider* provider, CurrentDeviceInfoSnapshot* info);

}  // namespace lilygo_box::app
