/*
 * @Description: Settings storage async task helpers
 * @Author: LILYGO_L
 * @Date: 2026-06-25 00:00:00
 * @LastEditTime: 2026-06-25 00:00:00
 * @License: GPL 3.0
 */
#include "app/storage/storage_task.h"

#include <functional>
#include <new>
#include <utility>

#include "base/logger.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace lilygo_box::app {
namespace {

constexpr uint32_t kStorageTaskStackBytes = 4 * 1024;
constexpr UBaseType_t kStorageTaskPriority = 2;

struct StorageTaskContext {
  std::function<void()> handler;
};

/**
 * @brief 后台存储任务入口
 * @param context StorageTaskContext 指针
 */
void StorageTaskEntry(void* context) {
  auto* task_context = static_cast<StorageTaskContext*>(context);
  if (task_context != nullptr) {
    task_context->handler();
    delete task_context;
  }
  vTaskDelete(nullptr);
}

}  // namespace

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
        name == nullptr ? "storage_task" : name, static_cast<int>(result));
    delete task_context;
    return false;
  }
  return true;
}

}  // namespace lilygo_box::app
