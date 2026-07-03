/**
 * @Description: WLAN 偏好存储实现
 * @Author: LILYGO_L
 * @Date: 2026-06-23 00:00:00
 * @LastEditTime: 2026-07-03 00:00:00
 * @License: GPL 3.0
 */
#include "app/storage/wifi_storage.h"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstdint>

#include "base/logger.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "nvs.h"

namespace lilygo_box::app {
namespace {

constexpr const char* kNvsNamespace = "settings";
constexpr const char* kWifiSavedNvsKey = "wifi_saved";
constexpr const char* kWifiPrefsNvsKey = "wifi_config";
constexpr uint32_t kWifiSavedMagic = 0x57494649;
constexpr uint32_t kWifiPrefsMagic = 0x57465052;

struct SavedBlob {
  uint32_t magic = kWifiSavedMagic;
  uint32_t count = 0;
  WifiSavedNetwork networks[kWifiSavedNetworkCapacity] = {};
};

struct PrefsBlob {
  uint32_t magic = kWifiPrefsMagic;
  uint8_t enabled_requested = 0;
  char auto_connect_ssid[hal::kWifiSsidMaxLength + 1] = {};
};

std::atomic<bool> g_enabled{false};
SemaphoreHandle_t g_mutex = nullptr;
char g_auto_connect_ssid[hal::kWifiSsidMaxLength + 1] = {};

}  // namespace

bool SaveWifiSavedNetworksToNvs(
    const WifiSavedNetwork* networks, size_t count) {
  if (networks == nullptr && count > 0) return false;

  SavedBlob blob;
  blob.count = static_cast<uint32_t>(std::min(count, kWifiSavedNetworkCapacity));
  for (size_t i = 0; i < blob.count; ++i) {
    blob.networks[i] = networks[i];
  }

  nvs_handle_t handle = 0;
  if (nvs_open(kNvsNamespace, NVS_READWRITE, &handle) != ESP_OK) return false;
  bool ok = nvs_set_blob(handle, kWifiSavedNvsKey, &blob, sizeof(blob)) == ESP_OK &&
            nvs_commit(handle) == ESP_OK;
  nvs_close(handle);
  return ok;
}

bool LoadWifiSavedNetworksFromNvs(
    WifiSavedNetwork* networks, size_t capacity, size_t* count) {
  if (networks == nullptr || count == nullptr) return false;
  *count = 0;

  nvs_handle_t handle = 0;
  if (nvs_open(kNvsNamespace, NVS_READONLY, &handle) != ESP_OK) return true;

  SavedBlob blob;
  size_t sz = sizeof(blob);
  if (nvs_get_blob(handle, kWifiSavedNvsKey, &blob, &sz) == ESP_OK &&
      sz == sizeof(blob) && blob.magic == kWifiSavedMagic) {
    size_t n = std::min<size_t>(blob.count, kWifiSavedNetworkCapacity);
    for (size_t i = 0; i < n && *count < capacity; ++i) {
      if (blob.networks[i].ssid[0] == '\0') continue;
      networks[*count] = blob.networks[i];
      ++(*count);
    }
  }
  nvs_close(handle);
  return true;
}

void InitWifiCache() {
  if (g_mutex == nullptr) {
    g_mutex = xSemaphoreCreateMutex();
  }
  if (g_mutex == nullptr) return;

  nvs_handle_t handle = 0;
  if (nvs_open(kNvsNamespace, NVS_READONLY, &handle) != ESP_OK) return;

  PrefsBlob blob;
  size_t sz = sizeof(blob);
  if (nvs_get_blob(handle, kWifiPrefsNvsKey, &blob, &sz) == ESP_OK &&
      sz == sizeof(blob) && blob.magic == kWifiPrefsMagic) {
    g_enabled.store(blob.enabled_requested != 0);
    xSemaphoreTake(g_mutex, portMAX_DELAY);
    std::snprintf(g_auto_connect_ssid, sizeof(g_auto_connect_ssid),
                  "%s", blob.auto_connect_ssid);
    xSemaphoreGive(g_mutex);
  }
  nvs_close(handle);
}

WifiPreferences GetWifiPreferences() {
  WifiPreferences prefs;
  prefs.enabled_requested = g_enabled.load();
  if (g_mutex != nullptr) {
    xSemaphoreTake(g_mutex, portMAX_DELAY);
    std::snprintf(prefs.auto_connect_ssid, sizeof(prefs.auto_connect_ssid),
                  "%s", g_auto_connect_ssid);
    xSemaphoreGive(g_mutex);
  }
  return prefs;
}

bool UpdateWifiPreferences(const WifiPreferences& prefs) {
  if (g_mutex == nullptr) return false;

  g_enabled.store(prefs.enabled_requested);
  xSemaphoreTake(g_mutex, portMAX_DELAY);
  std::snprintf(g_auto_connect_ssid, sizeof(g_auto_connect_ssid),
                "%s", prefs.auto_connect_ssid);
  xSemaphoreGive(g_mutex);

  nvs_handle_t handle = 0;
  if (nvs_open(kNvsNamespace, NVS_READWRITE, &handle) != ESP_OK) return false;

  PrefsBlob blob;
  blob.enabled_requested = prefs.enabled_requested ? 1 : 0;
  std::snprintf(blob.auto_connect_ssid, sizeof(blob.auto_connect_ssid),
                "%s", prefs.auto_connect_ssid);

  bool ok = nvs_set_blob(handle, kWifiPrefsNvsKey, &blob, sizeof(blob)) == ESP_OK &&
            nvs_commit(handle) == ESP_OK;
  nvs_close(handle);
  return ok;
}

}  // namespace lilygo_box::app
