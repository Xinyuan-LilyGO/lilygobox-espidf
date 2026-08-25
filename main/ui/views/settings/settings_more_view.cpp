/*
 * @Description: 更多设置页面
 * @Author: LILYGO_L
 * @Date: 2026-07-24 00:00:00
 * @LastEditTime: 2026-08-19 20:20:02
 * @License: GPL 3.0
 */
#include "app/storage/input_method_storage.h"
#include "app/storage/keyboard_expansion_storage.h"
#include "app/storage/otg_storage.h"
#include "base/logger.h"
#include "hal/providers/keyboard_expansion_provider.h"
#include "hal/providers/haptic_provider.h"
#include "hal/providers/otg_provider.h"
#include "ui/resources/fonts/icon_assets.h"
#include "ui/views/settings/settings_basic_view_common.h"
#include "ui/widgets/prompt/prompt_dialog.h"
#include "ui/widgets/shared_keyboard.h"

#include <cstdint>
#include <cstdio>

namespace lilygo_box::ui {
namespace {

constexpr uint32_t kOtgRefreshPeriodMs = 500;
constexpr uint32_t kKeyboardExpansionRefreshPeriodMs = 100;
constexpr char kKeyboardExpansionSubtitle[] =
    "Automatically use keyboard expansion hardware when connected.";
constexpr int kKeyboardExpansionPromptSideMargin = 34;
constexpr int kKeyboardExpansionPromptBottomMargin = 32;
constexpr int kKeyboardExpansionPromptRadius = 48;
constexpr int kKeyboardExpansionNotFoundPromptHeight = 280;
constexpr int kKeyboardExpansionFailurePromptHeight = 520;
constexpr int kKeyboardExpansionPromptInnerPadding = 32;
constexpr int kKeyboardExpansionPromptButtonHeight = 74;
constexpr int kKeyboardExpansionPromptButtonRadius = 24;

void PlaySettingsHapticPreview(SettingsViewState* state) {
  if (state == nullptr || !state->haptics_enabled ||
      state->config.haptic == nullptr) {
    return;
  }
  const uint8_t gain = static_cast<uint8_t>(
      state->haptic_strength_percent * UINT8_MAX / 100);
  state->config.haptic->PlayHapticWaveform(1, 1, gain, true);
}

void SaveKeyboardExpansionEnabledPreference(bool enabled) {
  app::KeyboardExpansionPreferences preferences =
      app::GetKeyboardExpansionPreferences();
  preferences.enabled = enabled;
  if (!app::UpdateKeyboardExpansionPreferences(preferences)) {
    LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
        "Save keyboard expansion preference failed\n");
  }
}

/**
 * @brief 刷新 OTG 开关的保存状态和外部电源限制
 * @param state 设置页状态
 */
void RefreshOtgSwitch(SettingsViewState* state) {
  if (state == nullptr || state->otg_switch == nullptr ||
      state->config.otg == nullptr) {
    return;
  }

  bool external_power_present = false;
  const bool status_available =
      state->config.otg->ReadExternalPowerPresent(&external_power_present);
  const bool enabled = app::GetOtgPreferences().enabled;
  state->otg_enabled = enabled;
  if (enabled) {
    lv_obj_add_state(state->otg_switch, LV_STATE_CHECKED);
  } else {
    lv_obj_remove_state(state->otg_switch, LV_STATE_CHECKED);
  }

  if (status_available && !external_power_present) {
    lv_obj_remove_state(state->otg_switch, LV_STATE_DISABLED);
  } else {
    lv_obj_add_state(state->otg_switch, LV_STATE_DISABLED);
  }
}

/**
 * @brief 定时刷新 OTG 连接状态
 * @param timer LVGL 定时器对象
 */
void OtgRefreshTimerCallback(lv_timer_t* timer) {
  if (timer == nullptr) {
    return;
  }
  RefreshOtgSwitch(static_cast<SettingsViewState*>(
      lv_timer_get_user_data(timer)));
}

/**
 * @brief OTG 页面删除时清理定时器和控件引用
 * @param event LVGL 事件对象
 */
void OtgPageDeleteEventCallback(lv_event_t* event) {
  if (lv_event_get_code(event) != LV_EVENT_DELETE) {
    return;
  }

  auto* state = static_cast<SettingsViewState*>(lv_event_get_user_data(event));
  if (state == nullptr) {
    return;
  }
  if (state->otg_refresh_timer != nullptr) {
    lv_timer_delete(state->otg_refresh_timer);
    state->otg_refresh_timer = nullptr;
  }
  state->otg_switch = nullptr;
}

/**
 * @brief 处理 OTG 反向供电开关状态变化
 * @param event LVGL 事件对象
 */
void OtgSwitchChangedEventCallback(lv_event_t* event) {
  auto* state = static_cast<SettingsViewState*>(lv_event_get_user_data(event));
  lv_obj_t* target = lv_event_get_target_obj(event);
  if (state == nullptr || target == nullptr || state->config.otg == nullptr) {
    return;
  }

  const bool enabled = lv_obj_has_state(target, LV_STATE_CHECKED);
  if (!state->config.otg->SetOtgPowerEnabled(enabled)) {
    if (state->otg_enabled) {
      lv_obj_add_state(target, LV_STATE_CHECKED);
    } else {
      lv_obj_remove_state(target, LV_STATE_CHECKED);
    }
    LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
        "Set OTG reverse power failed\n");
    return;
  }

  state->otg_enabled = enabled;
  app::OtgPreferences preferences;
  preferences.enabled = enabled;
  if (!app::UpdateOtgPreferences(preferences)) {
    LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
        "Save OTG preference failed\n");
  }
}

/**
 * @brief 构建 OTG 设置页面内容
 * @param body 内容容器
 * @param state 设置页状态
 * @return 创建成功返回 true，否则返回 false
 */
bool BuildOtgContent(lv_obj_t* body, SettingsViewState* state) {
  if (body == nullptr || state == nullptr || state->config.otg == nullptr) {
    return false;
  }

  lv_obj_add_event_cb(
      body, OtgPageDeleteEventCallback, LV_EVENT_DELETE, state);

  if (!CreateSwitchRow(body, "OTG switch", 0, state->config.width,
          false, OtgSwitchChangedEventCallback, state, false,
          &state->otg_switch,
          "Power USB devices when USB input is disconnected.")) {
    return false;
  }
  if (state->otg_refresh_timer != nullptr) {
    lv_timer_delete(state->otg_refresh_timer);
  }
  state->otg_refresh_timer =
      lv_timer_create(OtgRefreshTimerCallback, kOtgRefreshPeriodMs, state);
  if (state->otg_refresh_timer == nullptr) {
    return false;
  }
  RefreshOtgSwitch(state);
  return true;
}

/**
 * @brief 打开 OTG 设置页面
 * @param event LVGL 事件对象
 */
void OtgClickedEventCallback(lv_event_t* event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
    return;
  }
  ShowNestedPage(static_cast<SettingsViewState*>(lv_event_get_user_data(event)),
      "OTG", BuildOtgContent);
}

/**
 * @brief 请求立即锁屏
 * @param event LVGL 事件对象
 */
void LockNowClickedEventCallback(lv_event_t* event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
    return;
  }

  auto* state = static_cast<SettingsViewState*>(
      lv_event_get_user_data(event));
  if (state != nullptr && state->config.request_screen_lock) {
    state->config.request_screen_lock();
  }
}

/**
 * @brief 打开系统电源选项
 * @param event LVGL 事件对象
 */
void PowerOptionsClickedEventCallback(lv_event_t* event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
    return;
  }

  auto* state = static_cast<SettingsViewState*>(
      lv_event_get_user_data(event));
  if (state != nullptr && state->config.show_power_options) {
    state->config.show_power_options();
  }
}

/**
 * @brief 保存连接实体键盘时使用屏幕键盘的偏好
 * @param event LVGL 事件对象
 */
void OnScreenKeyboardSwitchChangedEventCallback(lv_event_t* event) {
  auto* state = static_cast<SettingsViewState*>(lv_event_get_user_data(event));
  lv_obj_t* target = lv_event_get_target_obj(event);
  if (state == nullptr || target == nullptr) {
    return;
  }
  const bool enabled = lv_obj_has_state(target, LV_STATE_CHECKED);
  app::InputMethodPreferences preferences =
      app::GetInputMethodPreferences();
  preferences.use_on_screen_keyboard = enabled;
  if (!app::UpdateInputMethodPreferences(preferences)) {
    if (state->on_screen_keyboard_enabled) {
      lv_obj_add_state(target, LV_STATE_CHECKED);
    } else {
      lv_obj_remove_state(target, LV_STATE_CHECKED);
    }
    LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
        "Save on-screen keyboard preference failed\n");
    return;
  }
  state->on_screen_keyboard_enabled = enabled;
  RefreshSharedKeyboardVisibility();
}

/**
 * @brief 构建输入法设置页面内容
 * @param body 内容容器
 * @param state 设置页状态
 * @return 创建成功返回 true，否则返回 false
 */
bool BuildInputMethodContent(lv_obj_t* body, SettingsViewState* state) {
  if (body == nullptr || state == nullptr) {
    return false;
  }

  int y = 0;
  if (!CreateSectionLabel(body, "Options", y, state->config.width)) {
    return false;
  }
  y += kBasicSectionHeight;
  if (!CreateSwitchRow(body, "Use on-screen keyboard", y,
          state->config.width, state->on_screen_keyboard_enabled,
          OnScreenKeyboardSwitchChangedEventCallback, state, false, nullptr,
          "Show with a connected physical keyboard.")) {
    return false;
  }
  return true;
}

/**
 * @brief 打开输入法设置页面
 * @param event LVGL 事件对象
 */
void InputMethodClickedEventCallback(lv_event_t* event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
    return;
  }
  ShowNestedPage(static_cast<SettingsViewState*>(lv_event_get_user_data(event)),
      "Input Method", BuildInputMethodContent);
}

void StopKeyboardExpansionRefreshTimer(SettingsViewState* state) {
  if (state != nullptr && state->keyboard_expansion_refresh_timer != nullptr) {
    lv_timer_delete(state->keyboard_expansion_refresh_timer);
    state->keyboard_expansion_refresh_timer = nullptr;
  }
}

void SetKeyboardExpansionSwitchChecked(
    SettingsViewState* state, bool checked) {
  if (state == nullptr) {
    return;
  }
  state->keyboard_expansion_enabled = checked;
  if (state->keyboard_expansion_switch == nullptr) {
    return;
  }
  if (checked) {
    lv_obj_add_state(state->keyboard_expansion_switch, LV_STATE_CHECKED);
  } else {
    lv_obj_remove_state(state->keyboard_expansion_switch, LV_STATE_CHECKED);
  }
}

void SetKeyboardExpansionScanning(
    SettingsViewState* state, bool scanning) {
  if (state == nullptr) {
    return;
  }
  if (state->keyboard_expansion_switch != nullptr) {
    if (scanning) {
      lv_obj_add_state(
          state->keyboard_expansion_switch, LV_STATE_DISABLED);
    } else {
      lv_obj_remove_state(
          state->keyboard_expansion_switch, LV_STATE_DISABLED);
    }
  }
}

void SetKeyboardBacklightControlsState(
    SettingsViewState* state, bool visible, bool enabled) {
  if (state == nullptr || state->keyboard_backlight_controls == nullptr) {
    return;
  }
  if (visible) {
    lv_obj_remove_flag(
        state->keyboard_backlight_controls, LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_obj_add_flag(
        state->keyboard_backlight_controls, LV_OBJ_FLAG_HIDDEN);
  }
  lv_obj_set_style_opa(state->keyboard_backlight_controls,
      enabled ? LV_OPA_COVER : LV_OPA_50, LV_PART_MAIN);
  if (state->keyboard_backlight_slider == nullptr) {
    return;
  }
  if (enabled) {
    lv_obj_remove_state(
        state->keyboard_backlight_slider, LV_STATE_DISABLED);
  } else {
    lv_obj_add_state(state->keyboard_backlight_slider, LV_STATE_DISABLED);
  }
}

void KeyboardBacklightSliderChangedEventCallback(lv_event_t* event) {
  auto* state = static_cast<SettingsViewState*>(lv_event_get_user_data(event));
  lv_obj_t* slider = lv_event_get_target_obj(event);
  if (state == nullptr || slider == nullptr ||
      state->config.keyboard_expansion == nullptr) {
    return;
  }

  const int previous_percent =
      state->keyboard_backlight_brightness_percent;
  const int brightness_percent = SliderPercentFromEvent(event);
  if (!state->config.keyboard_expansion->
          SetKeyboardBacklightBrightnessPercent(brightness_percent)) {
    lv_slider_set_value(slider, previous_percent, LV_ANIM_OFF);
    LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
        "Set keyboard backlight brightness failed\n");
    return;
  }
  state->keyboard_backlight_brightness_percent = brightness_percent;
  PlaySettingsHapticPreview(state);
}

void KeyboardBacklightSliderReleasedEventCallback(lv_event_t* event) {
  auto* state = static_cast<SettingsViewState*>(lv_event_get_user_data(event));
  if (state == nullptr) {
    return;
  }
  app::KeyboardExpansionPreferences preferences =
      app::GetKeyboardExpansionPreferences();
  preferences.backlight_brightness_percent =
      state->keyboard_backlight_brightness_percent;
  if (!app::UpdateKeyboardExpansionPreferences(preferences)) {
    LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
        "Save keyboard backlight preference failed\n");
  }
}

void AppendFailedKeyboardExpansionComponent(char* message, size_t capacity,
    size_t* used, const char* name,
    hal::KeyboardExpansionComponentState component_state) {
  if (message == nullptr || used == nullptr || name == nullptr ||
      *used >= capacity ||
      component_state != hal::KeyboardExpansionComponentState::kFailed) {
    return;
  }
  const int written = std::snprintf(
      message + *used, capacity - *used, "\n- %s", name);
  if (written > 0) {
    *used += static_cast<size_t>(written) < capacity - *used
        ? static_cast<size_t>(written)
        : capacity - *used - 1;
  }
}

void BuildKeyboardExpansionFailureMessage(SettingsViewState* state,
    const hal::KeyboardExpansionStatus& status) {
  if (state == nullptr) {
    return;
  }
  char* message = state->keyboard_expansion_prompt_message;
  constexpr size_t capacity =
      sizeof(state->keyboard_expansion_prompt_message);
  size_t used = 0;
  const int written = std::snprintf(
      message, capacity, "Could not initialize the following hardware:");
  if (written > 0) {
    used = static_cast<size_t>(written) < capacity
        ? static_cast<size_t>(written)
        : capacity - 1;
  }
  AppendFailedKeyboardExpansionComponent(
      message, capacity, &used, "XL9555", status.xl9555);
  AppendFailedKeyboardExpansionComponent(
      message, capacity, &used, "TCA8418", status.tca8418);
  AppendFailedKeyboardExpansionComponent(
      message, capacity, &used, "SY7200A", status.sy7200a);
  AppendFailedKeyboardExpansionComponent(
      message, capacity, &used, "CC1101", status.cc1101);
  AppendFailedKeyboardExpansionComponent(
      message, capacity, &used, "NRF24L01", status.nrf24l01);
  AppendFailedKeyboardExpansionComponent(
      message, capacity, &used, "ST25R3916", status.st25r3916);
  if (used == static_cast<size_t>(written)) {
    std::snprintf(message, capacity,
        "Keyboard expansion initialization failed. Check the connection and "
        "try again.");
  }
}

bool ShowKeyboardExpansionFailurePrompt(SettingsViewState* state,
    const hal::KeyboardExpansionStatus& status) {
  if (state == nullptr || state->root == nullptr) {
    return false;
  }

  const bool not_found =
      status.state == hal::KeyboardExpansionState::kNotFound ||
      status.state == hal::KeyboardExpansionState::kDisconnected;
  if (!not_found) {
    BuildKeyboardExpansionFailureMessage(state, status);
  }
  PromptDialogConfig config;
  config.screen_width = state->config.width;
  config.screen_height = state->config.height;
  config.dialog_width =
      state->config.width - 2 * kKeyboardExpansionPromptSideMargin;
  config.dialog_height = not_found
      ? kKeyboardExpansionNotFoundPromptHeight
      : kKeyboardExpansionFailurePromptHeight;
  config.dialog_radius = kKeyboardExpansionPromptRadius;
  config.inner_padding = kKeyboardExpansionPromptInnerPadding;
  config.header_height = 78;
  config.title_y = 34;
  config.title_subtitle_gap = 8;
  config.subtitle_body_gap = 16;
  config.action_height = 106;
  config.action_button_height = kKeyboardExpansionPromptButtonHeight;
  config.action_button_radius = kKeyboardExpansionPromptButtonRadius;
  config.action_button_gap = 20;
  config.action_bottom_padding = kKeyboardExpansionPromptInnerPadding;
  config.bottom_margin = kKeyboardExpansionPromptBottomMargin;
  config.animation_ms = kDetailSlideAnimationMs;
  config.slide_from_bottom = true;
  config.title = "Keyboard expansion unavailable";
  config.subtitle = not_found
      ? "No keyboard expansion was detected."
      : "Some keyboard expansion hardware could not be initialized.";
  config.title_font = Font32();
  config.subtitle_font = Font24();
  config.action_font = Font28();
  config.title_text_align = LV_TEXT_ALIGN_CENTER;
  config.subtitle_text_align = LV_TEXT_ALIGN_CENTER;
  config.cancel_text = "OK";
  config.cancel_background_color = SettingsThemeColors().action;
  config.cancel_pressed_color = SettingsThemeColors().action_pressed;
  config.cancel_text_color = SettingsThemeColors().on_action;
  config.confirm_text = nullptr;
  lv_obj_t* body = ShowPromptDialog(
      state->root, &state->keyboard_expansion_prompt, config);
  if (body == nullptr) {
    return false;
  }

  if (not_found) {
    return true;
  }
  lv_obj_t* label = lv_label_create(body);
  if (label == nullptr) {
    ClosePromptDialog(&state->keyboard_expansion_prompt);
    return false;
  }
  lv_label_set_text(label, state->keyboard_expansion_prompt_message);
  lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
  SetTextStyle(label, lv_color_hex(SettingsThemeColors().on_surface_variant), Font24());
  lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
  lv_obj_set_width(
      label, config.dialog_width - 2 * kKeyboardExpansionPromptInnerPadding);
  lv_obj_align(label, LV_ALIGN_TOP_MID, 0, 0);
  return true;
}

void KeyboardExpansionRefreshTimerCallback(lv_timer_t* timer) {
  auto* state = static_cast<SettingsViewState*>(
      timer == nullptr ? nullptr : lv_timer_get_user_data(timer));
  if (state == nullptr || state->config.keyboard_expansion == nullptr) {
    return;
  }

  if (state->keyboard_expansion_scan_pending) {
    state->keyboard_expansion_scan_pending = false;
    if (!state->config.keyboard_expansion->StartKeyboardExpansionScan()) {
      StopKeyboardExpansionRefreshTimer(state);
      SetKeyboardExpansionSwitchChecked(state, true);
      SetKeyboardExpansionScanning(state, false);
      SetKeyboardBacklightControlsState(state, true, false);
      RefreshSharedKeyboardVisibility();
      hal::KeyboardExpansionStatus failure_status;
      failure_status.state =
          hal::KeyboardExpansionState::kComponentFailure;
      ShowKeyboardExpansionFailurePrompt(state, failure_status);
      return;
    }
  }

  hal::KeyboardExpansionStatus status;
  if (!state->config.keyboard_expansion->ReadKeyboardExpansionStatus(
          &status) ||
      status.state == hal::KeyboardExpansionState::kScanning) {
    return;
  }
  StopKeyboardExpansionRefreshTimer(state);
  SetKeyboardExpansionScanning(state, false);

  if (status.state == hal::KeyboardExpansionState::kReady) {
    SetKeyboardExpansionSwitchChecked(state, true);
    SetKeyboardBacklightControlsState(state, true, true);
    SaveKeyboardExpansionEnabledPreference(true);
    RefreshSharedKeyboardVisibility();
    return;
  }

  SetKeyboardExpansionSwitchChecked(
      state, app::GetKeyboardExpansionPreferences().enabled);
  SetKeyboardBacklightControlsState(state,
      app::GetKeyboardExpansionPreferences().enabled, false);
  RefreshSharedKeyboardVisibility();
  if (status.state == hal::KeyboardExpansionState::kDisabled) {
    return;
  }
  ShowKeyboardExpansionFailurePrompt(state, status);
}

void KeyboardExpansionPageDeleteEventCallback(lv_event_t* event) {
  if (lv_event_get_code(event) != LV_EVENT_DELETE) {
    return;
  }
  auto* state = static_cast<SettingsViewState*>(lv_event_get_user_data(event));
  if (state == nullptr) {
    return;
  }
  state->keyboard_expansion_scan_pending = false;
  StopKeyboardExpansionRefreshTimer(state);
  ClosePromptDialog(&state->keyboard_expansion_prompt);
  state->keyboard_expansion_switch = nullptr;
  state->keyboard_backlight_controls = nullptr;
  state->keyboard_backlight_slider = nullptr;
}

/**
 * @brief 启用时扫描键盘扩展，关闭时释放扩展硬件
 * @param event LVGL 事件对象
 */
void KeyboardExpansionSwitchChangedEventCallback(lv_event_t* event) {
  auto* state = static_cast<SettingsViewState*>(lv_event_get_user_data(event));
  lv_obj_t* target = lv_event_get_target_obj(event);
  if (state == nullptr || target == nullptr ||
      state->config.keyboard_expansion == nullptr) {
    return;
  }

  const bool enabled = lv_obj_has_state(target, LV_STATE_CHECKED);
  if (!enabled) {
    state->keyboard_expansion_scan_pending = false;
    StopKeyboardExpansionRefreshTimer(state);
    ClosePromptDialog(&state->keyboard_expansion_prompt);
    SaveKeyboardExpansionEnabledPreference(false);
    if (!state->config.keyboard_expansion->DisableKeyboardExpansion()) {
      LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
          "Disable keyboard expansion failed\n");
    }
    SetKeyboardExpansionSwitchChecked(state, false);
    SetKeyboardExpansionScanning(state, false);
    SetKeyboardBacklightControlsState(state, false, false);
    RefreshSharedKeyboardVisibility();
    return;
  }

  if (!state->config.keyboard_expansion->
          SetKeyboardBacklightBrightnessPercent(
              state->keyboard_backlight_brightness_percent)) {
    SetKeyboardExpansionSwitchChecked(state, false);
    SetKeyboardBacklightControlsState(state, false, false);
    SaveKeyboardExpansionEnabledPreference(false);
    RefreshSharedKeyboardVisibility();
    hal::KeyboardExpansionStatus failure_status;
    failure_status.state = hal::KeyboardExpansionState::kComponentFailure;
    ShowKeyboardExpansionFailurePrompt(state, failure_status);
    return;
  }
  state->keyboard_expansion_scan_pending = true;
  SaveKeyboardExpansionEnabledPreference(true);
  SetKeyboardExpansionSwitchChecked(state, true);
  SetKeyboardExpansionScanning(state, true);
  SetKeyboardBacklightControlsState(state, true, false);
  StopKeyboardExpansionRefreshTimer(state);
  state->keyboard_expansion_refresh_timer = lv_timer_create(
      KeyboardExpansionRefreshTimerCallback,
      kKeyboardExpansionRefreshPeriodMs, state);
  if (state->keyboard_expansion_refresh_timer == nullptr) {
    state->keyboard_expansion_scan_pending = false;
    SetKeyboardExpansionSwitchChecked(state, false);
    SetKeyboardExpansionScanning(state, false);
    SetKeyboardBacklightControlsState(state, false, false);
    SaveKeyboardExpansionEnabledPreference(false);
    RefreshSharedKeyboardVisibility();
    hal::KeyboardExpansionStatus failure_status;
    failure_status.state = hal::KeyboardExpansionState::kComponentFailure;
    ShowKeyboardExpansionFailurePrompt(state, failure_status);
  } else {
    lv_timer_ready(state->keyboard_expansion_refresh_timer);
  }
}

/**
 * @brief 构建键盘扩展设置页面内容
 * @param body 内容容器
 * @param state 设置页状态
 * @return 创建成功返回 true，否则返回 false
 */
bool BuildKeyboardExpansionContent(lv_obj_t* body, SettingsViewState* state) {
  if (body == nullptr || state == nullptr ||
      state->config.keyboard_expansion == nullptr) {
    return false;
  }

  lv_obj_add_event_cb(body, KeyboardExpansionPageDeleteEventCallback,
      LV_EVENT_DELETE, state);
  hal::KeyboardExpansionStatus status;
  state->keyboard_expansion_enabled =
      app::GetKeyboardExpansionPreferences().enabled;
  if (state->config.keyboard_expansion->ReadKeyboardExpansionStatus(&status)) {
    if (status.backlight_brightness_percent >= 0) {
      state->keyboard_backlight_brightness_percent =
          status.backlight_brightness_percent;
    }
  }

  int y = 0;
  if (!CreateSectionLabel(body, "Connection", y, state->config.width)) {
    return false;
  }
  y += kBasicSectionHeight;
  if (!CreateSwitchRow(body, "Enable keyboard expansion", y,
          state->config.width, state->keyboard_expansion_enabled,
          KeyboardExpansionSwitchChangedEventCallback, state, false,
          &state->keyboard_expansion_switch,
          kKeyboardExpansionSubtitle)) {
    return false;
  }
  y += kBasicSwitchRowWithSubtitleHeight;

  lv_obj_t* controls = lv_obj_create(body);
  if (controls == nullptr) {
    return false;
  }
  state->keyboard_backlight_controls = controls;
  lv_obj_remove_flag(controls, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(controls, LV_OBJ_FLAG_GESTURE_BUBBLE);
  lv_obj_add_flag(controls, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
  lv_obj_set_size(controls, state->config.width,
      kBasicSectionHeight + kBasicRowHeight);
  lv_obj_set_pos(controls, 0, y);
  lv_obj_set_style_bg_opa(controls, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_set_style_border_width(controls, 0, LV_PART_MAIN);
  lv_obj_set_style_radius(controls, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(controls, 0, LV_PART_MAIN);

  if (!CreateSectionLabel(
          controls, "Keyboard settings", 0, state->config.width)) {
    return false;
  }
  if (!CreateSliderRow(controls, icon::kSunny, "Keyboard backlight",
          state->keyboard_backlight_brightness_percent, kBasicSectionHeight,
          state->config.width, KeyboardBacklightSliderChangedEventCallback,
          state)) {
    return false;
  }
  lv_obj_t* backlight_slider =
      lv_obj_get_child(controls, lv_obj_get_child_count(controls) - 1);
  state->keyboard_backlight_slider = backlight_slider;
  if (backlight_slider != nullptr) {
    lv_obj_add_event_cb(backlight_slider,
        KeyboardBacklightSliderReleasedEventCallback,
        LV_EVENT_RELEASED, state);
    lv_obj_add_event_cb(backlight_slider,
        KeyboardBacklightSliderReleasedEventCallback,
        LV_EVENT_PRESS_LOST, state);
  }
  SetKeyboardBacklightControlsState(state,
      state->keyboard_expansion_enabled,
      state->keyboard_expansion_enabled &&
          status.state == hal::KeyboardExpansionState::kReady);
  if (status.state == hal::KeyboardExpansionState::kScanning) {
    state->keyboard_expansion_scan_pending = false;
    SetKeyboardExpansionScanning(state, true);
    StopKeyboardExpansionRefreshTimer(state);
    state->keyboard_expansion_refresh_timer = lv_timer_create(
        KeyboardExpansionRefreshTimerCallback,
        kKeyboardExpansionRefreshPeriodMs, state);
    if (state->keyboard_expansion_refresh_timer == nullptr) {
      SetKeyboardExpansionScanning(state, false);
    }
  }
  return true;
}

/**
 * @brief 打开键盘扩展设置页面
 * @param event LVGL 事件对象
 */
void KeyboardExpansionClickedEventCallback(lv_event_t* event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
    return;
  }
  ShowNestedPage(static_cast<SettingsViewState*>(lv_event_get_user_data(event)),
      "Keyboard Expansion", BuildKeyboardExpansionContent);
}

/**
 * @brief 构建更多设置页面内容
 * @param body 内容容器
 * @param state 设置页状态
 * @return 创建成功返回 true，否则返回 false
 */
bool BuildMoreSettingsContent(lv_obj_t* body, SettingsViewState* state) {
  if (body == nullptr || state == nullptr) {
    return false;
  }

  const bool supports_keyboard_expansion =
      state->config.device_capabilities.supports_keyboard_expansion &&
      state->config.keyboard_expansion != nullptr;
  int y = 0;
  if (!CreateSectionLabel(
          body, "System settings", y, state->config.width)) {
    return false;
  }
  y += kBasicSectionHeight;
  if (!CreateArrowRow(body, "Input Method", "", y, state->config.width,
          InputMethodClickedEventCallback, state)) {
    return false;
  }
  y += kBasicRowHeight + 12;
  if (!CreateSectionLabel(body, "Accessibility", y, state->config.width)) {
    return false;
  }
  y += kBasicSectionHeight;
  if (!CreateActionRow(body, "Lock now", y, state->config.width,
          LockNowClickedEventCallback, state)) {
    return false;
  }
  y += kBasicRowHeight;
  if (!CreateArrowRow(body, "Power options", "", y,
          state->config.width, PowerOptionsClickedEventCallback, state)) {
    return false;
  }
  y += kBasicRowHeight + 12;

  const bool show_special_features =
      supports_keyboard_expansion || state->config.otg != nullptr;
  if (show_special_features) {
    if (!CreateSectionLabel(
            body, "Special features", y, state->config.width)) {
      return false;
    }
    y += kBasicSectionHeight;
    if (supports_keyboard_expansion) {
      if (!CreateArrowRow(body, "Keyboard Expansion", "", y,
              state->config.width, KeyboardExpansionClickedEventCallback,
              state)) {
        return false;
      }
      y += kBasicRowHeight;
    }
    if (state->config.otg != nullptr) {
      if (!CreateArrowRow(body, "OTG", "", y, state->config.width,
              OtgClickedEventCallback, state)) {
        return false;
      }
    }
  }

  return true;
}

}  // namespace

bool ShowMoreSettingsPage(SettingsViewState* state) {
  return ShowBasicPage(state, "More Settings", BuildMoreSettingsContent);
}

void RefreshKeyboardExpansionSettings(SettingsViewState* state) {
  if (state == nullptr || state->config.keyboard_expansion == nullptr) {
    return;
  }

  hal::KeyboardExpansionStatus status;
  if (!state->config.keyboard_expansion->ReadKeyboardExpansionStatus(
          &status)) {
    return;
  }
  const bool scanning =
      status.state == hal::KeyboardExpansionState::kScanning;
  const bool ready = status.state == hal::KeyboardExpansionState::kReady;
  if (scanning || ready) {
    ClosePromptDialog(&state->keyboard_expansion_prompt);
  }
  SetKeyboardExpansionSwitchChecked(state,
      app::GetKeyboardExpansionPreferences().enabled);
  SetKeyboardExpansionScanning(state, scanning);
  const bool enabled = app::GetKeyboardExpansionPreferences().enabled;
  SetKeyboardBacklightControlsState(state, enabled, enabled && ready);
}

}  // namespace lilygo_box::ui
