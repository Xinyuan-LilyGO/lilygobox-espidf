/*
 * @Description: 当前设备芯片、内存、软件与外设信息采集实现
 * @Author: LILYGO_L
 * @Date: 2026-05-19 23:20:00
 * @LastEditTime: 2026-05-20 00:10:00
 * @License: GPL 3.0
 */
#include "app/device_info_snapshot.h"

#include <cstdio>

#include "app/device_identity.h"
#include "base/logger.h"
#include "esp_app_desc.h"
#include "esp_chip_info.h"
#include "esp_err.h"
#include "esp_flash.h"
#include "esp_heap_caps.h"
#include "esp_image_format.h"
#include "esp_mac.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "hal/providers/device_info_provider.h"
#include "lvgl.h"
#include "sdkconfig.h"

namespace lilygo_box::app {
namespace {

/**
 * @brief 获取当前配置的芯片型号
 * @return 芯片型号字符串
 */
const char* ConfiguredChipModel() {
#if defined(CONFIG_IDF_TARGET)
  return CONFIG_IDF_TARGET;
#else
  return "unknown";
#endif
}

/**
 * @brief 获取当前配置的目标架构
 * @return 目标架构字符串
 */
const char* ConfiguredTargetArch() {
#if defined(CONFIG_IDF_TARGET_ARCH)
  return CONFIG_IDF_TARGET_ARCH;
#else
  return "unknown";
#endif
}

/**
 * @brief 返回有效字符串
 * @param text 待检查字符串
 * @return 有效字符串，空指针或空字符串返回 unknown
 */
const char* KnownString(const char* text) {
  return (text == nullptr || text[0] == '\0') ? "unknown" : text;
}

/**
 * @brief 获取用户设置的设备名称，未设置时回退到设备型号名
 * @param device_model_name 设备型号名
 * @return 设备名称字符串
 */
const char* ConfiguredDeviceNameOrModelName(const char* device_model_name) {
  const char* device_name = ConfiguredDeviceName();
  if (device_name != nullptr && device_name[0] != '\0') {
    return device_name;
  }
  return KnownString(device_model_name);
}

/**
 * @brief 格式化本机 efuse MAC 地址
 * @param buffer 输出缓冲区
 * @param size 输出缓冲区大小
 */
void FormatMacAddress(char* buffer, size_t size) {
  if (buffer == nullptr || size == 0) {
    return;
  }

  uint8_t mac[6] = {};
  const esp_err_t result = esp_efuse_mac_get_default(mac);
  if (result != ESP_OK) {
    LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
        "Read efuse MAC failed, result=%d\n", static_cast<int>(result));
    std::snprintf(buffer, size, "unknown");
    return;
  }

  std::snprintf(buffer, size, "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1],
      mac[2], mac[3], mac[4], mac[5]);
}

/**
 * @brief 读取当前运行固件镜像大小
 * @param image_size 镜像大小输出地址
 * @return 读取成功返回 true，否则返回 false
 */
bool ReadRunningImageSize(size_t* image_size) {
  if (image_size == nullptr) {
    LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
        "ReadRunningImageSize received empty output pointer\n");
    return false;
  }

  const esp_partition_t* running_partition = esp_ota_get_running_partition();
  if (running_partition == nullptr) {
    LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
        "Get running partition failed\n");
    return false;
  }

  esp_partition_pos_t partition = {};
  partition.offset = running_partition->address;
  partition.size = running_partition->size;

  esp_image_metadata_t metadata = {};
  const esp_err_t metadata_result = esp_image_get_metadata(&partition, &metadata);
  if (metadata_result != ESP_OK || metadata.image_len == 0) {
    LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
        "Read running image metadata failed, result=%d, image_len=%u\n",
        static_cast<int>(metadata_result),
        static_cast<unsigned int>(metadata.image_len));
    return false;
  }

  *image_size = metadata.image_len;
  return true;
}

/**
 * @brief 读取设备信息，失败时返回默认占位信息
 * @param provider 设备信息提供者
 * @return 设备信息
 */
hal::DeviceInfo ReadDeviceInfo(hal::DeviceInfoProvider* provider) {
  hal::DeviceInfo info;
  if (provider != nullptr) {
    provider->ReadDeviceInfo(&info);
  }
  return info;
}

}  // namespace

bool ReadCurrentDeviceInfoSnapshot(
    hal::DeviceInfoProvider* provider, CurrentDeviceInfoSnapshot* info) {
  if (info == nullptr) {
    LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
        "ReadCurrentDeviceInfoSnapshot received empty output pointer\n");
    return false;
  }

  *info = CurrentDeviceInfoSnapshot();

  esp_chip_info_t chip_info = {};
  esp_chip_info(&chip_info);
  info->chip.model = ConfiguredChipModel();
  FormatMacAddress(info->chip.efuse_mac, sizeof(info->chip.efuse_mac));
  info->chip.revision_major = chip_info.revision / 100;
  info->chip.revision_minor = chip_info.revision % 100;
  info->chip.cores = chip_info.cores;
  info->chip.flash_features =
      (chip_info.features & CHIP_FEATURE_EMB_FLASH) ? "embedded" : "external";
  const esp_err_t flash_size_result =
      esp_flash_get_size(nullptr, &info->chip.flash_total_bytes);
  if (flash_size_result != ESP_OK) {
    LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
        "Read flash size failed, result=%d\n", static_cast<int>(flash_size_result));
  }
  info->chip.running_image_size_valid =
      ReadRunningImageSize(&info->chip.running_image_bytes);

  info->memory.free_heap_bytes = esp_get_free_heap_size();
  info->memory.internal_free_bytes =
      heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
  info->memory.internal_total_bytes =
      heap_caps_get_total_size(MALLOC_CAP_INTERNAL);
  info->memory.psram_free_bytes = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
  info->memory.psram_total_bytes = heap_caps_get_total_size(MALLOC_CAP_SPIRAM);

  const esp_app_desc_t* app_description = esp_app_get_description();
  const hal::DeviceInfo device_info = ReadDeviceInfo(provider);
  info->software.company = "lilygo";
  info->software.device_model_name =
      KnownString(device_info.device_model_name);
  info->software.device_model_version =
      KnownString(device_info.device_model_version);
  info->software.device_name =
      ConfiguredDeviceNameOrModelName(info->software.device_model_name);
  info->software.software_name = KnownString(
      app_description == nullptr ? nullptr : app_description->project_name);
  info->software.software_version = KnownString(
      app_description == nullptr ? nullptr : app_description->version);
  info->software.software_build_date = KnownString(
      app_description == nullptr ? nullptr : app_description->date);
  info->software.software_build_time = KnownString(
      app_description == nullptr ? nullptr : app_description->time);
  info->software.esp_idf_version = esp_get_idf_version();
  info->software.target_arch = ConfiguredTargetArch();

  info->screen.type = KnownString(device_info.screen_type);
  info->screen.width = device_info.screen_width;
  info->screen.height = device_info.screen_height;
  info->screen.bits_per_pixel = device_info.screen_bits_per_pixel;
  info->screen.pixel_format = KnownString(device_info.screen_pixel_format);

  info->camera.type = KnownString(device_info.camera_name);
  info->camera.pixel_format = KnownString(device_info.camera_pixel_format);
  info->camera.bits_per_pixel = device_info.camera_bits_per_pixel;
  info->camera.buffer_count = device_info.camera_buffer_count;

  info->battery.fuel_gauge_name =
      KnownString(device_info.battery_fuel_gauge_name);
  info->battery.capacity_mah = device_info.battery_capacity_mah;

  info->lvgl.major = LVGL_VERSION_MAJOR;
  info->lvgl.minor = LVGL_VERSION_MINOR;
  info->lvgl.patch = LVGL_VERSION_PATCH;
  info->lvgl.extra_info = LVGL_VERSION_INFO;
  return true;
}

}  // namespace lilygo_box::app
