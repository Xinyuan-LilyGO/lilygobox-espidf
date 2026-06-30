/*
 * @Description: Settings audio NVS storage helpers
 * @Author: LILYGO_L
 * @Date: 2026-06-25 00:00:00
 * @LastEditTime: 2026-06-25 00:00:00
 * @License: GPL 3.0
 */
#include "app/storage/audio_storage.h"

#include <algorithm>
#include <cstdint>

#include "base/logger.h"
#include "esp_err.h"
#include "nvs.h"

namespace lilygo_box::app {
namespace {

constexpr const char* kSettingsNvsNamespace = "settings";
constexpr const char* kAudioPreferencesNvsKey = "audio_config";
constexpr uint32_t kAudioPreferencesMagic = 0x41554450;
constexpr uint32_t kAudioPreferencesVersion = 1;

struct AudioPreferencesStorage {
  uint32_t magic = kAudioPreferencesMagic;
  uint32_t version = kAudioPreferencesVersion;
  uint8_t volume_percent = 60;
};

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

bool SaveAudioPreferencesToNvs(const AudioPreferences& preferences) {
  AudioPreferencesStorage storage;
  storage.volume_percent = static_cast<uint8_t>(
      std::clamp(preferences.volume_percent, 0, 100));

  nvs_handle_t handle = 0;
  if (OpenSettingsNvs(NVS_READWRITE, &handle) != ESP_OK) {
    return false;
  }

  esp_err_t result = nvs_set_blob(
      handle, kAudioPreferencesNvsKey, &storage, sizeof(storage));
  if (result == ESP_OK) {
    result = nvs_commit(handle);
  }
  nvs_close(handle);

  if (result != ESP_OK) {
    LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
        "Save audio preferences failed (error code: %#X)\n", result);
    return false;
  }
  return true;
}

bool LoadAudioPreferencesFromNvs(AudioPreferences* preferences) {
  if (preferences == nullptr) {
    return false;
  }

  nvs_handle_t handle = 0;
  const esp_err_t open_result = OpenSettingsNvs(NVS_READONLY, &handle);
  if (open_result != ESP_OK) {
    return false;
  }

  AudioPreferencesStorage storage;
  size_t blob_size = sizeof(storage);
  const esp_err_t result = nvs_get_blob(
      handle, kAudioPreferencesNvsKey, &storage, &blob_size);
  nvs_close(handle);
  if (result == ESP_ERR_NVS_NOT_FOUND) {
    return false;
  }
  if (result != ESP_OK || blob_size != sizeof(storage) ||
      storage.magic != kAudioPreferencesMagic ||
      storage.version != kAudioPreferencesVersion) {
    LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
        "Load audio preferences failed (error code: %#X)\n", result);
    return false;
  }

  preferences->volume_percent = std::clamp<int>(storage.volume_percent, 0, 100);
  return true;
}

}  // namespace lilygo_box::app
