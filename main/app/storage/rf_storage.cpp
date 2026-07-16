/*
 * @Description: SX1262 LoRa 配置列表与唯一激活项持久化实现
 * @Author: LILYGO_L
 * @Date: 2026-07-16 00:00:00
 * @LastEditTime: 2026-07-16 16:24:00
 * @License: GPL 3.0
 */
#include "app/storage/rf_storage.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "nvs.h"

namespace lilygo_box::app {
namespace {

constexpr const char* kNvsNamespace = "settings";
constexpr const char* kNvsKey = "rf_profiles";
constexpr uint32_t kMagic = 0x52465046;
constexpr uint16_t kVersion = 2;

struct LegacyRfProfileV1 {
  uint32_t id = 0;
  char name[kRfProfileNameCapacity] = {};
  uint32_t frequency_hz = 915000000;
  uint32_t bandwidth_hz = 125000;
  uint16_t preamble_length = 8;
  uint8_t spreading_factor = 7;
  uint8_t coding_rate_denominator = 5;
  uint8_t sync_word = 0x12;
  int8_t output_power_dbm = 22;
  bool crc_enabled = true;
  bool invert_iq = false;
  bool rx_boosted = true;
};

struct LegacyRfPreferencesV1 {
  LegacyRfProfileV1 profiles[kRfProfileCapacity] = {};
  size_t profile_count = 0;
  uint32_t active_profile_id = 0;
  uint32_t next_profile_id = 1;
};

struct LegacyBlobV1 {
  uint32_t magic = kMagic;
  uint16_t version = 1;
  uint16_t reserved = 0;
  LegacyRfPreferencesV1 preferences;
};

struct Blob {
  uint32_t magic = kMagic;
  uint16_t version = kVersion;
  uint16_t reserved = 0;
  RfPreferences preferences;
};

RfPreferences CreateDefaultPreferences() {
  RfPreferences preferences;
  RfProfile& profile = preferences.profiles[0];
  profile.id = 1;
  std::snprintf(profile.name, sizeof(profile.name), "LoRa 915 MHz");
  preferences.profile_count = 1;
  preferences.active_profile_id = 1;
  preferences.next_profile_id = 2;
  return preferences;
}

RfPreferences g_preferences = CreateDefaultPreferences();

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

RfPreferences NormalizePreferences(const RfPreferences& source) {
  RfPreferences result = source;
  result.profile_count = std::min(result.profile_count, kRfProfileCapacity);
  uint32_t maximum_id = 0;
  for (size_t index = 0; index < result.profile_count; ++index) {
    RfProfile& profile = result.profiles[index];
    profile.name[kRfProfileNameCapacity - 1] = '\0';
    if (profile.name[0] == '\0') {
      std::snprintf(profile.name, sizeof(profile.name),
          "RF profile %u", static_cast<unsigned>(index + 1));
    }
    if (profile.chip != rf::ChipType::kSx1262) {
      profile.chip = rf::ChipType::kSx1262;
    }
    if (profile.protocol != rf::ProtocolType::kLora) {
      profile.protocol = rf::ProtocolType::kLora;
    }
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
  return result;
}

RfPreferences MigrateV1(const LegacyRfPreferencesV1& legacy) {
  RfPreferences migrated;
  migrated.profile_count = std::min(
      legacy.profile_count, kRfProfileCapacity);
  migrated.active_profile_id = legacy.active_profile_id;
  migrated.next_profile_id = legacy.next_profile_id;
  for (size_t index = 0; index < migrated.profile_count; ++index) {
    const LegacyRfProfileV1& source = legacy.profiles[index];
    RfProfile& target = migrated.profiles[index];
    target.id = source.id;
    std::snprintf(target.name, sizeof(target.name), "%s", source.name);
    target.chip = rf::ChipType::kSx1262;
    target.protocol = rf::ProtocolType::kLora;
    target.frequency_hz = source.frequency_hz;
    target.bandwidth_hz = source.bandwidth_hz;
    target.preamble_length = source.preamble_length;
    target.spreading_factor = source.spreading_factor;
    target.coding_rate_denominator =
        source.coding_rate_denominator;
    target.sync_word = source.sync_word;
    target.output_power_dbm = source.output_power_dbm;
    target.crc_enabled = source.crc_enabled;
    target.invert_iq = source.invert_iq;
    target.rx_boosted = source.rx_boosted;
  }
  return NormalizePreferences(migrated);
}

}  // namespace

void InitRfCache() {
  nvs_handle_t handle = 0;
  if (nvs_open(kNvsNamespace, NVS_READONLY, &handle) != ESP_OK) {
    return;
  }
  size_t size = 0;
  if (nvs_get_blob(handle, kNvsKey, nullptr, &size) == ESP_OK) {
    if (size == sizeof(Blob)) {
      Blob blob;
      if (nvs_get_blob(handle, kNvsKey, &blob, &size) == ESP_OK &&
          blob.magic == kMagic && blob.version == kVersion) {
        g_preferences = NormalizePreferences(blob.preferences);
      }
    } else if (size == sizeof(LegacyBlobV1)) {
      LegacyBlobV1 legacy;
      if (nvs_get_blob(handle, kNvsKey, &legacy, &size) == ESP_OK &&
          legacy.magic == kMagic && legacy.version == 1) {
        g_preferences = MigrateV1(legacy.preferences);
      }
    }
  }
  nvs_close(handle);
}

RfPreferences GetRfPreferences() { return g_preferences; }

bool UpdateRfPreferences(const RfPreferences& preferences) {
  g_preferences = NormalizePreferences(preferences);
  nvs_handle_t handle = 0;
  if (nvs_open(kNvsNamespace, NVS_READWRITE, &handle) != ESP_OK) {
    return false;
  }
  Blob blob;
  blob.preferences = g_preferences;
  const bool result =
      nvs_set_blob(handle, kNvsKey, &blob, sizeof(blob)) == ESP_OK &&
      nvs_commit(handle) == ESP_OK;
  nvs_close(handle);
  return result;
}

}  // namespace lilygo_box::app
