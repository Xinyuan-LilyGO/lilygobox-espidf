/*
 * @Description: NVS 与 LittleFS 即时持久化统一管理实现
 * @Author: LILYGO_L
 * @Date: 2026-07-03 00:00:00
 * @LastEditTime: 2026-07-18 00:00:00
 * @License: GPL 3.0
 */
#include "app/storage/storage.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <new>

#include "app/radio_chat_repository.h"
#include "app/storage/battery_storage.h"
#include "app/storage/display_storage.h"
#include "app/storage/first_boot_storage.h"
#include "app/storage/haptic_storage.h"
#include "app/storage/input_method_storage.h"
#include "app/storage/keyboard_expansion_storage.h"
#include "app/storage/littlefs_storage.h"
#include "app/storage/music_storage.h"
#include "app/storage/otg_storage.h"
#include "app/storage/power_state_storage.h"
#include "app/storage/radio_storage.h"
#include "app/storage/sound_storage.h"
#include "app/storage/storage_internal.h"
#include "app/storage/wifi_storage.h"
#include "base/logger.h"
#include "esp_err.h"
#include "esp_partition.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "nvs_flash.h"

namespace lilygo_box::app {
namespace {

constexpr size_t kMaximumShutdownFlushPasses = 3;
constexpr uint32_t kLittleFsFlushMergeDelayMs = 400;
constexpr uint32_t kLittleFsFlushRetryDelayMs = 2 * 1000;
constexpr uint32_t kLittleFsStorageTaskStackBytes = 8 * 1024;
constexpr UBaseType_t kLittleFsStorageTaskPriority = 1;
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
    {StagePowerStateStorage, FinishPowerStateStorage},
    {StageHapticStorage, FinishHapticStorage},
    {StageMusicStorage, FinishMusicStorage},
    {StageRadioStorage, FinishRadioStorage},
    {StageSoundStorage, FinishSoundStorage},
    {StageWifiPreferencesStorage, FinishWifiPreferencesStorage},
    {StageWifiSavedNetworksStorage, FinishWifiSavedNetworksStorage},
    {StageOtgStorage, FinishOtgStorage},
    {StageInputMethodStorage, FinishInputMethodStorage},
    {StageKeyboardExpansionStorage, FinishKeyboardExpansionStorage},
    {StageBatteryStorage, FinishBatteryStorage},
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
std::atomic<TaskHandle_t> g_littlefs_storage_task{nullptr};
std::atomic<bool> g_littlefs_flush_urgent{false};
std::atomic<bool> g_application_nvs_initialized{false};
uint32_t g_dirty_domains = 0;
// 重启或关机最终检查期间拒绝新的 RAM 存储更新。
bool g_updates_frozen = false;

/**
 * @brief 初始化独立应用 NVS 分区，并在格式不兼容时清空重建
 * @return 初始化成功返回 true
 */
bool InitializeApplicationNvs() {
  if (g_application_nvs_initialized.load()) {
    return true;
  }

  esp_err_t result =
      nvs_flash_init_partition(kApplicationNvsPartitionName);
  const char* recovery_reason = nullptr;
  if (result == ESP_ERR_NVS_NO_FREE_PAGES ||
      result == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    recovery_reason = result == ESP_ERR_NVS_NO_FREE_PAGES
                          ? "no free pages"
                          : "new version found";
    result = nvs_flash_erase_partition(kApplicationNvsPartitionName);
    if (result == ESP_OK) {
      result = nvs_flash_init_partition(kApplicationNvsPartitionName);
    }
  }
  if (result != ESP_OK) {
    LogMessage(LogLevel::kError, __FILE__, __LINE__,
        "Initialize application NVS partition failed: %s\n",
        esp_err_to_name(result));
    return false;
  }

  if (recovery_reason != nullptr) {
    LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
        "Application NVS recovered: reason=%s\n", recovery_reason);
  }
  g_application_nvs_initialized.store(true);
  nvs_stats_t statistics = {};
  result = nvs_get_stats(kApplicationNvsPartitionName, &statistics);
  if (result == ESP_OK) {
    LogMessage(LogLevel::kInfo, __FILE__, __LINE__,
        "Application NVS initialized: used=%u, free=%u, "
        "available=%u, total=%u, namespaces=%u\n",
        static_cast<unsigned>(statistics.used_entries),
        static_cast<unsigned>(statistics.free_entries),
        static_cast<unsigned>(statistics.available_entries),
        static_cast<unsigned>(statistics.total_entries),
        static_cast<unsigned>(statistics.namespace_count));
  } else {
    LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
        "Read application NVS statistics failed: %s\n",
        esp_err_to_name(result));
  }
  return true;
}

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
  esp_err_t result =
      OpenApplicationNvs(kNvsNamespace, NVS_READWRITE, &handle);
  if (result != ESP_OK) {
    LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
        "Open NVS for storage flush failed: %s\n",
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
        "NVS storage flush failed: %s\n", esp_err_to_name(result));
  }
  if (has_failed) {
    LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
        "One or more NVS storage domains could not be staged\n");
  }
  return transaction_committed && !has_failed;
}

/**
 * @brief 查询是否仍有配置域需要写入 NVS
 * @return 存在待写配置返回 true
 */
bool HasPendingNvsStorageWrites() {
  StorageCacheLock lock;
  return lock.IsLocked() && g_dirty_domains != 0;
}

/**
 * @brief 后台写入一批 Radio 聊天 LittleFS 数据
 * @return 本批写入成功或当前无待写数据返回 true
 */
bool FlushLittleFsStorageBatch() {
  if (g_storage_io_mutex == nullptr ||
      xSemaphoreTake(g_storage_io_mutex, portMAX_DELAY) != pdTRUE) {
    return false;
  }
  const bool success = GetRadioChatRepository().FlushPending(
      kRadioChatPendingCapacity);
  xSemaphoreGive(g_storage_io_mutex);
  return success;
}

/**
 * @brief 执行 LittleFS 合并写入后台任务
 * @param context 未使用的任务上下文
 */
void LittleFsStorageTaskEntry(void* context) {
  static_cast<void>(context);
  g_littlefs_storage_task.store(xTaskGetCurrentTaskHandle());
  while (true) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    if (!g_littlefs_flush_urgent.exchange(false)) {
      const TickType_t merge_start = xTaskGetTickCount();
      const TickType_t merge_ticks =
          pdMS_TO_TICKS(kLittleFsFlushMergeDelayMs);
      TickType_t elapsed = 0;
      while (!g_littlefs_flush_urgent.exchange(false) &&
             elapsed < merge_ticks) {
        ulTaskNotifyTake(pdTRUE, merge_ticks - elapsed);
        elapsed = xTaskGetTickCount() - merge_start;
      }
    }

    const bool success = FlushLittleFsStorageBatch();
    RadioChatRepository& repository = GetRadioChatRepository();
    if (!success) {
      LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
          "Background LittleFS flush failed, retry scheduled\n");
    }
    if (repository.HasPendingWrites()) {
      if (!success) {
        vTaskDelay(pdMS_TO_TICKS(kLittleFsFlushRetryDelayMs));
      }
      RequestLittleFsStorageFlush(true);
    }
  }
}

/**
 * @brief 启动 LittleFS 合并写入后台任务
 * @return 任务已经运行或创建成功返回 true
 */
bool StartLittleFsStorageTask() {
  if (g_littlefs_storage_task.load() != nullptr) {
    return true;
  }
  TaskHandle_t task = nullptr;
  const BaseType_t result = xTaskCreate(LittleFsStorageTaskEntry,
      "littlefs_save", kLittleFsStorageTaskStackBytes, nullptr,
      kLittleFsStorageTaskPriority, &task);
  if (result != pdPASS || task == nullptr) {
    LogMessage(LogLevel::kError, __FILE__, __LINE__,
        "Create LittleFS storage task failed\n");
    return false;
  }
  g_littlefs_storage_task.store(task);
  return true;
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

bool EraseAllNvsPartitions() {
  g_application_nvs_initialized.store(false);
  esp_partition_iterator_t iterator = esp_partition_find(
      ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_NVS, nullptr);
  if (iterator == nullptr) {
    LogMessage(LogLevel::kError, __FILE__, __LINE__,
        "No NVS partition found during factory reset\n");
    return false;
  }

  bool success = true;
  while (iterator != nullptr) {
    const esp_partition_t* partition = esp_partition_get(iterator);
    if (partition == nullptr) {
      success = false;
      iterator = esp_partition_next(iterator);
      continue;
    }

    const esp_err_t erase_result = nvs_flash_erase_partition_ptr(partition);
    if (erase_result != ESP_OK) {
      LogMessage(LogLevel::kError, __FILE__, __LINE__,
          "Erase NVS partition %s failed: %s\n", partition->label,
          esp_err_to_name(erase_result));
      success = false;
    }
    iterator = esp_partition_next(iterator);
  }
  esp_partition_iterator_release(iterator);
  return success;
}

}  // namespace

bool EnsureStorageCoordinatorInitialized() {
  return InitializeStorageCoordinator();
}

bool EnsureApplicationNvsInitialized() {
  return InitializeApplicationNvs();
}

esp_err_t OpenApplicationNvs(const char* namespace_name,
    nvs_open_mode_t open_mode, nvs_handle_t* handle) {
  return nvs_open_from_partition(kApplicationNvsPartitionName,
      namespace_name, open_mode, handle);
}

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

void RequestLittleFsStorageFlush(bool urgent) {
  if (urgent) {
    g_littlefs_flush_urgent.store(true);
  }
  const TaskHandle_t task = g_littlefs_storage_task.load();
  if (task != nullptr) {
    xTaskNotifyGive(task);
  }
}

void InitStorage(radio::ChipMask supported_radio_chips,
    radio::ChipType primary_radio_chip) {
  if (!EnsureStorageCoordinatorInitialized()) {
    return;
  }
  if (!EnsureApplicationNvsInitialized()) {
    LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
        "Application settings will use defaults until NVS is available\n");
  } else if (!InitPowerStateStorage()) {
    LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
        "Initialize power-state storage failed\n");
  }
  InitDisplayCache();
  InitFirstBootCache();
  InitHapticCache();
  InitMusicCache();
  InitRadioCache(supported_radio_chips, primary_radio_chip);
  InitSoundCache();
  InitWifiCache();
  InitOtgCache();
  InitInputMethodCache();
  InitKeyboardExpansionCache();
  if (!InitBatteryStorage()) {
    LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
        "Initialize battery storage failed\n");
  }
  LogMessage(LogLevel::kInfo, __FILE__, __LINE__,
      "NVS caches loaded: domains=%u, status=ready\n",
      static_cast<unsigned>(kStorageDomainCount));
  InitRadioChatCache();
  if (StartLittleFsStorageTask() &&
      GetRadioChatRepository().HasPendingWrites()) {
    RequestLittleFsStorageFlush(true);
  }
}

bool HasPendingStorageWrites() {
  return HasPendingNvsStorageWrites() ||
      GetRadioChatRepository().HasPendingWrites();
}

bool FlushPendingNvsStorage() {
  if (g_storage_io_mutex == nullptr ||
      xSemaphoreTake(g_storage_io_mutex, portMAX_DELAY) != pdTRUE) {
    return false;
  }

  if (HasPendingNvsStorageWrites()) {
    FlushStoragePass();
  }
  const bool complete = !HasPendingNvsStorageWrites();
  xSemaphoreGive(g_storage_io_mutex);

  if (!complete) {
    LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
        "NVS storage data remains dirty after immediate flush\n");
  }
  return complete;
}

bool FlushPendingStorageBeforeShutdown() {
  if (g_storage_io_mutex == nullptr ||
      xSemaphoreTake(g_storage_io_mutex, portMAX_DELAY) != pdTRUE) {
    return false;
  }

  for (size_t pass = 0;
       pass < kMaximumShutdownFlushPasses &&
       HasPendingStorageWrites(); ++pass) {
    if (HasPendingNvsStorageWrites()) {
      FlushStoragePass();
    }
    GetRadioChatRepository().FlushPending(kRadioChatGlobalCapacity);
  }
  const bool complete = !HasPendingStorageWrites();
  xSemaphoreGive(g_storage_io_mutex);

  if (!complete) {
    LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
        "Pending storage data remains dirty after %u attempts\n",
        static_cast<unsigned>(kMaximumShutdownFlushPasses));
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

  if (!EraseAllLittleFsStorage()) {
    LogMessage(LogLevel::kError, __FILE__, __LINE__,
        "Factory reset could not erase all LittleFS partitions\n");
    InitLittleFsStorage();
    xSemaphoreGive(g_storage_io_mutex);
    return false;
  }

  if (!EraseAllNvsPartitions()) {
    LogMessage(LogLevel::kError, __FILE__, __LINE__,
        "Factory reset could not erase all NVS partitions\n");
    nvs_flash_init();
    InitializeApplicationNvs();
    InitLittleFsStorage();
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
