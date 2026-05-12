/*
 * @Description: None
 * @Author: LILYGO_L
 * @Date: 2026-05-12 22:15:00
 * @LastEditTime: 2026-05-12 22:15:00
 * @License: GPL 3.0
 */
#include "ui/edge_back_gesture.h"

#include "lvgl_private.h"

namespace lilygo_box::ui {

bool ReadBackGestureInfo(const lv_indev_t* indev, BackGestureInfo* info) {
  if (indev == nullptr || info == nullptr) {
    return false;
  }

  lv_point_t point = {};
  lv_indev_get_point(indev, &point);

  info->direction = lv_indev_get_gesture_dir(indev);
  info->start_point.x = point.x - indev->pointer.gesture_sum.x;
  info->start_point.y = point.y - indev->pointer.gesture_sum.y;
  return true;
}

bool IsBackGestureFromEdge(
    const BackGestureInfo& info, int screen_width) {
  if (screen_width <= 0) {
    return false;
  }

  const int edge_width = BackGestureEdgeWidth(screen_width);
  if (info.direction == LV_DIR_RIGHT) {
    return info.start_point.x <= edge_width;
  }
  if (info.direction == LV_DIR_LEFT) {
    return info.start_point.x >= screen_width - edge_width;
  }
  return false;
}

}  // namespace lilygo_box::ui
