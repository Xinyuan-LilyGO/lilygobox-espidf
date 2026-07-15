/*
 * @Description: 音乐源文件夹偏好存储实现
 * @Author: LILYGO_L
 * @Date: 2026-07-14 23:25:00
 * @LastEditTime: 2026-07-14 23:25:00
 * @License: GPL 3.0
 */
#include "app/storage/music_storage.h"

#include <cstdint>

#include "base/logger.h"
#include "esp_err.h"
#include "nvs.h"

namespace lilygo_box::app {
namespace {

constexpr const char* kNvsNamespace = "settings";
constexpr const char* kMusicSourcesNvsKey = "music_sources";
constexpr uint32_t kMusicSourcesMagic = 0x4D555343;

struct MusicSourcesBlob {
  uint32_t magic = kMusicSourcesMagic;
  MusicSourcePreferences preferences;
};

/**
 * @brief 记录音乐源 NVS 操作失败信息
 * @param operation 操作名称
 * @param error ESP-IDF 错误码
 */
void LogMusicStorageError(const char* operation, esp_err_t error) {
  LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
      "Music source NVS %s failed, error=%s\n", operation,
      esp_err_to_name(error));
}

}  // namespace

bool LoadMusicSourcePreferences(MusicSourcePreferences* preferences) {
  if (preferences == nullptr) {
    return false;
  }
  *preferences = MusicSourcePreferences{};
  nvs_handle_t handle = 0;
  esp_err_t result = nvs_open(kNvsNamespace, NVS_READONLY, &handle);
  if (result == ESP_ERR_NVS_NOT_FOUND) {
    return true;
  }
  if (result != ESP_OK) {
    LogMusicStorageError("open", result);
    return false;
  }
  MusicSourcesBlob blob;
  size_t size = sizeof(blob);
  result = nvs_get_blob(handle, kMusicSourcesNvsKey, &blob, &size);
  nvs_close(handle);
  if (result == ESP_ERR_NVS_NOT_FOUND) {
    return true;
  }
  if (result != ESP_OK || size != sizeof(blob) ||
      blob.magic != kMusicSourcesMagic) {
    if (result != ESP_OK) {
      LogMusicStorageError("load", result);
    }
    return false;
  }
  *preferences = blob.preferences;
  for (size_t index = 0; index < kMusicSourceCapacity; ++index) {
    preferences->paths[index][kMusicSourcePathCapacity - 1] = '\0';
  }
  return true;
}

bool SaveMusicSourcePreferences(
    const MusicSourcePreferences& preferences) {
  const MusicSourcesBlob blob = {
      .magic = kMusicSourcesMagic,
      .preferences = preferences,
  };
  nvs_handle_t handle = 0;
  esp_err_t result = nvs_open(kNvsNamespace, NVS_READWRITE, &handle);
  if (result == ESP_OK) {
    result = nvs_set_blob(
        handle, kMusicSourcesNvsKey, &blob, sizeof(blob));
    if (result == ESP_OK) {
      result = nvs_commit(handle);
    }
    nvs_close(handle);
  }
  if (result != ESP_OK) {
    LogMusicStorageError("save", result);
  }
  return result == ESP_OK;
}

}  // namespace lilygo_box::app
