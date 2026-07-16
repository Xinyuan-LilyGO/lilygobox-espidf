/*
 * @Description: 首次开机欢迎页完成标志存储实现
 * @Author: LILYGO_L
 * @Date: 2026-07-15 00:00:00
 * @LastEditTime: 2026-07-16 22:35:14
 * @License: GPL 3.0
 */
#include "app/storage/first_boot_storage.h"

#include <cstdint>

#include "app/storage/storage_internal.h"
#include "base/logger.h"
#include "esp_err.h"
#include "nvs.h"

namespace lilygo_box::app {
namespace {

constexpr const char* kNvsNamespace = "settings";
constexpr const char* kNvsKey = "first_boot";

bool AreValuesEqual(const bool& left, const bool& right) {
  return left == right;
}

DeferredStorageCache<bool> g_cache(
    StorageDomain::kFirstBoot, AreValuesEqual);

}  // namespace

void InitFirstBootCache() {
  bool completed = false;
  nvs_handle_t handle = 0;
  if (nvs_open(kNvsNamespace, NVS_READONLY, &handle) == ESP_OK) {
    uint8_t stored = 0;
    if (nvs_get_u8(handle, kNvsKey, &stored) == ESP_OK) {
      completed = stored != 0;
    }
    nvs_close(handle);
  }
  if (!g_cache.Initialize(completed)) {
    LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
        "Initialize first-boot storage cache failed\n");
  }
}

bool IsFirstBootCompleted() {
  bool completed = false;
  g_cache.Read(&completed);
  return completed;
}

bool MarkFirstBootCompleted() {
  return g_cache.Update(true);
}

StorageStageResult StageFirstBootStorage(nvs_handle_t handle) {
  const bool* completed = nullptr;
  if (!g_cache.BeginFlush(&completed)) {
    return StorageStageResult::kClean;
  }
  const uint8_t value = *completed ? 1 : 0;
  if (nvs_set_u8(handle, kNvsKey, value) != ESP_OK) {
    return StorageStageResult::kFailed;
  }
  return StorageStageResult::kStaged;
}

void FinishFirstBootStorage(bool committed) {
  g_cache.FinishFlush(committed);
}

}  // namespace lilygo_box::app
