/*
 * @Description: 更多设置页面
 * @Author: LILYGO_L
 * @Date: 2026-07-24 00:00:00
 * @LastEditTime: 2026-07-24 00:00:00
 * @License: GPL 3.0
 */
#include "app/storage/otg_storage.h"
#include "base/logger.h"
#include "hal/providers/otg_provider.h"
#include "ui/views/settings/settings_basic_view_common.h"

namespace lilygo_box::ui {
namespace {

constexpr uint32_t kOtgRefreshPeriodMs = 500;

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
          &state->otg_switch)) {
    return false;
  }
  const lv_style_selector_t disabled_main =
      static_cast<lv_style_selector_t>(LV_PART_MAIN) |
      static_cast<lv_style_selector_t>(LV_STATE_DISABLED);
  const lv_style_selector_t disabled_indicator =
      static_cast<lv_style_selector_t>(LV_PART_INDICATOR) |
      static_cast<lv_style_selector_t>(LV_STATE_DISABLED);
  lv_obj_set_style_bg_color(state->otg_switch,
      lv_color_hex(theme::LightNeutralTheme().disabled_container),
      disabled_main);
  lv_obj_set_style_bg_color(state->otg_switch,
      lv_color_hex(theme::LightNeutralTheme().disabled_container),
      disabled_indicator);
  lv_obj_set_style_opa(state->otg_switch, LV_OPA_COVER, disabled_main);

  lv_obj_t* description = CreateLabel(body,
      "Allow this device to supply power to connected USB devices. "
      "OTG is unavailable while USB power is connected.",
      lv_color_hex(kBasicMutedColor), Font24());
  if (description == nullptr) {
    return false;
  }
  lv_obj_set_width(
      description, state->config.width - 2 * kBasicSidePadding);
  lv_label_set_long_mode(description, LV_LABEL_LONG_WRAP);
  lv_obj_align(description, LV_ALIGN_TOP_LEFT, kBasicSidePadding,
      kBasicRowHeight + 18);

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
 * @brief 构建更多设置页面内容
 * @param body 内容容器
 * @param state 设置页状态
 * @return 创建成功返回 true，否则返回 false
 */
bool BuildMoreSettingsContent(lv_obj_t* body, SettingsViewState* state) {
  if (body == nullptr || state == nullptr) {
    return false;
  }

  int y = 0;
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

  if (state->config.otg != nullptr) {
    if (!CreateSectionLabel(
            body, "Special features", y, state->config.width)) {
      return false;
    }
    y += kBasicSectionHeight;
    if (!CreateArrowRow(body, "OTG", "", y, state->config.width,
            OtgClickedEventCallback, state)) {
      return false;
    }
  }

  return true;
}

}  // namespace

bool ShowMoreSettingsPage(SettingsViewState* state) {
  return ShowBasicPage(state, "More Settings", BuildMoreSettingsContent);
}

}  // namespace lilygo_box::ui
