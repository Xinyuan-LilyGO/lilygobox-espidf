/*
 * @Description: Settings display brightness page
 * @Author: LILYGO_L
 * @Date: 2026-05-23 00:00:00
 * @LastEditTime: 2026-07-04 18:18:56
 * @License: GPL 3.0
 */
#include "ui/views/settings/settings_basic_view_common.h"

#include <cstdint>
#include <cstdio>

#include "app/storage/display_storage.h"
#include "hal/lvgl_port.h"
#include "hal/providers/haptic_provider.h"
#include "hal/providers/screen_provider.h"
#include "ui/font/material_symbols_assets.h"
#include "ui/input/press_cancel.h"
#include "ui/widgets/prompt/prompt_select_sheet.h"

namespace lilygo_box::ui {
namespace {

hal::LvglPort* g_lvgl_port = nullptr;
int g_pending_rotation_angle = -1;

void ApplyPendingRotation(void* /*ctx*/) {
  if (g_lvgl_port != nullptr && g_pending_rotation_angle >= 0) {
    g_lvgl_port->SetDisplayRotation(g_pending_rotation_angle);
    g_pending_rotation_angle = -1;
  }
}

// 屏幕旋转角度选项
constexpr PromptSelectSheetOption kScreenRotationOptions[] = {
    {0, "0 deg"},
    {90, "90 deg"},
    {180, "180 deg"},
    {270, "270 deg"},
};
constexpr size_t kScreenRotationOptionCount =
    sizeof(kScreenRotationOptions) / sizeof(kScreenRotationOptions[0]);
constexpr int kScreenRotationSheetSideMargin = 34;
constexpr int kScreenRotationSheetBottomMargin = 32;
constexpr int kScreenRotationSheetRadius = 48;
constexpr int kScreenRotationSheetInnerPadding = 32;
constexpr int kScreenRotationSheetHeight = 615;
constexpr int kScreenRotationOptionTop = 155;
constexpr int kScreenRotationOptionHeight = 78;
constexpr uint32_t kScreenRotationSelectedColor =
    theme::LightNeutralTheme().action_container;

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
 * @brief 异步保存显示亮度设置偏好
 * @param state 设置页状态
 */
void SaveBrightnessPreferencesAsync(SettingsViewState* state) {
  if (state == nullptr) {
    return;
  }

  app::DisplayPreferences preferences = app::GetDisplayPreferences();
  preferences.brightness_percent = state->display_brightness_percent;
  app::UpdateDisplayPreferences(preferences);
}

/**
 * @brief 保存屏幕亮度滑动条值并应用到硬件
 * @param event LVGL 事件对象
 */
void BrightnessSliderChangedEventCallback(lv_event_t* event) {
  auto* state = static_cast<SettingsViewState*>(lv_event_get_user_data(event));
  if (state != nullptr) {
    const int brightness_percent = SliderPercentFromEvent(event);
    state->display_brightness_percent = brightness_percent;
    PlaySettingsHapticPreview(state);
    if (state->config.screen != nullptr) {
      state->config.screen->SetScreenBrightnessPercent(brightness_percent);
    }
  }
}

/**
 * @brief 松开亮度滑动条时保存最终值
 * @param event LVGL 事件对象
 */
void BrightnessSliderReleasedEventCallback(lv_event_t* event) {
  SaveBrightnessPreferencesAsync(
      static_cast<SettingsViewState*>(lv_event_get_user_data(event)));
}

/**
 * @brief 格式化屏幕旋转角度显示文本
 * @param angle 旋转角度
 * @param buffer 输出缓冲区
 * @param size 输出缓冲区大小
 */
void FormatScreenRotationValue(int angle, char* buffer, size_t size) {
  if (buffer == nullptr || size == 0) {
    return;
  }
  std::snprintf(buffer, size, "%d deg", angle);
}

/**
 * @brief 处理屏幕旋转选项选中回调
 * @param context 设置页状态
 * @param value 旋转角度
 */
void ScreenRotationSelectedCallback(void* context, int value) {
  auto* state = static_cast<SettingsViewState*>(context);
  if (state == nullptr) {
    return;
  }

  state->screen_rotation_angle = value;
  if (state->screen_rotation_value_label != nullptr) {
    char text[16] = {};
    FormatScreenRotationValue(value, text, sizeof(text));
    lv_label_set_text(state->screen_rotation_value_label, text);
  }
  if (g_lvgl_port != nullptr) {
    // 推迟到下一个 LVGL tick 执行旋转，避免在 prompt sheet 回调链中修改 display
    g_pending_rotation_angle = value;
    lv_async_call(ApplyPendingRotation, nullptr);
  }
}

/**
 * @brief 打开屏幕旋转选择弹窗
 * @param state 设置页状态
 * @return 打开成功返回 true，否则返回 false
 */
bool ShowScreenRotationSheet(SettingsViewState* state) {
  if (state == nullptr || state->root == nullptr) {
    return false;
  }

  PromptSelectSheetConfig config;
  config.screen_width = state->config.width;
  config.screen_height = state->config.height;
  config.sheet_width = state->config.width - 2 * kScreenRotationSheetSideMargin;
  config.sheet_height = kScreenRotationSheetHeight;
  config.side_margin = kScreenRotationSheetSideMargin;
  config.bottom_margin = kScreenRotationSheetBottomMargin;
  config.sheet_radius = kScreenRotationSheetRadius;
  config.inner_padding = kScreenRotationSheetInnerPadding;
  config.option_top = kScreenRotationOptionTop;
  config.option_height = kScreenRotationOptionHeight;
  config.button_height = kWifiConnectButtonHeight;
  config.button_radius = 24;
  config.selected_color = kScreenRotationSelectedColor;
  config.primary_text_color = kPrimaryTextColor;
  config.secondary_text_color = kSecondaryTextColor;
  config.selected_text_color = kBasicBlueColor;
  config.cancel_background_color = kWifiConnectSecondaryColor;
  config.pressed_color = kWifiConnectSecondaryPressedColor;
  config.pressed_opacity = kPressedOpacity;
  config.animation_ms = kDetailSlideAnimationMs;
  config.title = "Screen rotation";
  config.message =
      "Changing the rotation angle may affect some application layouts.";
  config.cancel_text = "Cancel";
  config.check_icon = icon::kCheck;
  config.options = kScreenRotationOptions;
  config.option_count = kScreenRotationOptionCount;
  config.selected_value = state->screen_rotation_angle;
  config.title_font = Font32();
  config.message_font = Font24();
  config.option_font = Font28();
  config.cancel_font = Font28();
  config.icon_font = MaterialIconFont32();
  config.state = &state->screen_rotation_select_sheet;
  config.callback = ScreenRotationSelectedCallback;
  config.callback_context = state;
  return ShowPromptSelectSheet(state->root, config);
}

/**
 * @brief 处理屏幕旋转行点击
 * @param event LVGL 事件对象
 */
void ScreenRotationRowClickedEventCallback(lv_event_t* event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
    return;
  }

  ShowScreenRotationSheet(
      static_cast<SettingsViewState*>(lv_event_get_user_data(event)));
  lv_event_stop_bubbling(event);
  lv_event_stop_processing(event);
}

/**
 * @brief 创建屏幕旋转设置行
 * @param parent 父对象
 * @param state 设置页状态
 * @param y 顶部坐标
 * @param width 页面宽度
 * @return 创建成功返回 true，否则返回 false
 */
bool CreateScreenRotationRow(
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
  lv_obj_add_event_cb(row, ScreenRotationRowClickedEventCallback,
      LV_EVENT_CLICKED, state);

  lv_obj_t* title = CreateLabel(row, "Screen rotation",
      lv_color_hex(kPrimaryTextColor), Font28());
  if (title == nullptr) {
    return false;
  }
  lv_obj_set_width(title, width - 2 * kBasicSidePadding - 190);
  lv_label_set_long_mode(title, LV_LABEL_LONG_DOT);
  lv_obj_align(title, LV_ALIGN_LEFT_MID, kBasicSidePadding, 0);

  char value[16] = {};
  FormatScreenRotationValue(state->screen_rotation_angle, value, sizeof(value));
  lv_obj_t* value_label = CreateLabel(row, value,
      lv_color_hex(kSecondaryTextColor), Font24());
  if (value_label == nullptr) {
    return false;
  }
  state->screen_rotation_value_label = value_label;
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
 * @brief 构建显示与亮度设置内容
 * @param body 内容容器
 * @param state 设置页状态
 * @return 创建成功返回 true，否则返回 false
 */
bool BuildDisplayBrightnessContent(lv_obj_t* body, SettingsViewState* state) {
  const int width = state->config.width;
  int y = 0;
  if (!CreateSectionLabel(body, "Brightness", y, width)) {
    return false;
  }
  y += kBasicSectionHeight;
  if (!CreateSliderRow(body, icon::kSunny, "Screen brightness",
          state->display_brightness_percent, y,
          width, BrightnessSliderChangedEventCallback, state)) {
    return false;
  }

  lv_obj_t* brightness_slider =
      lv_obj_get_child(body, lv_obj_get_child_count(body) - 1);
  if (brightness_slider != nullptr) {
    lv_obj_add_event_cb(brightness_slider, BrightnessSliderReleasedEventCallback,
        LV_EVENT_RELEASED, state);
    lv_obj_add_event_cb(brightness_slider, BrightnessSliderReleasedEventCallback,
        LV_EVENT_PRESS_LOST, state);
  }

  y += 118;
  if (!CreateBasicDivider(body, y, width)) {
    return false;
  }
  y += 32;

  state->screen_rotation_value_label = nullptr;
  if (!CreateSectionLabel(body, "Screen", y, width)) {
    return false;
  }
  y += kBasicSectionHeight;
  return CreateScreenRotationRow(body, state, y, width);
}

}  // namespace

/**
 * @brief 从设置主页打开显示与亮度详情页
 * @param state 设置页状态
 * @return 打开成功返回 true，否则返回 false
 */
bool ShowDisplayBrightnessPage(SettingsViewState* state) {
  return ShowBasicPage(state, "Display & Brightness",
      BuildDisplayBrightnessContent);
}

void SetLvglPortForRotation(hal::LvglPort* port) {
  g_lvgl_port = port;
}

}  // namespace lilygo_box::ui
