/*
 * @Description: T-Display-P4 GNSS 硬件实现
 * @Author: LILYGO_L
 * @Date: 2026-08-28 00:00:00
 * @LastEditTime: 2026-09-02 17:53:01
 * @License: GPL 3.0
 */
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <new>

#include "base/logger.h"
#include "hal/device/common/gnss_utils.h"
#include "hal/device/t_display_p4/device.h"

namespace lilygo_box::hal {
namespace {

constexpr size_t kGpsMaxReadBufferBytes = 4096;

}  // namespace

bool TDisplayP4Device::SetGpsEnabled(bool enabled) {
  if (!enabled) {
    gps_parser_.Reset();
    gps_running_ = false;
    gps_status_.running = false;
    const bool result = driver_.SetL76kSleep(true);
    if (!result) {
      LogMessage(
          LogLevel::kWarning, __FILE__, __LINE__, "Disable GPS failed\n");
    }
    return result;
  }

  if (!gps_parser_.IsReady() || !driver_.SetL76kSleep(false) ||
      !driver_.IsL76kReady()) {
    driver_.SetL76kSleep(true);
    LogMessage(LogLevel::kWarning, __FILE__, __LINE__, "Enable GPS failed\n");
    return false;
  }

  // CIT GPS 测试同时接收 GPS、北斗和 GLONASS，并输出定位与卫星信息。
  auto& l76k = driver_.chip().l76k;
  cpp_bus_driver::L76k::NmeaOutputConfig nmea_config;
  nmea_config.rmc = 1;
  nmea_config.gga = 1;
  nmea_config.gsa = 1;
  // 多星座 GSV 数据较多，沿用示例的每五次定位输出一次。
  nmea_config.gsv = 5;
  if (!l76k->SetGnssConstellation(
          cpp_bus_driver::L76k::GnssConstellation::kGpsBeidouGlonass) ||
      !l76k->SetNmeaOutputConfig(nmea_config)) {
    gps_running_ = false;
    gps_status_.running = false;
    driver_.SetL76kSleep(true);
    LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
        "Configure GPS + BeiDou + GLONASS test output failed\n");
    return false;
  }
  LogMessage(LogLevel::kDebug, __FILE__, __LINE__,
      "GPS test configured: GPS + BeiDou + GLONASS, GSA every update, "
      "GSV every five updates\n");

  gps_satellites_.Reset();
  gps_status_ = GpsStatus();
  gps_parser_.Reset();
  gps_running_ = true;
  gps_status_.running = true;
  gps_status_.update_interval_ms = driver_.chip().l76k->update_interval_ms();
  if (!driver_.chip().l76k->ClearRxBufferData()) {
    gps_running_ = false;
    gps_status_.running = false;
    driver_.SetL76kSleep(true);
    return false;
  }
  return true;
}

bool TDisplayP4Device::ReadGpsStatus(GpsStatus* status) {
  if (status == nullptr) {
    return false;
  }

  gps_satellites_.Refresh(&gps_status_);
  gps_status_.running = gps_running_;
  if (driver_.IsL76kReady()) {
    gps_status_.update_interval_ms = driver_.chip().l76k->update_interval_ms();
  }
  *status = gps_status_;
  if (!gps_running_) {
    return true;
  }
  if (!driver_.IsL76kReady()) {
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
  next_status.update_interval_ms = driver_.chip().l76k->update_interval_ms();

  const auto feed_result = gps_parser_.Feed(buffer.get(), data_length);
  const auto* update = gps_parser_.update();
  next_status.parse_success = feed_result.HasParsedSentence();
  if (next_status.parse_success && update != nullptr) {
    gnss_utils::ApplyUpdate(*update, gps_satellites_, &next_status);
  }

  gps_status_ = next_status;
  *status = gps_status_;
  return true;
}

}  // namespace lilygo_box::hal
