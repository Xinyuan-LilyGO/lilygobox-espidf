/*
 * @Description: 屏幕多点触摸读取默认适配实现
 * @Author: LILYGO_L
 * @Date: 2026-05-10 13:27:05
 * @LastEditTime: 2026-05-14 00:45:00
 * @License: GPL 3.0
 */
#include "hal/providers/screen_provider.h"

namespace lilygo_box::hal {

bool ScreenProvider::ReadScreenTouchPoints(
    TouchPoint* points, size_t max_points, size_t* point_count) {
  if (point_count != nullptr) {
    *point_count = 0;
  }
  if (points == nullptr || max_points == 0 || point_count == nullptr) {
    return false;
  }

  TouchPoint point;
  if (!ReadScreenTouch(&point)) {
    return false;
  }

  points[0] = point;
  *point_count = 1;
  return true;
}

}  // namespace lilygo_box::hal
