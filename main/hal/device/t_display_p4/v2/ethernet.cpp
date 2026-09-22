/*
 * @Description: T-Display-P4 V2 USB 以太网及共用 Boost 供电实现
 * @Author: LILYGO_L
 * @License: GPL 3.0
 */
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "hal/device/t_display_p4/device.h"

namespace lilygo_box::hal {
bool TDisplayP4Device::SetEthernetEnabled(bool enabled) {
  return usb_ethernet_manager_.SetEnabled(enabled);
}

bool TDisplayP4Device::ReadEthernetStatus(EthernetStatus* status) {
  return usb_ethernet_manager_.ReadStatus(status);
}

bool TDisplayP4Device::SetEthernetPowerEnabled(bool enabled) {
  if (otg_mutex_ == nullptr ||
      xSemaphoreTake(otg_mutex_, pdMS_TO_TICKS(1000)) != pdTRUE) {
    return false;
  }
  auto* axp517 =
      driver_.IsAxp517Ready() ? driver_.chip().axp517.get() : nullptr;
  // VMID -> SYS-5V -> ETH-5V；Type-A IO10 是另一支路，不能跟随网卡开关。
  const bool result =
      axp517 != nullptr && axp517->SetBoostEnable(enabled || otg_enabled_);
  if (result) {
    ethernet_power_enabled_ = enabled;
  }
  xSemaphoreGive(otg_mutex_);
  return result;
}
}  // namespace lilygo_box::hal
