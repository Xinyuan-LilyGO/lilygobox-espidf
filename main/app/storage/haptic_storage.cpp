/**
 * @Description: 振动偏好存储实现
 * @Author: LILYGO_L
 * @Date: 2026-06-25 00:00:00
 * @LastEditTime: 2026-07-03 00:00:00
 * @License: GPL 3.0
 */
#include "app/storage/haptic_storage.h"

#include <algorithm>
#include <atomic>

#include "esp_err.h"
#include "nvs.h"

namespace lilygo_box::app {
namespace {

constexpr const char* kNvsNamespace = "settings";
constexpr const char* kNvsKey = "haptic_config";
constexpr uint32_t kMagic = 0x48505443;

struct Blob {
  uint32_t magic = kMagic;
  uint8_t enabled = 1;
  uint8_t strength_percent = 45;
};

std::atomic<bool> g_enabled{true};
std::atomic<int> g_strength{45};

}  // namespace

void InitHapticCache() {
  nvs_handle_t handle = 0;
  if (nvs_open(kNvsNamespace, NVS_READONLY, &handle) != ESP_OK) return;

  Blob blob;
  size_t sz = sizeof(blob);
  if (nvs_get_blob(handle, kNvsKey, &blob, &sz) == ESP_OK &&
      sz == sizeof(blob) && blob.magic == kMagic) {
    g_enabled.store(blob.enabled != 0);
    g_strength.store(std::clamp<int>(blob.strength_percent, 0, 100));
  }
  nvs_close(handle);
}

HapticPreferences GetHapticPreferences() {
  return {g_enabled.load(), g_strength.load()};
}

bool UpdateHapticPreferences(const HapticPreferences& prefs) {
  g_enabled.store(prefs.enabled);
  g_strength.store(prefs.strength_percent);

  nvs_handle_t handle = 0;
  if (nvs_open(kNvsNamespace, NVS_READWRITE, &handle) != ESP_OK) return false;

  Blob blob;
  blob.enabled = prefs.enabled ? 1 : 0;
  blob.strength_percent = static_cast<uint8_t>(
      std::clamp(prefs.strength_percent, 0, 100));

  bool ok = nvs_set_blob(handle, kNvsKey, &blob, sizeof(blob)) == ESP_OK &&
            nvs_commit(handle) == ESP_OK;
  nvs_close(handle);
  return ok;
}

}  // namespace lilygo_box::app
