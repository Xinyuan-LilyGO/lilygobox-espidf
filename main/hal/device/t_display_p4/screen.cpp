/*
 * @Description: T-Display-P4 显示、触摸与背光实现
 * @Author: LILYGO_L
 * @Date: 2026-08-28 00:00:00
 * @LastEditTime: 2026-08-28 00:00:00
 * @License: GPL 3.0
 */
#include "hal/device/t_display_p4/device.h"

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

namespace lilygo_box::hal {
namespace device = lilygo_device_driver::t_display_p4::device;
namespace gpio = lilygo_device_driver::t_display_p4::gpio;
namespace {

constexpr uint32_t kPt4103DutyScale = 1000;
constexpr uint32_t kScreenBrightnessFadeUpdateMs = 10;
constexpr uint8_t kRm69a10BrightnessMax = UINT8_MAX;

cpp_bus_driver::Pwm::DutyCycle ScreenBrightnessPercentToHi8561DutyCycle(
    int clamped_percent) {
  return device_utils::ScreenBrightnessPercentToDutyCycle(
      clamped_percent, kPt4103DutyScale);
}

uint8_t ScreenBrightnessPercentToRm69a10Value(int clamped_percent) {
  return static_cast<uint8_t>(clamped_percent * kRm69a10BrightnessMax /
                              device_utils::kMaximumPercent);
}

}  // namespace

bool TDisplayP4Device::InitializeTouchInterrupt() {
  if (touch_interrupt_initialized_) {
    return true;
  }
  if (tool_ == nullptr || !driver_.IsTouchReady() ||
      !driver_.IsXl9535Ready() || driver_.chip().xl9535 == nullptr) {
    return false;
  }

  if (!driver_.chip().xl9535->ClearIrqFlag()) {
    LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
        "Clear XL9535 interrupt failed during initialization\n");
    return false;
  }
  touch_interrupt_pending_.store(false, std::memory_order_relaxed);
  if (!tool_->InitGpioInterrupt(gpio::xl9535::kInt,
          cpp_bus_driver::Tool::InterruptMode::kFalling,
          TouchInterruptHandler, this,
          cpp_bus_driver::Tool::GpioStatus::kPullup)) {
    return false;
  }

  touch_interrupt_initialized_ = true;
  if (!tool_->GpioRead(gpio::xl9535::kInt)) {
    touch_interrupt_pending_.store(true, std::memory_order_relaxed);
  }
  return true;
}

void TDisplayP4Device::TouchInterruptHandler(void* context) {
  if (context == nullptr) {
    return;
  }
  auto* device = static_cast<TDisplayP4Device*>(context);
  device->touch_interrupt_pending_.store(true, std::memory_order_relaxed);
}

bool TDisplayP4Device::RegisterScreenDisplayCallbacks(
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

bool TDisplayP4Device::WriteScreenPixels(
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

bool TDisplayP4Device::ReadScreenTouch(TouchPoint* point) {
  if (point == nullptr) {
    return false;
  }
  *point = TouchPoint();

  if (!driver_.IsTouchReady()) {
    return false;
  }

  // 亮屏轮询也需要清除 XL9535 的汇总中断锁存，确保后续边沿可继续上报。
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

  if (driver_.screen_type() == device::ScreenType::kHi8561 &&
      frame.gesture == static_cast<uint8_t>(
          cpp_bus_driver::Hi8561Touch::Gesture::kDoubleTap)) {
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
  const bool hardware_edge_hint = frame.edge_touch ||
      (driver_.screen_type() == device::ScreenType::kRm69a10 &&
          touch_interrupt_received &&
          (read_status == cpp_bus_driver::TouchReadStatus::kNoData ||
              (read_status == cpp_bus_driver::TouchReadStatus::kSuccess &&
                  frame.contact_count == 0)));
  if (read_status != cpp_bus_driver::TouchReadStatus::kSuccess) {
    if (!hardware_edge_hint ||
        read_status != cpp_bus_driver::TouchReadStatus::kNoData) {
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

bool TDisplayP4Device::ReadScreenTouchPoints(
    TouchPoint* points, size_t max_points, size_t* point_count) {
  if (point_count != nullptr) {
    *point_count = 0;
  }
  if (points == nullptr || max_points == 0 || point_count == nullptr) {
    return false;
  }

  if (!driver_.IsTouchReady()) {
    return false;
  }

  // 亮屏轮询也需要清除 XL9535 的汇总中断锁存，确保后续边沿可继续上报。
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

  // GT9895 的通用触摸中断只有在缺少有效坐标时才可作为边缘候选提示。
  // 有效触摸不能携带这个提示，避免全局手势抢占边缘页面控件。
  const bool hardware_edge_hint = frame.edge_touch ||
      (driver_.screen_type() == device::ScreenType::kRm69a10 &&
          touch_interrupt_received &&
          (read_status == cpp_bus_driver::TouchReadStatus::kNoData ||
              (read_status == cpp_bus_driver::TouchReadStatus::kSuccess &&
                  frame.contact_count == 0)));
  if (read_status != cpp_bus_driver::TouchReadStatus::kSuccess) {
    if (!hardware_edge_hint ||
        read_status != cpp_bus_driver::TouchReadStatus::kNoData) {
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

bool TDisplayP4Device::SupportsHardwareEdgeTouchHint(
    int display_rotation_angle) const {
  switch (driver_.screen_type()) {
    case device::ScreenType::kHi8561:
      // HI8561 会在物理上下左右四边上报专用边缘触摸标记，
      // 因此软件旋转到任意方向时都可将其作为手势候选提示。
      return true;
    case device::ScreenType::kRm69a10:
      // GT9895 当前固件只在物理左右边缘存在坐标抑制。0°/180°
      // 竖屏时应用的返回边缘与其重合；90°/270° 横屏时应用左右边
      // 对应物理上下边，不得使用这个提示，否则普通中断可能被误判。
      return display_rotation_angle == 0 || display_rotation_angle == 180;
    default:
      return false;
  }
}

bool TDisplayP4Device::SupportsTouchInterrupt() const {
  return touch_interrupt_initialized_;
}

bool TDisplayP4Device::ConsumeTouchInterrupt() {
  if (!touch_interrupt_initialized_ ||
      !touch_interrupt_pending_.exchange(false, std::memory_order_relaxed)) {
    return false;
  }
  if (!driver_.IsXl9535Ready() || driver_.chip().xl9535 == nullptr) {
    return false;
  }

  // GPIO5 是 XL9535 的汇总中断，芯片没有独立的中断源状态寄存器。任务先
  // 清除两个输入端口的锁存，再由触摸报告内容确认这次通知是否属于触摸。
  if (!driver_.chip().xl9535->ClearIrqFlag()) {
    LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
        "Clear XL9535 interrupt failed\n");
    return false;
  }
  return true;
}

bool TDisplayP4Device::SetScreenBrightnessPercent(int percent) {
  if (!WaitForScreenReady()) {
    return false;
  }

  const int clamped_percent = device_utils::ClampPercent(percent);
  switch (driver_.screen_type()) {
    case device::ScreenType::kHi8561:
      if (driver_.IsPt4103Ready()) {
        const cpp_bus_driver::Pwm::DutyCycle duty =
            ScreenBrightnessPercentToHi8561DutyCycle(clamped_percent);
        return driver_.chip().pt4103->SetDuty(duty);
      }
      break;
    case device::ScreenType::kRm69a10:
      if (driver_.IsRm69a10Ready()) {
        const uint8_t brightness =
            ScreenBrightnessPercentToRm69a10Value(clamped_percent);
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

bool TDisplayP4Device::FadeScreenBrightnessPercent(
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
      if (driver_.IsPt4103Ready()) {
        const cpp_bus_driver::Pwm::DutyCycle target_duty =
            ScreenBrightnessPercentToHi8561DutyCycle(clamped_percent);
        if (driver_.chip().pt4103->FadeTo(target_duty, duration_ms,
                cpp_bus_driver::Pwm::FadeMode::kWaitForCompletion)) {
          return true;
        }
      }
      break;
    case device::ScreenType::kRm69a10:
      if (driver_.IsRm69a10Ready()) {
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
          const int brightness_percent = start_percent +
              (clamped_percent - start_percent) * step / step_count;
          const uint8_t brightness =
              ScreenBrightnessPercentToRm69a10Value(brightness_percent);
          if (!driver_.chip().rm69a10->SetBrightness(brightness)) {
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

bool TDisplayP4Device::WaitForScreenReady() {
  for (int elapsed_ms = 0; elapsed_ms < kScreenReadyTimeoutMs;
      elapsed_ms += kScreenReadyPollMs) {
    if (driver_.IsScreenReady()) {
      return true;
    }
    vTaskDelay(pdMS_TO_TICKS(kScreenReadyPollMs));
  }
  return driver_.IsScreenReady();
}

bool TDisplayP4Device::WaitForTouchReady() {
  for (int elapsed_ms = 0; elapsed_ms < kScreenReadyTimeoutMs;
      elapsed_ms += kScreenReadyPollMs) {
    if (driver_.IsTouchReady()) {
      return true;
    }
    vTaskDelay(pdMS_TO_TICKS(kScreenReadyPollMs));
  }
  return driver_.IsTouchReady();
}

}  // namespace lilygo_box::hal
