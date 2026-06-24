/*
 * @Description: Settings power saving and battery page
 * @Author: LILYGO_L
 * @Date: 2026-05-23 00:00:00
 * @LastEditTime: 2026-05-23 00:00:00
 * @License: GPL 3.0
 */
#include "ui/views/settings/settings_basic_view_common.h"

#include <cstdio>

#include "app/system_status_cache.h"
#include "hal/providers/bmu_provider.h"
#include "ui/font/material_symbols_assets.h"

namespace lilygo_box::ui {
namespace {

/**
 * @brief 构建电池保护详情页面
 * @param body 内容容器
 * @param state 设置页状态
 * @return 创建成功返回 true，否则返回 false
 */
bool BuildBatteryProtectionPage(lv_obj_t* body, SettingsViewState* state) {
  hal::BmuStatus status;
  bool has_status = false;
  if (state->config.system_status != nullptr &&
      state->config.system_status->bmu_status_valid()) {
    status = state->config.system_status->bmu_status();
    has_status = true;
  }
  if (!has_status) {
    status = hal::BmuStatus();
    status.health_percent = 100;
  }

  char health[24] = {};
  char temperature[24] = {};
  std::snprintf(health, sizeof(health), "%d%%", status.health_percent);
  std::snprintf(temperature, sizeof(temperature), "%.1f C",
      status.pack_temperature_c);

  const int width = state->config.width;
  int y = 0;
  if (!CreateSectionLabel(body, "Battery information", y, width)) {
    return false;
  }
  y += kBasicSectionHeight;
  if (!CreateArrowRow(body, "Battery health", health, y, width, nullptr,
          state)) {
    return false;
  }
  y += kBasicRowHeight;
  return CreateArrowRow(body, "Current battery temperature", temperature, y,
      width, nullptr, state);
}

/**
 * @brief 打开电池保护详情页面
 * @param event LVGL 事件对象
 */
void BatteryProtectionClickedEventCallback(lv_event_t* event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
    return;
  }
  ShowNestedPage(static_cast<SettingsViewState*>(lv_event_get_user_data(event)),
      "Battery protection", BuildBatteryProtectionPage);
}

/**
 * @brief 创建电池信息大卡片
 * @param body 内容容器
 * @param state 设置页状态
 * @param y 顶部坐标
 * @return 创建成功返回 true，否则返回 false
 */
bool CreateBatteryOverviewCard(
    lv_obj_t* body, SettingsViewState* state, int y) {
  hal::BmuStatus status;
  bool has_status = false;
  if (state->config.system_status != nullptr &&
      state->config.system_status->bmu_status_valid()) {
    status = state->config.system_status->bmu_status();
    has_status = true;
  }
  if (!has_status) {
    status = hal::BmuStatus();
    status.charge_percent = 47;
  }

  const int width = state->config.width - 2 * kBasicSidePadding;
  lv_obj_t* card = CreateBox(body, width, 190, kBasicCardColor,
      LV_OPA_COVER, kWifiConnectedCardRadius);
  if (card == nullptr) {
    return false;
  }
  lv_obj_set_pos(card, kBasicSidePadding, y);

  char percent[24] = {};
  std::snprintf(percent, sizeof(percent), "%d%%", status.charge_percent);
  lv_obj_t* percent_label =
      CreateLabel(card, percent, lv_color_hex(kPrimaryTextColor), Font48());
  if (percent_label == nullptr) {
    return false;
  }
  lv_obj_align(percent_label, LV_ALIGN_LEFT_MID, 34, -28);

  const char* status_text = status.charging ? "Charging" : "Not charging";
  lv_obj_t* charging_label =
      CreateLabel(card, status_text, lv_color_hex(kSecondaryTextColor),
          Font24());
  if (charging_label == nullptr) {
    return false;
  }
  lv_obj_align(charging_label, LV_ALIGN_LEFT_MID, 36, 26);

  lv_obj_t* battery_icon = CreateLabel(card, icon::kBatteryAndroidFull,
      lv_color_hex(0x55C76C), MaterialIconFont32());
  if (battery_icon == nullptr) {
    return false;
  }
  lv_obj_align(battery_icon, LV_ALIGN_RIGHT_MID, -36, 0);
  return true;
}

/**
 * @brief 构建省电与电池设置内容
 * @param body 内容容器
 * @param state 设置页状态
 * @return 创建成功返回 true，否则返回 false
 */
bool BuildPowerBatteryContent(lv_obj_t* body, SettingsViewState* state) {
  int y = 0;
  if (!CreateBatteryOverviewCard(body, state, y)) {
    return false;
  }
  y += 212;
  if (!CreateArrowRow(body, "Current mode", "Balanced", y,
          state->config.width, nullptr, state)) {
    return false;
  }
  y += kBasicRowHeight;
  return CreateArrowRow(body, "Battery protection", "", y,
      state->config.width, BatteryProtectionClickedEventCallback, state);
}

}  // namespace

/**
 * @brief 从设置主页打开省电与电池详情页
 * @param state 设置页状态
 * @return 打开成功返回 true，否则返回 false
 */
bool ShowPowerBatteryPage(SettingsViewState* state) {
  return ShowBasicPage(state, "Power Saving & Battery",
      BuildPowerBatteryContent);
}

}  // namespace lilygo_box::ui
