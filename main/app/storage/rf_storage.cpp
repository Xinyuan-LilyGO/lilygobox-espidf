/*
 * @Description: SX1262 LoRa 配置列表与唯一激活项持久化实现
 * @Author: LILYGO_L
 * @Date: 2026-07-16 00:00:00
 * @LastEditTime: 2026-07-16 22:38:14
 * @License: GPL 3.0
 */
#include "app/storage/rf_storage.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <memory>
#include <new>

#include "app/storage/storage_internal.h"
#include "base/logger.h"
#include "esp_err.h"
#include "nvs.h"

namespace lilygo_box::app {
namespace {

constexpr const char* kNvsNamespace = "settings";
constexpr const char* kNvsKey = "rf_profiles";
constexpr uint32_t kMagic = 0x52465046;

struct Blob {
  // 校验当前 NVS 数据是否属于 RF 配置。
  uint32_t magic = kMagic;
  // SX1262 LoRa 配置列表及唯一激活项。
  RfPreferences preferences;
};

void ResetProfile(RfProfile* profile) {
  if (profile == nullptr) {
    return;
  }
  profile->id = 0;
  std::fill(profile->name,
      profile->name + kRfProfileNameCapacity, '\0');
  profile->chip = rf::ChipType::kSx1262;
  profile->protocol = rf::ProtocolType::kLora;
  profile->frequency_hz = 915000000;
  profile->bandwidth_hz = 125000;
  profile->preamble_length = 8;
  profile->spreading_factor = 7;
  profile->coding_rate_denominator = 5;
  profile->sync_word = 0x12;
  profile->output_power_dbm = 22;
  profile->crc_enabled = true;
  profile->invert_iq = false;
  profile->rx_boosted = true;
}

void ResetPreferences(RfPreferences* preferences) {
  if (preferences == nullptr) {
    return;
  }
  for (size_t index = 0; index < kRfProfileCapacity; ++index) {
    ResetProfile(&preferences->profiles[index]);
  }
  RfProfile& profile = preferences->profiles[0];
  profile.id = 1;
  std::snprintf(profile.name, sizeof(profile.name), "LoRa 915 MHz");
  preferences->profile_count = 1;
  preferences->active_profile_id = 1;
  preferences->next_profile_id = 2;
}

bool HasProfileId(const RfPreferences& preferences, uint32_t id) {
  for (size_t index = 0; index < preferences.profile_count; ++index) {
    if (preferences.profiles[index].id == id) {
      return true;
    }
  }
  return false;
}

bool HasProfileIdBefore(
    const RfPreferences& preferences, size_t end, uint32_t id) {
  for (size_t index = 0; index < end; ++index) {
    if (preferences.profiles[index].id == id) {
      return true;
    }
  }
  return false;
}

bool IsSupportedBandwidth(uint32_t bandwidth_hz) {
  return bandwidth_hz == 62500 || bandwidth_hz == 125000 ||
      bandwidth_hz == 250000 || bandwidth_hz == 500000;
}

uint32_t NextUnusedProfileId(
    const RfPreferences& preferences, size_t end, uint32_t start) {
  uint32_t candidate = start == 0 ? 1 : start;
  while (HasProfileIdBefore(preferences, end, candidate)) {
    ++candidate;
    if (candidate == 0) {
      candidate = 1;
    }
  }
  return candidate;
}

void NormalizeProfileName(char* name) {
  name[kRfProfileNameCapacity - 1] = '\0';
  size_t length = 0;
  while (length < kRfProfileNameCapacity && name[length] != '\0') {
    ++length;
  }
  std::fill(name + length + 1, name + kRfProfileNameCapacity, '\0');
}

void NormalizePreferences(RfPreferences* preferences) {
  if (preferences == nullptr) {
    return;
  }
  RfPreferences& result = *preferences;
  result.profile_count = std::min(result.profile_count, kRfProfileCapacity);
  uint32_t maximum_id = 0;
  for (size_t index = 0; index < result.profile_count; ++index) {
    RfProfile& profile = result.profiles[index];
    NormalizeProfileName(profile.name);
    if (profile.name[0] == '\0') {
      std::snprintf(profile.name, sizeof(profile.name),
          "RF profile %u", static_cast<unsigned>(index + 1));
      NormalizeProfileName(profile.name);
    }
    profile.chip = rf::ChipType::kSx1262;
    profile.protocol = rf::ProtocolType::kLora;
    if (profile.frequency_hz < 150000000 ||
        profile.frequency_hz > 960000000) {
      profile.frequency_hz = 915000000;
    }
    if (!IsSupportedBandwidth(profile.bandwidth_hz)) {
      profile.bandwidth_hz = 125000;
    }
    if (profile.preamble_length == 0) {
      profile.preamble_length = 8;
    }
    if (profile.spreading_factor < 5 ||
        profile.spreading_factor > 12) {
      profile.spreading_factor = 7;
    }
    if (profile.coding_rate_denominator < 5 ||
        profile.coding_rate_denominator > 8) {
      profile.coding_rate_denominator = 5;
    }
    profile.output_power_dbm = std::clamp<int8_t>(
        profile.output_power_dbm, -9, 22);
    if (profile.id == 0 ||
        HasProfileIdBefore(result, index, profile.id)) {
      profile.id = NextUnusedProfileId(
          result, index, maximum_id + 1);
    }
    maximum_id = std::max(maximum_id, profile.id);
  }
  if (!HasProfileId(result, result.active_profile_id)) {
    result.active_profile_id = 0;
  }
  const uint32_t next_start = std::max(
      result.next_profile_id, maximum_id + 1);
  result.next_profile_id = NextUnusedProfileId(
      result, result.profile_count, next_start);
  for (size_t index = result.profile_count;
       index < kRfProfileCapacity; ++index) {
    ResetProfile(&result.profiles[index]);
  }
}

bool RfProfileEqual(const RfProfile& left, const RfProfile& right) {
  return left.id == right.id &&
      std::strcmp(left.name, right.name) == 0 &&
      left.chip == right.chip && left.protocol == right.protocol &&
      left.frequency_hz == right.frequency_hz &&
      left.bandwidth_hz == right.bandwidth_hz &&
      left.preamble_length == right.preamble_length &&
      left.spreading_factor == right.spreading_factor &&
      left.coding_rate_denominator == right.coding_rate_denominator &&
      left.sync_word == right.sync_word &&
      left.output_power_dbm == right.output_power_dbm &&
      left.crc_enabled == right.crc_enabled &&
      left.invert_iq == right.invert_iq &&
      left.rx_boosted == right.rx_boosted;
}

bool RfPreferencesEqual(
    const RfPreferences& left, const RfPreferences& right) {
  if (left.profile_count != right.profile_count ||
      left.active_profile_id != right.active_profile_id ||
      left.next_profile_id != right.next_profile_id) {
    return false;
  }
  for (size_t index = 0; index < left.profile_count; ++index) {
    if (!RfProfileEqual(left.profiles[index], right.profiles[index])) {
      return false;
    }
  }
  return true;
}

bool BlobEqual(const Blob& left, const Blob& right) {
  return left.magic == right.magic &&
      RfPreferencesEqual(left.preferences, right.preferences);
}

DeferredStorageCache<Blob> g_rf_cache(
    StorageDomain::kRfProfiles, BlobEqual);

void LogRfStorageError(const char* operation, esp_err_t error) {
  LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
      "RF profile NVS %s failed, error=%s\n", operation,
      esp_err_to_name(error));
}

}  // namespace

void InitRfCache() {
  auto blob = std::unique_ptr<Blob>(new (std::nothrow) Blob());
  if (blob == nullptr) {
    LogMessage(LogLevel::kError, __FILE__, __LINE__,
        "Allocate RF profile initialization buffer failed\n");
    return;
  }
  ResetPreferences(&blob->preferences);

  nvs_handle_t handle = 0;
  esp_err_t result = nvs_open(kNvsNamespace, NVS_READONLY, &handle);
  if (result == ESP_OK) {
    size_t size = sizeof(*blob);
    result = nvs_get_blob(handle, kNvsKey, blob.get(), &size);
    nvs_close(handle);
    if (result != ESP_OK || size != sizeof(*blob) ||
        blob->magic != kMagic) {
      if (result != ESP_ERR_NVS_NOT_FOUND) {
        if (result != ESP_OK) {
          LogRfStorageError("load", result);
        } else {
          LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
              "RF profile NVS blob is invalid\n");
        }
      }
      blob->magic = kMagic;
      ResetPreferences(&blob->preferences);
    } else {
      NormalizePreferences(&blob->preferences);
    }
  } else if (result != ESP_ERR_NVS_NOT_FOUND) {
    LogRfStorageError("open", result);
  }

  if (!g_rf_cache.Initialize(*blob)) {
    LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
        "Initialize RF profile cache failed\n");
  }
}

bool GetRfPreferences(RfPreferences* preferences) {
  if (preferences == nullptr) {
    return false;
  }
  auto blob = std::unique_ptr<Blob>(new (std::nothrow) Blob());
  if (blob == nullptr || !g_rf_cache.Read(blob.get())) {
    ResetPreferences(preferences);
    return false;
  }
  *preferences = blob->preferences;
  return true;
}

bool UpdateRfPreferences(const RfPreferences& preferences) {
  auto blob = std::unique_ptr<Blob>(new (std::nothrow) Blob());
  if (blob == nullptr) {
    return false;
  }
  blob->preferences = preferences;
  NormalizePreferences(&blob->preferences);
  return g_rf_cache.Update(*blob);
}

StorageStageResult StageRfStorage(nvs_handle_t handle) {
  const Blob* blob = nullptr;
  if (!g_rf_cache.BeginFlush(&blob)) {
    return StorageStageResult::kClean;
  }

  const esp_err_t result = nvs_set_blob(handle, kNvsKey, blob, sizeof(*blob));
  if (result != ESP_OK) {
    LogRfStorageError("stage", result);
    return StorageStageResult::kFailed;
  }
  return StorageStageResult::kStaged;
}

void FinishRfStorage(bool committed) {
  g_rf_cache.FinishFlush(committed);
}

}  // namespace lilygo_box::app
