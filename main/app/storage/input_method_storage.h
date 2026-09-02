/*
 * @Description: Input method preference storage
 * @Author: LILYGO_L
 * @Date: 2026-08-20 00:00:00
 * @LastEditTime: 2026-09-02 17:51:29
 * @License: GPL 3.0
 */
#pragma once

namespace lilygo_box::app {

struct InputMethodPreferences {
  // 连接实体键盘时是否仍显示屏幕键盘。
  bool use_on_screen_keyboard = true;
};

/**
 * @brief 初始化输入法偏好缓存，从 NVS 加载到内存
 */
void InitInputMethodCache();

/**
 * @brief 读取输入法偏好
 * @return 当前输入法偏好
 */
InputMethodPreferences GetInputMethodPreferences();

/**
 * @brief 比较并更新输入法偏好，存在变化时立即写入 NVS
 * @param preferences 新的输入法偏好
 * @return 无变化或 NVS 提交成功返回 true，否则返回 false
 */
bool UpdateInputMethodPreferences(const InputMethodPreferences& preferences);

}  // namespace lilygo_box::app
