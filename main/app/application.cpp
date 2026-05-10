/*
 * @Description: None
 * @Author: LILYGO_L
 * @Date: 2026-05-10 13:27:05
 * @LastEditTime: 2026-05-10 13:27:05
 * @License: GPL 3.0
 */
#include "app/application.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "base/logger.h"
#include "hal/screen_device_factory.h"

namespace lilygo_box {

Application::Application()
    : screen_device_(hal::CreateScreenDevice()) {}

bool Application::Init() {
  if (screen_device_ == nullptr) {
    LogMessage(LogLevel::kError, __FILE__, __LINE__,
               "No screen device selected\n");
    return false;
  }

  bool result = screen_device_->Init();
  if (!result) {
    LogMessage(LogLevel::kError, __FILE__, __LINE__,
               "ScreenDevice::Init failed\n");
    return false;
  }

  result = lvgl_port_.Init(screen_device_.get());
  if (!result) {
    LogMessage(LogLevel::kError, __FILE__, __LINE__,
               "LvglPort::Init failed\n");
    return false;
  }

  result = ui_manager_.Init(screen_device_.get());
  if (!result) {
    LogMessage(LogLevel::kError, __FILE__, __LINE__,
               "UiManager::Init failed\n");
    return false;
  }

  result = lvgl_port_.Start();
  if (!result) {
    LogMessage(LogLevel::kError, __FILE__, __LINE__,
               "LvglPort::Start failed\n");
    return false;
  }
  screen_device_->StartBacklight();

  LogMessage(LogLevel::kInfo, __FILE__, __LINE__,
             "LilygoBox initialized on %s\n", screen_device_->name());
  return true;
}

void Application::Run() {
  while (true) {
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}

}  // namespace lilygo_box
