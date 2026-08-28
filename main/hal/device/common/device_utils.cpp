/*
 * @Description: 设备 Provider 通用数据转换辅助实现
 * @Author: LILYGO_L
 * @Date: 2026-08-28 00:00:00
 * @LastEditTime: 2026-08-28 00:00:00
 * @License: GPL 3.0
 */
#include "hal/device/common/device_utils.h"

#include <algorithm>
#include <cstdio>

namespace lilygo_box::hal::device_utils {

void SetHardwareEdgeTouchPoint(TouchPoint* point) {
  if (point == nullptr) {
    return;
  }
  *point = TouchPoint();
  point->x = -1;
  point->y = -1;
  point->edge_touch_flag = true;
}

int ClampPercent(int percent) {
  return std::clamp(percent, kMinimumPercent, kMaximumPercent);
}

cpp_bus_driver::Pwm::DutyCycle ScreenBrightnessPercentToDutyCycle(
    int clamped_percent, uint32_t duty_scale) {
  if (clamped_percent <= kMinimumPercent) {
    return {.value = 0, .scale = duty_scale};
  }

  constexpr int kMinimumVisibleBrightnessPercent = 10;
  constexpr int kInputRangeSquared = kMaximumPercent * kMaximumPercent;
  const int input_percent =
      std::max(clamped_percent, kMinimumVisibleBrightnessPercent);
  const uint32_t scaled_duty = static_cast<uint32_t>(
      input_percent * input_percent * duty_scale);
  return {
      .value = (scaled_duty + kInputRangeSquared / 2) / kInputRangeSquared,
      .scale = duty_scale,
  };
}

uint8_t PercentToUint8Value(int percent, uint8_t maximum_value) {
  return static_cast<uint8_t>(
      ClampPercent(percent) * maximum_value / kMaximumPercent);
}

bool IsGnssFloatReady(float value) { return value >= 0.0F; }

void CopyString(char* destination, size_t destination_size,
    const std::string& source) {
  if (destination == nullptr || destination_size == 0) {
    return;
  }
  std::snprintf(destination, destination_size, "%s", source.c_str());
}

}  // namespace lilygo_box::hal::device_utils
