/*
 * @Description: 根据构建配置创建设备 Provider 上下文
 * @Author: LILYGO_L
 * @Date: 2026-05-10 13:27:05
 * @LastEditTime: 2026-05-14 00:45:00
 * @License: GPL 3.0
 */
#include "hal/device_provider_factory.h"

#include "sdkconfig.h"

#if defined(CONFIG_LILYGO_DEVICE_DRIVER_T_DISPLAY_P4)
#include "hal/device/t_display_p4/t_display_p4_device.h"
#endif

namespace lilygo_box::hal {

DeviceProviderContext CreateDeviceProviderContext() {
#if defined(CONFIG_LILYGO_DEVICE_DRIVER_T_DISPLAY_P4)
  auto device = std::make_unique<TDisplayP4Device>();
  DeviceProviderContext context;
  context.device = device.get();
  context.diagnostics = device.get();
  context.device_info = device.get();
  context.gps = device.get();
  context.imu = device.get();
  context.audio = device.get();
  context.haptic = device.get();
  context.bmu = device.get();
  context.camera = device.get();
  context.rtc = device.get();
  context.rf = device.get();
  context.ethernet = device.get();
  context.wifi = device.get();
  context.storage = device.get();
  context.screen = std::move(device);
  return context;
#else
  return DeviceProviderContext();
#endif
}

}  // namespace lilygo_box::hal
