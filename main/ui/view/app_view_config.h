/*
 * @Description: None
 * @Author: LILYGO_L
 * @Date: 2026-05-10 13:27:05
 * @LastEditTime: 2026-05-10 13:27:05
 * @License: GPL 3.0
 */
#pragma once

#include "lvgl.h"

namespace lilygo_box::hal {
class DeviceDiagnosticsProvider;
class ScreenDevice;
}  // namespace lilygo_box::hal

namespace lilygo_box::ui {

struct AppViewConfig {
  int width = 0;
  int height = 0;
  hal::ScreenDevice* screen = nullptr;
  hal::DeviceDiagnosticsProvider* diagnostics = nullptr;
  lv_event_cb_t back_callback = nullptr;
  void* back_context = nullptr;
};

}  // namespace lilygo_box::ui
