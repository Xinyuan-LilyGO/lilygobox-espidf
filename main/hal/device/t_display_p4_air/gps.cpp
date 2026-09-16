/*
 * @Description: T-Display-P4-Air GNSS 硬件实现
 * @Author: LILYGO_L
 * @Date: 2026-08-28 00:00:00
 * @LastEditTime: 2026-09-02 17:53:29
 * @License: GPL 3.0
 */
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
#include "hal/device/common/gnss_utils.h"
#include "hal/device/t_display_p4_air/device.h"

namespace lilygo_box::hal {
namespace {

constexpr size_t kGpsMaxReadBufferBytes = 4096;
constexpr uint32_t kNrf9151CommandTimeoutMs = 5000;
constexpr uint32_t kNrf9151StartupDelayMs = 1000;
constexpr uint32_t kNrf9151GnssUpdateIntervalMs = 1000;

}  // namespace

bool TDisplayP4AirDevice::SetGpsEnabled(bool enabled) {
  if (enabled && !gps_parser_.IsReady()) {
    return false;
  }
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
    gps_parser_.Reset();
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
  gps_parser_.Reset();
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

  const auto feed_result = gps_parser_.Feed(buffer.get(), data_length);
  const auto* update = gps_parser_.update();
  next_status.parse_success = feed_result.HasParsedSentence();
  if (next_status.parse_success && update != nullptr) {
    gnss_utils::ApplyUpdate(*update, &next_status);
  }

  gps_status_ = next_status;
  *status = gps_status_;
  xSemaphoreGive(nrf9151_mutex_);
  return true;
}

}  // namespace lilygo_box::hal
