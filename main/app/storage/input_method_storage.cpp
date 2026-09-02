/*
 * @Description: Input method preference storage implementation
 * @Author: LILYGO_L
 * @Date: 2026-08-20 00:00:00
 * @LastEditTime: 2026-09-02 17:51:27
 * @License: GPL 3.0
 */
#include "app/storage/input_method_storage.h"

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

constexpr char kInputMethodNvsNamespace[] = "settings";
constexpr char kInputMethodNvsKey[] = "input_method";
constexpr size_t kInputMethodTlvCapacity = 24;

enum class InputMethodField : uint16_t {
  kUseOnScreenKeyboard = 1,
};

bool AreInputMethodPreferencesEqual(
    const InputMethodPreferences& left, const InputMethodPreferences& right) {
  return left.use_on_screen_keyboard == right.use_on_screen_keyboard;
}

bool DecodeInputMethodPreferences(
    const storage::TlvBuffer& buffer, InputMethodPreferences* preferences) {
  if (preferences == nullptr) {
    return false;
  }

  InputMethodPreferences decoded;
  storage::TlvReader reader(
      storage::TlvDomain::kInputMethod, buffer.data.get(), buffer.size);
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
    switch (static_cast<InputMethodField>(field.tag())) {
      case InputMethodField::kUseOnScreenKeyboard:
        if (!field.ReadBool(&decoded.use_on_screen_keyboard)) {
          return false;
        }
        break;
      default:
        break;
    }
  }
}

bool EncodeInputMethodPreferences(const InputMethodPreferences& preferences,
    uint8_t* output, size_t capacity, size_t* encoded_size) {
  storage::TlvWriter writer(storage::TlvDomain::kInputMethod, output, capacity);
  return writer.WriteBool(
             static_cast<uint16_t>(InputMethodField::kUseOnScreenKeyboard),
             preferences.use_on_screen_keyboard) &&
         writer.Finalize(encoded_size);
}

NvsStorageCache<InputMethodPreferences> g_input_method_cache(
    StorageDomain::kInputMethod, AreInputMethodPreferencesEqual);

}  // namespace

void InitInputMethodCache() {
  InputMethodPreferences loaded;
  nvs_handle_t handle = 0;
  if (OpenApplicationNvs(kInputMethodNvsNamespace, NVS_READONLY, &handle) ==
      ESP_OK) {
    storage::TlvBuffer buffer;
    esp_err_t error = ESP_OK;
    const storage::TlvLoadResult result = storage::LoadTlvBuffer(handle,
        kInputMethodNvsKey, storage::TlvDomain::kInputMethod,
        kInputMethodTlvCapacity, &buffer, &error);
    if (result == storage::TlvLoadResult::kLoaded &&
        !DecodeInputMethodPreferences(buffer, &loaded)) {
      LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
          "Input method TLV payload is invalid\n");
    } else if (result == storage::TlvLoadResult::kInvalid) {
      LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
          "Input method TLV container is invalid\n");
    } else if (result == storage::TlvLoadResult::kError) {
      LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
          "Load input method TLV failed: %s\n", esp_err_to_name(error));
    }
    nvs_close(handle);
  }
  if (!g_input_method_cache.Initialize(loaded)) {
    LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
        "Initialize input method storage cache failed\n");
  }
}

InputMethodPreferences GetInputMethodPreferences() {
  InputMethodPreferences preferences;
  g_input_method_cache.Read(&preferences);
  return preferences;
}

bool UpdateInputMethodPreferences(const InputMethodPreferences& preferences) {
  return g_input_method_cache.UpdateAndPersist(preferences);
}

StorageStageResult StageInputMethodStorage(nvs_handle_t handle) {
  const InputMethodPreferences* preferences = nullptr;
  if (!g_input_method_cache.BeginFlush(&preferences)) {
    return StorageStageResult::kClean;
  }
  std::array<uint8_t, kInputMethodTlvCapacity> buffer = {};
  size_t encoded_size = 0;
  if (!EncodeInputMethodPreferences(
          *preferences, buffer.data(), buffer.size(), &encoded_size) ||
      nvs_set_blob(handle, kInputMethodNvsKey, buffer.data(), encoded_size) !=
          ESP_OK) {
    return StorageStageResult::kFailed;
  }
  return StorageStageResult::kStaged;
}

void FinishInputMethodStorage(bool committed) {
  g_input_method_cache.FinishFlush(committed);
}

}  // namespace lilygo_box::app
