/*
 * @Description: Keyboard expansion preference storage implementation
 * @Author: LILYGO_L
 * @Date: 2026-08-20 00:00:00
 * @LastEditTime: 2026-09-02 17:51:30
 * @License: GPL 3.0
 */
#include "app/storage/keyboard_expansion_storage.h"

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

constexpr char kKeyboardExpansionNvsNamespace[] = "settings";
constexpr char kKeyboardExpansionNvsKey[] = "keyboard_exp";
constexpr size_t kKeyboardExpansionTlvCapacity = 32;

enum class KeyboardExpansionField : uint16_t {
  kEnabled = 1,
  kBacklightBrightnessPercent = 2,
};

bool AreKeyboardExpansionPreferencesEqual(
    const KeyboardExpansionPreferences& left,
    const KeyboardExpansionPreferences& right) {
  return left.enabled == right.enabled &&
         left.backlight_brightness_percent ==
             right.backlight_brightness_percent;
}

bool DecodeKeyboardExpansionPreferences(const storage::TlvBuffer& buffer,
    KeyboardExpansionPreferences* preferences) {
  if (preferences == nullptr) {
    return false;
  }

  KeyboardExpansionPreferences decoded;
  storage::TlvReader reader(
      storage::TlvDomain::kKeyboardExpansion, buffer.data.get(), buffer.size);
  storage::TlvField field;
  while (true) {
    const storage::TlvReadResult result = reader.Next(&field);
    if (result == storage::TlvReadResult::kEnd) {
      *preferences = decoded;
      return true;
    }
    if (result == storage::TlvReadResult::kInvalid) {
      return false;
    }
    switch (static_cast<KeyboardExpansionField>(field.tag())) {
      case KeyboardExpansionField::kEnabled:
        if (!field.ReadBool(&decoded.enabled)) {
          return false;
        }
        break;
      case KeyboardExpansionField::kBacklightBrightnessPercent:
        if (!field.ReadInt32(&decoded.backlight_brightness_percent) ||
            decoded.backlight_brightness_percent < 0 ||
            decoded.backlight_brightness_percent > 100) {
          return false;
        }
        break;
      default:
        break;
    }
  }
}

bool EncodeKeyboardExpansionPreferences(
    const KeyboardExpansionPreferences& preferences, uint8_t* output,
    size_t capacity, size_t* encoded_size) {
  if (preferences.backlight_brightness_percent < 0 ||
      preferences.backlight_brightness_percent > 100) {
    return false;
  }
  storage::TlvWriter writer(
      storage::TlvDomain::kKeyboardExpansion, output, capacity);
  return writer.WriteBool(
             static_cast<uint16_t>(KeyboardExpansionField::kEnabled),
             preferences.enabled) &&
         writer.WriteInt32(
             static_cast<uint16_t>(
                 KeyboardExpansionField::kBacklightBrightnessPercent),
             preferences.backlight_brightness_percent) &&
         writer.Finalize(encoded_size);
}

NvsStorageCache<KeyboardExpansionPreferences> g_keyboard_expansion_cache(
    StorageDomain::kKeyboardExpansion, AreKeyboardExpansionPreferencesEqual);

}  // namespace

void InitKeyboardExpansionCache() {
  KeyboardExpansionPreferences loaded;
  nvs_handle_t handle = 0;
  if (OpenApplicationNvs(
          kKeyboardExpansionNvsNamespace, NVS_READONLY, &handle) == ESP_OK) {
    storage::TlvBuffer buffer;
    esp_err_t error = ESP_OK;
    const storage::TlvLoadResult result = storage::LoadTlvBuffer(handle,
        kKeyboardExpansionNvsKey, storage::TlvDomain::kKeyboardExpansion,
        kKeyboardExpansionTlvCapacity, &buffer, &error);
    if (result == storage::TlvLoadResult::kLoaded &&
        !DecodeKeyboardExpansionPreferences(buffer, &loaded)) {
      LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
          "Keyboard expansion TLV payload is invalid\n");
    } else if (result == storage::TlvLoadResult::kInvalid) {
      LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
          "Keyboard expansion TLV container is invalid\n");
    } else if (result == storage::TlvLoadResult::kError) {
      LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
          "Load keyboard expansion TLV failed: %s\n", esp_err_to_name(error));
    }
    nvs_close(handle);
  }
  if (!g_keyboard_expansion_cache.Initialize(loaded)) {
    LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
        "Initialize keyboard expansion storage cache failed\n");
  }
}

KeyboardExpansionPreferences GetKeyboardExpansionPreferences() {
  KeyboardExpansionPreferences preferences;
  g_keyboard_expansion_cache.Read(&preferences);
  return preferences;
}

bool UpdateKeyboardExpansionPreferences(
    const KeyboardExpansionPreferences& preferences) {
  return g_keyboard_expansion_cache.UpdateAndPersist(preferences);
}

StorageStageResult StageKeyboardExpansionStorage(nvs_handle_t handle) {
  const KeyboardExpansionPreferences* preferences = nullptr;
  if (!g_keyboard_expansion_cache.BeginFlush(&preferences)) {
    return StorageStageResult::kClean;
  }
  std::array<uint8_t, kKeyboardExpansionTlvCapacity> buffer = {};
  size_t encoded_size = 0;
  if (!EncodeKeyboardExpansionPreferences(
          *preferences, buffer.data(), buffer.size(), &encoded_size) ||
      nvs_set_blob(handle, kKeyboardExpansionNvsKey, buffer.data(),
          encoded_size) != ESP_OK) {
    return StorageStageResult::kFailed;
  }
  return StorageStageResult::kStaged;
}

void FinishKeyboardExpansionStorage(bool committed) {
  g_keyboard_expansion_cache.FinishFlush(committed);
}

}  // namespace lilygo_box::app
