/*
 * @Description: T-Display-P4-Air 片内 RTC 系统时间适配
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