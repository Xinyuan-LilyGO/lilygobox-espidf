/*
 * @Description: None
 * @Author: LILYGO_L
 * @Date: 2026-05-10 13:27:05
 * @LastEditTime: 2026-05-15 11:33:18
 * @License: GPL 3.0
 */
#include "app/application.h"
#include "base/logger.h"

extern "C" void app_main() {
  lilygo_box::Application app;
  const bool result = app.Init();
  if (!result) {
    lilygo_box::LogMessage(lilygo_box::LogLevel::kError, __FILE__, __LINE__,
        "Application::Init failed\n");
  }
  app.Run();
}
