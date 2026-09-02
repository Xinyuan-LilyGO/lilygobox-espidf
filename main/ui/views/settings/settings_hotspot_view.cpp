/*
 * @Description: Settings personal hotspot page
 * @Author: LILYGO_L
 * @Date: 2026-05-23 00:00:00
 * @LastEditTime: 2026-09-02 17:56:40
 * @License: GPL 3.0
 */
#include "ui/views/settings/settings_basic_view_common.h"

namespace lilygo_box::ui {
namespace {

/**
 * @brief 处理个人热点开关状态变化
 * @param event LVGL 事件对象
 */
void HotspotSwitchChangedEventCallback(lv_event_t* event) {
  auto* state = static_cast<SettingsViewState*>(lv_event_get_user_data(event));
  lv_obj_t* target = lv_event_get_target_obj(event);
  if (state != nullptr && target != nullptr) {
    state->hotspot_enabled = lv_obj_has_state(target, LV_STATE_CHECKED);
  }
}

/**
 * @brief 构建 WLAN 热点设置页面
 * @param body 内容容器
 * @param state 设置页状态
 * @return 创建成功返回 true，否则返回 false
 */
bool BuildHotspotSettingsPage(lv_obj_t* body, SettingsViewState* state) {
  const int width = state->config.width;
  int y = 0;
  if (!CreateSectionLabel(body, "WLAN hotspot settings", y, width)) {
    return false;
  }
  y += kBasicSectionHeight;
  if (!CreateArrowRow(body, "Network name", ReadBasicDeviceName(), y, width,
          nullptr, state)) {
    return false;
  }
  y += kBasicRowHeight;
  if (!CreateArrowRow(
          body, "Security", "WPA2-Personal", y, width, nullptr, state)) {
    return false;
  }
  y += kBasicRowHeight;
  if (!CreateArrowRow(
          body, "Password", "xinyuandianzi", y, width, nullptr, state)) {
    return false;
  }
  y += kBasicRowHeight;
  return CreateArrowRow(
      body, "AP band", "2.4 GHz and 5 GHz", y, width, nullptr, state);
}

/**
 * @brief 打开 WLAN 热点设置页面
 * @param event LVGL 事件对象
 */
void HotspotSettingsClickedEventCallback(lv_event_t* event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
    return;
  }
  ShowNestedPage(static_cast<SettingsViewState*>(lv_event_get_user_data(event)),
      "Set WLAN hotspot", BuildHotspotSettingsPage);
}

/**
 * @brief 构建个人热点设置内容
 * @param body 内容容器
 * @param state 设置页状态
 * @return 创建成功返回 true，否则返回 false
 */
bool BuildHotspotContent(lv_obj_t* body, SettingsViewState* state) {
  const int width = state->config.width;
  int y = 0;
  if (!CreateSwitchRow(body, "WLAN hotspot", y, width, state->hotspot_enabled,
          HotspotSwitchChangedEventCallback, state)) {
    return false;
  }
  y += kBasicRowHeight + 12;
  return CreateArrowRow(body, "Set WLAN hotspot", "", y, width,
      HotspotSettingsClickedEventCallback, state);
}

}  // namespace

bool ShowPersonalHotspotPage(SettingsViewState* state) {
  return ShowBasicPage(state, "Personal Hotspot", BuildHotspotContent);
}

}  // namespace lilygo_box::ui
