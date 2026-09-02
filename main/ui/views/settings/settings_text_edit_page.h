/*
 * @Description: Reusable settings single-line text edit page
 * @Author: LILYGO_L
 * @Date: 2026-09-01 00:00:00
 * @LastEditTime: 2026-09-02 17:56:51
 * @License: GPL 3.0
 */
#pragma once

#include <cstddef>

#include "lvgl.h"

namespace lilygo_box::ui {

using SettingsTextEditSaveCallback = bool (*)(const char* text, void* context);
using SettingsTextEditValidationCallback = bool (*)(
    const char* text, void* context);

// 单行设置编辑页的运行状态，同一设置页面可以在不同入口间复用。
struct SettingsTextEditPageState {
  lv_obj_t* page = nullptr;
  lv_obj_t* text_area = nullptr;
  lv_obj_t* keyboard = nullptr;
  lv_obj_t* confirm_button = nullptr;
  lv_obj_t* confirm_icon = nullptr;
  SettingsTextEditSaveCallback save_callback = nullptr;
  SettingsTextEditValidationCallback validation_callback = nullptr;
  void* callback_context = nullptr;
  int width = 0;
  bool closing = false;
};

// 单行设置编辑页的显示和输入配置。
struct SettingsTextEditPageConfig {
  lv_obj_t* parent = nullptr;
  int width = 0;
  int height = 0;
  const char* title = nullptr;
  const char* initial_text = nullptr;
  const char* help_text = nullptr;
  const char* accepted_chars = nullptr;
  size_t maximum_length = 0;
  lv_keyboard_mode_t keyboard_mode = LV_KEYBOARD_MODE_USER_1;
  SettingsTextEditSaveCallback save_callback = nullptr;
  SettingsTextEditValidationCallback validation_callback = nullptr;
  void* callback_context = nullptr;
};

/**
 * @brief 显示统一的设置单行编辑页
 * @param state 编辑页运行状态
 * @param config 编辑页显示、输入和保存配置
 * @return 页面已显示或创建成功返回 true，否则返回 false
 */
bool ShowSettingsTextEditPage(
    SettingsTextEditPageState* state, const SettingsTextEditPageConfig& config);

/**
 * @brief 关闭统一的设置单行编辑页
 * @param state 编辑页运行状态
 * @param animated 是否播放关闭动画
 */
void CloseSettingsTextEditPage(SettingsTextEditPageState* state, bool animated);

}  // namespace lilygo_box::ui
