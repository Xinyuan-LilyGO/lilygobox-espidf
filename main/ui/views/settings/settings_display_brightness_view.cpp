/*
 * @Description: Settings display brightness page
 * @Author: LILYGO_L
 * @Date: 2026-05-23 00:00:00
 * @LastEditTime: 2026-06-30 09:20:47
 * @License: GPL 3.0
 */
#include "ui/views/settings/settings_basic_view_common.h"

#include <cstdint>

#include "app/storage/display_storage.h"
#include "app/storage/storage_task.h"
#include "hal/providers/haptic_provider.h"
#include "hal/providers/screen_provider.h"
#include "ui/font/material_symbols_assets.h"

namespace lilygo_box::ui {
namespace {

void PlaySettingsHapticPreview(SettingsViewState* state) {
  if (state == nullptr || !state->haptics_enabled ||
      state->config.haptic == nullptr) {
    return;
  }
  const uint8_t gain = static_cast<uint8_t>(
      state->haptic_strength_percent * UINT8_MAX / 100);
  state->config.haptic->PlayHapticWaveform(1, 1, gain, true);
}

/**
 * @brief 异步保存显示亮度设置偏好
 * @param state 设置页状态
 */
void SaveBrightnessPreferencesAsync(SettingsViewState* state) {
  if (state == nullptr) {
    return;
  }

  app::DisplayPreferences preferences;
  app::LoadDisplayPreferencesFromNvs(&preferences);
  preferences.brightness_percent = state->display_brightness_percent;
  app::StartStorageTask("display_save", [preferences]() {
    app::SaveDisplayPreferencesToNvs(preferences);
  });
}

/**
 * @brief 保存屏幕亮度滑动条值并应用到硬件
 * @param event LVGL 事件对象
 */
void BrightnessSliderChangedEventCallback(lv_event_t* event) {
  auto* state = static_cast<SettingsViewState*>(lv_event_get_user_data(event));
  if (state != nullptr) {
    const int brightness_percent = SliderPercentFromEvent(event);
    state->display_brightness_percent = brightness_percent;
    PlaySettingsHapticPreview(state);
    if (state->config.screen != nullptr) {
      state->config.screen->SetScreenBrightnessPercent(brightness_percent);
    }
  }
}

/**
 * @brief 松开亮度滑动条时保存最终值
 * @param event LVGL 事件对象
 */
void BrightnessSliderReleasedEventCallback(lv_event_t* event) {
  SaveBrightnessPreferencesAsync(
      static_cast<SettingsViewState*>(lv_event_get_user_data(event)));
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
  if (!CreateSliderRow(body, icon::kSunny, "Screen brightness",
          state->display_brightness_percent, kBasicSectionHeight,
          state->config.width, BrightnessSliderChangedEventCallback, state)) {
    return false;
  }

  lv_obj_t* brightness_slider =
      lv_obj_get_child(body, lv_obj_get_child_count(body) - 1);
  if (brightness_slider != nullptr) {
    lv_obj_add_event_cb(brightness_slider, BrightnessSliderReleasedEventCallback,
        LV_EVENT_RELEASED, state);
    lv_obj_add_event_cb(brightness_slider, BrightnessSliderReleasedEventCallback,
        LV_EVENT_PRESS_LOST, state);
  }
  return true;
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
