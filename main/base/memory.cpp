/*
 * @Description: 统一内存分配策略、轻量模块计数和按需堆信息采集。
 * @Author: LILYGO_L
 * @Date: 2026-10-07 00:00:00
 * @LastEditTime: 2026-10-07 00:00:00
 * @License: GPL 3.0
 */
#include "base/memory.h"

#include <algorithm>
#include <cassert>
#include <cstring>
#include <limits>

#include "base/logger.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "sdkconfig.h"

namespace lilygo_box::memory {
namespace {

// WLAN 卡顿修复：P4 的 DMA 能力用于排除慢速 RTC RAM/TCM；DEFAULT 进一步
// 排除 SDIO 等专用的保留池。普通小对象只能回退到 PSRAM，不能放宽到任意内部堆。
constexpr uint32_t kFastCaps =
    MALLOC_CAP_DEFAULT | MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA | MALLOC_CAP_8BIT;
constexpr uint32_t kPsramCaps =
    MALLOC_CAP_DEFAULT | MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;
constexpr uint32_t kDmaCaps =
    MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA | MALLOC_CAP_8BIT;
constexpr const char* kModuleNames[] = {"general", "lvgl", "camera", "radio"};
static_assert(sizeof(kModuleNames) / sizeof(kModuleNames[0]) == kModuleCount);

portMUX_TYPE g_stats_lock = portMUX_INITIALIZER_UNLOCKED;
std::array<ModuleStats, kModuleCount> g_stats{};

struct Capabilities {
  uint32_t first;
  uint32_t fallback;
};

/**
 * @brief 将用途策略转换为明确的首选及回退内存能力
 * @param size 请求字节数，用于平衡策略的大小分流
 * @param policy 分配策略
 * @return 能力组合；fallback 为零表示禁止回退，first 为零表示策略无效
 */
Capabilities SelectCapabilities(size_t size, Policy policy) {
  switch (policy) {
    case Policy::kFastInternal:
      return {kFastCaps, 0};
    case Policy::kPsram:
      return {kPsramCaps, 0};
    case Policy::kInternalDma:
      return {kDmaCaps, 0};
    case Policy::kBalanced:
      // 保持已在 P4 上验证的 LVGL 分流阈值与区域回退顺序。
      return size <= CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL
                 ? Capabilities{kFastCaps, kPsramCaps}
                 : Capabilities{kPsramCaps, kFastCaps};
  }
  // 未知策略拒绝分配，不悄悄退回系统默认 malloc。
  return {0, 0};
}

/**
 * @brief 校验模块编号并转换为固定统计数组的索引
 * @param module 所属模块，不可传入 kCount
 * @return 有效模块索引
 */
size_t ModuleIndex(Module module) {
  const size_t index = static_cast<size_t>(module);
  assert(index < kModuleCount);
  return index;
}

/**
 * @brief 在短临界区内更新模块存活字节数、峰值和内存块数量
 * @param module 所属模块
 * @param old_bytes 调整前计入统计的实际字节数
 * @param new_bytes 调整后计入统计的实际字节数
 * @param had_block 本次操作前是否存在原内存块
 * @param has_block 本次操作后是否存在新内存块
 */
void RecordChange(Module module, size_t old_bytes, size_t new_bytes,
                  bool had_block, bool has_block) {
  const size_t index = ModuleIndex(module);
  // 临界区仅更新固定大小的计数，不分配内存、不查询堆、不输出日志。
  portENTER_CRITICAL(&g_stats_lock);
  auto& stats = g_stats[index];
  assert(stats.current_bytes >= old_bytes);
  assert(!had_block || stats.live_blocks > 0);
  stats.current_bytes = stats.current_bytes - old_bytes + new_bytes;
  stats.peak_bytes = std::max(stats.peak_bytes, stats.current_bytes);
  if (had_block) --stats.live_blocks;
  if (has_block) ++stats.live_blocks;
  portEXIT_CRITICAL(&g_stats_lock);
}

/**
 * @brief 累计分配失败并按指数间隔记录日志，避免连续失败刷屏
 * @param module 分配所属模块
 * @param size 申请字节数，乘法溢出时使用 size_t 最大值表示
 * @param policy 本次分配策略
 */
void RecordFailure(Module module, size_t size, Policy policy) {
  const size_t index = ModuleIndex(module);
  portENTER_CRITICAL(&g_stats_lock);
  auto& failures = g_stats[index].failures;
  if (failures < std::numeric_limits<uint32_t>::max()) ++failures;
  const uint32_t count = failures;
  portEXIT_CRITICAL(&g_stats_lock);
  // 连续失败只在第 1、2、4、8...次输出，避免内存紧张时日志反过来拖慢界面。
  if ((count & (count - 1)) == 0) {
    LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
               "Memory allocation failed: module=%s bytes=%zu policy=%u "
               "failures=%lu\n",
               kModuleNames[index], size, static_cast<unsigned>(policy),
               static_cast<unsigned long>(count));
  }
}

/**
 * @brief 读取满足指定能力的系统堆容量及碎片相关信息
 * @param caps ESP-IDF 内存能力组合
 * @return 堆总量、空闲量、历史最低空闲量和最大连续空闲块
 */
HeapStats ReadHeap(uint32_t caps) {
  multi_heap_info_t info{};
  heap_caps_get_info(&info, caps);
  return {heap_caps_get_total_size(caps), info.total_free_bytes,
          info.minimum_free_bytes, info.largest_free_block};
}

/**
 * @brief 使用项目日志格式输出单个堆区域的统计快照
 * @param name 区域名称
 * @param stats 已采集的统计信息，不在输出时重新查询堆
 */
void LogHeap(const char* name, const HeapStats& stats) {
  LogMessage(
      LogLevel::kInfo, __FILE__, __LINE__,
      "Memory heap=%s total=%zu free=%zu minimum_free=%zu largest=%zu bytes\n",
      name, stats.total_bytes, stats.free_bytes, stats.minimum_free_bytes,
      stats.largest_free_block);
}

}  // namespace

void* Allocate(size_t size, Policy policy, Module module) {
  ModuleIndex(module);
  if (size == 0) return nullptr;
  const auto caps = SelectCapabilities(size, policy);
  void* pointer = nullptr;
  if (caps.fallback != 0) {
    pointer = heap_caps_malloc_prefer(size, 2, caps.first, caps.fallback);
  } else if (caps.first != 0) {
    pointer = heap_caps_malloc(size, caps.first);
  }
  if (pointer == nullptr) {
    RecordFailure(module, size, policy);
    return nullptr;
  }
  RecordChange(module, 0, heap_caps_get_allocated_size(pointer), false, true);
  return pointer;
}

void* AllocateZeroed(size_t count, size_t size, Policy policy, Module module) {
  ModuleIndex(module);
  if (count == 0 || size == 0) return nullptr;
  if (count > std::numeric_limits<size_t>::max() / size) {
    RecordFailure(module, std::numeric_limits<size_t>::max(), policy);
    return nullptr;
  }
  void* pointer = Allocate(count * size, policy, module);
  if (pointer != nullptr) std::memset(pointer, 0, count * size);
  return pointer;
}

void* Reallocate(void* pointer, size_t size, Policy policy, Module module) {
  ModuleIndex(module);
  if (pointer == nullptr) return Allocate(size, policy, module);
  if (size == 0) {
    Free(pointer, module);
    return nullptr;
  }
  const size_t old_bytes = heap_caps_get_allocated_size(pointer);
  const auto caps = SelectCapabilities(size, policy);
  void* resized = nullptr;
  if (caps.fallback != 0) {
    resized =
        heap_caps_realloc_prefer(pointer, size, 2, caps.first, caps.fallback);
  } else if (caps.first != 0) {
    resized = heap_caps_realloc(pointer, size, caps.first);
  }
  if (resized == nullptr) {
    RecordFailure(module, size, policy);
    return nullptr;
  }
  RecordChange(module, old_bytes, heap_caps_get_allocated_size(resized), true,
               true);
  return resized;
}

void Free(void* pointer, Module module) {
  ModuleIndex(module);
  if (pointer == nullptr) return;
  const size_t bytes = heap_caps_get_allocated_size(pointer);
  RecordChange(module, bytes, 0, true, false);
  heap_caps_free(pointer);
}

Snapshot ReadSnapshot() {
  Snapshot snapshot{};
  portENTER_CRITICAL(&g_stats_lock);
  snapshot.modules = g_stats;
  portEXIT_CRITICAL(&g_stats_lock);
  // 各区域分别采样；系统任务仍会分配内存，因此不是跨所有堆的原子快照。
  snapshot.default_heap = ReadHeap(MALLOC_CAP_DEFAULT);
  snapshot.internal = ReadHeap(MALLOC_CAP_INTERNAL);
  snapshot.fast_internal = ReadHeap(kFastCaps);
  snapshot.psram = ReadHeap(MALLOC_CAP_SPIRAM);
  return snapshot;
}

void LogSnapshot() {
  if (!ShouldLog(LogLevel::kInfo)) return;
  const Snapshot snapshot = ReadSnapshot();
  LogHeap("default", snapshot.default_heap);
  LogHeap("internal", snapshot.internal);
  LogHeap("fast_internal", snapshot.fast_internal);
  LogHeap("psram", snapshot.psram);
  for (size_t i = 0; i < kModuleCount; ++i) {
    const auto& stats = snapshot.modules[i];
    LogMessage(
        LogLevel::kInfo, __FILE__, __LINE__,
        "Memory module=%s current=%zu peak=%zu bytes blocks=%zu failures=%lu\n",
        kModuleNames[i], stats.current_bytes, stats.peak_bytes,
        stats.live_blocks, static_cast<unsigned long>(stats.failures));
  }
}

}  // namespace lilygo_box::memory
