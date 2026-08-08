/*
 * @Description: 系统应用初始化、任务调度与电源状态管理实现
 * @Author: LILYGO_L
 * @Date: 2026-05-10 13:27:05
 * @LastEditTime: 2026-07-30 18:00:00
 * @License: GPL 3.0
 */
#include "app/application.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <ctime>

#include "app/firmware_update_manager.h"
#include "app/network_monitor.h"
#include "app/storage/display_storage.h"
#include "app/storage/first_boot_storage.h"
#include "app/storage/littlefs_storage.h"
#include "app/storage/sound_storage.h"
#include "app/storage/storage.h"
#include "app/wifi_manager.h"
#include "base/logger.h"
#include "esp_err.h"
#include "esp_sleep.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "hal/device_provider_factory.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "ui/haptic_feedback.h"
#include "ui/resources/fonts/icon_assets.h"
#include "ui/views/settings/settings_view_internal.h"

namespace lilygo_box {
namespace {

constexpr uint32_t kStartupWifiAutoConnectWaitMs = 15 * 1000;
constexpr uint32_t kStartupWifiAutoConnectPollMs = 200;
constexpr uint32_t kWifiAutoConnectIdleMs = 2 * 1000;
constexpr uint32_t kWifiAutoConnectFailureRetryMs = 30 * 1000;
constexpr uint32_t kBatteryMonitorPeriodMs = 1000;
constexpr uint32_t kStartupWifiAutoConnectTaskStackBytes = 8 * 1024;
constexpr UBaseType_t kStartupWifiAutoConnectTaskPriority = 3;
constexpr uint32_t kScreenLockTaskStackBytes = 6 * 1024;
constexpr UBaseType_t kScreenLockTaskPriority = 3;
constexpr uint32_t kScreenLockPollMs = 100;
constexpr uint32_t kScreenOffTouchPollMs = 30;
constexpr uint32_t kScreenLockSleepConfirmMs = 3 * 1000;
constexpr uint32_t kAwakeLockScreenSleepTimeoutMs = 10 * 1000;
constexpr uint32_t kLowBatteryStartupWarningMs = 10 * 1000;
constexpr uint32_t kScreenStartupFadeMs = 500;
constexpr uint32_t kScreenLockFadeMs = 300;
constexpr uint32_t kScreenBrightnessTransitionWaitMs = 10;
constexpr int kScreenLockFadeStepCount = 12;
constexpr int kScreenUnlockSwipeMinDistance = 120;
constexpr uint32_t kScreenUnlockAnimationWaitMs = 240;
constexpr int kScreenUnlockSwipeMaxHorizontalDrift = 90;
constexpr uint32_t kDoubleTapMaximumTapMs = 300;
constexpr uint32_t kDoubleTapMaximumIntervalMs = 450;
constexpr int kDoubleTapMaximumDistance = 80;
constexpr uint32_t kPowerActionPreSleepSettleMs = 30;
constexpr int kLowBatteryStartupThresholdPercent = 10;
constexpr uint32_t kLowBatteryStartupIconColor = 0xFF3B30;
constexpr uint32_t kBatteryFaultStartupIconColor = 0xFF9500;
constexpr char kNvsPartitionName[] = "nvs";
constexpr char kChinaTimeZone[] = "CST-8";

/**
 * @brief 判断两个触摸点是否属于同一次双击区域
 * @param left 第一个触摸点
 * @param right 第二个触摸点
 * @return 两个触摸点距离在允许范围内返回 true
 */
bool AreTouchPointsNearby(
    const hal::TouchPoint& left, const hal::TouchPoint& right) {
  const int x_distance = left.x - right.x;
  const int y_distance = left.y - right.y;
  return x_distance * x_distance + y_distance * y_distance <=
      kDoubleTapMaximumDistance * kDoubleTapMaximumDistance;
}

/**
 * @brief 将系统本地时区设置为中国标准时间 UTC+8
 * @return 时区环境设置成功返回 true，否则返回 false
 */
bool ConfigureChinaTimeZone() {
  if (setenv("TZ", kChinaTimeZone, 1) != 0) {
    return false;
  }
  tzset();
  return true;
}

/**
 * @brief 输出默认 NVS 分区的容量与命名空间统计信息
 */
void LogNvsStorageInfo() {
  nvs_stats_t statistics = {};
  const esp_err_t result =
      nvs_get_stats(kNvsPartitionName, &statistics);
  if (result != ESP_OK) {
    LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
        "Read NVS storage statistics failed: %s\n",
        esp_err_to_name(result));
    return;
  }
  const size_t usage_percent =
      statistics.total_entries == 0
          ? 0
          : statistics.used_entries * 100U / statistics.total_entries;
  LogMessage(LogLevel::kInfo, __FILE__, __LINE__,
      "NVS storage initialized: partition=%s, used=%u entries, "
      "free=%u entries, available=%u entries, total=%u entries, "
      "namespaces=%u, usage=%u%%\n",
      kNvsPartitionName, static_cast<unsigned>(statistics.used_entries),
      static_cast<unsigned>(statistics.free_entries),
      static_cast<unsigned>(statistics.available_entries),
      static_cast<unsigned>(statistics.total_entries),
      static_cast<unsigned>(statistics.namespace_count),
      static_cast<unsigned>(usage_percent));
}

hal::TouchPoint RotateTouchPointToDisplay(const hal::TouchPoint& point,
    int rotation_angle, int screen_width, int screen_height) {
  hal::TouchPoint rotated = point;
  switch (rotation_angle) {
    case 90:
      rotated.x = screen_height - point.y;
      rotated.y = point.x;
      break;
    case 180:
      rotated.x = screen_width - point.x;
      rotated.y = screen_height - point.y;
      break;
    case 270:
      rotated.x = point.y;
      rotated.y = screen_width - point.x;
      break;
    default:
      break;
  }
  return rotated;
}

int RotatedScreenHeight(int rotation_angle, int screen_width, int screen_height) {
  return (rotation_angle == 90 || rotation_angle == 270) ? screen_width
                                                          : screen_height;
}

}  // namespace

Application::Application()
    : device_provider_context_(hal::CreateDeviceProviderContext()) {}

bool Application::Init() {
  if (!ConfigureChinaTimeZone()) {
    LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
        "Configure China time zone failed\n");
  }

  esp_err_t nvs_result = nvs_flash_init();
  const char* nvs_recovery_reason = nullptr;
  if (nvs_result == ESP_ERR_NVS_NO_FREE_PAGES ||
      nvs_result == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    nvs_recovery_reason = nvs_result == ESP_ERR_NVS_NO_FREE_PAGES
                              ? "no free pages"
                              : "new version found";
    nvs_flash_erase();
    nvs_result = nvs_flash_init();
  }
  if (nvs_result != ESP_OK) {
    LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
        "NVS init failed: %s (%#X)\n", esp_err_to_name(nvs_result),
        static_cast<unsigned>(nvs_result));
  } else {
    if (nvs_recovery_reason != nullptr) {
      LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
          "NVS storage recovered: reason=%s\n", nvs_recovery_reason);
    }
    LogNvsStorageInfo();
  }
  if (!app::InitLittleFsStorage()) {
    LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
        "LittleFS internal storage is unavailable\n");
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

  result = ui_manager_.Init(screen, &lvgl_port_,
      device_provider_context_.diagnostics,
      device_provider_context_.device_info, device_provider_context_.gps,
      device_provider_context_.audio, device_provider_context_.haptic,
      device_provider_context_.battery_management, device_provider_context_.camera,
      device_provider_context_.rtc, device_provider_context_.radio,
      device_provider_context_.imu,
      device_provider_context_.ethernet, device_provider_context_.wifi,
      device_provider_context_.storage, device_provider_context_.nfc,
      device_provider_context_.infrared, device_provider_context_.cellular);
  if (!result) {
    LogMessage(LogLevel::kError, __FILE__, __LINE__, "Init failed\n");
    return false;
  }
  ui_manager_.SetScreenLockCallback([this]() { RequestScreenLock(); });
  ui_manager_.SetScreenBrightnessCallback(
      [this](int percent) { return ApplyScreenBrightness(percent); });
  ui_manager_.SetSystemPowerCallbacks(
      [this]() { RestartDevice(); }, [this]() { PowerOffDevice(); });

  result = lvgl_port_.Start();
  if (!result) {
    LogMessage(LogLevel::kError, __FILE__, __LINE__, "Start failed\n");
    return false;
  }
  app::InitStorage();
  if (!app::NetworkMonitor::Instance().Initialize(
          device_provider_context_.wifi)) {
    LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
        "Initialize network monitor failed\n");
  }
  if (!app::FirmwareUpdateManager::Instance().Initialize(
          device_provider_context_.wifi, *this)) {
    LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
        "Initialize firmware update manager failed\n");
  }
  lilygo_box::ui::SetLvglPortForRotation(&lvgl_port_);
  app::DisplayPreferences display_preferences = app::GetDisplayPreferences();
  lvgl_port_.Lock();
  lvgl_port_.SetDisplayRotation(display_preferences.screen_rotation_angle);
  lvgl_port_.Unlock();
  current_screen_brightness_percent_.store(0);
  if (device_provider_context_.audio != nullptr) {
    app::SoundPreferences sound_preferences = app::GetSoundPreferences();
    device_provider_context_.audio->SetSpeakerVolumePercent(
        sound_preferences.volume_percent);
  }

  int startup_battery_level = 0;
  const bool startup_battery_management_ready = device_provider_context_.battery_management != nullptr &&
      device_provider_context_.battery_management->ReadBatteryLevel(&startup_battery_level);
  if (!startup_battery_management_ready) {
    const bool shown = ShowBatteryStartupWarning(
        ui::icon::kBatteryAndroidQuestion, kBatteryFaultStartupIconColor,
        "Battery management fault", -1);
    if (!shown) {
      LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
          "ShowBatteryStartupWarning failed\n");
    }
    StartScreenBacklight(display_preferences.brightness_percent);
    vTaskDelay(pdMS_TO_TICKS(kLowBatteryStartupWarningMs));
    PowerOffDevice();
    return false;
  }
  if (startup_battery_level < kLowBatteryStartupThresholdPercent) {
    char percent_text[16] = {};
    std::snprintf(percent_text, sizeof(percent_text), "%d%%",
        std::clamp(startup_battery_level, 0, 100));
    const bool shown = ShowBatteryStartupWarning(
        ui::icon::kBatteryAndroid0, kLowBatteryStartupIconColor, percent_text,
        startup_battery_level);
    if (!shown) {
      LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
          "ShowBatteryStartupWarning failed\n");
    }
    StartScreenBacklight(display_preferences.brightness_percent);
    vTaskDelay(pdMS_TO_TICKS(kLowBatteryStartupWarningMs));
    PowerOffDevice();
    return false;
  }

  const bool startup_result = StartStartupScreen();
  if (!startup_result) {
    LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
        "StartStartupScreen failed\n");
  }
  StartScreenBacklight(display_preferences.brightness_percent);

  if (!app::IsFirstBootCompleted()) {
    lvgl_port_.Lock();
    const bool welcome_result = ui_manager_.ShowFirstBootWelcome(
        []() { return app::MarkFirstBootCompleted(); });
    lvgl_port_.Unlock();
    if (!welcome_result) {
      LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
          "ShowFirstBootWelcome failed\n");
    }
  }

  lvgl_port_.Lock();
  ui_manager_.RefreshSystemStatusNow();
  ui_manager_.SetStartupScreenProgress(33);
  lvgl_port_.Unlock();

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

bool Application::ShowBatteryStartupWarning(const char* icon,
    uint32_t icon_color, const char* message, int battery_percent) {
  const bool flush_paused = lvgl_port_.PauseDisplayFlush();
  if (!flush_paused) {
    LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
        "Pause display refresh before battery warning failed\n");
  }

  lvgl_port_.Lock();
  const bool shown = ui_manager_.ShowBatteryStartupWarning(
      icon, icon_color, message, battery_percent);
  lvgl_port_.Unlock();

  if (flush_paused && !lvgl_port_.ResumeDisplayFlushAndWaitForRefresh()) {
    LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
        "Refresh battery startup warning failed\n");
  }
  return shown;
}

bool Application::StartStartupScreen() {
  const bool flush_paused = lvgl_port_.PauseDisplayFlush();
  if (!flush_paused) {
    LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
        "Pause display refresh before startup screen failed\n");
  }

  lvgl_port_.Lock();
  const bool started = ui_manager_.StartStartupScreenAnimation();
  lvgl_port_.Unlock();

  if (flush_paused && !lvgl_port_.ResumeDisplayFlushAndWaitForRefresh()) {
    LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
        "Refresh startup screen failed\n");
  }
  return started;
}

void Application::Run() {
  while (true) {
    vTaskDelay(pdMS_TO_TICKS(kBatteryMonitorPeriodMs));
    if (power_action_in_progress_.load()) {
      continue;
    }

    int charge_percent = 0;
    if (device_provider_context_.battery_management != nullptr &&
        device_provider_context_.battery_management->ReadBatteryLevel(&charge_percent) &&
        charge_percent == 0) {
      LogMessage(LogLevel::kError, __FILE__, __LINE__,
          "Battery depleted; powering off device\n");
      PowerOffDevice();
    }
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
  options.connection_timeout_ms = kStartupWifiAutoConnectWaitMs;
  options.poll_interval_ms = kStartupWifiAutoConnectPollMs;
  while (true) {
    const app::WifiAutoConnectResult result =
        app::TryStartWifiAutoConnect(device_provider_context_.wifi, options);
    const bool failed = result == app::WifiAutoConnectResult::kFailed;
    const bool retry_later = failed ||
        result == app::WifiAutoConnectResult::kNoVisibleTarget;
    if (failed) {
      LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
          "Background WiFi auto connect failed\n");
    }
    vTaskDelay(pdMS_TO_TICKS(retry_later
        ? kWifiAutoConnectFailureRetryMs
        : kWifiAutoConnectIdleMs));
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
  uint32_t lock_screen_last_interaction_ms = last_touch_ms;
  bool unlock_touch_active = false;
  bool unlock_drag_ready = false;
  uint32_t unlock_touch_start_ms = 0;
  hal::TouchPoint unlock_touch_start = {};
  hal::TouchPoint unlock_touch_last = {};
  bool screen_off_touch_active = false;
  uint32_t screen_off_touch_start_ms = 0;
  hal::TouchPoint screen_off_touch_start = {};
  bool first_tap_pending = false;
  uint32_t first_tap_ms = 0;
  hal::TouchPoint first_tap_point = {};
  bool lock_screen_first_tap_pending = false;
  uint32_t lock_screen_first_tap_ms = 0;
  hal::TouchPoint lock_screen_first_tap_point = {};
  while (true) {
    const uint32_t now_ms = static_cast<uint32_t>(xTaskGetTickCount() *
        portTICK_PERIOD_MS);
    if (power_action_in_progress_.load()) {
      vTaskDelay(pdMS_TO_TICKS(kScreenLockPollMs));
      continue;
    }
    const bool recovery_required = lvgl_port_.IsDisplayFlushPaused() &&
        (!screen_locked_.load() || lock_screen_awake_.load() ||
            !screen_off_confirmed_.load());
    if (recovery_required) {
      bool screen_restored = false;
      bool recovery_performed = false;
      if (lvgl_port_.BeginScreenTransition()) {
        const bool still_requires_recovery =
            lvgl_port_.IsDisplayFlushPaused() &&
            (!screen_locked_.load() || lock_screen_awake_.load() ||
                !screen_off_confirmed_.load());
        screen_restored = !still_requires_recovery;
        if (still_requires_recovery) {
          screen_restored = RestoreScreenAfterSleep();
          recovery_performed = screen_restored;
        }
        lvgl_port_.EndScreenTransition();
      }
      if (!screen_restored) {
        vTaskDelay(pdMS_TO_TICKS(kScreenLockPollMs));
        continue;
      }
      if (recovery_performed && screen_locked_.load()) {
        lock_screen_awake_.store(true);
      }
      last_touch_ms = now_ms;
      lock_screen_last_interaction_ms = now_ms;
      screen_off_touch_active = false;
      first_tap_pending = false;
    }
    hal::TouchPoint point;
    if (ui_manager_.IsFirstBootWelcomeActive()) {
      last_touch_ms = now_ms;
      lock_screen_last_interaction_ms = now_ms;
      unlock_touch_active = false;
      unlock_drag_ready = false;
      screen_off_touch_active = false;
      first_tap_pending = false;
      lock_screen_first_tap_pending = false;
      vTaskDelay(pdMS_TO_TICKS(kScreenLockPollMs));
      continue;
    }
    if (screen_lock_requested_.exchange(false)) {
      if (!screen_locked_.load() && !LockScreenNow()) {
        last_touch_ms = now_ms;
      }
      vTaskDelay(pdMS_TO_TICKS(kScreenLockPollMs));
      continue;
    }
    if (screen_locked_.load()) {
      if (!lock_screen_awake_.load()) {
        lock_screen_first_tap_pending = false;
        const app::DisplayPreferences preferences =
            LoadDisplayPreferencesOrDefault();
        if (!preferences.lock_screen_double_tap_to_turn_screen_on_and_off) {
          screen_off_touch_active = false;
          first_tap_pending = false;
          vTaskDelay(pdMS_TO_TICKS(kScreenLockPollMs));
          continue;
        }

        if (first_tap_pending &&
            now_ms - first_tap_ms > kDoubleTapMaximumIntervalMs) {
          first_tap_pending = false;
        }
        hal::TouchPoint screen_off_point;
        const bool screen_off_touched =
            ReadScreenTouchWhileSleeping(&screen_off_point) &&
            screen_off_point.x >= 0 && screen_off_point.y >= 0;
        if (screen_off_touched) {
          if (!screen_off_touch_active) {
            screen_off_touch_active = true;
            screen_off_touch_start_ms = now_ms;
            screen_off_touch_start = screen_off_point;
          }
        } else if (screen_off_touch_active) {
          screen_off_touch_active = false;
          if (now_ms - screen_off_touch_start_ms <= kDoubleTapMaximumTapMs) {
            const bool double_tap =
                first_tap_pending &&
                now_ms - first_tap_ms <= kDoubleTapMaximumIntervalMs &&
                AreTouchPointsNearby(
                    first_tap_point, screen_off_touch_start);
            if (double_tap) {
              first_tap_pending = false;
              WakeScreenFromLock();
              if (lock_screen_awake_.load()) {
                ui::PlayUiHapticFeedback();
                last_touch_ms = now_ms;
                lock_screen_last_interaction_ms = now_ms;
              }
            } else {
              first_tap_pending = true;
              first_tap_ms = now_ms;
              first_tap_point = screen_off_touch_start;
            }
          }
        }
        vTaskDelay(pdMS_TO_TICKS(kScreenOffTouchPollMs));
        continue;
      }

      screen_off_touch_active = false;
      first_tap_pending = false;
      const app::DisplayPreferences preferences =
          LoadDisplayPreferencesOrDefault();
      if (!preferences.lock_screen_double_tap_to_turn_screen_on_and_off ||
          (lock_screen_first_tap_pending &&
              now_ms - lock_screen_first_tap_ms >
                  kDoubleTapMaximumIntervalMs)) {
        lock_screen_first_tap_pending = false;
      }
      if (ReadScreenTouchWhileAwake(&point)) {
        lock_screen_last_interaction_ms = now_ms;
        if (!unlock_touch_active) {
          unlock_touch_start_ms = now_ms;
          unlock_touch_start = point;
          unlock_touch_last = point;
          unlock_touch_active = true;
          unlock_drag_ready = false;
        } else {
          unlock_touch_last = point;
          const hal::TouchPoint visual_start = RotateTouchPointToDisplay(
              unlock_touch_start, preferences.screen_rotation_angle,
              screen->ScreenWidth(), screen->ScreenHeight());
          const hal::TouchPoint visual_current = RotateTouchPointToDisplay(
              point, preferences.screen_rotation_angle, screen->ScreenWidth(),
              screen->ScreenHeight());
          const int drag_distance =
              std::max(0, visual_start.y - visual_current.y);
          const int drag_limit = RotatedScreenHeight(
              preferences.screen_rotation_angle, screen->ScreenWidth(),
              screen->ScreenHeight());
          const int drag_offset = -std::min(drag_distance, drag_limit);
          lvgl_port_.Lock();
          ui_manager_.SetLockScreenDragOffset(drag_offset);
          lvgl_port_.Unlock();
          unlock_drag_ready = IsUnlockSwipe(unlock_touch_start, point);
        }
      } else {
        if (unlock_touch_active) {
          const bool short_tap = !unlock_drag_ready &&
              now_ms - unlock_touch_start_ms <= kDoubleTapMaximumTapMs &&
              AreTouchPointsNearby(unlock_touch_start, unlock_touch_last);
          const bool lock_screen_double_tap_to_turn_screen_off =
              preferences.lock_screen_double_tap_to_turn_screen_on_and_off &&
              short_tap &&
              lock_screen_first_tap_pending &&
              now_ms - lock_screen_first_tap_ms <=
                  kDoubleTapMaximumIntervalMs &&
              AreTouchPointsNearby(
                  lock_screen_first_tap_point, unlock_touch_start);
          if (unlock_drag_ready) {
            lock_screen_first_tap_pending = false;
            lvgl_port_.Lock();
            ui_manager_.PlayLockScreenUnlockAnimation();
            lvgl_port_.Unlock();
            vTaskDelay(pdMS_TO_TICKS(kScreenUnlockAnimationWaitMs));
            UnlockScreen();
            last_touch_ms = static_cast<uint32_t>(xTaskGetTickCount() *
                portTICK_PERIOD_MS);
            unlock_touch_active = false;
            unlock_drag_ready = false;
            vTaskDelay(pdMS_TO_TICKS(kScreenLockPollMs));
            continue;
          } else {
            lvgl_port_.Lock();
            if (lock_screen_double_tap_to_turn_screen_off) {
              ui_manager_.SetLockScreenDragOffset(0);
            } else {
              ui_manager_.ResetLockScreenDrag();
            }
            lvgl_port_.Unlock();
            if (lock_screen_double_tap_to_turn_screen_off) {
              lock_screen_first_tap_pending = false;
              ui::PlayUiHapticFeedback();
              unlock_touch_active = false;
              unlock_drag_ready = false;
              if (SleepLockScreenNow()) {
                vTaskDelay(pdMS_TO_TICKS(kScreenOffTouchPollMs));
                continue;
              }
              lock_screen_last_interaction_ms = now_ms;
            } else if (
                preferences.lock_screen_double_tap_to_turn_screen_on_and_off &&
                short_tap) {
              lock_screen_first_tap_pending = true;
              lock_screen_first_tap_ms = now_ms;
              lock_screen_first_tap_point = unlock_touch_start;
            } else {
              lock_screen_first_tap_pending = false;
            }
          }
        }
        unlock_touch_active = false;
        unlock_drag_ready = false;
        const uint32_t lock_screen_idle_ms =
            now_ms - lock_screen_last_interaction_ms;
        const uint32_t lock_screen_dim_start_ms =
            kAwakeLockScreenSleepTimeoutMs - kScreenLockSleepConfirmMs;
        if (lock_screen_idle_ms >= lock_screen_dim_start_ms) {
          if (!SleepAwakeLockScreenWithTimeout()) {
            lock_screen_last_interaction_ms = static_cast<uint32_t>(
                xTaskGetTickCount() * portTICK_PERIOD_MS);
          }
          lock_screen_first_tap_pending = false;
          unlock_touch_active = false;
          unlock_drag_ready = false;
        }
      }
      vTaskDelay(pdMS_TO_TICKS(kScreenLockPollMs));
      continue;
    }

    if (ReadScreenTouchWhileAwake(&point)) {
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
    if (preferences.lock_timeout_seconds ==
            app::kDisplayLockTimeoutDisabledSeconds ||
        idle_ms < dim_start_ms) {
      vTaskDelay(pdMS_TO_TICKS(kScreenLockPollMs));
      continue;
    }

    const int start_brightness = preferences.brightness_percent;
    const int target_brightness =
        app::kUserDisplayBrightnessMinPercent;
    bool fade_canceled = false;
    bool screen_access_interrupted = false;
    lvgl_port_.SetInputBlocked(true);
    for (int step = 1; step <= kScreenLockFadeStepCount; ++step) {
      const int brightness = start_brightness +
          (target_brightness - start_brightness) * step /
              kScreenLockFadeStepCount;
      if (!SetScreenBrightnessWhileAwake(brightness)) {
        screen_access_interrupted = true;
        break;
      }
      vTaskDelay(pdMS_TO_TICKS(
          std::max<uint32_t>(1, kScreenLockFadeMs / kScreenLockFadeStepCount)));
      bool touch_access_available = false;
      const bool touched = ReadScreenTouchWhileAwake(
          &point, &touch_access_available);
      if (!touch_access_available) {
        screen_access_interrupted = true;
        break;
      }
      if (touched) {
        FadeScreenBrightnessTo(start_brightness, kScreenLockFadeMs);
        lvgl_port_.SetInputBlocked(false);
        last_touch_ms = static_cast<uint32_t>(xTaskGetTickCount() *
            portTICK_PERIOD_MS);
        fade_canceled = true;
        break;
      }
    }
    if (screen_access_interrupted) {
      lvgl_port_.SetInputBlocked(false);
      last_touch_ms = now_ms;
      continue;
    }
    if (fade_canceled) {
      continue;
    }

    const uint32_t confirm_start_ms = static_cast<uint32_t>(
        xTaskGetTickCount() * portTICK_PERIOD_MS);
    while (static_cast<uint32_t>(xTaskGetTickCount() * portTICK_PERIOD_MS) -
               confirm_start_ms <
           sleep_confirm_ms) {
      bool touch_access_available = false;
      const bool touched = ReadScreenTouchWhileAwake(
          &point, &touch_access_available);
      if (!touch_access_available) {
        screen_access_interrupted = true;
        break;
      }
      if (touched) {
        FadeScreenBrightnessTo(start_brightness, kScreenLockFadeMs);
        lvgl_port_.SetInputBlocked(false);
        last_touch_ms = static_cast<uint32_t>(xTaskGetTickCount() *
            portTICK_PERIOD_MS);
        fade_canceled = true;
        break;
      }
      vTaskDelay(pdMS_TO_TICKS(kScreenLockPollMs));
    }
    if (screen_access_interrupted) {
      lvgl_port_.SetInputBlocked(false);
      last_touch_ms = now_ms;
      continue;
    }
    if (fade_canceled) {
      continue;
    }

    if (EnterScreenLockSleep()) {
      screen_locked_.store(true);
      lock_screen_awake_.store(false);
    } else {
      lvgl_port_.Lock();
      ui_manager_.HideLockScreen();
      lvgl_port_.Unlock();
      FadeScreenBrightnessTo(start_brightness, kScreenLockFadeMs);
      lvgl_port_.SetInputBlocked(false);
      last_touch_ms = static_cast<uint32_t>(xTaskGetTickCount() *
          portTICK_PERIOD_MS);
    }
  }
}

void Application::RequestScreenLock() {
  screen_lock_requested_.store(true);
}

bool Application::LockScreenNow() {
  if (screen_locked_.load()) {
    return true;
  }
  lvgl_port_.SetInputBlocked(true);
  if (!EnterScreenLockSleep()) {
    lvgl_port_.SetInputBlocked(false);
    return false;
  }
  screen_locked_.store(true);
  lock_screen_awake_.store(false);
  return true;
}

bool Application::EnterScreenLockSleep() {
  if (!SetScreenBrightnessWhileAwake(0)) {
    return false;
  }

  lvgl_port_.Lock();
  const bool lock_screen_shown = ui_manager_.ShowLockScreen();
  lvgl_port_.Unlock();
  if (!lock_screen_shown) {
    return false;
  }

  // 背光关闭后先把锁屏页面完整写入显示缓冲区，避免面板唤醒时短暂显示
  // 进入休眠前的应用页面。
  const bool flush_paused = lvgl_port_.PauseDisplayFlush();
  const bool lock_screen_refreshed =
      flush_paused && lvgl_port_.ResumeDisplayFlushAndWaitForRefresh();
  if (!lock_screen_refreshed || !EnterScreenSleep()) {
    if (flush_paused && lvgl_port_.IsDisplayFlushPaused()) {
      lvgl_port_.ResumeDisplayFlush();
    }
    lvgl_port_.Lock();
    ui_manager_.HideLockScreen();
    lvgl_port_.Unlock();
    return false;
  }
  return true;
}

void Application::WakeScreenFromLock() {
  hal::ScreenProvider* screen = device_provider_context_.screen.get();
  if (screen == nullptr) {
    return;
  }

  if (!lvgl_port_.BeginScreenTransition()) {
    return;
  }
  lvgl_port_.Lock();
  const bool shown = ui_manager_.ShowLockScreen();
  lvgl_port_.Unlock();
  if (!shown) {
    lvgl_port_.EndScreenTransition();
    return;
  }
  const bool screen_restored = RestoreScreenAfterSleep();
  lvgl_port_.EndScreenTransition();
  if (!screen_restored) {
    return;
  }
  lock_screen_awake_.store(true);
}

void Application::RestartDevice() {
  bool expected = false;
  if (!power_action_in_progress_.compare_exchange_strong(expected, true)) {
    return;
  }
  vTaskDelay(pdMS_TO_TICKS(kPowerActionPreSleepSettleMs));
  if (!PreparePowerActionStorage()) {
    power_action_in_progress_.store(false);
    return;
  }
  hal::ScreenProvider* screen = device_provider_context_.screen.get();
  if (screen != nullptr && !screen->EnterDeviceSleep(true)) {
    LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
        "Prepare device for restart failed after preferences were saved\n");
  }
  esp_restart();
}

void Application::PowerOffDevice() {
  bool expected = false;
  if (!power_action_in_progress_.compare_exchange_strong(expected, true)) {
    return;
  }
  vTaskDelay(pdMS_TO_TICKS(kPowerActionPreSleepSettleMs));
  if (!PreparePowerActionStorage()) {
    power_action_in_progress_.store(false);
    return;
  }
  hal::DeviceProvider* device = device_provider_context_.device;
  const hal::PowerOffAction action =
      device == nullptr ? hal::PowerOffAction::kFailed
                        : device->RequestPowerOff();
  if (action == hal::PowerOffAction::kEnterDeepSleep) {
    esp_deep_sleep_start();
    return;
  }
  if (action == hal::PowerOffAction::kWaitForPowerCut) {
    while (true) {
      vTaskDelay(portMAX_DELAY);
    }
  }

  LogMessage(LogLevel::kError, __FILE__, __LINE__,
      "Power off failed after preferences were saved\n");
  app::ResumeStorageUpdatesAfterShutdownFailure();
  const bool screen_restored = RestoreScreenAfterSleep();
  lvgl_port_.EndScreenTransition();
  lvgl_port_.SetInputBlocked(false);
  power_action_in_progress_.store(false);
  if (!screen_restored) {
    LogMessage(LogLevel::kError, __FILE__, __LINE__,
        "Power off failed and screen recovery also failed\n");
  }
}

bool Application::SleepAwakeLockScreenWithTimeout() {
  hal::ScreenProvider* screen = device_provider_context_.screen.get();
  if (screen == nullptr) {
    return false;
  }

  const app::DisplayPreferences preferences = LoadDisplayPreferencesOrDefault();
  const int start_brightness = current_screen_brightness_percent_.load();
  const int target_brightness =
      app::kUserDisplayBrightnessMinPercent;
  bool fade_canceled = false;
  for (int step = 1; step <= kScreenLockFadeStepCount; ++step) {
    const int brightness = start_brightness +
        (target_brightness - start_brightness) * step /
            kScreenLockFadeStepCount;
    if (!SetScreenBrightnessWhileAwake(brightness)) {
      lock_screen_awake_.store(true);
      return false;
    }
    vTaskDelay(pdMS_TO_TICKS(
        std::max<uint32_t>(1, kScreenLockFadeMs / kScreenLockFadeStepCount)));

    hal::TouchPoint point;
    bool touch_access_available = false;
    const bool touched = ReadScreenTouchWhileAwake(
        &point, &touch_access_available);
    if (!touch_access_available) {
      lock_screen_awake_.store(true);
      return false;
    }
    if (touched) {
      FadeScreenBrightnessTo(
          preferences.brightness_percent, kScreenLockFadeMs);
      fade_canceled = true;
      break;
    }
  }
  if (fade_canceled) {
    lock_screen_awake_.store(true);
    return false;
  }

  const uint32_t confirm_start_ms =
      static_cast<uint32_t>(xTaskGetTickCount() * portTICK_PERIOD_MS);
  while (static_cast<uint32_t>(xTaskGetTickCount() * portTICK_PERIOD_MS) -
             confirm_start_ms <
         kScreenLockSleepConfirmMs) {
    hal::TouchPoint point;
    bool touch_access_available = false;
    const bool touched = ReadScreenTouchWhileAwake(
        &point, &touch_access_available);
    if (!touch_access_available) {
      lock_screen_awake_.store(true);
      return false;
    }
    if (touched) {
      FadeScreenBrightnessTo(
          preferences.brightness_percent, kScreenLockFadeMs);
      lock_screen_awake_.store(true);
      return false;
    }
    vTaskDelay(pdMS_TO_TICKS(kScreenLockPollMs));
  }

  if (!EnterScreenSleep()) {
    lock_screen_awake_.store(true);
    return false;
  }

  lvgl_port_.SetInputBlocked(true);
  lock_screen_awake_.store(false);
  return true;
}

bool Application::SleepLockScreenNow() {
  if (!screen_locked_.load() || !lock_screen_awake_.load() ||
      !EnterScreenSleep()) {
    return false;
  }
  lvgl_port_.SetInputBlocked(true);
  lock_screen_awake_.store(false);
  return true;
}

bool Application::EnterScreenSleep() {
  hal::ScreenProvider* screen = device_provider_context_.screen.get();
  if (screen == nullptr) {
    return false;
  }
  if (!lvgl_port_.BeginScreenTransition()) {
    return false;
  }
  if (lvgl_port_.IsDisplayFlushPaused()) {
    lvgl_port_.EndScreenTransition();
    return false;
  }

  const int previous_brightness = current_screen_brightness_percent_.load();
  if (previous_brightness != 0 && !ApplyScreenBrightness(0)) {
    lvgl_port_.EndScreenTransition();
    return false;
  }

  lvgl_port_.AcquireSleepInputBlock();
  if (!lvgl_port_.PauseDisplayFlush()) {
    if (previous_brightness != 0) {
      ApplyScreenBrightness(previous_brightness);
    }
    lvgl_port_.ReleaseSleepInputBlock();
    lvgl_port_.EndScreenTransition();
    return false;
  }

  screen_off_confirmed_.store(false);
  const bool screen_off = screen->EnterDeviceSleep(false);
  if (!screen_off) {
    const bool screen_restored = RestoreScreenAfterSleep();
    lvgl_port_.EndScreenTransition();
    if (!screen_restored) {
      LogMessage(LogLevel::kError, __FILE__, __LINE__,
          "Screen sleep failed and safe display recovery also failed\n");
    }
    return false;
  }
  screen_off_confirmed_.store(true);

  current_screen_brightness_percent_.store(0);
  lvgl_port_.EndScreenTransition();
  return true;
}

bool Application::PreparePowerActionStorage() {
  hal::ScreenProvider* screen = device_provider_context_.screen.get();
  if (screen == nullptr) {
    return false;
  }

  lvgl_port_.SetInputBlocked(true);
  if (!lvgl_port_.BeginScreenTransition()) {
    lvgl_port_.SetInputBlocked(false);
    return false;
  }
  if (lvgl_port_.IsDisplayFlushPaused()) {
    lvgl_port_.EndScreenTransition();
    lvgl_port_.SetInputBlocked(false);
    return false;
  }

  const int previous_brightness = current_screen_brightness_percent_.load();
  if (previous_brightness != 0 && !ApplyScreenBrightness(0)) {
    lvgl_port_.EndScreenTransition();
    lvgl_port_.SetInputBlocked(false);
    return false;
  }

  lvgl_port_.AcquireSleepInputBlock();
  if (!lvgl_port_.PauseDisplayFlush()) {
    if (previous_brightness != 0) {
      ApplyScreenBrightness(previous_brightness);
    }
    lvgl_port_.ReleaseSleepInputBlock();
    lvgl_port_.EndScreenTransition();
    lvgl_port_.SetInputBlocked(false);
    return false;
  }

  screen_off_confirmed_.store(false);
  const bool screen_off = screen->EnterDeviceSleep(false);
  if (!screen_off) {
    const bool screen_restored = RestoreScreenAfterSleep();
    lvgl_port_.EndScreenTransition();
    lvgl_port_.SetInputBlocked(false);
    if (!screen_restored) {
      LogMessage(LogLevel::kError, __FILE__, __LINE__,
          "Power action canceled after screen recovery failed\n");
    }
    return false;
  }
  screen_off_confirmed_.store(true);
  current_screen_brightness_percent_.store(0);

  if (!app::FreezeStorageUpdatesForShutdown()) {
    const bool screen_restored = RestoreScreenAfterSleep();
    lvgl_port_.EndScreenTransition();
    lvgl_port_.SetInputBlocked(false);
    if (!screen_restored) {
      LogMessage(LogLevel::kError, __FILE__, __LINE__,
          "Storage freeze failed and screen recovery also failed\n");
    }
    return false;
  }

  // 冻结更新后执行最终落盘，并再次确认没有任何待保存数据。
  const bool flush_complete = app::FlushPendingStorageBeforeShutdown();
  const bool storage_clean = !app::HasPendingStorageWrites();
  if (flush_complete && storage_clean) {
    // 成功后保持转换锁、输入屏蔽与刷屏暂停，直到处理器终止。
    return true;
  }
  LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
      "Cancel power action because pending storage data is still dirty\n");
  app::ResumeStorageUpdatesAfterShutdownFailure();
  const bool screen_restored = RestoreScreenAfterSleep();
  lvgl_port_.EndScreenTransition();
  lvgl_port_.SetInputBlocked(false);
  if (!screen_restored) {
    LogMessage(LogLevel::kError, __FILE__, __LINE__,
        "Storage flush failed and screen recovery also failed\n");
  }
  return false;
}

bool Application::RestoreScreenAfterSleep() {
  hal::ScreenProvider* screen = device_provider_context_.screen.get();
  if (screen == nullptr) {
    return false;
  }
  screen_off_confirmed_.store(false);
  if (!lvgl_port_.IsDisplayFlushPaused()) {
    return true;
  }

  if (!screen->ExitDeviceSleep(false)) {
    return false;
  }

  // 面板保持亮度 0，直到专用 LVGL 任务确认锁屏完整帧传输结束。
  if (!lvgl_port_.ResumeDisplayFlushAndWaitForRefresh()) {
    LogMessage(LogLevel::kError, __FILE__, __LINE__,
        "Screen woke, but lock screen refresh did not complete\n");
    lvgl_port_.PauseDisplayFlush();
    return false;
  }

  const app::DisplayPreferences preferences =
      LoadDisplayPreferencesOrDefault();
  if (!ApplyScreenBrightness(preferences.brightness_percent)) {
    LogMessage(LogLevel::kError, __FILE__, __LINE__,
        "Screen woke, but restoring brightness failed\n");
    lvgl_port_.PauseDisplayFlush();
    return false;
  }
  lvgl_port_.ReleaseSleepInputBlock();
  return true;
}

void Application::UnlockScreen() {
  lvgl_port_.Lock();
  ui_manager_.HideLockScreen();
  lvgl_port_.Unlock();
  lvgl_port_.SetInputBlocked(false);
  lock_screen_awake_.store(false);
  screen_locked_.store(false);
}

bool Application::IsUnlockSwipe(const hal::TouchPoint& start,
    const hal::TouchPoint& current) const {
  const hal::ScreenProvider* screen = device_provider_context_.screen.get();
  if (screen == nullptr) {
    return false;
  }

  const app::DisplayPreferences preferences = LoadDisplayPreferencesOrDefault();
  const hal::TouchPoint visual_start = RotateTouchPointToDisplay(start,
      preferences.screen_rotation_angle, screen->ScreenWidth(),
      screen->ScreenHeight());
  const hal::TouchPoint visual_current = RotateTouchPointToDisplay(current,
      preferences.screen_rotation_angle, screen->ScreenWidth(),
      screen->ScreenHeight());
  const int vertical_distance = visual_start.y - visual_current.y;
  const int horizontal_drift = std::abs(visual_current.x - visual_start.x);
  return vertical_distance >= kScreenUnlockSwipeMinDistance &&
         horizontal_drift <= kScreenUnlockSwipeMaxHorizontalDrift;
}

bool Application::ReadScreenTouchWhileAwake(
    hal::TouchPoint* point, bool* access_available) {
  if (access_available != nullptr) {
    *access_available = false;
  }
  if (point == nullptr || device_provider_context_.screen == nullptr) {
    return false;
  }

  const bool can_access = !power_action_in_progress_.load() &&
      !lvgl_port_.IsDisplayFlushPaused() &&
      !screen_off_confirmed_.load();
  bool touch_access_available = false;
  const bool touched = can_access &&
      lvgl_port_.ReadTouch(point, &touch_access_available);
  if (access_available != nullptr) {
    *access_available = can_access && touch_access_available;
  }
  return touched;
}

bool Application::ReadScreenTouchWhileSleeping(hal::TouchPoint* point) {
  hal::ScreenProvider* screen = device_provider_context_.screen.get();
  if (point == nullptr || screen == nullptr ||
      power_action_in_progress_.load() ||
      !screen_off_confirmed_.load() ||
      !lvgl_port_.IsDisplayFlushPaused() ||
      !lvgl_port_.TryBeginScreenTransition()) {
    return false;
  }

  const bool can_access = screen_off_confirmed_.load() &&
      lvgl_port_.IsDisplayFlushPaused();
  const bool touched = can_access && screen->ReadScreenTouch(point);
  lvgl_port_.EndScreenTransition();
  return touched;
}

bool Application::SetScreenBrightnessWhileAwake(int percent) {
  const TickType_t timeout_ticks =
      pdMS_TO_TICKS(kScreenBrightnessTransitionWaitMs);
  if (!lvgl_port_.TryBeginScreenTransition(timeout_ticks)) {
    return false;
  }

  const bool can_access = !power_action_in_progress_.load() &&
      !lvgl_port_.IsDisplayFlushPaused() &&
      !screen_off_confirmed_.load();
  const bool updated = can_access && ApplyScreenBrightness(percent);
  lvgl_port_.EndScreenTransition();
  return updated;
}

bool Application::ApplyScreenBrightness(int percent) {
  hal::ScreenProvider* screen = device_provider_context_.screen.get();
  if (screen == nullptr) {
    return false;
  }

  const int clamped_percent = std::clamp(percent, 0, 100);
  if (!screen->SetScreenBrightnessPercent(clamped_percent)) {
    return false;
  }
  current_screen_brightness_percent_.store(clamped_percent);
  return true;
}

bool Application::StartScreenBacklight(int target_percent) {
  current_screen_brightness_percent_.store(0);
  return FadeScreenBrightnessTo(target_percent, kScreenStartupFadeMs);
}

bool Application::FadeScreenBrightnessTo(
    int target_percent, uint32_t duration_ms) {
  hal::ScreenProvider* screen = device_provider_context_.screen.get();
  const TickType_t timeout_ticks =
      pdMS_TO_TICKS(kScreenBrightnessTransitionWaitMs);
  if (screen == nullptr ||
      !lvgl_port_.TryBeginScreenTransition(timeout_ticks)) {
    return false;
  }

  const int clamped_percent = std::clamp(target_percent, 0, 100);
  const bool can_access = !power_action_in_progress_.load() &&
      !lvgl_port_.IsDisplayFlushPaused() &&
      !screen_off_confirmed_.load();
  const bool updated = can_access && screen->FadeScreenBrightnessPercent(
      clamped_percent, duration_ms);
  if (updated) {
    current_screen_brightness_percent_.store(clamped_percent);
  }
  lvgl_port_.EndScreenTransition();
  return updated;
}

app::DisplayPreferences Application::LoadDisplayPreferencesOrDefault() const {
  return app::GetDisplayPreferences();
}

}  // namespace lilygo_box
