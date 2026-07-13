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
#include <cstdint>
#include <cstdio>

#include "app/storage/storage.h"
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
std::atomic<bool> g_preferences_loaded{false};
SemaphoreHandle_t g_mutex = nullptr;
char g_auto_connect_ssid[hal::kWifiSsidMaxLength + 1] = {};
SavedBlob g_pending_saved_blob;
PrefsBlob g_pending_preferences_blob;
uint32_t g_saved_blob_revision = 0;
uint32_t g_preferences_revision = 0;
std::atomic<bool> g_saved_blob_task_running{false};
std::atomic<bool> g_preferences_task_running{false};

/**
 * @brief 记录 NVS 操作失败信息
 * @param operation 失败的操作名称
 * @param error ESP-IDF 错误码
 */
void LogNvsError(const char* operation, esp_err_t error) {
  LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
             "WLAN NVS %s failed, error=%s\n", operation,
             esp_err_to_name(error));
}

/**
 * @brief 将最新 WLAN 凭据快照串行写入 NVS
 */
void RunSavedNetworksSaveTask() {
  while (true) {
    SavedBlob blob;
    uint32_t revision = 0;
    xSemaphoreTake(g_mutex, portMAX_DELAY);
    blob = g_pending_saved_blob;
    revision = g_saved_blob_revision;
    xSemaphoreGive(g_mutex);

    SaveWifiSavedNetworksToNvs(blob.networks, blob.count);

    xSemaphoreTake(g_mutex, portMAX_DELAY);
    if (revision == g_saved_blob_revision) {
      g_saved_blob_task_running.store(false);
      xSemaphoreGive(g_mutex);
      return;
    }
    xSemaphoreGive(g_mutex);
  }
}

/**
 * @brief 将最新 WLAN 偏好快照串行写入 NVS
 */
void RunPreferencesSaveTask() {
  while (true) {
    PrefsBlob blob;
    uint32_t revision = 0;
    xSemaphoreTake(g_mutex, portMAX_DELAY);
    blob = g_pending_preferences_blob;
    revision = g_preferences_revision;
    xSemaphoreGive(g_mutex);

    nvs_handle_t handle = 0;
    esp_err_t result = nvs_open(kNvsNamespace, NVS_READWRITE, &handle);
    if (result == ESP_OK) {
      result = nvs_set_blob(handle, kWifiPrefsNvsKey, &blob, sizeof(blob));
      if (result == ESP_OK) {
        result = nvs_commit(handle);
      }
      nvs_close(handle);
    }
    if (result != ESP_OK) {
      LogNvsError("save preferences", result);
    }

    xSemaphoreTake(g_mutex, portMAX_DELAY);
    if (revision == g_preferences_revision) {
      g_preferences_task_running.store(false);
      xSemaphoreGive(g_mutex);
      return;
    }
    xSemaphoreGive(g_mutex);
  }
}

}  // namespace

bool SaveWifiSavedNetworksToNvs(
    const WifiSavedNetwork* networks, size_t count) {
  if (networks == nullptr && count > 0) {
    return false;
  }

  SavedBlob blob;
  blob.count = static_cast<uint32_t>(std::min(count, kWifiSavedNetworkCapacity));
  for (size_t i = 0; i < blob.count; ++i) {
    blob.networks[i] = networks[i];
  }

  nvs_handle_t handle = 0;
  esp_err_t result = nvs_open(kNvsNamespace, NVS_READWRITE, &handle);
  if (result != ESP_OK) {
    LogNvsError("open saved networks", result);
    return false;
  }
  result = nvs_set_blob(handle, kWifiSavedNvsKey, &blob, sizeof(blob));
  if (result == ESP_OK) {
    result = nvs_commit(handle);
  }
  nvs_close(handle);
  if (result != ESP_OK) {
    LogNvsError("save networks", result);
  }
  return result == ESP_OK;
}

bool ScheduleWifiSavedNetworksSave(
    const WifiSavedNetwork* networks, size_t count) {
  if (g_mutex == nullptr || (networks == nullptr && count > 0)) {
    return false;
  }

  xSemaphoreTake(g_mutex, portMAX_DELAY);
  g_pending_saved_blob = SavedBlob();
  g_pending_saved_blob.count =
      static_cast<uint32_t>(std::min(count, kWifiSavedNetworkCapacity));
  for (size_t i = 0; i < g_pending_saved_blob.count; ++i) {
    g_pending_saved_blob.networks[i] = networks[i];
  }
  ++g_saved_blob_revision;
  xSemaphoreGive(g_mutex);

  bool expected = false;
  if (!g_saved_blob_task_running.compare_exchange_strong(expected, true)) {
    return true;
  }
  if (!StartStorageTask("wifi_saved_save", RunSavedNetworksSaveTask)) {
    g_saved_blob_task_running.store(false);
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

  nvs_handle_t handle = 0;
  esp_err_t result = nvs_open(kNvsNamespace, NVS_READONLY, &handle);
  if (result == ESP_ERR_NVS_NOT_FOUND) {
    return true;
  }
  if (result != ESP_OK) {
    LogNvsError("open saved networks", result);
    return false;
  }

  SavedBlob blob;
  size_t sz = sizeof(blob);
  result = nvs_get_blob(handle, kWifiSavedNvsKey, &blob, &sz);
  if (result == ESP_ERR_NVS_NOT_FOUND) {
    nvs_close(handle);
    return true;
  }
  if (result == ESP_OK && sz == sizeof(blob) &&
      blob.magic == kWifiSavedMagic) {
    size_t n = std::min<size_t>(blob.count, kWifiSavedNetworkCapacity);
    for (size_t i = 0; i < n && *count < capacity; ++i) {
      if (blob.networks[i].ssid[0] == '\0') {
        continue;
      }
      networks[*count] = blob.networks[i];
      ++(*count);
    }
  } else {
    nvs_close(handle);
    if (result != ESP_OK) {
      LogNvsError("load saved networks", result);
    } else {
      LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
                 "WLAN saved network blob is invalid\n");
    }
    return false;
  }
  nvs_close(handle);
  return true;
}

void InitWifiCache() {
  if (g_mutex == nullptr) {
    g_mutex = xSemaphoreCreateMutex();
  }
  if (g_mutex == nullptr) {
    return;
  }

  nvs_handle_t handle = 0;
  const esp_err_t open_result =
      nvs_open(kNvsNamespace, NVS_READONLY, &handle);
  if (open_result == ESP_ERR_NVS_NOT_FOUND) {
    return;
  }
  if (open_result != ESP_OK) {
    LogNvsError("open preferences", open_result);
    return;
  }

  PrefsBlob blob;
  size_t sz = sizeof(blob);
  const esp_err_t load_result =
      nvs_get_blob(handle, kWifiPrefsNvsKey, &blob, &sz);
  if (load_result == ESP_OK && sz == sizeof(blob) &&
      blob.magic == kWifiPrefsMagic) {
    g_enabled.store(blob.enabled_requested != 0);
    g_preferences_loaded.store(true);
    xSemaphoreTake(g_mutex, portMAX_DELAY);
    std::snprintf(g_auto_connect_ssid, sizeof(g_auto_connect_ssid),
                  "%s", blob.auto_connect_ssid);
    xSemaphoreGive(g_mutex);
  } else if (load_result != ESP_ERR_NVS_NOT_FOUND) {
    if (load_result != ESP_OK) {
      LogNvsError("load preferences", load_result);
    } else {
      LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
                 "WLAN preferences blob is invalid\n");
    }
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

bool HasWifiPreferences() {
  return g_preferences_loaded.load();
}

bool UpdateWifiPreferences(const WifiPreferences& prefs) {
  if (g_mutex == nullptr) {
    return false;
  }

  g_enabled.store(prefs.enabled_requested);
  g_preferences_loaded.store(true);
  xSemaphoreTake(g_mutex, portMAX_DELAY);
  std::snprintf(g_auto_connect_ssid, sizeof(g_auto_connect_ssid),
                "%s", prefs.auto_connect_ssid);
  g_pending_preferences_blob = PrefsBlob();
  g_pending_preferences_blob.enabled_requested =
      prefs.enabled_requested ? 1 : 0;
  std::snprintf(g_pending_preferences_blob.auto_connect_ssid,
                sizeof(g_pending_preferences_blob.auto_connect_ssid), "%s",
                prefs.auto_connect_ssid);
  ++g_preferences_revision;
  xSemaphoreGive(g_mutex);

  bool expected = false;
  if (!g_preferences_task_running.compare_exchange_strong(expected, true)) {
    return true;
  }
  if (!StartStorageTask("wifi_prefs_save", RunPreferencesSaveTask)) {
    g_preferences_task_running.store(false);
    return false;
  }
  return true;
}

}  // namespace lilygo_box::app
