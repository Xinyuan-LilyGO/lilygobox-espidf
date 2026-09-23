/*
 * @Description: 设置开发者选项页面
 * @Author: LILYGO_L
 * @Date: 2026-09-23 00:00:00
 * @LastEditTime: 2026-09-23 17:49:58
 * @License: GPL 3.0
 */
#include "app/storage/developer_storage.h"
#include "ui/input/press_cancel.h"
#include "ui/resources/fonts/icon_assets.h"
#include "ui/views/settings/settings_basic_view_common.h"
#include "ui/widgets/prompt/prompt_select_sheet.h"

namespace lilygo_box::ui {
namespace {

struct LogSourceConfig {
  const char* title;
  const char* source;
  const char* sheet_title;
  const char* message;
  PromptSelectSheetOption options[5];
};

constexpr LogSourceConfig kLogSources[] = {
    {"Application logs", "lilygo_box", "Application log level",
        "lilygo_box serial logs.\nNone turns them off.",
        {{static_cast<int>(LogLevel::kDebug), "Debug"},
         {static_cast<int>(LogLevel::kInfo), "Info"},
         {static_cast<int>(LogLevel::kWarning), "Warning"},
         {static_cast<int>(LogLevel::kError), "Error"},
         {static_cast<int>(LogLevel::kNone), "None"}}},
    {"Bus driver logs", "cpp_bus_driver", "Bus driver log level",
        "cpp_bus_driver serial logs.\nNone turns them off.",
        {{static_cast<int>(cpp_bus_driver::Logger::LogLevel::kDebug), "Debug"},
         {static_cast<int>(cpp_bus_driver::Logger::LogLevel::kInfo), "Info"},
         {static_cast<int>(cpp_bus_driver::Logger::LogLevel::kWarning), "Warning"},
         {static_cast<int>(cpp_bus_driver::Logger::LogLevel::kError), "Error"},
         {static_cast<int>(cpp_bus_driver::Logger::LogLevel::kNone), "None"}}},
    {"Device driver logs", "lilygo_device_driver", "Device driver log level",
        "lilygo_device_driver serial logs.\nNone turns them off.",
        {{static_cast<int>(lilygo_device_driver::LogLevel::kDebug), "Debug"},
         {static_cast<int>(lilygo_device_driver::LogLevel::kInfo), "Info"},
         {static_cast<int>(lilygo_device_driver::LogLevel::kWarning), "Warning"},
         {static_cast<int>(lilygo_device_driver::LogLevel::kError), "Error"},
         {static_cast<int>(lilygo_device_driver::LogLevel::kNone), "None"}}},
};
constexpr size_t kLogSourceCount = sizeof(kLogSources) / sizeof(kLogSources[0]);
static_assert(kLogSourceCount == static_cast<size_t>(SettingsLogSource::kCount));
constexpr size_t kLogLevelOptionCount =
    sizeof(kLogSources[0].options) / sizeof(kLogSources[0].options[0]);
constexpr int kLogLevelRowHeight = 104;
constexpr int kLogLevelValueWidth = 110;
constexpr int kLogLevelSheetSideMargin = 34;
constexpr int kLogLevelSheetBottomMargin = 32;
constexpr int kLogLevelSheetRadius = 48;
constexpr int kLogLevelSheetInnerPadding = 32;
constexpr int kLogLevelOptionTop = 155;
constexpr int kLogLevelOptionHeight = 78;
constexpr int kLogLevelSheetHeight =
    kLogLevelOptionTop +
    static_cast<int>(kLogLevelOptionCount) * kLogLevelOptionHeight +
    2 * kLogLevelSheetInnerPadding + kWifiConnectButtonHeight;

/**
 * @brief 读取指定模块当前日志等级
 * @param source 日志来源
 * @return 当前等级对应的选项值
 */
int CurrentLogLevel(SettingsLogSource source) {
  switch (source) {
    case SettingsLogSource::kApp:
      return static_cast<int>(GetMinimumLogLevel());
    case SettingsLogSource::kCppBusDriver:
      return static_cast<int>(cpp_bus_driver::Logger::GetMinimumLogLevel());
    case SettingsLogSource::kLilygoDeviceDriver:
      return static_cast<int>(lilygo_device_driver::GetMinimumLogLevel());
    default:
      return -1;
  }
}

/**
 * @brief 获取指定模块当前日志等级的显示名称
 * @param source 日志来源
 * @return 日志等级名称
 */
const char* LogLevelText(SettingsLogSource source) {
  const int value = CurrentLogLevel(source);
  for (const auto& option : kLogSources[static_cast<size_t>(source)].options) {
    if (option.value == value) {
      return option.text;
    }
  }
  return "Unknown";
}

/**
 * @brief 处理串口日志等级选项选中回调
 * @param context 日志设置行上下文
 * @param value 日志等级
 */
void LogLevelSelectedCallback(void* context, int value) {
  auto* action = static_cast<SettingsLogLevelAction*>(context);
  if (action == nullptr || action->state == nullptr) {
    return;
  }

  const auto& config = kLogSources[static_cast<size_t>(action->source)];
  bool valid = false;
  for (const auto& option : config.options) {
    if (option.value == value) {
      valid = true;
      break;
    }
  }
  if (!valid) {
    return;
  }
  app::DeveloperPreferences preferences = app::GetDeveloperPreferences();
  switch (action->source) {
    case SettingsLogSource::kApp:
      preferences.log_level = static_cast<LogLevel>(value);
      break;
    case SettingsLogSource::kCppBusDriver:
      preferences.cpp_bus_driver_log_level =
          static_cast<cpp_bus_driver::Logger::LogLevel>(value);
      break;
    case SettingsLogSource::kLilygoDeviceDriver:
      preferences.lilygo_device_driver_log_level =
          static_cast<lilygo_device_driver::LogLevel>(value);
      break;
    default:
      return;
  }
  app::UpdateDeveloperPreferences(preferences);
  if (action->value_label != nullptr) {
    lv_label_set_text(action->value_label, LogLevelText(action->source));
  }
}

/**
 * @brief 打开串口日志等级选择弹窗
 * @param action 日志设置行上下文
 * @return 打开成功返回 true，否则返回 false
 */
bool ShowLogLevelSheet(SettingsLogLevelAction* action) {
  if (action == nullptr || action->state == nullptr ||
      action->state->settings_nested_page == nullptr) {
    return false;
  }

  auto* state = action->state;
  const auto& source_config = kLogSources[static_cast<size_t>(action->source)];
  PromptSelectSheetConfig config;
  config.screen_width = state->config.width;
  config.screen_height = state->config.height;
  config.sheet_width = state->config.width - 2 * kLogLevelSheetSideMargin;
  config.sheet_height = kLogLevelSheetHeight;
  config.side_margin = kLogLevelSheetSideMargin;
  config.bottom_margin = kLogLevelSheetBottomMargin;
  config.sheet_radius = kLogLevelSheetRadius;
  config.inner_padding = kLogLevelSheetInnerPadding;
  config.option_top = kLogLevelOptionTop;
  config.option_height = kLogLevelOptionHeight;
  config.button_height = kWifiConnectButtonHeight;
  config.button_radius = 24;
  config.selected_color = theme::FixedColors().action_container;
  config.primary_text_color = SettingsThemeColors().on_surface;
  config.secondary_text_color = SettingsThemeColors().on_surface_variant;
  config.selected_text_color = theme::FixedColors().action;
  config.cancel_background_color = SettingsThemeColors().button_secondary;
  config.pressed_color = SettingsThemeColors().button_secondary_pressed;
  config.pressed_opacity = kPressedOpacity;
  config.animation_ms = kDetailSlideAnimationMs;
  config.title = source_config.sheet_title;
  config.message = source_config.message;
  config.cancel_text = "Cancel";
  config.check_icon = icon::kCheck;
  config.options = source_config.options;
  config.option_count = kLogLevelOptionCount;
  config.selected_value = CurrentLogLevel(action->source);
  config.title_font = Font32();
  config.message_font = Font24();
  config.option_font = Font28();
  config.cancel_font = Font28();
  config.icon_font = MaterialIconFont32();
  config.state = &state->log_level_select_sheet;
  config.callback = LogLevelSelectedCallback;
  config.callback_context = action;
  return ShowPromptSelectSheet(state->settings_nested_page, config);
}

/**
 * @brief 处理串口日志等级行点击
 * @param event LVGL 事件对象
 */
void LogLevelRowClickedEventCallback(lv_event_t* event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
    return;
  }

  ShowLogLevelSheet(
      static_cast<SettingsLogLevelAction*>(lv_event_get_user_data(event)));
  lv_event_stop_bubbling(event);
  lv_event_stop_processing(event);
}

/**
 * @brief 创建串口日志等级设置行
 * @param parent 父对象
 * @param action 日志设置行上下文
 * @param y 顶部坐标
 * @param width 页面宽度
 * @return 创建成功返回 true，否则返回 false
 */
bool CreateLogLevelRow(
    lv_obj_t* parent, SettingsLogLevelAction* action, int y, int width) {
  const auto& source_config = kLogSources[static_cast<size_t>(action->source)];
  const int text_width =
      width - 2 * kBasicSidePadding - kLogLevelValueWidth - 40 - 24;
  lv_obj_t* row = lv_obj_create(parent);
  if (row == nullptr) {
    return false;
  }
  lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_flag(row, LV_OBJ_FLAG_GESTURE_BUBBLE);
  lv_obj_set_size(row, width, kLogLevelRowHeight);
  lv_obj_set_pos(row, 0, y);
  lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_set_style_bg_color(
      row, lv_color_hex(SettingsThemeColors().state_layer), LV_STATE_PRESSED);
  lv_obj_set_style_bg_opa(row, kPressedOpacity, LV_STATE_PRESSED);
  lv_obj_set_style_border_width(row, 0, LV_PART_MAIN);
  lv_obj_set_style_radius(row, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(row, 0, LV_PART_MAIN);
  if (!AddPressCancelOnLeave(row)) {
    return false;
  }
  lv_obj_add_event_cb(
      row, LogLevelRowClickedEventCallback, LV_EVENT_CLICKED, action);

  lv_obj_t* title = CreateLabel(row, source_config.title,
      lv_color_hex(SettingsThemeColors().on_surface), Font28());
  if (title == nullptr) {
    return false;
  }
  lv_obj_set_width(title, text_width);
  lv_obj_set_height(title, static_cast<int>(lv_font_get_line_height(Font28())));
  lv_label_set_long_mode(title, LV_LABEL_LONG_DOT);
  lv_obj_align(title, LV_ALIGN_TOP_LEFT, kBasicSidePadding, 18);

  lv_obj_t* subtitle = CreateLabel(row, source_config.source,
      lv_color_hex(SettingsThemeColors().on_surface_variant), Font22());
  if (subtitle == nullptr) {
    return false;
  }
  lv_obj_set_width(subtitle, text_width);
  lv_label_set_long_mode(subtitle, LV_LABEL_LONG_DOT);
  lv_obj_update_layout(row);
  lv_obj_align_to(subtitle, title, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 6);

  lv_obj_t* value_label = CreateLabel(row, LogLevelText(action->source),
      lv_color_hex(SettingsThemeColors().on_surface_variant), Font24());
  if (value_label == nullptr) {
    return false;
  }
  action->value_label = value_label;
  lv_obj_set_width(value_label, kLogLevelValueWidth);
  lv_obj_set_style_text_align(value_label, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);
  lv_obj_align(value_label, LV_ALIGN_RIGHT_MID, -(kBasicSidePadding + 40), 0);

  lv_obj_t* arrow = CreateLabel(row, icon::kChevronRight,
      lv_color_hex(SettingsThemeColors().on_surface_variant),
      MaterialIconFont32());
  if (arrow == nullptr) {
    return false;
  }
  lv_obj_align(arrow, LV_ALIGN_RIGHT_MID, -kBasicSidePadding, 0);
  return true;
}

/**
 * @brief 构建开发者选项内容
 * @param body 内容容器
 * @param state 设置页状态
 * @return 创建成功返回 true，否则返回 false
 */
bool BuildDeveloperOptionsContent(lv_obj_t* body, SettingsViewState* state) {
  const int width = state->config.width;
  // 弹窗属于开发者页面，上一页面删除时会随之释放。
  state->log_level_select_sheet = {};
  if (!CreateSectionLabel(body, "Serial logs", 0, width)) {
    return false;
  }
  int y = kBasicSectionHeight;
  for (size_t i = 0; i < kLogSourceCount; ++i) {
    auto& action = state->log_level_actions[i];
    action = {state, static_cast<SettingsLogSource>(i), nullptr};
    if (!CreateLogLevelRow(body, &action, y, width)) {
      return false;
    }
    y += kLogLevelRowHeight;
  }
  return true;
}

}  // namespace

bool ShowDeveloperOptionsPage(SettingsViewState* state) {
  return ShowNestedPage(
      state, "Developer options", BuildDeveloperOptionsContent);
}

}  // namespace lilygo_box::ui
