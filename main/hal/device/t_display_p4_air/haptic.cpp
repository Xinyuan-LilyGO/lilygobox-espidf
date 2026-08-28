/*
 * @Description: T-Display-P4-Air 振动硬件实现
 * @Author: LILYGO_L
 * @Date: 2026-08-28 00:00:00
 * @LastEditTime: 2026-08-28 00:00:00
 * @License: GPL 3.0
 */
#include "hal/device/t_display_p4_air/device.h"

#include <algorithm>
#include <cstdint>

#include "base/logger.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace lilygo_box::hal {
namespace {

constexpr uint32_t kHapticPlaybackTaskStackBytes = 4 * 1024;
constexpr UBaseType_t kHapticPlaybackTaskPriority = 3;
constexpr uint32_t kVibrationPreviewPlayMs = 10;
constexpr uint32_t kVibrationPreviewMinIntervalMs = 45;

}  // namespace

bool TDisplayP4AirDevice::ReadHapticWaveformCount(uint8_t* waveform_count) {
  if (waveform_count != nullptr) {
    *waveform_count = 0;
  }
  if (!driver_.IsAw86224Ready() && !driver_.InitAw86224()) {
    LogMessage(
        LogLevel::kWarning, __FILE__, __LINE__, "Aw86224 init retry failed\n");
    return false;
  }
  const auto info = cpp_bus_driver::Aw862xx::GetRamWaveformInfo(
      cpp_bus_driver::Aw862xx::RamWaveformLibrary::kRam12k041230_235);
  if (waveform_count != nullptr) {
    *waveform_count = info.waveform_count;
  }
  return info.waveform_count > 0;
}

bool TDisplayP4AirDevice::PlayHapticWaveform(uint8_t waveform_sequence_number,
    uint8_t loop_count, uint8_t gain, bool auto_brake) {
  haptic_.waveform_sequence_number.store(waveform_sequence_number);
  haptic_.loop_count.store(std::clamp<uint8_t>(loop_count, 1, 16));
  haptic_.gain.store(gain);
  haptic_.auto_brake.store(auto_brake);

  const uint32_t now_ms = static_cast<uint32_t>(
      xTaskGetTickCount() * portTICK_PERIOD_MS);
  const uint32_t last_preview_ms = haptic_.last_preview_ms.load();
  if (haptic_.running.load() ||
      now_ms - last_preview_ms < kVibrationPreviewMinIntervalMs) {
    return true;
  }
  haptic_.last_preview_ms.store(now_ms);

  bool expected = false;
  if (!haptic_.running.compare_exchange_strong(expected, true)) {
    return true;
  }

  const BaseType_t result = xTaskCreate(HapticPlaybackTaskEntry, "haptic_play",
      kHapticPlaybackTaskStackBytes, this, kHapticPlaybackTaskPriority,
      nullptr);
  if (result != pdPASS) {
    haptic_.running.store(false);
    return false;
  }
  return true;
}

void TDisplayP4AirDevice::HapticPlaybackTaskEntry(void* context) {
  auto* self = static_cast<TDisplayP4AirDevice*>(context);
  if (self != nullptr) {
    self->RunHapticPlaybackTask();
  }
  vTaskDelete(nullptr);
}

void TDisplayP4AirDevice::RunHapticPlaybackTask() {
  if (!driver_.IsAw86224Ready() && !driver_.InitAw86224()) {
    LogMessage(
        LogLevel::kWarning, __FILE__, __LINE__, "Aw86224 init retry failed\n");
    haptic_.running.store(false);
    return;
  }

  const uint8_t sequence = haptic_.waveform_sequence_number.load();
  const uint8_t loop_count = haptic_.loop_count.load();
  const uint8_t gain = haptic_.gain.load();
  const bool auto_brake = haptic_.auto_brake.load();
  LogMessage(LogLevel::kDebug, __FILE__, __LINE__,
      "Aw86224 vibration playback: sequence=%u loop=%u gain=%u auto_brake=%u\n",
      static_cast<unsigned int>(sequence),
      static_cast<unsigned int>(loop_count), static_cast<unsigned int>(gain),
      static_cast<unsigned int>(auto_brake ? 1 : 0));

  const bool needs_configure = !haptic_.ram_playback_configured ||
                               haptic_.configured_sequence_number != sequence ||
                               haptic_.configured_loop_count != loop_count ||
                               haptic_.configured_auto_brake != auto_brake;
  if (needs_configure) {
    if (!driver_.chip().aw86224->ConfigureRamPlaybackWaveform(
            sequence, loop_count - 1, gain, auto_brake)) {
      LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
          "Aw86224 ConfigureRamPlaybackWaveform failed, sequence=%u\n",
          static_cast<unsigned int>(sequence));
      driver_.SetAw86224Standby();
      haptic_.running.store(false);
      return;
    }
    haptic_.ram_playback_configured = true;
    haptic_.configured_sequence_number = sequence;
    haptic_.configured_loop_count = loop_count;
    haptic_.configured_auto_brake = auto_brake;
    haptic_.configured_gain = gain;
  } else if (haptic_.configured_gain != gain) {
    if (!driver_.chip().aw86224->SetRrtModeGain(gain)) {
      LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
          "Aw86224 SetRrtModeGain failed, gain=%u\n",
          static_cast<unsigned int>(gain));
      haptic_.running.store(false);
      return;
    }
    haptic_.configured_gain = gain;
  }

  if (!driver_.chip().aw86224->StartRamPlaybackWaveform()) {
    LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
        "Aw86224 StartRamPlaybackWaveform failed, sequence=%u\n",
        static_cast<unsigned int>(sequence));
    driver_.SetAw86224Standby();
    haptic_.running.store(false);
    return;
  }

  vTaskDelay(pdMS_TO_TICKS(kVibrationPreviewPlayMs));

  if (!driver_.SetAw86224Standby()) {
    LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
        "Aw86224 standby failed, sequence=%u\n",
        static_cast<unsigned int>(sequence));
  }

  haptic_.running.store(false);
}

}  // namespace lilygo_box::hal
