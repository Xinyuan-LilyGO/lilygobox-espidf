/**
 * @Description: WLAN 偏好存储实现
 * @Author: LILYGO_L
 * @Date: 2026-06-23 00:00:00
 * @LastEditTime: 2026-07-16 22:36:38
 * @License: GPL 3.0
 */
#include "app/storage/wifi_storage.h"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <memory>
#include <new>

#include "app/storage/storage_internal.h"
#include "base/logger.h"
#include "esp_err.h"
#include "nvs.h"

namespace lilygo_box::app {
namespace {

constexpr const char* kWifiNvsNamespace = "settings";
constexpr const char* kWifiSavedNetworksNvsKey = "wifi_saved";
constexpr const char* kWifiPreferencesNvsKey = "wifi_config";
constexpr uint32_t kWifiSavedNetworksMagic = 0x57494649;
constexpr uint32_t kWifiPreferencesMagic = 0x57465052;
constexpr uint16_t kWifiSavedNetworksSchemaVersion = 1;
constexpr uint16_t kWifiPreferencesSchemaVersion = 1;

struct WifiSavedNetworksBlob {
  // 校验当前 NVS 数据是否属于已保存 WLAN 凭据。
  uint32_t magic = kWifiSavedNetworksMagic;
  // 当前已保存 WLAN 凭据存储结构版本。
  uint16_t schema_version = kWifiSavedNetworksSchemaVersion;
  // 写入 NVS 的完整结构大小。
  uint16_t struct_size = sizeof(WifiSavedNetworksBlob);
  // networks 数组中的有效条目数量。
  uint32_t count = 0;
  // 用户确认连接后保存的 WLAN 凭据列表。
  WifiSavedNetwork networks[kWifiSavedNetworkCapacity] = {};
};

struct WifiPreferencesBlob {
  // 校验当前 NVS 数据是否属于 WLAN 用户偏好。
  uint32_t magic = kWifiPreferencesMagic;
  // 当前 WLAN 用户偏好存储结构版本。
  uint16_t schema_version = kWifiPreferencesSchemaVersion;
  // 写入 NVS 的完整结构大小。
  uint16_t struct_size = sizeof(WifiPreferencesBlob);
  // 用户是否期望启用 WLAN。
  uint8_t enabled_requested = 0;
  // 自动连接目标 SSID，空字符串表示未启用自动连接。
  char auto_connect_ssid[hal::kWifiSsidMaxLength + 1] = {};
};

template <size_t Capacity>
void CopyBoundedString(char (&destination)[Capacity], const char* source) {
  size_t length = 0;
  while (length + 1 < Capacity && source[length] != '\0') {
    destination[length] = source[length];
    ++length;
  }
  destination[length] = '\0';
  for (size_t index = length + 1; index < Capacity; ++index) {
    destination[index] = '\0';
  }
}

bool AreWifiPreferencesBlobsEqual(
    const WifiPreferencesBlob& left,
    const WifiPreferencesBlob& right) {
  return left.magic == right.magic &&
      left.schema_version == right.schema_version &&
      left.struct_size == right.struct_size &&
      left.enabled_requested == right.enabled_requested &&
      std::strcmp(left.auto_connect_ssid,
          right.auto_connect_ssid) == 0;
}

bool AreWifiSavedNetworksEqual(
    const WifiSavedNetwork& left, const WifiSavedNetwork& right) {
  return std::strcmp(left.ssid, right.ssid) == 0 &&
      std::strcmp(left.password, right.password) == 0 &&
      left.secure == right.secure && left.is_5g == right.is_5g &&
      left.rssi == right.rssi;
}

bool AreWifiSavedNetworksBlobsEqual(
    const WifiSavedNetworksBlob& left,
    const WifiSavedNetworksBlob& right) {
  if (left.magic != right.magic ||
      left.schema_version != right.schema_version ||
      left.struct_size != right.struct_size ||
      left.count != right.count) {
    return false;
  }
  for (size_t index = 0; index < left.count; ++index) {
    if (!AreWifiSavedNetworksEqual(
            left.networks[index], right.networks[index])) {
      return false;
    }
  }
  return true;
}

WifiPreferencesBlob NormalizeWifiPreferencesBlob(
    const WifiPreferencesBlob& source) {
  WifiPreferencesBlob normalized = {};
  normalized.enabled_requested = source.enabled_requested == 0 ? 0 : 1;
  CopyBoundedString(
      normalized.auto_connect_ssid, source.auto_connect_ssid);
  return normalized;
}

WifiPreferencesBlob MakeWifiPreferencesBlob(
    const WifiPreferences& preferences) {
  WifiPreferencesBlob blob = {};
  blob.enabled_requested = preferences.enabled_requested ? 1 : 0;
  CopyBoundedString(
      blob.auto_connect_ssid, preferences.auto_connect_ssid);
  return blob;
}

WifiSavedNetwork NormalizeSavedNetwork(const WifiSavedNetwork& source) {
  WifiSavedNetwork normalized = {};
  CopyBoundedString(normalized.ssid, source.ssid);
  CopyBoundedString(normalized.password, source.password);
  normalized.secure = source.secure;
  normalized.is_5g = source.is_5g;
  normalized.rssi = source.rssi;
  return normalized;
}

void MakeWifiSavedNetworksBlob(const WifiSavedNetwork* networks, size_t count,
    WifiSavedNetworksBlob* blob) {
  if (blob == nullptr) {
    return;
  }
  blob->magic = kWifiSavedNetworksMagic;
  blob->schema_version = kWifiSavedNetworksSchemaVersion;
  blob->struct_size = sizeof(WifiSavedNetworksBlob);
  blob->count = 0;
  const size_t bounded_count =
      std::min(count, kWifiSavedNetworkCapacity);
  for (size_t index = 0; index < bounded_count; ++index) {
    const WifiSavedNetwork network = NormalizeSavedNetwork(networks[index]);
    if (network.ssid[0] == '\0') {
      continue;
    }
    blob->networks[blob->count] = network;
    ++blob->count;
  }
  for (size_t index = blob->count;
       index < kWifiSavedNetworkCapacity; ++index) {
    blob->networks[index] = WifiSavedNetwork();
  }
}

void NormalizeWifiSavedNetworksBlob(WifiSavedNetworksBlob* blob) {
  if (blob == nullptr) {
    return;
  }
  const size_t input_count =
      std::min<size_t>(blob->count, kWifiSavedNetworkCapacity);
  size_t output_count = 0;
  for (size_t index = 0; index < input_count; ++index) {
    const WifiSavedNetwork network =
        NormalizeSavedNetwork(blob->networks[index]);
    if (network.ssid[0] == '\0') {
      continue;
    }
    blob->networks[output_count] = network;
    ++output_count;
  }
  for (size_t index = output_count;
       index < kWifiSavedNetworkCapacity; ++index) {
    blob->networks[index] = WifiSavedNetwork();
  }
  blob->magic = kWifiSavedNetworksMagic;
  blob->schema_version = kWifiSavedNetworksSchemaVersion;
  blob->struct_size = sizeof(WifiSavedNetworksBlob);
  blob->count = static_cast<uint32_t>(output_count);
}

DeferredStorageCache<WifiPreferencesBlob> g_wifi_preferences_cache(
    StorageDomain::kWifiPreferences, AreWifiPreferencesBlobsEqual);
DeferredStorageCache<WifiSavedNetworksBlob> g_wifi_saved_networks_cache(
    StorageDomain::kWifiSavedNetworks, AreWifiSavedNetworksBlobsEqual);
std::atomic<bool> g_wifi_preferences_loaded{false};

void LogWifiStorageError(const char* operation, esp_err_t error) {
  LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
      "WLAN NVS %s failed, error=%s\n", operation,
      esp_err_to_name(error));
}

void LoadWifiPreferencesBlob(nvs_handle_t handle,
    WifiPreferencesBlob* preferences,
    bool* loaded) {
  if (preferences == nullptr || loaded == nullptr) {
    return;
  }
  WifiPreferencesBlob stored = {};
  size_t size = sizeof(stored);
  const esp_err_t result =
      nvs_get_blob(handle, kWifiPreferencesNvsKey, &stored, &size);
  if (result == ESP_OK && size == sizeof(stored) &&
      stored.magic == kWifiPreferencesMagic &&
      stored.schema_version == kWifiPreferencesSchemaVersion &&
      stored.struct_size == sizeof(WifiPreferencesBlob)) {
    *preferences = NormalizeWifiPreferencesBlob(stored);
    *loaded = true;
    return;
  }
  if (result == ESP_ERR_NVS_NOT_FOUND) {
    return;
  }
  if (result != ESP_OK) {
    LogWifiStorageError("load preferences", result);
  } else {
    LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
        "WLAN preferences blob is invalid\n");
  }
}

void LoadWifiSavedNetworksBlob(
    nvs_handle_t handle, WifiSavedNetworksBlob* saved_networks) {
  if (saved_networks == nullptr) {
    return;
  }
  MakeWifiSavedNetworksBlob(nullptr, 0, saved_networks);
  size_t size = sizeof(*saved_networks);
  const esp_err_t result =
      nvs_get_blob(
          handle, kWifiSavedNetworksNvsKey, saved_networks, &size);
  if (result == ESP_OK && size == sizeof(*saved_networks) &&
      saved_networks->magic == kWifiSavedNetworksMagic &&
      saved_networks->schema_version == kWifiSavedNetworksSchemaVersion &&
      saved_networks->struct_size == sizeof(WifiSavedNetworksBlob)) {
    NormalizeWifiSavedNetworksBlob(saved_networks);
    return;
  }
  MakeWifiSavedNetworksBlob(nullptr, 0, saved_networks);
  if (result == ESP_ERR_NVS_NOT_FOUND) {
    return;
  }
  if (result != ESP_OK) {
    LogWifiStorageError("load saved networks", result);
  } else {
    LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
        "WLAN saved network blob is invalid\n");
  }
}

}  // namespace

bool UpdateWifiSavedNetworks(
    const WifiSavedNetwork* networks, size_t count) {
  if (networks == nullptr && count > 0) {
    return false;
  }
  auto blob = std::unique_ptr<WifiSavedNetworksBlob>(
      new (std::nothrow) WifiSavedNetworksBlob());
  if (blob == nullptr) {
    return false;
  }
  MakeWifiSavedNetworksBlob(networks, count, blob.get());
  return g_wifi_saved_networks_cache.Update(*blob);
}

bool GetWifiSavedNetworks(
    WifiSavedNetwork* networks, size_t capacity, size_t* count) {
  if (networks == nullptr || count == nullptr) {
    return false;
  }
  *count = 0;

  auto blob = std::unique_ptr<WifiSavedNetworksBlob>(
      new (std::nothrow) WifiSavedNetworksBlob());
  if (blob == nullptr || !g_wifi_saved_networks_cache.Read(blob.get())) {
    return false;
  }
  const size_t output_count =
      std::min<size_t>(blob->count, capacity);
  for (size_t index = 0; index < output_count; ++index) {
    networks[index] = blob->networks[index];
  }
  *count = output_count;
  return true;
}

void InitWifiCache() {
  WifiPreferencesBlob preferences = {};
  auto saved_networks = std::unique_ptr<WifiSavedNetworksBlob>(
      new (std::nothrow) WifiSavedNetworksBlob());
  if (saved_networks == nullptr) {
    LogMessage(LogLevel::kError, __FILE__, __LINE__,
        "Allocate WLAN saved network initialization buffer failed\n");
  }
  bool preferences_loaded = false;

  nvs_handle_t handle = 0;
  const esp_err_t open_result =
      nvs_open(kWifiNvsNamespace, NVS_READONLY, &handle);
  if (open_result == ESP_OK) {
    LoadWifiPreferencesBlob(handle, &preferences, &preferences_loaded);
    if (saved_networks != nullptr) {
      LoadWifiSavedNetworksBlob(handle, saved_networks.get());
    }
    nvs_close(handle);
  } else if (open_result != ESP_ERR_NVS_NOT_FOUND) {
    LogWifiStorageError("open cache", open_result);
  }

  if (!preferences_loaded) {
    preferences.magic = 0;
  }
  const bool preferences_initialized =
      g_wifi_preferences_cache.Initialize(preferences);
  if (!preferences_initialized) {
    LogMessage(LogLevel::kError, __FILE__, __LINE__,
        "Initialize WLAN preferences cache failed\n");
  }
  if (saved_networks != nullptr &&
      !g_wifi_saved_networks_cache.Initialize(*saved_networks)) {
    LogMessage(LogLevel::kError, __FILE__, __LINE__,
        "Initialize WLAN saved network cache failed\n");
  }
  g_wifi_preferences_loaded.store(preferences_loaded);
}

WifiPreferences GetWifiPreferences() {
  WifiPreferencesBlob blob = {};
  if (!g_wifi_preferences_cache.Read(&blob)) {
    return WifiPreferences{};
  }
  WifiPreferences preferences;
  preferences.enabled_requested = blob.enabled_requested != 0;
  CopyBoundedString(
      preferences.auto_connect_ssid, blob.auto_connect_ssid);
  return preferences;
}

bool HasWifiPreferences() {
  return g_wifi_preferences_loaded.load();
}

bool UpdateWifiPreferences(const WifiPreferences& preferences) {
  if (!g_wifi_preferences_cache.Update(
          MakeWifiPreferencesBlob(preferences))) {
    return false;
  }
  g_wifi_preferences_loaded.store(true);
  return true;
}

StorageStageResult StageWifiPreferencesStorage(nvs_handle_t handle) {
  const WifiPreferencesBlob* blob = nullptr;
  if (!g_wifi_preferences_cache.BeginFlush(&blob)) {
    return StorageStageResult::kClean;
  }
  const esp_err_t result =
      nvs_set_blob(handle, kWifiPreferencesNvsKey, blob, sizeof(*blob));
  if (result == ESP_OK) {
    return StorageStageResult::kStaged;
  }
  LogWifiStorageError("stage preferences", result);
  return StorageStageResult::kFailed;
}

void FinishWifiPreferencesStorage(bool committed) {
  g_wifi_preferences_cache.FinishFlush(committed);
}

StorageStageResult StageWifiSavedNetworksStorage(nvs_handle_t handle) {
  const WifiSavedNetworksBlob* blob = nullptr;
  if (!g_wifi_saved_networks_cache.BeginFlush(&blob)) {
    return StorageStageResult::kClean;
  }
  const esp_err_t result =
      nvs_set_blob(
          handle, kWifiSavedNetworksNvsKey, blob, sizeof(*blob));
  if (result == ESP_OK) {
    return StorageStageResult::kStaged;
  }
  LogWifiStorageError("stage saved networks", result);
  return StorageStageResult::kFailed;
}

void FinishWifiSavedNetworksStorage(bool committed) {
  g_wifi_saved_networks_cache.FinishFlush(committed);
}

}  // namespace lilygo_box::app
