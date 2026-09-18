/*
 * @Description: T-Display-P4 V2 电源、实体按键与关机充电实现
 * @Author: LILYGO_L
 * @Date: 2026-08-28 00:00:00
 * @LastEditTime: 2026-09-02 17:53:38
 * @License: GPL 3.0
 */
#include <cstdint>

#include "base/logger.h"
#include "esp_attr.h"
#include "esp_err.h"
#include "esp_sleep.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "hal/device/t_display_p4/device.h"

namespace lilygo_box::hal {
namespace gpio = lilygo_device_driver::t_display_p4::gpio;
namespace {

// 关机充电状态每 5 秒短暂唤醒一次，仅用于检查 USB 是否已经拔出。
constexpr uint64_t kPowerOffMonitorWakeIntervalUs = 5ULL * 1000 * 1000;
constexpr uint32_t kPowerOffStartupHoldMs = 2 * 1000;
constexpr uint32_t kPowerOffButtonPollMs = 20;
constexpr uint32_t kPowerOffButtonReleaseTimeoutMs = 3000;
constexpr uint32_t kPowerOffRtcMagic = 0x504F4646;  // "POFF"
constexpr uint32_t kOtgMutexTimeoutMs = 50;

// RTC 内存只跨越深度睡眠保留，用于避免每次 5 秒巡检都点亮充电界面。
RTC_DATA_ATTR uint32_t g_power_off_rtc_magic = 0;
RTC_DATA_ATTR bool g_power_off_charging_screen_pending = false;

}  // namespace

bool TDisplayP4Device::SetOtgPowerEnabled(bool enabled) {
  if (otg_mutex_ == nullptr ||
      xSemaphoreTake(otg_mutex_, pdMS_TO_TICKS(kOtgMutexTimeoutMs)) != pdTRUE) {
    return false;
  }
  bool result = false;
  if (!enabled) {
    result = DisableOtgPowerLocked();
  } else if (driver_.IsAxp517Ready() && driver_.IsXl9535Ready() &&
             driver_.chip().axp517 != nullptr) {
    // IO10 和 Boost 维持 Type-A 供电；Type-C 只单独切换 RBFET。
    result = driver_.chip().axp517->SetVbusDetectEnable(true) &&
             driver_.SetUsbHostPowerEnabled(true);
    if (result) {
      otg_enabled_ = true;
      result = UpdateOtgPowerStateLocked();
    }
    if (!result && !DisableOtgPowerLocked()) {
      LogMessage(LogLevel::kError, __FILE__, __LINE__,
          "Restore USB power after OTG enable failure failed\n");
    }
  }
  xSemaphoreGive(otg_mutex_);
  return result;
}

bool TDisplayP4Device::UpdateOtgPowerState() {
  if (otg_mutex_ == nullptr ||
      xSemaphoreTake(otg_mutex_, pdMS_TO_TICKS(kOtgMutexTimeoutMs)) != pdTRUE) {
    return false;
  }
  const bool result = driver_.IsAxp517Ready() && driver_.IsXl9535Ready() &&
                      driver_.chip().axp517 != nullptr &&
                      UpdateOtgPowerStateLocked();
  xSemaphoreGive(otg_mutex_);
  return result;
}

bool TDisplayP4Device::ReadExternalPowerPresent(bool* present) {
  if (present == nullptr || otg_mutex_ == nullptr ||
      xSemaphoreTake(otg_mutex_, pdMS_TO_TICKS(kOtgMutexTimeoutMs)) != pdTRUE) {
    return false;
  }
  auto* axp517 =
      driver_.IsAxp517Ready() ? driver_.chip().axp517.get() : nullptr;
  cpp_bus_driver::Axp517::PdConnectionStatus connection_status;
  cpp_bus_driver::Axp517::ChipStatus0 chip_status;
  const bool result = axp517 != nullptr &&
                      axp517->GetPdConnectionStatus(connection_status) &&
                      axp517->GetChipStatus0(chip_status);
  if (result) {
    // 排除 Type-C 自身反向输出的 VBUS，Type-A 是否开启不影响此判断。
    *present = connection_status.sink_power_attached ||
               (!type_c_output_enabled_ && chip_status.vbus_good_indication);
  } else if (axp517 != nullptr) {
    // 应用策略在读取失败时会跳过更新，因此在这里也撤销 Type-C 输出。
    SetTypeCOutputEnabledLocked(false);
  }
  xSemaphoreGive(otg_mutex_);
  return result;
}

bool TDisplayP4Device::SetTypeCOutputEnabledLocked(bool enabled) {
  auto* axp517 = driver_.chip().axp517.get();
  if (axp517 == nullptr || (enabled && !otg_enabled_)) {
    return false;
  }
  if (!axp517->SetForceRbfetEnable(enabled)) {
    return false;
  }
  if (type_c_output_enabled_ != enabled) {
    LogMessage(LogLevel::kInfo, __FILE__, __LINE__,
        "Type-C reverse-power output %s\n",
        enabled ? "enabled" : "disabled");
  }
  type_c_output_enabled_ = enabled;
  return true;
}

bool TDisplayP4Device::DisableOtgPowerLocked() {
  otg_enabled_ = false;
  bool result = true;
  if (driver_.IsAxp517Ready() && driver_.chip().axp517 != nullptr) {
    // 先断开 Type-C 输出，再关闭 Type-A 和共用 Boost。
    result = SetTypeCOutputEnabledLocked(false);
    const bool role_reset = driver_.chip().axp517->SetPdRole(false, false);
    result &= role_reset;
    if (role_reset) {
      type_c_source_role_enabled_ = false;
    }
  }
  result &= driver_.SetUsbHostPowerEnabled(false);
  return result;
}

bool TDisplayP4Device::UpdateOtgPowerStateLocked() {
  if (!otg_enabled_) {
    return DisableOtgPowerLocked();
  }
  auto* axp517 = driver_.chip().axp517.get();
  cpp_bus_driver::Axp517::PdConnectionStatus connection_status;
  cpp_bus_driver::Axp517::ChipStatus0 chip_status;
  if (!axp517->GetPdConnectionStatus(connection_status) ||
      !axp517->GetChipStatus0(chip_status)) {
    // 检测失败时撤销 Type-C 输出，保留已开启的 Type-A 电源。
    SetTypeCOutputEnabledLocked(false);
    return false;
  }
  const bool external_power_present =
      connection_status.sink_power_attached ||
      (!type_c_output_enabled_ && chip_status.vbus_good_indication);
  if (external_power_present) {
    bool result = SetTypeCOutputEnabledLocked(false);
    const bool role_reset = axp517->SetPdRole(false, false);
    result &= role_reset;
    if (role_reset) {
      type_c_source_role_enabled_ = false;
    }
    return result;
  }
  if (!type_c_source_role_enabled_) {
    if (!SetTypeCOutputEnabledLocked(false) ||
        !axp517->SetPdRole(true, true)) {
      return false;
    }
    type_c_source_role_enabled_ = true;
    // 角色刚切换，等待下次轮询取得新的连接状态。
    return true;
  }
  if (connection_status.looking_for_connection) {
    return SetTypeCOutputEnabledLocked(false);
  }
  if (connection_status.source_device_attached) {
    return SetTypeCOutputEnabledLocked(true);
  }
  if (!SetTypeCOutputEnabledLocked(false)) {
    return false;
  }
  return axp517->SetPdRole(true, true);
}

bool TDisplayP4Device::InitializePowerButton() {
  if (power_button_initialized_) {
    return true;
  }
  if (tool_ == nullptr) {
    return false;
  }

  power_button_initialized_ = tool_->SetGpioMode(gpio::button::kPower,
      cpp_bus_driver::PlatformHal::GpioMode::kInput,
      cpp_bus_driver::PlatformHal::GpioStatus::kPullup);
  return power_button_initialized_;
}

bool TDisplayP4Device::InitializeVolumeButtons() {
  if (volume_buttons_initialized_) {
    return true;
  }
  if (tool_ == nullptr) {
    return false;
  }

  volume_buttons_initialized_ =
      tool_->SetGpioMode(gpio::button::kEsp32p4Boot,
          cpp_bus_driver::PlatformHal::GpioMode::kInput,
          cpp_bus_driver::PlatformHal::GpioStatus::kPullup) &&
      tool_->SetGpioMode(gpio::button::kKey,
          cpp_bus_driver::PlatformHal::GpioMode::kInput,
          cpp_bus_driver::PlatformHal::GpioStatus::kPullup);
  return volume_buttons_initialized_;
}

bool TDisplayP4Device::IsPowerButtonHeldForStartup() {
  if (!InitializePowerButton()) {
    return false;
  }

  bool pressed = false;
  if (!ReadPowerButtonPressed(&pressed) || !pressed) {
    return false;
  }

  uint32_t held_ms = 0;
  while (held_ms < kPowerOffStartupHoldMs) {
    vTaskDelay(pdMS_TO_TICKS(kPowerOffButtonPollMs));
    held_ms += kPowerOffButtonPollMs;
    if (!ReadPowerButtonPressed(&pressed) || !pressed) {
      return false;
    }
  }
  return true;
}

void TDisplayP4Device::WaitForPowerButtonRelease() {
  if (!InitializePowerButton()) {
    return;
  }

  bool pressed = false;
  for (uint32_t waited_ms = 0; waited_ms < kPowerOffButtonReleaseTimeoutMs;
      waited_ms += kPowerOffButtonPollMs) {
    if (!ReadPowerButtonPressed(&pressed) || !pressed) {
      return;
    }
    vTaskDelay(pdMS_TO_TICKS(kPowerOffButtonPollMs));
  }
  LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
      "Power button remained pressed before power-off deep sleep\n");
}

bool TDisplayP4Device::ConfigurePowerOffWakeSources() {
  const esp_err_t disable_result =
      esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
  if (disable_result != ESP_OK) {
    LogMessage(LogLevel::kError, __FILE__, __LINE__,
        "Disable previous sleep wake sources failed: %s (%#X)\n",
        esp_err_to_name(disable_result), static_cast<unsigned>(disable_result));
    return false;
  }

  const esp_err_t timer_result =
      esp_sleep_enable_timer_wakeup(kPowerOffMonitorWakeIntervalUs);
  if (timer_result != ESP_OK) {
    LogMessage(LogLevel::kError, __FILE__, __LINE__,
        "Enable power-off timer wakeup failed: %s (%#X)\n",
        esp_err_to_name(timer_result), static_cast<unsigned>(timer_result));
    return false;
  }

  const uint64_t power_button_mask =
      uint64_t{1} << static_cast<unsigned>(gpio::button::kPower);
  const esp_err_t gpio_result = esp_deep_sleep_enable_gpio_wakeup(
      power_button_mask, ESP_GPIO_WAKEUP_GPIO_LOW);
  if (gpio_result != ESP_OK) {
    LogMessage(LogLevel::kError, __FILE__, __LINE__,
        "Enable power button deep-sleep wakeup failed: %s (%#X)\n",
        esp_err_to_name(gpio_result), static_cast<unsigned>(gpio_result));
    return false;
  }
  return true;
}

PowerOffBootAction TDisplayP4Device::PreparePowerOffDeepSleep() {
  WaitForPowerButtonRelease();
  if (!ConfigurePowerOffWakeSources()) {
    return PowerOffBootAction::kFailed;
  }
  if (!driver_.PrepareMinimalDriversForPowerOff()) {
    LogMessage(LogLevel::kError, __FILE__, __LINE__,
        "Prepare minimal power management path for deep sleep failed\n");
  }
  return PowerOffBootAction::kEnterDeepSleep;
}

PowerOffBootAction TDisplayP4Device::PreparePowerOffShippingMode() {
  WaitForPowerButtonRelease();
  if (!ConfigurePowerOffWakeSources()) {
    return PowerOffBootAction::kFailed;
  }
  const bool shipping_mode_enabled =
      driver_.IsAxp517Ready() && driver_.chip().axp517 != nullptr &&
      driver_.chip().axp517->SetShippingModeEnable(true);
  const bool sleep_prepared = driver_.PrepareMinimalDriversForPowerOff();
  if (!shipping_mode_enabled) {
    LogMessage(LogLevel::kError, __FILE__, __LINE__,
        "Enter AXP517 shipping mode failed; retrying after timer wakeup\n");
  }
  if (!sleep_prepared) {
    LogMessage(LogLevel::kError, __FILE__, __LINE__,
        "Prepare AXP517 shipping-mode path failed\n");
  }
  return PowerOffBootAction::kEnterDeepSleep;
}

bool TDisplayP4Device::ReadPowerButtonPressed(bool* pressed) {
  if (pressed == nullptr || !power_button_initialized_ || tool_ == nullptr) {
    return false;
  }

  // 电源键使用上拉输入，按下时把 GPIO 拉低。
  *pressed = !tool_->GpioRead(gpio::button::kPower);
  return true;
}

bool TDisplayP4Device::ReadVolumeUpButtonPressed(bool* pressed) {
  if (pressed == nullptr || !volume_buttons_initialized_ || tool_ == nullptr) {
    return false;
  }

  // BOOT 音量加按键使用上拉输入，按下时把 GPIO 拉低。
  *pressed = !tool_->GpioRead(gpio::button::kEsp32p4Boot);
  return true;
}

bool TDisplayP4Device::ReadVolumeDownButtonPressed(bool* pressed) {
  if (pressed == nullptr || !volume_buttons_initialized_ || tool_ == nullptr) {
    return false;
  }

  // KEY1 音量减按键使用上拉输入，按下时把 GPIO 拉低。
  *pressed = !tool_->GpioRead(gpio::button::kKey);
  return true;
}

PowerOffBootAction TDisplayP4Device::ResolvePowerOffBoot(
    bool power_off_requested) {
  if (!driver_.InitMinimal() || !InitializePowerButton() ||
      !driver_.IsAxp517Ready() || driver_.chip().axp517 == nullptr) {
    LogMessage(LogLevel::kError, __FILE__, __LINE__,
        "Initialize AXP517 power-off boot path failed\n");
    return PowerOffBootAction::kFailed;
  }

  auto& axp517 = *driver_.chip().axp517;
  cpp_bus_driver::Axp517::ChipStatus0 chip_status0;
  if (!axp517.GetChipStatus0(chip_status0)) {
    LogMessage(LogLevel::kError, __FILE__, __LINE__,
        "Read AXP517 VBUS status during power-off boot failed\n");
    driver_.PrepareMinimalDriversForPowerOff();
    return PowerOffBootAction::kFailed;
  }

  cpp_bus_driver::Axp517::IrqStatus0 irq_status0;
  cpp_bus_driver::Axp517::IrqStatus1 irq_status1;
  cpp_bus_driver::Axp517::IrqStatus2 irq_status2;
  cpp_bus_driver::Axp517::IrqStatus3 irq_status3;
  const bool irq_ready =
      axp517.GetIrqStatus(irq_status0, irq_status1, irq_status2, irq_status3);
  const bool axp_long_press = irq_ready && irq_status1.pwr_on_long_press_flag;
  const bool axp_short_press = irq_ready && irq_status1.pwr_on_short_press_flag;
  const bool vbus_inserted = irq_ready && irq_status1.vbus_insert_flag;
  if (!axp517.ClearAllIrq()) {
    LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
        "Clear AXP517 power-off boot IRQ flags failed\n");
  }

  const bool external_power_present = chip_status0.vbus_good_indication;
  const esp_sleep_wakeup_cause_t wakeup_cause = esp_sleep_get_wakeup_cause();
  const uint64_t power_button_mask =
      uint64_t{1} << static_cast<unsigned>(gpio::button::kPower);
  const bool power_button_wakeup =
      wakeup_cause == ESP_SLEEP_WAKEUP_GPIO &&
      (esp_sleep_get_gpio_wakeup_status() & power_button_mask) != 0;
  const esp_reset_reason_t reset_reason = esp_reset_reason();
  const bool usb_power_on = !power_off_requested && external_power_present &&
                            reset_reason == ESP_RST_POWERON && vbus_inserted;

  LogMessage(LogLevel::kInfo, __FILE__, __LINE__,
      "Resolve P4 V2 power-off boot: requested=%d, reset=%d, wakeup=%d, "
      "vbus=%d, button=%d, usb_insert=%d\n",
      power_off_requested ? 1 : 0, static_cast<int>(reset_reason),
      static_cast<int>(wakeup_cause), external_power_present ? 1 : 0,
      power_button_wakeup ? 1 : 0, vbus_inserted ? 1 : 0);

  if (!power_off_requested && !usb_power_on) {
    g_power_off_rtc_magic = 0;
    g_power_off_charging_screen_pending = false;
    return PowerOffBootAction::kContinueStartup;
  }

  // 定时器与按键可能同时触发，开机长按应在所有唤醒原因之前取得优先级。
  if (axp_long_press || IsPowerButtonHeldForStartup()) {
    g_power_off_rtc_magic = 0;
    g_power_off_charging_screen_pending = false;
    LogMessage(LogLevel::kInfo, __FILE__, __LINE__,
        "Long power-button press accepted; continuing normal startup\n");
    return PowerOffBootAction::kContinueStartup;
  }

  if (!external_power_present) {
    g_power_off_rtc_magic = kPowerOffRtcMagic;
    g_power_off_charging_screen_pending = true;
    return PreparePowerOffShippingMode();
  }

  if (usb_power_on || power_button_wakeup || axp_short_press) {
    g_power_off_rtc_magic = kPowerOffRtcMagic;
    g_power_off_charging_screen_pending = false;
    return PowerOffBootAction::kShowChargingScreen;
  }

  if (wakeup_cause == ESP_SLEEP_WAKEUP_TIMER) {
    if (g_power_off_rtc_magic == kPowerOffRtcMagic &&
        g_power_off_charging_screen_pending) {
      g_power_off_charging_screen_pending = false;
      return PowerOffBootAction::kShowChargingScreen;
    }
    return PreparePowerOffDeepSleep();
  }

  g_power_off_rtc_magic = kPowerOffRtcMagic;
  g_power_off_charging_screen_pending = false;
  return PowerOffBootAction::kShowChargingScreen;
}

PowerOffAction TDisplayP4Device::RequestPowerOff() {
  return RequestPowerOffInternal(true);
}

PowerOffAction TDisplayP4Device::RequestPowerOffFromChargingScreen() {
  return RequestPowerOffInternal(false);
}

PowerOffAction TDisplayP4Device::RequestPowerOffInternal(
    bool prepare_device_services) {
  if (!driver_.IsAxp517Ready() || driver_.chip().axp517 == nullptr) {
    LogMessage(LogLevel::kError, __FILE__, __LINE__,
        "Power off through AXP517 failed: device unavailable\n");
    return PowerOffAction::kFailed;
  }

  auto& axp517 = *driver_.chip().axp517;
  cpp_bus_driver::Axp517::ChipStatus0 chip_status0;
  if (!axp517.GetChipStatus0(chip_status0)) {
    LogMessage(LogLevel::kError, __FILE__, __LINE__,
        "Read AXP517 VBUS status before power off failed\n");
    return PowerOffAction::kFailed;
  }
  bool external_power_present = chip_status0.vbus_good_indication;

  // 先准备两种唤醒源，避免 USB 在外设关闭期间插入时落入运输模式等待死区。
  WaitForPowerButtonRelease();
  if (!ConfigurePowerOffWakeSources()) {
    return PowerOffAction::kFailed;
  }
  if (!axp517.ClearAllIrq()) {
    LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
        "Clear AXP517 IRQ flags before power off failed\n");
  }

  if (prepare_device_services && !PrepareForPowerOff()) {
    LogMessage(LogLevel::kError, __FILE__, __LINE__,
        "Prepare P4 V2 device for power off failed\n");
    // 不再访问可能仍由应用任务占用的硬件，定时唤醒后重新处理关机状态。
    g_power_off_rtc_magic = kPowerOffRtcMagic;
    g_power_off_charging_screen_pending = true;
    return PowerOffAction::kEnterDeepSleep;
  }

  cpp_bus_driver::Axp517::ChipStatus0 pre_hardware_shutdown_status0;
  if (axp517.GetChipStatus0(pre_hardware_shutdown_status0)) {
    external_power_present = pre_hardware_shutdown_status0.vbus_good_indication;
  } else {
    LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
        "Re-read AXP517 VBUS status before hardware shutdown failed; "
        "using previous state\n");
  }

  if (prepare_device_services && external_power_present) {
    g_power_off_rtc_magic = kPowerOffRtcMagic;
    g_power_off_charging_screen_pending = false;
    return PowerOffAction::kShowChargingScreen;
  }

  if (!driver_.PrepareDriversForPowerOff()) {
    LogMessage(LogLevel::kError, __FILE__, __LINE__,
        "Prepare P4 V2 board hardware for power off failed\n");
    // 不再访问可能仍由初始化任务占用的硬件，定时唤醒后重新处理关机状态。
    g_power_off_rtc_magic = kPowerOffRtcMagic;
    g_power_off_charging_screen_pending = true;
    return PowerOffAction::kEnterDeepSleep;
  }

  // V2 的完整关机准备会释放电源管理总线，重新建立最小路径后才能读取
  // VBUS 和请求运输模式。此时后台外设任务已经退出，不会再次启动。
  if (!driver_.InitMinimal() || !driver_.IsAxp517Ready()) {
    LogMessage(LogLevel::kError, __FILE__, __LINE__,
        "Restore V2 power management path after hardware shutdown failed\n");
    g_power_off_rtc_magic = kPowerOffRtcMagic;
    g_power_off_charging_screen_pending = true;
    return PowerOffAction::kEnterDeepSleep;
  }

  cpp_bus_driver::Axp517::ChipStatus0 final_chip_status0;
  if (axp517.GetChipStatus0(final_chip_status0)) {
    external_power_present = final_chip_status0.vbus_good_indication;
  } else {
    LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
        "Re-read AXP517 VBUS status after hardware shutdown failed; "
        "using previous state\n");
  }

  if (external_power_present) {
    if (g_power_off_rtc_magic != kPowerOffRtcMagic) {
      g_power_off_rtc_magic = kPowerOffRtcMagic;
      g_power_off_charging_screen_pending = true;
    }
    LogMessage(LogLevel::kInfo, __FILE__, __LINE__,
        "External power present; entering power-off charging deep sleep\n");
    if (!driver_.PrepareMinimalDriversForPowerOff()) {
      LogMessage(LogLevel::kError, __FILE__, __LINE__,
          "Prepare power management path for deep sleep failed\n");
    }
    return PowerOffAction::kEnterDeepSleep;
  }

  // 若运输模式执行瞬间 USB 插入导致未断电，下次定时唤醒应显示充电页。
  g_power_off_rtc_magic = kPowerOffRtcMagic;
  g_power_off_charging_screen_pending = true;
  LogMessage(LogLevel::kInfo, __FILE__, __LINE__,
      "Battery-only power off; entering AXP517 shipping mode\n");
  const bool shipping_mode_enabled = axp517.SetShippingModeEnable(true);
  const bool sleep_prepared = driver_.PrepareMinimalDriversForPowerOff();
  if (!shipping_mode_enabled) {
    LogMessage(LogLevel::kError, __FILE__, __LINE__,
        "Enter AXP517 shipping mode failed; retrying after timer wakeup\n");
  }
  if (!sleep_prepared) {
    LogMessage(LogLevel::kError, __FILE__, __LINE__,
        "Prepare power management path after shipping-mode request failed\n");
  }
  // 正常情况下 BATFET 会立即断开；深度睡眠是 USB 同时插入时的安全后备。
  return PowerOffAction::kEnterDeepSleep;
}

}  // namespace lilygo_box::hal
