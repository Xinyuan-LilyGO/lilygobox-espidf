/*
 * @Description: ESP32-P4 的 LVGL 内存分配适配，避免界面对象使用 RTC RAM。
 * @Author: LILYGO_L
 * @Date: 2026-10-07 00:00:00
 * @LastEditTime: 2026-10-07 00:00:00
 * @License: GPL 3.0
 */
#include <cstddef>
#include <cstdint>

#include "esp_heap_caps.h"
#include "sdkconfig.h"

#if CONFIG_IDF_TARGET_ESP32P4 && CONFIG_LV_USE_CLIB_MALLOC
namespace {

// WLAN 卡顿的主要修复：WiFi 启动后普通内部堆更紧张，LVGL 的 malloc
// 可能转入 RTC RAM，使对象访问和文字绘制明显变慢。小对象优先使用高速
// 内部 RAM，不足时使用 PSRAM；大缓冲按原阈值优先使用 PSRAM。
// P4 上 DMA 能力用于排除 RTC RAM/TCM；同时要求 DEFAULT，避免使用
// 系统为 SDIO 等保留的内部/DMA 内存池。不要改回不区分内存区域的 malloc。
constexpr uint32_t kFastHeapCaps =
    MALLOC_CAP_DEFAULT | MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA | MALLOC_CAP_8BIT;
constexpr uint32_t kExternalHeapCaps =
    MALLOC_CAP_DEFAULT | MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;

}  // namespace

// main/CMakeLists.txt 将 LVGL 的两个核心分配入口链接到此处，无需修改托管库。
// 申请和扩容使用同一策略；原 lv_free_core 最终调用系统 free，可释放这两种堆。
extern "C" {

/**
 * @brief 按原大小分流阈值为 LVGL 分配高速内部 RAM 或 PSRAM，排除 RTC RAM
 * @param size 申请的字节数
 * @return 分配结果，失败返回 nullptr
 */
void* __wrap_lv_malloc_core(size_t size) {
  const bool prefer_internal = size <= CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL;
  return heap_caps_malloc_prefer(size, 2,
      prefer_internal ? kFastHeapCaps : kExternalHeapCaps,
      prefer_internal ? kExternalHeapCaps : kFastHeapCaps);
}

/**
 * @brief 按相同内存策略扩缩 LVGL 对象，分配失败时保留原内容和原地址
 * @param pointer 原分配地址，可为空
 * @param size 新字节数，零表示释放
 * @return 调整后的地址，失败或释放时返回 nullptr
 */
void* __wrap_lv_realloc_core(void* pointer, size_t size) {
  const bool prefer_internal = size <= CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL;
  return heap_caps_realloc_prefer(pointer, size, 2,
      prefer_internal ? kFastHeapCaps : kExternalHeapCaps,
      prefer_internal ? kExternalHeapCaps : kFastHeapCaps);
}

}  // extern "C"
#endif
