/*
 * @Description: 按用途选择内存区域，并提供已接入模块用量和系统堆快照。
 */
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <new>
#include <utility>
#include <type_traits>

namespace lilygo_box::memory {

enum class Policy : uint8_t {
  // 小分配优先高速内部 RAM；大于 4 KB 的分配和内部余量不足时仅使用 PSRAM。
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

// 回收器仅在启动阶段注册，context 必须存活到应用结束。回调只能非阻塞地
// 释放明确空闲或已完成的资源，不能分配内存、终止活跃业务或保存 Flash。
using Reclaimer = bool (*)(void* context);

/**
 * @brief 注册低内存回收入口，使用固定槽位，不进行动态分配
 * @return 注册成功或重复注册返回 true；空入口或槽位已满返回 false
 */
bool RegisterReclaimer(Reclaimer reclaimer, void* context);

/**
 * @brief 尝试一次回收可丢弃的后台资源，供分配和任务创建失败路径调用
 * @return 至少有资源被释放时返回 true；并发或嵌套调用直接返回 false
 * @note 不保证回收后有足够或匹配能力的连续块；调用方最多再分配一次。
 */
bool TryReclaim();

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

// 自动释放公共接口申请的普通数组，不接收第三方分配的指针。
template <typename T>
struct BufferDeleter {
  Module module = Module::kGeneral;
  void operator()(T* pointer) const { Free(pointer, module); }
};

template <typename T>
using Buffer = std::unique_ptr<T[], BufferDeleter<T>>;

/**
 * @brief 申请普通数据数组并用独占所有权自动回收，默认仅使用 PSRAM
 * @param count 元素数；乘法溢出或分配失败返回空对象，调用方必须检查
 * @param policy 内存区域策略，默认仅 PSRAM
 * @param module 用量统计所属模块，自动释放时沿用该标签
 * @param zeroed 是否清零，默认不初始化内容
 * @return 独占数组，失败时为空；零数量不计分配失败
 * @note 仅用于平凡数据；不运行构造函数，不代替硬件对齐或 DMA 专用接口。
 */
template <typename T>
Buffer<T> MakeBuffer(size_t count, Policy policy = Policy::kPsram,
    Module module = Module::kGeneral, bool zeroed = false) {
  static_assert(std::is_trivial_v<T>, "Buffer requires trivial elements");
  static_assert(alignof(T) <= alignof(std::max_align_t),
      "Use a dedicated aligned allocator for over-aligned elements");
  // AllocateZeroed 统一检查数量乘法；不清零时先以除法检查溢出。
  void* pointer = nullptr;
  if (zeroed || count > static_cast<size_t>(-1) / sizeof(T)) {
    pointer = AllocateZeroed(count, sizeof(T), policy, module);
  } else {
    pointer = Allocate(count * sizeof(T), policy, module);
  }
  return Buffer<T>(static_cast<T*>(pointer), BufferDeleter<T>{module});
}

// 销毁对象后，通过公共分配接口释放对应内存。
template <typename T>
struct ObjectDeleter {
  Module module = Module::kGeneral;
  void operator()(T* pointer) const {
    if (pointer != nullptr) {
      pointer->~T();
      Free(pointer, module);
    }
  }
};

template <typename T>
using Object = std::unique_ptr<T, ObjectDeleter<T>>;

/**
 * @brief 在 PSRAM 中构造普通业务对象，分配失败时返回空对象
 * @param args 转发给对象构造函数的参数
 * @return 独占对象，分配失败时为空且不运行构造函数
 * @note 构造函数必须不抛异常；对象中的标准容器仍使用各自分配器。
 */
template <typename T, typename... Args>
Object<T> MakePsramObject(Args&&... args) {
  static_assert(std::is_nothrow_constructible_v<T, Args...>,
      "Object construction must not throw");
  static_assert(alignof(T) <= alignof(std::max_align_t),
      "Use a dedicated aligned allocator for over-aligned objects");
  void* storage = Allocate(sizeof(T), Policy::kPsram, Module::kGeneral);
  return Object<T>(storage != nullptr
      ? new (storage) T(std::forward<Args>(args)...) : nullptr);
}

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
