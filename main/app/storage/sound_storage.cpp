/**
 * @Description: 声音偏好存储实现
 * @Author: LILYGO_L
 * @Date: 2026-06-25 00:00:00
 * @LastEditTime: 2026-07-22 00:00:00
 * @License: GPL 3.0
 */
#include "app/storage/sound_storage.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

#include "app/storage/storage_internal.h"
#include "app/storage/tlv_storage.h"
#include "base/logger.h"
#include "esp_err.h"
#include "nvs.h"

namespace lilygo_box::app {
namespace {

constexpr const char* kSoundNvsNamespace = "settings";
constexpr const char* kSoundNvsKey = "audio_config";
constexpr size_t kSoundTlvCapacity = 48;

// 已分配字段编号只允许保留，禁止改号或复用。
enum class SoundField : uint16_t {
  kVolumePercent = 1,
};

SoundPreferences NormalizeSoundPreferences(
    const SoundPreferences& source) {
  SoundPreferences result;
  result.volume_percent = std::clamp(source.volume_percent, 0, 100);
  return result;
}

bool AreSoundPreferencesEqual(
    const SoundPreferences& left, const SoundPreferences& right) {
  return left.volume_percent == right.volume_percent;
}

bool DecodeSoundPreferences(const storage::TlvBuffer& buffer,
    SoundPreferences* preferences) {
  if (preferences == nullptr) {
    return false;
  }
  SoundPreferences decoded;
  storage::TlvReader reader(
      storage::TlvDomain::kSound, buffer.data.get(), buffer.size);
  storage::TlvField field;
  while (true) {
    const storage::TlvReadResult result = reader.Next(&field);
    if (result == storage::TlvReadResult::kEnd) {
      *preferences = NormalizeSoundPreferences(decoded);
      return true;
    }
    if (result == storage::TlvReadResult::kInvalid) {
      return false;
    }
    if (static_cast<SoundField>(field.tag()) ==
        SoundField::kVolumePercent) {
      uint8_t value = 0;
      if (!field.ReadUint8(&value)) {
        return false;
      }
      decoded.volume_percent = value;
    }
  }
}

bool EncodeSoundPreferences(const SoundPreferences& preferences,
    uint8_t* output, size_t capacity, size_t* encoded_size) {
  const SoundPreferences normalized =
      NormalizeSoundPreferences(preferences);
  storage::TlvWriter writer(
      storage::TlvDomain::kSound, output, capacity);
  return writer.WriteUint8(
             static_cast<uint16_t>(SoundField::kVolumePercent),
             static_cast<uint8_t>(normalized.volume_percent)) &&
      writer.Finalize(encoded_size);
}

DeferredStorageCache<SoundPreferences> g_sound_cache(
    StorageDomain::kSound, AreSoundPreferencesEqual);

}  // namespace

void InitSoundCache() {
  SoundPreferences loaded;
  nvs_handle_t handle = 0;
  if (OpenApplicationNvs(
          kSoundNvsNamespace, NVS_READONLY, &handle) == ESP_OK) {
    storage::TlvBuffer buffer;
    esp_err_t error = ESP_OK;
    const storage::TlvLoadResult result = storage::LoadTlvBuffer(handle,
        kSoundNvsKey, storage::TlvDomain::kSound,
        kSoundTlvCapacity, &buffer, &error);
    if (result == storage::TlvLoadResult::kLoaded &&
        !DecodeSoundPreferences(buffer, &loaded)) {
      LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
          "Sound TLV payload is invalid\n");
    } else if (result == storage::TlvLoadResult::kInvalid) {
      LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
          "Sound TLV container is invalid\n");
    } else if (result == storage::TlvLoadResult::kError) {
      LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
          "Load sound TLV failed: %s\n", esp_err_to_name(error));
    }
    nvs_close(handle);
  }
  if (!g_sound_cache.Initialize(NormalizeSoundPreferences(loaded))) {
    LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
        "Initialize sound storage cache failed\n");
  }
}

SoundPreferences GetSoundPreferences() {
  SoundPreferences preferences;
  g_sound_cache.Read(&preferences);
  return preferences;
}

bool UpdateSoundPreferences(const SoundPreferences& preferences) {
  return g_sound_cache.Update(
      NormalizeSoundPreferences(preferences));
}

StorageStageResult StageSoundStorage(nvs_handle_t handle) {
  const SoundPreferences* preferences = nullptr;
  if (!g_sound_cache.BeginFlush(&preferences)) {
    return StorageStageResult::kClean;
  }
  std::array<uint8_t, kSoundTlvCapacity> buffer = {};
  size_t encoded_size = 0;
  if (!EncodeSoundPreferences(*preferences, buffer.data(),
          buffer.size(), &encoded_size) ||
      nvs_set_blob(handle, kSoundNvsKey,
          buffer.data(), encoded_size) != ESP_OK) {
    return StorageStageResult::kFailed;
  }
  return StorageStageResult::kStaged;
}

void FinishSoundStorage(bool committed) {
  g_sound_cache.FinishFlush(committed);
}

}  // namespace lilygo_box::app
