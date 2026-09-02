/*
 * @Description: Settings power saving and battery page
 * @Author: LILYGO_L
 * @Date: 2026-05-23 00:00:00
 * @LastEditTime: 2026-05-23 00:00:00
 * @License: GPL 3.0
 */
#include "ui/views/settings/settings_basic_view_common.h"

#include <algorithm>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>

#include "app/storage/battery_storage.h"
#include "app/system_status_cache.h"
#include "base/logger.h"
#include "hal/providers/battery_management_provider.h"
#include "ui/resources/fonts/icon_assets.h"

namespace lilygo_box::ui {
namespace {

constexpr uint32_t kBatteryRefreshPeriodMs = 1000;
constexpr char kBatteryCapacityAcceptedChars[] = "0123456789";
constexpr size_t kBatteryCapacityMaximumLength = 5;
constexpr int kBatteryCardHeight = 190;
constexpr int kBatteryCardInnerPadding = 28;
constexpr int kBatteryCardRadius = 40;
constexpr int kBatteryFillRadius = 0;
constexpr int kBatteryMainTextTop = 42;
constexpr int kBatteryStatusTextTop = 128;
constexpr int kBatteryStatusIconLeft = kBatteryCardInnerPadding;
constexpr int kBatteryStatusIconTop = kBatteryStatusTextTop - 2;
constexpr int kBatteryStatusChargingTextLeft = kBatteryCardInnerPadding + 32;
constexpr int kBatteryLowThresholdPercent = 20;
constexpr uint32_t kBatteryNormalColor = 0x35D66B;
constexpr uint32_t kBatteryChargingColor = 0x27C769;
constexpr uint32_t kBatteryLowColor = 0xFF3B30;
constexpr uint32_t kBatteryRestColor = 0xB8EFC8;
constexpr uint32_t kBatteryLowRestColor = 0xF6B2AE;
constexpr uint32_t kBatteryTextColor = 0xFFFFFF;

/**
 * @brief 获取电池概览卡片的填充颜色
 * @param percent 电池百分比
 * @param charging 是否正在充电
 * @return 填充颜色
 */
uint32_t BatteryOverviewFillColor(int percent, bool charging) {
  if (charging) {
    return kBatteryChargingColor;
  }
  if (percent >= 0 && percent < kBatteryLowThresholdPercent) {
    return kBatteryLowColor;
  }
  return kBatteryNormalColor;
}

/**
 * @brief 获取电池概览卡片的剩余区域颜色
 * @param percent 电池百分比
 * @param charging 是否正在充电
 * @return 剩余区域颜色
 */
uint32_t BatteryOverviewRestColor(int percent, bool charging) {
  if (!charging && percent >= 0 &&
      percent < kBatteryLowThresholdPercent) {
    return kBatteryLowRestColor;
  }
  return kBatteryRestColor;
}

/**
 * @brief 将电池额定容量格式化为设置行文本
 * @param capacity_mah 电池额定容量，单位为 mAh
 * @param buffer 输出缓冲区
 * @param buffer_size 输出缓冲区大小
 */
void FormatBatteryCapacity(
    int capacity_mah, char* buffer, size_t buffer_size) {
  if (buffer == nullptr || buffer_size == 0) {
    return;
  }
  std::snprintf(buffer, buffer_size, "%d mAh", capacity_mah);
}

/**
 * @brief 从系统状态缓存读取电池管理状态，必要时主动刷新
 * @param state 设置页状态
 * @param status 电池管理状态输出地址
 * @return 读取到有效状态返回 true，否则返回 false
 */
bool ReadBatteryStatus(SettingsViewState* state, hal::BatteryManagementStatus* status) {
  if (state == nullptr || status == nullptr ||
      state->config.system_status == nullptr) {
    return false;
  }

  state->config.system_status->RefreshBattery();
  if (!state->config.system_status->battery_management_status_valid()) {
    return false;
  }

  *status = state->config.system_status->battery_management_status();
  return true;
}

/**
 * @brief 将分钟数格式化为小时和分钟
 * @param minutes 分钟数
 * @param buffer 输出缓冲区
 * @param buffer_size 输出缓冲区大小
 */
void FormatDuration(int minutes, char* buffer, size_t buffer_size) {
  if (buffer == nullptr || buffer_size == 0) {
    return;
  }

  if (minutes <= 0 || minutes >= 24 * 60 * 30) {
    std::snprintf(buffer, buffer_size, "-- h -- min");
    return;
  }

  std::snprintf(buffer, buffer_size, "%d h %02d min", minutes / 60,
      minutes % 60);
}

/**
 * @brief 格式化电池卡片状态说明文本
 * @param status 电池管理状态
 * @param buffer 输出缓冲区
 * @param buffer_size 输出缓冲区大小
 */
void FormatBatterySummary(
    const hal::BatteryManagementStatus& status, char* buffer, size_t buffer_size) {
  if (buffer == nullptr || buffer_size == 0) {
    return;
  }

  if (!status.pack_present) {
    std::snprintf(buffer, buffer_size, "Battery not detected");
    return;
  }

  if (status.full_charged) {
    std::snprintf(buffer, buffer_size, "Fully charged | Battery %d%%",
        status.charge_percent);
    return;
  }

  if (status.charging) {
    if (status.capabilities.remaining_time) {
      std::snprintf(buffer, buffer_size,
          "Charging time estimate | Battery %d%%", status.charge_percent);
    } else {
      std::snprintf(
          buffer, buffer_size, "Charging | Battery %d%%", status.charge_percent);
    }
    return;
  }

  if (status.capabilities.remaining_time) {
    std::snprintf(buffer, buffer_size, "Available time | Battery %d%%",
        status.charge_percent);
  } else {
    std::snprintf(
        buffer, buffer_size, "Battery %d%%", status.charge_percent);
  }
}

/**
 * @brief 根据电池管理状态选择主卡片时间数据
 * @param status 电池管理状态
 * @return 预计剩余时间，单位为分钟
 */
int BatteryEstimateMinutes(const hal::BatteryManagementStatus& status) {
  return status.charging ? status.time_to_full_min : status.time_to_empty_min;
}

/**
 * @brief 更新电池概览卡片内容
 * @param state 设置页状态
 * @param status 电池管理状态
 */
void UpdateBatteryOverview(
    SettingsViewState* state, const hal::BatteryManagementStatus& status) {
  if (state == nullptr || state->battery_overview_fill == nullptr ||
      state->battery_overview_time_label == nullptr ||
      state->battery_overview_status_label == nullptr) {
    return;
  }

  const int percent = std::clamp(status.charge_percent, 0, 100);
  const int card_width = state->config.width - 2 * kBasicSidePadding;
  const int fill_width = std::max(1, card_width * percent / 100);
  lv_obj_t* card = lv_obj_get_parent(state->battery_overview_fill);
  if (card != nullptr) {
    lv_obj_set_style_bg_color(card,
        lv_color_hex(BatteryOverviewRestColor(percent, status.charging)),
        LV_PART_MAIN);
  }
  lv_obj_set_width(state->battery_overview_fill, fill_width);
  lv_obj_set_style_bg_color(state->battery_overview_fill,
      lv_color_hex(BatteryOverviewFillColor(percent, status.charging)),
      LV_PART_MAIN);

  if (status.capabilities.remaining_time) {
    char time_text[32] = {};
    FormatDuration(
        BatteryEstimateMinutes(status), time_text, sizeof(time_text));
    lv_label_set_text(state->battery_overview_time_label, time_text);
    lv_obj_clear_flag(
        state->battery_overview_time_label, LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_label_set_text(state->battery_overview_time_label, "");
    lv_obj_add_flag(state->battery_overview_time_label, LV_OBJ_FLAG_HIDDEN);
  }

  char summary[80] = {};
  FormatBatterySummary(status, summary, sizeof(summary));
  lv_label_set_text(state->battery_overview_status_label, summary);
  if (state->battery_overview_status_icon_label != nullptr) {
    if (status.charging && !status.full_charged) {
      lv_obj_clear_flag(
          state->battery_overview_status_icon_label, LV_OBJ_FLAG_HIDDEN);
      lv_obj_set_x(state->battery_overview_status_label,
          kBatteryStatusChargingTextLeft);
    } else {
      lv_obj_add_flag(
          state->battery_overview_status_icon_label, LV_OBJ_FLAG_HIDDEN);
      lv_obj_set_x(state->battery_overview_status_label,
          kBatteryCardInnerPadding);
    }
  }
}

/**
 * @brief 更新电池保护页面内容
 * @param state 设置页状态
 * @param status 电池管理状态
 */
void UpdateBatteryProtection(
    SettingsViewState* state, const hal::BatteryManagementStatus& status) {
  if (state == nullptr) {
    return;
  }

  if (state->battery_health_value_label != nullptr) {
    char health[24] = {};
    std::snprintf(health, sizeof(health), "%d%%", status.health_percent);
    lv_label_set_text(state->battery_health_value_label, health);
  }
  if (state->battery_cycle_value_label != nullptr &&
      status.capabilities.cycle_count) {
    char cycles[24] = {};
    std::snprintf(cycles, sizeof(cycles), "%d", status.cycle_count);
    lv_label_set_text(state->battery_cycle_value_label, cycles);
  }
}

/**
 * @brief 刷新省电与电池页面显示内容
 * @param state 设置页状态
 */
void RefreshBatteryPage(SettingsViewState* state) {
  hal::BatteryManagementStatus status;
  if (!ReadBatteryStatus(state, &status)) {
    return;
  }

  UpdateBatteryOverview(state, status);
  UpdateBatteryProtection(state, status);
}

/**
 * @brief 电池页面定时刷新回调
 * @param timer LVGL 定时器对象
 */
void BatteryRefreshTimerCallback(lv_timer_t* timer) {
  if (timer == nullptr) {
    return;
  }

  RefreshBatteryPage(static_cast<SettingsViewState*>(lv_timer_get_user_data(
      timer)));
}

/**
 * @brief 电池详情页面删除时清理定时器和控件引用
 * @param event LVGL 事件对象
 */
void BatteryPageDeleteEventCallback(lv_event_t* event) {
  if (lv_event_get_code(event) != LV_EVENT_DELETE) {
    return;
  }

  auto* state = static_cast<SettingsViewState*>(lv_event_get_user_data(event));
  if (state == nullptr) {
    return;
  }

  if (state->battery_refresh_timer != nullptr) {
    lv_timer_delete(state->battery_refresh_timer);
    state->battery_refresh_timer = nullptr;
  }
  state->battery_overview_fill = nullptr;
  state->battery_overview_time_label = nullptr;
  state->battery_overview_status_icon_label = nullptr;
  state->battery_overview_status_label = nullptr;
  state->battery_health_value_label = nullptr;
  state->battery_cycle_value_label = nullptr;
  state->battery_capacity_value_label = nullptr;
}

/**
 * @brief 电池保护页面删除时清理控件引用
 * @param event LVGL 事件对象
 */
void BatteryProtectionDeleteEventCallback(lv_event_t* event) {
  if (lv_event_get_code(event) != LV_EVENT_DELETE) {
    return;
  }

  auto* state = static_cast<SettingsViewState*>(lv_event_get_user_data(event));
  if (state == nullptr) {
    return;
  }

  state->battery_health_value_label = nullptr;
  state->battery_cycle_value_label = nullptr;
}

/**
 * @brief 解析并校验电池额定容量输入
 * @param text 用户输入的 mAh 数值
 * @param state 设置页状态
 * @param capacity_mah 解析后的容量输出地址
 * @return 输入为当前设备支持的有效容量时返回 true
 */
bool ParseBatteryCapacityEdit(const char* text,
    const SettingsViewState* state, int* capacity_mah) {
  if (text == nullptr || text[0] == '\0' || state == nullptr ||
      state->config.battery_management == nullptr ||
      capacity_mah == nullptr) {
    return false;
  }

  errno = 0;
  char* end = nullptr;
  const long parsed_capacity = std::strtol(text, &end, 10);
  const hal::BatteryCapacityRange range =
      state->config.battery_management->GetBatteryCapacityRange();
  if (errno == ERANGE || end == text || end == nullptr || *end != '\0' ||
      parsed_capacity < range.minimum_mah ||
      parsed_capacity > range.maximum_mah) {
    return false;
  }
  *capacity_mah = static_cast<int>(parsed_capacity);
  return true;
}

/**
 * @brief 校验电池额定容量编辑内容
 * @param text 用户输入的 mAh 数值
 * @param context 设置页状态
 * @return 输入有效返回 true
 */
bool ValidateBatteryCapacityEdit(const char* text, void* context) {
  int capacity_mah = 0;
  return ParseBatteryCapacityEdit(text,
      static_cast<SettingsViewState*>(context), &capacity_mah);
}

/**
 * @brief 保存用户输入的电池额定容量
 * @param text 用户输入的 mAh 数值
 * @param context 设置页状态
 * @return 容量有效且已应用并持久化返回 true，否则返回 false
 */
bool SaveBatteryCapacityEdit(const char* text, void* context) {
  auto* state = static_cast<SettingsViewState*>(context);
  int capacity_mah = 0;
  if (!ParseBatteryCapacityEdit(text, state, &capacity_mah)) {
    LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
        "Invalid battery capacity: value=%s\n",
        text == nullptr ? "(null)" : text);
    return false;
  }

  const app::BatteryPreferences previous_preferences =
      app::GetBatteryPreferences();
  if (capacity_mah == previous_preferences.capacity_mah) {
    return true;
  }
  if (!state->config.battery_management->SetBatteryCapacityMah(
          capacity_mah)) {
    LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
        "Apply battery capacity failed: capacity=%d mAh\n", capacity_mah);
    return false;
  }

  app::BatteryPreferences updated_preferences = previous_preferences;
  updated_preferences.capacity_mah = capacity_mah;
  if (!app::UpdateBatteryPreferences(updated_preferences)) {
    const bool hardware_rolled_back =
        state->config.battery_management->SetBatteryCapacityMah(
            previous_preferences.capacity_mah);
    const bool storage_rolled_back =
        app::UpdateBatteryPreferences(previous_preferences);
    LogMessage(LogLevel::kError, __FILE__, __LINE__,
        "Persist battery capacity failed: capacity=%d mAh, "
        "hardware_rollback=%s, storage_rollback=%s\n",
        capacity_mah, hardware_rolled_back ? "success" : "failed",
        storage_rolled_back ? "success" : "failed");
    return false;
  }

  if (state->battery_capacity_value_label != nullptr) {
    char capacity_text[24] = {};
    FormatBatteryCapacity(capacity_mah, capacity_text, sizeof(capacity_text));
    lv_label_set_text(state->battery_capacity_value_label, capacity_text);
  }
  if (state->config.system_status != nullptr) {
    state->config.system_status->RefreshBattery();
  }
  return true;
}

/**
 * @brief 显示电池额定容量编辑页
 * @param state 设置页状态
 * @return 显示成功返回 true，否则返回 false
 */
bool ShowBatteryCapacityEditPage(SettingsViewState* state) {
  if (state == nullptr || state->root == nullptr ||
      state->config.battery_management == nullptr) {
    return false;
  }

  const app::BatteryPreferences preferences = app::GetBatteryPreferences();
  const hal::BatteryCapacityRange range =
      state->config.battery_management->GetBatteryCapacityRange();
  char capacity_text[16] = {};
  std::snprintf(
      capacity_text, sizeof(capacity_text), "%d", preferences.capacity_mah);
  char help_text[80] = {};
  std::snprintf(help_text, sizeof(help_text),
      "Enter the rated battery capacity in mAh (%d-%d).",
      range.minimum_mah, range.maximum_mah);

  SettingsTextEditPageConfig config;
  config.parent = state->root;
  config.width = state->config.width;
  config.height = state->config.height;
  config.title = "Edit battery capacity";
  config.initial_text = capacity_text;
  config.help_text = help_text;
  config.accepted_chars = kBatteryCapacityAcceptedChars;
  config.maximum_length = kBatteryCapacityMaximumLength;
  config.keyboard_mode = LV_KEYBOARD_MODE_USER_3;
  config.save_callback = SaveBatteryCapacityEdit;
  config.validation_callback = ValidateBatteryCapacityEdit;
  config.callback_context = state;
  return ShowSettingsTextEditPage(&state->text_edit_page, config);
}

/**
 * @brief 处理电池额定容量设置行点击事件
 * @param event LVGL 事件对象
 */
void BatteryCapacityClickedEventCallback(lv_event_t* event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
    return;
  }
  ShowBatteryCapacityEditPage(
      static_cast<SettingsViewState*>(lv_event_get_user_data(event)));
}

/**
 * @brief 创建电池保护信息行
 * @param body 内容容器
 * @param title 标题文本
 * @param value 文本值
 * @param y 顶部坐标
 * @param width 页面宽度
 * @param state 设置页状态
 * @param value_label 输出值标签对象
 * @return 创建成功返回 true，否则返回 false
 */
bool CreateBatteryInfoRow(lv_obj_t* body, const char* title, const char* value,
    int y, int width, SettingsViewState* state, lv_obj_t** value_label) {
  if (!CreateArrowRow(body, title, value, y, width, nullptr, state)) {
    return false;
  }

  lv_obj_t* row = lv_obj_get_child(body, lv_obj_get_child_count(body) - 1);
  if (row == nullptr || lv_obj_get_child_count(row) < 2) {
    return true;
  }

  *value_label = lv_obj_get_child(row, 1);
  return true;
}

/**
 * @brief 构建电池保护详情页面
 * @param body 内容容器
 * @param state 设置页状态
 * @return 创建成功返回 true，否则返回 false
 */
bool BuildBatteryProtectionPage(lv_obj_t* body, SettingsViewState* state) {
  if (body == nullptr || state == nullptr) {
    return false;
  }

  lv_obj_add_event_cb(
      body, BatteryProtectionDeleteEventCallback, LV_EVENT_DELETE, state);

  hal::BatteryManagementStatus status;
  if (!ReadBatteryStatus(state, &status)) {
    status = hal::BatteryManagementStatus();
    status.health_percent = 100;
  }

  char health[24] = {};
  std::snprintf(health, sizeof(health), "%d%%", status.health_percent);

  const int width = state->config.width;
  int y = 0;
  if (!CreateSectionLabel(body, "Battery information", y, width)) {
    return false;
  }
  y += kBasicSectionHeight;
  if (!CreateBatteryInfoRow(body, "Battery health", health, y, width, state,
          &state->battery_health_value_label)) {
    return false;
  }
  if (!status.capabilities.cycle_count) {
    return true;
  }
  y += kBasicRowHeight;
  char cycles[24] = {};
  std::snprintf(cycles, sizeof(cycles), "%d", status.cycle_count);
  return CreateBatteryInfoRow(body, "Charge cycles", cycles, y, width, state,
      &state->battery_cycle_value_label);
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
 * @brief 创建电池信息大卡片背景
 * @param body 内容容器
 * @param state 设置页状态
 * @param y 顶部坐标
 * @return 创建成功返回卡片控件，否则返回 nullptr
 */
lv_obj_t* CreateBatteryCardBackground(
    lv_obj_t* body, SettingsViewState* state, int y) {
  const int width = state->config.width - 2 * kBasicSidePadding;
  lv_obj_t* card = CreateBox(body, width, kBatteryCardHeight, kBatteryRestColor,
      LV_OPA_COVER, kBatteryCardRadius);
  if (card == nullptr) {
    return nullptr;
  }
  lv_obj_set_pos(card, kBasicSidePadding, y);
  lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_clip_corner(card, true, LV_PART_MAIN);
  return card;
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
  hal::BatteryManagementStatus status;
  if (!ReadBatteryStatus(state, &status)) {
    status = hal::BatteryManagementStatus();
    status.pack_present = true;
    status.charge_percent = 47;
  }

  lv_obj_t* card = CreateBatteryCardBackground(body, state, y);
  if (card == nullptr) {
    return false;
  }

  const int card_width = state->config.width - 2 * kBasicSidePadding;
  const int percent = std::clamp(status.charge_percent, 0, 100);
  lv_obj_set_style_bg_color(card,
      lv_color_hex(BatteryOverviewRestColor(percent, status.charging)),
      LV_PART_MAIN);

  lv_obj_t* fill = CreateBox(card, card_width, kBatteryCardHeight,
      BatteryOverviewFillColor(percent, status.charging), LV_OPA_COVER,
      kBatteryFillRadius);
  if (fill == nullptr) {
    return false;
  }
  lv_obj_set_pos(fill, 0, 0);
  lv_obj_remove_flag(fill, LV_OBJ_FLAG_SCROLLABLE);
  state->battery_overview_fill = fill;

  lv_obj_t* time_label = CreateLabel(card, "-- h -- min",
      lv_color_hex(kBatteryTextColor), Font64());
  if (time_label == nullptr) {
    return false;
  }
  lv_obj_set_style_text_font(time_label, Font64(), LV_PART_MAIN);
  lv_obj_set_pos(time_label, kBatteryCardInnerPadding, kBatteryMainTextTop);
  state->battery_overview_time_label = time_label;

  lv_obj_t* status_icon_label = CreateLabel(card, icon::kBolt,
      lv_color_hex(kBatteryTextColor), MaterialIconFont32());
  if (status_icon_label == nullptr) {
    return false;
  }
  lv_obj_set_pos(status_icon_label, kBatteryStatusIconLeft,
      kBatteryStatusIconTop);
  state->battery_overview_status_icon_label = status_icon_label;

  lv_obj_t* status_label = CreateLabel(card, "Available time | Battery --%",
      lv_color_hex(kBatteryTextColor), Font24());
  if (status_label == nullptr) {
    return false;
  }
  lv_obj_set_pos(status_label, kBatteryCardInnerPadding, kBatteryStatusTextTop);
  state->battery_overview_status_label = status_label;

  UpdateBatteryOverview(state, status);
  return true;
}

/**
 * @brief 构建省电与电池设置内容
 * @param body 内容容器
 * @param state 设置页状态
 * @return 创建成功返回 true，否则返回 false
 */
bool BuildPowerBatteryContent(lv_obj_t* body, SettingsViewState* state) {
  if (body == nullptr || state == nullptr) {
    return false;
  }

  lv_obj_add_event_cb(body, BatteryPageDeleteEventCallback, LV_EVENT_DELETE,
      state);

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
  if (!CreateArrowRow(body, "Battery protection", "", y,
          state->config.width, BatteryProtectionClickedEventCallback, state)) {
    return false;
  }
  y += kBasicRowHeight;
  const app::BatteryPreferences battery_preferences =
      app::GetBatteryPreferences();
  char capacity_text[24] = {};
  FormatBatteryCapacity(battery_preferences.capacity_mah, capacity_text,
      sizeof(capacity_text));
  if (!CreateArrowRow(body, "Battery capacity", capacity_text, y,
          state->config.width, BatteryCapacityClickedEventCallback, state)) {
    return false;
  }
  lv_obj_t* capacity_row =
      lv_obj_get_child(body, lv_obj_get_child_count(body) - 1);
  if (capacity_row == nullptr || lv_obj_get_child_count(capacity_row) < 2) {
    return false;
  }
  state->battery_capacity_value_label = lv_obj_get_child(capacity_row, 1);

  if (state->battery_refresh_timer != nullptr) {
    lv_timer_delete(state->battery_refresh_timer);
  }
  state->battery_refresh_timer = lv_timer_create(
      BatteryRefreshTimerCallback, kBatteryRefreshPeriodMs, state);
  return state->battery_refresh_timer != nullptr;
}

}  // namespace

bool ShowPowerBatteryPage(SettingsViewState* state) {
  return ShowBasicPage(state, "Power Saving & Battery",
      BuildPowerBatteryContent);
}

}  // namespace lilygo_box::ui
