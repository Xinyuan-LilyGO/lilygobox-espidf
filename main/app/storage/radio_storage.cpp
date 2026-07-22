/*
 * @Description: Radio 配置列表与唯一激活项持久化实现
 * @Author: LILYGO_L
 * @Date: 2026-07-16 00:00:00
 * @LastEditTime: 2026-07-22 00:00:00
 * @License: GPL 3.0
 */
#include "app/storage/radio_storage.h"

#include <algorithm>
#include <array>
#include <cstdio>
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

constexpr const char* kRadioProfilesNvsNamespace = "settings";
constexpr const char* kRadioProfilesNvsKey = "radio_profiles";
constexpr size_t kRadioProfileTlvCapacity = 512;
constexpr size_t kRadioProfilesTlvCapacity = 4096;

// 已分配字段编号只允许保留，禁止改号或复用。
enum class RadioProfilesField : uint16_t {
  kActiveProfileId = 1,
  kNextProfileId = 2,
  kProfile = 3,
};

// 子配置字段同样保持永久稳定，未知字段由旧固件跳过。
enum class RadioProfileField : uint16_t {
  kId = 1,
  kName = 2,
  kChip = 3,
  kProtocol = 4,
  kFrequencyHz = 5,
  kBandwidthHz = 6,
  kPreambleLength = 7,
  kSpreadingFactor = 8,
  kCodingRateDenominator = 9,
  kSyncWord = 10,
  kOutputPowerDbm = 11,
  kCrcEnabled = 12,
  kInvertIq = 13,
  kRxBoosted = 14,
  kAntenna = 15,
  kAutoSendEnabled = 16,
  kAutoSendText = 17,
  kAutoSendIntervalMs = 18,
};

void ResetProfile(RadioProfile* profile) {
  if (profile == nullptr) {
    return;
  }
  *profile = RadioProfile{};
}

void ResetPreferences(RadioPreferences* preferences) {
  if (preferences == nullptr) {
    return;
  }
  *preferences = RadioPreferences{};
}

bool HasProfileId(const RadioPreferences& preferences, uint32_t id) {
  for (size_t index = 0; index < preferences.profile_count; ++index) {
    if (preferences.profiles[index].id == id) {
      return true;
    }
  }
  return false;
}

bool HasProfileIdBefore(
    const RadioPreferences& preferences, size_t end, uint32_t id) {
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
    const RadioPreferences& preferences, size_t end, uint32_t start) {
  uint32_t candidate = start == 0 ? 1 : start;
  while (HasProfileIdBefore(preferences, end, candidate)) {
    ++candidate;
    if (candidate == 0) {
      candidate = 1;
    }
  }
  return candidate;
}

template <size_t Capacity>
void NormalizeString(char (&value)[Capacity]) {
  value[Capacity - 1] = '\0';
  size_t length = 0;
  while (length < Capacity && value[length] != '\0') {
    ++length;
  }
  std::fill(value + length + 1, value + Capacity, '\0');
}

void NormalizePreferences(RadioPreferences* preferences) {
  if (preferences == nullptr) {
    return;
  }
  RadioPreferences& result = *preferences;
  result.profile_count = std::min(result.profile_count, kRadioProfileCapacity);
  uint32_t maximum_id = 0;
  for (size_t index = 0; index < result.profile_count; ++index) {
    RadioProfile& profile = result.profiles[index];
    NormalizeString(profile.name);
    NormalizeString(profile.auto_send_text);
    if (profile.name[0] == '\0') {
      std::snprintf(profile.name, sizeof(profile.name),
          "Radio profile %u", static_cast<unsigned>(index + 1));
      NormalizeString(profile.name);
    }
    profile.chip = radio::ChipType::kSx1262;
    profile.protocol = radio::ProtocolType::kLora;
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
    if (profile.antenna != radio::AntennaType::kInternal &&
        profile.antenna != radio::AntennaType::kExternal) {
      profile.antenna = radio::AntennaType::kInternal;
    }
    profile.auto_send_interval_ms = std::clamp(
        profile.auto_send_interval_ms, kRadioAutoSendMinimumIntervalMs,
        kRadioAutoSendMaximumIntervalMs);
    if (profile.auto_send_text[0] == '\0') {
      profile.auto_send_enabled = false;
    }
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
       index < kRadioProfileCapacity; ++index) {
    ResetProfile(&result.profiles[index]);
  }
}

bool RadioProfileEqual(
    const RadioProfile& left, const RadioProfile& right) {
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
      left.rx_boosted == right.rx_boosted &&
      left.antenna == right.antenna &&
      left.auto_send_enabled == right.auto_send_enabled &&
      std::strcmp(left.auto_send_text, right.auto_send_text) == 0 &&
      left.auto_send_interval_ms == right.auto_send_interval_ms;
}

bool RadioPreferencesEqual(
    const RadioPreferences& left, const RadioPreferences& right) {
  if (left.profile_count != right.profile_count ||
      left.active_profile_id != right.active_profile_id ||
      left.next_profile_id != right.next_profile_id) {
    return false;
  }
  for (size_t index = 0; index < left.profile_count; ++index) {
    if (!RadioProfileEqual(left.profiles[index], right.profiles[index])) {
      return false;
    }
  }
  return true;
}

bool EncodeRadioProfile(const RadioProfile& profile,
    uint8_t* output, size_t capacity, size_t* encoded_size) {
  storage::TlvWriter writer(
      storage::TlvDomain::kRadioProfile, output, capacity);
  return writer.WriteUint32(
             static_cast<uint16_t>(RadioProfileField::kId), profile.id) &&
      writer.WriteString(static_cast<uint16_t>(RadioProfileField::kName),
          profile.name, sizeof(profile.name)) &&
      writer.WriteUint8(static_cast<uint16_t>(RadioProfileField::kChip),
          static_cast<uint8_t>(profile.chip)) &&
      writer.WriteUint8(static_cast<uint16_t>(RadioProfileField::kProtocol),
          static_cast<uint8_t>(profile.protocol)) &&
      writer.WriteUint32(
          static_cast<uint16_t>(RadioProfileField::kFrequencyHz),
          profile.frequency_hz) &&
      writer.WriteUint32(
          static_cast<uint16_t>(RadioProfileField::kBandwidthHz),
          profile.bandwidth_hz) &&
      writer.WriteUint16(
          static_cast<uint16_t>(RadioProfileField::kPreambleLength),
          profile.preamble_length) &&
      writer.WriteUint8(
          static_cast<uint16_t>(RadioProfileField::kSpreadingFactor),
          profile.spreading_factor) &&
      writer.WriteUint8(
          static_cast<uint16_t>(
              RadioProfileField::kCodingRateDenominator),
          profile.coding_rate_denominator) &&
      writer.WriteUint8(
          static_cast<uint16_t>(RadioProfileField::kSyncWord),
          profile.sync_word) &&
      writer.WriteInt8(
          static_cast<uint16_t>(RadioProfileField::kOutputPowerDbm),
          profile.output_power_dbm) &&
      writer.WriteBool(
          static_cast<uint16_t>(RadioProfileField::kCrcEnabled),
          profile.crc_enabled) &&
      writer.WriteBool(
          static_cast<uint16_t>(RadioProfileField::kInvertIq),
          profile.invert_iq) &&
      writer.WriteBool(
          static_cast<uint16_t>(RadioProfileField::kRxBoosted),
          profile.rx_boosted) &&
      writer.WriteUint8(
          static_cast<uint16_t>(RadioProfileField::kAntenna),
          static_cast<uint8_t>(profile.antenna)) &&
      writer.WriteBool(
          static_cast<uint16_t>(RadioProfileField::kAutoSendEnabled),
          profile.auto_send_enabled) &&
      writer.WriteString(
          static_cast<uint16_t>(RadioProfileField::kAutoSendText),
          profile.auto_send_text, sizeof(profile.auto_send_text)) &&
      writer.WriteUint32(
          static_cast<uint16_t>(RadioProfileField::kAutoSendIntervalMs),
          profile.auto_send_interval_ms) &&
      writer.Finalize(encoded_size);
}

bool DecodeRadioProfile(
    const uint8_t* data, size_t size, RadioProfile* profile) {
  if (profile == nullptr) {
    return false;
  }
  RadioProfile decoded;
  storage::TlvReader reader(
      storage::TlvDomain::kRadioProfile, data, size);
  storage::TlvField field;
  while (true) {
    const storage::TlvReadResult result = reader.Next(&field);
    if (result == storage::TlvReadResult::kEnd) {
      *profile = decoded;
      return true;
    }
    if (result == storage::TlvReadResult::kInvalid) {
      return false;
    }
    switch (static_cast<RadioProfileField>(field.tag())) {
      case RadioProfileField::kId:
        if (!field.ReadUint32(&decoded.id)) {
          return false;
        }
        break;
      case RadioProfileField::kName:
        if (!field.CopyString(decoded.name, sizeof(decoded.name))) {
          return false;
        }
        break;
      case RadioProfileField::kChip: {
        uint8_t value = 0;
        if (!field.ReadUint8(&value)) {
          return false;
        }
        decoded.chip = static_cast<radio::ChipType>(value);
        break;
      }
      case RadioProfileField::kProtocol: {
        uint8_t value = 0;
        if (!field.ReadUint8(&value)) {
          return false;
        }
        decoded.protocol = static_cast<radio::ProtocolType>(value);
        break;
      }
      case RadioProfileField::kFrequencyHz:
        if (!field.ReadUint32(&decoded.frequency_hz)) {
          return false;
        }
        break;
      case RadioProfileField::kBandwidthHz:
        if (!field.ReadUint32(&decoded.bandwidth_hz)) {
          return false;
        }
        break;
      case RadioProfileField::kPreambleLength:
        if (!field.ReadUint16(&decoded.preamble_length)) {
          return false;
        }
        break;
      case RadioProfileField::kSpreadingFactor:
        if (!field.ReadUint8(&decoded.spreading_factor)) {
          return false;
        }
        break;
      case RadioProfileField::kCodingRateDenominator:
        if (!field.ReadUint8(&decoded.coding_rate_denominator)) {
          return false;
        }
        break;
      case RadioProfileField::kSyncWord:
        if (!field.ReadUint8(&decoded.sync_word)) {
          return false;
        }
        break;
      case RadioProfileField::kOutputPowerDbm:
        if (!field.ReadInt8(&decoded.output_power_dbm)) {
          return false;
        }
        break;
      case RadioProfileField::kCrcEnabled:
        if (!field.ReadBool(&decoded.crc_enabled)) {
          return false;
        }
        break;
      case RadioProfileField::kInvertIq:
        if (!field.ReadBool(&decoded.invert_iq)) {
          return false;
        }
        break;
      case RadioProfileField::kRxBoosted:
        if (!field.ReadBool(&decoded.rx_boosted)) {
          return false;
        }
        break;
      case RadioProfileField::kAntenna: {
        uint8_t value = 0;
        if (!field.ReadUint8(&value)) {
          return false;
        }
        decoded.antenna = static_cast<radio::AntennaType>(value);
        break;
      }
      case RadioProfileField::kAutoSendEnabled:
        if (!field.ReadBool(&decoded.auto_send_enabled)) {
          return false;
        }
        break;
      case RadioProfileField::kAutoSendText:
        if (!field.CopyString(decoded.auto_send_text,
                sizeof(decoded.auto_send_text))) {
          return false;
        }
        break;
      case RadioProfileField::kAutoSendIntervalMs:
        if (!field.ReadUint32(&decoded.auto_send_interval_ms)) {
          return false;
        }
        break;
      default:
        break;
    }
  }
}

bool EncodeRadioPreferences(const RadioPreferences& preferences,
    uint8_t* output, size_t capacity, size_t* encoded_size) {
  storage::TlvWriter writer(
      storage::TlvDomain::kRadioProfiles, output, capacity);
  if (!writer.WriteUint32(
          static_cast<uint16_t>(RadioProfilesField::kActiveProfileId),
          preferences.active_profile_id) ||
      !writer.WriteUint32(
          static_cast<uint16_t>(RadioProfilesField::kNextProfileId),
          preferences.next_profile_id)) {
    return false;
  }
  std::array<uint8_t, kRadioProfileTlvCapacity> profile_buffer = {};
  for (size_t index = 0; index < preferences.profile_count; ++index) {
    size_t profile_size = 0;
    if (!EncodeRadioProfile(preferences.profiles[index],
            profile_buffer.data(), profile_buffer.size(), &profile_size) ||
        !writer.WriteBytes(
            static_cast<uint16_t>(RadioProfilesField::kProfile),
            profile_buffer.data(), profile_size)) {
      return false;
    }
  }
  return writer.Finalize(encoded_size);
}

bool DecodeRadioPreferences(const storage::TlvBuffer& buffer,
    RadioPreferences* preferences) {
  if (preferences == nullptr) {
    return false;
  }
  ResetPreferences(preferences);
  storage::TlvReader reader(storage::TlvDomain::kRadioProfiles,
      buffer.data.get(), buffer.size);
  storage::TlvField field;
  while (true) {
    const storage::TlvReadResult result = reader.Next(&field);
    if (result == storage::TlvReadResult::kEnd) {
      NormalizePreferences(preferences);
      return true;
    }
    if (result == storage::TlvReadResult::kInvalid) {
      return false;
    }
    switch (static_cast<RadioProfilesField>(field.tag())) {
      case RadioProfilesField::kActiveProfileId:
        if (!field.ReadUint32(&preferences->active_profile_id)) {
          return false;
        }
        break;
      case RadioProfilesField::kNextProfileId:
        if (!field.ReadUint32(&preferences->next_profile_id)) {
          return false;
        }
        break;
      case RadioProfilesField::kProfile:
        if (preferences->profile_count >= kRadioProfileCapacity) {
          break;
        }
        if (!DecodeRadioProfile(field.data(), field.size(),
                &preferences->profiles[preferences->profile_count])) {
          return false;
        }
        ++preferences->profile_count;
        break;
      default:
        break;
    }
  }
}

NvsStorageCache<RadioPreferences> g_radio_profiles_cache(
    StorageDomain::kRadioProfiles, RadioPreferencesEqual);

void LogRadioStorageError(const char* operation, esp_err_t error) {
  LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
      "Radio profile NVS %s failed, error=%s\n", operation,
      esp_err_to_name(error));
}

}  // namespace

void InitRadioCache() {
  auto preferences = std::unique_ptr<RadioPreferences>(
      new (std::nothrow) RadioPreferences());
  if (preferences == nullptr) {
    LogMessage(LogLevel::kError, __FILE__, __LINE__,
        "Allocate Radio profile initialization buffer failed\n");
    return;
  }
  nvs_handle_t handle = 0;
  const esp_err_t open_result = OpenApplicationNvs(
      kRadioProfilesNvsNamespace, NVS_READONLY, &handle);
  if (open_result == ESP_OK) {
    storage::TlvBuffer buffer;
    esp_err_t error = ESP_OK;
    const storage::TlvLoadResult result = storage::LoadTlvBuffer(handle,
        kRadioProfilesNvsKey, storage::TlvDomain::kRadioProfiles,
        kRadioProfilesTlvCapacity, &buffer, &error);
    nvs_close(handle);
    if (result == storage::TlvLoadResult::kLoaded &&
        !DecodeRadioPreferences(buffer, preferences.get())) {
      LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
          "Radio profile TLV payload is invalid\n");
      ResetPreferences(preferences.get());
    } else if (result == storage::TlvLoadResult::kInvalid) {
      LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
          "Radio profile TLV container is invalid\n");
    } else if (result == storage::TlvLoadResult::kError) {
      LogRadioStorageError("load", error);
    }
  } else if (open_result != ESP_ERR_NVS_NOT_FOUND) {
    LogRadioStorageError("open", open_result);
  }
  NormalizePreferences(preferences.get());
  if (!g_radio_profiles_cache.Initialize(*preferences)) {
    LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
        "Initialize Radio profile cache failed\n");
  }
}

bool GetRadioPreferences(RadioPreferences* preferences) {
  if (preferences == nullptr) {
    return false;
  }
  if (!g_radio_profiles_cache.Read(preferences)) {
    ResetPreferences(preferences);
    return false;
  }
  return true;
}

bool UpdateRadioPreferences(const RadioPreferences& preferences) {
  auto normalized = std::unique_ptr<RadioPreferences>(
      new (std::nothrow) RadioPreferences(preferences));
  if (normalized == nullptr) {
    return false;
  }
  NormalizePreferences(normalized.get());
  return g_radio_profiles_cache.UpdateAndPersist(*normalized);
}

StorageStageResult StageRadioStorage(nvs_handle_t handle) {
  const RadioPreferences* preferences = nullptr;
  if (!g_radio_profiles_cache.BeginFlush(&preferences)) {
    return StorageStageResult::kClean;
  }
  auto buffer = std::unique_ptr<uint8_t[]>(
      new (std::nothrow) uint8_t[kRadioProfilesTlvCapacity]);
  if (buffer == nullptr) {
    return StorageStageResult::kFailed;
  }
  size_t encoded_size = 0;
  if (!EncodeRadioPreferences(*preferences, buffer.get(),
          kRadioProfilesTlvCapacity, &encoded_size)) {
    return StorageStageResult::kFailed;
  }
  const esp_err_t result = nvs_set_blob(
      handle, kRadioProfilesNvsKey, buffer.get(), encoded_size);
  if (result != ESP_OK) {
    LogRadioStorageError("stage", result);
    return StorageStageResult::kFailed;
  }
  return StorageStageResult::kStaged;
}

void FinishRadioStorage(bool committed) {
  g_radio_profiles_cache.FinishFlush(committed);
}

}  // namespace lilygo_box::app
