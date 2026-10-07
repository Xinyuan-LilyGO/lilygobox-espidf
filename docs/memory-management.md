# 内存分配管理

公共接口位于 [main/base/memory.h](../main/base/memory.h)，实现在 [memory.cpp](../main/base/memory.cpp)。它负责显式选择内存区域，并记录已接入模块的基础用量；不替换系统 `malloc/new`，也不修改第三方库的分配器。

## 分配策略

| 策略 | 用途 | 回退行为 |
| --- | --- | --- |
| `kBalanced` | LVGL 等大小混合的普通对象 | 小分配先高速内部 RAM，大分配先 PSRAM；允许在这两者之间回退 |
| `kFastInternal` | 明确需要快速访问的内部缓冲 | 仅普通高速内部堆，失败返回空指针 |
| `kPsram` | 图片、较大的业务缓存 | 仅 PSRAM，失败后由业务决定降级 |
| `kInternalDma` | 明确要求内部 DMA 的缓冲 | 仅内部 DMA 堆，允许使用系统 DMA 保留池 |

`kBalanced` 沿用 `CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL` 的大小分界。普通高速内部堆要求 `DEFAULT`、`INTERNAL`、`DMA` 和 `8BIT` 能力，在 ESP32-P4 上排除 RTC RAM/TCM，同时避开系统专用保留池。不要以“不够时分配到任意内部内存”替代这个限制，具体背景见 [WLAN 页面性能](wlan-performance.md)。

`kInternalDma` 只表示内存区域要求，不承诺满足所有外设的缓存行、对齐、连续长度或缓存同步要求。需要特殊对齐的显示、相机、USB 等驱动缓冲继续使用各自驱动接口，不应直接迁入普通分配接口。

## 使用方式

```cpp
#include "base/memory.h"

using namespace lilygo_box;

void* buffer = memory::Allocate(
    4096, memory::Policy::kPsram, memory::Module::kGeneral);
if (buffer != nullptr) {
  // 使用缓冲区。
  memory::Free(buffer, memory::Module::kGeneral);
}
```

- `AllocateZeroed` 校验元素数量乘法是否溢出，并清零申请范围。
- `Reallocate` 失败时返回空指针，原指针、原内容及存活用量保持有效；调用方先保存返回值，成功后再替换原指针。
- 零大小分配返回空指针，不算失败；对非空指针扩容到零等同释放。
- 所有接口仅在任务上下文调用，不支持 ISR；同一指针的并发访问由调用方协调。
- 申请、扩容和释放必须全部经过公共接口，并传入相同的 `Module`。不能用它释放其他分配器的指针，也不能绕过它直接 `free` 已计数的指针。

模块标签由调用方维护，不为每个分配增加块头或地址追踪表。因此它不会自动识别错误的模块标签、越界、重复释放或所有权错误。

## 当前接入范围

| 模块 | 接入内容 |
| --- | --- |
| LVGL | 链接包装 `lv_malloc_core`、`lv_realloc_core`、`lv_free_core` |
| Camera | 相机页面持有的预览图像缓冲；不包含硬件驱动的相机缓冲 |
| Radio | 聊天记录热缓存和会话状态数组 |

LVGL 保留此前验证的内存优先级和回退顺序。相机页面继续仅使用 PSRAM。Radio 大缓存继续优先使用 PSRAM，失败后按原逻辑降低容量；内部缓存改为明确使用高速内部堆，避免落入 RTC RAM。

SDK、标准容器、驱动、TLS 及尚未接入的分配不包含在模块计数中，但会反映在系统堆快照中。模块用量之和不是整个程序的内存占用。

## 按需读取统计

```cpp
const auto snapshot = lilygo_box::memory::ReadSnapshot();
const auto& lvgl = snapshot.modules[
    static_cast<size_t>(lilygo_box::memory::Module::kLvgl)];
// lvgl.current_bytes、peak_bytes、live_blocks、failures

// 需要日志诊断时主动调用，不要放进高频绘制定时器。
lilygo_box::memory::LogSnapshot();
```

模块计数包括当前实际分配块字节数、历史峰值、存活块数和最终分配失败次数。实际块大小可能包含对齐产生的额外空间，不等于请求大小；统计不包括堆管理元数据，也不统计 `realloc` 内部搬迁的瞬时双份占用。计数从本次启动开始累积。

系统快照提供默认堆、全部内部堆、普通高速内部堆和 PSRAM 的总量、空闲量、历史最低空闲量和最大连续空闲块。总量减空闲量可用于观察该区域的堆占用；它不等同于固件全部静态及动态 RAM 的总和。各区域范围存在重叠，不能相加；最大连续空闲块有助于判断是否能容纳下一次大分配。多堆快照不是全系统原子采样，区域最低空闲量也不一定出现在同一时刻。

统计使用固定数组和短临界区，不创建后台任务或周期日志，也不添加界面。分配失败按模块在第 1、2、4、8 等次数记录项目日志，避免连续失败刷屏；首选区域失败但回退成功不算最终失败。

