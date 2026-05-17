/*
 * @Description: None
 * @Author: LILYGO_L
 * @Date: 2026-05-10 13:27:05
 * @LastEditTime: 2026-05-12 23:05:00
 * @License: GPL 3.0
 */
#pragma once

#include "hal/lvgl_port.h"
#include "hal/device_provider_factory.h"
#include "ui/ui_manager.h"

namespace lilygo_box {

class Application final {
 public:
  Application();

  /**
   * @brief 初始化当前设备、LVGL 和本地 UI
   * @return 初始化成功返回 true，否则返回 false
   */
  bool Init();

  /**
   * @brief 运行应用主循环
   */
  void Run();

 private:
  hal::DeviceProviderContext device_provider_context_;
  hal::LvglPort lvgl_port_;
  ui::UiManager ui_manager_;
};

}  // namespace lilygo_box
