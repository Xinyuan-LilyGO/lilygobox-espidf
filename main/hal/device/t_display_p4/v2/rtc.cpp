/*
 * @Description: T-Display-P4 V2 片内 RTC 系统时间适配
 */
#include "hal/device/common/rtc_utils.h"
#include "hal/device/t_display_p4/device.h"

namespace lilygo_box::hal {

bool TDisplayP4Device::ReadRtcStatus(RtcStatus* status) {
  return rtc_utils::ReadSystemClockStatus(status);
}

bool TDisplayP4Device::WriteRtcUnixTime(int64_t unix_time) {
  return rtc_utils::WriteSystemClockUnixTime(unix_time);
}

}  // namespace lilygo_box::hal