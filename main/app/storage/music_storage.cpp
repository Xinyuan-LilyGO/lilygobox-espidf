/*
 * @Description: 音乐源文件夹偏好存储实现
 * @Author: LILYGO_L
 * @Date: 2026-07-14 23:25:00
 * @LastEditTime: 2026-07-16 22:38:14
 * @License: GPL 3.0
 */
#include "app/storage/music_storage.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <memory>
#include <new>

#include "app/storage/storage_internal.h"
#include "base/logger.h"
#include "esp_err.h"
#include "nvs.h"

namespace lilygo_box::app {
namespace {

constexpr const char* kNvsNamespace = "settings";
constexpr const char* kMusicSourcesNvsKey = "music_sources";
constexpr uint32_t kMusicSourcesMagic = 0x4D555343;

struct MusicSourcesBlob {
  // 校验当前 NVS 数据是否属于音乐源偏好。
  uint32_t magic = kMusicSourcesMagic;
  // 用户配置的音乐源目录快照。
  MusicSourcePreferences preferences;
};

void ResetMusicSourcePreferences(MusicSourcePreferences* preferences) {
  if (preferences == nullptr) {
    return;
  }
  for (size_t index = 0; index < kMusicSourceCapacity; ++index) {
    std::fill(preferences->paths[index],
        preferences->paths[index] + kMusicSourcePathCapacity, '\0');
  }
}

void ResetMusicSourcesBlob(MusicSourcesBlob* blob) {
  if (blob == nullptr) {
    return;
  }
  blob->magic = kMusicSourcesMagic;
  ResetMusicSourcePreferences(&blob->preferences);
}

void NormalizePath(char* path) {
  path[kMusicSourcePathCapacity - 1] = '\0';
  size_t length = 0;
  while (length < kMusicSourcePathCapacity && path[length] != '\0') {
    ++length;
  }
  std::fill(path + length + 1,
      path + kMusicSourcePathCapacity, '\0');
}

void NormalizeBlob(MusicSourcesBlob* blob) {
  if (blob == nullptr) {
    return;
  }
  blob->magic = kMusicSourcesMagic;
  for (size_t index = 0; index < kMusicSourceCapacity; ++index) {
    NormalizePath(blob->preferences.paths[index]);
  }
}

bool MusicSourcesBlobEqual(
    const MusicSourcesBlob& left, const MusicSourcesBlob& right) {
  if (left.magic != right.magic) {
    return false;
  }
  for (size_t index = 0; index < kMusicSourceCapacity; ++index) {
    if (std::strcmp(left.preferences.paths[index],
            right.preferences.paths[index]) != 0) {
      return false;
    }
  }
  return true;
}

DeferredStorageCache<MusicSourcesBlob> g_music_sources_cache(
    StorageDomain::kMusicSources, MusicSourcesBlobEqual);

void LogMusicStorageError(const char* operation, esp_err_t error) {
  LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
      "Music source NVS %s failed, error=%s\n", operation,
      esp_err_to_name(error));
}

}  // namespace

void InitMusicCache() {
  auto blob = std::unique_ptr<MusicSourcesBlob>(
      new (std::nothrow) MusicSourcesBlob());
  if (blob == nullptr) {
    LogMessage(LogLevel::kError, __FILE__, __LINE__,
        "Allocate music source initialization buffer failed\n");
    return;
  }

  nvs_handle_t handle = 0;
  esp_err_t result = nvs_open(kNvsNamespace, NVS_READONLY, &handle);
  if (result == ESP_OK) {
    size_t size = sizeof(*blob);
    result = nvs_get_blob(
        handle, kMusicSourcesNvsKey, blob.get(), &size);
    nvs_close(handle);
    if (result != ESP_OK || size != sizeof(*blob) ||
        blob->magic != kMusicSourcesMagic) {
      if (result != ESP_ERR_NVS_NOT_FOUND) {
        if (result != ESP_OK) {
          LogMusicStorageError("load", result);
        } else {
          LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
              "Music source NVS blob is invalid\n");
        }
      }
      ResetMusicSourcesBlob(blob.get());
    }
  } else if (result != ESP_ERR_NVS_NOT_FOUND) {
    LogMusicStorageError("open", result);
  }

  NormalizeBlob(blob.get());
  if (!g_music_sources_cache.Initialize(*blob)) {
    LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
        "Initialize music source cache failed\n");
  }
}

bool GetMusicSourcePreferences(MusicSourcePreferences* preferences) {
  if (preferences == nullptr) {
    return false;
  }

  auto blob = std::unique_ptr<MusicSourcesBlob>(
      new (std::nothrow) MusicSourcesBlob());
  if (blob == nullptr || !g_music_sources_cache.Read(blob.get())) {
    ResetMusicSourcePreferences(preferences);
    return false;
  }
  *preferences = blob->preferences;
  return true;
}

bool UpdateMusicSourcePreferences(
    const MusicSourcePreferences& preferences) {
  auto blob = std::unique_ptr<MusicSourcesBlob>(
      new (std::nothrow) MusicSourcesBlob());
  if (blob == nullptr) {
    return false;
  }
  blob->preferences = preferences;
  NormalizeBlob(blob.get());
  return g_music_sources_cache.Update(*blob);
}

StorageStageResult StageMusicStorage(nvs_handle_t handle) {
  const MusicSourcesBlob* blob = nullptr;
  if (!g_music_sources_cache.BeginFlush(&blob)) {
    return StorageStageResult::kClean;
  }

  const esp_err_t result = nvs_set_blob(
      handle, kMusicSourcesNvsKey, blob, sizeof(*blob));
  if (result != ESP_OK) {
    LogMusicStorageError("stage", result);
    return StorageStageResult::kFailed;
  }
  return StorageStageResult::kStaged;
}

void FinishMusicStorage(bool committed) {
  g_music_sources_cache.FinishFlush(committed);
}

}  // namespace lilygo_box::app
