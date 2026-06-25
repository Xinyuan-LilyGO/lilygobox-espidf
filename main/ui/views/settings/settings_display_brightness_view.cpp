/*
 * @Description: Settings display brightness page
 * @Author: LILYGO_L
 * @Date: 2026-05-23 00:00:00
 * @LastEditTime: 2026-05-23 00:00:00
 * @License: GPL 3.0
 */
#include "ui/views/settings/settings_basic_view_common.h"

#include "app/storage/display_storage.h"
#include "hal/providers/screen_provider.h"
#include "ui/font/material_symbols_assets.h"

namespace lilygo_box::ui {
namespace {

constexpr uint32_t kBrightnessSaveDebounceMs = 500;

/**
 * @brief 亮度保存防抖定时器回调，将最后一次滑动值写入 NVS
 * @param timer LVGL 定时器对象，user data 为设置页状态
 */
void SaveBrightnessPreferencesTimerCallback(lv_timer_t* timer) {
  auto* state = static_cast<SettingsViewState*>(lv_timer_get_user_data(timer));
  if (state == nullptr) {
    return;
  }

  app::DisplayPreferences preferences;
  preferences.brightness_percent = state->display_brightness_percent;
  app::SaveDisplayPreferencesToNvs(preferences);
  lv_timer_delete(timer);
  state->display_brightness_save_timer = nullptr;
}

/**
 * @brief 调度亮度偏好异步保存，连续滑动时只保存最后一次值
 * @param state 设置页状态
 */
void ScheduleBrightnessPreferencesSave(SettingsViewState* state) {
  if (state == nullptr) {
    return;
  }

  if (state->display_brightness_save_timer == nullptr) {
    state->display_brightness_save_timer = lv_timer_create(
        SaveBrightnessPreferencesTimerCallback, kBrightnessSaveDebounceMs,
        state);
    if (state->display_brightness_save_timer != nullptr) {
      lv_timer_set_repeat_count(state->display_brightness_save_timer, 1);
    }
    return;
  }

  lv_timer_reset(state->display_brightness_save_timer);
}

/**
 * @brief 保存屏幕亮度滑动条值
 * @param event LVGL 事件对象
 */
void BrightnessSliderChangedEventCallback(lv_event_t* event) {
  auto* state = static_cast<SettingsViewState*>(lv_event_get_user_data(event));
  if (state != nullptr) {
    const int brightness_percent = SliderPercentFromEvent(event);
    state->display_brightness_percent = brightness_percent;
    ScheduleBrightnessPreferencesSave(state);
    if (state->config.screen != nullptr) {
      state->config.screen->SetScreenBrightnessPercent(brightness_percent);
    }
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
