/*
 * @Description: None
 * @Author: LILYGO_L
 * @Date: 2026-05-10 13:27:05
 * @LastEditTime: 2026-05-12 22:55:00
 * @License: GPL 3.0
 */
#pragma once

#include <memory>

#include "hal/screen_device.h"

namespace lilygo_box::hal {

/**
 * @brief 创建 Kconfig 选择的屏幕设备实现
 * @return 创建成功返回屏幕设备对象，否则返回 nullptr
 * @Date 2026-05-12 22:55:00
 */
std::unique_ptr<ScreenDevice> CreateScreenDevice();

}  // namespace lilygo_box::hal
