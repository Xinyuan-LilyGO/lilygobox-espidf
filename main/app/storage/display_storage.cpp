/**
 * @Description: 显示偏好存储实现
 * @Author: LILYGO_L
 * @Date: 2026-06-25 00:00:00
 * @LastEditTime: 2026-07-16 22:35:14
 * @License: GPL 3.0
 */
#include "app/storage/display_storage.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>

#include "app/storage/storage_internal.h"
#include "base/logger.h"
#include "esp_err.h"
#include "nvs.h"

namespace lilygo_box::app {
namespace {

constexpr const char* kDisplayNvsNamespace = "settings";
constexpr const char* kDisplayNvsKey = "display_config";
constexpr uint32_t kDisplayMagic = 0x4453504C;
constexpr uint16_t kDisplaySchemaVersion = 1;

struct DisplayBlob {
  // 校验当前 NVS 数据是否属于显示偏好。
  uint32_t magic = kDisplayMagic;
  // 当前显示偏好存储结构版本。
  uint16_t schema_version = kDisplaySchemaVersion;
  // 写入 NVS 的完整结构大小。
  uint16_t struct_size = sizeof(DisplayBlob);
  // 屏幕背光亮度百分比。
  uint8_t brightness_percent = 90;
  // 自动锁屏等待秒数。
  uint32_t lock_timeout_seconds = 300;
  // 屏幕旋转角度。
  int32_t screen_rotation_angle = 0;
};

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

DisplayBlob NormalizeDisplayBlob(const DisplayBlob& source) {
  DisplayBlob result;
  result.brightness_percent = static_cast<uint8_t>(
      std::clamp<int>(source.brightness_percent,
          kUserDisplayBrightnessMinPercent,
          kUserDisplayBrightnessMaxPercent));
  result.lock_timeout_seconds = std::min<uint32_t>(
      source.lock_timeout_seconds, 24U * 60U * 60U);
  result.screen_rotation_angle =
      NormalizeScreenRotationAngle(source.screen_rotation_angle);
  return result;
}

bool AreDisplayBlobsEqual(
    const DisplayBlob& left, const DisplayBlob& right) {
  return left.magic == right.magic &&
      left.schema_version == right.schema_version &&
      left.struct_size == right.struct_size &&
      left.brightness_percent == right.brightness_percent &&
      left.lock_timeout_seconds == right.lock_timeout_seconds &&
      left.screen_rotation_angle == right.screen_rotation_angle;
}

DeferredStorageCache<DisplayBlob> g_display_cache(
    StorageDomain::kDisplay, AreDisplayBlobsEqual);

}  // namespace

void InitDisplayCache() {
  DisplayBlob loaded;
  nvs_handle_t handle = 0;
  if (nvs_open(kDisplayNvsNamespace, NVS_READONLY, &handle) == ESP_OK) {
    DisplayBlob stored;
    size_t size = sizeof(stored);
    if (nvs_get_blob(handle, kDisplayNvsKey, &stored, &size) == ESP_OK &&
        size == sizeof(stored) && stored.magic == kDisplayMagic &&
        stored.schema_version == kDisplaySchemaVersion &&
        stored.struct_size == sizeof(DisplayBlob)) {
      loaded = NormalizeDisplayBlob(stored);
    }
    nvs_close(handle);
  }
  if (!g_display_cache.Initialize(loaded)) {
    LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
        "Initialize display storage cache failed\n");
  }
}

DisplayPreferences GetDisplayPreferences() {
  DisplayBlob blob;
  g_display_cache.Read(&blob);
  return {blob.brightness_percent,
      static_cast<int>(blob.lock_timeout_seconds),
      blob.screen_rotation_angle};
}

bool UpdateDisplayPreferences(const DisplayPreferences& prefs) {
  DisplayBlob blob;
  blob.brightness_percent = static_cast<uint8_t>(
      std::clamp(prefs.brightness_percent,
          kUserDisplayBrightnessMinPercent,
          kUserDisplayBrightnessMaxPercent));
  blob.lock_timeout_seconds = static_cast<uint32_t>(std::clamp(
      prefs.lock_timeout_seconds, 0, 24 * 60 * 60));
  blob.screen_rotation_angle =
      NormalizeScreenRotationAngle(prefs.screen_rotation_angle);
  return g_display_cache.Update(blob);
}

StorageStageResult StageDisplayStorage(nvs_handle_t handle) {
  const DisplayBlob* blob = nullptr;
  if (!g_display_cache.BeginFlush(&blob)) {
    return StorageStageResult::kClean;
  }
  if (nvs_set_blob(
      handle, kDisplayNvsKey, blob, sizeof(*blob)) != ESP_OK) {
    return StorageStageResult::kFailed;
  }
  return StorageStageResult::kStaged;
}

void FinishDisplayStorage(bool committed) {
  g_display_cache.FinishFlush(committed);
}

}  // namespace lilygo_box::app
