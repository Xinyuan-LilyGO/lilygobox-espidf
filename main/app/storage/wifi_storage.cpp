/**
 * @Description: WLAN 偏好存储实现
 * @Author: LILYGO_L
 * @Date: 2026-06-23 00:00:00
 * @LastEditTime: 2026-07-22 00:00:00
 * @License: GPL 3.0
 */
#include "app/storage/wifi_storage.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <memory>
#include <new>

#include "app/storage/storage_internal.h"
#include "app/storage/tlv_storage.h"
#include "base/logger.h"
#include "esp_err.h"
#include "nvs.h"

namespace lilygo_box::app {
namespace {

constexpr const char* kWifiNvsNamespace = "settings";
constexpr const char* kWifiSavedNetworksNvsKey = "wifi_saved";
constexpr const char* kWifiPreferencesNvsKey = "wifi_config";
constexpr size_t kWifiPreferencesTlvCapacity = 128;
constexpr size_t kWifiSavedNetworkTlvCapacity = 256;
constexpr size_t kWifiSavedNetworksTlvCapacity = 3072;

// 已分配字段编号只允许保留，禁止改号或复用。
enum class WifiPreferencesField : uint16_t {
  kEnabledRequested = 1,
  kAutoConnectSsid = 2,
};

enum class WifiSavedNetworksField : uint16_t {
  kNetwork = 1,
};

enum class WifiSavedNetworkField : uint16_t {
  kSsid = 1,
  kPassword = 2,
  kSecure = 3,
  kIs5g = 4,
};

struct WifiSavedNetworks {
  WifiSavedNetwork networks[kWifiSavedNetworkCapacity] = {};
  size_t count = 0;
};

struct WifiPreferencesState {
  WifiPreferences preferences;
  // 区分“从未保存”与“用户明确保存了默认值”。
  bool has_value = false;
};

/**
 * @brief 清空已保存 WLAN 列表且不创建大型聚合临时对象
 * @param networks 待清空的 WLAN 列表
 */
void ResetWifiSavedNetworks(WifiSavedNetworks* networks) {
  if (networks == nullptr) {
    return;
  }
  for (WifiSavedNetwork& network : networks->networks) {
    std::fill(network.ssid, network.ssid + sizeof(network.ssid), '\0');
    std::fill(network.password,
        network.password + sizeof(network.password), '\0');
    network.secure = false;
    network.is_5g = false;
    network.rssi = kWifiUnknownRssi;
  }
  networks->count = 0;
}

template <size_t Capacity>
void CopyBoundedString(char (&destination)[Capacity], const char* source) {
  size_t length = 0;
  while (length + 1 < Capacity && source[length] != '\0') {
    destination[length] = source[length];
    ++length;
  }
  destination[length] = '\0';
  std::fill(destination + length + 1, destination + Capacity, '\0');
}

WifiPreferences NormalizeWifiPreferences(
    const WifiPreferences& source) {
  WifiPreferences normalized;
  normalized.enabled_requested = source.enabled_requested;
  CopyBoundedString(
      normalized.auto_connect_ssid, source.auto_connect_ssid);
  return normalized;
}

WifiSavedNetwork NormalizeSavedNetwork(const WifiSavedNetwork& source) {
  WifiSavedNetwork normalized;
  CopyBoundedString(normalized.ssid, source.ssid);
  CopyBoundedString(normalized.password, source.password);
  normalized.secure = source.secure;
  normalized.is_5g = source.is_5g;
  normalized.rssi = source.rssi;
  return normalized;
}

void MakeWifiSavedNetworks(const WifiSavedNetwork* networks, size_t count,
    WifiSavedNetworks* result) {
  if (result == nullptr) {
    return;
  }
  ResetWifiSavedNetworks(result);
  const size_t bounded_count =
      std::min(count, kWifiSavedNetworkCapacity);
  for (size_t index = 0; index < bounded_count; ++index) {
    const WifiSavedNetwork network = NormalizeSavedNetwork(networks[index]);
    if (network.ssid[0] == '\0') {
      continue;
    }
    result->networks[result->count] = network;
    ++result->count;
  }
}

bool AreWifiPreferencesEqual(
    const WifiPreferences& left, const WifiPreferences& right) {
  return left.enabled_requested == right.enabled_requested &&
      std::strcmp(left.auto_connect_ssid,
          right.auto_connect_ssid) == 0;
}

bool AreWifiPreferenceStatesEqual(
    const WifiPreferencesState& left,
    const WifiPreferencesState& right) {
  return left.has_value == right.has_value &&
      AreWifiPreferencesEqual(left.preferences, right.preferences);
}

bool AreWifiSavedNetworksEqual(
    const WifiSavedNetwork& left, const WifiSavedNetwork& right) {
  return std::strcmp(left.ssid, right.ssid) == 0 &&
      std::strcmp(left.password, right.password) == 0 &&
      left.secure == right.secure && left.is_5g == right.is_5g;
}

bool AreWifiSavedNetworkListsEqual(
    const WifiSavedNetworks& left, const WifiSavedNetworks& right) {
  if (left.count != right.count) {
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

bool EncodeWifiPreferences(const WifiPreferences& preferences,
    uint8_t* output, size_t capacity, size_t* encoded_size) {
  const WifiPreferences normalized = NormalizeWifiPreferences(preferences);
  storage::TlvWriter writer(
      storage::TlvDomain::kWifiPreferences, output, capacity);
  return writer.WriteBool(
             static_cast<uint16_t>(
                 WifiPreferencesField::kEnabledRequested),
             normalized.enabled_requested) &&
      writer.WriteString(
          static_cast<uint16_t>(WifiPreferencesField::kAutoConnectSsid),
          normalized.auto_connect_ssid,
          sizeof(normalized.auto_connect_ssid)) &&
      writer.Finalize(encoded_size);
}

bool DecodeWifiPreferences(const storage::TlvBuffer& buffer,
    WifiPreferences* preferences) {
  if (preferences == nullptr) {
    return false;
  }
  WifiPreferences decoded;
  storage::TlvReader reader(storage::TlvDomain::kWifiPreferences,
      buffer.data.get(), buffer.size);
  storage::TlvField field;
  while (true) {
    const storage::TlvReadResult result = reader.Next(&field);
    if (result == storage::TlvReadResult::kEnd) {
      *preferences = NormalizeWifiPreferences(decoded);
      return true;
    }
    if (result == storage::TlvReadResult::kInvalid) {
      return false;
    }
    switch (static_cast<WifiPreferencesField>(field.tag())) {
      case WifiPreferencesField::kEnabledRequested:
        if (!field.ReadBool(&decoded.enabled_requested)) {
          return false;
        }
        break;
      case WifiPreferencesField::kAutoConnectSsid:
        if (!field.CopyString(decoded.auto_connect_ssid,
                sizeof(decoded.auto_connect_ssid))) {
          return false;
        }
        break;
      default:
        break;
    }
  }
}

bool EncodeWifiSavedNetwork(const WifiSavedNetwork& network,
    uint8_t* output, size_t capacity, size_t* encoded_size) {
  const WifiSavedNetwork normalized = NormalizeSavedNetwork(network);
  storage::TlvWriter writer(
      storage::TlvDomain::kWifiSavedNetwork, output, capacity);
  return writer.WriteString(
             static_cast<uint16_t>(WifiSavedNetworkField::kSsid),
             normalized.ssid, sizeof(normalized.ssid)) &&
      writer.WriteString(
          static_cast<uint16_t>(WifiSavedNetworkField::kPassword),
          normalized.password, sizeof(normalized.password)) &&
      writer.WriteBool(
          static_cast<uint16_t>(WifiSavedNetworkField::kSecure),
          normalized.secure) &&
      writer.WriteBool(
          static_cast<uint16_t>(WifiSavedNetworkField::kIs5g),
          normalized.is_5g) &&
      writer.Finalize(encoded_size);
}

bool DecodeWifiSavedNetwork(
    const uint8_t* data, size_t size, WifiSavedNetwork* network) {
  if (network == nullptr) {
    return false;
  }
  WifiSavedNetwork decoded;
  storage::TlvReader reader(
      storage::TlvDomain::kWifiSavedNetwork, data, size);
  storage::TlvField field;
  while (true) {
    const storage::TlvReadResult result = reader.Next(&field);
    if (result == storage::TlvReadResult::kEnd) {
      *network = NormalizeSavedNetwork(decoded);
      return true;
    }
    if (result == storage::TlvReadResult::kInvalid) {
      return false;
    }
    switch (static_cast<WifiSavedNetworkField>(field.tag())) {
      case WifiSavedNetworkField::kSsid:
        if (!field.CopyString(decoded.ssid, sizeof(decoded.ssid))) {
          return false;
        }
        break;
      case WifiSavedNetworkField::kPassword:
        if (!field.CopyString(
                decoded.password, sizeof(decoded.password))) {
          return false;
        }
        break;
      case WifiSavedNetworkField::kSecure:
        if (!field.ReadBool(&decoded.secure)) {
          return false;
        }
        break;
      case WifiSavedNetworkField::kIs5g:
        if (!field.ReadBool(&decoded.is_5g)) {
          return false;
        }
        break;
      default:
        break;
    }
  }
}

bool EncodeWifiSavedNetworks(const WifiSavedNetworks& networks,
    uint8_t* output, size_t capacity, size_t* encoded_size) {
  storage::TlvWriter writer(
      storage::TlvDomain::kWifiSavedNetworks, output, capacity);
  std::array<uint8_t, kWifiSavedNetworkTlvCapacity> network_buffer = {};
  for (size_t index = 0; index < networks.count; ++index) {
    size_t network_size = 0;
    if (!EncodeWifiSavedNetwork(networks.networks[index],
            network_buffer.data(), network_buffer.size(), &network_size) ||
        !writer.WriteBytes(
            static_cast<uint16_t>(WifiSavedNetworksField::kNetwork),
            network_buffer.data(), network_size)) {
      return false;
    }
  }
  return writer.Finalize(encoded_size);
}

bool DecodeWifiSavedNetworks(const storage::TlvBuffer& buffer,
    WifiSavedNetworks* networks) {
  if (networks == nullptr) {
    return false;
  }
  ResetWifiSavedNetworks(networks);
  storage::TlvReader reader(storage::TlvDomain::kWifiSavedNetworks,
      buffer.data.get(), buffer.size);
  storage::TlvField field;
  while (true) {
    const storage::TlvReadResult result = reader.Next(&field);
    if (result == storage::TlvReadResult::kEnd) {
      return true;
    }
    if (result == storage::TlvReadResult::kInvalid) {
      return false;
    }
    if (static_cast<WifiSavedNetworksField>(field.tag()) !=
        WifiSavedNetworksField::kNetwork ||
        networks->count >= kWifiSavedNetworkCapacity) {
      continue;
    }
    WifiSavedNetwork network;
    if (!DecodeWifiSavedNetwork(field.data(), field.size(), &network)) {
      return false;
    }
    if (network.ssid[0] == '\0') {
      continue;
    }
    networks->networks[networks->count] = network;
    ++networks->count;
  }
}

NvsStorageCache<WifiPreferencesState> g_wifi_preferences_cache(
    StorageDomain::kWifiPreferences, AreWifiPreferenceStatesEqual);
NvsStorageCache<WifiSavedNetworks> g_wifi_saved_networks_cache(
    StorageDomain::kWifiSavedNetworks, AreWifiSavedNetworkListsEqual);

void LogWifiStorageError(const char* operation, esp_err_t error) {
  LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
      "WLAN NVS %s failed, error=%s\n", operation,
      esp_err_to_name(error));
}

}  // namespace

bool UpdateWifiSavedNetworks(
    const WifiSavedNetwork* networks, size_t count) {
  if (networks == nullptr && count > 0) {
    return false;
  }
  auto normalized = std::unique_ptr<WifiSavedNetworks>(
      new (std::nothrow) WifiSavedNetworks());
  if (normalized == nullptr) {
    return false;
  }
  MakeWifiSavedNetworks(networks, count, normalized.get());
  return g_wifi_saved_networks_cache.UpdateAndPersist(*normalized);
}

bool GetWifiSavedNetworks(
    WifiSavedNetwork* networks, size_t capacity, size_t* count) {
  if (networks == nullptr || count == nullptr) {
    return false;
  }
  *count = 0;
  auto stored = std::unique_ptr<WifiSavedNetworks>(
      new (std::nothrow) WifiSavedNetworks());
  if (stored == nullptr || !g_wifi_saved_networks_cache.Read(stored.get())) {
    return false;
  }
  const size_t output_count = std::min(stored->count, capacity);
  for (size_t index = 0; index < output_count; ++index) {
    networks[index] = stored->networks[index];
  }
  *count = output_count;
  return true;
}

void InitWifiCache() {
  WifiPreferences preferences;
  auto saved_networks = std::unique_ptr<WifiSavedNetworks>(
      new (std::nothrow) WifiSavedNetworks());
  if (saved_networks == nullptr) {
    LogMessage(LogLevel::kError, __FILE__, __LINE__,
        "Allocate WLAN saved network initialization buffer failed\n");
  }
  bool preferences_loaded = false;

  nvs_handle_t handle = 0;
  const esp_err_t open_result = OpenApplicationNvs(
      kWifiNvsNamespace, NVS_READONLY, &handle);
  if (open_result == ESP_OK) {
    storage::TlvBuffer preferences_buffer;
    esp_err_t preferences_error = ESP_OK;
    const storage::TlvLoadResult preferences_result =
        storage::LoadTlvBuffer(handle, kWifiPreferencesNvsKey,
            storage::TlvDomain::kWifiPreferences,
            kWifiPreferencesTlvCapacity, &preferences_buffer,
            &preferences_error);
    if (preferences_result == storage::TlvLoadResult::kLoaded) {
      preferences_loaded =
          DecodeWifiPreferences(preferences_buffer, &preferences);
      if (!preferences_loaded) {
        LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
            "WLAN preferences TLV payload is invalid\n");
      }
    } else if (preferences_result == storage::TlvLoadResult::kInvalid) {
      LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
          "WLAN preferences TLV container is invalid\n");
    } else if (preferences_result == storage::TlvLoadResult::kError) {
      LogWifiStorageError("load preferences", preferences_error);
    }

    if (saved_networks != nullptr) {
      storage::TlvBuffer networks_buffer;
      esp_err_t networks_error = ESP_OK;
      const storage::TlvLoadResult networks_result =
          storage::LoadTlvBuffer(handle, kWifiSavedNetworksNvsKey,
              storage::TlvDomain::kWifiSavedNetworks,
              kWifiSavedNetworksTlvCapacity, &networks_buffer,
              &networks_error);
      if (networks_result == storage::TlvLoadResult::kLoaded &&
          !DecodeWifiSavedNetworks(networks_buffer,
              saved_networks.get())) {
        LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
            "WLAN saved networks TLV payload is invalid\n");
        ResetWifiSavedNetworks(saved_networks.get());
      } else if (networks_result == storage::TlvLoadResult::kInvalid) {
        LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
            "WLAN saved networks TLV container is invalid\n");
      } else if (networks_result == storage::TlvLoadResult::kError) {
        LogWifiStorageError("load saved networks", networks_error);
      }
    }
    nvs_close(handle);
  } else if (open_result != ESP_ERR_NVS_NOT_FOUND) {
    LogWifiStorageError("open cache", open_result);
  }

  WifiPreferencesState preferences_state;
  preferences_state.preferences = NormalizeWifiPreferences(preferences);
  preferences_state.has_value = preferences_loaded;
  if (!g_wifi_preferences_cache.Initialize(preferences_state)) {
    LogMessage(LogLevel::kError, __FILE__, __LINE__,
        "Initialize WLAN preferences cache failed\n");
  }
  if (saved_networks != nullptr &&
      !g_wifi_saved_networks_cache.Initialize(*saved_networks)) {
    LogMessage(LogLevel::kError, __FILE__, __LINE__,
        "Initialize WLAN saved network cache failed\n");
  }
}

WifiPreferences GetWifiPreferences() {
  WifiPreferencesState state;
  if (!g_wifi_preferences_cache.Read(&state)) {
    return WifiPreferences{};
  }
  return state.preferences;
}

bool HasWifiPreferences() {
  WifiPreferencesState state;
  return g_wifi_preferences_cache.Read(&state) && state.has_value;
}

bool UpdateWifiPreferences(const WifiPreferences& preferences) {
  WifiPreferencesState state;
  state.preferences = NormalizeWifiPreferences(preferences);
  state.has_value = true;
  return g_wifi_preferences_cache.UpdateAndPersist(state);
}

StorageStageResult StageWifiPreferencesStorage(nvs_handle_t handle) {
  const WifiPreferencesState* state = nullptr;
  if (!g_wifi_preferences_cache.BeginFlush(&state)) {
    return StorageStageResult::kClean;
  }
  std::array<uint8_t, kWifiPreferencesTlvCapacity> buffer = {};
  size_t encoded_size = 0;
  if (!EncodeWifiPreferences(state->preferences, buffer.data(),
          buffer.size(), &encoded_size)) {
    return StorageStageResult::kFailed;
  }
  const esp_err_t result = nvs_set_blob(handle,
      kWifiPreferencesNvsKey, buffer.data(), encoded_size);
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
  const WifiSavedNetworks* networks = nullptr;
  if (!g_wifi_saved_networks_cache.BeginFlush(&networks)) {
    return StorageStageResult::kClean;
  }
  auto buffer = std::unique_ptr<uint8_t[]>(
      new (std::nothrow) uint8_t[kWifiSavedNetworksTlvCapacity]);
  if (buffer == nullptr) {
    return StorageStageResult::kFailed;
  }
  size_t encoded_size = 0;
  if (!EncodeWifiSavedNetworks(*networks, buffer.get(),
          kWifiSavedNetworksTlvCapacity, &encoded_size)) {
    return StorageStageResult::kFailed;
  }
  const esp_err_t result = nvs_set_blob(
      handle, kWifiSavedNetworksNvsKey, buffer.get(), encoded_size);
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
