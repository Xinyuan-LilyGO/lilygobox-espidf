/*
 * @Description: None
 * @Author: LILYGO_L
 * @Date: 2026-05-15 10:18:00
 * @LastEditTime: 2026-05-15 10:18:00
 * @License: GPL 3.0
 */
#include "ui/input/press_cancel.h"

#include <cstdlib>
#include <new>

namespace lilygo_box::ui {
namespace {

constexpr int kPressCancelDragThreshold = 12;

// 按压取消状态
struct PressCancelState {
  lv_point_t start_point = {};
  bool has_start_point = false;
  bool cancelled = false;
};

/**
 * @brief 判断指针移动距离是否已经达到拖动阈值
 * @param state 按压取消状态
 * @return 达到拖动阈值返回 true，否则返回 false
 */
bool HasPointerExceededDragThreshold(const PressCancelState& state) {
  if (!state.has_start_point) {
    return false;
  }

  lv_indev_t* indev = lv_indev_active();
  if (indev == nullptr) {
    return false;
  }

  lv_point_t point = {};
  lv_indev_get_point(indev, &point);
  const int delta_x = std::abs(point.x - state.start_point.x);
  const int delta_y = std::abs(point.y - state.start_point.y);
  return delta_x >= kPressCancelDragThreshold ||
         delta_y >= kPressCancelDragThreshold;
}

/**
 * @brief 处理按压移出或滑动取消事件
 * @param event LVGL 事件
 */
void PressCancelOnLeaveEventCallback(lv_event_t* event) {
  auto* state = static_cast<PressCancelState*>(lv_event_get_user_data(event));
  if (state == nullptr) {
    return;
  }

  const lv_event_code_t code = lv_event_get_code(event);
  if (code == LV_EVENT_DELETE) {
    delete state;
    return;
  }

  auto* object = static_cast<lv_obj_t*>(lv_event_get_current_target(event));
  if (object == nullptr) {
    return;
  }

  if (code == LV_EVENT_PRESSED) {
    state->cancelled = false;
    state->has_start_point = false;
    lv_indev_t* indev = lv_indev_active();
    if (indev != nullptr) {
      lv_indev_get_point(indev, &state->start_point);
      state->has_start_point = true;
    }
    return;
  }

  if (code == LV_EVENT_PRESSING) {
    if (HasPointerExceededDragThreshold(*state) ||
        !IsPointerInsideObject(object)) {
      state->cancelled = true;
      lv_obj_remove_state(object, LV_STATE_PRESSED);
    }
    return;
  }

  if (code == LV_EVENT_RELEASED) {
    if (HasPointerExceededDragThreshold(*state) ||
        !IsPointerInsideObject(object)) {
      state->cancelled = true;
    }
    state->has_start_point = false;
    lv_obj_remove_state(object, LV_STATE_PRESSED);
    return;
  }

  if (code == LV_EVENT_PRESS_LOST) {
    state->cancelled = true;
    state->has_start_point = false;
    lv_obj_remove_state(object, LV_STATE_PRESSED);
    return;
  }

  if (code != LV_EVENT_CLICKED) {
    return;
  }

  const bool click_cancelled =
      state->cancelled || !IsPointerInsideObject(object);
  state->cancelled = false;
  state->has_start_point = false;
  lv_obj_remove_state(object, LV_STATE_PRESSED);
  if (!click_cancelled) {
    return;
  }

  lv_event_stop_bubbling(event);
  lv_event_stop_processing(event);
}

}  // namespace

bool IsPointerInsideObject(lv_obj_t* object) {
  if (object == nullptr) {
    return false;
  }

  lv_indev_t* indev = lv_indev_active();
  if (indev == nullptr) {
    return false;
  }

  lv_point_t point = {};
  lv_indev_get_point(indev, &point);

  lv_area_t coords = {};
  lv_obj_get_coords(object, &coords);
  return point.x >= coords.x1 && point.x <= coords.x2 &&
         point.y >= coords.y1 && point.y <= coords.y2;
}

bool AddPressCancelOnLeave(lv_obj_t* object) {
  if (object == nullptr) {
    return false;
  }

  auto* state = new (std::nothrow) PressCancelState();
  if (state == nullptr) {
    return false;
  }

  lv_obj_add_event_cb(
      object, PressCancelOnLeaveEventCallback, LV_EVENT_ALL, state);
  return true;
}

}  // namespace lilygo_box::ui
