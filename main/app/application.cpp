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
#include "app/storage/keyboard_expansion_storage.h"
#include "app/storage/littlefs_storage.h"
#include "app/storage/otg_storage.h"
#include "app/storage/power_state_storage.h"
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
#include "ui/widgets/shared_keyboard.h"

namespace lilygo_box {
namespace {

constexpr uint32_t kStartupWifiAutoConnectWaitMs = 15 * 1000;
constexpr uint32_t kStartupWifiAutoConnectPollMs = 200;
constexpr uint32_t kWifiAutoConnectIdleMs = 2 * 1000;
constexpr uint32_t kWifiAutoConnectFailureRetryMs = 30 * 1000;
constexpr uint32_t kOtgMonitorPeriodMs = 200;
constexpr uint32_t kOtgRestoreDelayMs = 500;
constexpr uint32_t kStartupWifiAutoConnectTaskStackBytes = 8 * 1024;
constexpr UBaseType_t kStartupWifiAutoConnectTaskPriority = 3;
constexpr uint32_t kScreenLockTaskStackBytes = 6 * 1024;
constexpr UBaseType_t kScreenLockTaskPriority = 3;
// 该任务只读取 GPIO、去抖并投递原子事件，不执行 UI 或存储调用链。
constexpr uint32_t kPowerButtonTaskStackBytes = 2 * 1024;
constexpr UBaseType_t kPowerButtonTaskPriority = 3;
constexpr uint32_t kPowerButtonPollMs = 20;
constexpr uint32_t kPowerButtonDebounceMs = 40;
constexpr uint32_t kPowerButtonLongPressMs = 2 * 1000;
constexpr uint32_t kVolumeButtonTaskStackBytes = 3 * 1024;
constexpr UBaseType_t kVolumeButtonTaskPriority = 3;
constexpr uint32_t kVolumeButtonPollMs = 20;
constexpr uint32_t kVolumeButtonDebounceMs = 40;
constexpr uint32_t kVolumeButtonRepeatDelayMs = 320;
constexpr uint32_t kVolumeButtonRepeatIntervalMs = 40;
constexpr int kVolumeButtonStepPercent = 2;
constexpr uint32_t kScreenLockPollMs = 100;
constexpr uint32_t kKeyboardExpansionConnectionUpdateWaitMs = 5000;
constexpr uint32_t kScreenTouchPollMs = 30;
// 防止触发熄屏的双击被触摸固件延迟上报为新的唤醒手势。
constexpr uint32_t kScreenWakeInputGuardMs = 200;
constexpr uint32_t kScreenLockSleepConfirmMs = 3 * 1000;
constexpr uint32_t kAwakeLockScreenSleepTimeoutMs = 10 * 1000;
constexpr uint32_t kLowBatteryStartupWarningMs = 10 * 1000;
constexpr uint32_t kScreenStartupFadeMs = 500;
constexpr uint32_t kScreenLockFadeMs = 300;
constexpr uint32_t kScreenBrightnessTransitionWaitMs = 10;
constexpr int kScreenUnlockSwipeMinDistance = 120;
constexpr uint32_t kScreenUnlockAnimationWaitMs = 240;
constexpr int kScreenUnlockSwipeMaxHorizontalDrift = 90;
constexpr uint32_t kDoubleTapMaximumTapMs = 350;
constexpr uint32_t kDoubleTapMaximumIntervalMs = 550;
constexpr uint32_t kDoubleTapPressConfirmationMs = 30;
constexpr int kDoubleTapMaximumDistance = 100;
constexpr uint32_t kPowerActionPreSleepSettleMs = 30;
constexpr int kLowBatteryStartupThresholdPercent = 10;
constexpr uint32_t kLowBatteryStartupIconColor = 0xFF3B30;
constexpr uint32_t kBatteryFaultStartupIconColor = 0xFF9500;
constexpr uint32_t kPowerOffChargingScreenMs = 5 * 1000;
constexpr int kPowerOffChargingBrightnessPercent = 30;
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
  const int64_t x_distance =
      static_cast<int64_t>(left.x) - static_cast<int64_t>(right.x);
  const int64_t y_distance =
      static_cast<int64_t>(left.y) - static_cast<int64_t>(right.y);
  return x_distance * x_distance + y_distance * y_distance <=
         kDoubleTapMaximumDistance * kDoubleTapMaximumDistance;
}

/**
 * @brief 判断触摸点是否位于当前屏幕的有效坐标范围内
 * @param point 触摸点
 * @param width 屏幕宽度
 * @param height 屏幕高度
 * @return 坐标有效返回 true
 */
bool IsScreenTouchPointValid(
    const hal::TouchPoint& point, int width, int height) {
  return point.x >= 0 && point.y >= 0 && point.x < width && point.y < height;
}

/**
 * @brief 双击识别器产生的状态事件
 */
enum class DoubleTapEvent : uint8_t {
  kNone,
  kSecondPressConfirmed,
  kCompleted,
};

/**
 * @brief 将轮询得到的触摸状态转换为稳定的双击手势事件
 */
class DoubleTapRecognizer final {
 public:
  /**
   * @brief 更新当前触摸状态
   * @param point 当前触摸点，nullptr 表示触摸已释放
   * @param now_ms 当前系统时间
   * @return 本次状态更新产生的双击事件
   */
  DoubleTapEvent Update(const hal::TouchPoint* point, uint32_t now_ms);

  /**
   * @brief 清除当前按压状态和待确认的第一次点击
   */
  void Reset();

 private:
  /**
   * @brief 记录一次新的按压
   * @param point 本次按压起点
   * @param now_ms 当前系统时间
   */
  void BeginPress(const hal::TouchPoint& point, uint32_t now_ms);

  /**
   * @brief 更新持续按压状态并确认第二次按下事件
   * @param point 当前触摸点
   * @param now_ms 当前系统时间
   * @return 本次状态更新产生的双击事件
   */
  DoubleTapEvent ContinuePress(
      const hal::TouchPoint& point, uint32_t now_ms);

  /**
   * @brief 完成当前按压并识别双击释放事件
   * @param now_ms 当前系统时间
   * @return 本次状态更新产生的双击事件
   */
  DoubleTapEvent EndPress(uint32_t now_ms);

  /**
   * @brief 判断当前按压是否仍满足短按要求
   * @param now_ms 当前系统时间
   * @return 当前按压有效返回 true
   */
  bool IsCurrentTapValid(uint32_t now_ms) const;

  /**
   * @brief 清除已超过双击时间窗口的第一次点击
   * @param now_ms 当前系统时间
   */
  void ExpireFirstTap(uint32_t now_ms);

  bool press_active_ = false;
  bool first_tap_pending_ = false;
  bool second_tap_candidate_ = false;
  bool second_press_reported_ = false;
  uint32_t press_start_ms_ = 0;
  uint32_t first_tap_release_ms_ = 0;
  hal::TouchPoint press_start_point_ = {};
  hal::TouchPoint press_last_point_ = {};
  hal::TouchPoint first_tap_point_ = {};
};

enum class PowerButtonEvent : uint8_t {
  kNone,
  kShortPress,
  kLongPress,
};

/**
 * @brief 将带抖动的电源键电平转换为短按和长按事件
 */
class PowerButtonRecognizer final {
 public:
  PowerButtonEvent Update(bool pressed, uint32_t now_ms) {
    if (!initialized_) {
      initialized_ = true;
      raw_pressed_ = pressed;
      stable_pressed_ = pressed;
      ignore_initial_press_ = pressed;
      raw_changed_ms_ = now_ms;
      press_started_ms_ = now_ms;
      return PowerButtonEvent::kNone;
    }

    if (pressed != raw_pressed_) {
      raw_pressed_ = pressed;
      raw_changed_ms_ = now_ms;
    }
    if (raw_pressed_ != stable_pressed_ &&
        now_ms - raw_changed_ms_ >= kPowerButtonDebounceMs) {
      stable_pressed_ = raw_pressed_;
      if (stable_pressed_) {
        press_started_ms_ = now_ms;
        long_press_reported_ = false;
        return PowerButtonEvent::kNone;
      }
      if (ignore_initial_press_) {
        ignore_initial_press_ = false;
        return PowerButtonEvent::kNone;
      }
      return long_press_reported_ ? PowerButtonEvent::kNone
                                  : PowerButtonEvent::kShortPress;
    }

    if (stable_pressed_ && !ignore_initial_press_ &&
        !long_press_reported_ &&
        now_ms - press_started_ms_ >= kPowerButtonLongPressMs) {
      long_press_reported_ = true;
      return PowerButtonEvent::kLongPress;
    }
    return PowerButtonEvent::kNone;
  }

 private:
  bool initialized_ = false;
  bool raw_pressed_ = false;
  bool stable_pressed_ = false;
  bool ignore_initial_press_ = false;
  bool long_press_reported_ = false;
  uint32_t raw_changed_ms_ = 0;
  uint32_t press_started_ms_ = 0;
};

/**
 * @brief 对物理音量键去抖并生成按下和长按连续调节事件
 */
class RepeatingButtonRecognizer final {
 public:
  bool Update(bool pressed, uint32_t now_ms) {
    if (!initialized_) {
      initialized_ = true;
      raw_pressed_ = pressed;
      stable_pressed_ = pressed;
      ignore_initial_press_ = pressed;
      raw_changed_ms_ = now_ms;
      next_repeat_ms_ = now_ms + kVolumeButtonRepeatDelayMs;
      return false;
    }

    if (pressed != raw_pressed_) {
      raw_pressed_ = pressed;
      raw_changed_ms_ = now_ms;
    }
    if (raw_pressed_ != stable_pressed_ &&
        now_ms - raw_changed_ms_ >= kVolumeButtonDebounceMs) {
      stable_pressed_ = raw_pressed_;
      if (!stable_pressed_) {
        ignore_initial_press_ = false;
        return false;
      }
      next_repeat_ms_ = now_ms + kVolumeButtonRepeatDelayMs;
      return !ignore_initial_press_;
    }

    if (stable_pressed_ && !ignore_initial_press_ &&
        static_cast<int32_t>(now_ms - next_repeat_ms_) >= 0) {
      next_repeat_ms_ = now_ms + kVolumeButtonRepeatIntervalMs;
      return true;
    }
    return false;
  }

  bool pressed() const { return stable_pressed_; }

 private:
  bool initialized_ = false;
  bool raw_pressed_ = false;
  bool stable_pressed_ = false;
  bool ignore_initial_press_ = false;
  uint32_t raw_changed_ms_ = 0;
  uint32_t next_repeat_ms_ = 0;
};

DoubleTapEvent DoubleTapRecognizer::Update(
    const hal::TouchPoint* point, uint32_t now_ms) {
  ExpireFirstTap(now_ms);
  if (point != nullptr) {
    if (!press_active_) {
      BeginPress(*point, now_ms);
      return DoubleTapEvent::kNone;
    }
    return ContinuePress(*point, now_ms);
  }
  return press_active_ ? EndPress(now_ms) : DoubleTapEvent::kNone;
}

void DoubleTapRecognizer::Reset() {
  press_active_ = false;
  first_tap_pending_ = false;
  second_tap_candidate_ = false;
  second_press_reported_ = false;
}

void DoubleTapRecognizer::BeginPress(
    const hal::TouchPoint& point, uint32_t now_ms) {
  press_active_ = true;
  press_start_ms_ = now_ms;
  press_start_point_ = point;
  press_last_point_ = point;
  second_tap_candidate_ =
      first_tap_pending_ &&
      now_ms - first_tap_release_ms_ <= kDoubleTapMaximumIntervalMs &&
      AreTouchPointsNearby(first_tap_point_, point);
  second_press_reported_ = false;
}

DoubleTapEvent DoubleTapRecognizer::ContinuePress(
    const hal::TouchPoint& point, uint32_t now_ms) {
  press_last_point_ = point;
  if (!IsCurrentTapValid(now_ms)) {
    second_tap_candidate_ = false;
    return DoubleTapEvent::kNone;
  }
  if (!second_tap_candidate_ || second_press_reported_ ||
      now_ms - press_start_ms_ < kDoubleTapPressConfirmationMs) {
    return DoubleTapEvent::kNone;
  }
  second_press_reported_ = true;
  return DoubleTapEvent::kSecondPressConfirmed;
}

DoubleTapEvent DoubleTapRecognizer::EndPress(uint32_t now_ms) {
  const bool valid_tap = IsCurrentTapValid(now_ms);
  const bool double_tap = valid_tap && second_tap_candidate_;
  press_active_ = false;
  second_tap_candidate_ = false;
  second_press_reported_ = false;
  if (double_tap) {
    first_tap_pending_ = false;
    return DoubleTapEvent::kCompleted;
  }
  if (!valid_tap) {
    first_tap_pending_ = false;
    return DoubleTapEvent::kNone;
  }
  first_tap_pending_ = true;
  first_tap_release_ms_ = now_ms;
  first_tap_point_ = press_start_point_;
  return DoubleTapEvent::kNone;
}

bool DoubleTapRecognizer::IsCurrentTapValid(uint32_t now_ms) const {
  return now_ms - press_start_ms_ <= kDoubleTapMaximumTapMs &&
         AreTouchPointsNearby(press_start_point_, press_last_point_);
}

void DoubleTapRecognizer::ExpireFirstTap(uint32_t now_ms) {
  if (!press_active_ && first_tap_pending_ &&
      now_ms - first_tap_release_ms_ > kDoubleTapMaximumIntervalMs) {
    first_tap_pending_ = false;
  }
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
  hal::DeviceProvider* device = device_provider_context_.device;
  if (device == nullptr) {
    LogMessage(
        LogLevel::kError, __FILE__, __LINE__, "No device provider selected\n");
    return false;
  }

  if (device->SupportsPowerOffCharging()) {
    if (!app::InitPowerStateStorage()) {
      LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
          "Initialize persistent power state failed\n");
    }
    bool power_off_requested = false;
    if (!app::ReadPowerOffRequested(&power_off_requested)) {
      LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
          "Read persistent power-off state failed; continuing normal startup\n");
    } else {
      const hal::PowerOffBootAction boot_action =
          device->ResolvePowerOffBoot(power_off_requested);
      if (boot_action == hal::PowerOffBootAction::kEnterDeepSleep) {
        esp_deep_sleep_start();
        return false;
      }
      if (boot_action == hal::PowerOffBootAction::kWaitForPowerCut) {
        while (true) {
          vTaskDelay(portMAX_DELAY);
        }
      }
      if (boot_action == hal::PowerOffBootAction::kShowChargingScreen) {
        if (!power_off_requested &&
            !app::WritePowerOffRequested(true)) {
          LogMessage(LogLevel::kError, __FILE__, __LINE__,
              "Persist USB power-on charging state failed; "
              "continuing normal startup\n");
        } else {
          power_off_charging_boot_ = true;
        }
      } else {
        if (boot_action == hal::PowerOffBootAction::kFailed) {
          LogMessage(LogLevel::kError, __FILE__, __LINE__,
              "Resolve persistent power-off boot failed; recovering normal startup\n");
        }
        if (power_off_requested &&
            !app::WritePowerOffRequested(false)) {
          LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
              "Clear persistent power-off state during startup failed\n");
        }
      }
    }
  }

  // 关机充电的 5 秒巡检会在此之前重新入睡，避免反复挂载文件系统。
  if (!app::InitLittleFsStorage()) {
    LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
        "LittleFS internal storage is unavailable\n");
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

  result = lvgl_port_.Init(
      screen, device_provider_context_.keyboard_expansion);
  if (!result) {
    LogMessage(LogLevel::kError, __FILE__, __LINE__, "Init failed\n");
    return false;
  }

  result = ui_manager_.Init(screen, &lvgl_port_,
      device_provider_context_.capabilities,
      device_provider_context_.diagnostics,
      device_provider_context_.device_info, device_provider_context_.gps,
      device_provider_context_.audio, device_provider_context_.haptic,
      device_provider_context_.battery_management, device_provider_context_.camera,
      device_provider_context_.rtc, device_provider_context_.radio,
      device_provider_context_.keyboard_expansion,
      device_provider_context_.imu,
      device_provider_context_.ethernet, device_provider_context_.wifi,
      device_provider_context_.storage, device_provider_context_.otg,
      device_provider_context_.nfc,
      device_provider_context_.infrared, device_provider_context_.cellular);
  if (!result) {
    LogMessage(LogLevel::kError, __FILE__, __LINE__, "Init failed\n");
    return false;
  }
  ui_manager_.SetScreenLockCallback([this]() { RequestScreenLock(); });
  ui_manager_.SetScreenBrightnessCallback(
      [this](int percent) { return ApplyScreenBrightness(percent); });
  ui_manager_.SetBatteryManagementStatusCallback(
      [this](const hal::BatteryManagementStatus& status) {
        HandleBatteryManagementStatusUpdate(status);
      });
  ui_manager_.SetSystemPowerCallbacks(
      [this]() { RestartDevice(); }, [this]() { PowerOffDevice(); });

  result = lvgl_port_.Start();
  if (!result) {
    LogMessage(LogLevel::kError, __FILE__, __LINE__, "Start failed\n");
    return false;
  }

  if (power_off_charging_boot_) {
    RunPowerOffChargingScreen();
    return false;
  }

  hal::RadioCapabilities radio_capabilities;
  radio::ChipType primary_radio_chip = radio::ChipType::kUnknown;
  if (device_provider_context_.radio != nullptr &&
      device_provider_context_.radio->ReadRadioCapabilities(
          &radio_capabilities)) {
    for (size_t index = 0; index < radio_capabilities.count; ++index) {
      const radio::ChipType chip = radio_capabilities.entries[index].chip;
      if (chip == radio::ChipType::kSx1262 ||
          chip == radio::ChipType::kLr2021 ||
          chip == radio::ChipType::kLr1121) {
        primary_radio_chip = chip;
        break;
      }
    }
  }
  app::InitStorage(
      device_provider_context_.capabilities.supported_radio_chips,
      primary_radio_chip);
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
  const app::KeyboardExpansionPreferences keyboard_expansion_preferences =
      app::GetKeyboardExpansionPreferences();
  if (device_provider_context_.keyboard_expansion != nullptr &&
      keyboard_expansion_preferences.enabled) {
    const int keyboard_backlight_percent =
        keyboard_expansion_preferences.backlight_brightness_percent;
    const bool brightness_configured =
        device_provider_context_.keyboard_expansion->
            SetKeyboardBacklightBrightnessPercent(
                keyboard_backlight_percent);
    keyboard_expansion_scan_pending_ = brightness_configured &&
        device_provider_context_.keyboard_expansion->
            StartKeyboardExpansionScan();
    if (!keyboard_expansion_scan_pending_) {
      keyboard_expansion_unavailable_notice_pending_ = true;
    }
  }
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
        "Battery Status Error", -1);
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

  if (!UpdateOtgPowerPolicy()) {
    LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
        "Initialize OTG reverse-power policy failed\n");
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

  bool power_button_pressed = false;
  if (screen_lock_task_result == pdPASS &&
      device->ReadPowerButtonPressed(&power_button_pressed)) {
    const BaseType_t power_button_task_result = xTaskCreate(
        PowerButtonTaskEntry, "power_button",
        kPowerButtonTaskStackBytes, this, kPowerButtonTaskPriority,
        nullptr);
    if (power_button_task_result != pdPASS) {
      LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
          "Create power button task failed\n");
    }
  }

  bool volume_up_pressed = false;
  bool volume_down_pressed = false;
  if (screen_lock_task_result == pdPASS &&
      device->ReadVolumeUpButtonPressed(&volume_up_pressed) &&
      device->ReadVolumeDownButtonPressed(&volume_down_pressed)) {
    const BaseType_t volume_button_task_result = xTaskCreate(
        VolumeButtonTaskEntry, "volume_button",
        kVolumeButtonTaskStackBytes, this, kVolumeButtonTaskPriority,
        nullptr);
    if (volume_button_task_result != pdPASS) {
      LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
          "Create volume button task failed\n");
    }
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

bool Application::ShowPowerOffChargingScreen(
    int battery_percent, bool critical, bool full_charged) {
  const bool flush_paused = lvgl_port_.PauseDisplayFlush();
  if (!flush_paused) {
    LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
        "Pause display refresh before power-off charging screen failed\n");
  }

  lvgl_port_.Lock();
  const bool shown = ui_manager_.ShowPowerOffChargingScreen(
      battery_percent, critical, full_charged);
  lvgl_port_.Unlock();

  if (flush_paused && !lvgl_port_.ResumeDisplayFlushAndWaitForRefresh()) {
    LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
        "Refresh power-off charging screen failed\n");
  }
  return shown;
}

void Application::RunPowerOffChargingScreen() {
  hal::BatteryManagementStatus battery_status;
  const bool battery_ready =
      device_provider_context_.battery_management != nullptr &&
      device_provider_context_.battery_management
          ->ReadBatteryManagementStatus(&battery_status) &&
      battery_status.ready && battery_status.pack_present;
  bool shown = false;
  if (battery_ready) {
    shown = ShowPowerOffChargingScreen(battery_status.charge_percent,
        battery_status.charge_percent < kLowBatteryStartupThresholdPercent,
        battery_status.full_charged);
  } else {
    shown = ShowBatteryStartupWarning(ui::icon::kBatteryAndroidQuestion,
        kBatteryFaultStartupIconColor, "Battery Status Error", -1);
  }
  if (!shown) {
    LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
        "Show power-off charging screen failed\n");
  }
  StartScreenBacklight(kPowerOffChargingBrightnessPercent);

  PowerButtonRecognizer recognizer;
  TickType_t last_poll_ticks = xTaskGetTickCount();
  const uint32_t screen_started_ms = static_cast<uint32_t>(
      last_poll_ticks * portTICK_PERIOD_MS);
  while (static_cast<uint32_t>(xTaskGetTickCount() * portTICK_PERIOD_MS) -
          screen_started_ms < kPowerOffChargingScreenMs) {
    bool pressed = false;
    hal::DeviceProvider* device = device_provider_context_.device;
    if (device != nullptr && device->ReadPowerButtonPressed(&pressed)) {
      const uint32_t now_ms = static_cast<uint32_t>(
          xTaskGetTickCount() * portTICK_PERIOD_MS);
      if (recognizer.Update(pressed, now_ms) ==
          PowerButtonEvent::kLongPress) {
        app::ResumeStorageUpdatesAfterShutdownFailure();
        if (app::WritePowerOffRequested(false)) {
          LogMessage(LogLevel::kInfo, __FILE__, __LINE__,
              "Long power-button press accepted from power-off charging; "
              "restarting for normal startup\n");
          RestartSystem();
          return;
        }
        LogMessage(LogLevel::kError, __FILE__, __LINE__,
            "Clear persistent power-off state from charging screen failed\n");
        break;
      }
    }
    xTaskDelayUntil(&last_poll_ticks, pdMS_TO_TICKS(kPowerButtonPollMs));
  }
  ReturnToPowerOffStateAfterChargingScreen();
}

bool Application::WakeScreenForPowerOffCharging() {
  hal::ScreenProvider* screen = device_provider_context_.screen.get();
  if (screen == nullptr || !screen->ExitDeviceSleep(false)) {
    return false;
  }
  screen_off_confirmed_.store(false);
  if (lvgl_port_.IsDisplayFlushPaused() &&
      !lvgl_port_.ResumeDisplayFlushAndWaitForRefresh()) {
    LogMessage(LogLevel::kError, __FILE__, __LINE__,
        "Restore display refresh for power-off charging failed\n");
    return false;
  }
  return true;
}

void Application::ReturnToPowerOffStateAfterChargingScreen() {
  power_action_in_progress_.store(true);
  lvgl_port_.SetInputBlocked(true);
  if (current_screen_brightness_percent_.load() != 0) {
    ApplyScreenBrightness(0);
  }
  if (!lvgl_port_.IsDisplayFlushPaused() && !lvgl_port_.PauseDisplayFlush()) {
    LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
        "Pause display refresh before returning to power-off state failed\n");
  }

  hal::DeviceProvider* device = device_provider_context_.device;
  const hal::PowerOffAction action =
      device == nullptr ? hal::PowerOffAction::kFailed
                        : device->RequestPowerOffFromChargingScreen();
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
      "Return to power-off state after charging screen failed; restarting\n");
  if (!app::WritePowerOffRequested(false)) {
    LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
        "Clear persistent power-off state after charging failure failed\n");
  }
  esp_restart();
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
    vTaskDelay(pdMS_TO_TICKS(kOtgMonitorPeriodMs));
    if (power_action_in_progress_.load()) {
      continue;
    }

    UpdateOtgPowerPolicy();
    UpdateKeyboardExpansionScan();
    HandleKeyboardExpansionDisconnection();
    UpdateKeyboardExpansionConnection();
    ShowPendingKeyboardExpansionUnavailableNotice();

    if (battery_depleted_pending_.exchange(false)) {
      LogMessage(LogLevel::kError, __FILE__, __LINE__,
          "Battery depleted; powering off device\n");
      PowerOffDevice();
    }
  }
}

void Application::HandleBatteryManagementStatusUpdate(
    const hal::BatteryManagementStatus& status) {
  const bool previous_charging = battery_charging_.exchange(status.charging);
  const bool charging_state_known =
      battery_charging_state_known_.exchange(true);
  if (charging_state_known && previous_charging != status.charging) {
    RequestSystemActivity(
        status.charging
            ? SystemActivityReason::kChargingStarted
            : SystemActivityReason::kChargingStopped);
  }
  battery_depleted_pending_.store(
      status.pack_present && status.charge_percent == 0);
}

void Application::UpdateKeyboardExpansionScan() {
  if (!keyboard_expansion_scan_pending_ ||
      device_provider_context_.keyboard_expansion == nullptr) {
    return;
  }

  const app::KeyboardExpansionPreferences preferences =
      app::GetKeyboardExpansionPreferences();
  if (!preferences.enabled) {
    keyboard_expansion_scan_pending_ = false;
    return;
  }

  hal::KeyboardExpansionStatus status;
  if (!device_provider_context_.keyboard_expansion->
          ReadKeyboardExpansionStatus(&status) ||
      status.state == hal::KeyboardExpansionState::kScanning) {
    return;
  }
  keyboard_expansion_scan_pending_ = false;
  keyboard_expansion_disconnection_handled_ = false;
  keyboard_expansion_disconnection_activity_reported_.store(false);
  if (status.state == hal::KeyboardExpansionState::kReady) {
    keyboard_expansion_unavailable_notice_pending_ = false;
    lvgl_port_.Lock();
    ui_manager_.CloseKeyboardExpansionUnavailablePrompt();
    ui_manager_.RefreshActiveSettingsKeyboardExpansion();
    ui_manager_.RefreshSystemStatusNow();
    ui::RefreshSharedKeyboardVisibility();
    lvgl_port_.Unlock();
    return;
  }

  keyboard_expansion_unavailable_notice_pending_ = true;
  lvgl_port_.Lock();
  ui_manager_.RefreshActiveSettingsKeyboardExpansion();
  ui_manager_.RefreshSystemStatusNow();
  ui::RefreshSharedKeyboardVisibility();
  lvgl_port_.Unlock();
}

void Application::HandleKeyboardExpansionDisconnection() {
  if (keyboard_expansion_scan_pending_ ||
      device_provider_context_.keyboard_expansion == nullptr ||
      screen_lock_transition_in_progress_.load()) {
    return;
  }

  if (screen_lock_state_.load() != ScreenLockState::kUnlocked) {
    bool update_expected = false;
    if (!keyboard_expansion_connection_update_in_progress_.
            compare_exchange_strong(update_expected, true)) {
      return;
    }
    if (screen_lock_transition_in_progress_.load()) {
      keyboard_expansion_connection_update_in_progress_.store(false);
      return;
    }
    device_provider_context_.keyboard_expansion->
        UpdateKeyboardExpansionDisconnectionState();
    keyboard_expansion_connection_update_in_progress_.store(false);
  }
  hal::KeyboardExpansionStatus status;
  if (!device_provider_context_.keyboard_expansion->
          ReadKeyboardExpansionStatus(&status)) {
    return;
  }
  if (status.state != hal::KeyboardExpansionState::kDisconnected) {
    keyboard_expansion_disconnection_handled_ = false;
    keyboard_expansion_disconnection_activity_reported_.store(false);
    return;
  }
  if (keyboard_expansion_disconnection_handled_) {
    return;
  }
  keyboard_expansion_disconnection_handled_ = true;
  if (!keyboard_expansion_disconnection_activity_reported_.exchange(true)) {
    RequestSystemActivity(SystemActivityReason::kKeyboardDisconnected);
  }

  keyboard_expansion_unavailable_notice_pending_ = true;
  lvgl_port_.Lock();
  ui_manager_.RefreshActiveSettingsKeyboardExpansion();
  ui_manager_.RefreshSystemStatusNow();
  ui::RefreshSharedKeyboardVisibility();
  lvgl_port_.Unlock();
}

void Application::UpdateKeyboardExpansionConnection() {
  if (device_provider_context_.keyboard_expansion == nullptr ||
      !app::GetKeyboardExpansionPreferences().enabled) {
    keyboard_expansion_connection_update_failed_ = false;
    return;
  }

  // 面板完全熄灭时只读取连接中断并请求锁屏亮屏，避免提前初始化扩展
  // 并点亮键盘背光；锁屏页面亮起后再完成识别和状态栏刷新。连接事务
  // 与所有熄屏入口握手，保证扫描完成后再统一让扩展和面板进入睡眠。
  if (device_provider_context_.keyboard_expansion->
          HasKeyboardExpansionConnectionChangePending()) {
    RequestSystemActivity(SystemActivityReason::kKeyboardConnected);
  }
  bool update_expected = false;
  if (!keyboard_expansion_connection_update_in_progress_.
          compare_exchange_strong(update_expected, true)) {
    return;
  }
  if (screen_lock_state_.load() == ScreenLockState::kAsleep ||
      screen_lock_transition_in_progress_.load()) {
    keyboard_expansion_connection_update_in_progress_.store(false);
    return;
  }

  bool scan_started = false;
  const bool connection_updated =
      device_provider_context_.keyboard_expansion->
          UpdateKeyboardExpansionConnection(&scan_started);
  // 先发布异步扫描状态，再结束连接事务，避免熄屏任务在两个标志之间
  // 观察到短暂空窗并越过刚启动的扫描。
  if (scan_started) {
    keyboard_expansion_scan_pending_.store(true);
  }
  keyboard_expansion_connection_update_in_progress_.store(false);
  if (!connection_updated) {
    if (!keyboard_expansion_connection_update_failed_) {
      LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
          "Update keyboard expansion connection failed\n");
    }
    keyboard_expansion_connection_update_failed_ = true;
    return;
  }

  keyboard_expansion_connection_update_failed_ = false;
  if (scan_started) {
    lvgl_port_.Lock();
    ui_manager_.RefreshActiveSettingsKeyboardExpansion();
    lvgl_port_.Unlock();
  }
}

void Application::RequestSystemActivity(SystemActivityReason reason) {
  if (reason == SystemActivityReason::kNone ||
      power_action_in_progress_.load()) {
    return;
  }
  pending_system_activity_reason_.store(reason);
  LogMessage(LogLevel::kDebug, __FILE__, __LINE__,
      "System activity requested (reason: %s)\n",
      SystemActivityReasonName(reason));
}

Application::SystemActivityReason Application::ConsumeSystemActivity() {
  return pending_system_activity_reason_.exchange(
      SystemActivityReason::kNone);
}

bool Application::ConfirmKeyboardExpansionDisconnectionForScreenTransition() {
  hal::KeyboardExpansionProvider* keyboard_expansion =
      device_provider_context_.keyboard_expansion;
  if (keyboard_expansion == nullptr ||
      keyboard_expansion_scan_pending_.load() ||
      !screen_lock_transition_in_progress_.load()) {
    return false;
  }

  hal::KeyboardExpansionStatus status;
  if (keyboard_expansion->ReadKeyboardExpansionStatus(&status) &&
      status.state == hal::KeyboardExpansionState::kDisconnected) {
    return true;
  }
  if (!lvgl_port_.IsInputBlocked() ||
      !keyboard_expansion->
          HasKeyboardExpansionDisconnectionCheckPending()) {
    return false;
  }

  bool update_expected = false;
  if (!keyboard_expansion_connection_update_in_progress_.
          compare_exchange_strong(update_expected, true)) {
    return false;
  }

  bool disconnected = false;
  if (keyboard_expansion->UpdateKeyboardExpansionDisconnectionState()) {
    disconnected = keyboard_expansion->ReadKeyboardExpansionStatus(&status) &&
        status.state == hal::KeyboardExpansionState::kDisconnected;
  }
  keyboard_expansion_connection_update_in_progress_.store(false);
  return disconnected;
}

Application::SystemActivityReason
Application::ConsumeScreenTransitionActivity() {
  SystemActivityReason activity_reason = ConsumeSystemActivity();
  if (activity_reason != SystemActivityReason::kNone ||
      !ConfirmKeyboardExpansionDisconnectionForScreenTransition()) {
    return activity_reason;
  }

  keyboard_expansion_disconnection_activity_reported_.store(true);
  return SystemActivityReason::kKeyboardDisconnected;
}

const char* Application::SystemActivityReasonName(
    SystemActivityReason reason) {
  switch (reason) {
    case SystemActivityReason::kChargingStarted:
      return "charging started";
    case SystemActivityReason::kChargingStopped:
      return "charging stopped";
    case SystemActivityReason::kKeyboardConnected:
      return "keyboard connected";
    case SystemActivityReason::kKeyboardDisconnected:
      return "keyboard disconnected";
    case SystemActivityReason::kNone:
      return "none";
  }
  return "unknown";
}

bool Application::ApplyScreenActivity(
    uint32_t* last_touch_ms,
    uint32_t* lock_screen_last_interaction_ms,
    int restore_brightness_percent) {
  if (last_touch_ms == nullptr ||
      lock_screen_last_interaction_ms == nullptr) {
    return false;
  }

  const uint32_t activity_ms = static_cast<uint32_t>(
      xTaskGetTickCount() * portTICK_PERIOD_MS);
  *last_touch_ms = activity_ms;
  *lock_screen_last_interaction_ms = activity_ms;

  bool result = true;
  const ScreenLockState screen_lock_state = screen_lock_state_.load();
  if (screen_lock_state == ScreenLockState::kAsleep) {
    result = WakeScreenFromLock();
  } else if (restore_brightness_percent >= 0) {
    result = FadeScreenBrightnessTo(
        restore_brightness_percent, kScreenLockFadeMs);
    if (screen_lock_state != ScreenLockState::kUnlocked) {
      screen_lock_state_.store(ScreenLockState::kAwake);
    }
    lvgl_port_.SetInputBlocked(false);
  }
  return result;
}

void Application::ShowPendingKeyboardExpansionUnavailableNotice() {
  if (!keyboard_expansion_unavailable_notice_pending_ ||
      screen_lock_state_.load() != ScreenLockState::kUnlocked ||
      screen_lock_transition_in_progress_.load() ||
      ui_manager_.IsStartupScreenActive() ||
      ui_manager_.IsFirstBootWelcomeActive()) {
    return;
  }

  lvgl_port_.Lock();
  // 与锁屏页面创建使用同一 LVGL 临界区，并在取得锁后重新检查，避免
  // 条件检查通过后恰好进入锁屏而把普通提示框创建到锁屏之上。
  if (screen_lock_state_.load() != ScreenLockState::kUnlocked ||
      screen_lock_transition_in_progress_.load()) {
    lvgl_port_.Unlock();
    return;
  }
  const bool shown =
      ui_manager_.ShowKeyboardExpansionUnavailablePrompt();
  lvgl_port_.Unlock();
  if (shown) {
    keyboard_expansion_unavailable_notice_pending_ = false;
    return;
  }
  LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
      "Show keyboard expansion unavailable prompt failed\n");
  lvgl_port_.Lock();
  ui::RefreshSharedKeyboardVisibility();
  lvgl_port_.Unlock();
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

void Application::PowerButtonTaskEntry(void* context) {
  auto* self = static_cast<Application*>(context);
  if (self != nullptr) {
    self->RunPowerButtonTask();
  }
  vTaskDelete(nullptr);
}

void Application::VolumeButtonTaskEntry(void* context) {
  auto* self = static_cast<Application*>(context);
  if (self != nullptr) {
    self->RunVolumeButtonTask();
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

void Application::RunPowerButtonTask() {
  hal::DeviceProvider* device = device_provider_context_.device;
  if (device == nullptr) {
    return;
  }

  PowerButtonRecognizer recognizer;
  TickType_t last_poll_ticks = xTaskGetTickCount();
  while (true) {
    if (power_action_in_progress_.load()) {
      pending_power_button_action_.store(PowerButtonAction::kNone);
      xTaskDelayUntil(&last_poll_ticks, pdMS_TO_TICKS(kPowerButtonPollMs));
      continue;
    }

    const uint32_t now_ms = static_cast<uint32_t>(
        xTaskGetTickCount() * portTICK_PERIOD_MS);
    bool pressed = false;
    if (device->ReadPowerButtonPressed(&pressed)) {
      const PowerButtonEvent event = recognizer.Update(pressed, now_ms);
      if (physical_button_events_enabled_.load()) {
        if (event == PowerButtonEvent::kShortPress) {
          pending_power_button_action_.store(PowerButtonAction::kShortPress);
        } else if (event == PowerButtonEvent::kLongPress) {
          pending_power_button_action_.store(
              PowerButtonAction::kShowPowerMenu);
        }
      }
    }
    xTaskDelayUntil(&last_poll_ticks, pdMS_TO_TICKS(kPowerButtonPollMs));
  }
}

bool Application::UpdateOtgPowerPolicy() {
  hal::OtgProvider* otg = device_provider_context_.otg;
  if (otg == nullptr) {
    return true;
  }

  bool external_power_present = false;
  if (!otg->ReadExternalPowerPresent(&external_power_present)) {
    otg_hardware_state_known_ = false;
    return false;
  }

  const bool requested = app::GetOtgPreferences().enabled;
  bool enable_hardware = requested && !external_power_present;
  if (external_power_present) {
    if (requested && !otg_suspended_for_external_power_) {
      LogMessage(LogLevel::kInfo, __FILE__, __LINE__,
          "External power connected; OTG reverse power suspended\n");
    }
    otg_suspended_for_external_power_ = requested;
    otg_external_power_removed_tick_ = 0;
  } else if (!requested) {
    otg_suspended_for_external_power_ = false;
    otg_external_power_removed_tick_ = 0;
  } else if (otg_suspended_for_external_power_) {
    const TickType_t current_tick = xTaskGetTickCount();
    if (otg_external_power_removed_tick_ == 0) {
      otg_external_power_removed_tick_ = current_tick;
      enable_hardware = false;
    } else if (current_tick - otg_external_power_removed_tick_ <
               pdMS_TO_TICKS(kOtgRestoreDelayMs)) {
      enable_hardware = false;
    }
  }

  if (!otg_hardware_state_known_ ||
      otg_hardware_enabled_ != enable_hardware) {
    if (!otg->SetOtgPowerEnabled(enable_hardware)) {
      otg_hardware_state_known_ = false;
      return false;
    }
    otg_hardware_enabled_ = enable_hardware;
    otg_hardware_state_known_ = true;
    if (enable_hardware && otg_suspended_for_external_power_) {
      otg_suspended_for_external_power_ = false;
      otg_external_power_removed_tick_ = 0;
      LogMessage(LogLevel::kInfo, __FILE__, __LINE__,
          "OTG reverse power restored after external power removal\n");
    }
  }

  return !enable_hardware || otg->UpdateOtgPowerState();
}

void Application::RunVolumeButtonTask() {
  hal::DeviceProvider* device = device_provider_context_.device;
  if (device == nullptr) {
    return;
  }

  RepeatingButtonRecognizer volume_up_recognizer;
  RepeatingButtonRecognizer volume_down_recognizer;
  int adjusted_volume_percent = 0;
  bool adjustment_active = false;
  bool volume_up_limit_feedback_sent = false;
  bool volume_down_limit_feedback_sent = false;
  TickType_t last_poll_ticks = xTaskGetTickCount();
  while (true) {
    const uint32_t now_ms = static_cast<uint32_t>(
        xTaskGetTickCount() * portTICK_PERIOD_MS);
    bool volume_up_pressed = false;
    bool volume_down_pressed = false;
    const bool volume_up_available =
        device->ReadVolumeUpButtonPressed(&volume_up_pressed);
    const bool volume_down_available =
        device->ReadVolumeDownButtonPressed(&volume_down_pressed);
    const bool volume_up_event = volume_up_available &&
        volume_up_recognizer.Update(volume_up_pressed, now_ms);
    const bool volume_down_event = volume_down_available &&
        volume_down_recognizer.Update(volume_down_pressed, now_ms);

    if (!power_action_in_progress_.load() &&
        physical_button_events_enabled_.load()) {
      int delta_percent = 0;
      if (volume_up_event && !volume_down_event) {
        delta_percent = kVolumeButtonStepPercent;
      } else if (volume_down_event && !volume_up_event) {
        delta_percent = -kVolumeButtonStepPercent;
      }
      if (delta_percent != 0) {
        if (!adjustment_active) {
          adjusted_volume_percent =
              app::GetSoundPreferences().volume_percent;
        }
        const int target_percent = std::clamp(
            adjusted_volume_percent + delta_percent, 0, 100);
        const bool at_upper_limit = delta_percent > 0 &&
            target_percent == adjusted_volume_percent;
        const bool at_lower_limit = delta_percent < 0 &&
            target_percent == adjusted_volume_percent;
        if ((at_upper_limit && !volume_up_limit_feedback_sent) ||
            (at_lower_limit && !volume_down_limit_feedback_sent)) {
          ui::PlayUiHapticFeedback();
        }
        volume_up_limit_feedback_sent |= at_upper_limit;
        volume_down_limit_feedback_sent |= at_lower_limit;
        if (HandleVolumeButtonValue(target_percent)) {
          adjusted_volume_percent = target_percent;
          adjustment_active = true;
        }
      }
      if (!volume_up_recognizer.pressed()) {
        volume_up_limit_feedback_sent = false;
      }
      if (!volume_down_recognizer.pressed()) {
        volume_down_limit_feedback_sent = false;
      }
      if (adjustment_active && !volume_up_recognizer.pressed() &&
          !volume_down_recognizer.pressed()) {
        ApplySpeakerVolume(adjusted_volume_percent, true);
        adjustment_active = false;
      }
    }
    xTaskDelayUntil(&last_poll_ticks, pdMS_TO_TICKS(kVolumeButtonPollMs));
  }
}

bool Application::HandleVolumeButtonValue(int volume_percent) {
  const int clamped_percent = std::clamp(volume_percent, 0, 100);
  if (!ApplySpeakerVolume(clamped_percent, false)) {
    LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
        "Apply physical volume-button change failed\n");
    return false;
  }

  if (screen_lock_state_.load() == ScreenLockState::kAsleep) {
    return true;
  }
  lvgl_port_.Lock();
  const bool shown = ui_manager_.ShowVolumeOverlay(clamped_percent,
      [this](int percent, bool commit) {
        return ApplySpeakerVolume(percent, commit);
      });
  lvgl_port_.Unlock();
  if (!shown) {
    LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
        "Show volume overlay failed\n");
  }
  return true;
}

bool Application::ApplySpeakerVolume(int percent, bool commit) {
  if (device_provider_context_.audio == nullptr) {
    return false;
  }
  const int clamped_percent = std::clamp(percent, 0, 100);
  if (!device_provider_context_.audio->SetSpeakerVolumePercent(
          clamped_percent)) {
    return false;
  }
  if (!commit) {
    return true;
  }

  app::SoundPreferences preferences = app::GetSoundPreferences();
  preferences.volume_percent = clamped_percent;
  if (!app::UpdateSoundPreferences(preferences)) {
    LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
        "Persist volume-button setting failed\n");
  }
  return true;
}

void Application::RunScreenLockTask() {
  hal::ScreenProvider* screen = device_provider_context_.screen.get();
  if (screen == nullptr) {
    return;
  }

  while (ui_manager_.IsStartupScreenActive()) {
    vTaskDelay(pdMS_TO_TICKS(kScreenLockPollMs));
  }
  physical_button_events_enabled_.store(true);

  uint32_t last_touch_ms = static_cast<uint32_t>(xTaskGetTickCount() *
      portTICK_PERIOD_MS);
  uint32_t lock_screen_last_interaction_ms = last_touch_ms;
  uint32_t screen_sleep_started_ms = last_touch_ms;
  bool unlock_touch_active = false;
  bool unlock_drag_ready = false;
  ScreenLockState observed_screen_lock_state = screen_lock_state_.load();
  bool screen_wake_input_armed =
      observed_screen_lock_state != ScreenLockState::kAsleep;
  // 仅在触摸中断不可用的轮询降级路径中锁存固件手势状态。
  bool polled_firmware_double_tap_latched = false;
  // 状态切换后丢弃触发切换的剩余触摸序列，直到确认手指释放。
  bool discard_transition_touch_until_release = false;
  hal::TouchPoint unlock_touch_start = {};
  DoubleTapRecognizer wake_double_tap_recognizer;
  DoubleTapRecognizer sleep_double_tap_recognizer;
  while (true) {
    const uint32_t now_ms = static_cast<uint32_t>(xTaskGetTickCount() *
        portTICK_PERIOD_MS);
    if (power_action_in_progress_.load()) {
      pending_power_button_action_.store(PowerButtonAction::kNone);
      pending_system_activity_reason_.store(SystemActivityReason::kNone);
      vTaskDelay(pdMS_TO_TICKS(kScreenLockPollMs));
      continue;
    }
    const PowerButtonAction power_button_action =
        pending_power_button_action_.exchange(PowerButtonAction::kNone);
    if (power_button_action != PowerButtonAction::kNone) {
      last_touch_ms = now_ms;
      lock_screen_last_interaction_ms = now_ms;
      unlock_touch_active = false;
      unlock_drag_ready = false;
      discard_transition_touch_until_release = false;
      wake_double_tap_recognizer.Reset();
      sleep_double_tap_recognizer.Reset();
      if (power_button_action == PowerButtonAction::kShortPress) {
        HandlePowerButtonShortPress();
      } else {
        LogMessage(LogLevel::kInfo, __FILE__, __LINE__,
            "Power button long press; showing power menu\n");
        ShowPowerMenuFromPhysicalButton();
      }
      vTaskDelay(pdMS_TO_TICKS(kScreenLockPollMs));
      continue;
    }
    const ScreenLockState screen_lock_state = screen_lock_state_.load();
    if (screen_lock_state != observed_screen_lock_state) {
      observed_screen_lock_state = screen_lock_state;
      polled_firmware_double_tap_latched = false;
      if (screen_lock_state == ScreenLockState::kAsleep) {
        screen_sleep_started_ms = now_ms;
        screen_wake_input_armed = false;
      }
    }
    const bool recovery_required = lvgl_port_.IsDisplayFlushPaused() &&
        (screen_lock_state != ScreenLockState::kAsleep ||
            !screen_off_confirmed_.load());
    if (recovery_required) {
      bool screen_restored = false;
      bool recovery_performed = false;
      if (lvgl_port_.BeginScreenTransition()) {
        const bool still_requires_recovery =
            lvgl_port_.IsDisplayFlushPaused() &&
            (screen_lock_state_.load() != ScreenLockState::kAsleep ||
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
      if (recovery_performed &&
          screen_lock_state_.load() != ScreenLockState::kUnlocked) {
        screen_lock_state_.store(ScreenLockState::kAwake);
        discard_transition_touch_until_release = true;
      }
      last_touch_ms = now_ms;
      lock_screen_last_interaction_ms = now_ms;
      wake_double_tap_recognizer.Reset();
      if (!recovery_performed) {
        sleep_double_tap_recognizer.Reset();
      }
    }
    const SystemActivityReason activity_reason = ConsumeSystemActivity();
    if (activity_reason != SystemActivityReason::kNone) {
      const ScreenLockState activity_screen_lock_state =
          screen_lock_state_.load();
      LogMessage(LogLevel::kInfo, __FILE__, __LINE__,
          "System activity applied (reason: %s, screen state: %u)\n",
          SystemActivityReasonName(activity_reason),
          static_cast<unsigned int>(activity_screen_lock_state));
      const bool screen_activity_applied = ApplyScreenActivity(
          &last_touch_ms, &lock_screen_last_interaction_ms);
      if (activity_screen_lock_state == ScreenLockState::kAsleep) {
        wake_double_tap_recognizer.Reset();
        sleep_double_tap_recognizer.Reset();
        unlock_touch_active = false;
        unlock_drag_ready = false;
        discard_transition_touch_until_release = false;
        if (screen_activity_applied) {
          observed_screen_lock_state = ScreenLockState::kAwake;
          screen_wake_input_armed = true;
        }
      }
      vTaskDelay(pdMS_TO_TICKS(kScreenLockPollMs));
      continue;
    }
    hal::TouchPoint point;
    if (ui_manager_.IsFirstBootWelcomeActive()) {
      last_touch_ms = now_ms;
      lock_screen_last_interaction_ms = now_ms;
      unlock_touch_active = false;
      unlock_drag_ready = false;
      discard_transition_touch_until_release = false;
      wake_double_tap_recognizer.Reset();
      sleep_double_tap_recognizer.Reset();
      vTaskDelay(pdMS_TO_TICKS(kScreenLockPollMs));
      continue;
    }
    if (physical_power_menu_active_.load()) {
      last_touch_ms = now_ms;
      lock_screen_last_interaction_ms = now_ms;
      unlock_touch_active = false;
      unlock_drag_ready = false;
      discard_transition_touch_until_release = false;
      wake_double_tap_recognizer.Reset();
      sleep_double_tap_recognizer.Reset();
      vTaskDelay(pdMS_TO_TICKS(kScreenLockPollMs));
      continue;
    }
    if (screen_lock_requested_.exchange(false)) {
      wake_double_tap_recognizer.Reset();
      sleep_double_tap_recognizer.Reset();
      discard_transition_touch_until_release = false;
      if (screen_lock_state_.load() == ScreenLockState::kUnlocked) {
        if (LockScreenNow()) {
          discard_transition_touch_until_release = true;
        } else {
          last_touch_ms = now_ms;
        }
      }
      vTaskDelay(pdMS_TO_TICKS(kScreenLockPollMs));
      continue;
    }
    if (screen_lock_state_.load() != ScreenLockState::kUnlocked) {
      if (screen_lock_state_.load() == ScreenLockState::kAsleep) {
        const app::DisplayPreferences preferences =
            LoadDisplayPreferencesOrDefault();
        if (!preferences.lock_screen_double_tap_to_turn_screen_on_and_off) {
          wake_double_tap_recognizer.Reset();
          sleep_double_tap_recognizer.Reset();
          polled_firmware_double_tap_latched = false;
          discard_transition_touch_until_release = false;
          vTaskDelay(pdMS_TO_TICKS(kScreenLockPollMs));
          continue;
        }

        hal::TouchPoint screen_off_point;
        bool touch_access_available = false;
        const bool screen_off_sample_available =
            ReadScreenTouchWhileSleeping(
                &screen_off_point, &touch_access_available);
        const bool firmware_double_tap_active =
            screen_off_sample_available &&
            screen_off_point.gesture == hal::TouchGesture::kDoubleTap;
        const bool touch_interrupt_supported =
            screen->SupportsTouchInterrupt();
        const bool firmware_double_tap_detected =
            touch_access_available && firmware_double_tap_active &&
            (touch_interrupt_supported ||
                !polled_firmware_double_tap_latched);
        if (!touch_interrupt_supported && touch_access_available) {
          polled_firmware_double_tap_latched = firmware_double_tap_active;
        }
        const bool screen_off_touched =
            screen_off_sample_available &&
            IsScreenTouchPointValid(
                screen_off_point, screen->ScreenWidth(), screen->ScreenHeight());
        if (firmware_double_tap_detected) {
          LogMessage(LogLevel::kDebug, __FILE__, __LINE__,
              "Lock screen firmware double-tap report received (armed: %s)\n",
              screen_wake_input_armed ? "yes" : "no");
        }
        if (!screen_wake_input_armed) {
          wake_double_tap_recognizer.Reset();
          const bool input_guard_elapsed =
              now_ms - screen_sleep_started_ms >= kScreenWakeInputGuardMs;
          if (input_guard_elapsed) {
            screen_wake_input_armed = true;
            discard_transition_touch_until_release = false;
            LogMessage(LogLevel::kDebug, __FILE__, __LINE__,
                "Lock screen wake input armed (guard: %u ms)\n",
                static_cast<unsigned int>(kScreenWakeInputGuardMs));
          }
          vTaskDelay(pdMS_TO_TICKS(kScreenTouchPollMs));
          continue;
        }
        if (touch_access_available &&
            discard_transition_touch_until_release) {
          wake_double_tap_recognizer.Reset();
          discard_transition_touch_until_release = screen_off_touched;
        } else if (touch_access_available) {
          const DoubleTapEvent event = firmware_double_tap_detected
                                           ? DoubleTapEvent::kCompleted
                                           : wake_double_tap_recognizer.Update(
                                                 screen_off_touched
                                                     ? &screen_off_point
                                                     : nullptr,
                                                 now_ms);
          if (event == DoubleTapEvent::kSecondPressConfirmed ||
              event == DoubleTapEvent::kCompleted) {
            LogMessage(LogLevel::kDebug, __FILE__, __LINE__,
                "Lock screen wake requested (source: %s)\n",
                firmware_double_tap_detected ? "firmware gesture"
                                             : "software double tap");
            const bool screen_woke = ApplyScreenActivity(
                &last_touch_ms, &lock_screen_last_interaction_ms);
            wake_double_tap_recognizer.Reset();
            if (screen_woke) {
              // 唤醒在第二次按下阶段完成时，丢弃同一触摸剩余的抬手事件。
              discard_transition_touch_until_release =
                  event == DoubleTapEvent::kSecondPressConfirmed;
              sleep_double_tap_recognizer.Reset();
              ui::PlayUiHapticFeedback();
            } else {
              sleep_double_tap_recognizer.Reset();
              discard_transition_touch_until_release = true;
            }
          }
        }
        vTaskDelay(pdMS_TO_TICKS(kScreenTouchPollMs));
        continue;
      }

      const app::DisplayPreferences preferences =
          LoadDisplayPreferencesOrDefault();
      bool touch_access_available = false;
      const bool touched =
          ReadScreenTouchWhileAwake(&point, &touch_access_available) &&
          IsScreenTouchPointValid(
              point, screen->ScreenWidth(), screen->ScreenHeight());
      if (!touch_access_available) {
        vTaskDelay(pdMS_TO_TICKS(kScreenTouchPollMs));
        continue;
      }
      if (discard_transition_touch_until_release) {
        sleep_double_tap_recognizer.Reset();
        unlock_touch_active = false;
        unlock_drag_ready = false;
        discard_transition_touch_until_release = touched;
        vTaskDelay(pdMS_TO_TICKS(kScreenTouchPollMs));
        continue;
      }
      DoubleTapEvent double_tap_event = DoubleTapEvent::kNone;
      if (preferences.lock_screen_double_tap_to_turn_screen_on_and_off) {
        double_tap_event = sleep_double_tap_recognizer.Update(
            touched ? &point : nullptr, now_ms);
      } else {
        sleep_double_tap_recognizer.Reset();
      }
      if (touched) {
        ApplyScreenActivity(
            &last_touch_ms, &lock_screen_last_interaction_ms);
        if (!unlock_touch_active) {
          unlock_touch_start = point;
          unlock_touch_active = true;
          unlock_drag_ready = false;
        } else {
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
          const bool lock_screen_double_tap_to_turn_screen_off =
              preferences.lock_screen_double_tap_to_turn_screen_on_and_off &&
              double_tap_event == DoubleTapEvent::kCompleted;
          if (unlock_drag_ready) {
            wake_double_tap_recognizer.Reset();
            sleep_double_tap_recognizer.Reset();
            lvgl_port_.Lock();
            ui_manager_.PlayLockScreenUnlockAnimation();
            lvgl_port_.Unlock();
            vTaskDelay(pdMS_TO_TICKS(kScreenUnlockAnimationWaitMs));
            UnlockScreen();
            ApplyScreenActivity(
                &last_touch_ms, &lock_screen_last_interaction_ms);
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
              unlock_touch_active = false;
              unlock_drag_ready = false;
              if (SleepLockScreenNow()) {
                // 双击熄屏在第二次触摸释放后才成立，不向熄屏态传递该手势。
                wake_double_tap_recognizer.Reset();
                sleep_double_tap_recognizer.Reset();
                discard_transition_touch_until_release = false;
                ui::PlayUiHapticFeedback();
                vTaskDelay(pdMS_TO_TICKS(kScreenTouchPollMs));
                continue;
              }
              wake_double_tap_recognizer.Reset();
              sleep_double_tap_recognizer.Reset();
              ApplyScreenActivity(
                  &last_touch_ms, &lock_screen_last_interaction_ms);
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
          const bool screen_slept = SleepAwakeLockScreenWithTimeout(
              &last_touch_ms, &lock_screen_last_interaction_ms);
          if (!screen_slept) {
            const uint32_t activity_ms = static_cast<uint32_t>(
                xTaskGetTickCount() * portTICK_PERIOD_MS);
            last_touch_ms = activity_ms;
            lock_screen_last_interaction_ms = activity_ms;
          }
          wake_double_tap_recognizer.Reset();
          sleep_double_tap_recognizer.Reset();
          discard_transition_touch_until_release = !screen_slept;
          unlock_touch_active = false;
          unlock_drag_ready = false;
        }
      }
      vTaskDelay(pdMS_TO_TICKS(kScreenTouchPollMs));
      continue;
    }

    wake_double_tap_recognizer.Reset();
    sleep_double_tap_recognizer.Reset();
    discard_transition_touch_until_release = false;
    if (ReadScreenTouchWhileAwake(&point)) {
      ApplyScreenActivity(
          &last_touch_ms, &lock_screen_last_interaction_ms);
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

    bool transition_expected = false;
    if (!screen_lock_transition_in_progress_.compare_exchange_strong(
            transition_expected, true)) {
      vTaskDelay(pdMS_TO_TICKS(kScreenLockPollMs));
      continue;
    }

    const int start_brightness = preferences.brightness_percent;
    const int target_brightness =
        app::kUserDisplayBrightnessMinPercent;
    bool fade_canceled = false;
    bool screen_access_interrupted = false;
    SystemActivityReason transition_activity_reason =
        ConsumeScreenTransitionActivity();
    if (transition_activity_reason != SystemActivityReason::kNone) {
      LogMessage(LogLevel::kInfo, __FILE__, __LINE__,
          "System activity canceled screen dimming (reason: %s)\n",
          SystemActivityReasonName(transition_activity_reason));
      ApplyScreenActivity(
          &last_touch_ms, &lock_screen_last_interaction_ms);
      screen_lock_transition_in_progress_.store(false);
      continue;
    }
    lvgl_port_.SetInputBlocked(true);
    if (!FadeScreenBrightnessTo(target_brightness, kScreenLockFadeMs)) {
      lvgl_port_.SetInputBlocked(false);
      screen_lock_transition_in_progress_.store(false);
      last_touch_ms = now_ms;
      continue;
    }

    const uint32_t confirm_start_ms = static_cast<uint32_t>(
        xTaskGetTickCount() * portTICK_PERIOD_MS);
    while (static_cast<uint32_t>(xTaskGetTickCount() * portTICK_PERIOD_MS) -
               confirm_start_ms <
           sleep_confirm_ms) {
      transition_activity_reason = ConsumeScreenTransitionActivity();
      if (transition_activity_reason != SystemActivityReason::kNone) {
        LogMessage(LogLevel::kInfo, __FILE__, __LINE__,
            "System activity restored screen brightness (reason: %s)\n",
            SystemActivityReasonName(transition_activity_reason));
        ApplyScreenActivity(&last_touch_ms,
            &lock_screen_last_interaction_ms, start_brightness);
        screen_lock_transition_in_progress_.store(false);
        fade_canceled = true;
        break;
      }
      bool touch_access_available = false;
      const bool touched = ReadScreenTouchWhileAwake(
          &point, &touch_access_available);
      if (!touch_access_available) {
        screen_access_interrupted = true;
        break;
      }
      if (touched) {
        ApplyScreenActivity(&last_touch_ms,
            &lock_screen_last_interaction_ms, start_brightness);
        screen_lock_transition_in_progress_.store(false);
        fade_canceled = true;
        break;
      }
      vTaskDelay(pdMS_TO_TICKS(kScreenTouchPollMs));
    }
    if (screen_access_interrupted) {
      FadeScreenBrightnessTo(start_brightness, kScreenLockFadeMs);
      lvgl_port_.SetInputBlocked(false);
      screen_lock_transition_in_progress_.store(false);
      last_touch_ms = now_ms;
      continue;
    }
    if (fade_canceled) {
      continue;
    }

    transition_activity_reason = ConsumeScreenTransitionActivity();
    if (transition_activity_reason != SystemActivityReason::kNone) {
      LogMessage(LogLevel::kInfo, __FILE__, __LINE__,
          "System activity canceled automatic screen lock (reason: %s)\n",
          SystemActivityReasonName(transition_activity_reason));
      ApplyScreenActivity(&last_touch_ms,
          &lock_screen_last_interaction_ms, start_brightness);
      screen_lock_transition_in_progress_.store(false);
      continue;
    }

    if (!WaitForKeyboardExpansionConnectionIdle()) {
      LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
          "Cancel automatic screen lock because keyboard expansion "
          "connection update did not finish\n");
      FadeScreenBrightnessTo(start_brightness, kScreenLockFadeMs);
      lvgl_port_.SetInputBlocked(false);
      screen_lock_transition_in_progress_.store(false);
      last_touch_ms = now_ms;
      continue;
    }
    transition_activity_reason = ConsumeScreenTransitionActivity();
    if (transition_activity_reason != SystemActivityReason::kNone) {
      LogMessage(LogLevel::kInfo, __FILE__, __LINE__,
          "System activity canceled screen sleep (reason: %s)\n",
          SystemActivityReasonName(transition_activity_reason));
      ApplyScreenActivity(&last_touch_ms,
          &lock_screen_last_interaction_ms, start_brightness);
      screen_lock_transition_in_progress_.store(false);
      continue;
    }
    if (EnterScreenLockSleep()) {
      screen_lock_state_.store(ScreenLockState::kAsleep);
    } else {
      lvgl_port_.Lock();
      ui_manager_.HideLockScreen();
      lvgl_port_.Unlock();
      FadeScreenBrightnessTo(start_brightness, kScreenLockFadeMs);
      lvgl_port_.SetInputBlocked(false);
      last_touch_ms = static_cast<uint32_t>(xTaskGetTickCount() *
          portTICK_PERIOD_MS);
    }
    screen_lock_transition_in_progress_.store(false);
  }
}

void Application::HandlePowerButtonShortPress() {
  if (power_action_in_progress_.load()) {
    return;
  }

  const bool physical_menu_was_active =
      physical_power_menu_active_.exchange(false);
  lvgl_port_.Lock();
  const bool power_menu_visible = ui_manager_.IsPowerMenuVisible();
  if (power_menu_visible) {
    ui_manager_.HidePowerMenu();
  }
  lvgl_port_.Unlock();
  if (physical_menu_was_active &&
      screen_lock_state_.load() != ScreenLockState::kUnlocked) {
    lvgl_port_.SetInputBlocked(true);
  }

  bool result = false;
  const char* action = "unknown";
  switch (screen_lock_state_.load()) {
    case ScreenLockState::kUnlocked:
      action = "lock and sleep";
      result = LockScreenNow();
      break;
    case ScreenLockState::kAwake:
      action = "sleep";
      result = SleepLockScreenNow();
      break;
    case ScreenLockState::kAsleep:
      action = "wake";
      result = WakeScreenFromLock();
      break;
  }
  if (!result) {
    LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
        "Screen lock or wake action failed: %s\n", action);
  }
}

void Application::ShowPowerMenuFromPhysicalButton() {
  if (power_action_in_progress_.load()) {
    return;
  }

  lvgl_port_.Lock();
  const bool already_visible = ui_manager_.IsPowerMenuVisible();
  lvgl_port_.Unlock();
  if (already_visible) {
    return;
  }

  if (screen_lock_state_.load() == ScreenLockState::kAsleep &&
      !WakeScreenFromLock()) {
    LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
        "Wake screen before showing power menu failed\n");
    return;
  }

  const bool restore_lock_input =
      screen_lock_state_.load() != ScreenLockState::kUnlocked;
  if (restore_lock_input) {
    // 全局电源操作页可在锁屏页上选择操作；退出后恢复锁屏输入限制。
    lvgl_port_.SetInputBlocked(false);
  }
  physical_power_menu_active_.store(true);
  lvgl_port_.Lock();
  const bool shown = ui_manager_.ShowPowerMenu(
      [this]() { RestartDevice(); },
      [this]() { PowerOffDevice(); },
      [this, restore_lock_input]() {
        physical_power_menu_active_.store(false);
        if (restore_lock_input && !power_action_in_progress_.load() &&
            screen_lock_state_.load() != ScreenLockState::kUnlocked) {
          lvgl_port_.SetInputBlocked(true);
        }
      });
  lvgl_port_.Unlock();
  if (!shown) {
    physical_power_menu_active_.store(false);
    if (restore_lock_input) {
      lvgl_port_.SetInputBlocked(true);
    }
    LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
        "Show power menu from physical button failed\n");
  }
}

void Application::RequestScreenLock() {
  screen_lock_requested_.store(true);
}

bool Application::WaitForKeyboardExpansionConnectionIdle() {
  uint32_t waited_ms = 0;
  while ((keyboard_expansion_connection_update_in_progress_.load() ||
         keyboard_expansion_scan_pending_.load()) &&
      waited_ms < kKeyboardExpansionConnectionUpdateWaitMs) {
    vTaskDelay(pdMS_TO_TICKS(kScreenLockPollMs));
    waited_ms += kScreenLockPollMs;
  }
  return !keyboard_expansion_connection_update_in_progress_.load() &&
      !keyboard_expansion_scan_pending_.load();
}

bool Application::LockScreenNow() {
  if (screen_lock_state_.load() != ScreenLockState::kUnlocked) {
    return true;
  }
  bool transition_expected = false;
  if (!screen_lock_transition_in_progress_.compare_exchange_strong(
          transition_expected, true)) {
    return false;
  }
  if (!WaitForKeyboardExpansionConnectionIdle()) {
    LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
        "Cancel screen lock because keyboard expansion connection update "
        "did not finish\n");
    screen_lock_transition_in_progress_.store(false);
    return false;
  }
  const int previous_brightness = current_screen_brightness_percent_.load();
  lvgl_port_.SetInputBlocked(true);
  if (!EnterScreenLockSleep()) {
    FadeScreenBrightnessTo(previous_brightness, kScreenLockFadeMs);
    lvgl_port_.SetInputBlocked(false);
    screen_lock_transition_in_progress_.store(false);
    return false;
  }
  screen_lock_state_.store(ScreenLockState::kAsleep);
  screen_lock_transition_in_progress_.store(false);
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

bool Application::WakeScreenFromLock() {
  hal::ScreenProvider* screen = device_provider_context_.screen.get();
  if (screen == nullptr ||
      screen_lock_state_.load() != ScreenLockState::kAsleep) {
    LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
        "Lock screen wake failed (screen available: %s, state: %u)\n",
        screen != nullptr ? "yes" : "no",
        static_cast<unsigned int>(screen_lock_state_.load()));
    return false;
  }

  if (!lvgl_port_.BeginScreenTransition()) {
    LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
        "Lock screen wake failed (screen transition unavailable)\n");
    return false;
  }
  lvgl_port_.Lock();
  const bool shown = ui_manager_.ShowLockScreen();
  lvgl_port_.Unlock();
  if (!shown) {
    lvgl_port_.EndScreenTransition();
    LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
        "Lock screen wake failed (lock screen unavailable)\n");
    return false;
  }
  const bool screen_restored = RestoreScreenAfterSleep();
  lvgl_port_.EndScreenTransition();
  if (!screen_restored) {
    LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
        "Lock screen wake failed (display restore failed)\n");
    return false;
  }
  screen_lock_state_.store(ScreenLockState::kAwake);
  LogMessage(LogLevel::kDebug, __FILE__, __LINE__,
      "Lock screen wake success (brightness: %d%%)\n",
      current_screen_brightness_percent_.load());
  return true;
}

void Application::RestartDevice() {
  bool expected = false;
  if (!power_action_in_progress_.compare_exchange_strong(expected, true)) {
    return;
  }
  hal::DeviceProvider* device = device_provider_context_.device;
  if (device != nullptr && device->SupportsPowerOffCharging() &&
      !app::WritePowerOffRequested(false)) {
    LogMessage(LogLevel::kError, __FILE__, __LINE__,
        "Clear persistent power-off state before restart failed\n");
    power_action_in_progress_.store(false);
    return;
  }
  vTaskDelay(pdMS_TO_TICKS(kPowerActionPreSleepSettleMs));
  if (!PreparePowerActionStorage()) {
    power_action_in_progress_.store(false);
    return;
  }
  // 重启仅复位处理器，不进入设备关机准备，避免关闭外设电源轨。
  LogMessage(LogLevel::kInfo, __FILE__, __LINE__,
      "Restarting system without powering off device rails\n");
  RestartSystem();
}

void Application::RestartSystem() {
  lvgl_port_.SetInputBlocked(true);
  if (current_screen_brightness_percent_.load() != 0 &&
      !ApplyScreenBrightness(0)) {
    LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
        "Turn off screen backlight before restart failed\n");
  }
  if (!lvgl_port_.IsDisplayFlushPaused() &&
      !lvgl_port_.PauseDisplayFlush()) {
    LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
        "Pause display refresh before restart failed\n");
  }
  hal::ScreenProvider* screen = device_provider_context_.screen.get();
  if (screen != nullptr && !screen->EnterDeviceSleep(false)) {
    LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
        "Put screen to sleep before restart failed\n");
  }
  vTaskDelay(pdMS_TO_TICKS(kPowerActionPreSleepSettleMs));
  esp_restart();
}

void Application::PowerOffDevice() {
  bool expected = false;
  if (!power_action_in_progress_.compare_exchange_strong(expected, true)) {
    return;
  }
  hal::DeviceProvider* device = device_provider_context_.device;
  const bool persist_power_off =
      device != nullptr && device->SupportsPowerOffCharging();
  if (persist_power_off && !app::WritePowerOffRequested(true)) {
    LogMessage(LogLevel::kError, __FILE__, __LINE__,
        "Persist power-off state failed; canceling shutdown\n");
    power_action_in_progress_.store(false);
    return;
  }
  vTaskDelay(pdMS_TO_TICKS(kPowerActionPreSleepSettleMs));
  if (!PreparePowerActionStorage()) {
    if (persist_power_off && !app::WritePowerOffRequested(false)) {
      LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
          "Clear persistent power-off state after canceled shutdown failed\n");
    }
    power_action_in_progress_.store(false);
    return;
  }
  const hal::PowerOffAction action =
      device == nullptr ? hal::PowerOffAction::kFailed
                        : device->RequestPowerOff();
  if (action == hal::PowerOffAction::kShowChargingScreen) {
    if (!WakeScreenForPowerOffCharging()) {
      LogMessage(LogLevel::kError, __FILE__, __LINE__,
          "Wake screen for power-off charging failed\n");
      ReturnToPowerOffStateAfterChargingScreen();
      return;
    }
    RunPowerOffChargingScreen();
    return;
  }
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
  if (persist_power_off && !app::WritePowerOffRequested(false)) {
    LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
        "Clear persistent power-off state after shutdown failure failed\n");
  }
  const bool screen_restored = RestoreScreenAfterSleep();
  lvgl_port_.EndScreenTransition();
  lvgl_port_.SetInputBlocked(false);
  power_action_in_progress_.store(false);
  if (!screen_restored) {
    LogMessage(LogLevel::kError, __FILE__, __LINE__,
        "Power off failed and screen recovery also failed\n");
  }
}

bool Application::SleepAwakeLockScreenWithTimeout(
    uint32_t* last_touch_ms,
    uint32_t* lock_screen_last_interaction_ms) {
  hal::ScreenProvider* screen = device_provider_context_.screen.get();
  if (screen == nullptr) {
    return false;
  }
  bool transition_expected = false;
  if (!screen_lock_transition_in_progress_.compare_exchange_strong(
          transition_expected, true)) {
    return false;
  }
  if (!WaitForKeyboardExpansionConnectionIdle()) {
    screen_lock_transition_in_progress_.store(false);
    return false;
  }

  const app::DisplayPreferences preferences = LoadDisplayPreferencesOrDefault();
  SystemActivityReason activity_reason = ConsumeScreenTransitionActivity();
  if (activity_reason != SystemActivityReason::kNone) {
    LogMessage(LogLevel::kInfo, __FILE__, __LINE__,
        "System activity canceled lock-screen dimming (reason: %s)\n",
        SystemActivityReasonName(activity_reason));
    ApplyScreenActivity(last_touch_ms, lock_screen_last_interaction_ms);
    screen_lock_transition_in_progress_.store(false);
    return false;
  }
  const int target_brightness =
      app::kUserDisplayBrightnessMinPercent;
  if (!FadeScreenBrightnessTo(target_brightness, kScreenLockFadeMs)) {
    screen_lock_state_.store(ScreenLockState::kAwake);
    screen_lock_transition_in_progress_.store(false);
    return false;
  }

  const uint32_t confirm_start_ms =
      static_cast<uint32_t>(xTaskGetTickCount() * portTICK_PERIOD_MS);
  while (static_cast<uint32_t>(xTaskGetTickCount() * portTICK_PERIOD_MS) -
             confirm_start_ms <
         kScreenLockSleepConfirmMs) {
    activity_reason = ConsumeScreenTransitionActivity();
    if (activity_reason != SystemActivityReason::kNone) {
      LogMessage(LogLevel::kInfo, __FILE__, __LINE__,
          "System activity restored lock-screen brightness (reason: %s)\n",
          SystemActivityReasonName(activity_reason));
      ApplyScreenActivity(last_touch_ms, lock_screen_last_interaction_ms,
          preferences.brightness_percent);
      screen_lock_transition_in_progress_.store(false);
      return false;
    }
    hal::TouchPoint point;
    bool touch_access_available = false;
    const bool touched = ReadScreenTouchWhileAwake(
        &point, &touch_access_available);
    if (!touch_access_available) {
      FadeScreenBrightnessTo(
          preferences.brightness_percent, kScreenLockFadeMs);
      screen_lock_state_.store(ScreenLockState::kAwake);
      screen_lock_transition_in_progress_.store(false);
      return false;
    }
    if (touched) {
      ApplyScreenActivity(last_touch_ms, lock_screen_last_interaction_ms,
          preferences.brightness_percent);
      screen_lock_transition_in_progress_.store(false);
      return false;
    }
    vTaskDelay(pdMS_TO_TICKS(kScreenTouchPollMs));
  }

  activity_reason = ConsumeScreenTransitionActivity();
  if (activity_reason != SystemActivityReason::kNone) {
    LogMessage(LogLevel::kInfo, __FILE__, __LINE__,
        "System activity canceled lock-screen sleep (reason: %s)\n",
        SystemActivityReasonName(activity_reason));
    ApplyScreenActivity(last_touch_ms, lock_screen_last_interaction_ms,
        preferences.brightness_percent);
    screen_lock_transition_in_progress_.store(false);
    return false;
  }

  if (!EnterScreenSleep()) {
    FadeScreenBrightnessTo(
        preferences.brightness_percent, kScreenLockFadeMs);
    screen_lock_state_.store(ScreenLockState::kAwake);
    screen_lock_transition_in_progress_.store(false);
    return false;
  }

  lvgl_port_.SetInputBlocked(true);
  screen_lock_state_.store(ScreenLockState::kAsleep);
  screen_lock_transition_in_progress_.store(false);
  return true;
}

bool Application::SleepLockScreenNow() {
  if (screen_lock_state_.load() != ScreenLockState::kAwake) {
    return false;
  }
  bool transition_expected = false;
  if (!screen_lock_transition_in_progress_.compare_exchange_strong(
          transition_expected, true)) {
    return false;
  }
  if (!WaitForKeyboardExpansionConnectionIdle() || !EnterScreenSleep()) {
    screen_lock_transition_in_progress_.store(false);
    return false;
  }
  lvgl_port_.SetInputBlocked(true);
  screen_lock_state_.store(ScreenLockState::kAsleep);
  screen_lock_transition_in_progress_.store(false);
  LogMessage(LogLevel::kDebug, __FILE__, __LINE__,
      "Lock screen sleep success (reason: double tap)\n");
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
  LogMessage(LogLevel::kDebug, __FILE__, __LINE__,
      "Lock screen display sleep started (brightness: %d%%, flush paused: %s)\n",
      previous_brightness, lvgl_port_.IsDisplayFlushPaused() ? "yes" : "no");
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
  LogMessage(LogLevel::kDebug, __FILE__, __LINE__,
      "Lock screen display sleep completed\n");

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

  LogMessage(LogLevel::kDebug, __FILE__, __LINE__,
      "Lock screen display wake started\n");
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
  LogMessage(LogLevel::kDebug, __FILE__, __LINE__,
      "Lock screen display wake completed (brightness: %d%%)\n",
      preferences.brightness_percent);
  lvgl_port_.ReleaseSleepInputBlock();
  return true;
}

void Application::UnlockScreen() {
  lvgl_port_.Lock();
  ui_manager_.HideLockScreen();
  lvgl_port_.Unlock();
  lvgl_port_.SetInputBlocked(false);
  screen_lock_state_.store(ScreenLockState::kUnlocked);
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

bool Application::ReadScreenTouchWhileSleeping(
    hal::TouchPoint* point, bool* access_available) {
  if (access_available != nullptr) {
    *access_available = false;
  }
  hal::ScreenProvider* screen = device_provider_context_.screen.get();
  if (point == nullptr || screen == nullptr ||
      power_action_in_progress_.load() ||
      screen_lock_state_.load() != ScreenLockState::kAsleep ||
      !screen_off_confirmed_.load() ||
      !lvgl_port_.IsDisplayFlushPaused()) {
    return false;
  }

  if (!lvgl_port_.TryBeginScreenTransition()) {
    return false;
  }

  // 有硬件中断时只读取新报告，避免把固件保持的手势值重复解释为新的
  // 双击。先取得屏幕事务所有权再消费通知，防止竞争时丢失事件。
  // 没有中断能力的设备继续使用原有轮询降级路径。
  if (screen->SupportsTouchInterrupt() &&
      !screen->ConsumeTouchInterrupt()) {
    lvgl_port_.EndScreenTransition();
    return false;
  }

  const bool can_access = screen_off_confirmed_.load() &&
      lvgl_port_.IsDisplayFlushPaused();
  if (access_available != nullptr) {
    *access_available = can_access;
  }
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
