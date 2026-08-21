/*
 * @Description: 根据构建配置创建设备 Provider 上下文
 * @Author: LILYGO_L
 * @Date: 2026-05-10 13:27:05
 * @LastEditTime: 2026-07-30 18:00:00
 * @License: GPL 3.0
 */
#include "hal/device_provider_factory.h"

#include "sdkconfig.h"

#if defined(CONFIG_LILYGO_DEVICE_DRIVER_T_DISPLAY_P4_AIR) && \
    defined(CONFIG_LILYGO_DEVICE_DRIVER_T_DISPLAY_P4)
#error \
    "T-Display-P4-Air and T-Display-P4 are different devices; select only one"
#elif defined(CONFIG_LILYGO_DEVICE_DRIVER_T_DISPLAY_P4_AIR) && \
    !defined(CONFIG_SLAVE_IDF_TARGET_ESP32C5)
#error "T-Display-P4-Air requires the ESP32-C5 ESP-Hosted slave target"
#elif defined(CONFIG_LILYGO_DEVICE_DRIVER_T_DISPLAY_P4) && \
    !defined(CONFIG_SLAVE_IDF_TARGET_ESP32C6)
#error "T-Display-P4 requires the ESP32-C6 ESP-Hosted slave target"
#endif

#if defined(CONFIG_LILYGO_DEVICE_DRIVER_T_DISPLAY_P4_AIR)
#include "hal/device/t_display_p4_air/t_display_p4_air_device.h"
#elif defined(CONFIG_LILYGO_DEVICE_DRIVER_T_DISPLAY_P4)
#include "hal/device/t_display_p4/t_display_p4_device.h"
#endif

namespace lilygo_box::hal {

DeviceProviderContext CreateDeviceProviderContext() {
#if defined(CONFIG_LILYGO_DEVICE_DRIVER_T_DISPLAY_P4_AIR)
  auto device = std::make_unique<TDisplayP4AirDevice>();
#elif defined(CONFIG_LILYGO_DEVICE_DRIVER_T_DISPLAY_P4)
  auto device = std::make_unique<TDisplayP4Device>();
#else
  return DeviceProviderContext();
#endif

#if defined(CONFIG_LILYGO_DEVICE_DRIVER_T_DISPLAY_P4) || \
    defined(CONFIG_LILYGO_DEVICE_DRIVER_T_DISPLAY_P4_AIR)
  DeviceProviderContext context;
  context.device = device.get();
  context.diagnostics = device.get();
  context.device_info = device.get();
  context.gps = device.get();
  context.imu = device.get();
  context.audio = device.get();
  context.haptic = device.get();
  context.battery_management = device.get();
  context.camera = device.get();
#if defined(CONFIG_LILYGO_DEVICE_DRIVER_T_DISPLAY_P4)
  context.capabilities.supports_keyboard_expansion = true;
  context.keyboard_expansion = device.get();
  context.nfc = device.get();
  context.rtc = device.get();
#endif
  context.radio = device.get();
#if defined(CONFIG_LILYGO_DEVICE_DRIVER_T_DISPLAY_P4_AIR)
  context.otg = device.get();
  context.nfc = device.get();
  context.infrared = device.get();
  context.cellular = device.get();
#endif
#if defined(CONFIG_LILYGO_DEVICE_DRIVER_T_DISPLAY_P4)
  context.ethernet = device.get();
#endif
  context.wifi = device.get();
  context.storage = device.get();
  context.screen = std::move(device);
  return context;
#endif
}

}  // namespace lilygo_box::hal
