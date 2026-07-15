/**
 * @Description: 声音偏好存储实现
 * @Author: LILYGO_L
 * @Date: 2026-06-25 00:00:00
 * @LastEditTime: 2026-07-03 00:00:00
 * @License: GPL 3.0
 */
#include "app/storage/sound_storage.h"

#include <algorithm>
#include <atomic>

#include "esp_err.h"
#include "nvs.h"

namespace lilygo_box::app {
namespace {

constexpr const char* kNvsNamespace = "settings";
constexpr const char* kNvsKey = "audio_config";
constexpr uint32_t kMagic = 0x41554450;

struct Blob {
  uint32_t magic = kMagic;
  uint8_t volume_percent = 90;
};

std::atomic<int> g_volume{90};

}  // namespace

void InitSoundCache() {
  nvs_handle_t handle = 0;
  if (nvs_open(kNvsNamespace, NVS_READONLY, &handle) != ESP_OK) return;

  Blob blob;
  size_t sz = sizeof(blob);
  if (nvs_get_blob(handle, kNvsKey, &blob, &sz) == ESP_OK &&
      sz == sizeof(blob) && blob.magic == kMagic) {
    g_volume.store(std::clamp<int>(blob.volume_percent, 0, 100));
  }
  nvs_close(handle);
}

SoundPreferences GetSoundPreferences() {
  return {g_volume.load()};
}

bool UpdateSoundPreferences(const SoundPreferences& prefs) {
  g_volume.store(prefs.volume_percent);

  nvs_handle_t handle = 0;
  if (nvs_open(kNvsNamespace, NVS_READWRITE, &handle) != ESP_OK) return false;

  Blob blob;
  blob.volume_percent = static_cast<uint8_t>(
      std::clamp(prefs.volume_percent, 0, 100));

  bool ok = nvs_set_blob(handle, kNvsKey, &blob, sizeof(blob)) == ESP_OK &&
            nvs_commit(handle) == ESP_OK;
  nvs_close(handle);
  return ok;
}

}  // namespace lilygo_box::app
