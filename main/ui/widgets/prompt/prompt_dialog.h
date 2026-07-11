/*
 * @Description: 公共居中提示框控件
 * @Author: LILYGO_L
 * @Date: 2026-07-11 00:00:00
 * @LastEditTime: 2026-07-11 00:00:00
 * @License: GPL 3.0
 */
#pragma once

#include <cstdint>

#include "lvgl.h"
#include "ui/input/edge_back_gesture.h"
#include "ui/theme/theme_provider.h"

namespace lilygo_box::ui {

using PromptDialogActionCallback = void (*)(void* context);

// 居中提示框运行期间的对象、动画和回调状态。
struct PromptDialogState {
  lv_obj_t* overlay = nullptr;
  lv_obj_t* panel = nullptr;
  lv_obj_t* body = nullptr;
  EdgeBackSwipeState edge_swipe = {};
  PromptDialogActionCallback cancel_callback = nullptr;
  PromptDialogActionCallback confirm_callback = nullptr;
  void* callback_context = nullptr;
  uint32_t animation_ms = 180;
  bool closing = false;
};

// 居中提示框尺寸、颜色、文本和字体配置。
struct PromptDialogConfig {
  int screen_width = 0;
  int screen_height = 0;
  int dialog_width = 0;
  int dialog_height = 0;
  int dialog_radius = 30;
  int inner_padding = 28;
  int header_height = 92;
  int title_y = 24;
  int action_height = 106;
  int action_button_height = 74;
  int action_button_gap = 20;
  int action_bottom_padding = 32;
  uint32_t dialog_color =
      theme::LightNeutralTheme().surface_container_lowest;
  uint32_t primary_text_color = theme::LightNeutralTheme().on_surface;
  uint32_t secondary_text_color =
      theme::LightNeutralTheme().on_surface_variant;
  uint32_t divider_color = theme::LightNeutralTheme().outline_variant;
  uint32_t pressed_color = theme::LightNeutralTheme().state_layer;
  uint32_t cancel_background_color =
      theme::LightNeutralTheme().button_secondary;
  uint32_t cancel_pressed_color =
      theme::LightNeutralTheme().button_secondary_pressed;
  uint32_t cancel_text_color =
      theme::LightNeutralTheme().on_button_secondary;
  uint32_t confirm_background_color = theme::LightNeutralTheme().action;
  uint32_t confirm_pressed_color =
      theme::LightNeutralTheme().action_pressed;
  uint32_t confirm_text_color = theme::LightNeutralTheme().on_action;
  lv_opa_t overlay_opacity = 115;
  uint32_t animation_ms = 180;
  const char* title = nullptr;
  const char* cancel_text = "Cancel";
  const char* confirm_text = "Save";
  const lv_font_t* title_font = nullptr;
  const lv_font_t* action_font = nullptr;
  PromptDialogActionCallback cancel_callback = nullptr;
  PromptDialogActionCallback confirm_callback = nullptr;
  void* callback_context = nullptr;
};

/**
 * @brief 创建居中提示框并播放淡入动画
 * @param parent 父对象
 * @param state 提示框状态
 * @param config 提示框配置
 * @return 创建成功返回可滚动内容区域，否则返回 nullptr
 */
lv_obj_t* ShowPromptDialog(lv_obj_t* parent, PromptDialogState* state,
    const PromptDialogConfig& config);

/**
 * @brief 播放淡出动画并关闭提示框
 * @param state 提示框状态
 */
void ClosePromptDialog(PromptDialogState* state);

/**
 * @brief 判断提示框当前是否可见
 * @param state 提示框状态
 * @return 可见返回 true，否则返回 false
 */
bool IsPromptDialogVisible(const PromptDialogState* state);

}  // namespace lilygo_box::ui
