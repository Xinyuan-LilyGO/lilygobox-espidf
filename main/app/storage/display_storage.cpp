/**
 * @Description: Settings display NVS storage helpers
 * @Author: LILYGO_L
 * @Date: 2026-06-25 00:00:00
 * @LastEditTime: 2026-06-25 00:00:00
 * @License: GPL 3.0
 */
#include "app/storage/display_storage.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>

#include "base/logger.h"
#include "esp_err.h"
#include "nvs.h"

namespace lilygo_box::app {
namespace {

constexpr const char* kSettingsNvsNamespace = "settings";
constexpr const char* kDisplayPreferencesNvsKey = "display_config";
constexpr uint32_t kDisplayPreferencesMagic = 0x4453504C;
constexpr uint32_t kDisplayPreferencesVersion = 2;
constexpr int kDefaultLockTimeoutSeconds = 10;
constexpr int kMinLockTimeoutSeconds = 0;
constexpr int kMaxLockTimeoutSeconds = 24 * 60 * 60;

struct DisplayPreferencesStorage {
  uint32_t magic = kDisplayPreferencesMagic;
  uint32_t version = kDisplayPreferencesVersion;
  uint8_t brightness_percent = 70;
  uint32_t lock_timeout_seconds = kDefaultLockTimeoutSeconds;
};

constexpr size_t kDisplayPreferencesV1Size =
    offsetof(DisplayPreferencesStorage, lock_timeout_seconds);

esp_err_t OpenSettingsNvs(nvs_open_mode_t mode, nvs_handle_t* handle) {
  if (handle == nullptr) {
    return ESP_ERR_INVALID_ARG;
  }

  const esp_err_t result = nvs_open(kSettingsNvsNamespace, mode, handle);
  if (result == ESP_ERR_NVS_NOT_FOUND && mode == NVS_READONLY) {
    return result;
  }
  if (result != ESP_OK) {
    LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
        "Open settings NVS failed (error code: %#X)\n", result);
  }
  return result;
}

}  // namespace

bool SaveDisplayPreferencesToNvs(const DisplayPreferences& preferences) {
  DisplayPreferencesStorage storage;
  storage.brightness_percent = static_cast<uint8_t>(
      std::clamp(preferences.brightness_percent, 0, 100));
  storage.lock_timeout_seconds = static_cast<uint32_t>(std::clamp(
      preferences.lock_timeout_seconds, kMinLockTimeoutSeconds,
      kMaxLockTimeoutSeconds));

  nvs_handle_t handle = 0;
  if (OpenSettingsNvs(NVS_READWRITE, &handle) != ESP_OK) {
    return false;
  }

  esp_err_t result = nvs_set_blob(
      handle, kDisplayPreferencesNvsKey, &storage, sizeof(storage));
  if (result == ESP_OK) {
    result = nvs_commit(handle);
  }
  nvs_close(handle);

  if (result != ESP_OK) {
    LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
        "Save display preferences failed (error code: %#X)\n", result);
    return false;
  }
  return true;
}

bool LoadDisplayPreferencesFromNvs(DisplayPreferences* preferences) {
  if (preferences == nullptr) {
    return false;
  }

  nvs_handle_t handle = 0;
  const esp_err_t open_result = OpenSettingsNvs(NVS_READONLY, &handle);
  if (open_result != ESP_OK) {
    return false;
  }

  DisplayPreferencesStorage storage;
  size_t blob_size = sizeof(storage);
  const esp_err_t result = nvs_get_blob(
      handle, kDisplayPreferencesNvsKey, &storage, &blob_size);
  nvs_close(handle);
  if (result == ESP_ERR_NVS_NOT_FOUND) {
    return false;
  }
  const bool can_read_v1 = result == ESP_OK &&
                           blob_size == kDisplayPreferencesV1Size &&
                           storage.magic == kDisplayPreferencesMagic &&
                           storage.version == 1;
  const bool can_read_v2 = result == ESP_OK && blob_size == sizeof(storage) &&
                           storage.magic == kDisplayPreferencesMagic &&
                           storage.version == kDisplayPreferencesVersion;
  if (!can_read_v1 && !can_read_v2) {
    LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
        "Load display preferences failed (error code: %#X)\n", result);
    return false;
  }

  preferences->brightness_percent = std::clamp<int>(
      storage.brightness_percent, 0, 100);
  preferences->lock_timeout_seconds = can_read_v2
      ? std::clamp<int>(storage.lock_timeout_seconds, kMinLockTimeoutSeconds,
            kMaxLockTimeoutSeconds)
      : kDefaultLockTimeoutSeconds;
  return true;
}

}  // namespace lilygo_box::app
