/*
 * @Description: Settings Bluetooth page
 * @Author: LILYGO_L
 * @Date: 2026-05-23 00:00:00
 * @LastEditTime: 2026-05-23 00:00:00
 * @License: GPL 3.0
 */
#include "ui/views/settings/settings_basic_view_common.h"

namespace lilygo_box::ui {
namespace {

/**
 * @brief 处理蓝牙开关状态变化
 * @param event LVGL 事件对象
 */
void BluetoothSwitchChangedEventCallback(lv_event_t* event) {
  auto* state = static_cast<SettingsViewState*>(lv_event_get_user_data(event));
  lv_obj_t* target = lv_event_get_target_obj(event);
  if (state != nullptr && target != nullptr) {
    state->bluetooth_enabled = lv_obj_has_state(target, LV_STATE_CHECKED);
  }
}

/**
 * @brief 构建已配对蓝牙设备页面
 * @param body 内容容器
 * @param state 设置页状态
 * @return 创建成功返回 true，否则返回 false
 */
bool BuildPairedBluetoothPage(lv_obj_t* body, SettingsViewState* state) {
  const int width = state->config.width;
  if (!CreateSectionLabel(body, "Paired Bluetooth devices", 0, width)) {
    return false;
  }
  if (!CreateArrowRow(body, ReadBasicDeviceName(), "", kBasicSectionHeight,
          width, nullptr, state)) {
    return false;
  }
  if (!CreateBasicDivider(body, kBasicSectionHeight + kBasicRowHeight + 10,
          width)) {
    return false;
  }
  return CreateArrowRow(body, "Unpair", "", kBasicSectionHeight +
      kBasicRowHeight + 28, width, nullptr, state);
}

/**
 * @brief 打开已配对蓝牙设备页面
 * @param event LVGL 事件对象
 */
void PairedBluetoothClickedEventCallback(lv_event_t* event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
    return;
  }
  ShowNestedPage(static_cast<SettingsViewState*>(lv_event_get_user_data(event)),
      "Paired devices", BuildPairedBluetoothPage);
}

/**
 * @brief 构建蓝牙设置内容
 * @param body 内容容器
 * @param state 设置页状态
 * @return 创建成功返回 true，否则返回 false
 */
bool BuildBluetoothContent(lv_obj_t* body, SettingsViewState* state) {
  const int width = state->config.width;
  int y = 0;
  if (!CreateSwitchRow(body, "Bluetooth", y, width, state->bluetooth_enabled,
          BluetoothSwitchChangedEventCallback, state)) {
    return false;
  }
  y += kBasicRowHeight + 12;
  if (!CreateSectionLabel(body, "Device name", y, width)) {
    return false;
  }
  y += kBasicSectionHeight;
  return CreateArrowRow(body, ReadBasicDeviceName(), "", y, width,
      PairedBluetoothClickedEventCallback, state);
}

}  // namespace

/**
 * @brief 从设置主页打开蓝牙详情页
 * @param state 设置页状态
 * @return 打开成功返回 true，否则返回 false
 */
bool ShowBluetoothPage(SettingsViewState* state) {
  return ShowBasicPage(state, "Bluetooth", BuildBluetoothContent);
}

}  // namespace lilygo_box::ui
