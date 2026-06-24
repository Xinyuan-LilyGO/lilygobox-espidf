/*
 * @Description: Settings WLAN NVS storage helpers
 * @Author: LILYGO_L
 * @Date: 2026-06-23 00:00:00
 * @LastEditTime: 2026-06-23 00:00:00
 * @License: GPL 3.0
 */
#include "app/storage/settings_wifi_storage.h"

#include <algorithm>
#include <cstdio>
#include <cstdint>

#include "base/logger.h"
#include "esp_err.h"
#include "nvs.h"

namespace lilygo_box::app {
namespace {

constexpr const char* kSettingsNvsNamespace = "settings";
constexpr const char* kWifiSavedNetworksNvsKey = "wifi_saved";
constexpr const char* kWifiPreferencesNvsKey = "wifi_config";
constexpr uint32_t kWifiSavedNetworksMagic = 0x57494649;
constexpr uint32_t kWifiSavedNetworksVersion = 1;
constexpr uint32_t kWifiPreferencesMagic = 0x57465052;
constexpr uint32_t kWifiPreferencesVersion = 1;

struct WifiSavedNetworksStorage {
  uint32_t magic = kWifiSavedNetworksMagic;
  uint32_t version = kWifiSavedNetworksVersion;
  uint32_t count = 0;
  WifiSavedNetwork networks[kWifiSavedNetworkCapacity] = {};
};

struct WifiPreferencesStorage {
  uint32_t magic = kWifiPreferencesMagic;
  uint32_t version = kWifiPreferencesVersion;
  uint8_t enabled_requested = 0;
  char auto_connect_ssid[hal::kWifiSsidMaxLength + 1] = {};
};

/**
 * @brief 打开设置 NVS 命名空间
 * @param mode NVS 打开模式
 * @param handle NVS 句柄输出地址
 * @return 打开成功返回 ESP_OK，否则返回错误码
 */
esp_err_t OpenSettingsNvs(nvs_open_mode_t mode, nvs_handle_t* handle) {
  if (handle == nullptr) {
    return ESP_ERR_INVALID_ARG;
  }

  const esp_err_t result = nvs_open(kSettingsNvsNamespace, mode, handle);
  if (result == ESP_ERR_NVS_NOT_FOUND && mode == NVS_READONLY) {
    return result;
  }
  if (result != ESP_OK) {
    LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
        "Open settings NVS failed (error code: %#X)\n", result);
  }
  return result;
}

}  // namespace

bool SaveWifiSavedNetworksToNvs(
    const WifiSavedNetwork* networks, size_t count) {
  if (networks == nullptr && count > 0) {
    return false;
  }

  WifiSavedNetworksStorage storage;
  storage.count =
      static_cast<uint32_t>(std::min(count, kWifiSavedNetworkCapacity));
  for (size_t i = 0; i < storage.count; ++i) {
    storage.networks[i] = networks[i];
  }

  nvs_handle_t handle = 0;
  if (OpenSettingsNvs(NVS_READWRITE, &handle) != ESP_OK) {
    return false;
  }

  esp_err_t result = nvs_set_blob(
      handle, kWifiSavedNetworksNvsKey, &storage, sizeof(storage));
  if (result == ESP_OK) {
    result = nvs_commit(handle);
  }
  nvs_close(handle);

  if (result != ESP_OK) {
    LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
        "Save WLAN credentials failed (error code: %#X)\n", result);
    return false;
  }
  return true;
}

bool LoadWifiSavedNetworksFromNvs(
    WifiSavedNetwork* networks, size_t capacity, size_t* count) {
  if (networks == nullptr || count == nullptr) {
    return false;
  }

  *count = 0;
  for (size_t i = 0; i < capacity; ++i) {
    networks[i] = WifiSavedNetwork();
  }

  nvs_handle_t handle = 0;
  const esp_err_t open_result = OpenSettingsNvs(NVS_READONLY, &handle);
  if (open_result == ESP_ERR_NVS_NOT_FOUND) {
    return true;
  }
  if (open_result != ESP_OK) {
    return false;
  }

  WifiSavedNetworksStorage storage;
  size_t blob_size = sizeof(storage);
  const esp_err_t result = nvs_get_blob(
      handle, kWifiSavedNetworksNvsKey, &storage, &blob_size);
  nvs_close(handle);
  if (result == ESP_ERR_NVS_NOT_FOUND) {
    return true;
  }
  if (result != ESP_OK || blob_size != sizeof(storage) ||
      storage.magic != kWifiSavedNetworksMagic ||
      storage.version != kWifiSavedNetworksVersion) {
    LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
        "Load WLAN credentials failed (error code: %#X)\n", result);
    return false;
  }

  const size_t storage_count = std::min<size_t>(
      storage.count, kWifiSavedNetworkCapacity);
  for (size_t i = 0; i < storage_count && *count < capacity; ++i) {
    if (storage.networks[i].ssid[0] == '\0') {
      continue;
    }
    networks[*count] = storage.networks[i];
    ++(*count);
  }
  return true;
}

bool SaveWifiPreferencesToNvs(const WifiPreferences& preferences) {
  WifiPreferencesStorage storage;
  storage.enabled_requested = preferences.enabled_requested ? 1 : 0;
  std::snprintf(storage.auto_connect_ssid,
      sizeof(storage.auto_connect_ssid), "%s",
      preferences.auto_connect_ssid);

  nvs_handle_t handle = 0;
  if (OpenSettingsNvs(NVS_READWRITE, &handle) != ESP_OK) {
    return false;
  }

  esp_err_t result = nvs_set_blob(
      handle, kWifiPreferencesNvsKey, &storage, sizeof(storage));
  if (result == ESP_OK) {
    result = nvs_commit(handle);
  }
  nvs_close(handle);

  if (result != ESP_OK) {
    LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
        "Save WLAN preferences failed (error code: %#X)\n", result);
    return false;
  }
  return true;
}

bool LoadWifiPreferencesFromNvs(WifiPreferences* preferences) {
  if (preferences == nullptr) {
    return false;
  }

  nvs_handle_t handle = 0;
  const esp_err_t open_result = OpenSettingsNvs(NVS_READONLY, &handle);
  if (open_result != ESP_OK) {
    return false;
  }

  WifiPreferencesStorage storage;
  size_t blob_size = sizeof(storage);
  const esp_err_t result = nvs_get_blob(
      handle, kWifiPreferencesNvsKey, &storage, &blob_size);
  nvs_close(handle);
  if (result == ESP_ERR_NVS_NOT_FOUND) {
    return false;
  }
  if (result != ESP_OK || blob_size != sizeof(storage) ||
      storage.magic != kWifiPreferencesMagic ||
      storage.version != kWifiPreferencesVersion) {
    LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
        "Load WLAN preferences failed (error code: %#X)\n", result);
    return false;
  }

  preferences->enabled_requested = storage.enabled_requested != 0;
  std::snprintf(preferences->auto_connect_ssid,
      sizeof(preferences->auto_connect_ssid), "%s",
      storage.auto_connect_ssid);
  return true;
}

}  // namespace lilygo_box::app
