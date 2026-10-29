/*
 * @Description: T-Display-P4-Air 片内 RTC 系统时间适配
 * @Author: LILYGO_L
 * @Date: 2026-10-28 00:00:00
 * @LastEditTime: 2026-10-28 00:00:00
 * @License: GPL 3.0
 */
#include "hal/device/common/rtc_utils.h"
#include "hal/device/t_display_p4_air/device.h"

namespace lilygo_box::hal {

bool TDisplayP4AirDevice::ReadRtcStatus(RtcStatus* status) {
  return rtc_utils::ReadSystemClockStatus(status);
}

bool TDisplayP4AirDevice::WriteRtcUnixTime(int64_t unix_time) {
  return rtc_utils::WriteSystemClockUnixTime(unix_time);
}

}  // namespace lilygo_box::hal