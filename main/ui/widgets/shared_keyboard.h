/*
 * @Description: 可复用 LVGL 屏幕键盘创建与绑定接口
 * @Author: LILYGO_L
 * @Date: 2026-05-18 12:08:00
 * @LastEditTime: 2026-09-02 17:54:56
 * @License: GPL 3.0
 */
#pragma once

#include "lvgl.h"

namespace lilygo_box::hal {
class KeyboardExpansionProvider;
}  // namespace lilygo_box::hal

namespace lilygo_box::ui {

// 共享键盘配置
struct SharedKeyboardConfig {
  int width = 0;
  int height = 0;
  lv_keyboard_mode_t initial_mode = LV_KEYBOARD_MODE_USER_1;
};

/**
 * @brief 注册用于判断实体键盘连接状态的接口
 * @param provider 键盘扩展接口，不支持实体键盘时可为 nullptr
 */
void RegisterSharedKeyboardPhysicalKeyboardProvider(
    hal::KeyboardExpansionProvider* provider);

/**
 * @brief 按当前输入法偏好和实体键盘连接状态刷新全部屏幕键盘
 */
void RefreshSharedKeyboardVisibility();

/**
 * @brief 取消全部共享键盘当前绑定输入框的焦点并隐藏屏幕键盘
 */
void DefocusSharedKeyboardTextAreas();

/**
 * @brief 判断当前输入法策略是否需要显示屏幕键盘
 * @return 需要显示返回 true，否则返回 false
 */
bool ShouldShowSharedKeyboard();

/**
 * @brief 创建共享屏幕键盘
 * @param parent 父对象
 * @param config 键盘配置
 * @return 创建成功返回键盘对象，否则返回 nullptr
 */
lv_obj_t* CreateSharedKeyboard(
    lv_obj_t* parent, const SharedKeyboardConfig& config);

/**
 * @brief 绑定共享键盘并让文本框仅在有效点击释放后激活
 * @param keyboard 共享键盘对象
 * @param text_area 文本输入框对象
 * @param accepted_chars 允许输入的字符集合，也用于识别数值型输入
 * @param initial_mode 无法识别为数值型输入时使用的键盘布局
 * @return 绑定成功返回 true，否则返回 false
 */
bool AttachSharedKeyboardToTextArea(lv_obj_t* keyboard, lv_obj_t* text_area,
    const char* accepted_chars,
    lv_keyboard_mode_t initial_mode = LV_KEYBOARD_MODE_USER_1);

/**
 * @brief 隐藏共享键盘
 * @param keyboard 共享键盘对象
 */
void HideSharedKeyboard(lv_obj_t* keyboard);

}  // namespace lilygo_box::ui
