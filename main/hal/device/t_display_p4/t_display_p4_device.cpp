/*
 * @Description: None
 * @Author: LILYGO_L
 * @Date: 2026-05-10 13:27:05
 * @LastEditTime: 2026-05-10 23:29:22
 * @License: GPL 3.0
 */
#include "hal/device/t_display_p4/t_display_p4_device.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>

#include "audio/new_notification_010_c2_b16_s44100.h"
#include "base/logger.h"
#include "esp_lcd_mipi_dsi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace lilygo_box::hal {
namespace {

constexpr uint8_t kVibrationTestGain = 255;
constexpr uint8_t kVibrationTestLoopCount = 15;
constexpr uint32_t kVibrationTestPlayMs = 220;
constexpr uint32_t kVibrationTestStopMs = 180;
constexpr size_t kSpeakerTestChunkBytes = 4096;
constexpr uint32_t kSpeakerTestTaskStackBytes = 4 * 1024;
constexpr UBaseType_t kSpeakerTestTaskPriority = 3;
constexpr uint32_t kSpeakerTestSampleRateHz = 44100;
constexpr uint8_t kSpeakerTestChannelCount = 2;
constexpr uint8_t kSpeakerTestBitsPerSample = 16;

}  // namespace

TDisplayP4Device::TDisplayP4Device()
    : driver_(lilygo_device_driver::TDisplayP4Driver::GetInstance()) {}

bool TDisplayP4Device::Init() {
  const bool result =
      driver_.Init(lilygo_device_driver::TDisplayP4Driver::InitMode::kAsync);
  if (!result) {
    LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
        "TDisplayP4Driver::Init failed\n");
  }

  if (!WaitForScreenReady()) {
    LogMessage(LogLevel::kError, __FILE__, __LINE__,
        "TDisplayP4Device::WaitForScreenReady failed\n");
    return false;
  }
  return true;
}

bool TDisplayP4Device::RegisterFlushReadyCallback(
    ScreenFlushReadyCallback callback, void* callback_context) {
  if (!IsScreenReady()) {
    LogMessage(LogLevel::kError, __FILE__, __LINE__,
        "Screen is not ready for flush callback registration\n");
    return false;
  }

  flush_ready_handler_.callback = callback;
  flush_ready_handler_.context = callback_context;

  esp_lcd_dpi_panel_event_callbacks_t panel_callbacks = {
      .on_color_trans_done =
          [](esp_lcd_panel_handle_t, esp_lcd_dpi_panel_event_data_t*,
              void* user_context) -> bool {
        auto* handler = static_cast<ScreenFlushReadyHandler*>(user_context);
        if (handler != nullptr && handler->callback != nullptr) {
          handler->callback(handler->context);
        }
        return false;
      },
      .on_refresh_done =
          [](esp_lcd_panel_handle_t, esp_lcd_dpi_panel_event_data_t*,
              void*) -> bool { return false; },
  };

  const auto screen_bus = driver_.bus().screen_mipi_bus;
  if (screen_bus == nullptr) {
    LogMessage(
        LogLevel::kError, __FILE__, __LINE__, "Screen MIPI bus is empty\n");
    return false;
  }

  esp_lcd_panel_handle_t panel = screen_bus->device_handle();
  if (panel == nullptr) {
    LogMessage(
        LogLevel::kError, __FILE__, __LINE__, "Screen panel handle is empty\n");
    return false;
  }

  const int result = esp_lcd_dpi_panel_register_event_callbacks(
      panel, &panel_callbacks, &flush_ready_handler_);
  if (result != 0) {
    LogMessage(LogLevel::kError, __FILE__, __LINE__,
        "esp_lcd_dpi_panel_register_event_callbacks failed "
        "(error code: %#X)\n",
        result);
    return false;
  }
  return true;
}

bool TDisplayP4Device::WritePixels(
    int x_start, int y_start, int x_end, int y_end, const void* pixels) {
  if (!IsScreenReady()) {
    return false;
  }

#if defined(CONFIG_SCREEN_TYPE_HI8561)
  return driver_.chip().hi8561->SendColorStreamCoordinate(
      x_start, y_start, x_end, y_end, pixels);
#elif defined(CONFIG_SCREEN_TYPE_RM69A10)
  return driver_.chip().rm69a10->SendColorStreamCoordinate(
      x_start, y_start, x_end, y_end, pixels);
#endif
  return false;
}

bool TDisplayP4Device::ReadTouch(TouchPoint* point) {
  if (point == nullptr) {
    return false;
  }

  if (!IsTouchReady()) {
    return false;
  }

#if defined(CONFIG_SCREEN_TYPE_HI8561)
  cpp_bus_driver::Hi8561Touch::TouchPoint touch_point;
  const bool result =
      driver_.chip().hi8561_touch->GetSingleTouchPoint(touch_point);
  if (!result || touch_point.info.empty()) {
    return false;
  }
  point->id = 1;
  point->x = touch_point.info[0].x;
  point->y = touch_point.info[0].y;
  point->pressure = touch_point.info[0].pressure_value;
  return true;
#elif defined(CONFIG_SCREEN_TYPE_RM69A10)
  cpp_bus_driver::Gt9895::TouchPoint touch_point;
  const bool result = driver_.chip().gt9895->GetSingleTouchPoint(touch_point);
  if (!result || touch_point.info.empty()) {
    return false;
  }
  point->id = touch_point.info[0].finger_id;
  point->x = touch_point.info[0].x;
  point->y = touch_point.info[0].y;
  point->pressure = touch_point.info[0].pressure_value;
  return true;
#endif
  return false;
}

bool TDisplayP4Device::ReadTouchPoints(
    TouchPoint* points, size_t max_points, size_t* point_count) {
  if (point_count != nullptr) {
    *point_count = 0;
  }
  if (points == nullptr || max_points == 0 || point_count == nullptr) {
    return false;
  }

  if (!IsTouchReady()) {
    return false;
  }

#if defined(CONFIG_SCREEN_TYPE_HI8561)
  cpp_bus_driver::Hi8561Touch::TouchPoint touch_point;
  const bool result =
      driver_.chip().hi8561_touch->GetMultipleTouchPoint(touch_point);
  if (!result || touch_point.info.empty()) {
    return false;
  }

  const size_t count = std::min(max_points, touch_point.info.size());
  for (size_t i = 0; i < count; ++i) {
    if (touch_point.info[i].x == UINT16_MAX &&
        touch_point.info[i].y == UINT16_MAX) {
      continue;
    }
    points[*point_count].id = static_cast<uint8_t>(i + 1);
    points[*point_count].x = touch_point.info[i].x;
    points[*point_count].y = touch_point.info[i].y;
    points[*point_count].pressure = touch_point.info[i].pressure_value;
    ++(*point_count);
  }
  return *point_count > 0;
#elif defined(CONFIG_SCREEN_TYPE_RM69A10)
  cpp_bus_driver::Gt9895::TouchPoint touch_point;
  const bool result = driver_.chip().gt9895->GetMultipleTouchPoint(touch_point);
  if (!result || touch_point.info.empty()) {
    return false;
  }

  const size_t count = std::min(max_points, touch_point.info.size());
  for (size_t i = 0; i < count; ++i) {
    if (touch_point.info[i].x == UINT16_MAX &&
        touch_point.info[i].y == UINT16_MAX) {
      continue;
    }
    points[*point_count].id = touch_point.info[i].finger_id;
    points[*point_count].x = touch_point.info[i].x;
    points[*point_count].y = touch_point.info[i].y;
    points[*point_count].pressure = touch_point.info[i].pressure_value;
    ++(*point_count);
  }
  return *point_count > 0;
#endif
  return false;
}

bool TDisplayP4Device::PlayVibrationTest(uint8_t* waveform_count) {
  if (waveform_count != nullptr) {
    *waveform_count = 0;
  }

  if (!driver_.status().aw86224.init_flag && !driver_.InitAw86224()) {
    LogMessage(
        LogLevel::kWarning, __FILE__, __LINE__, "Aw86224 init retry failed\n");
    return false;
  }

  const auto& info = driver_.status().aw86224.ram_waveform_selection.info;
  if (info.waveform_count == 0) {
    LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
        "Aw86224 RAM waveform count is zero\n");
    return false;
  }

  LogMessage(LogLevel::kInfo, __FILE__, __LINE__,
      "Aw86224 CIT vibration test: library=%s count=%u gain=%u\n",
      info.name == nullptr ? "unknown" : info.name,
      static_cast<unsigned int>(info.waveform_count),
      static_cast<unsigned int>(kVibrationTestGain));

  for (uint8_t sequence = 1; sequence <= info.waveform_count; ++sequence) {
    if (!driver_.chip().aw86224->PlayRamWaveform(
            sequence, kVibrationTestLoopCount, kVibrationTestGain)) {
      LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
          "Aw86224 PlayRamWaveform failed, sequence=%u\n",
          static_cast<unsigned int>(sequence));
      driver_.chip().aw86224->StopRamPlaybackWaveform();
      return false;
    }

    vTaskDelay(pdMS_TO_TICKS(kVibrationTestPlayMs));

    if (!driver_.chip().aw86224->StopRamPlaybackWaveform()) {
      LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
          "Aw86224 StopRamPlaybackWaveform failed, sequence=%u\n",
          static_cast<unsigned int>(sequence));
      return false;
    }

    if (waveform_count != nullptr) {
      *waveform_count = sequence;
    }

    vTaskDelay(pdMS_TO_TICKS(kVibrationTestStopMs));
  }

  return true;
}

bool TDisplayP4Device::PlaySpeakerTest(size_t* bytes_written) {
  if (bytes_written != nullptr) {
    *bytes_written = 0;
  }

  if (!driver_.status().es8311.init_flag && !driver_.InitEs8311()) {
    LogMessage(
        LogLevel::kWarning, __FILE__, __LINE__, "Es8311 init retry failed\n");
    return false;
  }

  const auto* audio_data =
      reinterpret_cast<const uint8_t*>(c2_b16_s44100);
  const size_t audio_size = sizeof(c2_b16_s44100);
  speaker_test_total_bytes_.store(audio_size);
  const size_t frame_size =
      (kSpeakerTestBitsPerSample / 8) * kSpeakerTestChannelCount;
  const size_t duration_ms =
      ((audio_size / frame_size) * 1000U) / kSpeakerTestSampleRateHz;

  LogMessage(LogLevel::kInfo, __FILE__, __LINE__,
      "ES8311 CIT speaker test: bytes=%u, sample_rate=%u, channels=%u, "
      "duration=%u ms\n",
      static_cast<unsigned int>(audio_size),
      static_cast<unsigned int>(kSpeakerTestSampleRateHz),
      static_cast<unsigned int>(kSpeakerTestChannelCount),
      static_cast<unsigned int>(duration_ms));

  size_t total_written = 0;
  while (total_written < audio_size) {
    const size_t write_size =
        std::min(kSpeakerTestChunkBytes, audio_size - total_written);
    const size_t written = driver_.chip().es8311->WriteI2s(
        audio_data + total_written, write_size);
    if (written == 0) {
      LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
          "ES8311 WriteI2s failed, written=%u/%u\n",
          static_cast<unsigned int>(total_written),
          static_cast<unsigned int>(audio_size));
      return false;
    }
    total_written += written;
    if (bytes_written != nullptr) {
      *bytes_written = total_written;
    }
    speaker_test_bytes_written_.store(total_written);
  }

  return true;
}

bool TDisplayP4Device::StartSpeakerTest() {
  bool expected = false;
  if (!speaker_test_running_.compare_exchange_strong(expected, true)) {
    return false;
  }

  speaker_test_completed_.store(false);
  speaker_test_success_.store(false);
  speaker_test_bytes_written_.store(0);
  speaker_test_total_bytes_.store(sizeof(c2_b16_s44100));

  const BaseType_t result = xTaskCreate(SpeakerTestTaskEntry,
      "cit_speaker", kSpeakerTestTaskStackBytes, this,
      kSpeakerTestTaskPriority, nullptr);
  if (result != pdPASS) {
    speaker_test_running_.store(false);
    speaker_test_completed_.store(true);
    return false;
  }

  return true;
}

bool TDisplayP4Device::ReadSpeakerTestStatus(
    SpeakerTestPlaybackStatus* status) {
  if (status == nullptr) {
    return false;
  }

  status->running = speaker_test_running_.load();
  status->completed = speaker_test_completed_.load();
  status->success = speaker_test_success_.load();
  status->bytes_written = speaker_test_bytes_written_.load();
  status->total_bytes = speaker_test_total_bytes_.load();
  return true;
}

void TDisplayP4Device::SpeakerTestTaskEntry(void* context) {
  auto* self = static_cast<TDisplayP4Device*>(context);
  if (self != nullptr) {
    self->RunSpeakerTestTask();
  }
  vTaskDelete(nullptr);
}

void TDisplayP4Device::RunSpeakerTestTask() {
  size_t bytes_written = 0;
  const bool played = PlaySpeakerTest(&bytes_written);
  speaker_test_bytes_written_.store(bytes_written);
  speaker_test_success_.store(played);
  speaker_test_completed_.store(true);
  speaker_test_running_.store(false);
}

bool TDisplayP4Device::ReadDiagnostics(DeviceDiagnostics* diagnostics) {
  if (diagnostics == nullptr) {
    return false;
  }

  *diagnostics = DeviceDiagnostics();
  bool result = false;

  if (driver_.status().bq27220.init_flag && driver_.chip().bq27220 != nullptr) {
    cpp_bus_driver::Bq27220::BatteryStatus battery_status;
    const bool battery_status_ok =
        driver_.chip().bq27220->GetBatteryStatus(battery_status);
    const uint16_t voltage_mv = driver_.chip().bq27220->GetVoltage();
    const int16_t current_ma = driver_.chip().bq27220->GetCurrent();
    const uint16_t charge_percent = driver_.chip().bq27220->GetStatusOfCharge();

    if (voltage_mv > 0 && voltage_mv != UINT16_MAX) {
      diagnostics->power.ready = true;
      diagnostics->power.voltage_mv = voltage_mv;
      diagnostics->power.current_ma = current_ma;
      diagnostics->power.average_current_ma =
          driver_.chip().bq27220->GetAverageCurrent();
      diagnostics->power.average_power_mw =
          driver_.chip().bq27220->GetAveragePower();
      diagnostics->power.charge_percent =
          charge_percent == UINT16_MAX ? 0 : charge_percent;
      diagnostics->power.health_percent =
          driver_.chip().bq27220->GetStatusOfHealth();
      diagnostics->power.design_capacity_mah =
          driver_.chip().bq27220->GetDesignCapacity();
      diagnostics->power.remaining_capacity_mah =
          driver_.chip().bq27220->GetRemainingCapacity();
      diagnostics->power.full_charge_capacity_mah =
          driver_.chip().bq27220->GetFullChargeCapacity();
      diagnostics->power.time_to_empty_min =
          driver_.chip().bq27220->GetTimeToEmpty();
      diagnostics->power.time_to_full_min =
          driver_.chip().bq27220->GetTimeToFull();
      diagnostics->power.cycle_count = driver_.chip().bq27220->GetCycleCount();
      diagnostics->power.battery_temperature_c =
          driver_.chip().bq27220->GetTemperatureCelsius();
      diagnostics->power.gauge_temperature_c =
          driver_.chip().bq27220->GetChipTemperatureCelsius();
      diagnostics->power.battery_present =
          battery_status_ok && battery_status.flag.battery_present;
      diagnostics->power.discharging =
          battery_status_ok ? battery_status.flag.discharging : current_ma > 0;
      diagnostics->power.charging =
          battery_status_ok ? (!battery_status.flag.discharging &&
                                  current_ma < 0)
                            : current_ma < 0;
      diagnostics->power.full_charged =
          battery_status_ok && battery_status.flag.full_charged;
      diagnostics->power.full_discharged =
          battery_status_ok && battery_status.flag.full_discharged;
      result = true;
    }
  }

  if (driver_.status().icm20948.init_flag &&
      driver_.chip().icm20948 != nullptr) {
    xyzFloat acceleration;
    driver_.chip().icm20948->readSensor();
    driver_.chip().icm20948->getGValues(&acceleration);

    diagnostics->motion.ready = true;
    diagnostics->motion.acceleration_x_g = acceleration.x;
    diagnostics->motion.acceleration_y_g = acceleration.y;
    diagnostics->motion.acceleration_z_g = acceleration.z;
    result = true;
  }

  return result;
}

void TDisplayP4Device::StartBacklight() {
#if defined(CONFIG_SCREEN_TYPE_HI8561)
  if (driver_.status().hi8561_backlight.init_flag) {
    driver_.chip().hi8561_backlight->StartGradientTime(100, 500);
  }
#elif defined(CONFIG_SCREEN_TYPE_RM69A10)
  if (driver_.status().rm69a10.init_flag) {
    for (uint8_t brightness = 0; brightness < 180; brightness += 5) {
      driver_.chip().rm69a10->SetBrightness(brightness);
      vTaskDelay(pdMS_TO_TICKS(10));
    }
  }
#endif
}

bool TDisplayP4Device::WaitForScreenReady() {
  for (int elapsed_ms = 0; elapsed_ms < kScreenReadyTimeoutMs;
      elapsed_ms += kScreenReadyPollMs) {
    if (IsScreenReady()) {
      return true;
    }
    vTaskDelay(pdMS_TO_TICKS(kScreenReadyPollMs));
  }
  return IsScreenReady();
}

bool TDisplayP4Device::IsScreenReady() const {
  const auto screen_bus = driver_.bus().screen_mipi_bus;
  if (screen_bus == nullptr || screen_bus->device_handle() == nullptr) {
    return false;
  }

#if defined(CONFIG_SCREEN_TYPE_HI8561)
  return driver_.status().hi8561.init_flag && driver_.chip().hi8561 != nullptr;
#elif defined(CONFIG_SCREEN_TYPE_RM69A10)
  return driver_.status().rm69a10.init_flag &&
         driver_.chip().rm69a10 != nullptr;
#endif
  return false;
}

bool TDisplayP4Device::IsTouchReady() const {
#if defined(CONFIG_SCREEN_TYPE_HI8561)
  return driver_.status().hi8561_touch.init_flag &&
         driver_.chip().hi8561_touch != nullptr;
#elif defined(CONFIG_SCREEN_TYPE_RM69A10)
  return driver_.status().gt9895.init_flag && driver_.chip().gt9895 != nullptr;
#endif
  return false;
}

}  // namespace lilygo_box::hal
