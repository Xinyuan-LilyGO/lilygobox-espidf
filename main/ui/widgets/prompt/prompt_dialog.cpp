/*
 * @Description: 公共居中提示框控件
 * @Author: LILYGO_L
 * @Date: 2026-07-11 00:00:00
 * @LastEditTime: 2026-07-11 00:00:00
 * @License: GPL 3.0
 */
#include "ui/widgets/prompt/prompt_dialog.h"

#include <algorithm>

#include "ui/input/edge_back_gesture.h"
#include "ui/widgets/prompt/prompt_sheet.h"

namespace lilygo_box::ui {
namespace {

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
  state->body = nullptr;
  state->edge_swipe = EdgeBackSwipeState();
  state->closing = false;
  lv_obj_delete(overlay);
}

/**
 * @brief 处理提示框淡出动画完成事件
 * @param animation LVGL 动画对象
 */
void FadeOutCompletedCallback(lv_anim_t* animation) {
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
 * @brief 处理提示框边缘返回手势
 * @param event LVGL 事件对象
 */
void EdgeBackEventCallback(lv_event_t* event) {
  auto* state = static_cast<PromptDialogState*>(
      lv_event_get_user_data(event));
  if (state == nullptr || state->overlay == nullptr ||
      !HandleEdgeBackSwipeEvent(event, lv_obj_get_width(state->overlay),
          &state->edge_swipe)) {
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
 * @brief 创建提示框底部取消和确认按钮
 * @param state 提示框状态
 * @param config 提示框配置
 * @param action_y 操作区域顶部坐标
 * @return 创建成功返回 true，否则返回 false
 */
bool CreateActionButtons(PromptDialogState* state,
    const PromptDialogConfig& config, int action_y) {
  const int gap = config.action_button_gap;
  const int content_width =
      config.dialog_width - 2 * config.inner_padding;
  const int button_width = (content_width - gap) / 2;
  const int button_height = config.action_button_height;
  const int button_y = action_y + config.action_height -
      config.action_bottom_padding - button_height;

  PromptSheetButtonConfig cancel_config;
  cancel_config.text = config.cancel_text;
  cancel_config.x = config.inner_padding;
  cancel_config.y = button_y;
  cancel_config.width = button_width;
  cancel_config.height = button_height;
  cancel_config.radius = button_height / 2;
  cancel_config.background_color = config.cancel_background_color;
  cancel_config.pressed_background_color = config.cancel_pressed_color;
  cancel_config.text_color = config.cancel_text_color;
  cancel_config.font = config.action_font;
  cancel_config.callback = CancelActionEventCallback;
  cancel_config.user_data = state;
  if (CreatePromptSheetButton(state->panel, cancel_config) == nullptr) {
    return false;
  }

  PromptSheetButtonConfig confirm_config = cancel_config;
  confirm_config.text = config.confirm_text;
  confirm_config.x = config.inner_padding + button_width + gap;
  confirm_config.background_color = config.confirm_background_color;
  confirm_config.pressed_background_color = config.confirm_pressed_color;
  confirm_config.text_color = config.confirm_text_color;
  confirm_config.callback = ConfirmActionEventCallback;
  return CreatePromptSheetButton(state->panel, confirm_config) != nullptr;
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
  lv_obj_t* overlay = CreatePromptSheetOverlay(parent, sheet_config);
  if (overlay == nullptr) {
    return nullptr;
  }
  state->overlay = overlay;
  state->edge_swipe = EdgeBackSwipeState();
  state->animation_ms = config.animation_ms;
  state->cancel_callback = config.cancel_callback;
  state->confirm_callback = config.confirm_callback;
  state->callback_context = config.callback_context;
  state->closing = false;
  lv_obj_add_event_cb(overlay, OverlayClickedEventCallback,
                      LV_EVENT_CLICKED, state);
  AddEdgeBackSwipeEvents(overlay, EdgeBackEventCallback, state);

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
  lv_obj_set_pos(panel, panel_x, panel_y);
  lv_obj_add_event_cb(panel, PanelClickedEventCallback,
                      LV_EVENT_CLICKED, state);
  AddEdgeBackSwipeEvents(panel, EdgeBackEventCallback, state);

  lv_obj_t* title = CreatePromptSheetLabel(panel, config.title,
      config.primary_text_color, config.title_font);
  if (title == nullptr) {
    CloseImmediately(state);
    return nullptr;
  }
  lv_obj_set_pos(title, config.inner_padding, config.title_y);

  const int action_y =
      sheet_config.sheet_height - config.action_height;
  state->body = lv_obj_create(panel);
  if (state->body == nullptr) {
    CloseImmediately(state);
    return nullptr;
  }
  lv_obj_set_pos(state->body, 0, config.header_height);
  lv_obj_set_size(state->body, sheet_config.sheet_width,
                  action_y - config.header_height);
  lv_obj_set_style_bg_opa(state->body, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_set_style_border_width(state->body, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(state->body, 0, LV_PART_MAIN);
  lv_obj_set_scroll_dir(state->body, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(state->body, LV_SCROLLBAR_MODE_AUTO);
  lv_obj_add_flag(state->body, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(state->body, LV_OBJ_FLAG_GESTURE_BUBBLE);

  if (!CreateActionButtons(state, config, action_y)) {
    CloseImmediately(state);
    return nullptr;
  }
  lv_obj_set_style_opa(overlay, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_set_style_opa(panel, LV_OPA_TRANSP, LV_PART_MAIN);
  StartFadeAnimation(overlay, LV_OPA_TRANSP, LV_OPA_COVER,
      config.animation_ms, nullptr, nullptr);
  StartFadeAnimation(panel, LV_OPA_TRANSP, LV_OPA_COVER,
      config.animation_ms, nullptr, nullptr);
  lv_obj_move_to_index(overlay, -1);
  EnableEdgeBackSwipeEventBubble(overlay);
  return state->body;
}

void ClosePromptDialog(PromptDialogState* state) {
  if (state == nullptr || state->overlay == nullptr || state->closing) {
    return;
  }
  state->closing = true;
  lv_obj_remove_flag(state->overlay, LV_OBJ_FLAG_CLICKABLE);
  if (state->panel != nullptr) {
    const int panel_opacity =
        lv_obj_get_style_opa(state->panel, LV_PART_MAIN);
    StartFadeAnimation(state->panel, panel_opacity, LV_OPA_TRANSP,
        state->animation_ms, nullptr, nullptr);
  }
  const int current =
      lv_obj_get_style_opa(state->overlay, LV_PART_MAIN);
  StartFadeAnimation(state->overlay, current, LV_OPA_TRANSP,
      state->animation_ms, state, FadeOutCompletedCallback);
}

bool IsPromptDialogVisible(const PromptDialogState* state) {
  return state != nullptr && state->overlay != nullptr;
}

}  // namespace lilygo_box::ui
