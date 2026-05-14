/*
 * @Description: None
 * @Author: LILYGO_L
 * @Date: 2026-05-10 13:27:05
 * @LastEditTime: 2026-05-14 17:25:45
 * @License: GPL 3.0
 */
#include "hal/device/t_display_p4/t_display_p4_device.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <new>

#include "audio/new_notification_010_c2_b16_s44100.h"
#include "base/logger.h"
#include "esp_err.h"
#include "esp_eth.h"
#include "esp_eth_mac.h"
#include "esp_eth_phy_802_3.h"
#include "esp_event.h"
#include "esp_lcd_mipi_dsi.h"
#include "esp_netif.h"
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
constexpr uint32_t kMicrophoneTestTaskStackBytes = 4 * 1024;
constexpr UBaseType_t kMicrophoneTestTaskPriority = 3;
constexpr size_t kMicrophoneReadSampleCount = 128;
constexpr uint32_t kMicrophoneReadDelayMs = 40;
constexpr int kMicrophoneLevelFullScale = 1000;
constexpr int kMicrophoneLevelRiseDivisor = 4;
constexpr int kMicrophoneLevelFallDivisor = 8;
constexpr size_t kGpsMaxReadBufferBytes = 4096;
constexpr uint32_t kEthernetInitTaskStackBytes = 6 * 1024;
constexpr UBaseType_t kEthernetInitTaskPriority = 3;

/**
 * @brief 将 6 字节 MAC 地址打包为整数
 * @param mac_address MAC 地址数组
 * @return 打包后的 MAC 地址
 * @Date 2026-05-14 00:20:00
 */
uint64_t PackMacAddress(const uint8_t* mac_address) {
  if (mac_address == nullptr) {
    return 0;
  }

  uint64_t packed = 0;
  for (size_t i = 0; i < 6; ++i) {
    packed = (packed << 8) | mac_address[i];
  }
  return packed;
}

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

  if (!StartEthernet()) {
    LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
        "TDisplayP4Device::StartEthernet failed\n");
  }

  if (!WaitForScreenReady()) {
    LogMessage(LogLevel::kError, __FILE__, __LINE__,
        "TDisplayP4Device::WaitForScreenReady failed\n");
    return false;
  }
  return true;
}

bool TDisplayP4Device::StartEthernet() {
  if (ethernet_initialized_.load()) {
    if (!ethernet_running_.load() && ethernet_handle_ != nullptr) {
      const esp_err_t result =
          esp_eth_start(reinterpret_cast<esp_eth_handle_t>(ethernet_handle_));
      if (result != ESP_OK && result != ESP_ERR_INVALID_STATE) {
        SetEthernetFailure(result);
        return false;
      }
      ethernet_running_.store(true);
      ethernet_start_failed_.store(false);
      ethernet_last_error_.store(ESP_OK);
    }
    return true;
  }

  bool expected = false;
  if (!ethernet_initializing_.compare_exchange_strong(expected, true)) {
    return true;
  }

  ethernet_start_failed_.store(false);
  ethernet_last_error_.store(ESP_OK);
  const BaseType_t result = xTaskCreate(EthernetInitTaskEntry, "ethernet",
      kEthernetInitTaskStackBytes, this, kEthernetInitTaskPriority, nullptr);
  if (result != pdPASS) {
    SetEthernetFailure(ESP_ERR_NO_MEM);
    return false;
  }
  return true;
}

bool TDisplayP4Device::ReadEthernetStatus(EthernetStatus* status) {
  if (status == nullptr) {
    return false;
  }

  status->initializing = ethernet_initializing_.load();
  status->initialized = ethernet_initialized_.load();
  status->running = ethernet_running_.load();
  status->link_up = ethernet_link_up_.load();
  status->got_ip = ethernet_got_ip_.load();
  status->start_failed = ethernet_start_failed_.load();
  status->port_count = ethernet_port_count_.load();
  status->last_error = ethernet_last_error_.load();
  status->mac_address = ethernet_mac_address_.load();
  status->ip_address = ethernet_ip_address_.load();
  status->netmask = ethernet_netmask_.load();
  status->gateway = ethernet_gateway_.load();
  return true;
}

bool TDisplayP4Device::RegisterFlushReadyCallback(
    ScreenProviderFlushReadyCallback callback, void* callback_context) {
  if (!IsScreenReady()) {
    LogMessage(LogLevel::kError, __FILE__, __LINE__,
        "Screen is not ready for flush callback registration\n");
    return false;
  }

  flush_ready_handler_.callback = callback;
  flush_ready_handler_.context = callback_context;

  esp_lcd_dpi_panel_event_callbacks_t panel_callbacks = {
      .on_color_trans_done = [](esp_lcd_panel_handle_t,
                                 esp_lcd_dpi_panel_event_data_t*,
                                 void* user_context) -> bool {
        auto* handler =
            static_cast<ScreenProviderFlushReadyHandler*>(user_context);
        if (handler != nullptr && handler->callback != nullptr) {
          handler->callback(handler->context);
        }
        return false;
      },
      .on_refresh_done = [](esp_lcd_panel_handle_t,
                             esp_lcd_dpi_panel_event_data_t*,
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

bool TDisplayP4Device::PlayHapticWaveform(uint8_t* waveform_count) {
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

bool TDisplayP4Device::PlaySpeakerTone(size_t* bytes_written) {
  if (bytes_written != nullptr) {
    *bytes_written = 0;
  }

  if (!driver_.status().es8311.init_flag && !driver_.InitEs8311()) {
    LogMessage(
        LogLevel::kWarning, __FILE__, __LINE__, "Es8311 init retry failed\n");
    return false;
  }

  const auto* audio_data = reinterpret_cast<const uint8_t*>(c2_b16_s44100);
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
    const size_t written =
        driver_.chip().es8311->WriteI2s(audio_data + total_written, write_size);
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

bool TDisplayP4Device::StartSpeakerTone() {
  bool expected = false;
  if (!speaker_test_running_.compare_exchange_strong(expected, true)) {
    return false;
  }

  speaker_test_completed_.store(false);
  speaker_test_success_.store(false);
  speaker_test_bytes_written_.store(0);
  speaker_test_total_bytes_.store(sizeof(c2_b16_s44100));

  const BaseType_t result = xTaskCreate(SpeakerToneTaskEntry, "cit_speaker",
      kSpeakerTestTaskStackBytes, this, kSpeakerTestTaskPriority, nullptr);
  if (result != pdPASS) {
    speaker_test_running_.store(false);
    speaker_test_completed_.store(true);
    return false;
  }

  return true;
}

bool TDisplayP4Device::ReadSpeakerToneStatus(SpeakerPlaybackStatus* status) {
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

void TDisplayP4Device::SpeakerToneTaskEntry(void* context) {
  auto* self = static_cast<TDisplayP4Device*>(context);
  if (self != nullptr) {
    self->RunSpeakerToneTask();
  }
  vTaskDelete(nullptr);
}

void TDisplayP4Device::RunSpeakerToneTask() {
  size_t bytes_written = 0;
  const bool played = PlaySpeakerTone(&bytes_written);
  speaker_test_bytes_written_.store(bytes_written);
  speaker_test_success_.store(played);
  speaker_test_completed_.store(true);
  speaker_test_running_.store(false);
}

bool TDisplayP4Device::StartMicrophone() {
  if (!driver_.status().es8311.init_flag && !driver_.InitEs8311()) {
    LogMessage(
        LogLevel::kWarning, __FILE__, __LINE__, "Es8311 init retry failed\n");
    return false;
  }

  bool expected = false;
  if (!microphone_test_running_.compare_exchange_strong(expected, true)) {
    return !microphone_test_stop_requested_.load();
  }

  microphone_test_stop_requested_.store(false);
  microphone_level_percent_.store(0);
  microphone_peak_sample_.store(0);
  microphone_bytes_read_.store(0);
  if (!SetAdcToDac(false)) {
    microphone_test_running_.store(false);
    return false;
  }

  const BaseType_t result = xTaskCreate(MicrophoneTestTaskEntry,
      "cit_microphone", kMicrophoneTestTaskStackBytes, this,
      kMicrophoneTestTaskPriority, nullptr);
  if (result != pdPASS) {
    microphone_test_running_.store(false);
    microphone_test_stop_requested_.store(true);
    return false;
  }

  return true;
}

bool TDisplayP4Device::StopMicrophone() {
  microphone_test_stop_requested_.store(true);
  microphone_level_percent_.store(0);
  microphone_peak_sample_.store(0);
  if (!driver_.status().es8311.init_flag) {
    microphone_adc_to_dac_enabled_.store(false);
    return true;
  }
  return SetAdcToDac(false);
}

bool TDisplayP4Device::SetAdcToDac(bool enable) {
  if (!driver_.status().es8311.init_flag) {
    return false;
  }

  if (!driver_.chip().es8311->SetAdcDataToDac(enable)) {
    LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
        "Es8311 SetAdcDataToDac failed\n");
    return false;
  }

  microphone_adc_to_dac_enabled_.store(enable);
  return true;
}

bool TDisplayP4Device::ReadMicrophoneStatus(MicrophoneStatus* status) {
  if (status == nullptr) {
    return false;
  }

  status->running = microphone_test_running_.load();
  status->adc_to_dac_enabled = microphone_adc_to_dac_enabled_.load();
  status->level_percent = microphone_level_percent_.load();
  status->peak_sample = microphone_peak_sample_.load();
  status->bytes_read = microphone_bytes_read_.load();
  return true;
}

bool TDisplayP4Device::StartGps() {
  if (!IsGpsReady() && !driver_.InitL76k()) {
    LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
        "TDisplayP4Driver::InitL76k failed\n");
    return false;
  }
  if (!IsGpsReady()) {
    LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
        "L76k is not ready for GPS test\n");
    return false;
  }

  gps_status_ = GpsStatus();
  gps_running_ = true;
  gps_status_.running = true;

  bool result = driver_.chip().l76k->ClearRxBufferData();
  result &= driver_.chip().l76k->Sleep(false);
  if (!result) {
    gps_running_ = false;
    gps_status_.running = false;
    LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
        "TDisplayP4Device::StartGps failed\n");
    return false;
  }
  return true;
}

bool TDisplayP4Device::StopGps() {
  gps_running_ = false;
  gps_status_.running = false;
  if (!IsGpsReady()) {
    return true;
  }

  const bool result = driver_.chip().l76k->Sleep(true);
  if (!result) {
    LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
        "TDisplayP4Device::StopGps failed\n");
  }
  return result;
}

bool TDisplayP4Device::ReadGpsStatus(GpsStatus* status) {
  if (status == nullptr) {
    return false;
  }

  gps_status_.running = gps_running_;
  *status = gps_status_;
  if (!gps_running_) {
    return true;
  }
  if (!IsGpsReady()) {
    return false;
  }

  const size_t rx_buffer_length = driver_.chip().l76k->GetRxBufferLength();
  if (rx_buffer_length == 0) {
    return true;
  }

  const size_t buffer_length =
      std::min(rx_buffer_length, kGpsMaxReadBufferBytes);
  std::unique_ptr<uint8_t[]> buffer(
      new (std::nothrow) uint8_t[buffer_length + 1]);
  if (buffer == nullptr) {
    return false;
  }

  const uint32_t read_length = driver_.chip().l76k->ReadData(
      buffer.get(), static_cast<uint32_t>(buffer_length));
  if (read_length == 0) {
    return true;
  }

  const size_t data_length =
      std::min(static_cast<size_t>(read_length), buffer_length);
  buffer[data_length] = '\0';

  GpsStatus next_status = gps_status_;
  next_status.running = true;
  next_status.data_ready = true;
  next_status.bytes_read = data_length;

  cpp_bus_driver::Gnss::Rmc rmc;
  next_status.parse_success =
      driver_.chip().l76k->ParseRmcInfo(buffer.get(), data_length, rmc);
  if (next_status.parse_success) {
    std::snprintf(next_status.location_status,
        sizeof(next_status.location_status), "%s", rmc.location_status.c_str());

    if (rmc.utc.update_flag) {
      next_status.utc.ready = true;
      next_status.utc.hour = rmc.utc.hour;
      next_status.utc.minute = rmc.utc.minute;
      next_status.utc.second = rmc.utc.second;
    }

    if (rmc.data.update_flag) {
      next_status.date.ready = true;
      next_status.date.day = rmc.data.day;
      next_status.date.month = rmc.data.month;
      next_status.date.year = rmc.data.year;
    }

    if (rmc.location.lat.update_flag &&
        rmc.location.lat.direction_update_flag) {
      next_status.latitude.ready = true;
      next_status.latitude.degrees = rmc.location.lat.degrees;
      next_status.latitude.minutes = rmc.location.lat.minutes;
      next_status.latitude.degrees_minutes = rmc.location.lat.degrees_minutes;
      std::snprintf(next_status.latitude.direction,
          sizeof(next_status.latitude.direction), "%s",
          rmc.location.lat.direction.c_str());
    }

    if (rmc.location.lon.update_flag &&
        rmc.location.lon.direction_update_flag) {
      next_status.longitude.ready = true;
      next_status.longitude.degrees = rmc.location.lon.degrees;
      next_status.longitude.minutes = rmc.location.lon.minutes;
      next_status.longitude.degrees_minutes = rmc.location.lon.degrees_minutes;
      std::snprintf(next_status.longitude.direction,
          sizeof(next_status.longitude.direction), "%s",
          rmc.location.lon.direction.c_str());
    }

    next_status.positioned =
        next_status.positioned ||
        (next_status.latitude.ready && next_status.longitude.ready);
  }

  gps_status_ = next_status;
  *status = gps_status_;
  return true;
}

void TDisplayP4Device::MicrophoneTestTaskEntry(void* context) {
  auto* self = static_cast<TDisplayP4Device*>(context);
  if (self != nullptr) {
    self->RunMicrophoneTestTask();
  }
  vTaskDelete(nullptr);
}

void TDisplayP4Device::RunMicrophoneTestTask() {
  std::array<int16_t, kMicrophoneReadSampleCount> samples = {};
  while (!microphone_test_stop_requested_.load()) {
    const size_t read_bytes = driver_.chip().es8311->ReadI2s(
        samples.data(), samples.size() * sizeof(samples[0]));
    if (read_bytes > 0) {
      microphone_bytes_read_.fetch_add(read_bytes);

      int peak_sample = 0;
      int64_t absolute_sum = 0;
      const size_t sample_count = read_bytes / sizeof(samples[0]);
      for (size_t i = 0; i < sample_count && i < samples.size(); ++i) {
        const int sample = samples[i];
        const int absolute_sample = sample < 0 ? -sample : sample;
        absolute_sum += absolute_sample;
        peak_sample = std::max(peak_sample, absolute_sample);
      }

      const int average_sample =
          sample_count == 0 ? 0 : absolute_sum / static_cast<int>(sample_count);
      const int target_level_percent =
          std::min(100, (average_sample * 100) / kMicrophoneLevelFullScale);
      const int current_level_percent = microphone_level_percent_.load();
      const int difference = target_level_percent - current_level_percent;
      const int divisor = difference > 0 ? kMicrophoneLevelRiseDivisor
                                         : kMicrophoneLevelFallDivisor;
      int level_percent = current_level_percent + difference / divisor;
      if (level_percent == current_level_percent && difference != 0) {
        level_percent += difference > 0 ? 1 : -1;
      }
      microphone_peak_sample_.store(peak_sample);
      microphone_level_percent_.store(level_percent);
    }

    vTaskDelay(pdMS_TO_TICKS(kMicrophoneReadDelayMs));
  }

  if (microphone_adc_to_dac_enabled_.load()) {
    driver_.chip().es8311->SetAdcDataToDac(false);
    microphone_adc_to_dac_enabled_.store(false);
  }
  microphone_level_percent_.store(0);
  microphone_peak_sample_.store(0);
  microphone_test_running_.store(false);
}

void TDisplayP4Device::EthernetInitTaskEntry(void* context) {
  auto* self = static_cast<TDisplayP4Device*>(context);
  if (self != nullptr) {
    self->RunEthernetInitTask();
  }
  vTaskDelete(nullptr);
}

void TDisplayP4Device::RunEthernetInitTask() {
  const int result = InitializeEthernetStack();
  if (result != ESP_OK) {
    SetEthernetFailure(result);
    LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
        "Ethernet init failed (error code: %#X)\n", result);
  }
  ethernet_initializing_.store(false);
}

int TDisplayP4Device::InitializeEthernetStack() {
  if (ethernet_handle_ != nullptr) {
    const esp_err_t start_result =
        esp_eth_start(reinterpret_cast<esp_eth_handle_t>(ethernet_handle_));
    if (start_result != ESP_OK && start_result != ESP_ERR_INVALID_STATE) {
      return start_result;
    }
    ethernet_initialized_.store(true);
    ethernet_running_.store(true);
    ethernet_start_failed_.store(false);
    ethernet_last_error_.store(ESP_OK);
    return ESP_OK;
  }

  esp_err_t result = esp_netif_init();
  if (result != ESP_OK && result != ESP_ERR_INVALID_STATE) {
    return result;
  }

  result = esp_event_loop_create_default();
  if (result != ESP_OK && result != ESP_ERR_INVALID_STATE) {
    return result;
  }

  eth_mac_config_t mac_config = ETH_MAC_DEFAULT_CONFIG();
  eth_phy_config_t phy_config = ETH_PHY_DEFAULT_CONFIG();
  phy_config.phy_addr = ETHERNET_PHY_ADDRESS;
  phy_config.reset_gpio_num = ETHERNET_PHY_RST;

  eth_esp32_emac_config_t emac_config = {};
  emac_config.smi_gpio.mdc_num = ETHERNET_MDC;
  emac_config.smi_gpio.mdio_num = ETHERNET_MDIO;
  emac_config.interface = EMAC_DATA_INTERFACE_RMII;
  emac_config.clock_config.rmii.clock_mode = EMAC_CLK_EXT_IN;
  emac_config.clock_config.rmii.clock_gpio =
      static_cast<emac_rmii_clock_gpio_t>(ETHERNET_RMII_REF_CLK);
  emac_config.dma_burst_len = ETH_DMA_BURST_LEN_32;
  emac_config.intr_priority = 0;
#if SOC_EMAC_USE_MULTI_IO_MUX || SOC_EMAC_MII_USE_GPIO_MATRIX
  emac_config.emac_dataif_gpio.rmii.tx_en_num = ETHERNET_RMII_TX_EN;
  emac_config.emac_dataif_gpio.rmii.txd0_num = ETHERNET_RMII_TXD0;
  emac_config.emac_dataif_gpio.rmii.txd1_num = ETHERNET_RMII_TXD1;
  emac_config.emac_dataif_gpio.rmii.crs_dv_num = ETHERNET_RMII_CRS_DV;
  emac_config.emac_dataif_gpio.rmii.rxd0_num = ETHERNET_RMII_RXD0;
  emac_config.emac_dataif_gpio.rmii.rxd1_num = ETHERNET_RMII_RXD1;
#endif
#if !SOC_EMAC_RMII_CLK_OUT_INTERNAL_LOOPBACK
  emac_config.clock_config_out_in.rmii.clock_mode = EMAC_CLK_EXT_IN;
  emac_config.clock_config_out_in.rmii.clock_gpio =
      static_cast<emac_rmii_clock_gpio_t>(ETHERNET_RMII_CLK_OUT);
#endif
  emac_config.mdc_freq_hz = 0;

  esp_eth_mac_t* mac = esp_eth_mac_new_esp32(&emac_config, &mac_config);
  if (mac == nullptr) {
    return ESP_ERR_NO_MEM;
  }

  esp_eth_phy_t* phy = esp_eth_phy_new_ip101(&phy_config);
  if (phy == nullptr) {
    mac->del(mac);
    return ESP_ERR_NO_MEM;
  }

  esp_eth_handle_t handle = nullptr;
  esp_eth_config_t config = ETH_DEFAULT_CONFIG(mac, phy);
  result = esp_eth_driver_install(&config, &handle);
  if (result != ESP_OK) {
    mac->del(mac);
    phy->del(phy);
    return result;
  }

  esp_netif_inherent_config_t inherent_config = *ESP_NETIF_BASE_DEFAULT_ETH;
  esp_netif_config_t netif_config = {
      .base = &inherent_config,
      .driver = nullptr,
      .stack = ESP_NETIF_NETSTACK_DEFAULT_ETH,
  };
  esp_netif_t* netif = esp_netif_new(&netif_config);
  if (netif == nullptr) {
    return ESP_ERR_NO_MEM;
  }

  auto glue = esp_eth_new_netif_glue(handle);
  if (glue == nullptr) {
    return ESP_ERR_NO_MEM;
  }

  result = esp_netif_attach(netif, glue);
  if (result != ESP_OK) {
    return result;
  }

  result = esp_event_handler_register(
      ETH_EVENT, ESP_EVENT_ANY_ID, EthernetEventHandler, this);
  if (result != ESP_OK) {
    return result;
  }

  result = esp_event_handler_register(
      IP_EVENT, IP_EVENT_ETH_GOT_IP, EthernetGotIpEventHandler, this);
  if (result != ESP_OK) {
    return result;
  }

  ethernet_handle_ = handle;
  ethernet_port_count_.store(1);

  result = esp_eth_start(handle);
  if (result != ESP_OK && result != ESP_ERR_INVALID_STATE) {
    return result;
  }

  ethernet_initialized_.store(true);
  ethernet_running_.store(true);
  ethernet_start_failed_.store(false);
  ethernet_last_error_.store(ESP_OK);
  return ESP_OK;
}

void TDisplayP4Device::SetEthernetFailure(int error) {
  ethernet_initializing_.store(false);
  ethernet_initialized_.store(ethernet_handle_ != nullptr);
  ethernet_running_.store(false);
  ethernet_link_up_.store(false);
  ethernet_got_ip_.store(false);
  ethernet_start_failed_.store(true);
  ethernet_last_error_.store(error);
  ethernet_ip_address_.store(0);
  ethernet_netmask_.store(0);
  ethernet_gateway_.store(0);
}

void TDisplayP4Device::EthernetEventHandler(
    void* arg, const char* event_base, int32_t event_id, void* event_data) {
  (void)event_base;
  auto* self = static_cast<TDisplayP4Device*>(arg);
  if (self == nullptr) {
    return;
  }

  switch (event_id) {
    case ETHERNET_EVENT_CONNECTED: {
      self->ethernet_running_.store(true);
      self->ethernet_link_up_.store(true);
      self->ethernet_got_ip_.store(false);
      self->ethernet_ip_address_.store(0);
      self->ethernet_netmask_.store(0);
      self->ethernet_gateway_.store(0);

      if (event_data != nullptr) {
        esp_eth_handle_t handle = *static_cast<esp_eth_handle_t*>(event_data);
        uint8_t mac_address[6] = {};
        if (esp_eth_ioctl(handle, ETH_CMD_G_MAC_ADDR, mac_address) == ESP_OK) {
          self->ethernet_mac_address_.store(PackMacAddress(mac_address));
        }
      }
      break;
    }
    case ETHERNET_EVENT_DISCONNECTED:
      self->ethernet_link_up_.store(false);
      self->ethernet_got_ip_.store(false);
      self->ethernet_ip_address_.store(0);
      self->ethernet_netmask_.store(0);
      self->ethernet_gateway_.store(0);
      break;
    case ETHERNET_EVENT_START:
      self->ethernet_running_.store(true);
      self->ethernet_start_failed_.store(false);
      self->ethernet_last_error_.store(ESP_OK);
      break;
    case ETHERNET_EVENT_STOP:
      self->ethernet_running_.store(false);
      self->ethernet_link_up_.store(false);
      self->ethernet_got_ip_.store(false);
      self->ethernet_ip_address_.store(0);
      self->ethernet_netmask_.store(0);
      self->ethernet_gateway_.store(0);
      break;
    default:
      break;
  }
}

void TDisplayP4Device::EthernetGotIpEventHandler(
    void* arg, const char* event_base, int32_t event_id, void* event_data) {
  (void)event_base;
  (void)event_id;
  auto* self = static_cast<TDisplayP4Device*>(arg);
  auto* event = static_cast<ip_event_got_ip_t*>(event_data);
  if (self == nullptr || event == nullptr) {
    return;
  }

  self->ethernet_link_up_.store(true);
  self->ethernet_got_ip_.store(true);
  self->ethernet_ip_address_.store(event->ip_info.ip.addr);
  self->ethernet_netmask_.store(event->ip_info.netmask.addr);
  self->ethernet_gateway_.store(event->ip_info.gw.addr);
}

bool TDisplayP4Device::ReadDiagnostics(DeviceDiagnostics* diagnostics) {
  if (diagnostics == nullptr) {
    return false;
  }

  *diagnostics = DeviceDiagnostics();
  const bool bmu_result = ReadBmuStatus(&diagnostics->bmu);
  const bool imu_result = ReadImuStatus(&diagnostics->imu);
  return bmu_result || imu_result;
}

bool TDisplayP4Device::ReadBmuStatus(BmuStatus* status) {
  if (status == nullptr) {
    return false;
  }

  *status = BmuStatus();

  if (driver_.status().bq27220.init_flag && driver_.chip().bq27220 != nullptr) {
    cpp_bus_driver::Bq27220::BatteryStatus bmu_status_flags;
    const bool bmu_status_ok =
        driver_.chip().bq27220->GetBatteryStatus(bmu_status_flags);
    const uint16_t voltage_mv = driver_.chip().bq27220->GetVoltage();
    const int16_t current_ma = driver_.chip().bq27220->GetCurrent();
    const uint16_t charge_percent = driver_.chip().bq27220->GetStatusOfCharge();

    if (voltage_mv > 0 && voltage_mv != UINT16_MAX) {
      status->ready = true;
      status->voltage_mv = voltage_mv;
      status->current_ma = current_ma;
      status->average_current_ma = driver_.chip().bq27220->GetAverageCurrent();
      status->average_bmu_mw = driver_.chip().bq27220->GetAveragePower();
      status->charge_percent =
          charge_percent == UINT16_MAX ? 0 : charge_percent;
      status->health_percent = driver_.chip().bq27220->GetStatusOfHealth();
      status->design_capacity_mah = driver_.chip().bq27220->GetDesignCapacity();
      status->remaining_capacity_mah =
          driver_.chip().bq27220->GetRemainingCapacity();
      status->full_charge_capacity_mah =
          driver_.chip().bq27220->GetFullChargeCapacity();
      status->time_to_empty_min = driver_.chip().bq27220->GetTimeToEmpty();
      status->time_to_full_min = driver_.chip().bq27220->GetTimeToFull();
      status->cycle_count = driver_.chip().bq27220->GetCycleCount();
      status->pack_temperature_c =
          driver_.chip().bq27220->GetTemperatureCelsius();
      status->gauge_temperature_c =
          driver_.chip().bq27220->GetChipTemperatureCelsius();
      status->pack_present =
          bmu_status_ok && bmu_status_flags.flag.battery_present;
      status->discharging =
          bmu_status_ok ? bmu_status_flags.flag.discharging : current_ma > 0;
      status->charging =
          bmu_status_ok ? (!bmu_status_flags.flag.discharging && current_ma < 0)
                        : current_ma < 0;
      status->full_charged =
          bmu_status_ok && bmu_status_flags.flag.full_charged;
      status->full_discharged =
          bmu_status_ok && bmu_status_flags.flag.full_discharged;
      return true;
    }
  }

  return false;
}

bool TDisplayP4Device::ReadImuStatus(ImuStatus* status) {
  if (status == nullptr) {
    return false;
  }

  *status = ImuStatus();

  if (driver_.status().icm20948.init_flag &&
      driver_.chip().icm20948 != nullptr) {
    xyzFloat acceleration;
    driver_.chip().icm20948->readSensor();
    driver_.chip().icm20948->getGValues(&acceleration);

    status->ready = true;
    status->acceleration_x_g = acceleration.x;
    status->acceleration_y_g = acceleration.y;
    status->acceleration_z_g = acceleration.z;
    return true;
  }

  return false;
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

bool TDisplayP4Device::IsGpsReady() const {
  return driver_.status().l76k.init_flag && driver_.chip().l76k != nullptr;
}

}  // namespace lilygo_box::hal
