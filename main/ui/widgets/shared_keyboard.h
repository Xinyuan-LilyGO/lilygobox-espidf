/*
 * @Description: 可复用 LVGL 屏幕键盘创建与绑定接口
 * @Author: LILYGO_L
 * @Date: 2026-05-18 12:08:00
 * @LastEditTime: 2026-05-18 12:08:00
 * @License: GPL 3.0
 */
#pragma once

#include "lvgl.h"

namespace lilygo_box::ui {

// 共享键盘配置
struct SharedKeyboardConfig {
  int width = 0;
  int height = 0;
};

/**
 * @brief 创建共享屏幕键盘
 * @param parent 父对象
 * @param config 键盘配置
 * @return 创建成功返回键盘对象，否则返回 nullptr
 */
lv_obj_t* CreateSharedKeyboard(
    lv_obj_t* parent, const SharedKeyboardConfig& config);

/**
 * @brief 绑定共享键盘和文本输入框
 * @param keyboard 共享键盘对象
 * @param text_area 文本输入框对象
 * @param accepted_chars 允许输入的字符集合
 * @return 绑定成功返回 true，否则返回 false
 */
bool AttachSharedKeyboardToTextArea(
    lv_obj_t* keyboard, lv_obj_t* text_area, const char* accepted_chars);

/**
 * @brief 隐藏共享键盘
 * @param keyboard 共享键盘对象
 */
void HideSharedKeyboard(lv_obj_t* keyboard);

}  // namespace lilygo_box::ui
