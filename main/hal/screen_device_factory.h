/*
 * @Description: None
 * @Author: LILYGO_L
 * @Date: 2026-05-10 13:27:05
 * @LastEditTime: 2026-05-10 13:27:05
 * @License: GPL 3.0
 */
#pragma once

#include <memory>

#include "hal/screen_device.h"

namespace lilygo_box::hal {

// Creates the screen implementation selected by Kconfig.
std::unique_ptr<ScreenDevice> CreateScreenDevice();

}  // namespace lilygo_box::hal
