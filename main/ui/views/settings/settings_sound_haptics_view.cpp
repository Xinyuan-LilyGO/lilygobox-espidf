/*
 * @Description: Settings sound and haptics page
 * @Author: LILYGO_L
 * @Date: 2026-05-23 00:00:00
 * @LastEditTime: 2026-05-23 00:00:00
 * @License: GPL 3.0
 */
#include "ui/views/settings/settings_basic_view_common.h"

#include <cstdint>

#include "app/storage/audio_storage.h"
#include "app/storage/haptic_storage.h"
#include "app/storage/storage_task.h"
#include "hal/providers/audio_provider.h"
#include "hal/providers/haptic_provider.h"
#include "ui/font/material_symbols_assets.h"
#include "ui/haptic_feedback.h"

namespace lilygo_box::ui {
namespace {

struct SoundHapticsRefreshRequest {
  lv_obj_t* body = nullptr;
  SettingsViewState* state = nullptr;
};

SoundHapticsRefreshRequest g_sound_haptics_refresh_request = {};

/**
 * @brief 构建声音与触感设置内容
 * @param body 内容容器
 * @param state 设置页状态
 * @return 创建成功返回 true，否则返回 false
 */
bool BuildSoundHapticsContent(lv_obj_t* body, SettingsViewState* state);

/**
 * @brief 异步保存音频设置偏好
 * @param state 设置页状态
 */
void SaveAudioPreferencesAsync(SettingsViewState* state) {
  if (state == nullptr) {
    return;
  }
  app::AudioPreferences preferences;
  preferences.volume_percent = state->audio_volume_percent;
  app::StartStorageTask("audio_save", [preferences]() {
    app::SaveAudioPreferencesToNvs(preferences);
  });
}

/**
 * @brief 异步保存振动设置偏好
 * @param state 设置页状态
 */
void SaveHapticPreferencesAsync(SettingsViewState* state) {
  if (state == nullptr) {
    return;
  }
  app::HapticPreferences preferences;
  preferences.enabled = state->haptics_enabled;
  preferences.strength_percent = state->haptic_strength_percent;
  SetUiHapticPreferences(preferences.enabled, preferences.strength_percent);
  app::StartStorageTask("haptic_save", [preferences]() {
    app::SaveHapticPreferencesToNvs(preferences);
  });
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
 * @brief 异步重建声音与触感页面内容
 * @param user_data 回调用户数据
 */
void RebuildSoundHapticsContentAsync(void* user_data) {
  auto* request = static_cast<SoundHapticsRefreshRequest*>(user_data);
  if (request == nullptr || request->body == nullptr ||
      request->state == nullptr) {
    return;
  }
  lv_obj_clean(request->body);
  BuildSoundHapticsContent(request->body, request->state);
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
    SaveHapticPreferencesAsync(state);
    PlaySettingsHapticPreview(state);
    lv_obj_t* row = lv_obj_get_parent(target);
    lv_obj_t* body = row == nullptr ? nullptr : lv_obj_get_parent(row);
    if (body != nullptr) {
      g_sound_haptics_refresh_request.body = body;
      g_sound_haptics_refresh_request.state = state;
      lv_async_call(RebuildSoundHapticsContentAsync,
          &g_sound_haptics_refresh_request);
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
    SaveAudioPreferencesAsync(state);
  }
}

/**
 * @brief 设置滑动条松开后保存最终值
 * @param event LVGL 事件对象
 */
void SettingsSliderReleasedEventCallback(lv_event_t* event) {
  SaveHapticPreferencesAsync(
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
  lv_obj_t* volume_slider =
      lv_obj_get_child(body, lv_obj_get_child_count(body) - 1);
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
  if (!state->haptics_enabled) {
    return true;
  }
  y += kBasicRowHeight;
  if (!CreateSliderRow(body, icon::kTouchApp, "Haptics",
          state->haptic_strength_percent, y, state->config.width,
          HapticSliderChangedEventCallback, state)) {
    return false;
  }
  lv_obj_t* haptic_slider =
      lv_obj_get_child(body, lv_obj_get_child_count(body) - 1);
  if (haptic_slider != nullptr) {
    lv_obj_add_event_cb(haptic_slider, SettingsSliderReleasedEventCallback,
        LV_EVENT_RELEASED, state);
    lv_obj_add_event_cb(haptic_slider, SettingsSliderReleasedEventCallback,
        LV_EVENT_PRESS_LOST, state);
  }
  return true;
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
