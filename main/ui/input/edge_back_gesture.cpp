/*
 * @Description: None
 * @Author: LILYGO_L
 * @Date: 2026-05-12 22:15:00
 * @LastEditTime: 2026-05-18 12:00:00
 * @License: GPL 3.0
 */
#include "ui/input/edge_back_gesture.h"

#include <cstdint>

#include "hal/lvgl_port.h"
#include "ui/resources/fonts/font_assets.h"
#include "ui/resources/fonts/icon_assets.h"
#include "ui/haptic_feedback.h"

namespace lilygo_box::ui {
namespace {

constexpr int kIndicatorMinDiameter = 96;
constexpr int kIndicatorMaxDiameter = 132;
constexpr int kIndicatorVisibleDivisor = 3;
constexpr int kIndicatorAnimationMs = 120;
constexpr int kIndicatorMaxOpacity = 238;
constexpr uint32_t kIndicatorColor = 0x202020;
constexpr uint32_t kIndicatorIconColor = 0xFFFFFF;

lv_obj_t* g_back_indicator = nullptr;
lv_obj_t* g_back_indicator_icon = nullptr;
lv_opa_t g_back_indicator_opacity = LV_OPA_TRANSP;

/**
 * @brief 计算整数绝对值
 * @param value 原始值
 * @return 绝对值
 */
int AbsInt(int value) { return value < 0 ? -value : value; }

/**
 * @brief 判断触摸点 X 坐标是否在屏幕宽度范围内。
 * @param point 触摸点坐标。
 * @param screen_width 屏幕宽度。
 * @return X 坐标在屏幕范围内返回 true，否则返回 false。
 */
bool IsPointInsideScreen(lv_point_t point, int screen_width) {
  return point.x >= 0 && point.x < screen_width;
}

/**
 * @brief 将整数限制在指定范围内
 * @param value 原始值
 * @param min_value 最小值
 * @param max_value 最大值
 * @return 限制后的值
 */
int ClampInt(int value, int min_value, int max_value) {
  if (value < min_value) {
    return min_value;
  }
  if (value > max_value) {
    return max_value;
  }
  return value;
}

/**
 * @brief 根据真实按下点计算朝返回方向移动的距离
 * @param state 边缘返回手势状态
 * @param point 当前触摸点
 * @return 朝返回方向移动的横向距离
 */
int GestureBackDistance(const EdgeBackSwipeState& state, lv_point_t point) {
  if (state.from_left_edge) {
    return point.x - state.gesture_start_point.x;
  }
  if (state.from_right_edge) {
    return state.gesture_start_point.x - point.x;
  }
  return 0;
}

/**
 * @brief 根据侧滑距离计算手势进度
 * @param state 边缘返回手势状态
 * @param point 当前触摸点
 * @return 0 到 1000 的进度值
 */
int GestureProgressPermille(
    const EdgeBackSwipeState& state, lv_point_t point) {
  int distance = GestureBackDistance(state, point);
  distance = ClampInt(distance, 0, kBackGestureMinSwipeDistance);
  return distance * 1000 / kBackGestureMinSwipeDistance;
}

/**
 * @brief 判断当前移动是否可以显示边缘返回指示器
 * @param back_distance 朝返回方向移动的横向距离
 * @param delta_y 垂直方向移动距离
 * @return 横向移动超过阈值且不是明显纵向滑动时返回 true
 */
bool IsBackGestureIndicatorReady(int back_distance, int delta_y) {
  return back_distance > kBackGestureIndicatorStartDistance &&
         back_distance >= AbsInt(delta_y);
}

/**
 * @brief 设置全局返回指示器及图标透明度
 * @param object 返回指示器对象
 * @param value 透明度数值
 */
void SetBackIndicatorOpacity(void* object, int32_t value) {
  if (object == nullptr) {
    return;
  }

  g_back_indicator_opacity =
      static_cast<lv_opa_t>(
          ClampInt(static_cast<int>(value), 0, LV_OPA_COVER));
  auto* indicator = static_cast<lv_obj_t*>(object);
  lv_obj_set_style_opa(indicator, g_back_indicator_opacity, LV_PART_MAIN);
  lv_obj_set_style_bg_opa(indicator, g_back_indicator_opacity, LV_PART_MAIN);
  if (g_back_indicator_icon != nullptr) {
    lv_obj_set_style_text_opa(
        g_back_indicator_icon, g_back_indicator_opacity, LV_PART_MAIN);
  }
}

/**
 * @brief 处理返回指示器淡出动画完成事件
 * @param animation LVGL 动画对象
 */
void BackIndicatorFadeCompletedCallback(lv_anim_t* animation) {
  auto* indicator = static_cast<lv_obj_t*>(lv_anim_get_user_data(animation));
  if (indicator != nullptr) {
    lv_obj_add_flag(indicator, LV_OBJ_FLAG_HIDDEN);
  }
}

/**
 * @brief 确保全局返回指示器对象已经创建
 * @return 创建或复用成功返回 true，否则返回 false
 */
bool EnsureBackIndicator() {
  if (g_back_indicator != nullptr && g_back_indicator_icon != nullptr) {
    return true;
  }

  lv_obj_t* parent = lv_layer_top();
  if (parent == nullptr) {
    return false;
  }

  g_back_indicator = lv_obj_create(parent);
  if (g_back_indicator == nullptr) {
    return false;
  }
  lv_obj_remove_flag(g_back_indicator, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(g_back_indicator, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_bg_color(
      g_back_indicator, lv_color_hex(kIndicatorColor), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(g_back_indicator, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_border_width(g_back_indicator, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(g_back_indicator, 0, LV_PART_MAIN);
  lv_obj_set_style_opa(g_back_indicator, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_add_flag(g_back_indicator, LV_OBJ_FLAG_HIDDEN);

  g_back_indicator_icon = lv_label_create(g_back_indicator);
  if (g_back_indicator_icon == nullptr) {
    lv_obj_delete(g_back_indicator);
    g_back_indicator = nullptr;
    return false;
  }
  lv_obj_set_style_text_color(
      g_back_indicator_icon, lv_color_hex(kIndicatorIconColor), LV_PART_MAIN);
  lv_obj_set_style_text_font(
      g_back_indicator_icon, &lvgl_font_material_symbols_fill_32,
      LV_PART_MAIN);
  lv_obj_set_style_text_align(
      g_back_indicator_icon, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
  lv_obj_set_style_text_opa(
      g_back_indicator_icon, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_remove_flag(g_back_indicator_icon, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(g_back_indicator_icon, LV_OBJ_FLAG_CLICKABLE);
  return true;
}

/**
 * @brief 根据当前手势状态刷新返回指示器位置和外观
 * @param state 边缘返回手势状态
 * @param screen_width 屏幕宽度
 * @param point 当前触摸点
 */
void UpdateBackIndicator(
    const EdgeBackSwipeState& state, int screen_width, lv_point_t point) {
  if (!EnsureBackIndicator()) {
    return;
  }

  lv_anim_delete(g_back_indicator, SetBackIndicatorOpacity);
  lv_obj_remove_flag(g_back_indicator, LV_OBJ_FLAG_HIDDEN);
  lv_obj_move_to_index(g_back_indicator, -1);

  const int progress = GestureProgressPermille(state, point);
  const int diameter =
      kIndicatorMinDiameter +
      (kIndicatorMaxDiameter - kIndicatorMinDiameter) * progress / 1000;
  const int visible_width =
      diameter * progress / 1000 / kIndicatorVisibleDivisor;
  const int opacity = kIndicatorMaxOpacity * progress / 1000;
  int parent_height = lv_obj_get_height(lv_layer_top());
  if (parent_height <= 0) {
    parent_height = lv_obj_get_height(lv_screen_active());
  }
  const int max_y = parent_height > diameter ? parent_height - diameter : 0;
  const int center_y = state.indicator_center_y;
  const int y = ClampInt(center_y - diameter / 2, 0, max_y);
  const int x = state.from_left_edge ? -(diameter - visible_width)
                                     : screen_width - visible_width;
  const int icon_offset =
      state.from_left_edge ? (diameter - visible_width) / 2
                           : -(diameter - visible_width) / 2;

  lv_obj_set_size(g_back_indicator, diameter, diameter);
  lv_obj_set_pos(g_back_indicator, x, y);
  lv_obj_set_style_radius(g_back_indicator, LV_RADIUS_CIRCLE, LV_PART_MAIN);
  SetBackIndicatorOpacity(g_back_indicator, opacity);

  lv_label_set_text(g_back_indicator_icon, icon::kChevronRight);
  lv_obj_update_layout(g_back_indicator_icon);
  lv_obj_set_style_transform_pivot_x(
      g_back_indicator_icon, lv_obj_get_width(g_back_indicator_icon) / 2,
      LV_PART_MAIN);
  lv_obj_set_style_transform_pivot_y(
      g_back_indicator_icon, lv_obj_get_height(g_back_indicator_icon) / 2,
      LV_PART_MAIN);
  lv_obj_set_style_transform_rotation(
      g_back_indicator_icon, state.from_left_edge ? 0 : 1800, LV_PART_MAIN);
  lv_obj_align(g_back_indicator_icon, LV_ALIGN_CENTER, icon_offset, 0);
}

/**
 * @brief 隐藏全局返回指示器
 * @param animated 是否使用淡出动画
 */
void HideBackIndicator(bool animated) {
  if (g_back_indicator == nullptr) {
    return;
  }

  lv_anim_delete(g_back_indicator, SetBackIndicatorOpacity);
  if (!animated || g_back_indicator_opacity == LV_OPA_TRANSP ||
      lv_obj_has_flag(g_back_indicator, LV_OBJ_FLAG_HIDDEN)) {
    SetBackIndicatorOpacity(g_back_indicator, LV_OPA_TRANSP);
    lv_obj_add_flag(g_back_indicator, LV_OBJ_FLAG_HIDDEN);
    return;
  }

  lv_anim_t animation;
  lv_anim_init(&animation);
  lv_anim_set_var(&animation, g_back_indicator);
  lv_anim_set_user_data(&animation, g_back_indicator);
  lv_anim_set_values(&animation, g_back_indicator_opacity, LV_OPA_TRANSP);
  lv_anim_set_duration(&animation, kIndicatorAnimationMs);
  lv_anim_set_path_cb(&animation, lv_anim_path_ease_out);
  lv_anim_set_exec_cb(&animation, SetBackIndicatorOpacity);
  lv_anim_set_completed_cb(&animation, BackIndicatorFadeCompletedCallback);
  if (lv_anim_start(&animation) == nullptr) {
    SetBackIndicatorOpacity(g_back_indicator, LV_OPA_TRANSP);
    lv_obj_add_flag(g_back_indicator, LV_OBJ_FLAG_HIDDEN);
  }
}

}  // namespace

bool HandleEdgeBackSwipeEvent(
    lv_event_t* event, int screen_width, EdgeBackSwipeState* state) {
  if (event == nullptr || state == nullptr || screen_width <= 0) {
    return false;
  }

  const lv_event_code_t code = lv_event_get_code(event);
  if (code != LV_EVENT_PRESSED && code != LV_EVENT_PRESSING &&
      code != LV_EVENT_RELEASED && code != LV_EVENT_PRESS_LOST) {
    return false;
  }

  lv_indev_t* indev = lv_indev_active();
  if (indev == nullptr) {
    return false;
  }

  lv_point_t point = {};
  lv_indev_get_point(indev, &point);

  if (code == LV_EVENT_PRESSED) {
    const int edge_width = BackGestureEdgeWidth(screen_width);
    const bool valid_x = IsPointInsideScreen(point, screen_width);
    const bool hardware_edge = hal::LvglPort::ActiveInputEdgeTouch();
    state->start_point = point;
    state->gesture_start_point = point;
    state->from_left_edge = valid_x && point.x <= edge_width;
    state->from_right_edge = valid_x && point.x >= screen_width - edge_width;
    if (hardware_edge && valid_x && !state->from_left_edge &&
        !state->from_right_edge) {
      state->from_left_edge = point.x < screen_width / 2;
      state->from_right_edge = !state->from_left_edge;
    }
    if (hardware_edge && state->from_left_edge) {
      state->start_point.x = 0;
    } else if (hardware_edge && state->from_right_edge) {
      state->start_point.x = screen_width - 1;
    }
    state->tracking = state->from_left_edge || state->from_right_edge;
    state->active = false;
    state->haptic_feedback_played = false;
    state->indicator_center_y = point.y;
    HideBackIndicator(false);
    return false;
  }

  if (!state->tracking) {
    return false;
  }

  const int delta_y = point.y - state->gesture_start_point.y;
  const int back_distance = GestureBackDistance(*state, point);
  if (code == LV_EVENT_PRESS_LOST &&
      lv_indev_get_state(indev) == LV_INDEV_STATE_PRESSED) {
    if (state->active) {
      UpdateBackIndicator(*state, screen_width, point);
    }
    return false;
  }

  if (code == LV_EVENT_PRESSING) {
    if (!state->active) {
      if (AbsInt(delta_y) > kBackGestureMaxVerticalOffset) {
        state->tracking = false;
        HideBackIndicator(true);
        return false;
      }
      if (!IsBackGestureIndicatorReady(back_distance, delta_y)) {
        HideBackIndicator(false);
        return false;
      }
      state->active = true;
    }
    UpdateBackIndicator(*state, screen_width, point);
    if (back_distance < kBackGestureMinSwipeDistance) {
      state->haptic_feedback_played = false;
    } else if (!state->haptic_feedback_played) {
      PlayUiHapticFeedback();
      state->haptic_feedback_played = true;
    }
    return false;
  }

  const bool was_active = state->active;
  state->tracking = false;
  state->active = false;
  state->haptic_feedback_played = false;
  HideBackIndicator(true);
  if (!was_active && AbsInt(delta_y) > kBackGestureMaxVerticalOffset) {
    return false;
  }
  if (!was_active) {
    return false;
  }

  if (back_distance >= kBackGestureMinSwipeDistance) {
    return true;
  }
  return false;
}

void AddEdgeBackSwipeEvents(
    lv_obj_t* object, lv_event_cb_t callback, void* user_data) {
  if (object == nullptr || callback == nullptr) {
    return;
  }

  lv_obj_add_flag(object, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_flag(object, LV_OBJ_FLAG_EVENT_BUBBLE);
  lv_obj_add_event_cb(object, callback, LV_EVENT_PRESSED, user_data);
  lv_obj_add_event_cb(object, callback, LV_EVENT_PRESSING, user_data);
  lv_obj_add_event_cb(object, callback, LV_EVENT_RELEASED, user_data);
  lv_obj_add_event_cb(object, callback, LV_EVENT_PRESS_LOST, user_data);
}

void EnableEdgeBackSwipeEventBubble(lv_obj_t* object) {
  if (object == nullptr) {
    return;
  }

  lv_obj_add_flag(object, LV_OBJ_FLAG_EVENT_BUBBLE);
  const uint32_t child_count = lv_obj_get_child_count(object);
  for (uint32_t i = 0; i < child_count; ++i) {
    EnableEdgeBackSwipeEventBubble(lv_obj_get_child(object, i));
  }
}

}  // namespace lilygo_box::ui
