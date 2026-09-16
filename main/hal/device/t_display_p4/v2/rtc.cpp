/*
 * @Description: T-Display-P4 V2 片内 RTC 系统时间适配
 * @Author: LILYGO_L
 * @Date: 2026-09-16 00:00:00
 * @LastEditTime: 2026-09-16 00:00:00
 * @License: GPL 3.0
 */
#include <ctime>
#include <sys/time.h>

#include "hal/device/common/device_utils.h"
#include "hal/device/t_display_p4/device.h"

namespace lilygo_box::hal {

bool TDisplayP4Device::ReadRtcStatus(RtcStatus* status) {
  if (status == nullptr) {
    return false;
  }
  *status = RtcStatus();
  const std::time_t now = std::time(nullptr);
  if (now <= device_utils::kValidUnixTimeThreshold) {
    return false;
  }
  std::tm local_time = {};
  if (localtime_r(&now, &local_time) == nullptr ||
      local_time.tm_year + 1900 < 2000) {
    return false;
  }
  status->ready = true;
  status->clock_integrity = true;
  status->year = static_cast<uint16_t>(local_time.tm_year + 1900);
  status->month = static_cast<uint8_t>(local_time.tm_mon + 1);
  status->day = static_cast<uint8_t>(local_time.tm_mday);
  status->week = static_cast<uint8_t>(local_time.tm_wday);
  status->hour = static_cast<uint8_t>(local_time.tm_hour);
  status->minute = static_cast<uint8_t>(local_time.tm_min);
  status->second = static_cast<uint8_t>(local_time.tm_sec);
  return true;
}

bool TDisplayP4Device::WriteRtcUnixTime(int64_t unix_time) {
  if (unix_time <= device_utils::kValidUnixTimeThreshold) {
    return false;
  }
  const timeval value = {.tv_sec = static_cast<time_t>(unix_time), .tv_usec = 0};
  return settimeofday(&value, nullptr) == 0;
}

}  // namespace lilygo_box::hal
