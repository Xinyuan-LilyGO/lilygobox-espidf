/*
 * @Description: Settings lock screen page
 * @Author: LILYGO_L
 * @Date: 2026-05-23 00:00:00
 * @LastEditTime: 2026-07-04 18:18:46
 * @License: GPL 3.0
 */
#include "ui/views/settings/settings_basic_view_common.h"

#include <cstdio>

#include "app/storage/display_storage.h"
#include "ui/font/material_symbols_assets.h"
#include "ui/input/press_cancel.h"
#include "ui/widgets/prompt/prompt_select_sheet.h"

namespace lilygo_box::ui {
namespace {

constexpr PromptSelectSheetOption kAutoLockOptions[] = {
    {15, "15 seconds"},
    {30, "30 seconds"},
    {60, "1 minute"},
    {2 * 60, "2 minutes"},
    {5 * 60, "5 minutes"},
    {10 * 60, "10 minutes"},
};
constexpr size_t kAutoLockOptionCount =
    sizeof(kAutoLockOptions) / sizeof(kAutoLockOptions[0]);
constexpr int kAutoLockSheetSideMargin = 34;
constexpr int kAutoLockSheetBottomMargin = 32;
constexpr int kAutoLockSheetRadius = 48;
constexpr int kAutoLockSheetInnerPadding = 32;
constexpr int kAutoLockSheetHeight = 755;
constexpr int kAutoLockOptionTop = 155;
constexpr int kAutoLockOptionHeight = 78;
constexpr uint32_t kAutoLockSelectedColor =
    theme::LightNeutralTheme().action_container;

/**
 * @brief 格式化自动锁屏等待时间
 * @param seconds 等待时间，单位为秒
 * @param buffer 输出缓冲区
 * @param size 输出缓冲区大小
 */
void FormatAutoLockValue(int seconds, char* buffer, size_t size) {
  if (buffer == nullptr || size == 0) {
    return;
  }
  if (seconds < 60) {
    std::snprintf(buffer, size, "%d seconds", seconds);
    return;
  }
  const int minutes = seconds / 60;
  std::snprintf(buffer, size, "%d %s", minutes,
      minutes == 1 ? "minute" : "minutes");
}

/**
 * @brief 处理自动锁屏选项选中回调
 * @param context 设置页状态
 * @param value 自动锁屏等待时间，单位为秒
 */
void AutoLockSelectedCallback(void* context, int value) {
  auto* state = static_cast<SettingsViewState*>(context);
  if (state == nullptr) {
    return;
  }

  state->auto_lock_seconds = value;
  if (state->auto_lock_value_label != nullptr) {
    char text[24] = {};
    FormatAutoLockValue(value, text, sizeof(text));
    lv_label_set_text(state->auto_lock_value_label, text);
  }

  app::DisplayPreferences preferences = app::GetDisplayPreferences();
  preferences.lock_timeout_seconds = value;
  app::UpdateDisplayPreferences(preferences);
}

/**
 * @brief 打开自动锁屏选择弹窗
 * @param state 设置页状态
 * @return 打开成功返回 true，否则返回 false
 */
bool ShowAutoLockSheet(SettingsViewState* state) {
  if (state == nullptr || state->root == nullptr) {
    return false;
  }

  PromptSelectSheetConfig config;
  config.screen_width = state->config.width;
  config.screen_height = state->config.height;
  config.sheet_width = state->config.width - 2 * kAutoLockSheetSideMargin;
  config.sheet_height = kAutoLockSheetHeight;
  config.side_margin = kAutoLockSheetSideMargin;
  config.bottom_margin = kAutoLockSheetBottomMargin;
  config.sheet_radius = kAutoLockSheetRadius;
  config.inner_padding = kAutoLockSheetInnerPadding;
  config.option_top = kAutoLockOptionTop;
  config.option_height = kAutoLockOptionHeight;
  config.button_height = kWifiConnectButtonHeight;
  config.button_radius = 24;
  config.selected_color = kAutoLockSelectedColor;
  config.primary_text_color = kPrimaryTextColor;
  config.secondary_text_color = kSecondaryTextColor;
  config.selected_text_color = kBasicBlueColor;
  config.cancel_background_color = kWifiConnectSecondaryColor;
  config.pressed_color = kWifiConnectSecondaryPressedColor;
  config.pressed_opacity = kPressedOpacity;
  config.animation_ms = kDetailSlideAnimationMs;
  config.title = "Auto lock";
  config.message =
      "Long display of still content may cause uneven screen color.";
  config.cancel_text = "Cancel";
  config.check_icon = icon::kCheck;
  config.options = kAutoLockOptions;
  config.option_count = kAutoLockOptionCount;
  config.selected_value = state->auto_lock_seconds;
  config.title_font = Font32();
  config.message_font = Font24();
  config.option_font = Font28();
  config.cancel_font = Font28();
  config.icon_font = MaterialIconFont32();
  config.state = &state->auto_lock_select_sheet;
  config.callback = AutoLockSelectedCallback;
  config.callback_context = state;
  return ShowPromptSelectSheet(state->root, config);
}

/**
 * @brief 处理自动锁屏行点击
 * @param event LVGL 事件对象
 */
void AutoLockRowClickedEventCallback(lv_event_t* event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
    return;
  }

  ShowAutoLockSheet(
      static_cast<SettingsViewState*>(lv_event_get_user_data(event)));
  lv_event_stop_bubbling(event);
  lv_event_stop_processing(event);
}

/**
 * @brief 创建自动锁屏设置行
 * @param parent 父对象
 * @param state 设置页状态
 * @param y 顶部坐标
 * @param width 页面宽度
 * @return 创建成功返回 true，否则返回 false
 */
bool CreateAutoLockRow(
    lv_obj_t* parent, SettingsViewState* state, int y, int width) {
  lv_obj_t* row = lv_obj_create(parent);
  if (row == nullptr) {
    return false;
  }
  lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_flag(row, LV_OBJ_FLAG_GESTURE_BUBBLE);
  lv_obj_set_size(row, width, kBasicRowHeight);
  lv_obj_set_pos(row, 0, y);
  lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_set_style_bg_color(row, lv_color_hex(kPressedColor),
      LV_STATE_PRESSED);
  lv_obj_set_style_bg_opa(row, kPressedOpacity, LV_STATE_PRESSED);
  lv_obj_set_style_border_width(row, 0, LV_PART_MAIN);
  lv_obj_set_style_radius(row, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(row, 0, LV_PART_MAIN);
  if (!AddPressCancelOnLeave(row)) {
    return false;
  }
  lv_obj_add_event_cb(row, AutoLockRowClickedEventCallback,
      LV_EVENT_CLICKED, state);

  lv_obj_t* title = CreateLabel(row, "Auto lock",
      lv_color_hex(kPrimaryTextColor), Font28());
  if (title == nullptr) {
    return false;
  }
  lv_obj_set_width(title, width - 2 * kBasicSidePadding - 190);
  lv_label_set_long_mode(title, LV_LABEL_LONG_DOT);
  lv_obj_align(title, LV_ALIGN_LEFT_MID, kBasicSidePadding, 0);

  char value[24] = {};
  FormatAutoLockValue(state->auto_lock_seconds, value, sizeof(value));
  lv_obj_t* value_label = CreateLabel(row, value,
      lv_color_hex(kSecondaryTextColor), Font24());
  if (value_label == nullptr) {
    return false;
  }
  state->auto_lock_value_label = value_label;
  lv_obj_set_width(value_label, 184);
  lv_obj_set_style_text_align(value_label, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);
  lv_obj_align(value_label, LV_ALIGN_RIGHT_MID,
      -(kBasicSidePadding + 40), 0);

  lv_obj_t* arrow = CreateLabel(row, icon::kChevronRight,
      lv_color_hex(kSecondaryTextColor), MaterialIconFont32());
  if (arrow == nullptr) {
    return false;
  }
  lv_obj_align(arrow, LV_ALIGN_RIGHT_MID, -kBasicSidePadding, 0);
  return true;
}

/**
 * @brief 构建锁屏设置内容
 * @param body 内容容器
 * @param state 设置页状态
 * @return 创建成功返回 true，否则返回 false
 */
bool BuildLockScreenContent(lv_obj_t* body, SettingsViewState* state) {
  const int width = state->config.width;
  state->auto_lock_value_label = nullptr;
  if (!CreateSectionLabel(body, "Lock screen settings", 0, width)) {
    return false;
  }
  return CreateAutoLockRow(body, state, kBasicSectionHeight, width);
}

}  // namespace

/**
 * @brief 从设置主页打开锁屏详情页
 * @param state 设置页状态
 * @return 打开成功返回 true，否则返回 false
 */
bool ShowLockScreenPage(SettingsViewState* state) {
  return ShowBasicPage(state, "Lock Screen", BuildLockScreenContent);
}

}  // namespace lilygo_box::ui
