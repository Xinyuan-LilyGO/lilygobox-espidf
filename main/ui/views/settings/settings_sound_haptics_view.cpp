/*
 * @Description: Settings sound and haptics page
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
 * @brief 处理系统触感开关状态变化
 * @param event LVGL 事件对象
 */
void HapticsSwitchChangedEventCallback(lv_event_t* event) {
  auto* state = static_cast<SettingsViewState*>(lv_event_get_user_data(event));
  lv_obj_t* target = lv_event_get_target_obj(event);
  if (state != nullptr && target != nullptr) {
    state->haptics_enabled = lv_obj_has_state(target, LV_STATE_CHECKED);
  }
}

/**
 * @brief 保存音量滑动条值
 * @param event LVGL 事件对象
 */
void VolumeSliderChangedEventCallback(lv_event_t* event) {
  auto* state = static_cast<SettingsViewState*>(lv_event_get_user_data(event));
  if (state != nullptr) {
    state->audio_volume_percent = SliderPercentFromEvent(event);
  }
}

/**
 * @brief 保存触感强度滑动条值
 * @param event LVGL 事件对象
 */
void HapticSliderChangedEventCallback(lv_event_t* event) {
  auto* state = static_cast<SettingsViewState*>(lv_event_get_user_data(event));
  if (state != nullptr) {
    state->haptic_strength_percent = SliderPercentFromEvent(event);
  }
}

/**
 * @brief 构建声音与触感设置内容
 * @param body 内容容器
 * @param state 设置页状态
 * @return 创建成功返回 true，否则返回 false
 */
bool BuildSoundHapticsContent(lv_obj_t* body, SettingsViewState* state) {
  int y = 0;
  if (!CreateSectionLabel(body, "Volume adjustment", y,
          state->config.width)) {
    return false;
  }
  y += kBasicSectionHeight;
  if (!CreateSliderRow(body, icon::kVolumeUp, "Volume",
          state->audio_volume_percent, y, state->config.width,
          VolumeSliderChangedEventCallback, state)) {
    return false;
  }
  y += 150;
  if (!CreateSwitchRow(body, "System haptics", y, state->config.width,
          state->haptics_enabled, HapticsSwitchChangedEventCallback, state)) {
    return false;
  }
  y += kBasicRowHeight + 16;
  if (!CreateSectionLabel(body, "Haptic adjustment", y,
          state->config.width)) {
    return false;
  }
  y += kBasicSectionHeight;
  return CreateSliderRow(body, icon::kSettings, "Haptics",
      state->haptic_strength_percent, y, state->config.width,
      HapticSliderChangedEventCallback, state);
}

}  // namespace

/**
 * @brief 从设置主页打开声音与触感详情页
 * @param state 设置页状态
 * @return 打开成功返回 true，否则返回 false
 */
bool ShowSoundHapticsPage(SettingsViewState* state) {
  return ShowBasicPage(state, "Sound & Haptics", BuildSoundHapticsContent);
}

}  // namespace lilygo_box::ui
