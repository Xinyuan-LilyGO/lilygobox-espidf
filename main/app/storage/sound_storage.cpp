/*
 * @Description: Settings sound and haptics NVS storage compatibility helpers
 * @Author: LILYGO_L
 * @Date: 2026-06-25 00:00:00
 * @LastEditTime: 2026-06-25 00:00:00
 * @License: GPL 3.0
 */
#include "app/storage/sound_storage.h"

#include <algorithm>
#include <cstdint>

#include "app/storage/audio_storage.h"
#include "app/storage/haptic_storage.h"
#include "base/logger.h"
#include "esp_err.h"
#include "nvs.h"

namespace lilygo_box::app {
namespace {

constexpr const char* kSettingsNvsNamespace = "settings";
constexpr const char* kLegacySoundPreferencesNvsKey = "sound_config";
constexpr uint32_t kLegacySoundPreferencesMagic = 0x534E4448;

struct LegacySoundPreferencesStorage {
  uint32_t magic = kLegacySoundPreferencesMagic;
  uint8_t volume_percent = 60;
  uint8_t haptics_enabled = 1;
  uint8_t haptic_strength_percent = 45;
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

bool LoadLegacySoundPreferencesFromNvs(SoundPreferences* preferences) {
  if (preferences == nullptr) {
    return false;
  }

  nvs_handle_t handle = 0;
  const esp_err_t open_result = OpenSettingsNvs(NVS_READONLY, &handle);
  if (open_result != ESP_OK) {
    return false;
  }

  LegacySoundPreferencesStorage storage;
  size_t blob_size = sizeof(storage);
  const esp_err_t result = nvs_get_blob(
      handle, kLegacySoundPreferencesNvsKey, &storage, &blob_size);
  nvs_close(handle);
  if (result == ESP_ERR_NVS_NOT_FOUND) {
    return false;
  }
  if (result != ESP_OK || blob_size != sizeof(storage) ||
      storage.magic != kLegacySoundPreferencesMagic) {
    LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
        "Load legacy sound preferences failed (error code: %#X)\n", result);
    return false;
  }

  preferences->volume_percent = std::clamp<int>(storage.volume_percent, 0, 100);
  preferences->haptics_enabled = storage.haptics_enabled != 0;
  preferences->haptic_strength_percent = std::clamp<int>(
      storage.haptic_strength_percent, 0, 100);
  return true;
}

}  // namespace

bool SaveSoundPreferencesToNvs(const SoundPreferences& preferences) {
  AudioPreferences audio_preferences;
  audio_preferences.volume_percent = preferences.volume_percent;
  HapticPreferences haptic_preferences;
  haptic_preferences.enabled = preferences.haptics_enabled;
  haptic_preferences.strength_percent = preferences.haptic_strength_percent;
  const bool audio_saved = SaveAudioPreferencesToNvs(audio_preferences);
  const bool haptic_saved = SaveHapticPreferencesToNvs(haptic_preferences);
  return audio_saved && haptic_saved;
}

bool LoadSoundPreferencesFromNvs(SoundPreferences* preferences) {
  if (preferences == nullptr) {
    return false;
  }

  bool loaded = false;
  AudioPreferences audio_preferences;
  if (LoadAudioPreferencesFromNvs(&audio_preferences)) {
    preferences->volume_percent = audio_preferences.volume_percent;
    loaded = true;
  }

  HapticPreferences haptic_preferences;
  if (LoadHapticPreferencesFromNvs(&haptic_preferences)) {
    preferences->haptics_enabled = haptic_preferences.enabled;
    preferences->haptic_strength_percent = haptic_preferences.strength_percent;
    loaded = true;
  }

  if (loaded) {
    return true;
  }
  return LoadLegacySoundPreferencesFromNvs(preferences);
}

}  // namespace lilygo_box::app
