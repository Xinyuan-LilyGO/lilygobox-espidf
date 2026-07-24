/*
 * @Description: 更多设置页面
 * @Author: LILYGO_L
 * @Date: 2026-07-24 00:00:00
 * @LastEditTime: 2026-07-24 00:00:00
 * @License: GPL 3.0
 */
#include "ui/views/settings/settings_basic_view_common.h"

namespace lilygo_box::ui {
namespace {

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

  if (!CreateSectionLabel(body, "Accessibility", 0, state->config.width)) {
    return false;
  }
  int y = kBasicSectionHeight;
  if (!CreateActionRow(body, "Lock now", y, state->config.width,
          LockNowClickedEventCallback, state)) {
    return false;
  }
  y += kBasicRowHeight;
  return CreateArrowRow(body, "Power options", "", y,
      state->config.width, PowerOptionsClickedEventCallback, state);
}

}  // namespace

bool ShowMoreSettingsPage(SettingsViewState* state) {
  return ShowBasicPage(state, "More Settings", BuildMoreSettingsContent);
}

}  // namespace lilygo_box::ui
