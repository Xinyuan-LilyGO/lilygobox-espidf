/*
 * @Description: 边缘返回手势公共接口
 * @Author: LILYGO_L
 * @Date: 2026-05-12 22:15:00
 * @LastEditTime: 2026-05-18 12:00:00
 * @License: GPL 3.0
 */
#include "ui/input/edge_back_gesture.h"

#include <cstdint>

namespace lilygo_box::ui {
namespace {

/**
 * @brief 返回整数绝对值
 * @param value 整数值
 * @return 绝对值
 */
int AbsInt(int value) {
  return value < 0 ? -value : value;
}

}  // namespace

bool HandleEdgeBackSwipeEvent(
    lv_event_t* event, int screen_width, EdgeBackSwipeState* state) {
  if (event == nullptr || state == nullptr || screen_width <= 0) {
    return false;
  }

  const lv_event_code_t code = lv_event_get_code(event);
  if (code != LV_EVENT_PRESSED && code != LV_EVENT_RELEASED &&
      code != LV_EVENT_PRESS_LOST) {
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
    state->start_point = point;
    state->from_left_edge = point.x <= edge_width;
    state->from_right_edge = point.x >= screen_width - edge_width;
    state->tracking = state->from_left_edge || state->from_right_edge;
    return false;
  }

  if (!state->tracking) {
    return false;
  }

  state->tracking = false;
  const int delta_x = point.x - state->start_point.x;
  const int delta_y = point.y - state->start_point.y;
  if (AbsInt(delta_y) > kBackGestureMaxVerticalOffset) {
    return false;
  }

  if (state->from_left_edge && delta_x >= kBackGestureMinSwipeDistance) {
    return true;
  }
  if (state->from_right_edge && -delta_x >= kBackGestureMinSwipeDistance) {
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
