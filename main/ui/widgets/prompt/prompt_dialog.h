/*
 * @Description: 公共提示框控件
 * @Author: LILYGO_L
 * @Date: 2026-07-11 00:00:00
 * @LastEditTime: 2026-09-02 17:57:09
 * @License: GPL 3.0
 */
#pragma once

#include <cstdint>

#include "lvgl.h"
#include "ui/theme/theme_provider.h"

namespace lilygo_box::ui {

using PromptDialogActionCallback = void (*)(void* context);

// 提示框运行期间的对象、动画和回调状态。
struct PromptDialogState {
  lv_obj_t* overlay = nullptr;
  lv_obj_t* panel = nullptr;
  lv_obj_t* title_label = nullptr;
  lv_obj_t* subtitle_label = nullptr;
  lv_obj_t* body = nullptr;
  lv_obj_t* cancel_button = nullptr;
  lv_obj_t* cancel_button_label = nullptr;
  lv_obj_t* confirm_button = nullptr;
  lv_obj_t* confirm_button_label = nullptr;
  PromptDialogActionCallback cancel_callback = nullptr;
  PromptDialogActionCallback confirm_callback = nullptr;
  void* callback_context = nullptr;
  uint32_t animation_ms = 180;
  bool slide_from_bottom = false;
  bool closing = false;
};

// 提示框尺寸、颜色、文本和字体配置。
struct PromptDialogConfig {
  int screen_width = 0;
  int screen_height = 0;
  int dialog_width = 0;
  int dialog_height = 0;
  int dialog_radius = 30;
  int inner_padding = 28;
  int header_height = 92;
  int title_y = 24;
  int title_subtitle_gap = 8;
  int subtitle_body_gap = 16;
  int action_height = 106;
  int action_button_height = 74;
  // 设为 0 时按钮使用高度一半的圆角。
  int action_button_radius = 0;
  int action_button_gap = 20;
  int action_bottom_padding = 32;
  int bottom_margin = 0;
  uint32_t dialog_color = theme::ActiveThemeColors().surface_container_lowest;
  uint32_t primary_text_color = theme::ActiveThemeColors().on_surface;
  uint32_t secondary_text_color = theme::ActiveThemeColors().on_surface_variant;
  uint32_t divider_color = theme::ActiveThemeColors().outline_variant;
  uint32_t pressed_color = theme::ActiveThemeColors().state_layer;
  uint32_t cancel_background_color =
      theme::ActiveThemeColors().button_secondary;
  uint32_t cancel_pressed_color =
      theme::ActiveThemeColors().button_secondary_pressed;
  uint32_t cancel_text_color = theme::ActiveThemeColors().on_button_secondary;
  uint32_t confirm_background_color = theme::FixedColors().action;
  uint32_t confirm_pressed_color = theme::FixedColors().action_pressed;
  uint32_t confirm_text_color = theme::FixedColors().on_action;
  lv_opa_t overlay_opacity = 115;
  uint32_t animation_ms = 180;
  const char* title = nullptr;
  const char* subtitle = nullptr;
  // 设为 nullptr 可隐藏对应按钮；仅保留一个按钮时会自动占满操作区域。
  const char* cancel_text = "Cancel";
  const char* confirm_text = "Save";
  const lv_font_t* title_font = nullptr;
  const lv_font_t* subtitle_font = nullptr;
  const lv_font_t* action_font = nullptr;
  lv_text_align_t title_text_align = LV_TEXT_ALIGN_LEFT;
  lv_text_align_t subtitle_text_align = LV_TEXT_ALIGN_LEFT;
  PromptDialogActionCallback cancel_callback = nullptr;
  PromptDialogActionCallback confirm_callback = nullptr;
  void* callback_context = nullptr;
  bool slide_from_bottom = false;
};

/**
 * @brief 创建提示框并播放进入动画
 * @param parent 父对象
 * @param state 提示框状态
 * @param config 提示框配置
 * @return 创建成功返回可滚动内容区域，否则返回 nullptr
 */
lv_obj_t* ShowPromptDialog(lv_obj_t* parent, PromptDialogState* state,
    const PromptDialogConfig& config);

/**
 * @brief 原位更新可见提示框的文字、布局、按钮和回调
 * @param state 提示框状态
 * @param config 更新后的提示框配置
 * @return 更新成功返回正文区域，否则返回 nullptr
 */
lv_obj_t* UpdatePromptDialog(
    PromptDialogState* state, const PromptDialogConfig& config);

/**
 * @brief 播放退出动画并关闭提示框
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
