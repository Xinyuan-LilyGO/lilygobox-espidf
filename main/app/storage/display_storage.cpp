/**
 * @Description: 显示偏好存储实现
 * @Author: LILYGO_L
 * @Date: 2026-06-25 00:00:00
 * @LastEditTime: 2026-07-03 00:00:00
 * @License: GPL 3.0
 */
#include "app/storage/display_storage.h"

#include <algorithm>
#include <atomic>
#include <cstddef>

#include "esp_err.h"
#include "nvs.h"

namespace lilygo_box::app {
namespace {

constexpr const char* kNvsNamespace = "settings";
constexpr const char* kNvsKey = "display_config";
constexpr uint32_t kMagic = 0x4453504C;

struct Blob {
  uint32_t magic = kMagic;
  uint8_t brightness_percent = 70;
  uint32_t lock_timeout_seconds = 300;
  int32_t screen_rotation_angle = 0;
};

std::atomic<int> g_brightness{70};
std::atomic<int> g_lock_timeout{300};
std::atomic<int> g_screen_rotation{0};

int NormalizeScreenRotationAngle(int angle) {
  switch (angle) {
    case 0:
    case 90:
    case 180:
    case 270:
      return angle;
    default:
      return 0;
  }
}

}  // namespace

void InitDisplayCache() {
  nvs_handle_t handle = 0;
  if (nvs_open(kNvsNamespace, NVS_READONLY, &handle) != ESP_OK) return;

  Blob blob;
  size_t sz = sizeof(blob);
  if (nvs_get_blob(handle, kNvsKey, &blob, &sz) == ESP_OK &&
      sz >= offsetof(Blob, screen_rotation_angle) && blob.magic == kMagic) {
    g_brightness.store(std::clamp<int>(blob.brightness_percent, 0, 100));
    g_lock_timeout.store(std::clamp<int>(
        static_cast<int>(blob.lock_timeout_seconds), 0, 24 * 60 * 60));
    if (sz >= sizeof(blob)) {
      g_screen_rotation.store(
          NormalizeScreenRotationAngle(blob.screen_rotation_angle));
    }
  }
  nvs_close(handle);
}

DisplayPreferences GetDisplayPreferences() {
  return {g_brightness.load(), g_lock_timeout.load(),
      g_screen_rotation.load()};
}

bool UpdateDisplayPreferences(const DisplayPreferences& prefs) {
  g_brightness.store(std::clamp(prefs.brightness_percent, 0, 100));
  g_lock_timeout.store(std::clamp(prefs.lock_timeout_seconds, 0, 24 * 60 * 60));
  g_screen_rotation.store(
      NormalizeScreenRotationAngle(prefs.screen_rotation_angle));

  nvs_handle_t handle = 0;
  if (nvs_open(kNvsNamespace, NVS_READWRITE, &handle) != ESP_OK) return false;

  Blob blob;
  blob.brightness_percent = static_cast<uint8_t>(
      std::clamp(prefs.brightness_percent, 0, 100));
  blob.lock_timeout_seconds = static_cast<uint32_t>(std::clamp(
      prefs.lock_timeout_seconds, 0, 24 * 60 * 60));
  blob.screen_rotation_angle =
      NormalizeScreenRotationAngle(prefs.screen_rotation_angle);

  bool ok = nvs_set_blob(handle, kNvsKey, &blob, sizeof(blob)) == ESP_OK &&
            nvs_commit(handle) == ESP_OK;
  nvs_close(handle);
  return ok;
}

}  // namespace lilygo_box::app
