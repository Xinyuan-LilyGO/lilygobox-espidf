/*
 * @Description: 偏好存储统一管理实现
 * @Author: LILYGO_L
 * @Date: 2026-07-03 00:00:00
 * @LastEditTime: 2026-07-17 17:22:18
 * @License: GPL 3.0
 */
#include "app/storage/storage.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <new>

#include "app/radio_chat_repository.h"
#include "app/storage/display_storage.h"
#include "app/storage/first_boot_storage.h"
#include "app/storage/haptic_storage.h"
#include "app/storage/littlefs_storage.h"
#include "app/storage/music_storage.h"
#include "app/storage/radio_storage.h"
#include "app/storage/sound_storage.h"
#include "app/storage/storage_internal.h"
#include "app/storage/wifi_storage.h"
#include "base/logger.h"
#include "esp_err.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "nvs_flash.h"

namespace lilygo_box::app {
namespace {

constexpr size_t kMaximumFlushPasses = 3;
constexpr const char* kNvsNamespace = "settings";

struct StorageBackend {
  // 将一个存储域的固定 RAM 快照写入当前 NVS 事务。
  StorageStageResult (*stage)(nvs_handle_t handle);
  // 根据统一事务结果推进或保留该存储域的写入快照。
  void (*finish)(bool committed);
};

constexpr StorageBackend kStorageBackends[] = {
    {StageDisplayStorage, FinishDisplayStorage},
    {StageFirstBootStorage, FinishFirstBootStorage},
    {StageHapticStorage, FinishHapticStorage},
    {StageMusicStorage, FinishMusicStorage},
    {StageRadioStorage, FinishRadioStorage},
    {StageSoundStorage, FinishSoundStorage},
    {StageWifiPreferencesStorage, FinishWifiPreferencesStorage},
    {StageWifiSavedNetworksStorage, FinishWifiSavedNetworksStorage},
};
constexpr size_t kStorageBackendCount =
    sizeof(kStorageBackends) / sizeof(kStorageBackends[0]);
constexpr size_t kStorageDomainCount =
    static_cast<size_t>(StorageDomain::kCount);
static_assert(kStorageBackendCount == kStorageDomainCount,
    "Every storage domain must have a backend");
static_assert(kStorageDomainCount <= sizeof(uint32_t) * 8,
    "Storage dirty bitmap is too small");

using StorageStageResults =
    std::array<StorageStageResult, kStorageBackendCount>;

StaticSemaphore_t g_cache_mutex_buffer;
StaticSemaphore_t g_storage_io_mutex_buffer;
SemaphoreHandle_t g_cache_mutex = nullptr;
SemaphoreHandle_t g_storage_io_mutex = nullptr;
uint32_t g_dirty_domains = 0;
// 重启或关机最终检查期间拒绝新的 RAM 偏好更新。
bool g_updates_frozen = false;

uint32_t DomainBit(StorageDomain domain) {
  const auto index = static_cast<uint8_t>(domain);
  if (index >= static_cast<uint8_t>(StorageDomain::kCount)) {
    return 0;
  }
  return uint32_t{1} << index;
}

bool InitializeStorageCoordinator() {
  if (g_cache_mutex == nullptr) {
    g_cache_mutex = xSemaphoreCreateMutexStatic(&g_cache_mutex_buffer);
  }
  if (g_storage_io_mutex == nullptr) {
    g_storage_io_mutex =
        xSemaphoreCreateMutexStatic(&g_storage_io_mutex_buffer);
  }
  if (g_cache_mutex != nullptr && g_storage_io_mutex != nullptr) {
    return true;
  }
  LogMessage(LogLevel::kError, __FILE__, __LINE__,
      "Initialize storage coordinator mutex failed\n");
  return false;
}

void StageStorageBackends(nvs_handle_t handle,
    StorageStageResults* stages, bool* has_staged, bool* has_failed) {
  if (stages == nullptr || has_staged == nullptr || has_failed == nullptr) {
    return;
  }
  *has_staged = false;
  *has_failed = false;
  for (size_t index = 0; index < kStorageBackendCount; ++index) {
    (*stages)[index] = kStorageBackends[index].stage(handle);
    if ((*stages)[index] == StorageStageResult::kStaged) {
      *has_staged = true;
    } else if ((*stages)[index] == StorageStageResult::kFailed) {
      *has_failed = true;
    }
  }
}

void FinishStorageBackends(
    const StorageStageResults& stages, bool transaction_committed) {
  for (size_t index = 0; index < kStorageBackendCount; ++index) {
    const bool domain_committed =
        transaction_committed &&
        stages[index] == StorageStageResult::kStaged;
    kStorageBackends[index].finish(domain_committed);
  }
}

bool FlushStoragePass() {
  nvs_handle_t handle = 0;
  esp_err_t result = nvs_open(kNvsNamespace, NVS_READWRITE, &handle);
  if (result != ESP_OK) {
    LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
        "Open NVS for deferred flush failed: %s\n",
        esp_err_to_name(result));
    return false;
  }

  StorageStageResults stages = {};
  bool has_staged = false;
  bool has_failed = false;
  StageStorageBackends(
      handle, &stages, &has_staged, &has_failed);
  if (has_staged) {
    result = nvs_commit(handle);
  }
  nvs_close(handle);

  const bool transaction_committed = result == ESP_OK;
  FinishStorageBackends(stages, transaction_committed);
  if (!transaction_committed) {
    LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
        "Deferred NVS flush failed: %s\n", esp_err_to_name(result));
  }
  if (has_failed) {
    LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
        "One or more deferred NVS domains could not be staged\n");
  }
  return transaction_committed && !has_failed;
}

/**
 * @brief 在启动阶段将现有 Radio 配置的 LittleFS 聊天记录预载到 RAM
 */
void InitRadioChatCache() {
  RadioChatRepository& repository = GetRadioChatRepository();
  if (!repository.Initialize()) {
    LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
        "Initialize Radio chat cache failed\n");
    return;
  }

  auto preferences = std::unique_ptr<RadioPreferences>(
      new (std::nothrow) RadioPreferences{});
  if (preferences == nullptr || !GetRadioPreferences(preferences.get())) {
    LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
        "Read Radio preferences for chat cache failed\n");
    return;
  }

  std::array<uint32_t, kRadioProfileCapacity> profile_ids = {};
  for (size_t index = 0; index < preferences->profile_count; ++index) {
    profile_ids[index] = preferences->profiles[index].id;
  }
  if (!repository.LoadProfiles(
          profile_ids.data(), preferences->profile_count)) {
    LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
        "Preload Radio chat history failed\n");
    return;
  }
  LogMessage(LogLevel::kInfo, __FILE__, __LINE__,
      "Radio chat cache loaded: profiles=%u, records=%u\n",
      static_cast<unsigned>(preferences->profile_count),
      static_cast<unsigned>(repository.GetCachedMessageCount()));
}

}  // namespace

StorageCacheLock::StorageCacheLock() {
  if (g_cache_mutex != nullptr) {
    locked_ = xSemaphoreTake(g_cache_mutex, portMAX_DELAY) == pdTRUE;
  }
}

StorageCacheLock::~StorageCacheLock() {
  if (locked_) {
    xSemaphoreGive(g_cache_mutex);
  }
}

void SetStorageDomainDirtyLocked(StorageDomain domain, bool dirty) {
  const uint32_t bit = DomainBit(domain);
  if (dirty) {
    g_dirty_domains |= bit;
  } else {
    g_dirty_domains &= ~bit;
  }
}

bool IsStorageDomainDirtyLocked(StorageDomain domain) {
  return (g_dirty_domains & DomainBit(domain)) != 0;
}

bool AreStorageUpdatesFrozenLocked() {
  return g_updates_frozen;
}

void InitStorage() {
  if (!InitializeStorageCoordinator()) {
    return;
  }
  InitDisplayCache();
  InitFirstBootCache();
  InitHapticCache();
  InitMusicCache();
  InitRadioCache();
  InitSoundCache();
  InitWifiCache();
  LogMessage(LogLevel::kInfo, __FILE__, __LINE__,
      "NVS caches loaded: domains=%u, status=ready\n",
      static_cast<unsigned>(kStorageDomainCount));
  InitRadioChatCache();
}

bool HasPendingStorageWrites() {
  bool nvs_dirty = false;
  {
    StorageCacheLock lock;
    nvs_dirty = lock.IsLocked() && g_dirty_domains != 0;
  }
  return nvs_dirty || GetRadioChatRepository().HasPendingWrites();
}

bool FlushPendingStorageAfterScreenOff() {
  if (g_storage_io_mutex == nullptr ||
      xSemaphoreTake(g_storage_io_mutex, portMAX_DELAY) != pdTRUE) {
    return false;
  }

  for (size_t pass = 0;
       pass < kMaximumFlushPasses && HasPendingStorageWrites(); ++pass) {
    bool nvs_dirty = false;
    {
      StorageCacheLock lock;
      nvs_dirty = lock.IsLocked() && g_dirty_domains != 0;
    }
    if (nvs_dirty) {
      FlushStoragePass();
    }
    GetRadioChatRepository().FlushPending(kRadioChatGlobalCapacity);
  }
  const bool complete = !HasPendingStorageWrites();
  xSemaphoreGive(g_storage_io_mutex);

  if (!complete) {
    LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
        "Deferred storage data remains dirty after %u attempts\n",
        static_cast<unsigned>(kMaximumFlushPasses));
  }
  return complete;
}

bool FreezeStorageUpdatesForShutdown() {
  StorageCacheLock lock;
  if (!lock.IsLocked()) {
    return false;
  }
  g_updates_frozen = true;
  return true;
}

void ResumeStorageUpdatesAfterShutdownFailure() {
  StorageCacheLock lock;
  if (lock.IsLocked()) {
    g_updates_frozen = false;
  }
}

bool FactoryResetAfterScreenOff() {
  if (g_storage_io_mutex == nullptr ||
      xSemaphoreTake(g_storage_io_mutex, portMAX_DELAY) != pdTRUE) {
    return false;
  }

  const esp_err_t result = nvs_flash_erase();
  if (result != ESP_OK) {
    LogMessage(LogLevel::kError, __FILE__, __LINE__,
        "Factory reset failed: %s\n", esp_err_to_name(result));
    xSemaphoreGive(g_storage_io_mutex);
    return false;
  }
  if (!FormatLittleFsStorage()) {
    LogMessage(LogLevel::kError, __FILE__, __LINE__,
        "Factory reset could not clear LittleFS storage\n");
    xSemaphoreGive(g_storage_io_mutex);
    return false;
  }

  LogMessage(LogLevel::kInfo, __FILE__, __LINE__,
      "Factory reset completed, restarting device\n");
  esp_restart();
  xSemaphoreGive(g_storage_io_mutex);
  return false;
}

}  // namespace lilygo_box::app
