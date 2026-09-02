/*
 * @Description: 音乐源文件夹偏好存储实现
 * @Author: LILYGO_L
 * @Date: 2026-07-14 23:25:00
 * @LastEditTime: 2026-09-02 17:51:33
 * @License: GPL 3.0
 */
#include "app/storage/music_storage.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <memory>
#include <new>

#include "app/storage/storage_internal.h"
#include "app/storage/tlv_storage.h"
#include "base/logger.h"
#include "esp_err.h"
#include "nvs.h"

namespace lilygo_box::app {
namespace {

constexpr const char* kMusicSourcesNvsNamespace = "settings";
constexpr const char* kMusicSourcesNvsKey = "music_sources";
constexpr size_t kMusicSourcesTlvCapacity = 3200;

// 已分配字段编号只允许保留，禁止改号或复用。
enum class MusicSourcesField : uint16_t {
  // 每个目录槽位使用一个重复字段，空字符串也保留槽位顺序。
  kPath = 1,
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

void NormalizePath(char* path) {
  path[kMusicSourcePathCapacity - 1] = '\0';
  size_t length = 0;
  while (length < kMusicSourcePathCapacity && path[length] != '\0') {
    ++length;
  }
  std::fill(path + length + 1, path + kMusicSourcePathCapacity, '\0');
}

void NormalizeMusicSourcePreferences(MusicSourcePreferences* preferences) {
  if (preferences == nullptr) {
    return;
  }
  for (size_t index = 0; index < kMusicSourceCapacity; ++index) {
    NormalizePath(preferences->paths[index]);
  }
}

bool AreMusicSourcePreferencesEqual(
    const MusicSourcePreferences& left, const MusicSourcePreferences& right) {
  for (size_t index = 0; index < kMusicSourceCapacity; ++index) {
    if (std::strcmp(left.paths[index], right.paths[index]) != 0) {
      return false;
    }
  }
  return true;
}

bool DecodeMusicSourcePreferences(
    const storage::TlvBuffer& buffer, MusicSourcePreferences* preferences) {
  if (preferences == nullptr) {
    return false;
  }
  ResetMusicSourcePreferences(preferences);
  size_t path_count = 0;
  storage::TlvReader reader(
      storage::TlvDomain::kMusicSources, buffer.data.get(), buffer.size);
  storage::TlvField field;
  while (true) {
    const storage::TlvReadResult result = reader.Next(&field);
    if (result == storage::TlvReadResult::kEnd) {
      NormalizeMusicSourcePreferences(preferences);
      return true;
    }
    if (result == storage::TlvReadResult::kInvalid) {
      return false;
    }
    if (static_cast<MusicSourcesField>(field.tag()) !=
        MusicSourcesField::kPath) {
      continue;
    }
    if (path_count >= kMusicSourceCapacity) {
      continue;
    }
    if (!field.CopyString(
            preferences->paths[path_count], kMusicSourcePathCapacity)) {
      return false;
    }
    ++path_count;
  }
}

bool EncodeMusicSourcePreferences(const MusicSourcePreferences& preferences,
    uint8_t* output, size_t capacity, size_t* encoded_size) {
  storage::TlvWriter writer(
      storage::TlvDomain::kMusicSources, output, capacity);
  for (size_t index = 0; index < kMusicSourceCapacity; ++index) {
    if (!writer.WriteString(static_cast<uint16_t>(MusicSourcesField::kPath),
            preferences.paths[index], kMusicSourcePathCapacity)) {
      return false;
    }
  }
  return writer.Finalize(encoded_size);
}

NvsStorageCache<MusicSourcePreferences> g_music_sources_cache(
    StorageDomain::kMusicSources, AreMusicSourcePreferencesEqual);

void LogMusicStorageError(const char* operation, esp_err_t error) {
  LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
      "Music source NVS %s failed, error=%s\n", operation,
      esp_err_to_name(error));
}

}  // namespace

void InitMusicCache() {
  auto loaded = std::unique_ptr<MusicSourcePreferences>(
      new (std::nothrow) MusicSourcePreferences());
  if (loaded == nullptr) {
    LogMessage(LogLevel::kError, __FILE__, __LINE__,
        "Allocate music source initialization buffer failed\n");
    return;
  }
  nvs_handle_t handle = 0;
  const esp_err_t open_result =
      OpenApplicationNvs(kMusicSourcesNvsNamespace, NVS_READONLY, &handle);
  if (open_result == ESP_OK) {
    storage::TlvBuffer buffer;
    esp_err_t error = ESP_OK;
    const storage::TlvLoadResult result = storage::LoadTlvBuffer(handle,
        kMusicSourcesNvsKey, storage::TlvDomain::kMusicSources,
        kMusicSourcesTlvCapacity, &buffer, &error);
    nvs_close(handle);
    if (result == storage::TlvLoadResult::kLoaded &&
        !DecodeMusicSourcePreferences(buffer, loaded.get())) {
      LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
          "Music source TLV payload is invalid\n");
      ResetMusicSourcePreferences(loaded.get());
    } else if (result == storage::TlvLoadResult::kInvalid) {
      LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
          "Music source TLV container is invalid\n");
    } else if (result == storage::TlvLoadResult::kError) {
      LogMusicStorageError("load", error);
    }
  } else if (open_result != ESP_ERR_NVS_NOT_FOUND) {
    LogMusicStorageError("open", open_result);
  }
  NormalizeMusicSourcePreferences(loaded.get());
  if (!g_music_sources_cache.Initialize(*loaded)) {
    LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
        "Initialize music source cache failed\n");
  }
}

bool GetMusicSourcePreferences(MusicSourcePreferences* preferences) {
  if (preferences == nullptr) {
    return false;
  }
  if (!g_music_sources_cache.Read(preferences)) {
    ResetMusicSourcePreferences(preferences);
    return false;
  }
  return true;
}

bool UpdateMusicSourcePreferences(const MusicSourcePreferences& preferences) {
  auto normalized = std::unique_ptr<MusicSourcePreferences>(
      new (std::nothrow) MusicSourcePreferences(preferences));
  if (normalized == nullptr) {
    return false;
  }
  NormalizeMusicSourcePreferences(normalized.get());
  return g_music_sources_cache.UpdateAndPersist(*normalized);
}

StorageStageResult StageMusicStorage(nvs_handle_t handle) {
  const MusicSourcePreferences* preferences = nullptr;
  if (!g_music_sources_cache.BeginFlush(&preferences)) {
    return StorageStageResult::kClean;
  }
  auto buffer = std::unique_ptr<uint8_t[]>(
      new (std::nothrow) uint8_t[kMusicSourcesTlvCapacity]);
  if (buffer == nullptr) {
    return StorageStageResult::kFailed;
  }
  size_t encoded_size = 0;
  if (!EncodeMusicSourcePreferences(*preferences, buffer.get(),
          kMusicSourcesTlvCapacity, &encoded_size)) {
    return StorageStageResult::kFailed;
  }
  const esp_err_t result =
      nvs_set_blob(handle, kMusicSourcesNvsKey, buffer.get(), encoded_size);
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
