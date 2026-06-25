/*
 * @Description: Settings display brightness page
 * @Author: LILYGO_L
 * @Date: 2026-05-23 00:00:00
 * @LastEditTime: 2026-05-23 00:00:00
 * @License: GPL 3.0
 */
#include "ui/views/settings/settings_basic_view_common.h"

#include "ui/font/material_symbols_assets.h"

namespace lilygo_box::ui {
namespace {

/**
 * @brief 保存屏幕亮度滑动条值
 * @param event LVGL 事件对象
 */
void BrightnessSliderChangedEventCallback(lv_event_t* event) {
  auto* state = static_cast<SettingsViewState*>(lv_event_get_user_data(event));
  if (state != nullptr) {
    state->display_brightness_percent = SliderPercentFromEvent(event);
  }
}

/**
 * @brief 构建显示与亮度设置内容
 * @param body 内容容器
 * @param state 设置页状态
 * @return 创建成功返回 true，否则返回 false
 */
bool BuildDisplayBrightnessContent(lv_obj_t* body, SettingsViewState* state) {
  if (!CreateSectionLabel(body, "Brightness", 0, state->config.width)) {
    return false;
  }
  return CreateSliderRow(body, icon::kSunny, "Screen brightness",
      state->display_brightness_percent, kBasicSectionHeight,
      state->config.width, BrightnessSliderChangedEventCallback, state);
}

}  // namespace

/**
 * @brief 从设置主页打开显示与亮度详情页
 * @param state 设置页状态
 * @return 打开成功返回 true，否则返回 false
 */
bool ShowDisplayBrightnessPage(SettingsViewState* state) {
  return ShowBasicPage(state, "Display & Brightness",
      BuildDisplayBrightnessContent);
}

}  // namespace lilygo_box::ui
