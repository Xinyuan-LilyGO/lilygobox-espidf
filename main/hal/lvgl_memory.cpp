/*
 * @Description: ESP32-P4 的 LVGL 内存分配适配，避免界面对象使用 RTC RAM。
 * @Author: LILYGO_L
 * @Date: 2026-10-07 00:00:00
 * @LastEditTime: 2026-10-07 00:00:00
 * @License: GPL 3.0
 */
#include <cstddef>

#include "base/memory.h"
#include "sdkconfig.h"

#if CONFIG_IDF_TARGET_ESP32P4 && CONFIG_LV_USE_CLIB_MALLOC
// WLAN 卡顿修复的区域约束集中在 base/memory.cpp；Balanced 保留原有大小阈值、
// 高速内部 RAM/PSRAM 的优先级，禁止回退到 RTC RAM。
// 必须同时包装分配、扩容和释放，才能维护准确的 LVGL 模块存活用量。
extern "C" {

/**
 * @brief 使用公共分配策略申请 LVGL 内存并记录模块用量
 * @param size 申请字节数
 * @return 分配结果，失败返回 nullptr
 */
void* __wrap_lv_malloc_core(size_t size) {
  return lilygo_box::memory::Allocate(size,
                                      lilygo_box::memory::Policy::kBalanced,
                                      lilygo_box::memory::Module::kLvgl);
}

/**
 * @brief 调整 LVGL 内存大小，失败时保留原内容和统计
 * @param pointer 原地址，可为空
 * @param size 新字节数，零表示释放
 * @return 调整后的地址，失败或释放时返回 nullptr
 */
void* __wrap_lv_realloc_core(void* pointer, size_t size) {
  return lilygo_box::memory::Reallocate(pointer, size,
                                        lilygo_box::memory::Policy::kBalanced,
                                        lilygo_box::memory::Module::kLvgl);
}

/**
 * @brief 释放 LVGL 内存并同步模块占用统计
 * @param pointer 待释放地址，可为空
 */
void __wrap_lv_free_core(void* pointer) {
  lilygo_box::memory::Free(pointer, lilygo_box::memory::Module::kLvgl);
}

}  // extern "C"
#endif
