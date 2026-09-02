/*
 * @Description: Reusable settings single-line text edit page implementation
 * @Author: LILYGO_L
 * @Date: 2026-09-01 00:00:00
 * @LastEditTime: 2026-09-02 17:56:50
 * @License: GPL 3.0
 */
#include "ui/views/settings/settings_text_edit_page.h"

#include "ui/animation/transition_animation.h"
#include "ui/resources/fonts/icon_assets.h"
#include "ui/views/settings/settings_basic_view_common.h"
#include "ui/widgets/shared_keyboard.h"

namespace lilygo_box::ui {
namespace {

/**
 * @brief 清空单行编辑页运行引用
 * @param state 编辑页运行状态
 */
void ClearSettingsTextEditPageState(SettingsTextEditPageState* state) {
  if (state == nullptr) {
    return;
  }
  state->page = nullptr;
  state->text_area = nullptr;
  state->keyboard = nullptr;
  state->confirm_button = nullptr;
  state->confirm_icon = nullptr;
  state->save_callback = nullptr;
  state->validation_callback = nullptr;
  state->callback_context = nullptr;
  state->width = 0;
  state->closing = false;
}

/**
 * @brief 处理编辑页被父对象直接删除的情况
 * @param event LVGL 事件对象
 */
void SettingsTextEditPageDeleteEventCallback(lv_event_t* event) {
  if (lv_event_get_code(event) != LV_EVENT_DELETE) {
    return;
  }
  auto* state =
      static_cast<SettingsTextEditPageState*>(lv_event_get_user_data(event));
  if (state != nullptr && lv_event_get_target_obj(event) == state->page) {
    ClearSettingsTextEditPageState(state);
  }
}

/**
 * @brief 处理编辑页关闭动画完成
 * @param animation LVGL 动画对象
 */
void SettingsTextEditCloseCompletedCallback(lv_anim_t* animation) {
  auto* state =
      static_cast<SettingsTextEditPageState*>(lv_anim_get_user_data(animation));
  if (state == nullptr || state->page == nullptr) {
    return;
  }
  lv_obj_t* page = state->page;
  ClearSettingsTextEditPageState(state);
  lv_obj_delete(page);
}

/**
 * @brief 处理编辑页背景点击并隐藏屏幕键盘
 * @param event LVGL 事件对象
 */
void SettingsTextEditKeyboardDismissEventCallback(lv_event_t* event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
    return;
  }
  auto* state =
      static_cast<SettingsTextEditPageState*>(lv_event_get_user_data(event));
  if (state == nullptr || state->keyboard == nullptr ||
      state->text_area == nullptr) {
    return;
  }
  lv_obj_t* target = lv_event_get_target_obj(event);
  if (IsObjectOrChildOf(target, state->keyboard) ||
      IsObjectOrChildOf(target, state->text_area)) {
    return;
  }
  HideSharedKeyboard(state->keyboard);
}

/**
 * @brief 更新单行编辑页输入框的错误描边
 * @param text_area 文本输入框
 * @param show_error 是否显示错误描边
 */
void UpdateSettingsTextEditErrorStyle(lv_obj_t* text_area, bool show_error) {
  if (text_area == nullptr) {
    return;
  }
  const int outline_width = show_error ? 2 : 0;
  lv_obj_set_style_border_width(text_area, 0, LV_PART_MAIN);
  lv_obj_set_style_border_width(text_area, 0, LV_STATE_FOCUSED);
  lv_obj_set_style_outline_width(text_area, outline_width, LV_PART_MAIN);
  lv_obj_set_style_outline_width(text_area, outline_width, LV_STATE_FOCUSED);
  lv_obj_set_style_outline_color(
      text_area, lv_color_hex(theme::FixedColors().error), LV_PART_MAIN);
  lv_obj_set_style_outline_color(
      text_area, lv_color_hex(theme::FixedColors().error), LV_STATE_FOCUSED);
  lv_obj_set_style_outline_opa(text_area, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_outline_opa(text_area, LV_OPA_COVER, LV_STATE_FOCUSED);
  lv_obj_set_style_outline_pad(text_area, -2, LV_PART_MAIN);
  lv_obj_set_style_outline_pad(text_area, -2, LV_STATE_FOCUSED);
}

/**
 * @brief 设置单行编辑页确认操作是否可用
 * @param state 编辑页运行状态
 * @param enabled 是否启用确认操作
 */
void SetSettingsTextEditConfirmEnabled(
    SettingsTextEditPageState* state, bool enabled) {
  if (state == nullptr || state->confirm_button == nullptr ||
      state->confirm_icon == nullptr) {
    return;
  }
  if (enabled) {
    lv_obj_remove_state(state->confirm_button, LV_STATE_DISABLED);
    lv_obj_remove_state(state->confirm_icon, LV_STATE_DISABLED);
  } else {
    lv_obj_add_state(state->confirm_button, LV_STATE_DISABLED);
    lv_obj_add_state(state->confirm_icon, LV_STATE_DISABLED);
  }
}

/**
 * @brief 根据当前输入更新错误描边和确认操作状态
 * @param state 编辑页运行状态
 * @param show_empty_error 空输入无效时是否显示错误描边
 * @return 当前输入有效返回 true
 */
bool UpdateSettingsTextEditValidationState(
    SettingsTextEditPageState* state, bool show_empty_error) {
  if (state == nullptr || state->text_area == nullptr) {
    return false;
  }
  const char* text = lv_textarea_get_text(state->text_area);
  const bool valid = state->validation_callback == nullptr ||
                     state->validation_callback(text, state->callback_context);
  const bool has_text = text != nullptr && text[0] != '\0';
  UpdateSettingsTextEditErrorStyle(
      state->text_area, !valid && (has_text || show_empty_error));
  SetSettingsTextEditConfirmEnabled(state, valid);
  return valid;
}

/**
 * @brief 处理单行编辑页输入内容变化
 * @param event LVGL 事件对象
 */
void SettingsTextEditValueChangedEventCallback(lv_event_t* event) {
  if (lv_event_get_code(event) != LV_EVENT_VALUE_CHANGED) {
    return;
  }
  auto* state =
      static_cast<SettingsTextEditPageState*>(lv_event_get_user_data(event));
  if (state == nullptr || state->text_area == nullptr ||
      state->validation_callback == nullptr) {
    return;
  }
  UpdateSettingsTextEditValidationState(state, false);
}

/**
 * @brief 处理编辑页取消按钮
 * @param event LVGL 事件对象
 */
void SettingsTextEditCancelClickedEventCallback(lv_event_t* event) {
  if (lv_event_get_code(event) == LV_EVENT_CLICKED) {
    CloseSettingsTextEditPage(
        static_cast<SettingsTextEditPageState*>(lv_event_get_user_data(event)),
        true);
  }
}

/**
 * @brief 处理编辑页确认按钮
 * @param event LVGL 事件对象
 */
void SettingsTextEditConfirmClickedEventCallback(lv_event_t* event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
    return;
  }
  auto* state =
      static_cast<SettingsTextEditPageState*>(lv_event_get_user_data(event));
  if (state == nullptr || state->text_area == nullptr ||
      state->save_callback == nullptr) {
    return;
  }
  if (!UpdateSettingsTextEditValidationState(state, true)) {
    return;
  }
  const char* text = lv_textarea_get_text(state->text_area);
  if (state->save_callback(text, state->callback_context)) {
    CloseSettingsTextEditPage(state, true);
  }
}

/**
 * @brief 创建编辑页工具栏图标
 * @param parent 工具栏按钮
 * @param symbol Material Symbols 图标
 * @return 创建成功返回图标对象，否则返回 nullptr
 */
lv_obj_t* CreateSettingsTextEditToolbarIcon(
    lv_obj_t* parent, const char* symbol) {
  lv_obj_t* icon_label = CreateLabel(parent, symbol,
      lv_color_hex(SettingsThemeColors().on_surface), MaterialIconFont44());
  if (icon_label == nullptr) {
    return nullptr;
  }
  lv_obj_set_style_text_color(icon_label,
      lv_color_hex(SettingsThemeColors().disabled_content), LV_STATE_DISABLED);
  lv_obj_center(icon_label);
  return icon_label;
}

/**
 * @brief 创建编辑页顶部工具栏
 * @param parent 编辑页对象
 * @param state 编辑页运行状态
 * @param config 编辑页配置
 * @return 创建成功返回 true，否则返回 false
 */
bool CreateSettingsTextEditHeader(lv_obj_t* parent,
    SettingsTextEditPageState* state,
    const SettingsTextEditPageConfig& config) {
  lv_obj_t* cancel = CreateToolbarButton(parent, kTextEditButtonSide,
      kTextEditButtonTop, SettingsTextEditCancelClickedEventCallback, state);
  if (cancel == nullptr ||
      CreateSettingsTextEditToolbarIcon(cancel, icon::kClose) == nullptr) {
    return false;
  }
  lv_obj_t* confirm = CreateToolbarButton(parent,
      config.width - kTextEditButtonSide - kTextEditButtonSize,
      kTextEditButtonTop, SettingsTextEditConfirmClickedEventCallback, state);
  if (confirm == nullptr) {
    return false;
  }
  state->confirm_button = confirm;
  state->confirm_icon =
      CreateSettingsTextEditToolbarIcon(confirm, icon::kCheck);
  if (state->confirm_icon == nullptr) {
    return false;
  }
  lv_obj_t* title = CreateLabel(parent, config.title,
      lv_color_hex(SettingsThemeColors().on_surface), Font32());
  if (title == nullptr) {
    return false;
  }
  lv_obj_set_width(title, config.width);
  lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
  lv_obj_align(title, LV_ALIGN_TOP_MID, 0, kDetailTitleTop);
  return true;
}

/**
 * @brief 创建编辑页输入框、说明和键盘
 * @param parent 编辑页对象
 * @param state 编辑页运行状态
 * @param config 编辑页配置
 * @return 创建成功返回 true，否则返回 false
 */
bool CreateSettingsTextEditContent(lv_obj_t* parent,
    SettingsTextEditPageState* state,
    const SettingsTextEditPageConfig& config) {
  lv_obj_t* text_area = lv_textarea_create(parent);
  if (text_area == nullptr) {
    return false;
  }
  state->text_area = text_area;
  lv_obj_add_flag(text_area, LV_OBJ_FLAG_GESTURE_BUBBLE);
  lv_obj_set_size(text_area, config.width - 2 * kTextEditTextAreaSide,
      kTextEditTextAreaHeight);
  lv_obj_align(text_area, LV_ALIGN_TOP_LEFT, kTextEditTextAreaSide,
      kTextEditTextAreaTop);
  lv_textarea_set_one_line(text_area, true);
  lv_textarea_set_max_length(
      text_area, static_cast<uint32_t>(config.maximum_length));
  lv_textarea_set_accepted_chars(text_area, config.accepted_chars);
  lv_textarea_set_text(text_area, config.initial_text);
  lv_textarea_set_cursor_pos(text_area, LV_TEXTAREA_CURSOR_LAST);
  ApplySettingsTextAreaStyle(text_area, Font32(), kTextEditTextAreaHeight);
  lv_obj_add_event_cb(text_area, SettingsTextEditValueChangedEventCallback,
      LV_EVENT_VALUE_CHANGED, state);
  UpdateSettingsTextEditValidationState(state, false);

  lv_obj_t* help = CreateLabel(parent, config.help_text,
      lv_color_hex(SettingsThemeColors().on_surface_variant), Font24());
  if (help == nullptr) {
    return false;
  }
  lv_obj_set_width(help, config.width - 2 * (kTextEditTextAreaSide + 10));
  lv_label_set_long_mode(help, LV_LABEL_LONG_WRAP);
  lv_obj_align(
      help, LV_ALIGN_TOP_LEFT, kTextEditTextAreaSide + 10, kTextEditHelpTop);

  SharedKeyboardConfig keyboard_config;
  keyboard_config.width = config.width;
  keyboard_config.height = config.height * kTextEditKeyboardHeightPercent / 100;
  keyboard_config.initial_mode = config.keyboard_mode;
  state->keyboard = CreateSharedKeyboard(parent, keyboard_config);
  if (state->keyboard == nullptr) {
    return false;
  }
  lv_obj_add_flag(state->keyboard, LV_OBJ_FLAG_GESTURE_BUBBLE);
  return AttachSharedKeyboardToTextArea(
      state->keyboard, text_area, config.accepted_chars, config.keyboard_mode);
}

}  // namespace

bool ShowSettingsTextEditPage(SettingsTextEditPageState* state,
    const SettingsTextEditPageConfig& config) {
  if (state == nullptr || config.parent == nullptr || config.width <= 0 ||
      config.height <= 0 || config.title == nullptr ||
      config.initial_text == nullptr || config.help_text == nullptr ||
      config.maximum_length == 0 || config.save_callback == nullptr) {
    return false;
  }
  if (state->closing) {
    return true;
  }
  if (state->page != nullptr) {
    lv_obj_move_to_index(state->page, -1);
    return true;
  }

  lv_obj_t* page = lv_obj_create(config.parent);
  if (page == nullptr) {
    return false;
  }
  state->page = page;
  state->save_callback = config.save_callback;
  state->validation_callback = config.validation_callback;
  state->callback_context = config.callback_context;
  state->width = config.width;
  state->closing = false;
  lv_obj_remove_flag(page, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(page, LV_OBJ_FLAG_GESTURE_BUBBLE);
  lv_obj_add_event_cb(
      page, SettingsTextEditPageDeleteEventCallback, LV_EVENT_DELETE, state);
  lv_obj_add_event_cb(page, SettingsTextEditKeyboardDismissEventCallback,
      LV_EVENT_CLICKED, state);
  lv_obj_set_size(page, config.width, config.height);
  lv_obj_set_pos(page, 0, 0);
  lv_obj_set_style_bg_color(
      page, lv_color_hex(SettingsThemeColors().surface), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(page, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_border_width(page, 0, LV_PART_MAIN);
  lv_obj_set_style_radius(page, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(page, 0, LV_PART_MAIN);

  if (!CreateSettingsTextEditHeader(page, state, config) ||
      !CreateSettingsTextEditContent(page, state, config) ||
      !StartSlideLeftWindowTransition(
          page, config.width, kDetailSlideAnimationMs, state, nullptr) ||
      !RegisterBackNavigationHandler(
          page, [state]() { CloseSettingsTextEditPage(state, true); })) {
    CloseSettingsTextEditPage(state, false);
    return false;
  }
  return true;
}

void CloseSettingsTextEditPage(
    SettingsTextEditPageState* state, bool animated) {
  if (state == nullptr || state->page == nullptr || state->closing) {
    return;
  }
  if (animated && StartSlideRightWindowTransition(state->page, state->width,
                      kDetailSlideAnimationMs, state,
                      SettingsTextEditCloseCompletedCallback)) {
    state->closing = true;
    return;
  }
  lv_obj_t* page = state->page;
  ClearSettingsTextEditPageState(state);
  lv_obj_delete(page);
}

}  // namespace lilygo_box::ui
