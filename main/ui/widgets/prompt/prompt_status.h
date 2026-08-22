/*
 * @Description: Shared in-page status prompt widget
 * @Author: LILYGO_L
 * @Date: 2026-08-22 00:00:00
 * @LastEditTime: 2026-08-22 00:00:00
 * @License: GPL 3.0
 */
#pragma once

#include <cstdint>

#include "lvgl.h"
#include "ui/theme/theme_provider.h"

namespace lilygo_box::ui {

enum class PromptStatusVisual {
  kIcon,
  kSpinner,
};

// 页面内状态提示的内容、样式和布局配置。
struct PromptStatusConfig {
  int width = 0;
  int height = 0;
  PromptStatusVisual visual = PromptStatusVisual::kIcon;
  int visual_top = 0;

  const char* icon = nullptr;
  const lv_font_t* icon_font = nullptr;
  int icon_background_size = 96;
  uint32_t icon_background_color =
      theme::LightNeutralTheme().action_container;
  uint32_t icon_color = theme::LightNeutralTheme().action;

  int spinner_size = 68;
  int spinner_arc_width = 7;
  uint32_t spinner_period_ms = 850;
  uint32_t spinner_arc_length = 250;
  uint32_t spinner_track_color =
      theme::LightNeutralTheme().surface_container_high;
  uint32_t spinner_indicator_color = theme::LightNeutralTheme().action;

  const char* title = nullptr;
  const lv_font_t* title_font = nullptr;
  uint32_t title_color = theme::LightNeutralTheme().on_surface;
  int title_top = 112;

  const char* message = nullptr;
  const lv_font_t* message_font = nullptr;
  uint32_t message_color = theme::LightNeutralTheme().on_surface_variant;
  int message_top = 150;
  int horizontal_padding = 40;

  const char* button_text = nullptr;
  const lv_font_t* button_font = nullptr;
  int button_top = 212;
  int button_width = 196;
  int button_height = 64;
  int button_radius = 0;
  uint32_t button_background_color = theme::LightNeutralTheme().action;
  uint32_t button_pressed_color =
      theme::LightNeutralTheme().action_pressed;
  uint32_t button_text_color = theme::LightNeutralTheme().on_action;
  lv_event_cb_t button_callback = nullptr;
  void* button_user_data = nullptr;

  bool bubble_gestures = true;
};

/**
 * @brief 创建页面内状态提示
 * @param parent 父对象
 * @param config 状态提示配置
 * @return 创建成功返回状态提示容器，否则返回 nullptr
 */
lv_obj_t* CreatePromptStatus(
    lv_obj_t* parent, const PromptStatusConfig& config);

}  // namespace lilygo_box::ui
