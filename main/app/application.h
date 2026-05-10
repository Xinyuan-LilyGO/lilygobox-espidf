/*
 * @Description: None
 * @Author: LILYGO_L
 * @Date: 2026-05-10 13:27:05
 * @LastEditTime: 2026-05-10 23:51:34
 * @License: GPL 3.0
 */
#pragma once

#include <memory>

#include "hal/lvgl_port.h"
#include "hal/screen_device.h"
#include "ui/ui_manager.h"

namespace lilygo_box {

// Owns the top-level startup sequence for the selected device, LVGL port, and
// initial UI. Platform-specific details stay behind the HAL objects.
class Application final {
 public:
  Application();

  /**
   * @brief 初始化当前设备、LVGL 和本地 UI
   * @return 初始化成功返回 true，否则返回 false
   * @Date 2026-05-10 13:01:03
   */
  bool Init();

  /**
   * @brief 运行应用主循环
   * @return
   * @Date 2026-05-10 13:01:03
   */
  void Run();

 private:
  std::unique_ptr<hal::ScreenDevice> screen_device_;
  hal::LvglPort lvgl_port_;
  ui::UiManager ui_manager_;
};

}  // namespace lilygo_box
