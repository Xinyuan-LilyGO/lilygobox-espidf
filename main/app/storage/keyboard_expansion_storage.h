/*
 * @Description: Keyboard expansion preference storage
 * @Author: LILYGO_L
 * @Date: 2026-08-20 00:00:00
 * @LastEditTime: 2026-08-20 00:00:00
 * @License: GPL 3.0
 */
#pragma once

#include <cstdint>

namespace lilygo_box::app {

struct KeyboardExpansionPreferences {
  // 是否在系统启动后自动检测并启用键盘扩展。
  bool enabled = false;
  // 键盘扩展背光亮度百分比，默认 10%。
  int32_t backlight_brightness_percent = 10;
};

/**
 * @brief 初始化键盘扩展偏好缓存，从 NVS 加载到内存
 */
void InitKeyboardExpansionCache();

/**
 * @brief 读取键盘扩展偏好
 * @return 当前键盘扩展偏好
 */
KeyboardExpansionPreferences GetKeyboardExpansionPreferences();

/**
 * @brief 比较并更新键盘扩展偏好，存在变化时立即写入 NVS
 * @param preferences 新的键盘扩展偏好
 * @return 无变化或 NVS 提交成功返回 true，否则返回 false
 */
bool UpdateKeyboardExpansionPreferences(
    const KeyboardExpansionPreferences& preferences);

}  // namespace lilygo_box::app
