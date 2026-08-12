/*
 * @Description: LilyGoBox 应用程序入口与系统启动流程
 * @Author: LILYGO_L
 * @Date: 2026-05-10 13:27:05
 * @LastEditTime: 2026-08-12 09:47:08
 * @License: GPL 3.0
 */
#include "app/application.h"
#include "base/logger.h"
#include "cpp_bus_driver_library.h"
#include "lilygo_device_driver_library.h"

extern "C" void app_main() {
  // lilygo_box::SetMinimumLogLevel(lilygo_box::LogLevel::kDebug);
  // cpp_bus_driver::Tool::SetMinimumLogLevel(
  //     cpp_bus_driver::Tool::LogLevel::kDebug);
  // lilygo_device_driver::SetMinimumLogLevel(
  //     lilygo_device_driver::LogLevel::kDebug);

  lilygo_box::Application app;
  const bool result = app.Init();
  if (!result) {
    lilygo_box::LogMessage(
        lilygo_box::LogLevel::kError, __FILE__, __LINE__, "Init failed\n");
  }
  app.Run();
}
