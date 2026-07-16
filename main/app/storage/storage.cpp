/*
 * @Description: 偏好存储统一管理实现
 * @Author: LILYGO_L
 * @Date: 2026-07-03 00:00:00
 * @LastEditTime: 2026-07-15 15:53:19
 * @License: GPL 3.0
 */
#include "app/storage/storage.h"

#include <atomic>
#include <functional>
#include <new>
#include <utility>

#include "app/storage/display_storage.h"
#include "app/storage/first_boot_storage.h"
#include "app/storage/haptic_storage.h"
#include "app/storage/rf_storage.h"
#include "app/storage/sound_storage.h"
#include "app/storage/wifi_storage.h"
#include "base/logger.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"

namespace lilygo_box::app {
namespace {

constexpr uint32_t kStorageTaskStackBytes = 4 * 1024;
constexpr UBaseType_t kStorageTaskPriority = 2;
constexpr uint32_t kFactoryResetPreEraseDelayMs = 30;
std::atomic<bool> g_factory_reset_started{false};

struct StorageTaskContext {
  std::function<void()> handler;
};

void StorageTaskEntry(void* context) {
  auto* task_context = static_cast<StorageTaskContext*>(context);
  if (task_context != nullptr) {
    task_context->handler();
    delete task_context;
  }
  vTaskDelete(nullptr);
}

}  // namespace

void InitStorage() {
  InitDisplayCache();
  InitFirstBootCache();
  InitHapticCache();
  InitRfCache();
  InitSoundCache();
  InitWifiCache();
}

bool StartStorageTask(const char* name, std::function<void()> handler) {
  if (!handler) {
    LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
               "StartStorageTask received empty handler\n");
    return false;
  }

  auto* task_context = new (std::nothrow) StorageTaskContext{
      .handler = std::move(handler),
  };
  if (task_context == nullptr) {
    LogMessage(LogLevel::kError, __FILE__, __LINE__,
               "Allocate storage task context failed\n");
    return false;
  }

  const BaseType_t result = xTaskCreate(StorageTaskEntry,
      name == nullptr ? "storage_task" : name, kStorageTaskStackBytes,
      task_context, kStorageTaskPriority, nullptr);
  if (result != pdPASS) {
    LogMessage(LogLevel::kError, __FILE__, __LINE__,
               "Create storage task failed, name=%s, result=%d\n",
               name == nullptr ? "storage_task" : name,
               static_cast<int>(result));
    delete task_context;
    return false;
  }
  return true;
}

bool StartFactoryReset() {
  bool expected = false;
  if (!g_factory_reset_started.compare_exchange_strong(expected, true)) {
    return true;
  }

  const bool started = StartStorageTask("factory_reset", []() {
    vTaskDelay(pdMS_TO_TICKS(kFactoryResetPreEraseDelayMs));
    const esp_err_t result = nvs_flash_erase();
    if (result != ESP_OK) {
      LogMessage(LogLevel::kError, __FILE__, __LINE__,
          "Factory reset failed: %s\n", esp_err_to_name(result));
      g_factory_reset_started.store(false);
      return;
    }

    LogMessage(LogLevel::kInfo, __FILE__, __LINE__,
        "Factory reset completed, restarting device\n");
    esp_restart();
  });
  if (!started) {
    g_factory_reset_started.store(false);
  }
  return started;
}

}  // namespace lilygo_box::app
