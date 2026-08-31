/*
 * @Description: LilyGoBox 应用程序入口与系统启动流程
 * @Author: LILYGO_L
 * @Date: 2026-05-10 13:27:05
 * @LastEditTime: 2026-08-12 09:47:08
 * @License: GPL 3.0
 */

#include <cstdint>

#include "app/application.h"
#include "base/logger.h"
#include "cpp_bus_driver_library.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lilygo_device_driver_library.h"

namespace {

constexpr uint32_t kInitializationFailureRestartDelayMs = 1000;

/**
 * @brief 在应用初始化失败后通过最小系统路径重新启动
 *
 * 此时设备、屏幕或 LVGL 可能尚未初始化完成，不能调用依赖 UI 和存储
 * 收尾流程的 Application::RestartDevice()。短暂等待日志输出后直接复位，
 * 同时保留 bootloader 对待验证 OTA 固件的回滚保护。
 */
[[noreturn]] void RestartAfterInitializationFailure() {
  lilygo_box::LogMessage(lilygo_box::LogLevel::kInfo, __FILE__, __LINE__,
      "Application initialization failed; restarting system\n");
  vTaskDelay(pdMS_TO_TICKS(kInitializationFailureRestartDelayMs));
  esp_restart();
  while (true) {
    vTaskDelay(portMAX_DELAY);
  }
}

}  // namespace

extern "C" void app_main() {
  // lilygo_box::SetMinimumLogLevel(lilygo_box::LogLevel::kDebug);
  // cpp_bus_driver::Tool::SetMinimumLogLevel(
  //     cpp_bus_driver::Tool::LogLevel::kDebug);
  // lilygo_device_driver::SetMinimumLogLevel(
  //     lilygo_device_driver::LogLevel::kDebug);

  static lilygo_box::Application app;
  const bool result = app.Init();
  if (!result) {
    lilygo_box::LogMessage(
        lilygo_box::LogLevel::kError, __FILE__, __LINE__, "Init failed\n");
    RestartAfterInitializationFailure();
  }
  app.Run();
}
