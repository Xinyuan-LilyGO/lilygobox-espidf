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
#include "hal/device_provider_factory.h"

namespace lilygo_box {

Application::Application()
    : device_provider_context_(hal::CreateDeviceProviderContext()) {}

bool Application::Init() {
  hal::DeviceProvider* device = device_provider_context_.device;
  if (device == nullptr) {
    LogMessage(LogLevel::kError, __FILE__, __LINE__,
               "No device provider selected\n");
    return false;
  }

  hal::ScreenProvider* screen = device_provider_context_.screen.get();
  if (screen == nullptr) {
    LogMessage(LogLevel::kError, __FILE__, __LINE__,
               "No screen provider selected\n");
    return false;
  }

  bool result = device->InitDevice();
  if (!result) {
    LogMessage(LogLevel::kError, __FILE__, __LINE__,
               "DeviceProvider::InitDevice failed\n");
    return false;
  }

  result = lvgl_port_.Init(screen);
  if (!result) {
    LogMessage(LogLevel::kError, __FILE__, __LINE__,
               "LvglPort::Init failed\n");
    return false;
  }

  result = ui_manager_.Init(screen, device_provider_context_.diagnostics,
      device_provider_context_.gps, device_provider_context_.audio,
      device_provider_context_.haptic, device_provider_context_.bmu,
      device_provider_context_.rtc, device_provider_context_.imu,
      device_provider_context_.ethernet, device_provider_context_.wifi);
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
  screen->StartScreenBacklight();

  if (device_provider_context_.ethernet != nullptr &&
      !device_provider_context_.ethernet->StartEthernet()) {
    LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
               "EthernetProvider::StartEthernet failed\n");
  }

  if (device_provider_context_.wifi != nullptr &&
      !device_provider_context_.wifi->StartWifi()) {
    LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
               "WifiProvider::StartWifi failed\n");
  }

  LogMessage(LogLevel::kInfo, __FILE__, __LINE__,
             "LilygoBox initialized on %s\n", screen->ScreenName());
  return true;
}

void Application::Run() {
  while (true) {
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}

}  // namespace lilygo_box
