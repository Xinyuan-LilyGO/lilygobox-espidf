/**
 * @Description: 声音偏好存储实现
 * @Author: LILYGO_L
 * @Date: 2026-06-25 00:00:00
 * @LastEditTime: 2026-07-16 22:35:14
 * @License: GPL 3.0
 */
#include "app/storage/sound_storage.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>

#include "app/storage/storage_internal.h"
#include "base/logger.h"
#include "esp_err.h"
#include "nvs.h"

namespace lilygo_box::app {
namespace {

constexpr const char* kSoundNvsNamespace = "settings";
constexpr const char* kSoundNvsKey = "audio_config";
constexpr uint32_t kSoundMagic = 0x41554450;
constexpr uint16_t kSoundSchemaVersion = 1;

struct SoundBlob {
  // 校验当前 NVS 数据是否属于声音偏好。
  uint32_t magic = kSoundMagic;
  // 当前声音偏好存储结构版本。
  uint16_t schema_version = kSoundSchemaVersion;
  // 写入 NVS 的完整结构大小。
  uint16_t struct_size = sizeof(SoundBlob);
  // 系统输出音量百分比。
  uint8_t volume_percent = 90;
};

SoundBlob NormalizeSoundBlob(const SoundBlob& source) {
  SoundBlob result;
  result.volume_percent = static_cast<uint8_t>(
      std::clamp<int>(source.volume_percent, 0, 100));
  return result;
}

bool AreSoundBlobsEqual(
    const SoundBlob& left, const SoundBlob& right) {
  return left.magic == right.magic &&
      left.schema_version == right.schema_version &&
      left.struct_size == right.struct_size &&
      left.volume_percent == right.volume_percent;
}

DeferredStorageCache<SoundBlob> g_sound_cache(
    StorageDomain::kSound, AreSoundBlobsEqual);

}  // namespace

void InitSoundCache() {
  SoundBlob loaded;
  nvs_handle_t handle = 0;
  if (nvs_open(kSoundNvsNamespace, NVS_READONLY, &handle) == ESP_OK) {
    SoundBlob stored;
    size_t size = sizeof(stored);
    if (nvs_get_blob(handle, kSoundNvsKey, &stored, &size) == ESP_OK &&
        size == sizeof(stored) && stored.magic == kSoundMagic &&
        stored.schema_version == kSoundSchemaVersion &&
        stored.struct_size == sizeof(SoundBlob)) {
      loaded = NormalizeSoundBlob(stored);
    }
    nvs_close(handle);
  }
  if (!g_sound_cache.Initialize(loaded)) {
    LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
        "Initialize sound storage cache failed\n");
  }
}

SoundPreferences GetSoundPreferences() {
  SoundBlob blob;
  g_sound_cache.Read(&blob);
  return {blob.volume_percent};
}

bool UpdateSoundPreferences(const SoundPreferences& prefs) {
  SoundBlob blob;
  blob.volume_percent = static_cast<uint8_t>(
      std::clamp(prefs.volume_percent, 0, 100));
  return g_sound_cache.Update(blob);
}

StorageStageResult StageSoundStorage(nvs_handle_t handle) {
  const SoundBlob* blob = nullptr;
  if (!g_sound_cache.BeginFlush(&blob)) {
    return StorageStageResult::kClean;
  }
  if (nvs_set_blob(handle, kSoundNvsKey, blob, sizeof(*blob)) != ESP_OK) {
    return StorageStageResult::kFailed;
  }
  return StorageStageResult::kStaged;
}

void FinishSoundStorage(bool committed) {
  g_sound_cache.FinishFlush(committed);
}

}  // namespace lilygo_box::app
