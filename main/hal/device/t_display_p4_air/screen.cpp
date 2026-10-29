/*
 * @Description: T-Display-P4-Air 显示、触摸与背光实现
 * @Author: LILYGO_L
 * @Date: 2026-08-28 00:00:00
 * @LastEditTime: 2026-09-02 17:53:41
 * @License: GPL 3.0
 */
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>

#include "base/logger.h"
#include "esp_err.h"
#include "esp_lcd_mipi_dsi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "hal/device/common/device_utils.h"
#include "hal/device/t_display_p4_air/device.h"

namespace lilygo_box::hal {
namespace device = lilygo_device_driver::t_display_p4_air::device;
namespace gpio = lilygo_device_driver::t_display_p4_air::gpio;
namespace {

constexpr uint32_t kSy7200aDutyScale = 1000;
constexpr uint32_t kScreenBrightnessFadeUpdateMs = 10;

/**
 * @brief 判断触摸控制器是否可用
 * @param driver 当前板级驱动
 * @return 触摸控制器可用返回 true
 */
bool IsTouchReady(const TDisplayP4AirBoardDriver& driver) {
  return driver.IsTouchReady();
}

cpp_bus_driver::Pwm::DutyCycle ScreenBrightnessPercentToHi8561DutyCycle(
    int clamped_percent) {
  return device_utils::ScreenBrightnessPercentToDutyCycle(
      clamped_percent, kSy7200aDutyScale);
}

}  // namespace

bool TDisplayP4AirDevice::InitializeTouchInterrupt() {
  if (touch_interrupt_initialized_) {
    return true;
  }
  if (tool_ == nullptr || !IsTouchReady(driver_)) {
    return false;
  }

  touch_interrupt_pending_.store(false, std::memory_order_relaxed);
  if (!tool_->InitGpioInterrupt(gpio::hi8561::kTouchInt,
          cpp_bus_driver::PlatformHal::InterruptMode::kFalling, TouchInterruptHandler,
          this, cpp_bus_driver::PlatformHal::GpioStatus::kPullup)) {
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
  if (!driver_.IsScreenReady()) {
    LogMessage(LogLevel::kError, __FILE__, __LINE__,
        "Screen is not ready for display callback registration\n");
    return false;
  }

  display_callbacks_ = callbacks;

  esp_lcd_dpi_panel_event_callbacks_t panel_callbacks = {
      .on_color_trans_done = [](esp_lcd_panel_handle_t,
                                 esp_lcd_dpi_panel_event_data_t*,
                                 void* user_context) -> bool {
        const auto* display_callbacks =
            static_cast<const ScreenProviderDisplayCallbacks*>(user_context);
        if (display_callbacks != nullptr &&
            display_callbacks->flush_ready_callback != nullptr) {
          display_callbacks->flush_ready_callback(
              display_callbacks->callback_context);
        }
        return false;
      },
      .on_refresh_done = [](esp_lcd_panel_handle_t,
                             esp_lcd_dpi_panel_event_data_t*,
                             void* user_context) -> bool {
        const auto* display_callbacks =
            static_cast<const ScreenProviderDisplayCallbacks*>(user_context);
        if (display_callbacks != nullptr &&
            display_callbacks->refresh_done_callback != nullptr) {
          display_callbacks->refresh_done_callback(
              display_callbacks->callback_context);
        }
        return false;
      },
  };

  const auto screen_bus = driver_.bus().screen_mipi_bus;
  if (screen_bus == nullptr) {
    LogMessage(
        LogLevel::kError, __FILE__, __LINE__, "Screen MIPI bus is empty\n");
    return false;
  }

  esp_lcd_panel_handle_t panel = screen_bus->device_handle();
  if (panel == nullptr) {
    LogMessage(
        LogLevel::kError, __FILE__, __LINE__, "Screen panel handle is empty\n");
    return false;
  }

  const int result = esp_lcd_dpi_panel_register_event_callbacks(
      panel, &panel_callbacks, &display_callbacks_);
  if (result != 0) {
    LogMessage(LogLevel::kError, __FILE__, __LINE__,
        "esp_lcd_dpi_panel_register_event_callbacks failed: %s (%#X)\n",
        esp_err_to_name(static_cast<esp_err_t>(result)),
        static_cast<unsigned>(result));
    return false;
  }
  return true;
}

bool TDisplayP4AirDevice::WriteScreenPixels(
    int x_start, int y_start, int x_end, int y_end, const void* pixels) {
  if (!driver_.IsScreenReady()) {
    return false;
  }

  switch (driver_.screen_type()) {
    case device::ScreenType::kHi8561:
      return driver_.chip().hi8561->SendColorStreamCoordinate(
          x_start, y_start, x_end, y_end, pixels);
    case device::ScreenType::kRm69a10:
      return driver_.chip().rm69a10->SendColorStreamCoordinate(
          x_start, y_start, x_end, y_end, pixels);
    default:
      break;
  }
  return false;
}

bool TDisplayP4AirDevice::ReadScreenTouch(TouchPoint* point) {
  if (point == nullptr) {
    return false;
  }
  *point = TouchPoint();

  if (!IsTouchReady(driver_)) {
    return false;
  }

  // 亮屏轮询会顺带消费通知，避免休眠时误把旧中断当成新的手势。
  const bool touch_interrupt_received = ConsumeTouchInterrupt();

  cpp_bus_driver::TouchFrame frame;
  cpp_bus_driver::TouchReadStatus read_status =
      cpp_bus_driver::TouchReadStatus::kInvalidData;
  switch (driver_.screen_type()) {
    case device::ScreenType::kHi8561:
      read_status = driver_.chip().hi8561_touch->ReadPrimaryTouch(&frame);
      break;
    case device::ScreenType::kRm69a10:
      read_status = driver_.chip().gt9895->ReadPrimaryTouch(&frame);
      break;
    default:
      return false;
  }
  if (read_status == cpp_bus_driver::TouchReadStatus::kSuccess ||
      read_status == cpp_bus_driver::TouchReadStatus::kNoData) {
    point->report_sequence = frame.sequence;
    point->report_sequence_valid = true;
  }
  const bool firmware_double_tap =
      (driver_.screen_type() == device::ScreenType::kHi8561 &&
          frame.gesture == static_cast<uint8_t>(
                               cpp_bus_driver::Hi8561Touch::Gesture::kDoubleTap)) ||
      (driver_.screen_type() == device::ScreenType::kRm69a10 &&
          frame.gesture == static_cast<uint8_t>(
                               cpp_bus_driver::Gt9895::Gesture::kDoubleTap));
  if (firmware_double_tap) {
    point->x = -1;
    point->y = -1;
    point->gesture = TouchGesture::kDoubleTap;
    return true;
  }
  const bool hardware_edge_hint =
      frame.edge_touch ||
      (driver_.screen_type() == device::ScreenType::kRm69a10 &&
          touch_interrupt_received &&
          (read_status == cpp_bus_driver::TouchReadStatus::kNoData ||
              (read_status == cpp_bus_driver::TouchReadStatus::kSuccess &&
                  frame.contact_count == 0)));
  if (read_status != cpp_bus_driver::TouchReadStatus::kSuccess) {
    if (read_status != cpp_bus_driver::TouchReadStatus::kNoData ||
        !hardware_edge_hint) {
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

bool TDisplayP4AirDevice::ReadScreenTouchPoints(
    TouchPoint* points, size_t max_points, size_t* point_count) {
  if (point_count != nullptr) {
    *point_count = 0;
  }
  if (points == nullptr || max_points == 0 || point_count == nullptr) {
    return false;
  }

  if (!IsTouchReady(driver_)) {
    return false;
  }

  // 亮屏轮询会顺带消费通知，避免休眠时误把旧中断当成新的手势。
  const bool touch_interrupt_received = ConsumeTouchInterrupt();

  cpp_bus_driver::TouchFrame frame;
  cpp_bus_driver::TouchReadStatus read_status =
      cpp_bus_driver::TouchReadStatus::kInvalidData;
  switch (driver_.screen_type()) {
    case device::ScreenType::kHi8561:
      read_status = driver_.chip().hi8561_touch->ReadTouchFrame(&frame);
      break;
    case device::ScreenType::kRm69a10:
      read_status = driver_.chip().gt9895->ReadTouchFrame(&frame);
      break;
    default:
      return false;
  }
  if (read_status != cpp_bus_driver::TouchReadStatus::kSuccess) {
    if (read_status != cpp_bus_driver::TouchReadStatus::kNoData ||
        driver_.screen_type() != device::ScreenType::kRm69a10 ||
        !touch_interrupt_received) {
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
  if (*point_count == 0 && frame.edge_touch) {
    device_utils::SetHardwareEdgeTouchPoint(&points[0]);
    *point_count = 1;
  }
  return *point_count > 0;
}

bool TDisplayP4AirDevice::SupportsHardwareEdgeTouchHint(
    int display_rotation_angle) const {
  switch (driver_.screen_type()) {
    case device::ScreenType::kHi8561:
      return true;
    case device::ScreenType::kRm69a10:
      return display_rotation_angle == 0 || display_rotation_angle == 180;
    default:
      return false;
  }
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
  return !touch_gesture_wake_enabled_;
}

bool TDisplayP4AirDevice::RefreshTouchWakeConfiguration() {
  bool refreshed = false;
  switch (driver_.screen_type()) {
    case device::ScreenType::kHi8561:
      refreshed = driver_.chip().hi8561_touch != nullptr &&
                  driver_.chip().hi8561_touch->SetGestureWakeEnabled(true);
      break;
    case device::ScreenType::kRm69a10:
      refreshed = driver_.chip().gt9895 != nullptr &&
                  driver_.chip().gt9895->EnterGestureMode();
      break;
    default:
      return false;
  }
  if (refreshed) {
    touch_gesture_wake_enabled_ = true;
  }
  return refreshed;
}

bool TDisplayP4AirDevice::SetScreenBrightnessPercent(int percent) {
  if (!WaitForScreenReady()) {
    return false;
  }

  const int clamped_percent = device_utils::ClampPercent(percent);
  switch (driver_.screen_type()) {
    case device::ScreenType::kHi8561:
      if (driver_.IsScreenBacklightReady() &&
          driver_.chip().sy7200a != nullptr) {
        const cpp_bus_driver::Pwm::DutyCycle duty =
            ScreenBrightnessPercentToHi8561DutyCycle(clamped_percent);
        return driver_.chip().sy7200a->SetDuty(duty);
      }
      break;
    case device::ScreenType::kRm69a10:
      if (driver_.IsScreenReady() && driver_.chip().rm69a10 != nullptr) {
        const uint8_t brightness =
            device_utils::PercentToUint8Value(clamped_percent, UINT8_MAX);
        const bool result = driver_.chip().rm69a10->SetBrightness(brightness);
        if (result) {
          rm69a10_brightness_percent_ = clamped_percent;
        }
        return result;
      }
      break;
    default:
      break;
  }
  return false;
}

bool TDisplayP4AirDevice::FadeScreenBrightnessPercent(
    int target_percent, uint32_t duration_ms) {
  if (!WaitForScreenReady()) {
    return false;
  }

  const int clamped_percent = device_utils::ClampPercent(target_percent);
  if (duration_ms == 0) {
    return SetScreenBrightnessPercent(clamped_percent);
  }

  switch (driver_.screen_type()) {
    case device::ScreenType::kHi8561:
      if (driver_.IsScreenBacklightReady() &&
          driver_.chip().sy7200a != nullptr) {
        const cpp_bus_driver::Pwm::DutyCycle target_duty =
            ScreenBrightnessPercentToHi8561DutyCycle(clamped_percent);
        if (driver_.chip().sy7200a->FadeTo(target_duty, duration_ms,
                cpp_bus_driver::Pwm::FadeMode::kWaitForCompletion)) {
          return true;
        }
      }
      break;
    case device::ScreenType::kRm69a10:
      if (driver_.IsScreenReady() && driver_.chip().rm69a10 != nullptr) {
        const int start_percent = rm69a10_brightness_percent_;
        const int brightness_delta = std::abs(clamped_percent - start_percent);
        if (brightness_delta == 0) {
          return true;
        }
        const int duration_step_count =
            static_cast<int>(duration_ms / kScreenBrightnessFadeUpdateMs);
        const int step_count =
            std::max(1, std::min(brightness_delta, duration_step_count));
        for (int step = 1; step <= step_count; ++step) {
          const int brightness_percent =
              start_percent +
              (clamped_percent - start_percent) * step / step_count;
          if (!driver_.chip().rm69a10->SetBrightness(
                  device_utils::PercentToUint8Value(brightness_percent, UINT8_MAX))) {
            return false;
          }
          rm69a10_brightness_percent_ = brightness_percent;
          vTaskDelay(pdMS_TO_TICKS(
              std::max<uint32_t>(1, duration_ms / step_count)));
        }
        return true;
      }
      break;
    default:
      break;
  }
  return false;
}

bool TDisplayP4AirDevice::WaitForScreenReady() {
  for (int elapsed_ms = 0; elapsed_ms < kScreenReadyTimeoutMs;
      elapsed_ms += kScreenReadyPollMs) {
    if (driver_.IsScreenReady()) {
      return true;
    }
    vTaskDelay(pdMS_TO_TICKS(kScreenReadyPollMs));
  }
  return driver_.IsScreenReady();
}

bool TDisplayP4AirDevice::WaitForTouchReady() {
  for (int elapsed_ms = 0; elapsed_ms < kScreenReadyTimeoutMs;
      elapsed_ms += kScreenReadyPollMs) {
    if (IsTouchReady(driver_)) {
      return true;
    }
    vTaskDelay(pdMS_TO_TICKS(kScreenReadyPollMs));
  }
  return IsTouchReady(driver_);
}

}  // namespace lilygo_box::hal
