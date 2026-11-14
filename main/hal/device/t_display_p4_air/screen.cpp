/*
 * @Description: T-Display-P4-Air 显示、触摸与背光实现
 * @Author: LILYGO_L
 * @Date: 2026-08-28 00:00:00
 * @LastEditTime: 2026-11-14 10:41:03
 */
#include <atomic>
#include <cstdint>

#include "base/logger.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "hal/device/common/screen_utils.h"
#include "hal/device/common/touch_utils.h"
#include "hal/device/t_display_p4_air/device.h"

namespace lilygo_box::hal {
namespace device = lilygo_device_driver::t_display_p4_air::device;
namespace gpio = lilygo_device_driver::t_display_p4_air::gpio;
namespace {

constexpr uint32_t kTouchReadDiagnosticIntervalMs = 1000;

// Air 与 P4 V2 使用 SY7200A，P4 V1 在自己的入口选择 PT4103。
cpp_bus_driver::Pwm* GetScreenBacklight(TDisplayP4AirBoardDriver& driver) {
  return driver.IsScreenBacklightReady() ? driver.chip().sy7200a.get()
                                         : nullptr;
}

// 将板级驱动映射为公共接口使用的实际芯片。
screen_utils::Hardware GetScreenHardware(
    const TDisplayP4AirBoardDriver& driver) {
  screen_utils::Hardware hardware;
  using Type = decltype(driver.screen_type());
  switch (driver.screen_type()) {
    case Type::kHi8561:
      hardware.type = screen_utils::Hardware::Type::kHi8561;
      break;
    case Type::kRm69a10:
      hardware.type = screen_utils::Hardware::Type::kRm69a10;
      break;
    default:
      break;
  }
  hardware.ready = driver.IsScreenReady();
  hardware.hi8561 = driver.chip().hi8561.get();
  hardware.rm69a10 = driver.chip().rm69a10.get();
  const auto bus = driver.bus().screen_mipi_bus;
  hardware.panel = bus != nullptr ? bus->device_handle() : nullptr;
  return hardware;
}

touch_utils::Hardware GetTouchHardware(const TDisplayP4AirBoardDriver& driver) {
  touch_utils::Hardware hardware;
  using Type = decltype(driver.screen_type());
  switch (driver.screen_type()) {
    case Type::kHi8561:
      hardware.type = touch_utils::Hardware::Type::kHi8561;
      break;
    case Type::kRm69a10:
      hardware.type = touch_utils::Hardware::Type::kGt9895;
      break;
    default:
      break;
  }
  hardware.ready = driver.IsTouchReady();
  hardware.hi8561_touch = driver.chip().hi8561_touch.get();
  hardware.gt9895 = driver.chip().gt9895.get();
  return hardware;
}

}  // namespace

bool TDisplayP4AirDevice::InitializeTouchInterrupt() {
  if (touch_interrupt_initialized_) {
    return true;
  }
  if (tool_ == nullptr || !driver_.IsTouchReady()) {
    return false;
  }

  touch_interrupt_pending_.store(false, std::memory_order_relaxed);
  if (!tool_->InitGpioInterrupt(gpio::hi8561::kTouchInt,
          cpp_bus_driver::PlatformHal::InterruptMode::kFalling,
          TouchInterruptHandler, this,
          cpp_bus_driver::PlatformHal::GpioStatus::kPullup)) {
    return false;
  }

  touch_interrupt_initialized_ = true;
  touch_interrupt_line_active_ = !tool_->GpioRead(gpio::hi8561::kTouchInt);
  if (touch_interrupt_line_active_) {
    touch_interrupt_pending_.store(true, std::memory_order_relaxed);
  }
  return true;
}

void TDisplayP4AirDevice::TouchInterruptHandler(void* context) {
  if (context == nullptr) {
    return;
  }
  auto* device = static_cast<TDisplayP4AirDevice*>(context);
  device->touch_interrupt_pending_.store(true, std::memory_order_relaxed);
}

bool TDisplayP4AirDevice::RegisterScreenDisplayCallbacks(
    const ScreenProviderDisplayCallbacks& callbacks) {
  return screen_utils::RegisterDisplayCallbacks(
      GetScreenHardware(driver_), callbacks, &display_callbacks_);
}

bool TDisplayP4AirDevice::WriteScreenPixels(
    int x_start, int y_start, int x_end, int y_end, const void* pixels) {
  return screen_utils::WritePixels(
      GetScreenHardware(driver_), x_start, y_start, x_end, y_end, pixels);
}

bool TDisplayP4AirDevice::ReadScreenTouch(TouchPoint* point) {
  if (point == nullptr) return false;
  *point = TouchPoint();
  if (!driver_.IsTouchReady()) return false;
  // 由板级入口消费触摸中断，公共解析不操作 GPIO。
  const bool interrupt_received = ConsumeTouchInterrupt();
  cpp_bus_driver::TouchFrame frame;
  const auto status =
      touch_utils::ReadTouchFrame(GetTouchHardware(driver_), &frame, true);
  touch_utils::UpdateTouchRecovery(
      status, &consecutive_touch_bus_errors_, &touch_gesture_wake_enabled_);
  const uint32_t now_ms =
      static_cast<uint32_t>(xTaskGetTickCount() * portTICK_PERIOD_MS);
  if (ShouldLog(LogLevel::kDebug) && now_ms - last_touch_read_diagnostic_ms_ >=
                                         kTouchReadDiagnosticIntervalMs) {
    last_touch_read_diagnostic_ms_ = now_ms;
    touch_utils::LogTouchRead(status, frame, gpio::hi8561::kTouchInt,
        tool_ != nullptr && !tool_->GpioRead(gpio::hi8561::kTouchInt),
        touch_interrupt_initialized_, touch_gesture_wake_enabled_);
  }
  return touch_utils::DecodePrimaryTouch(status, frame,
      (GetTouchHardware(driver_).type == touch_utils::Hardware::Type::kGt9895),
      interrupt_received, point);
}

bool TDisplayP4AirDevice::ReadScreenTouchPoints(
    TouchPoint* points, size_t max_points, size_t* point_count) {
  if (point_count != nullptr) *point_count = 0;
  if (points == nullptr || max_points == 0 || point_count == nullptr ||
      !driver_.IsTouchReady())
    return false;
  const bool interrupt_received = ConsumeTouchInterrupt();
  cpp_bus_driver::TouchFrame frame;
  const auto status =
      touch_utils::ReadTouchFrame(GetTouchHardware(driver_), &frame, false);
  return touch_utils::DecodeTouchPoints(status, frame,
      (GetTouchHardware(driver_).type == touch_utils::Hardware::Type::kGt9895),
      interrupt_received, points, max_points, point_count);
}

bool TDisplayP4AirDevice::SupportsHardwareEdgeTouchHint(
    int display_rotation_angle) const {
  return touch_utils::SupportsHardwareEdgeTouchHint(
      GetTouchHardware(driver_), display_rotation_angle);
}

bool TDisplayP4AirDevice::SupportsTouchInterrupt() const {
  return touch_interrupt_initialized_;
}

bool TDisplayP4AirDevice::ConsumeTouchInterrupt(bool* edge_received) {
  if (edge_received != nullptr) {
    *edge_received = false;
  }
  if (!touch_interrupt_initialized_) {
    return false;
  }
  // 原子标志处理正常下降沿，当前低电平用于恢复 HI8561 漏掉的边沿。
  const bool interrupt_pending =
      touch_interrupt_pending_.exchange(false, std::memory_order_relaxed);
  const bool interrupt_line_active =
      tool_ != nullptr && !tool_->GpioRead(gpio::hi8561::kTouchInt);
  const bool recovered_falling_edge =
      interrupt_line_active && !touch_interrupt_line_active_;
  touch_interrupt_line_active_ = interrupt_line_active;
  if (edge_received != nullptr) {
    *edge_received = interrupt_pending || recovered_falling_edge;
  }
  return interrupt_pending || interrupt_line_active;
}

bool TDisplayP4AirDevice::RequiresContinuousSleepingTouchPolling() const {
  return touch_utils::RequiresSleepingTouchPolling(
      GetTouchHardware(driver_), touch_gesture_wake_enabled_);
}

bool TDisplayP4AirDevice::RefreshTouchWakeConfiguration() {
  return touch_utils::RefreshTouchWakeConfiguration(GetTouchHardware(driver_),
      &touch_gesture_wake_enabled_, &consecutive_touch_bus_errors_);
}

bool TDisplayP4AirDevice::SetTouchGestureWakeEnabled(bool enabled) {
  return touch_utils::SetTouchGestureWakeEnabled(
      GetTouchHardware(driver_), enabled);
}

bool TDisplayP4AirDevice::SetScreenBrightnessPercent(int percent) {
  if (!WaitForScreenReady()) return false;
  return screen_utils::SetBrightness(GetScreenHardware(driver_),
      GetScreenBacklight(driver_), percent, &rm69a10_brightness_percent_);
}

bool TDisplayP4AirDevice::FadeScreenBrightnessPercent(
    int target_percent, uint32_t duration_ms) {
  if (!WaitForScreenReady()) return false;
  return screen_utils::FadeBrightness(GetScreenHardware(driver_),
      GetScreenBacklight(driver_), target_percent, duration_ms,
      &rm69a10_brightness_percent_);
}

bool TDisplayP4AirDevice::WaitForScreenReady() {
  for (int elapsed_ms = 0; elapsed_ms < kScreenReadyTimeoutMs;
      elapsed_ms += kScreenReadyPollMs) {
    if (driver_.IsScreenReady()) return true;
    vTaskDelay(pdMS_TO_TICKS(kScreenReadyPollMs));
  }
  return driver_.IsScreenReady();
}

bool TDisplayP4AirDevice::WaitForTouchReady() {
  for (int elapsed_ms = 0; elapsed_ms < kScreenReadyTimeoutMs;
      elapsed_ms += kScreenReadyPollMs) {
    if (driver_.IsTouchReady()) return true;
    vTaskDelay(pdMS_TO_TICKS(kScreenReadyPollMs));
  }
  return driver_.IsTouchReady();
}

}  // namespace lilygo_box::hal
