/*
 * @Description: None
 * @Author: LILYGO_L
 * @Date: 2026-05-10 13:27:05
 * @LastEditTime: 2026-06-15 13:57:35
 * @License: GPL 3.0
 */
#include "app/application.h"

#include <algorithm>
#include <cstdint>

#include "app/storage/audio_storage.h"
#include "app/storage/display_storage.h"
#include "app/wifi_manager.h"
#include "base/logger.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "hal/device_provider_factory.h"
#include "nvs_flash.h"

namespace lilygo_box {
namespace {

constexpr uint32_t kStartupWifiAutoConnectWaitMs = 15 * 1000;
constexpr uint32_t kStartupWifiAutoConnectPollMs = 200;
constexpr uint32_t kStartupWifiAutoConnectTaskStackBytes = 4 * 1024;
constexpr UBaseType_t kStartupWifiAutoConnectTaskPriority = 3;
constexpr uint32_t kScreenLockTaskStackBytes = 4 * 1024;
constexpr UBaseType_t kScreenLockTaskPriority = 3;
constexpr uint32_t kScreenLockPollMs = 100;
constexpr uint32_t kScreenLockSleepConfirmMs = 3 * 1000;
constexpr uint32_t kScreenLockFadeMs = 300;
constexpr int kScreenLockFadeStepCount = 12;

}  // namespace

Application::Application()
    : device_provider_context_(hal::CreateDeviceProviderContext()) {}

bool Application::Init() {
  esp_err_t nvs_result = nvs_flash_init();
  if (nvs_result == ESP_ERR_NVS_NO_FREE_PAGES ||
      nvs_result == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    nvs_flash_erase();
    nvs_result = nvs_flash_init();
  }
  if (nvs_result != ESP_OK) {
    LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
        "NVS init failed (error code: %#X)\n", nvs_result);
  }

  hal::DeviceProvider* device = device_provider_context_.device;
  if (device == nullptr) {
    LogMessage(
        LogLevel::kError, __FILE__, __LINE__, "No device provider selected\n");
    return false;
  }

  hal::ScreenProvider* screen = device_provider_context_.screen.get();
  if (screen == nullptr) {
    LogMessage(
        LogLevel::kError, __FILE__, __LINE__, "No screen provider selected\n");
    return false;
  }

  bool result = device->InitDevice();
  if (!result) {
    LogMessage(LogLevel::kError, __FILE__, __LINE__, "InitDevice failed\n");
    return false;
  }

  result = lvgl_port_.Init(screen);
  if (!result) {
    LogMessage(LogLevel::kError, __FILE__, __LINE__, "Init failed\n");
    return false;
  }

  result = ui_manager_.Init(screen, device_provider_context_.diagnostics,
      device_provider_context_.device_info, device_provider_context_.gps,
      device_provider_context_.audio, device_provider_context_.haptic,
      device_provider_context_.bmu, device_provider_context_.rtc,
      device_provider_context_.imu,
      device_provider_context_.ethernet, device_provider_context_.wifi);
  if (!result) {
    LogMessage(LogLevel::kError, __FILE__, __LINE__, "Init failed\n");
    return false;
  }

  result = lvgl_port_.Start();
  if (!result) {
    LogMessage(LogLevel::kError, __FILE__, __LINE__, "Start failed\n");
    return false;
  }
  app::DisplayPreferences display_preferences;
  const bool has_display_preferences =
      app::LoadDisplayPreferencesFromNvs(&display_preferences);
  current_screen_brightness_percent_.store(
      has_display_preferences ? display_preferences.brightness_percent : 100);
  screen->StartScreenBacklight(current_screen_brightness_percent_.load());
  app::AudioPreferences audio_preferences;
  if (device_provider_context_.audio != nullptr &&
      app::LoadAudioPreferencesFromNvs(&audio_preferences)) {
    device_provider_context_.audio->SetSpeakerVolumePercent(
        audio_preferences.volume_percent);
  }

  lvgl_port_.Lock();
  const bool startup_result = ui_manager_.StartStartupScreenAnimation();
  lvgl_port_.Unlock();
  if (!startup_result) {
    LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
        "StartStartupScreenAnimation failed\n");
  }

  lvgl_port_.Lock();
  ui_manager_.RefreshSystemStatusNow();
  ui_manager_.SetStartupScreenProgress(33);
  lvgl_port_.Unlock();

  if (device_provider_context_.wifi != nullptr &&
      !device_provider_context_.wifi->StartWifi()) {
    LogMessage(LogLevel::kWarning, __FILE__, __LINE__, "StartWifi failed\n");
  }
  if (device_provider_context_.wifi != nullptr) {
    const BaseType_t task_result =
        xTaskCreate(StartupWifiAutoConnectTaskEntry, "wifi_auto",
            kStartupWifiAutoConnectTaskStackBytes, this,
            kStartupWifiAutoConnectTaskPriority, nullptr);
    if (task_result != pdPASS) {
      LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
          "Create startup WiFi auto connect task failed\n");
    }
  }
  lvgl_port_.Lock();
  ui_manager_.SetStartupScreenProgress(66);
  lvgl_port_.Unlock();

  if (device_provider_context_.ethernet != nullptr &&
      !device_provider_context_.ethernet->StartEthernet()) {
    LogMessage(
        LogLevel::kWarning, __FILE__, __LINE__, "StartEthernet failed\n");
  }
  lvgl_port_.Lock();
  ui_manager_.SetStartupScreenProgress(100);
  lvgl_port_.Unlock();

  const char* device_model_name = "unknown";
  hal::DeviceInfo init_device_info;
  if (device_provider_context_.device_info != nullptr &&
      device_provider_context_.device_info->ReadDeviceInfo(&init_device_info) &&
      init_device_info.device_model_name != nullptr &&
      init_device_info.device_model_name[0] != '\0') {
    device_model_name = init_device_info.device_model_name;
  }

  const BaseType_t screen_lock_task_result =
      xTaskCreate(ScreenLockTaskEntry, "screen_lock", kScreenLockTaskStackBytes,
          this, kScreenLockTaskPriority, nullptr);
  if (screen_lock_task_result != pdPASS) {
    LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
        "Create screen lock task failed\n");
  }

  LogMessage(LogLevel::kInfo, __FILE__, __LINE__,
      "LilygoBox initialized on %s\n", device_model_name);
  return true;
}

void Application::Run() {
  while (true) {
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}

void Application::StartupWifiAutoConnectTaskEntry(void* context) {
  auto* self = static_cast<Application*>(context);
  if (self != nullptr) {
    self->RunStartupWifiAutoConnectTask();
  }
  vTaskDelete(nullptr);
}

void Application::ScreenLockTaskEntry(void* context) {
  auto* self = static_cast<Application*>(context);
  if (self != nullptr) {
    self->RunScreenLockTask();
  }
  vTaskDelete(nullptr);
}

void Application::RunStartupWifiAutoConnectTask() {
  app::WifiAutoConnectOptions options;
  options.start_driver_if_needed = true;
  options.wait_for_driver = true;
  options.wait_timeout_ms = kStartupWifiAutoConnectWaitMs;
  options.poll_interval_ms = kStartupWifiAutoConnectPollMs;
  const app::WifiAutoConnectResult result =
      app::TryStartWifiAutoConnect(device_provider_context_.wifi, options);
  if (result == app::WifiAutoConnectResult::kFailed) {
    LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
        "Startup WiFi auto connect failed to start\n");
  }
}

void Application::RunScreenLockTask() {
  hal::ScreenProvider* screen = device_provider_context_.screen.get();
  if (screen == nullptr) {
    return;
  }

  while (ui_manager_.IsStartupScreenActive()) {
    vTaskDelay(pdMS_TO_TICKS(kScreenLockPollMs));
  }

  uint32_t last_touch_ms = static_cast<uint32_t>(xTaskGetTickCount() *
      portTICK_PERIOD_MS);
  bool was_wake_button_pressed = screen->IsLockWakeButtonPressed();
  while (true) {
    const uint32_t now_ms = static_cast<uint32_t>(xTaskGetTickCount() *
        portTICK_PERIOD_MS);
    hal::TouchPoint point;
    const bool wake_button_pressed = screen->IsLockWakeButtonPressed();
    const bool wake_button_clicked =
        wake_button_pressed && !was_wake_button_pressed;
    was_wake_button_pressed = wake_button_pressed;
    if (screen_locked_.load()) {
      if (wake_button_clicked) {
        WakeScreenFromLock();
        last_touch_ms = now_ms;
      }
      vTaskDelay(pdMS_TO_TICKS(kScreenLockPollMs));
      continue;
    }

    if (wake_button_clicked) {
      lvgl_port_.SetInputBlocked(true);
      if (screen->EnterDeviceSleep()) {
        screen_locked_.store(true);
      } else {
        lvgl_port_.SetInputBlocked(false);
        last_touch_ms = now_ms;
      }
      vTaskDelay(pdMS_TO_TICKS(kScreenLockPollMs));
      continue;
    }

    if (screen->ReadScreenTouch(&point)) {
      last_touch_ms = now_ms;
      vTaskDelay(pdMS_TO_TICKS(kScreenLockPollMs));
      continue;
    }

    const app::DisplayPreferences preferences =
        LoadDisplayPreferencesOrDefault();
    const uint32_t lock_timeout_ms =
        static_cast<uint32_t>(preferences.lock_timeout_seconds) * 1000;
    const uint32_t sleep_confirm_ms =
        std::min(lock_timeout_ms, kScreenLockSleepConfirmMs);
    const uint32_t dim_start_ms = lock_timeout_ms - sleep_confirm_ms;
    const uint32_t idle_ms = now_ms - last_touch_ms;
    if (preferences.lock_timeout_seconds <= 0 || idle_ms < dim_start_ms) {
      vTaskDelay(pdMS_TO_TICKS(kScreenLockPollMs));
      continue;
    }

    const int start_brightness = preferences.brightness_percent;
    bool fade_canceled = false;
    lvgl_port_.SetInputBlocked(true);
    for (int step = 1; step <= kScreenLockFadeStepCount; ++step) {
      const int brightness =
          start_brightness * (kScreenLockFadeStepCount - step) /
          kScreenLockFadeStepCount;
      screen->SetScreenBrightnessPercent(brightness);
      current_screen_brightness_percent_.store(brightness);
      vTaskDelay(pdMS_TO_TICKS(
          std::max<uint32_t>(1, kScreenLockFadeMs / kScreenLockFadeStepCount)));
      if (screen->ReadScreenTouch(&point)) {
        FadeScreenBrightnessTo(start_brightness);
        lvgl_port_.SetInputBlocked(false);
        last_touch_ms = static_cast<uint32_t>(xTaskGetTickCount() *
            portTICK_PERIOD_MS);
        fade_canceled = true;
        break;
      }
    }
    if (fade_canceled) {
      continue;
    }

    const uint32_t confirm_start_ms = static_cast<uint32_t>(
        xTaskGetTickCount() * portTICK_PERIOD_MS);
    while (static_cast<uint32_t>(xTaskGetTickCount() * portTICK_PERIOD_MS) -
               confirm_start_ms <
           sleep_confirm_ms) {
      if (screen->ReadScreenTouch(&point)) {
        FadeScreenBrightnessTo(start_brightness);
        lvgl_port_.SetInputBlocked(false);
        last_touch_ms = static_cast<uint32_t>(xTaskGetTickCount() *
            portTICK_PERIOD_MS);
        fade_canceled = true;
        break;
      }
      vTaskDelay(pdMS_TO_TICKS(kScreenLockPollMs));
    }
    if (fade_canceled) {
      continue;
    }

    if (screen->EnterDeviceSleep()) {
      screen_locked_.store(true);
    } else {
      FadeScreenBrightnessTo(start_brightness);
      lvgl_port_.SetInputBlocked(false);
      last_touch_ms = static_cast<uint32_t>(xTaskGetTickCount() *
          portTICK_PERIOD_MS);
    }
  }
}

void Application::WakeScreenFromLock() {
  hal::ScreenProvider* screen = device_provider_context_.screen.get();
  if (screen == nullptr) {
    return;
  }

  const app::DisplayPreferences preferences = LoadDisplayPreferencesOrDefault();
  if (!screen->ExitDeviceSleep()) {
    return;
  }
  screen->SetScreenBrightnessPercent(preferences.brightness_percent);
  current_screen_brightness_percent_.store(preferences.brightness_percent);
  lvgl_port_.SetInputBlocked(false);
  screen_locked_.store(false);
}

void Application::FadeScreenBrightnessTo(int target_percent) {
  hal::ScreenProvider* screen = device_provider_context_.screen.get();
  if (screen == nullptr) {
    return;
  }

  const int start_percent = current_screen_brightness_percent_.load();
  for (int step = 1; step <= kScreenLockFadeStepCount; ++step) {
    const int brightness = start_percent +
        (target_percent - start_percent) * step / kScreenLockFadeStepCount;
    screen->SetScreenBrightnessPercent(brightness);
    vTaskDelay(pdMS_TO_TICKS(
        std::max<uint32_t>(1, kScreenLockFadeMs / kScreenLockFadeStepCount)));
  }
  current_screen_brightness_percent_.store(target_percent);
}

app::DisplayPreferences Application::LoadDisplayPreferencesOrDefault() const {
  app::DisplayPreferences preferences;
  app::LoadDisplayPreferencesFromNvs(&preferences);
  return preferences;
}

}  // namespace lilygo_box
