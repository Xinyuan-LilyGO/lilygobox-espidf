/*
 * @Description: P4 V1/V2 与 Air 的屏幕显示与亮度公共实现
 */
#include "hal/device/common/screen_utils.h"

#include <algorithm>
#include <cstdlib>

#include "base/logger.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "hal/device/common/device_utils.h"
namespace lilygo_box::hal::screen_utils {
bool RegisterDisplayCallbacks(
    esp_lcd_panel_handle_t panel, ScreenProviderDisplayCallbacks* callbacks) {
  if (panel == nullptr || callbacks == nullptr) {
    LogMessage(
        LogLevel::kError, __FILE__, __LINE__, "Screen panel handle is empty\n");
    return false;
  }
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

  const esp_err_t result = esp_lcd_dpi_panel_register_event_callbacks(
      panel, &panel_callbacks, callbacks);
  if (result != ESP_OK) {
    LogMessage(LogLevel::kError, __FILE__, __LINE__,
        "esp_lcd_dpi_panel_register_event_callbacks failed: %s (%#X)\n",
        esp_err_to_name(result), static_cast<unsigned>(result));
    return false;
  }
  return true;
}

bool WritePixels(const Hardware& hardware, int x_start, int y_start, int x_end,
    int y_end, const void* pixels) {
  if (!hardware.ready) return false;
  using Type = Hardware::Type;
  switch (hardware.type) {
    case Type::kHi8561:
      return hardware.hi8561 != nullptr &&
             hardware.hi8561->SendColorStreamCoordinate(
                 x_start, y_start, x_end, y_end, pixels);
    case Type::kRm69a10:
      return hardware.rm69a10 != nullptr &&
             hardware.rm69a10->SendColorStreamCoordinate(
                 x_start, y_start, x_end, y_end, pixels);
    default:
      return false;
  }
}

bool RegisterDisplayCallbacks(const Hardware& hardware,
    const ScreenProviderDisplayCallbacks& callbacks,
    ScreenProviderDisplayCallbacks* stored_callbacks) {
  if (!hardware.ready || stored_callbacks == nullptr) return false;
  *stored_callbacks = callbacks;
  return RegisterDisplayCallbacks(hardware.panel, stored_callbacks);
}

bool SetBrightness(const Hardware& hardware, cpp_bus_driver::Pwm* backlight,
    int percent, int* rm69a10_percent) {
  if (!hardware.ready) return false;
  const int target = device_utils::ClampPercent(percent);
  using Type = Hardware::Type;
  switch (hardware.type) {
    case Type::kHi8561:
      return backlight != nullptr &&
             backlight->SetDuty(
                 device_utils::ScreenBrightnessPercentToDutyCycle(
                     target, 1000));
    case Type::kRm69a10:
      if (hardware.rm69a10 == nullptr ||
          !hardware.rm69a10->SetBrightness(
              device_utils::PercentToUint8Value(target, UINT8_MAX)))
        return false;
      *rm69a10_percent = target;
      return true;
    default:
      return false;
  }
}

bool FadeBrightness(const Hardware& hardware, cpp_bus_driver::Pwm* backlight,
    int percent, uint32_t duration_ms, int* rm69a10_percent) {
  if (!hardware.ready) return false;
  const int target = device_utils::ClampPercent(percent);
  if (duration_ms == 0)
    return SetBrightness(hardware, backlight, target, rm69a10_percent);
  using Type = Hardware::Type;
  switch (hardware.type) {
    case Type::kHi8561:
      return backlight != nullptr &&
             backlight->FadeTo(
                 device_utils::ScreenBrightnessPercentToDutyCycle(target, 1000),
                 duration_ms,
                 cpp_bus_driver::Pwm::FadeMode::kWaitForCompletion);
    case Type::kRm69a10: {
      if (hardware.rm69a10 == nullptr) return false;
      const int start = *rm69a10_percent;
      const int delta = std::abs(target - start);
      if (delta == 0) return true;
      const int steps =
          std::max(1, std::min(delta, static_cast<int>(duration_ms / 10)));
      for (int step = 1; step <= steps; ++step) {
        const int value = start + (target - start) * step / steps;
        if (!hardware.rm69a10->SetBrightness(
                device_utils::PercentToUint8Value(value, UINT8_MAX)))
          return false;
        *rm69a10_percent = value;
        vTaskDelay(pdMS_TO_TICKS(std::max<uint32_t>(1, duration_ms / steps)));
      }
      return true;
    }
    default:
      return false;
  }
}
}  // namespace lilygo_box::hal::screen_utils
