/*
 * @Description: 集中创建内部或 PSRAM 栈任务，使用预分配服务安全回收临时任务。
 */
#include "base/task.h"

#include <new>

#include "base/logger.h"
#include "base/memory.h"
#include "esp_heap_caps.h"
#include "freertos/idf_additions.h"
#include "freertos/queue.h"
#include "sdkconfig.h"

namespace lilygo_box::tasks {
namespace {

struct Work {
  TaskFunction_t entry;
  void* context;
};
struct Completed {
  TaskHandle_t task;
  Work* work;
};

// 静态资源在启动时就保留，避免低内存时为回收任务再申请内部栈。
constexpr uint32_t kReaperStackBytes = 3072;
constexpr UBaseType_t kReaperQueueLength = 16;
StaticTask_t g_reaper_tcb;
StackType_t g_reaper_stack[kReaperStackBytes / sizeof(StackType_t)];
StaticQueue_t g_queue_control;
uint8_t g_queue_storage[kReaperQueueLength * sizeof(Completed)];
QueueHandle_t g_completed_queue = nullptr;
TaskHandle_t g_reaper = nullptr;

// 在独立任务中删除已完成任务，释放控制块、栈及包装参数。
void ReaperEntry(void*) {
  for (;;) {
    Completed completed{};
    if (xQueueReceive(g_completed_queue, &completed, portMAX_DELAY) == pdTRUE) {
      vTaskDeleteWithCaps(completed.task);
      memory::Free(completed.work, memory::Module::kGeneral);
    }
  }
}

// 内存不足时同步排空已完成任务队列，不等待正在执行业务的任务。
bool ReclaimCompleted(void*) {
  if (g_completed_queue == nullptr) return false;
  bool reclaimed = false;
  Completed completed{};
  for (UBaseType_t index = 0; index < kReaperQueueLength; ++index) {
    if (xQueueReceive(g_completed_queue, &completed, 0) != pdTRUE) break;
    vTaskDeleteWithCaps(completed.task);
    memory::Free(completed.work, memory::Module::kGeneral);
    reclaimed = true;
  }
  return reclaimed;
}

// 入口正常返回并析构局部对象后，移交回收队列并停止访问业务上下文。
void DetachedEntry(void* context) {
  auto* work = static_cast<Work*>(context);
  work->entry(work->context);
  const Completed completed{xTaskGetCurrentTaskHandle(), work};
  xQueueSend(g_completed_queue, &completed, portMAX_DELAY);
  // 回收任务可能在另一核心立即删除本任务，之后不再访问 work 或业务对象。
  for (;;) vTaskSuspend(nullptr);
}

void LogFailure(const char* name, uint32_t bytes, StackRegion region) {
  constexpr uint32_t kInternal = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
  constexpr uint32_t kPsram = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;
  LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
      "Task creation failed: name=%s stack=%lu region=%s, "
      "internal free/largest=%zu/%zu, PSRAM free/largest=%zu/%zu bytes\n",
      name != nullptr ? name : "?", static_cast<unsigned long>(bytes),
      region == StackRegion::kPsram ? "PSRAM" : "internal",
      heap_caps_get_free_size(kInternal), heap_caps_get_largest_free_block(kInternal),
      heap_caps_get_free_size(kPsram), heap_caps_get_largest_free_block(kPsram));
}

}  // namespace

bool Initialize() {
  // 仅由 app_main 在发布应用和工作任务前调用，不支持并发初始化。
  if (g_reaper != nullptr) return true;
  g_completed_queue = xQueueCreateStatic(kReaperQueueLength, sizeof(Completed),
      g_queue_storage, &g_queue_control);
  if (g_completed_queue == nullptr) return false;
  g_reaper = xTaskCreateStatic(ReaperEntry, "task_reaper", kReaperStackBytes,
      nullptr, 5, g_reaper_stack, &g_reaper_tcb);
  return g_reaper != nullptr &&
      memory::RegisterReclaimer(ReclaimCompleted, nullptr);
}

BaseType_t CreateInternal(TaskFunction_t entry, const char* name,
    uint32_t stack_bytes, void* context, UBaseType_t priority,
    TaskHandle_t* handle) {
  if (handle != nullptr) *handle = nullptr;
  if (entry == nullptr || name == nullptr || stack_bytes == 0) return pdFAIL;
  BaseType_t result = xTaskCreate(entry, name, stack_bytes,
      context, priority, handle);
  if (result != pdPASS && memory::TryReclaim()) {
    result = xTaskCreate(entry, name, stack_bytes, context, priority, handle);
  }
  if (result != pdPASS) LogFailure(name, stack_bytes, StackRegion::kInternal);
  return result;
}

BaseType_t CreateManaged(TaskFunction_t entry, const char* name,
    uint32_t stack_bytes, void* context, UBaseType_t priority,
    TaskHandle_t* handle, StackRegion region) {
  if (handle == nullptr) return pdFAIL;
  *handle = nullptr;
  if (entry == nullptr || name == nullptr || stack_bytes == 0 ||
      (region != StackRegion::kPsram && region != StackRegion::kInternal)) {
    return pdFAIL;
  }
  UBaseType_t caps = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
  if (region == StackRegion::kPsram) {
#if defined(CONFIG_FREERTOS_TASK_CREATE_ALLOW_EXT_MEM)
    caps = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;
#else
    // 未启用外部栈的构建拒绝请求，调用方必须明确选择内部栈。
    LogFailure(name, stack_bytes, region);
    return pdFAIL;
#endif
  }
  BaseType_t result = xTaskCreateWithCaps(entry, name, stack_bytes,
      context, priority, handle, caps);
  if (result != pdPASS && memory::TryReclaim()) {
    result = xTaskCreateWithCaps(entry, name, stack_bytes, context, priority,
        handle, caps);
  }
  if (result != pdPASS) LogFailure(name, stack_bytes, region);
  return result;
}

void DeleteManaged(TaskHandle_t handle) {
  configASSERT(handle != nullptr && handle != xTaskGetCurrentTaskHandle());
  if (handle != nullptr && handle != xTaskGetCurrentTaskHandle()) {
    vTaskDeleteWithCaps(handle);
  }
}

BaseType_t CreateDetached(TaskFunction_t entry, const char* name,
    uint32_t stack_bytes, void* context, UBaseType_t priority,
    StackRegion region) {
  if (g_reaper == nullptr || entry == nullptr) return pdFAIL;
  auto* work = static_cast<Work*>(memory::Allocate(
      sizeof(Work), memory::Policy::kPsram, memory::Module::kGeneral));
  if (work == nullptr) {
    LogFailure(name, stack_bytes, region);
    return pdFAIL;
  }
  new (work) Work{entry, context};
  TaskHandle_t handle = nullptr;
  const BaseType_t result = CreateManaged(DetachedEntry, name, stack_bytes,
      work, priority, &handle, region);
  // 创建成功后 work 可能已经被回收，禁止再读取或修改它。
  if (result != pdPASS) memory::Free(work, memory::Module::kGeneral);
  return result;
}

}  // namespace lilygo_box::tasks
