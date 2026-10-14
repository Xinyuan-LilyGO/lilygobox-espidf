/*
 * @Description: 设备名称校验、存储与读取实现
 * @Author: LILYGO_L
 * @Date: 2026-05-18 12:08:00
 * @LastEditTime: 2026-10-13 12:00:00
 * @License: GPL 3.0
 */
#include "app/device_identity.h"

#include <array>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <mutex>

#include "app/storage/storage_internal.h"
#include "base/logger.h"
#include "esp_mac.h"

namespace lilygo_box::app {
namespace {

// 默认名称仅由硬件身份生成；NVS 只保存用户设置的名称。
char g_device_name[kMaxDeviceNameLength + 1] = "";
std::mutex g_device_name_mutex;
constexpr char kDeviceNameNvsKey[] = "device_name";
using DeviceName = std::array<char, kMaxDeviceNameLength + 1>;

/**
 * @brief 比较两个设备名称缓存快照
 */
bool AreDeviceNamesEqual(const DeviceName& left, const DeviceName& right) {
  return left == right;
}

NvsStorageCache<DeviceName> g_name_cache(StorageDomain::kDeviceIdentity,
                                         AreDeviceNamesEqual);

/**
 * @brief 判断字符是否为空白字符
 * @param character 待判断字符
 * @return 是空白字符返回 true，否则返回 false
 */
bool IsSpace(char character) {
  return std::isspace(static_cast<unsigned char>(character)) != 0;
}

}  // namespace

bool InitDeviceIdentity() {
  std::lock_guard<std::mutex> lock(g_device_name_mutex);
  constexpr char kDefaultNamePrefix[] = "LilygoBox";
  uint8_t mac[6] = {};
  if (esp_efuse_mac_get_default(mac) == ESP_OK) {
    std::snprintf(g_device_name, sizeof(g_device_name),
                  "%s_%02X%02X%02X%02X%02X%02X", kDefaultNamePrefix, mac[0],
                  mac[1], mac[2], mac[3], mac[4], mac[5]);
  } else {
    std::snprintf(g_device_name, sizeof(g_device_name), "%s",
                  kDefaultNamePrefix);
    LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
               "Read MAC for default device name failed\n");
  }

  // 名称在其他配置缓存之前加载，必须先建立缓存锁和独立 NVS 分区。
  if (!EnsureStorageCoordinatorInitialized() ||
      !EnsureApplicationNvsInitialized()) {
    return false;
  }

  DeviceName stored = {};
  nvs_handle_t handle = 0;
  const esp_err_t open_result =
      OpenApplicationNvs("settings", NVS_READONLY, &handle);
  esp_err_t read_result = ESP_ERR_NVS_NOT_FOUND;
  if (open_result == ESP_OK) {
    size_t capacity = stored.size();
    read_result =
        nvs_get_str(handle, kDeviceNameNvsKey, stored.data(), &capacity);
    nvs_close(handle);
    if (read_result == ESP_OK && stored[0] != '\0') {
      std::snprintf(g_device_name, sizeof(g_device_name), "%s", stored.data());
    } else {
      stored.fill('\0');
    }
  }
  const bool initialized = g_name_cache.Initialize(stored);
  return initialized &&
         (open_result == ESP_OK || open_result == ESP_ERR_NVS_NOT_FOUND) &&
         (read_result == ESP_OK || read_result == ESP_ERR_NVS_NOT_FOUND);
}

StorageStageResult StageDeviceIdentityStorage(nvs_handle_t handle) {
  const DeviceName* name = nullptr;
  if (!g_name_cache.BeginFlush(&name)) return StorageStageResult::kClean;
  const esp_err_t result = nvs_set_str(handle, kDeviceNameNvsKey, name->data());
  if (result != ESP_OK) {
    LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
               "Stage device name failed: %s\n", esp_err_to_name(result));
  }
  return result == ESP_OK ? StorageStageResult::kStaged
                          : StorageStageResult::kFailed;
}

void FinishDeviceIdentityStorage(bool committed) {
  g_name_cache.FinishFlush(committed);
}

const char* ConfiguredDeviceName() { return g_device_name; }

void CopyConfiguredDeviceName(char* output, size_t capacity) {
  if (output == nullptr || capacity == 0) return;
  std::lock_guard<std::mutex> lock(g_device_name_mutex);
  std::snprintf(output, capacity, "%s", g_device_name);
}

bool SetConfiguredDeviceName(const char* name) {
  if (name == nullptr) {
    return false;
  }

  const char* begin = name;
  while (*begin != '\0' && IsSpace(*begin)) {
    ++begin;
  }

  const char* end = begin + std::strlen(begin);
  while (end > begin && IsSpace(*(end - 1))) {
    --end;
  }

  if (begin == end) {
    return false;
  }

  size_t length = static_cast<size_t>(end - begin);
  if (length > kMaxDeviceNameLength) {
    length = kMaxDeviceNameLength;
  }

  std::lock_guard<std::mutex> lock(g_device_name_mutex);
  DeviceName updated = {};
  std::memcpy(updated.data(), begin, length);
  // 提交失败时不更新界面名称；缓存保留待写快照供后续重试。
  if (!g_name_cache.UpdateAndPersist(updated)) {
    LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
               "Persist device name failed: bytes=%u\n",
               static_cast<unsigned>(length));
    return false;
  }
  std::memcpy(g_device_name, updated.data(), updated.size());
  return true;
}

}  // namespace lilygo_box::app
