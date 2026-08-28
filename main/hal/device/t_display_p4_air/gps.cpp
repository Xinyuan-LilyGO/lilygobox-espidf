/*
 * @Description: T-Display-P4-Air GNSS 硬件实现
 * @Author: LILYGO_L
 * @Date: 2026-08-28 00:00:00
 * @LastEditTime: 2026-08-28 00:00:00
 * @License: GPL 3.0
 */
#include "hal/device/t_display_p4_air/device.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <new>
#include <string>

#include "base/logger.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "hal/device/common/device_utils.h"

namespace lilygo_box::hal {
namespace {

constexpr size_t kGpsMaxReadBufferBytes = 4096;
constexpr uint32_t kNrf9151CommandTimeoutMs = 5000;
constexpr uint32_t kNrf9151StartupDelayMs = 1000;
constexpr size_t kNrf9151PendingDataLimit = 8192;
constexpr uint32_t kNrf9151GnssUpdateIntervalMs = 1000;

}  // namespace

bool TDisplayP4AirDevice::SetGpsEnabled(bool enabled) {
  if (enabled && gps_running_) {
    return true;
  }
  if (enabled && cellular_.task_active.load() && !SetCellularEnabled(false)) {
    return false;
  }
  if (nrf9151_mutex_ == nullptr ||
      xSemaphoreTake(nrf9151_mutex_, portMAX_DELAY) != pdTRUE) {
    return false;
  }

  bool result = true;
  if (!enabled) {
    if (gps_running_ && driver_.IsNrf9151Ready() &&
        driver_.chip().nrf9151 != nullptr) {
      std::string response;
      const auto stop_result = driver_.chip().nrf9151->SendCommand(
          "AT#XGNSS=0", &response, kNrf9151CommandTimeoutMs);
      response.clear();
      const auto nmea_result = driver_.chip().nrf9151->SendCommand(
          "AT#XGNSSNMEA=0", &response, kNrf9151CommandTimeoutMs);
      result &= stop_result == cpp_bus_driver::Nrf9151::CommandResult::kOk;
      result &= nmea_result == cpp_bus_driver::Nrf9151::CommandResult::kOk;
    }
    gps_running_ = false;
    gps_status_.running = false;
    gps_pending_data_.clear();
    if (!cellular_.task_active.load()) {
      result &= driver_.DeinitNrf9151();
    }
    xSemaphoreGive(nrf9151_mutex_);
    if (!result) {
      LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
          "Disable nRF9151 GNSS failed\n");
    }
    return result;
  }

  if (!driver_.InitNrf9151() || !driver_.IsNrf9151Ready() ||
      driver_.chip().nrf9151 == nullptr ||
      driver_.bus().nrf9151_uart_bus == nullptr) {
    driver_.DeinitNrf9151();
    xSemaphoreGive(nrf9151_mutex_);
    LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
        "Enable nRF9151 GNSS failed: modem unavailable\n");
    return false;
  }

  vTaskDelay(pdMS_TO_TICKS(kNrf9151StartupDelayMs));
  constexpr std::array<const char*, 5> kStartupCommands = {{
      "AT+CFUN=0",
      "AT%XSYSTEMMODE=0,0,1,0",
      "AT+CFUN=31",
      "AT#XGNSSNMEA=1",
      "AT#XGNSS=1,0,1",
  }};
  size_t completed_command_count = 0;
  for (const char* command : kStartupCommands) {
    std::string response;
    const auto command_result = driver_.chip().nrf9151->SendCommand(
        command, &response, kNrf9151CommandTimeoutMs);
    if (command_result != cpp_bus_driver::Nrf9151::CommandResult::kOk) {
      LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
          "Enable nRF9151 GNSS failed at %s: %s\n", command,
          cpp_bus_driver::Nrf9151::CommandResultToString(command_result));
      break;
    }
    ++completed_command_count;
  }

  if (completed_command_count != kStartupCommands.size()) {
    std::string response;
    if (completed_command_count >= 4) {
      driver_.chip().nrf9151->SendCommand(
          "AT#XGNSS=0", &response, kNrf9151CommandTimeoutMs);
    }
    if (completed_command_count >= 3) {
      response.clear();
      driver_.chip().nrf9151->SendCommand(
          "AT#XGNSSNMEA=0", &response, kNrf9151CommandTimeoutMs);
    }
    gps_running_ = false;
    gps_status_.running = false;
    driver_.DeinitNrf9151();
    xSemaphoreGive(nrf9151_mutex_);
    return false;
  }

  gps_status_ = GpsStatus();
  gps_status_.running = true;
  gps_status_.update_interval_ms = kNrf9151GnssUpdateIntervalMs;
  gps_pending_data_.clear();
  gps_running_ = true;
  result = driver_.bus().nrf9151_uart_bus->ClearRxBufferData();
  if (!result) {
    gps_running_ = false;
    gps_status_.running = false;
    driver_.DeinitNrf9151();
  }
  xSemaphoreGive(nrf9151_mutex_);
  return result;
}

bool TDisplayP4AirDevice::ReadGpsStatus(GpsStatus* status) {
  if (status == nullptr) {
    return false;
  }

  gps_status_.running = gps_running_;
  gps_status_.update_interval_ms = kNrf9151GnssUpdateIntervalMs;
  *status = gps_status_;
  if (!gps_running_) {
    return true;
  }
  if (!driver_.IsNrf9151Ready() || driver_.bus().nrf9151_uart_bus == nullptr ||
      nrf9151_mutex_ == nullptr ||
      xSemaphoreTake(nrf9151_mutex_, pdMS_TO_TICKS(20)) != pdTRUE) {
    return false;
  }

  auto& uart = *driver_.bus().nrf9151_uart_bus;
  const size_t rx_buffer_length = uart.GetRxBufferLength();
  if (rx_buffer_length == 0) {
    xSemaphoreGive(nrf9151_mutex_);
    return true;
  }

  const size_t buffer_length =
      std::min(rx_buffer_length, kGpsMaxReadBufferBytes);
  std::unique_ptr<uint8_t[]> buffer(
      new (std::nothrow) uint8_t[buffer_length + 1]);
  if (buffer == nullptr) {
    xSemaphoreGive(nrf9151_mutex_);
    return false;
  }

  const int32_t read_length =
      uart.Read(buffer.get(), static_cast<uint32_t>(buffer_length));
  if (read_length == 0) {
    xSemaphoreGive(nrf9151_mutex_);
    return true;
  }
  if (read_length < 0) {
    xSemaphoreGive(nrf9151_mutex_);
    return false;
  }

  const size_t data_length =
      std::min(static_cast<size_t>(read_length), buffer_length);
  buffer[data_length] = '\0';

  GpsStatus next_status = gps_status_;
  next_status.running = true;
  next_status.data_ready = true;
  next_status.bytes_read = data_length;
  next_status.update_interval_ms = kNrf9151GnssUpdateIntervalMs;

  cpp_bus_driver::GnssParser::Info info;
  gps_pending_data_.append(
      reinterpret_cast<const char*>(buffer.get()), data_length);
  const size_t complete_data_end = gps_pending_data_.rfind('\n');
  bool parse_success = false;
  if (complete_data_end != std::string::npos) {
    const size_t complete_data_length = complete_data_end + 1;
    parse_success = gps_parser_.ParseInfo(
        reinterpret_cast<const uint8_t*>(gps_pending_data_.data()),
        complete_data_length, info);
    gps_pending_data_.erase(0, complete_data_length);
  }
  if (gps_pending_data_.size() > kNrf9151PendingDataLimit) {
    LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
        "Discard oversized nRF9151 GNSS partial frame: %u bytes\n",
        static_cast<unsigned>(gps_pending_data_.size()));
    gps_pending_data_.clear();
  }
  xSemaphoreGive(nrf9151_mutex_);
  next_status.parse_success = next_status.parse_success || parse_success;
  if (parse_success) {
    const auto& rmc = info.rmc;
    const auto& gga = info.gga;
    const auto& gsv = info.gsv;
    const auto& gsa = info.gsa;
    const auto& vtg = info.vtg;
    const auto& zda = info.zda;

    if (rmc.location_status_update_flag) {
      device_utils::CopyString(next_status.location_status,
          sizeof(next_status.location_status), rmc.location_status);
    }
    if (!rmc.mode_indicator.empty()) {
      device_utils::CopyString(next_status.mode_indicator, sizeof(next_status.mode_indicator),
          rmc.mode_indicator);
    } else if (!vtg.mode_indicator.empty()) {
      device_utils::CopyString(next_status.mode_indicator, sizeof(next_status.mode_indicator),
          vtg.mode_indicator);
    }
    if (!rmc.navigational_status.empty()) {
      device_utils::CopyString(next_status.navigational_status,
          sizeof(next_status.navigational_status), rmc.navigational_status);
    }

    if (rmc.utc.update_flag) {
      next_status.utc.ready = true;
      next_status.utc.hour = rmc.utc.hour;
      next_status.utc.minute = rmc.utc.minute;
      next_status.utc.second = rmc.utc.second;
    } else if (zda.utc.update_flag) {
      next_status.utc.ready = true;
      next_status.utc.hour = zda.utc.hour;
      next_status.utc.minute = zda.utc.minute;
      next_status.utc.second = zda.utc.second;
    }

    if (rmc.data.update_flag) {
      next_status.date.ready = true;
      next_status.date.day = rmc.data.day;
      next_status.date.month = rmc.data.month;
      next_status.date.year = 2000 + rmc.data.year;
    } else if (zda.date.update_flag) {
      next_status.date.ready = true;
      next_status.date.day = zda.date.day;
      next_status.date.month = zda.date.month;
      next_status.date.year = zda.date.year;
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

    if (device_utils::IsGnssFloatReady(rmc.speed_over_ground_knots)) {
      next_status.speed_ready = true;
      next_status.speed_knots = rmc.speed_over_ground_knots;
      next_status.speed_kmh = rmc.speed_over_ground_knots * 1.852F;
    } else if (vtg.update_flag && device_utils::IsGnssFloatReady(vtg.speed_kmh)) {
      next_status.speed_ready = true;
      next_status.speed_knots = vtg.speed_knots;
      next_status.speed_kmh = vtg.speed_kmh;
    }
    if (device_utils::IsGnssFloatReady(rmc.course_over_ground_degree)) {
      next_status.course_ready = true;
      next_status.course_degree = rmc.course_over_ground_degree;
    } else if (vtg.update_flag && device_utils::IsGnssFloatReady(vtg.course_true_degree)) {
      next_status.course_ready = true;
      next_status.course_degree = vtg.course_true_degree;
    }
    if (gga.gps_mode_status != 0xFF) {
      next_status.fix_quality_ready = true;
      next_status.fix_quality = gga.gps_mode_status;
    }
    if (gga.online_satellite_count != 0xFF) {
      next_status.satellites_used_ready = true;
      next_status.satellites_used = gga.online_satellite_count;
    }
    if (gsv.update_flag && gsv.total_satellite_count != 0xFF) {
      next_status.satellites_in_view_ready = true;
      next_status.satellites_in_view = gsv.total_satellite_count;
    }
    if (gsv.update_flag) {
      next_status.satellite_info_count = gsv.satellites.size();
      int16_t strongest_cn0 = -1;
      uint16_t strongest_id = 0;
      for (const auto& satellite : gsv.satellites) {
        if (satellite.cn0 > strongest_cn0) {
          strongest_cn0 = satellite.cn0;
          strongest_id = satellite.id;
        }
      }
      if (strongest_cn0 >= 0) {
        next_status.strongest_satellite_ready = true;
        next_status.strongest_satellite_id = strongest_id;
        next_status.strongest_satellite_cn0 = strongest_cn0;
      }
    }
    if (device_utils::IsGnssFloatReady(gga.hdop)) {
      next_status.hdop_ready = true;
      next_status.hdop = gga.hdop;
    }
    if (device_utils::IsGnssFloatReady(gga.altitude)) {
      next_status.altitude_ready = true;
      next_status.altitude = gga.altitude;
      device_utils::CopyString(next_status.altitude_unit, sizeof(next_status.altitude_unit),
          gga.altitude_unit);
    }
    if (gsa.update_flag && !gsa.sentences.empty()) {
      const auto& sentence = gsa.sentences.front();
      if (sentence.fix_mode != 0xFF) {
        next_status.fix_mode_ready = true;
        next_status.fix_mode = sentence.fix_mode;
      }
      if (device_utils::IsGnssFloatReady(sentence.pdop)) {
        next_status.pdop_ready = true;
        next_status.pdop = sentence.pdop;
      }
      if (device_utils::IsGnssFloatReady(sentence.hdop)) {
        next_status.hdop_ready = true;
        next_status.hdop = sentence.hdop;
      }
      if (device_utils::IsGnssFloatReady(sentence.vdop)) {
        next_status.vdop_ready = true;
        next_status.vdop = sentence.vdop;
      }
    }
  }

  gps_status_ = next_status;
  *status = gps_status_;
  return true;
}

}  // namespace lilygo_box::hal
