/*
 * @Description: Prompt select sheet widget
 * @Author: LILYGO_L
 * @Date: 2026-06-25 00:00:00
 * @LastEditTime: 2026-09-02 17:57:10
 * @License: GPL 3.0
 */
#include "ui/widgets/prompt/prompt_select_sheet.h"

#include <algorithm>

#include "ui/input/back_navigation_controller.h"
#include "ui/input/press_cancel.h"
#include "ui/widgets/prompt/prompt_sheet.h"

namespace lilygo_box::ui {
namespace {

/**
 * @brief 立即删除选择提示栏对象
 * @param state 选择提示栏状态
 */
void ClosePromptSelectSheetImmediately(PromptSelectSheetState* state) {
  if (state == nullptr || state->overlay == nullptr) {
    return;
  }

  lv_obj_t* overlay = state->overlay;
  state->overlay = nullptr;
  state->sheet = nullptr;
  lv_obj_delete(overlay);
}

/**
 * @brief 处理选择提示栏遮罩点击
 * @param event LVGL 事件对象
 */
void PromptSelectOverlayClickedEventCallback(lv_event_t* event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
    return;
  }

  ClosePromptSelectSheet(
      static_cast<PromptSelectSheetState*>(lv_event_get_user_data(event)));
  lv_event_stop_bubbling(event);
  lv_event_stop_processing(event);
}

/**
 * @brief 阻止选择提示栏内容点击冒泡到遮罩层
 * @param event LVGL 事件对象
 */
void PromptSelectSheetClickedEventCallback(lv_event_t* event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
    return;
  }
  lv_event_stop_bubbling(event);
}

/**
 * @brief 处理选择提示栏选项点击
 * @param event LVGL 事件对象
 */
void PromptSelectOptionClickedEventCallback(lv_event_t* event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
    return;
  }

  auto* action = static_cast<PromptSelectSheetOptionAction*>(
      lv_event_get_user_data(event));
  if (action == nullptr) {
    return;
  }
  if (action->callback != nullptr) {
    action->callback(action->context, action->value);
  }
  ClosePromptSelectSheet(action->sheet_state);
  lv_event_stop_bubbling(event);
  lv_event_stop_processing(event);
}

/**
 * @brief 创建选择提示栏选项行
 * @param parent 父对象
 * @param action 选项点击参数
 * @param config 选择提示栏配置
 * @param text 选项文本
 * @param y 顶部坐标
 * @param selected 当前是否选中
 * @return 创建成功返回 true，否则返回 false
 */
bool CreatePromptSelectOptionRow(lv_obj_t* parent,
    PromptSelectSheetOptionAction* action,
    const PromptSelectSheetConfig& config, const char* text, int y,
    bool selected) {
  if (parent == nullptr || action == nullptr || text == nullptr) {
    return false;
  }

  lv_obj_t* row = lv_button_create(parent);
  if (row == nullptr) {
    return false;
  }
  lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
  lv_obj_set_size(row, config.sheet_width, config.option_height);
  lv_obj_set_pos(row, 0, y);
  lv_obj_set_style_border_width(row, 0, LV_PART_MAIN);
  lv_obj_set_style_border_width(row, 0, LV_STATE_PRESSED);
  lv_obj_set_style_border_width(row, 0, LV_STATE_FOCUSED);
  lv_obj_set_style_border_width(row, 0, LV_STATE_FOCUS_KEY);
  lv_obj_set_style_outline_width(row, 0, LV_PART_MAIN);
  lv_obj_set_style_outline_width(row, 0, LV_STATE_PRESSED);
  lv_obj_set_style_outline_width(row, 0, LV_STATE_FOCUSED);
  lv_obj_set_style_outline_width(row, 0, LV_STATE_FOCUS_KEY);
  lv_obj_set_style_shadow_width(row, 0, LV_PART_MAIN);
  lv_obj_set_style_shadow_width(row, 0, LV_STATE_PRESSED);
  lv_obj_set_style_shadow_width(row, 0, LV_STATE_FOCUSED);
  lv_obj_set_style_shadow_width(row, 0, LV_STATE_FOCUS_KEY);
  lv_obj_set_style_radius(row, 0, LV_PART_MAIN);
  lv_obj_set_style_radius(row, 0, LV_STATE_PRESSED);
  lv_obj_set_style_pad_all(row, 0, LV_PART_MAIN);
  lv_obj_set_style_bg_color(row,
      lv_color_hex(selected ? config.selected_color : config.sheet_color),
      LV_PART_MAIN);
  lv_obj_set_style_bg_opa(row, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_bg_color(
      row, lv_color_hex(config.pressed_color), LV_STATE_PRESSED);
  lv_obj_set_style_bg_opa(row, config.pressed_opacity, LV_STATE_PRESSED);
  if (!AddPressCancelOnLeave(row)) {
    return false;
  }
  lv_obj_add_event_cb(
      row, PromptSelectOptionClickedEventCallback, LV_EVENT_CLICKED, action);

  lv_obj_t* label = CreatePromptSheetLabel(row, text,
      selected ? config.selected_text_color : config.primary_text_color,
      config.option_font);
  if (label == nullptr) {
    return false;
  }
  lv_obj_align(label, LV_ALIGN_LEFT_MID, config.inner_padding, 0);

  if (selected && config.check_icon != nullptr && config.icon_font != nullptr) {
    lv_obj_t* check = CreatePromptSheetLabel(
        row, config.check_icon, config.selected_text_color, config.icon_font);
    if (check == nullptr) {
      return false;
    }
    lv_obj_align(check, LV_ALIGN_RIGHT_MID, -config.inner_padding, 0);
  }
  return true;
}

/**
 * @brief 创建选择提示栏选项滚动容器
 * @param parent 父对象
 * @param config 选择提示栏配置
 * @param height 容器高度
 * @return 创建成功返回对象指针，否则返回 nullptr
 */
lv_obj_t* CreatePromptSelectOptionContainer(lv_obj_t* parent,
    const PromptSelectSheetConfig& config, int top, int height) {
  if (parent == nullptr || height <= 0) {
    return nullptr;
  }

  lv_obj_t* container = lv_obj_create(parent);
  if (container == nullptr) {
    return nullptr;
  }
  lv_obj_set_pos(container, 0, top);
  lv_obj_set_size(container, config.sheet_width, height);
  lv_obj_set_style_bg_opa(container, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_set_style_border_width(container, 0, LV_PART_MAIN);
  lv_obj_set_style_outline_width(container, 0, LV_PART_MAIN);
  lv_obj_set_style_shadow_width(container, 0, LV_PART_MAIN);
  lv_obj_set_style_radius(container, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(container, 0, LV_PART_MAIN);
  lv_obj_set_scroll_dir(container, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(container, LV_SCROLLBAR_MODE_OFF);
  lv_obj_add_flag(container, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(container, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
  lv_obj_add_event_cb(container, PromptSelectSheetClickedEventCallback,
      LV_EVENT_CLICKED, nullptr);
  return container;
}

}  // namespace

void ClosePromptSelectSheet(PromptSelectSheetState* state) {
  if (state == nullptr || state->overlay == nullptr) {
    return;
  }

  lv_obj_t* overlay = state->overlay;
  lv_obj_t* sheet = state->sheet;
  state->overlay = nullptr;
  state->sheet = nullptr;
  if (!AnimatePromptSheetOut(overlay, sheet, 160)) {
    lv_obj_delete(overlay);
  }
}

bool ShowPromptSelectSheet(
    lv_obj_t* parent, const PromptSelectSheetConfig& config) {
  if (parent == nullptr || config.state == nullptr ||
      config.screen_width <= 0 || config.screen_height <= 0 ||
      config.sheet_width <= 0 || config.sheet_height <= 0 ||
      config.option_height <= 0 || config.button_height <= 0 ||
      config.title == nullptr || config.options == nullptr ||
      config.option_count == 0 ||
      config.option_count > kPromptSelectSheetMaxOptions) {
    return false;
  }

  ClosePromptSelectSheetImmediately(config.state);

  const int effective_bottom_margin =
      std::min(config.bottom_margin, std::max(0, config.screen_height - 1));
  const int effective_sheet_height = std::min(
      config.sheet_height, config.screen_height - effective_bottom_margin);
  if (effective_sheet_height <= 0) {
    return false;
  }

  PromptSheetConfig sheet_config;
  sheet_config.screen_width = config.screen_width;
  sheet_config.screen_height = config.screen_height;
  sheet_config.sheet_width = config.sheet_width;
  sheet_config.sheet_height = effective_sheet_height;
  sheet_config.side_margin = config.side_margin;
  sheet_config.bottom_margin = effective_bottom_margin;
  sheet_config.sheet_radius = config.sheet_radius;
  sheet_config.sheet_color = config.sheet_color;
  sheet_config.overlay_opacity = config.overlay_opacity;

  lv_obj_t* overlay = CreatePromptSheetOverlay(parent, sheet_config);
  if (overlay == nullptr) {
    return false;
  }
  config.state->overlay = overlay;
  if (!RegisterBackNavigationHandler(overlay,
          [state = config.state]() { ClosePromptSelectSheet(state); })) {
    ClosePromptSelectSheetImmediately(config.state);
    return false;
  }
  lv_obj_add_event_cb(overlay, PromptSelectOverlayClickedEventCallback,
      LV_EVENT_CLICKED, config.state);

  lv_obj_t* sheet = CreatePromptSheet(overlay, sheet_config);
  if (sheet == nullptr) {
    ClosePromptSelectSheetImmediately(config.state);
    return false;
  }
  config.state->sheet = sheet;
  lv_obj_add_event_cb(sheet, PromptSelectSheetClickedEventCallback,
      LV_EVENT_CLICKED, config.state);

  lv_obj_t* title = CreatePromptSheetLabel(
      sheet, config.title, config.primary_text_color, config.title_font);
  if (title == nullptr) {
    ClosePromptSelectSheetImmediately(config.state);
    return false;
  }
  lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 34);

  if (config.message != nullptr && config.message[0] != '\0') {
    lv_obj_t* message = CreatePromptSheetLabel(sheet, config.message,
        config.secondary_text_color, config.message_font);
    if (message == nullptr) {
      ClosePromptSelectSheetImmediately(config.state);
      return false;
    }
    lv_obj_set_width(message, config.sheet_width - 2 * config.inner_padding);
    lv_obj_set_style_text_align(message, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_label_set_long_mode(message, LV_LABEL_LONG_WRAP);
    AlignPromptSheetSubtitle(message, title, config.title_message_gap);
  }

  const int cancel_y =
      effective_sheet_height - config.inner_padding - config.button_height;
  const bool sheet_compacted = effective_sheet_height < config.sheet_height;
  const int effective_option_top = sheet_compacted
                                       ? std::max(118, config.option_top - 28)
                                       : config.option_top;
  const int option_area_height =
      cancel_y - effective_option_top - config.inner_padding;
  lv_obj_t* option_container = CreatePromptSelectOptionContainer(
      sheet, config, effective_option_top, option_area_height);
  if (option_container == nullptr) {
    ClosePromptSelectSheetImmediately(config.state);
    return false;
  }

  for (size_t i = 0; i < config.option_count; ++i) {
    PromptSelectSheetOptionAction& action = config.state->actions[i];
    action.sheet_state = config.state;
    action.callback = config.callback;
    action.context = config.callback_context;
    action.value = config.options[i].value;
    const bool selected = config.options[i].value == config.selected_value;
    if (!CreatePromptSelectOptionRow(option_container, &action, config,
            config.options[i].text, static_cast<int>(i) * config.option_height,
            selected)) {
      ClosePromptSelectSheetImmediately(config.state);
      return false;
    }
  }

  PromptSheetButtonConfig cancel_config;
  cancel_config.text = config.cancel_text;
  cancel_config.x = config.inner_padding;
  cancel_config.y = cancel_y;
  cancel_config.width = config.sheet_width - 2 * config.inner_padding;
  cancel_config.height = config.button_height;
  cancel_config.radius = config.button_radius;
  cancel_config.background_color = config.cancel_background_color;
  cancel_config.pressed_background_color = config.cancel_pressed_color;
  cancel_config.pressed_opacity = config.pressed_opacity;
  cancel_config.text_color = config.primary_text_color;
  cancel_config.font = config.cancel_font;
  cancel_config.callback = PromptSelectOverlayClickedEventCallback;
  cancel_config.user_data = config.state;
  if (CreatePromptSheetButton(sheet, cancel_config) == nullptr) {
    ClosePromptSelectSheetImmediately(config.state);
    return false;
  }

  AnimatePromptSheetIn(sheet, sheet_config, config.animation_ms);
  return true;
}

}  // namespace lilygo_box::ui
