/*
 * @Description: 按用途选择内存区域，并提供已接入模块用量和系统堆快照。
 * @Author: LILYGO_L
 * @Date: 2026-10-07 00:00:00
 * @LastEditTime: 2026-10-07 00:00:00
 * @License: GPL 3.0
 */
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace lilygo_box::memory {

enum class Policy : uint8_t {
  // 小分配优先高速内部 RAM，大分配优先 PSRAM；仅在这两种区域间回退。
  kBalanced,
  // 仅使用普通高速内部堆，不占用系统保留池，不回退到 RTC RAM 或 PSRAM。
  kFastInternal,
  // 大缓冲显式使用 PSRAM，失败时由业务决定是否降低容量。
  kPsram,
  // 仅供确实要求内部 DMA 内存的调用方，可使用 DMA 保留池，不跨区域回退。
  kInternalDma,
};

enum class Module : uint8_t {
  kGeneral,
  kLvgl,
  kCamera,
  kRadio,
  kCount,
};

constexpr size_t kModuleCount = static_cast<size_t>(Module::kCount);

struct ModuleStats {
  // ESP-IDF 返回的实际分配块字节数，可能大于调用方请求的大小。
  size_t current_bytes = 0;
  size_t peak_bytes = 0;
  size_t live_blocks = 0;
  uint32_t failures = 0;
};

struct HeapStats {
  size_t total_bytes = 0;
  size_t free_bytes = 0;
  size_t minimum_free_bytes = 0;
  size_t largest_free_block = 0;
};

struct Snapshot {
  HeapStats default_heap;
  HeapStats internal;
  HeapStats fast_internal;
  HeapStats psram;
  std::array<ModuleStats, kModuleCount> modules;
};

// 以下分配接口仅用于任务上下文，不可在 ISR 中调用。同一指针的申请、扩容、
// 释放必须使用相同 Module，并全部经过本模块；不接受其他分配器产生的指针。
// 不增加块头或地址表，保留原指针对齐；调用方负责指针所有权和并发访问。

/**
 * @brief 按明确策略申请内存，并记录所属模块的实际占用
 * @param size 请求字节数，零返回 nullptr 且不计为失败
 * @param policy 内存区域与回退策略
 * @param module 所属模块，不能为 kCount
 * @return 分配结果，失败返回 nullptr
 */
void* Allocate(size_t size, Policy policy, Module module);

/**
 * @brief 检查乘法溢出后分配并清零内存
 * @param count 元素数量
 * @param size 单个元素字节数
 * @param policy 内存区域与回退策略
 * @param module 所属模块
 * @return 清零后的内存，溢出或分配失败返回 nullptr
 */
void* AllocateZeroed(size_t count, size_t size, Policy policy, Module module);

/**
 * @brief 按策略调整本模块分配的内存，失败时保留原指针、内容和占用统计
 * @param pointer 原指针，为空时等同 Allocate
 * @param size 新字节数，为零时释放并返回 nullptr
 * @param policy 新分配的区域与回退策略
 * @param module 必须与原分配所属模块一致
 * @return 调整后的地址，失败或释放时返回 nullptr
 */
void* Reallocate(void* pointer, size_t size, Policy policy, Module module);

/**
 * @brief 释放本模块分配的内存并扣除统计，空指针不作处理
 * @param pointer 待释放地址，不可重复释放
 * @param module 必须与原分配所属模块一致
 */
void Free(void* pointer, Module module);

/**
 * @brief 读取模块统计和各类系统堆信息，不输出日志，不执行动态分配
 * @return 近似时刻快照；各堆范围可能重叠，不能直接求和
 */
Snapshot ReadSnapshot();

/**
 * @brief 按需通过项目日志输出一次堆余量、最大空闲块及模块当前/峰值用量
 * @note 不安装定时器；只在需要诊断时由调用方主动调用。
 */
void LogSnapshot();

}  // namespace lilygo_box::memory
