/**
 * @Description: 振动偏好存储实现
 * @Author: LILYGO_L
 * @Date: 2026-06-25 00:00:00
 * @LastEditTime: 2026-07-22 00:00:00
 * @License: GPL 3.0
 */
#include "app/storage/haptic_storage.h"

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

constexpr const char* kHapticNvsNamespace = "settings";
constexpr const char* kHapticNvsKey = "haptic_config";
constexpr size_t kHapticTlvCapacity = 64;

// 已分配字段编号只允许保留，禁止改号或复用。
enum class HapticField : uint16_t {
  kEnabled = 1,
  kStrengthPercent = 2,
};

HapticPreferences NormalizeHapticPreferences(
    const HapticPreferences& source) {
  HapticPreferences result;
  result.enabled = source.enabled;
  result.strength_percent =
      std::clamp(source.strength_percent, 0, 100);
  return result;
}

bool AreHapticPreferencesEqual(
    const HapticPreferences& left, const HapticPreferences& right) {
  return left.enabled == right.enabled &&
      left.strength_percent == right.strength_percent;
}

bool DecodeHapticPreferences(const storage::TlvBuffer& buffer,
    HapticPreferences* preferences) {
  if (preferences == nullptr) {
    return false;
  }
  HapticPreferences decoded;
  storage::TlvReader reader(
      storage::TlvDomain::kHaptic, buffer.data.get(), buffer.size);
  storage::TlvField field;
  while (true) {
    const storage::TlvReadResult result = reader.Next(&field);
    if (result == storage::TlvReadResult::kEnd) {
      *preferences = NormalizeHapticPreferences(decoded);
      return true;
    }
    if (result == storage::TlvReadResult::kInvalid) {
      return false;
    }
    switch (static_cast<HapticField>(field.tag())) {
      case HapticField::kEnabled:
        if (!field.ReadBool(&decoded.enabled)) {
          return false;
        }
        break;
      case HapticField::kStrengthPercent: {
        uint8_t value = 0;
        if (!field.ReadUint8(&value)) {
          return false;
        }
        decoded.strength_percent = value;
        break;
      }
      default:
        break;
    }
  }
}

bool EncodeHapticPreferences(const HapticPreferences& preferences,
    uint8_t* output, size_t capacity, size_t* encoded_size) {
  const HapticPreferences normalized =
      NormalizeHapticPreferences(preferences);
  storage::TlvWriter writer(
      storage::TlvDomain::kHaptic, output, capacity);
  return writer.WriteBool(
             static_cast<uint16_t>(HapticField::kEnabled),
             normalized.enabled) &&
      writer.WriteUint8(
          static_cast<uint16_t>(HapticField::kStrengthPercent),
          static_cast<uint8_t>(normalized.strength_percent)) &&
      writer.Finalize(encoded_size);
}

DeferredStorageCache<HapticPreferences> g_haptic_cache(
    StorageDomain::kHaptic, AreHapticPreferencesEqual);

}  // namespace

void InitHapticCache() {
  HapticPreferences loaded;
  nvs_handle_t handle = 0;
  if (nvs_open(kHapticNvsNamespace, NVS_READONLY, &handle) == ESP_OK) {
    storage::TlvBuffer buffer;
    esp_err_t error = ESP_OK;
    const storage::TlvLoadResult result = storage::LoadTlvBuffer(handle,
        kHapticNvsKey, storage::TlvDomain::kHaptic,
        kHapticTlvCapacity, &buffer, &error);
    if (result == storage::TlvLoadResult::kLoaded &&
        !DecodeHapticPreferences(buffer, &loaded)) {
      LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
          "Haptic TLV payload is invalid\n");
    } else if (result == storage::TlvLoadResult::kInvalid) {
      LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
          "Haptic TLV container is invalid\n");
    } else if (result == storage::TlvLoadResult::kError) {
      LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
          "Load haptic TLV failed: %s\n", esp_err_to_name(error));
    }
    nvs_close(handle);
  }
  if (!g_haptic_cache.Initialize(
          NormalizeHapticPreferences(loaded))) {
    LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
        "Initialize haptic storage cache failed\n");
  }
}

HapticPreferences GetHapticPreferences() {
  HapticPreferences preferences;
  g_haptic_cache.Read(&preferences);
  return preferences;
}

bool UpdateHapticPreferences(const HapticPreferences& preferences) {
  return g_haptic_cache.Update(
      NormalizeHapticPreferences(preferences));
}

StorageStageResult StageHapticStorage(nvs_handle_t handle) {
  const HapticPreferences* preferences = nullptr;
  if (!g_haptic_cache.BeginFlush(&preferences)) {
    return StorageStageResult::kClean;
  }
  std::array<uint8_t, kHapticTlvCapacity> buffer = {};
  size_t encoded_size = 0;
  if (!EncodeHapticPreferences(*preferences, buffer.data(),
          buffer.size(), &encoded_size) ||
      nvs_set_blob(handle, kHapticNvsKey,
          buffer.data(), encoded_size) != ESP_OK) {
    return StorageStageResult::kFailed;
  }
  return StorageStageResult::kStaged;
}

void FinishHapticStorage(bool committed) {
  g_haptic_cache.FinishFlush(committed);
}

}  // namespace lilygo_box::app
