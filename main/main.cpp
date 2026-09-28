/*
 * @Description: LilyGoBox 应用程序入口与系统启动流程
 * @Author: LILYGO_L
 * @Date: 2026-05-10 13:27:05
 * @LastEditTime: 2026-09-28 09:09:17
 * @License: GPL 3.0
 */

#include "app/application.h"
#include "base/logger.h"
#include "cpp_bus_driver.h"
#include "lilygo_device_driver.h"

extern "C" void app_main() {
  static lilygo_box::Application app;
  const bool result = app.Init();
  if (!result) {
    lilygo_box::LogMessage(
        lilygo_box::LogLevel::kError, __FILE__, __LINE__, "Init failed\n");
    // 初始化未完成，跳过依赖 UI 和存储的准备，进入统一系统重启流程。
    app.RestartDevice(false);
    return;
  }
  app.Run();
}
