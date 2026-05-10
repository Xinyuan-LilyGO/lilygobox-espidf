/*
 * @Description: None
 * @Author: LILYGO_L
 * @Date: 2026-05-10 13:27:05
 * @LastEditTime: 2026-05-10 13:27:05
 * @License: GPL 3.0
 */
#include "hal/screen_device_factory.h"

#include "sdkconfig.h"

#if defined(CONFIG_LILYGO_BOX_DEVICE_T_DISPLAY_P4)
#include "hal/device/t_display_p4/t_display_p4_device.h"
#endif

namespace lilygo_box::hal {

std::unique_ptr<ScreenDevice> CreateScreenDevice() {
#if defined(CONFIG_LILYGO_BOX_DEVICE_T_DISPLAY_P4)
  return std::make_unique<TDisplayP4Device>();
#else
  return nullptr;
#endif
}

}  // namespace lilygo_box::hal
