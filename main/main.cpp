/*
 * @Description: None
 * @Author: LILYGO_L
 * @Date: 2026-05-10 13:27:05
 * @LastEditTime: 2026-05-10 23:48:45
 * @License: GPL 3.0
 */
#include "app/application.h"
#include "base/logger.h"

/**
 * @brief 应用入口，初始化并运行 LilygoBox 应用
 * @return
 * @Date 2026-05-13 09:55:00
 */
extern "C" void app_main() {
  lilygo_box::Application app;
  const bool result = app.Init();
  if (!result) {
    lilygo_box::LogMessage(lilygo_box::LogLevel::kError, __FILE__, __LINE__,
        "Application::Init failed\n");
  }
  app.Run();
}
