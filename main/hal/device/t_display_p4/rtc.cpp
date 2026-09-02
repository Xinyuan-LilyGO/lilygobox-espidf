/*
 * @Description: T-Display-P4 PCF8563 RTC 实现
 * @Author: LILYGO_L
 * @Date: 2026-08-28 00:00:00
 * @LastEditTime: 2026-09-02 17:53:12
 * @License: GPL 3.0
 */
#include <ctime>

#include "base/logger.h"
#include "hal/device/common/device_utils.h"
#include "hal/device/t_display_p4/device.h"

namespace lilygo_box::hal {

bool TDisplayP4Device::ReadRtcStatus(RtcStatus* status) {
  if (status == nullptr) {
    return false;
  }

  *status = RtcStatus();
  if (!driver_.IsPcf8563Ready() && !driver_.InitPcf8563()) {
    LogMessage(
        LogLevel::kWarning, __FILE__, __LINE__, "Pcf8563 init retry failed\n");
    return false;
  }

  cpp_bus_driver::Pcf8563x::Time time;
  if (!driver_.chip().pcf8563->GetTime(time)) {
    return false;
  }

  status->ready = true;
  status->clock_integrity = driver_.chip().pcf8563->CheckClockIntegrityFlag();
  status->year = static_cast<uint16_t>(time.year) + 2000;
  status->month = time.month;
  status->day = time.day;
  status->week = static_cast<uint8_t>(time.week);
  status->hour = time.hour;
  status->minute = time.minute;
  status->second = time.second;
  return true;
}

bool TDisplayP4Device::WriteRtcUnixTime(int64_t unix_time) {
  if (unix_time <= device_utils::kValidUnixTimeThreshold ||
      (!driver_.IsPcf8563Ready() && !driver_.InitPcf8563())) {
    return false;
  }

  const std::time_t time_value = static_cast<std::time_t>(unix_time);
  std::tm local_time = {};
  if (localtime_r(&time_value, &local_time) == nullptr ||
      local_time.tm_year + 1900 < 2000 || local_time.tm_year + 1900 > 2099) {
    return false;
  }

  cpp_bus_driver::Pcf8563x::Time rtc_time;
  rtc_time.year = static_cast<uint8_t>(local_time.tm_year + 1900 - 2000);
  rtc_time.month = static_cast<uint8_t>(local_time.tm_mon + 1);
  rtc_time.day = static_cast<uint8_t>(local_time.tm_mday);
  rtc_time.week =
      static_cast<cpp_bus_driver::Pcf8563x::Week>(local_time.tm_wday);
  rtc_time.hour = static_cast<uint8_t>(local_time.tm_hour);
  rtc_time.minute = static_cast<uint8_t>(local_time.tm_min);
  rtc_time.second = static_cast<uint8_t>(local_time.tm_sec);
  return driver_.chip().pcf8563->SetTime(rtc_time) &&
         driver_.chip().pcf8563->ClearClockIntegrityFlag();
}

}  // namespace lilygo_box::hal
