/*
 * @Description: 公共提示框控件
 * @Author: LILYGO_L
 * @Date: 2026-07-11 00:00:00
 * @LastEditTime: 2026-08-20 10:32:37
 * @License: GPL 3.0
 */
#include "ui/widgets/prompt/prompt_dialog.h"

#include <algorithm>

#include "ui/input/back_navigation_controller.h"
#include "ui/widgets/prompt/prompt_sheet.h"

namespace lilygo_box::ui {
namespace {

lv_style_selector_t StyleSelector(lv_part_t part, lv_state_t state) {
  return static_cast<lv_style_selector_t>(
      static_cast<uint32_t>(part) | static_cast<uint32_t>(state));
}

void UpdateActionButtonStyle(lv_obj_t* button, lv_obj_t* label,
    uint32_t background_color, uint32_t pressed_color,
    uint32_t text_color) {
  if (button == nullptr) {
    return;
  }
  lv_obj_set_style_bg_color(
      button, lv_color_hex(background_color), LV_PART_MAIN);
  lv_obj_set_style_bg_color(button, lv_color_hex(background_color),
      StyleSelector(LV_PART_MAIN, LV_STATE_FOCUSED));
  lv_obj_set_style_bg_color(button, lv_color_hex(background_color),
      StyleSelector(LV_PART_MAIN, LV_STATE_FOCUS_KEY));
  lv_obj_set_style_bg_color(button, lv_color_hex(pressed_color),
      StyleSelector(LV_PART_MAIN, LV_STATE_PRESSED));
  if (label != nullptr) {
    lv_obj_set_style_text_color(
        label, lv_color_hex(text_color), LV_PART_MAIN);
  }
}

/**
 * @brief 设置对象整体透明度
 * @param object LVGL 对象
 * @param opacity 透明度
 */
void SetObjectOpacity(void* object, int32_t opacity) {
  if (object != nullptr) {
    lv_obj_set_style_opa(static_cast<lv_obj_t*>(object),
                         static_cast<lv_opa_t>(opacity), LV_PART_MAIN);
  }
}

/**
 * @brief 立即关闭并删除提示框
 * @param state 提示框状态
 */
void CloseImmediately(PromptDialogState* state) {
  if (state == nullptr || state->overlay == nullptr) {
    return;
  }
  lv_obj_t* overlay = state->overlay;
  state->overlay = nullptr;
  state->panel = nullptr;
  state->title_label = nullptr;
  state->subtitle_label = nullptr;
  state->body = nullptr;
  state->cancel_button = nullptr;
  state->cancel_button_label = nullptr;
  state->confirm_button = nullptr;
  state->confirm_button_label = nullptr;
  state->slide_from_bottom = false;
  state->closing = false;
  lv_obj_delete(overlay);
}

/**
 * @brief 处理提示框关闭动画完成事件
 * @param animation LVGL 动画对象
 */
void CloseAnimationCompletedCallback(lv_anim_t* animation) {
  CloseImmediately(static_cast<PromptDialogState*>(
      lv_anim_get_user_data(animation)));
}

/**
 * @brief 启动对象透明度渐变动画
 * @param object LVGL 对象
 * @param start 起始透明度
 * @param end 结束透明度
 * @param duration_ms 动画时长
 * @param user_data 动画用户数据
 * @param completed_callback 动画完成回调
 */
void StartFadeAnimation(lv_obj_t* object, int start, int end,
    uint32_t duration_ms, void* user_data,
    lv_anim_completed_cb_t completed_callback) {
  lv_anim_delete(object, SetObjectOpacity);
  lv_anim_t animation;
  lv_anim_init(&animation);
  lv_anim_set_var(&animation, object);
  lv_anim_set_values(&animation, start, end);
  lv_anim_set_duration(&animation, duration_ms);
  lv_anim_set_path_cb(&animation, lv_anim_path_linear);
  lv_anim_set_exec_cb(&animation, SetObjectOpacity);
  lv_anim_set_user_data(&animation, user_data);
  if (completed_callback != nullptr) {
    lv_anim_set_completed_cb(&animation, completed_callback);
  }
  lv_anim_start(&animation);
}

/**
 * @brief 处理提示框取消按钮点击事件
 * @param event LVGL 事件对象
 */
void CancelActionEventCallback(lv_event_t* event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
    return;
  }
  auto* state = static_cast<PromptDialogState*>(
      lv_event_get_user_data(event));
  if (state == nullptr) {
    return;
  }
  if (state->cancel_callback != nullptr) {
    state->cancel_callback(state->callback_context);
  }
  ClosePromptDialog(state);
  lv_event_stop_bubbling(event);
  lv_event_stop_processing(event);
}

/**
 * @brief 处理提示框确认按钮点击事件
 * @param event LVGL 事件对象
 */
void ConfirmActionEventCallback(lv_event_t* event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
    return;
  }
  auto* state = static_cast<PromptDialogState*>(
      lv_event_get_user_data(event));
  if (state == nullptr) {
    return;
  }
  if (state->confirm_callback != nullptr) {
    state->confirm_callback(state->callback_context);
  }
  ClosePromptDialog(state);
  lv_event_stop_bubbling(event);
  lv_event_stop_processing(event);
}

/**
 * @brief 处理提示框遮罩点击事件
 * @param event LVGL 事件对象
 */
void OverlayClickedEventCallback(lv_event_t* event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED ||
      lv_event_get_target_obj(event) !=
          lv_event_get_current_target_obj(event)) {
    return;
  }
  CancelActionEventCallback(event);
}

/**
 * @brief 阻止面板点击事件冒泡到遮罩层
 * @param event LVGL 事件对象
 */
void PanelClickedEventCallback(lv_event_t* event) {
  if (lv_event_get_code(event) == LV_EVENT_CLICKED) {
    lv_event_stop_bubbling(event);
  }
}

/**
 * @brief 创建提示框底部取消和确认按钮
 * @param state 提示框状态
 * @param config 提示框配置
 * @param panel_width 提示框面板宽度
 * @param action_y 操作区域顶部坐标
 * @return 创建成功返回 true，否则返回 false
 */
bool CreateActionButtons(PromptDialogState* state,
    const PromptDialogConfig& config, int panel_width, int action_y) {
  state->cancel_button = nullptr;
  state->cancel_button_label = nullptr;
  state->confirm_button = nullptr;
  state->confirm_button_label = nullptr;
  const bool show_cancel = config.cancel_text != nullptr;
  const bool show_confirm = config.confirm_text != nullptr;
  if (!show_cancel && !show_confirm) {
    return false;
  }
  const int gap = config.action_button_gap;
  const int content_width =
      panel_width - 2 * config.inner_padding;
  const int button_width = show_cancel && show_confirm
      ? (content_width - gap) / 2
      : content_width;
  const int button_height = config.action_button_height;
  const int button_y = action_y + config.action_height -
      config.action_bottom_padding - button_height;

  PromptSheetButtonConfig cancel_config;
  cancel_config.text = config.cancel_text;
  cancel_config.x = config.inner_padding;
  cancel_config.y = button_y;
  cancel_config.width = button_width;
  cancel_config.height = button_height;
  cancel_config.radius = config.action_button_radius > 0
      ? config.action_button_radius
      : button_height / 2;
  cancel_config.background_color = config.cancel_background_color;
  cancel_config.pressed_background_color = config.cancel_pressed_color;
  cancel_config.text_color = config.cancel_text_color;
  cancel_config.font = config.action_font;
  cancel_config.callback = CancelActionEventCallback;
  cancel_config.user_data = state;
  if (show_cancel) {
    state->cancel_button =
        CreatePromptSheetButton(state->panel, cancel_config);
    if (state->cancel_button == nullptr) {
      return false;
    }
    state->cancel_button_label = lv_obj_get_child(state->cancel_button, 0);
  }
  if (!show_confirm) {
    return true;
  }

  PromptSheetButtonConfig confirm_config = cancel_config;
  confirm_config.text = config.confirm_text;
  confirm_config.x = show_cancel
      ? config.inner_padding + button_width + gap
      : config.inner_padding;
  confirm_config.background_color = config.confirm_background_color;
  confirm_config.pressed_background_color = config.confirm_pressed_color;
  confirm_config.text_color = config.confirm_text_color;
  confirm_config.callback = ConfirmActionEventCallback;
  state->confirm_button =
      CreatePromptSheetButton(state->panel, confirm_config);
  if (state->confirm_button == nullptr) {
    return false;
  }
  state->confirm_button_label = lv_obj_get_child(state->confirm_button, 0);
  return true;
}

}  // namespace

lv_obj_t* ShowPromptDialog(lv_obj_t* parent, PromptDialogState* state,
    const PromptDialogConfig& config) {
  if (parent == nullptr || state == nullptr || config.screen_width <= 0 ||
      config.screen_height <= 0 || config.dialog_width <= 0 ||
      config.dialog_height <= 0 || config.title == nullptr) {
    return nullptr;
  }
  CloseImmediately(state);

  PromptSheetConfig sheet_config;
  sheet_config.screen_width = config.screen_width;
  sheet_config.screen_height = config.screen_height;
  sheet_config.sheet_width = std::min(
      config.dialog_width, config.screen_width - 16);
  sheet_config.sheet_height = std::min(
      config.dialog_height, config.screen_height - 24);
  sheet_config.sheet_radius = config.dialog_radius;
  sheet_config.sheet_color = config.dialog_color;
  sheet_config.overlay_opacity = config.overlay_opacity;
  sheet_config.bottom_margin = config.bottom_margin;
  sheet_config.side_margin =
      (config.screen_width - sheet_config.sheet_width) / 2;
  lv_obj_t* overlay = CreatePromptSheetOverlay(parent, sheet_config);
  if (overlay == nullptr) {
    return nullptr;
  }
  state->overlay = overlay;
  state->animation_ms = config.animation_ms;
  state->cancel_callback = config.cancel_callback;
  state->confirm_callback = config.confirm_callback;
  state->callback_context = config.callback_context;
  state->slide_from_bottom = config.slide_from_bottom;
  state->closing = false;
  lv_obj_add_event_cb(overlay, OverlayClickedEventCallback,
                      LV_EVENT_CLICKED, state);
  if (!RegisterBackNavigationHandler(overlay, [state]() {
        if (state == nullptr || state->overlay == nullptr ||
            state->closing) {
          return;
        }
        if (state->cancel_callback != nullptr) {
          state->cancel_callback(state->callback_context);
        }
        ClosePromptDialog(state);
      })) {
    CloseImmediately(state);
    return nullptr;
  }

  lv_obj_t* panel = CreatePromptSheet(overlay, sheet_config);
  if (panel == nullptr) {
    CloseImmediately(state);
    return nullptr;
  }
  state->panel = panel;
  const int panel_x =
      (config.screen_width - sheet_config.sheet_width) / 2;
  const int panel_y =
      (config.screen_height - sheet_config.sheet_height) / 2;
  lv_obj_set_x(panel, panel_x);
  if (!config.slide_from_bottom) {
    lv_obj_set_y(panel, panel_y);
  }
  lv_obj_add_event_cb(panel, PanelClickedEventCallback,
                      LV_EVENT_CLICKED, state);

  lv_obj_t* title = CreatePromptSheetLabel(panel, config.title,
      config.primary_text_color, config.title_font);
  if (title == nullptr) {
    CloseImmediately(state);
    return nullptr;
  }
  state->title_label = title;
  lv_obj_set_pos(title, config.inner_padding, config.title_y);
  lv_obj_set_width(
      title, sheet_config.sheet_width - 2 * config.inner_padding);
  lv_label_set_long_mode(title, LV_LABEL_LONG_DOT);
  lv_obj_set_style_text_align(
      title, config.title_text_align, LV_PART_MAIN);

  int body_y = config.header_height;
  if (config.subtitle != nullptr && config.subtitle[0] != '\0') {
    lv_obj_t* subtitle = CreatePromptSheetLabel(panel, config.subtitle,
        config.secondary_text_color, config.subtitle_font);
    if (subtitle == nullptr) {
      CloseImmediately(state);
      return nullptr;
    }
    state->subtitle_label = subtitle;
    lv_obj_set_width(
        subtitle, sheet_config.sheet_width - 2 * config.inner_padding);
    lv_label_set_long_mode(subtitle, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(
        subtitle, config.subtitle_text_align, LV_PART_MAIN);
    AlignPromptSheetSubtitle(
        subtitle, title, config.title_subtitle_gap);
    lv_obj_update_layout(subtitle);
    const int subtitle_bottom =
        lv_obj_get_y(subtitle) + lv_obj_get_height(subtitle);
    body_y = std::max(
        body_y, subtitle_bottom + config.subtitle_body_gap);
  }

  const int action_y =
      sheet_config.sheet_height - config.action_height;
  if (body_y >= action_y) {
    CloseImmediately(state);
    return nullptr;
  }
  state->body = lv_obj_create(panel);
  if (state->body == nullptr) {
    CloseImmediately(state);
    return nullptr;
  }
  lv_obj_set_pos(state->body, 0, body_y);
  lv_obj_set_size(state->body, sheet_config.sheet_width,
                  action_y - body_y);
  lv_obj_set_style_bg_opa(state->body, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_set_style_border_width(state->body, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(state->body, 0, LV_PART_MAIN);
  lv_obj_set_scroll_dir(state->body, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(state->body, LV_SCROLLBAR_MODE_AUTO);
  lv_obj_add_flag(state->body, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(state->body, LV_OBJ_FLAG_GESTURE_BUBBLE);

  if (!CreateActionButtons(
      state, config, sheet_config.sheet_width, action_y)) {
    CloseImmediately(state);
    return nullptr;
  }
  if (config.slide_from_bottom) {
    AnimatePromptSheetIn(panel, sheet_config, config.animation_ms);
  } else {
    lv_obj_set_style_opa(overlay, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_opa(panel, LV_OPA_TRANSP, LV_PART_MAIN);
    StartFadeAnimation(overlay, LV_OPA_TRANSP, LV_OPA_COVER,
        config.animation_ms, nullptr, nullptr);
    StartFadeAnimation(panel, LV_OPA_TRANSP, LV_OPA_COVER,
        config.animation_ms, nullptr, nullptr);
  }
  lv_obj_move_to_index(overlay, -1);
  return state->body;
}

lv_obj_t* UpdatePromptDialog(
    PromptDialogState* state, const PromptDialogConfig& config) {
  if (state == nullptr || state->overlay == nullptr ||
      state->panel == nullptr || state->title_label == nullptr ||
      state->body == nullptr || state->closing || config.title == nullptr ||
      config.screen_width <= 0 || config.screen_height <= 0 ||
      config.dialog_width <= 0 || config.dialog_height <= 0) {
    return nullptr;
  }

  const bool show_subtitle =
      config.subtitle != nullptr && config.subtitle[0] != '\0';
  const bool show_cancel = config.cancel_text != nullptr;
  const bool show_confirm = config.confirm_text != nullptr;
  if (show_subtitle != (state->subtitle_label != nullptr) ||
      show_cancel != (state->cancel_button != nullptr) ||
      show_confirm != (state->confirm_button != nullptr)) {
    return nullptr;
  }

  const int panel_width =
      std::min(config.dialog_width, config.screen_width - 16);
  const int panel_height =
      std::min(config.dialog_height, config.screen_height - 24);
  const int panel_x = (config.screen_width - panel_width) / 2;
  const int panel_y = config.slide_from_bottom
      ? config.screen_height - panel_height - config.bottom_margin
      : (config.screen_height - panel_height) / 2;
  StopPromptSheetAnimation(state->panel);
  lv_obj_set_size(
      state->overlay, config.screen_width, config.screen_height);
  lv_obj_set_size(state->panel, panel_width, panel_height);
  lv_obj_set_pos(state->panel, panel_x, panel_y);
  lv_label_set_text(state->title_label, config.title);
  lv_obj_set_pos(
      state->title_label, config.inner_padding, config.title_y);
  lv_obj_set_width(
      state->title_label, panel_width - 2 * config.inner_padding);
  lv_label_set_long_mode(state->title_label, LV_LABEL_LONG_DOT);
  lv_obj_set_style_text_color(state->title_label,
      lv_color_hex(config.primary_text_color), LV_PART_MAIN);
  if (config.title_font != nullptr) {
    lv_obj_set_style_text_font(
        state->title_label, config.title_font, LV_PART_MAIN);
  }
  lv_obj_set_style_text_align(
      state->title_label, config.title_text_align, LV_PART_MAIN);

  int body_y = config.header_height;
  if (show_subtitle) {
    lv_label_set_text(state->subtitle_label, config.subtitle);
    lv_obj_set_width(
        state->subtitle_label, panel_width - 2 * config.inner_padding);
    lv_label_set_long_mode(state->subtitle_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(state->subtitle_label,
        lv_color_hex(config.secondary_text_color), LV_PART_MAIN);
    if (config.subtitle_font != nullptr) {
      lv_obj_set_style_text_font(
          state->subtitle_label, config.subtitle_font, LV_PART_MAIN);
    }
    lv_obj_set_style_text_align(
        state->subtitle_label, config.subtitle_text_align, LV_PART_MAIN);
    AlignPromptSheetSubtitle(state->subtitle_label, state->title_label,
        config.title_subtitle_gap);
    lv_obj_update_layout(state->subtitle_label);
    const int subtitle_bottom = lv_obj_get_y(state->subtitle_label) +
        lv_obj_get_height(state->subtitle_label);
    body_y = std::max(
        body_y, subtitle_bottom + config.subtitle_body_gap);
  }

  const int action_y = panel_height - config.action_height;
  if (body_y >= action_y) {
    return nullptr;
  }
  lv_obj_set_pos(state->body, 0, body_y);
  lv_obj_set_size(state->body, panel_width, action_y - body_y);

  const int button_gap = config.action_button_gap;
  const int button_content_width =
      panel_width - 2 * config.inner_padding;
  const int button_width = show_cancel && show_confirm
      ? (button_content_width - button_gap) / 2
      : button_content_width;
  const int button_height = config.action_button_height;
  const int button_y = action_y + config.action_height -
      config.action_bottom_padding - button_height;
  const int button_radius = config.action_button_radius > 0
      ? config.action_button_radius
      : button_height / 2;
  if (show_cancel) {
    lv_obj_set_pos(
        state->cancel_button, config.inner_padding, button_y);
    lv_obj_set_size(state->cancel_button, button_width, button_height);
    lv_obj_set_style_radius(
        state->cancel_button, button_radius, LV_PART_MAIN);
    if (state->cancel_button_label != nullptr) {
      lv_obj_center(state->cancel_button_label);
    }
  }
  if (show_confirm) {
    const int confirm_x = show_cancel
        ? config.inner_padding + button_width + button_gap
        : config.inner_padding;
    lv_obj_set_pos(state->confirm_button, confirm_x, button_y);
    lv_obj_set_size(state->confirm_button, button_width, button_height);
    lv_obj_set_style_radius(
        state->confirm_button, button_radius, LV_PART_MAIN);
    if (state->confirm_button_label != nullptr) {
      lv_obj_center(state->confirm_button_label);
    }
  }

  if (show_cancel && state->cancel_button_label != nullptr) {
    lv_label_set_text(state->cancel_button_label, config.cancel_text);
    if (config.action_font != nullptr) {
      lv_obj_set_style_text_font(
          state->cancel_button_label, config.action_font, LV_PART_MAIN);
    }
    UpdateActionButtonStyle(state->cancel_button,
        state->cancel_button_label, config.cancel_background_color,
        config.cancel_pressed_color, config.cancel_text_color);
  }
  if (show_confirm && state->confirm_button_label != nullptr) {
    lv_label_set_text(state->confirm_button_label, config.confirm_text);
    if (config.action_font != nullptr) {
      lv_obj_set_style_text_font(
          state->confirm_button_label, config.action_font, LV_PART_MAIN);
    }
    UpdateActionButtonStyle(state->confirm_button,
        state->confirm_button_label, config.confirm_background_color,
        config.confirm_pressed_color, config.confirm_text_color);
  }

  state->animation_ms = config.animation_ms;
  state->cancel_callback = config.cancel_callback;
  state->confirm_callback = config.confirm_callback;
  state->callback_context = config.callback_context;
  state->slide_from_bottom = config.slide_from_bottom;
  return state->body;
}

void ClosePromptDialog(PromptDialogState* state) {
  if (state == nullptr || state->overlay == nullptr || state->closing) {
    return;
  }
  state->closing = true;
  lv_obj_remove_flag(state->overlay, LV_OBJ_FLAG_CLICKABLE);
  if (state->slide_from_bottom && state->panel != nullptr) {
    lv_obj_t* overlay = state->overlay;
    lv_obj_t* panel = state->panel;
    const uint32_t animation_ms = state->animation_ms;
    state->overlay = nullptr;
    state->panel = nullptr;
    state->title_label = nullptr;
    state->subtitle_label = nullptr;
    state->body = nullptr;
    state->cancel_button = nullptr;
    state->cancel_button_label = nullptr;
    state->confirm_button = nullptr;
    state->confirm_button_label = nullptr;
    state->slide_from_bottom = false;
    state->closing = false;
    if (!AnimatePromptSheetOut(overlay, panel, animation_ms)) {
      lv_obj_delete(overlay);
    }
    return;
  }
  if (state->panel != nullptr) {
    const int panel_opacity =
        lv_obj_get_style_opa(state->panel, LV_PART_MAIN);
    StartFadeAnimation(state->panel, panel_opacity, LV_OPA_TRANSP,
        state->animation_ms, nullptr, nullptr);
  }
  const int current =
      lv_obj_get_style_opa(state->overlay, LV_PART_MAIN);
  StartFadeAnimation(state->overlay, current, LV_OPA_TRANSP,
      state->animation_ms, state, CloseAnimationCompletedCallback);
}

bool IsPromptDialogVisible(const PromptDialogState* state) {
  return state != nullptr && state->overlay != nullptr;
}

}  // namespace lilygo_box::ui
