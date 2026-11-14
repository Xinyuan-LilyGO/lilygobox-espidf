/*
 * @Description: P4 V1/V2 与 Air 的触摸读取与手势处理公共实现
 * @Author: LILYGO_L
 * @Date: 2026-11-14 10:53:16
 * @LastEditTime: 2026-11-14 19:27:53
 */
#include "hal/device/common/touch_utils.h"

#include <algorithm>
#include <cstdlib>

#include "base/logger.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "hal/device/common/device_utils.h"
namespace lilygo_box::hal::touch_utils {
bool SupportsHardwareEdgeTouchHint(const Hardware& hardware, int angle) {
  using Type = Hardware::Type;
  switch (hardware.type) {
    case Type::kHi8561:
      return true;
    case Type::kGt9895:
      return angle == 0 || angle == 180;
    default:
      return false;
  }
}

bool RequiresSleepingTouchPolling(
    const Hardware& hardware, bool gesture_enabled) {
  using Type = Hardware::Type;
  switch (hardware.type) {
    case Type::kHi8561:
    case Type::kGt9895:
      return !gesture_enabled;
    default:
      return false;
  }
}

bool SetTouchGestureWakeEnabled(const Hardware& hardware, bool enabled) {
  if (!hardware.ready) return false;
  using Type = Hardware::Type;
  switch (hardware.type) {
    case Type::kHi8561:
      return hardware.hi8561_touch != nullptr &&
             hardware.hi8561_touch->SetGestureWakeEnabled(enabled);
    case Type::kGt9895:
      if (hardware.gt9895 == nullptr) return false;
      return enabled ? hardware.gt9895->EnterGestureMode()
                     : hardware.gt9895->ExitGestureMode();
    default:
      return false;
  }
}

bool RefreshTouchWakeConfiguration(const Hardware& hardware,
    bool* gesture_enabled, uint8_t* consecutive_errors) {
  const bool refreshed = SetTouchGestureWakeEnabled(hardware, true);
  *gesture_enabled = refreshed;
  if (refreshed) *consecutive_errors = 0;
  return refreshed;
}

cpp_bus_driver::TouchReadStatus ReadTouchFrame(const Hardware& hardware,
    cpp_bus_driver::TouchFrame* frame, bool primary_only) {
  using Status = cpp_bus_driver::TouchReadStatus;
  if (frame == nullptr || !hardware.ready) return Status::kInvalidData;
  using Type = Hardware::Type;
  switch (hardware.type) {
    case Type::kHi8561:
      if (hardware.hi8561_touch == nullptr) return Status::kInvalidData;
      return primary_only ? hardware.hi8561_touch->ReadPrimaryTouch(frame)
                          : hardware.hi8561_touch->ReadTouchFrame(frame);
    case Type::kGt9895:
      if (hardware.gt9895 == nullptr) return Status::kInvalidData;
      return primary_only ? hardware.gt9895->ReadPrimaryTouch(frame)
                          : hardware.gt9895->ReadTouchFrame(frame);
    default:
      return Status::kInvalidData;
  }
}
bool DecodePrimaryTouch(cpp_bus_driver::TouchReadStatus status,
    const cpp_bus_driver::TouchFrame& frame, bool rm69a10,
    bool interrupt_received, TouchPoint* point) {
  if (point == nullptr) return false;
  *point = TouchPoint();
  if (status == cpp_bus_driver::TouchReadStatus::kSuccess ||
      status == cpp_bus_driver::TouchReadStatus::kNoData) {
    point->report_sequence = frame.sequence;
    point->report_sequence_valid = true;
  }

  const bool firmware_double_tap =
      (!rm69a10 && frame.gesture ==
                       static_cast<uint8_t>(
                           cpp_bus_driver::Hi8561Touch::Gesture::kDoubleTap)) ||
      (rm69a10 &&
          frame.gesture == static_cast<uint8_t>(
                               cpp_bus_driver::Gt9895::Gesture::kDoubleTap));
  if (firmware_double_tap) {
    point->x = -1;
    point->y = -1;
    point->gesture = TouchGesture::kDoubleTap;
    return true;
  }
  // GT9895 在物理左右边缘可能先产生硬件中断而暂不提供有效坐标。
  // 该中断只作为应用层边缘手势候选提示，不能单独触发返回操作。
  // GT9895 的 INT 是通用触摸通知，不能把每次中断都解释为边缘触摸。
  // 只有收到中断但固件暂时没有提供坐标时，才将其作为边缘候选提示；
  // 普通有效坐标必须继续交给 LVGL，否则屏幕边缘控件会全部失效。
  const bool hardware_edge_hint =
      frame.edge_touch ||
      (rm69a10 && interrupt_received &&
          (status == cpp_bus_driver::TouchReadStatus::kNoData ||
              (status == cpp_bus_driver::TouchReadStatus::kSuccess &&
                  frame.contact_count == 0)));
  if (status != cpp_bus_driver::TouchReadStatus::kSuccess) {
    if (!hardware_edge_hint ||
        status != cpp_bus_driver::TouchReadStatus::kNoData) {
      return false;
    }
    device_utils::SetHardwareEdgeTouchPoint(point);
    return true;
  }
  if (frame.contact_count == 0) {
    if (!hardware_edge_hint) {
      return false;
    }
    device_utils::SetHardwareEdgeTouchPoint(point);
    return true;
  }

  const cpp_bus_driver::TouchContact& contact = frame.contacts[0];
  point->id = contact.id;
  point->x = contact.x;
  point->y = contact.y;
  point->pressure =
      static_cast<uint8_t>(std::min<uint16_t>(contact.pressure, UINT8_MAX));
  point->edge_touch_flag = frame.edge_touch;
  return true;
}

bool DecodeTouchPoints(cpp_bus_driver::TouchReadStatus status,
    const cpp_bus_driver::TouchFrame& frame, bool rm69a10,
    bool interrupt_received, TouchPoint* points, size_t max_points,
    size_t* point_count) {
  if (point_count != nullptr) *point_count = 0;
  if (points == nullptr || max_points == 0 || point_count == nullptr)
    return false;
  // GT9895 的通用触摸中断只有在缺少有效坐标时才可作为边缘候选提示。
  // 有效触摸不能携带这个提示，避免全局手势抢占边缘页面控件。
  const bool hardware_edge_hint =
      frame.edge_touch ||
      (rm69a10 && interrupt_received &&
          (status == cpp_bus_driver::TouchReadStatus::kNoData ||
              (status == cpp_bus_driver::TouchReadStatus::kSuccess &&
                  frame.contact_count == 0)));
  if (status != cpp_bus_driver::TouchReadStatus::kSuccess) {
    if (!hardware_edge_hint ||
        status != cpp_bus_driver::TouchReadStatus::kNoData) {
      return false;
    }
    device_utils::SetHardwareEdgeTouchPoint(&points[0]);
    *point_count = 1;
    return true;
  }
  const size_t count = std::min<size_t>(max_points, frame.contact_count);
  for (size_t i = 0; i < count; ++i) {
    const cpp_bus_driver::TouchContact& contact = frame.contacts[i];
    points[i].id = contact.id;
    points[i].x = contact.x;
    points[i].y = contact.y;
    points[i].pressure =
        static_cast<uint8_t>(std::min<uint16_t>(contact.pressure, UINT8_MAX));
    points[i].edge_touch_flag = frame.edge_touch;
  }
  *point_count = count;
  if (*point_count == 0 && hardware_edge_hint) {
    device_utils::SetHardwareEdgeTouchPoint(&points[0]);
    *point_count = 1;
  }
  return *point_count > 0;
}

void UpdateTouchRecovery(cpp_bus_driver::TouchReadStatus status,
    uint8_t* consecutive_errors, bool* gesture_enabled) {
  constexpr uint8_t kRecoveryThreshold = 3;
  if (status != cpp_bus_driver::TouchReadStatus::kBusError) {
    *consecutive_errors = 0;
    return;
  }
  if (*consecutive_errors < kRecoveryThreshold) ++*consecutive_errors;
  if (*gesture_enabled && *consecutive_errors >= kRecoveryThreshold) {
    *gesture_enabled = false;
    LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
        "Touch wake recovery requested after %u consecutive bus read errors\n",
        static_cast<unsigned int>(*consecutive_errors));
  }
}

void LogTouchRead(cpp_bus_driver::TouchReadStatus status,
    const cpp_bus_driver::TouchFrame& frame, int interrupt_gpio,
    bool interrupt_low, bool interrupt_initialized, bool gesture_enabled) {
  const char* status_name = "invalid data";
  switch (status) {
    case cpp_bus_driver::TouchReadStatus::kSuccess:
      status_name = "success";
      break;
    case cpp_bus_driver::TouchReadStatus::kNoData:
      status_name = "no data";
      break;
    case cpp_bus_driver::TouchReadStatus::kBusError:
      status_name = "bus error";
      break;
    case cpp_bus_driver::TouchReadStatus::kInvalidData:
      break;
  }
  LogMessage(LogLevel::kDebug, __FILE__, __LINE__,
      "Touch read result (status: %s, sequence: %u, gesture: 0X%02X, "
      "contacts: %u, INT GPIO: %d, INT low: %s, interrupt registered: %s, "
      "gesture wake configured: %s)\n",
      status_name, static_cast<unsigned int>(frame.sequence),
      static_cast<unsigned int>(frame.gesture),
      static_cast<unsigned int>(frame.contact_count), interrupt_gpio,
      interrupt_low ? "yes" : "no", interrupt_initialized ? "yes" : "no",
      gesture_enabled ? "yes" : "no");
}

}  // namespace lilygo_box::hal::touch_utils
