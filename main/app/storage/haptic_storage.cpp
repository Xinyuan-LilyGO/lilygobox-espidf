/**
 * @Description: 振动偏好存储实现
 * @Author: LILYGO_L
 * @Date: 2026-06-25 00:00:00
 * @LastEditTime: 2026-07-16 22:35:14
 * @License: GPL 3.0
 */
#include "app/storage/haptic_storage.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>

#include "app/storage/storage_internal.h"
#include "base/logger.h"
#include "esp_err.h"
#include "nvs.h"

namespace lilygo_box::app {
namespace {

constexpr const char* kNvsNamespace = "settings";
constexpr const char* kNvsKey = "haptic_config";
constexpr uint32_t kMagic = 0x48505443;

struct Blob {
  // 校验当前 NVS 数据是否属于触感偏好。
  uint32_t magic = kMagic;
  // 是否启用触感反馈。
  uint8_t enabled = 1;
  // 触感强度百分比。
  uint8_t strength_percent = 90;
};

Blob NormalizeBlob(const Blob& source) {
  Blob result;
  result.enabled = source.enabled == 0 ? 0 : 1;
  result.strength_percent = static_cast<uint8_t>(
      std::clamp<int>(source.strength_percent, 0, 100));
  return result;
}

bool AreBlobsEqual(const Blob& left, const Blob& right) {
  return left.magic == right.magic && left.enabled == right.enabled &&
      left.strength_percent == right.strength_percent;
}

DeferredStorageCache<Blob> g_cache(
    StorageDomain::kHaptic, AreBlobsEqual);

}  // namespace

void InitHapticCache() {
  Blob loaded;
  nvs_handle_t handle = 0;
  if (nvs_open(kNvsNamespace, NVS_READONLY, &handle) == ESP_OK) {
    Blob stored;
    size_t size = sizeof(stored);
    if (nvs_get_blob(handle, kNvsKey, &stored, &size) == ESP_OK &&
        size == sizeof(stored) && stored.magic == kMagic) {
      loaded = NormalizeBlob(stored);
    }
    nvs_close(handle);
  }
  if (!g_cache.Initialize(loaded)) {
    LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
        "Initialize haptic storage cache failed\n");
  }
}

HapticPreferences GetHapticPreferences() {
  Blob blob;
  g_cache.Read(&blob);
  return {blob.enabled != 0, blob.strength_percent};
}

bool UpdateHapticPreferences(const HapticPreferences& prefs) {
  Blob blob;
  blob.enabled = prefs.enabled ? 1 : 0;
  blob.strength_percent = static_cast<uint8_t>(
      std::clamp(prefs.strength_percent, 0, 100));
  return g_cache.Update(blob);
}

StorageStageResult StageHapticStorage(nvs_handle_t handle) {
  const Blob* blob = nullptr;
  if (!g_cache.BeginFlush(&blob)) {
    return StorageStageResult::kClean;
  }
  if (nvs_set_blob(handle, kNvsKey, blob, sizeof(*blob)) != ESP_OK) {
    return StorageStageResult::kFailed;
  }
  return StorageStageResult::kStaged;
}

void FinishHapticStorage(bool committed) {
  g_cache.FinishFlush(committed);
}

}  // namespace lilygo_box::app
