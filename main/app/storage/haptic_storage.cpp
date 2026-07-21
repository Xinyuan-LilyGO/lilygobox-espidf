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

constexpr const char* kHapticNvsNamespace = "settings";
constexpr const char* kHapticNvsKey = "haptic_config";
constexpr uint32_t kHapticMagic = 0x48505443;
constexpr uint16_t kHapticSchemaVersion = 1;

struct HapticBlob {
  // 校验当前 NVS 数据是否属于触感偏好。
  uint32_t magic = kHapticMagic;
  // 当前触感偏好存储结构版本。
  uint16_t schema_version = kHapticSchemaVersion;
  // 写入 NVS 的完整结构大小。
  uint16_t struct_size = sizeof(HapticBlob);
  // 是否启用触感反馈。
  uint8_t enabled = 1;
  // 触感强度百分比。
  uint8_t strength_percent = 90;
};

HapticBlob NormalizeHapticBlob(const HapticBlob& source) {
  HapticBlob result;
  result.enabled = source.enabled == 0 ? 0 : 1;
  result.strength_percent = static_cast<uint8_t>(
      std::clamp<int>(source.strength_percent, 0, 100));
  return result;
}

bool AreHapticBlobsEqual(
    const HapticBlob& left, const HapticBlob& right) {
  return left.magic == right.magic &&
      left.schema_version == right.schema_version &&
      left.struct_size == right.struct_size &&
      left.enabled == right.enabled &&
      left.strength_percent == right.strength_percent;
}

DeferredStorageCache<HapticBlob> g_haptic_cache(
    StorageDomain::kHaptic, AreHapticBlobsEqual);

}  // namespace

void InitHapticCache() {
  HapticBlob loaded;
  nvs_handle_t handle = 0;
  if (nvs_open(kHapticNvsNamespace, NVS_READONLY, &handle) == ESP_OK) {
    HapticBlob stored;
    size_t size = sizeof(stored);
    if (nvs_get_blob(handle, kHapticNvsKey, &stored, &size) == ESP_OK &&
        size == sizeof(stored) && stored.magic == kHapticMagic &&
        stored.schema_version == kHapticSchemaVersion &&
        stored.struct_size == sizeof(HapticBlob)) {
      loaded = NormalizeHapticBlob(stored);
    }
    nvs_close(handle);
  }
  if (!g_haptic_cache.Initialize(loaded)) {
    LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
        "Initialize haptic storage cache failed\n");
  }
}

HapticPreferences GetHapticPreferences() {
  HapticBlob blob;
  g_haptic_cache.Read(&blob);
  return {blob.enabled != 0, blob.strength_percent};
}

bool UpdateHapticPreferences(const HapticPreferences& prefs) {
  HapticBlob blob;
  blob.enabled = prefs.enabled ? 1 : 0;
  blob.strength_percent = static_cast<uint8_t>(
      std::clamp(prefs.strength_percent, 0, 100));
  return g_haptic_cache.Update(blob);
}

StorageStageResult StageHapticStorage(nvs_handle_t handle) {
  const HapticBlob* blob = nullptr;
  if (!g_haptic_cache.BeginFlush(&blob)) {
    return StorageStageResult::kClean;
  }
  if (nvs_set_blob(
      handle, kHapticNvsKey, blob, sizeof(*blob)) != ESP_OK) {
    return StorageStageResult::kFailed;
  }
  return StorageStageResult::kStaged;
}

void FinishHapticStorage(bool committed) {
  g_haptic_cache.FinishFlush(committed);
}

}  // namespace lilygo_box::app
