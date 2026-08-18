/*
 * @Description: Settings sound and haptics page
 * @Author: LILYGO_L
 * @Date: 2026-05-23 00:00:00
 * @LastEditTime: 2026-05-23 00:00:00
 * @License: GPL 3.0
 */
#include "ui/views/settings/settings_basic_view_common.h"

#include <cstdint>

#include "app/storage/haptic_storage.h"
#include "app/storage/sound_storage.h"
#include "hal/providers/audio_provider.h"
#include "hal/providers/haptic_provider.h"
#include "ui/resources/fonts/icon_assets.h"
#include "ui/haptic_feedback.h"

namespace lilygo_box::ui {
namespace {

/**
 * @brief 保存最终音量设置
 * @param state 设置页状态
 */
void SaveSoundPreferences(SettingsViewState* state) {
  if (state == nullptr) {
    return;
  }
  app::SoundPreferences preferences = app::GetSoundPreferences();
  preferences.volume_percent = state->audio_volume_percent;
  app::UpdateSoundPreferences(preferences);
}

/**
 * @brief 保存最终振动设置
 * @param state 设置页状态
 */
void SaveHapticPreferences(SettingsViewState* state) {
  if (state == nullptr) {
    return;
  }
  app::HapticPreferences preferences = app::GetHapticPreferences();
  preferences.enabled = state->haptics_enabled;
  preferences.strength_percent = state->haptic_strength_percent;
  SetUiHapticPreferences(preferences.enabled, preferences.strength_percent);
  app::UpdateHapticPreferences(preferences);
}

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
 * @brief 处理系统触感开关状态变化
 * @param event LVGL 事件对象
 */
void HapticsSwitchChangedEventCallback(lv_event_t* event) {
  auto* state = static_cast<SettingsViewState*>(lv_event_get_user_data(event));
  lv_obj_t* target = lv_event_get_target_obj(event);
  if (state != nullptr && target != nullptr) {
    state->haptics_enabled = lv_obj_has_state(target, LV_STATE_CHECKED);
    SaveHapticPreferences(state);
    PlaySettingsHapticPreview(state);
    if (state->haptic_strength_controls != nullptr) {
      if (state->haptics_enabled) {
        lv_obj_clear_flag(
            state->haptic_strength_controls, LV_OBJ_FLAG_HIDDEN);
      } else {
        lv_obj_add_flag(
            state->haptic_strength_controls, LV_OBJ_FLAG_HIDDEN);
      }
    }
  }
}

/**
 * @brief 保存音量滑动条值并应用到硬件
 * @param event LVGL 事件对象
 */
void VolumeSliderChangedEventCallback(lv_event_t* event) {
  auto* state = static_cast<SettingsViewState*>(lv_event_get_user_data(event));
  if (state != nullptr) {
    state->audio_volume_percent = SliderPercentFromEvent(event);
    PlaySettingsHapticPreview(state);
    if (state->config.audio != nullptr) {
      state->config.audio->SetSpeakerVolumePercent(
          state->audio_volume_percent);
      state->config.audio->StartSpeakerToneLoop();
    }
  }
}

/**
 * @brief 音量滑动条按下后开始循环播放提示音
 * @param event LVGL 事件对象
 */
void VolumeSliderPressedEventCallback(lv_event_t* event) {
  auto* state = static_cast<SettingsViewState*>(lv_event_get_user_data(event));
  if (state != nullptr && state->config.audio != nullptr) {
    state->config.audio->SetSpeakerVolumePercent(state->audio_volume_percent);
    state->config.audio->StartSpeakerToneLoop();
  }
}

/**
 * @brief 音量滑动条松开后停止循环播放提示音
 * @param event LVGL 事件对象
 */
void VolumeSliderReleasedEventCallback(lv_event_t* event) {
  auto* state = static_cast<SettingsViewState*>(lv_event_get_user_data(event));
  if (state != nullptr) {
    if (state->config.audio != nullptr) {
      state->config.audio->StopSpeakerToneLoop();
    }
    SaveSoundPreferences(state);
  }
}

/**
 * @brief 设置滑动条松开后保存最终值
 * @param event LVGL 事件对象
 */
void SettingsSliderReleasedEventCallback(lv_event_t* event) {
  SaveHapticPreferences(
      static_cast<SettingsViewState*>(lv_event_get_user_data(event)));
}

/**
 * @brief 保存触感强度滑动条值并按当前强度预览振动
 * @param event LVGL 事件对象
 */
void HapticSliderChangedEventCallback(lv_event_t* event) {
  auto* state = static_cast<SettingsViewState*>(lv_event_get_user_data(event));
  if (state != nullptr) {
    state->haptic_strength_percent = SliderPercentFromEvent(event);
    PlaySettingsHapticPreview(state);
  }
}

bool BuildSoundHapticsContent(lv_obj_t* body, SettingsViewState* state) {
  state->haptic_strength_controls = nullptr;
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
  lv_obj_t* volume_slider =
      lv_obj_get_child(body, lv_obj_get_child_count(body) - 1);
  state->audio_volume_slider = volume_slider;
  if (volume_slider != nullptr) {
    lv_obj_add_event_cb(volume_slider, VolumeSliderPressedEventCallback,
        LV_EVENT_PRESSED, state);
    lv_obj_add_event_cb(volume_slider, VolumeSliderReleasedEventCallback,
        LV_EVENT_RELEASED, state);
    lv_obj_add_event_cb(volume_slider, VolumeSliderReleasedEventCallback,
        LV_EVENT_PRESS_LOST, state);
  }
  y += 118;
  if (!CreateBasicDivider(body, y, state->config.width)) {
    return false;
  }
  y += 32;
  if (!CreateSwitchRow(body, "System haptics", y, state->config.width,
          state->haptics_enabled, HapticsSwitchChangedEventCallback, state)) {
    return false;
  }
  y += kBasicRowHeight;

  lv_obj_t* controls = lv_obj_create(body);
  if (controls == nullptr) {
    return false;
  }
  state->haptic_strength_controls = controls;
  lv_obj_remove_flag(controls, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(controls, LV_OBJ_FLAG_GESTURE_BUBBLE);
  lv_obj_add_flag(controls, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
  lv_obj_set_size(controls, state->config.width, kBasicRowHeight);
  lv_obj_set_pos(controls, 0, y);
  lv_obj_set_style_bg_opa(controls, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_set_style_border_width(controls, 0, LV_PART_MAIN);
  lv_obj_set_style_radius(controls, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(controls, 0, LV_PART_MAIN);

  if (!CreateSliderRow(controls, icon::kTouchApp, "Haptics",
          state->haptic_strength_percent, 0, state->config.width,
          HapticSliderChangedEventCallback, state)) {
    return false;
  }
  lv_obj_t* haptic_slider =
      lv_obj_get_child(controls, lv_obj_get_child_count(controls) - 1);
  if (haptic_slider != nullptr) {
    lv_obj_add_event_cb(haptic_slider, SettingsSliderReleasedEventCallback,
        LV_EVENT_RELEASED, state);
    lv_obj_add_event_cb(haptic_slider, SettingsSliderReleasedEventCallback,
        LV_EVENT_PRESS_LOST, state);
  }
  if (!state->haptics_enabled) {
    lv_obj_add_flag(controls, LV_OBJ_FLAG_HIDDEN);
  }
  return true;
}

}  // namespace

bool ShowSoundHapticsPage(SettingsViewState* state) {
  return ShowBasicPage(state, "Sound & Haptics", BuildSoundHapticsContent);
}

}  // namespace lilygo_box::ui
