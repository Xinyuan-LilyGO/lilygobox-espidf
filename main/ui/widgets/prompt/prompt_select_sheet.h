/*
 * @Description: Prompt select sheet widget
 * @Author: LILYGO_L
 * @Date: 2026-06-25 00:00:00
 * @LastEditTime: 2026-06-25 00:00:00
 * @License: GPL 3.0
 */
#pragma once

#include <cstddef>
#include <cstdint>

#include "lvgl.h"
#include "ui/theme/theme_provider.h"

namespace lilygo_box::ui {

constexpr size_t kPromptSelectSheetMaxOptions = 10;

struct PromptSelectSheetState;

using PromptSelectSheetSelectedCallback = void (*)(void* context, int value);

struct PromptSelectSheetOption {
  int value = 0;
  const char* text = nullptr;
};

struct PromptSelectSheetOptionAction {
  PromptSelectSheetState* sheet_state = nullptr;
  PromptSelectSheetSelectedCallback callback = nullptr;
  void* context = nullptr;
  int value = 0;
};

struct PromptSelectSheetState {
  lv_obj_t* overlay = nullptr;
  lv_obj_t* sheet = nullptr;
  PromptSelectSheetOptionAction actions[kPromptSelectSheetMaxOptions] = {};
};

struct PromptSelectSheetConfig {
  int screen_width = 0;
  int screen_height = 0;
  int sheet_width = 0;
  int sheet_height = 0;
  int side_margin = 0;
  int bottom_margin = 0;
  int sheet_radius = 0;
  int inner_padding = 0;
  int option_top = 0;
  int option_height = 0;
  int button_height = 0;
  int button_radius = 0;
  int title_message_gap = 8;
  uint32_t sheet_color = theme::LightNeutralTheme().surface_container_lowest;
  uint32_t selected_color = theme::LightNeutralTheme().action_container;
  uint32_t primary_text_color = theme::LightNeutralTheme().on_surface;
  uint32_t secondary_text_color =
      theme::LightNeutralTheme().on_surface_variant;
  uint32_t selected_text_color = theme::LightNeutralTheme().action;
  uint32_t cancel_background_color =
      theme::LightNeutralTheme().button_secondary;
  uint32_t cancel_pressed_color =
      theme::LightNeutralTheme().surface_container_high;
  uint32_t pressed_color = theme::LightNeutralTheme().surface_container_low;
  lv_opa_t pressed_opacity = 190;
  lv_opa_t overlay_opacity = 115;
  uint32_t animation_ms = 180;
  const char* title = nullptr;
  const char* message = nullptr;
  const char* cancel_text = "Cancel";
  const char* check_icon = nullptr;
  const PromptSelectSheetOption* options = nullptr;
  size_t option_count = 0;
  int selected_value = 0;
  const lv_font_t* title_font = nullptr;
  const lv_font_t* message_font = nullptr;
  const lv_font_t* option_font = nullptr;
  const lv_font_t* cancel_font = nullptr;
  const lv_font_t* icon_font = nullptr;
  PromptSelectSheetState* state = nullptr;
  PromptSelectSheetSelectedCallback callback = nullptr;
  void* callback_context = nullptr;
};

/**
 * @brief 关闭选择型底部提示栏
 * @param state 选择提示栏状态
 */
void ClosePromptSelectSheet(PromptSelectSheetState* state);

/**
 * @brief 创建选择型底部提示栏
 * @param parent 父对象
 * @param config 选择提示栏配置
 * @return 创建成功返回 true，否则返回 false
 */
bool ShowPromptSelectSheet(
    lv_obj_t* parent, const PromptSelectSheetConfig& config);

}  // namespace lilygo_box::ui
