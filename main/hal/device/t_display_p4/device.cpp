/*
 * @Description: T-Display-P4 板级初始化与电源生命周期实现
 * @Author: LILYGO_L
 * @Date: 2026-08-28 00:00:00
 * @LastEditTime: 2026-09-02 17:52:56
 * @License: GPL 3.0
 */
#include "hal/device/t_display_p4/device.h"

#include <memory>

#include "base/logger.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

namespace lilygo_box::hal {
namespace device = lilygo_device_driver::t_display_p4::device;

TDisplayP4Device::TDisplayP4Device()
    : driver_(lilygo_device_driver::TDisplayP4Driver::GetInstance()),
      tool_(std::make_unique<cpp_bus_driver::Tool>()) {
  wifi_.scan_results_mutex = xSemaphoreCreateMutex();
  radio_.mutex = xSemaphoreCreateMutex();
  cc1101_radio_.mutex = xSemaphoreCreateMutex();
  nrf24l01_radio_.mutex = xSemaphoreCreateMutex();
  nfc_.mutex = xSemaphoreCreateMutex();
}

bool TDisplayP4Device::InitDevice() {
  if (nfc_.mutex == nullptr) {
    LogMessage(LogLevel::kError, __FILE__, __LINE__,
        "Create T-Display-P4 NFC synchronization resource failed\n");
    return false;
  }
  const bool result =
      driver_.Init(lilygo_device_driver::TDisplayP4Driver::InitMode::kAsync);
  if (!result) {
    LogMessage(LogLevel::kWarning, __FILE__, __LINE__, "Init failed\n");
  }
  if (driver_.IsBq27220Ready() &&
      !SetBatteryCapacityMah(battery_capacity_mah_.load())) {
    LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
        "Apply configured battery capacity failed\n");
  }

  if (!WaitForScreenReady()) {
    LogMessage(
        LogLevel::kError, __FILE__, __LINE__, "WaitForScreenReady failed\n");
    return false;
  }
  if (!WaitForTouchReady()) {
    LogMessage(
        LogLevel::kError, __FILE__, __LINE__, "WaitForTouchReady failed\n");
    return false;
  }
  if (!driver_.SetScreenSleep(false)) {
    LogMessage(
        LogLevel::kError, __FILE__, __LINE__, "Activate screen failed\n");
    return false;
  }
  if (!InitializeTouchInterrupt()) {
    LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
        "Initialize touch interrupt failed; using polling fallback\n");
  }
  return true;
}

PowerOffAction TDisplayP4Device::RequestPowerOff() {
  if (!PrepareForPowerOff()) {
    LogMessage(LogLevel::kError, __FILE__, __LINE__,
        "Prepare device for power off failed\n");
    return PowerOffAction::kFailed;
  }
  if (!driver_.PrepareDriversForPowerOff()) {
    LogMessage(LogLevel::kError, __FILE__, __LINE__,
        "Prepare device hardware for power off failed\n");
    return PowerOffAction::kFailed;
  }
  return PowerOffAction::kEnterDeepSleep;
}

int TDisplayP4Device::ScreenWidth() const {
  return driver_.screen_info().width;
}

int TDisplayP4Device::ScreenHeight() const {
  return driver_.screen_info().height;
}

int TDisplayP4Device::ScreenBitsPerPixel() const {
  return driver_.screen_info().bits_per_pixel;
}

bool TDisplayP4Device::ReadDeviceInfo(DeviceInfo* info) {
  if (info == nullptr) {
    return false;
  }

  const auto device_info = driver_.device_info();
  info->device_model_name = device_info.model.name;
  info->device_model_version = device_info.model.version;
  info->screen_type = device_info.screen.name;
  info->screen_width = device_info.screen.width;
  info->screen_height = device_info.screen.height;
  info->screen_bits_per_pixel = device_info.screen.bits_per_pixel;
  info->screen_pixel_format = device_info.screen.pixel_format;
  info->camera_name = device_info.camera.name;
  info->camera_pixel_format = device_info.camera.pixel_format;
  info->camera_bits_per_pixel = device_info.camera.bits_per_pixel;
  info->camera_buffer_count = device_info.camera.buffer_count;
  info->battery_charger_chip_name = device_info.battery.charger_chip_name;
  info->battery_fuel_gauge_chip_name = device_info.battery.fuel_gauge_chip_name;
  info->battery_capacity_mah = device_info.battery.capacity_mah;
  return true;
}

bool TDisplayP4Device::ReadDeviceDiagnostics(DeviceDiagnostics* diagnostics) {
  if (diagnostics == nullptr) {
    return false;
  }

  *diagnostics = DeviceDiagnostics();
  const bool battery_management_result =
      ReadBatteryManagementStatus(&diagnostics->battery_management);
  const bool imu_result = ReadImuStatus(&diagnostics->imu);
  return battery_management_result || imu_result;
}

bool TDisplayP4Device::EnterDeviceSleep(bool deep_sleep) {
  if (!deep_sleep && !WaitForScreenReady()) {
    return false;
  }
  if (!deep_sleep) {
    if (keyboard_expansion_.task_running.load()) {
      if (!WaitForKeyboardExpansionTask()) {
        return false;
      }
    }
    const bool keyboard_expansion_slept =
        keyboard_expansion_.state.load() != KeyboardExpansionState::kReady ||
        driver_.SetKeyboardExpansionOperatingMode(lilygo_device_driver::
                TDisplayP4Driver::KeyboardExpansionOperatingMode::kSleep);
    touch_gesture_wake_enabled_ = SetTouchGestureWakeEnabled(true);
    const bool screen_slept = driver_.SetScreenSleep(true);
    if (!screen_slept && touch_gesture_wake_enabled_) {
      SetTouchGestureWakeEnabled(false);
      touch_gesture_wake_enabled_ = false;
    }
    if (!keyboard_expansion_slept) {
      LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
          "Sleep keyboard expansion failed; continue sleeping the screen\n");
    }
    return screen_slept;
  }

  const bool prepared = PrepareForPowerOff();
  if (!prepared) {
    LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
        "Prepare device for power off failed\n");
    return false;
  }
  return driver_.PrepareDriversForPowerOff();
}

bool TDisplayP4Device::RestoreKeyboardExpansionOperatingState() {
  if (keyboard_expansion_.state.load() != KeyboardExpansionState::kReady) {
    return true;
  }

  bool keyboard_state_restored = SetKeyboardBacklightBrightnessPercent(
      keyboard_expansion_.backlight_brightness_percent.load());
  keyboard_state_restored &=
      SetKeyboardExpansionLed(KeyboardExpansionLed::kLed1,
          keyboard_expansion_.caps_lock_enabled.load());
  RadioState* extension_states[] = {&cc1101_radio_, &nrf24l01_radio_};
  for (RadioState* state : extension_states) {
    if (!state->active || state->mutex == nullptr ||
        xSemaphoreTake(state->mutex, pdMS_TO_TICKS(50)) != pdTRUE) {
      continue;
    }
    if (state->chip == radio::ChipType::kCc1101 && driver_.IsCc1101Ready()) {
      auto* radio = driver_.chip().cc1101.get();
      bool restored =
          driver_.SetCc1101OperatingMode(lilygo_device_driver::
                  TDisplayP4Driver::Cc1101OperatingMode::kStandby) &&
          radio != nullptr && InitializeCc1101ReceiveInterrupt();
      if (restored) {
        state->receive_interrupt_pending.store(
            false, std::memory_order_relaxed);
        restored = radio->StartReceive();
      }
      state->chip_error = !restored;
      state->active = restored;
      keyboard_state_restored &= restored;
    } else if (state->chip == radio::ChipType::kNrf24l01 &&
               driver_.IsNrf24l01Ready()) {
      auto* radio = driver_.chip().nrf24l01.get();
      const bool restored =
          driver_.SetNrf24l01OperatingMode(lilygo_device_driver::
                  TDisplayP4Driver::Nrf24l01OperatingMode::kStandby) &&
          radio != nullptr && radio->StartReceive();
      state->chip_error = !restored;
      state->active = restored;
      keyboard_state_restored &= restored;
    }
    xSemaphoreGive(state->mutex);
  }
  return keyboard_state_restored;
}

bool TDisplayP4Device::ExitDeviceSleep(bool deep_sleep) {
  if (deep_sleep) {
    return false;
  }
  const bool result = driver_.SetScreenSleep(false);
  if (!result) {
    LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
        "Wake device from chip sleep failed\n");
    return false;
  }
  if (touch_gesture_wake_enabled_) {
    if (!SetTouchGestureWakeEnabled(false)) {
      LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
          "Disable touch gesture wake failed\n");
    }
    touch_gesture_wake_enabled_ = false;
  }
  if (!WaitForScreenReady()) {
    return false;
  }
  if (!keyboard_expansion_.screen_lock_suspended.load() &&
      !RestoreKeyboardExpansionOperatingState()) {
    LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
        "Restore keyboard expansion state failed; "
        "continue waking the screen\n");
  }
  return true;
}

bool TDisplayP4Device::PrepareForPowerOff() {
  bool result = true;

  if (speaker_.running.load()) {
    if (speaker_.playback_kind.load() ==
        SpeakerState::PlaybackKind::kAudioFile) {
      result &= StopAudioFile();
    } else {
      speaker_.stop_requested.store(true);
      speaker_.loop_enabled.store(false);
    }
  }
  if (microphone_.running.load() || microphone_.adc_to_dac_enabled.load()) {
    result &= StopMicrophone();
  }
  if (camera_preview_.task_active.load() ||
      camera_preview_.initialized.load()) {
    result &= StopCameraPreview();
  }
  if (radio_.active || radio_.transmitting || cc1101_radio_.active ||
      cc1101_radio_.transmitting || nrf24l01_radio_.active ||
      nrf24l01_radio_.transmitting) {
    result &= DeactivateRadio();
  }
  result &= SetNfcPollingEnabled(false);
  result &= SetGpsEnabled(false);
  result &= SetImuEnabled(false);
  result &= SetEthernetEnabled(false);
  result &= SetWifiEnabled(false);
  result &= StopUsbStorage();
  result &= DisableKeyboardExpansion();
  result &= WaitForPowerOffTasks();
  return result;
}

bool TDisplayP4Device::WaitForPowerOffTasks() {
  for (int elapsed_ms = 0; elapsed_ms < kPowerOffTaskTimeoutMs;
      elapsed_ms += kPowerOffTaskPollMs) {
    const bool tasks_running =
        speaker_.running.load() || haptic_.running.load() ||
        microphone_.running.load() || camera_preview_.task_active.load() ||
        ethernet_.init_task_running.load() ||
        keyboard_expansion_.task_running.load() || nfc_.task_active.load() ||
        wifi_.init_task_running.load() || wifi_.scan_task_running.load() ||
        wifi_.connect_task_running.load();
    if (!tasks_running) {
      return true;
    }
    vTaskDelay(pdMS_TO_TICKS(kPowerOffTaskPollMs));
  }
  return false;
}

}  // namespace lilygo_box::hal
