/*
 * @Description: Settings sound and haptics page
 * @Author: LILYGO_L
 * @Date: 2026-05-23 00:00:00
 * @LastEditTime: 2026-10-13 12:00:00
 * @License: GPL 3.0
 */
#include <cstdint>

#include "app/storage/haptic_storage.h"
#include "app/storage/sound_storage.h"
#include "hal/providers/audio_provider.h"
#include "hal/providers/haptic_provider.h"
#include "ui/haptic_feedback.h"
#include "ui/resources/fonts/icon_assets.h"
#include "ui/views/settings/settings_basic_view_common.h"

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
  preferences.enabled = state->sound_enabled;
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
  const uint8_t gain =
      static_cast<uint8_t>(state->haptic_strength_percent * UINT8_MAX / 100);
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
    SetSettingsSliderControlsEnabled(state->haptic_strength_controls,
                                     state->haptic_strength_slider,
                                     state->haptics_enabled);
  }
}

/**
 * @brief 后台应用静音开关，保留原音量并淡化滑动条
 * @param event LVGL 事件对象
 */
void MuteSwitchChangedEventCallback(lv_event_t* event) {
  auto* state = static_cast<SettingsViewState*>(lv_event_get_user_data(event));
  lv_obj_t* target = lv_event_get_target_obj(event);
  if (state == nullptr || target == nullptr) return;
  const bool enabled = !lv_obj_has_state(target, LV_STATE_CHECKED);
  if (state->config.audio == nullptr ||
      !state->config.audio->RequestSpeakerVolumePercent(
          state->audio_volume_percent, false, enabled)) {
    if (state->sound_enabled) {
      lv_obj_remove_state(target, LV_STATE_CHECKED);
    } else {
      lv_obj_add_state(target, LV_STATE_CHECKED);
    }
    return;
  }
  state->sound_enabled = enabled;
  SaveSoundPreferences(state);
  SetSettingsSliderControlsEnabled(state->audio_volume_controls,
                                   state->audio_volume_slider, enabled);
}

/**
 * @brief 保存音量滑动条值并应用到硬件
 * @param event LVGL 事件对象
 */
void VolumeSliderChangedEventCallback(lv_event_t* event) {
  auto* state = static_cast<SettingsViewState*>(lv_event_get_user_data(event));
  if (state != nullptr && state->sound_enabled) {
    state->audio_volume_percent = SliderPercentFromEvent(event);
    PlaySettingsHapticPreview(state);
    if (state->config.audio != nullptr) {
      state->config.audio->RequestSpeakerVolumePercent(
          state->audio_volume_percent, true, state->sound_enabled);
    }
  }
}

/**
 * @brief 音量滑动条按下后开始循环播放提示音
 * @param event LVGL 事件对象
 */
void VolumeSliderPressedEventCallback(lv_event_t* event) {
  auto* state = static_cast<SettingsViewState*>(lv_event_get_user_data(event));
  if (state != nullptr && state->sound_enabled &&
      state->config.audio != nullptr) {
    state->config.audio->RequestSpeakerVolumePercent(
        state->audio_volume_percent, true, state->sound_enabled);
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
      // 停止标志为非阻塞操作，先中止试听，避免后台音量请求等待播放锁时继续循环。
      state->config.audio->StopSpeakerToneLoop();
      state->config.audio->RequestSpeakerVolumePercent(
          state->audio_volume_percent, false, state->sound_enabled);
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
  if (state != nullptr && state->haptics_enabled) {
    state->haptic_strength_percent = SliderPercentFromEvent(event);
    PlaySettingsHapticPreview(state);
  }
}

/**
 * @brief 创建固定高度的滑动条容器，关闭时仍保留布局
 * @param body 页面内容容器
 * @param state 设置页状态
 * @param y 顶部坐标
 * @return 新控件容器，分配失败时返回 nullptr
 */
lv_obj_t* CreateSoundSliderControls(lv_obj_t* body, SettingsViewState* state,
                                    int y) {
  lv_obj_t* controls = lv_obj_create(body);
  if (controls == nullptr) return nullptr;
  lv_obj_remove_flag(controls, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(controls, LV_OBJ_FLAG_GESTURE_BUBBLE);
  lv_obj_add_flag(controls, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
  lv_obj_set_size(controls, state->config.width, 118);
  lv_obj_set_pos(controls, 0, y);
  lv_obj_set_style_bg_opa(controls, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_set_style_border_width(controls, 0, LV_PART_MAIN);
  lv_obj_set_style_radius(controls, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(controls, 0, LV_PART_MAIN);
  return controls;
}

bool BuildSoundHapticsContent(lv_obj_t* body, SettingsViewState* state) {
  RefreshSoundHapticsSettings(state);
  int y = 0;
  if (!CreateSectionLabel(body, "Volume adjustment", y, state->config.width)) {
    return false;
  }
  y += kBasicSectionHeight;
  lv_obj_t* mute_switch = nullptr;
  if (!CreateSwitchRow(body, "Mute", y, state->config.width,
                       !state->sound_enabled, MuteSwitchChangedEventCallback,
                       state, false, &mute_switch)) {
    return false;
  }
  BindSettingsObject(mute_switch, &state->sound_switch);
  y += kBasicRowHeight;
  lv_obj_t* volume_controls = CreateSoundSliderControls(body, state, y);
  if (volume_controls == nullptr) return false;
  BindSettingsObject(volume_controls, &state->audio_volume_controls);
  if (!CreateSliderRow(volume_controls, icon::kVolumeUp, "Volume",
                       state->audio_volume_percent, 0, state->config.width,
                       VolumeSliderChangedEventCallback, state)) {
    return false;
  }
  lv_obj_t* volume_slider = lv_obj_get_child(
      volume_controls, lv_obj_get_child_count(volume_controls) - 1);
  BindSettingsObject(volume_slider, &state->audio_volume_slider);
  if (volume_slider != nullptr) {
    lv_obj_add_event_cb(volume_slider, VolumeSliderPressedEventCallback,
        LV_EVENT_PRESSED, state);
    lv_obj_add_event_cb(volume_slider, VolumeSliderReleasedEventCallback,
        LV_EVENT_RELEASED, state);
    lv_obj_add_event_cb(volume_slider, VolumeSliderReleasedEventCallback,
        LV_EVENT_PRESS_LOST, state);
  }
  SetSettingsSliderControlsEnabled(volume_controls, volume_slider,
                                   state->sound_enabled);
  y += 118;
  if (!CreateBasicDivider(body, y, state->config.width)) {
    return false;
  }
  y += 32;
  lv_obj_t* haptics_switch = nullptr;
  if (!CreateSwitchRow(body, "System haptics", y, state->config.width,
                       state->haptics_enabled,
                       HapticsSwitchChangedEventCallback, state, false,
                       &haptics_switch)) {
    return false;
  }
  BindSettingsObject(haptics_switch, &state->haptics_switch);
  y += kBasicRowHeight;

  lv_obj_t* controls = CreateSoundSliderControls(body, state, y);
  if (controls == nullptr) {
    return false;
  }
  BindSettingsObject(controls, &state->haptic_strength_controls);

  if (!CreateSliderRow(controls, icon::kTouchApp, "Haptics",
          state->haptic_strength_percent, 0, state->config.width,
          HapticSliderChangedEventCallback, state)) {
    return false;
  }
  lv_obj_t* haptic_slider =
      lv_obj_get_child(controls, lv_obj_get_child_count(controls) - 1);
  BindSettingsObject(haptic_slider, &state->haptic_strength_slider);
  if (haptic_slider != nullptr) {
    lv_obj_add_event_cb(haptic_slider, SettingsSliderReleasedEventCallback,
        LV_EVENT_RELEASED, state);
    lv_obj_add_event_cb(haptic_slider, SettingsSliderReleasedEventCallback,
        LV_EVENT_PRESS_LOST, state);
  }
  SetSettingsSliderControlsEnabled(controls, haptic_slider,
                                   state->haptics_enabled);
  return true;
}

}  // namespace

void RefreshSoundHapticsSettings(SettingsViewState* state) {
  if (state == nullptr) return;
  state->audio_volume_percent = app::GetSoundPreferences().volume_percent;
  state->sound_enabled =
      state->config.audio != nullptr && state->config.audio->SpeakerEnabled();
  const auto haptic = app::GetHapticPreferences();
  state->haptics_enabled = haptic.enabled;
  state->haptic_strength_percent = haptic.strength_percent;
  if (state->audio_volume_slider != nullptr) {
    lv_slider_set_value(state->audio_volume_slider, state->audio_volume_percent,
                        LV_ANIM_OFF);
  }
  if (state->haptic_strength_slider != nullptr) {
    lv_slider_set_value(state->haptic_strength_slider,
                        state->haptic_strength_percent, LV_ANIM_OFF);
  }
  if (state->sound_switch != nullptr) {
    if (state->sound_enabled) {
      lv_obj_remove_state(state->sound_switch, LV_STATE_CHECKED);
    } else {
      lv_obj_add_state(state->sound_switch, LV_STATE_CHECKED);
    }
  }
  if (state->haptics_switch != nullptr) {
    if (state->haptics_enabled) {
      lv_obj_add_state(state->haptics_switch, LV_STATE_CHECKED);
    } else {
      lv_obj_remove_state(state->haptics_switch, LV_STATE_CHECKED);
    }
  }
  SetSettingsSliderControlsEnabled(state->audio_volume_controls,
                                   state->audio_volume_slider,
                                   state->sound_enabled);
  SetSettingsSliderControlsEnabled(state->haptic_strength_controls,
                                   state->haptic_strength_slider,
                                   state->haptics_enabled);
}

bool ShowSoundHapticsPage(SettingsViewState* state) {
  return ShowBasicPage(state, "Sound & Haptics", BuildSoundHapticsContent);
}

}  // namespace lilygo_box::ui
