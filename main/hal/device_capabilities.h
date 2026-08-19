/*
 * @Description: 设备可选功能能力描述
 * @Author: LILYGO_L
 * @Date: 2026-08-19 00:00:00
 * @LastEditTime: 2026-08-19 00:00:00
 * @License: GPL 3.0
 */
#pragma once

namespace lilygo_box::hal {

// 描述当前板级实现提供的可选能力，避免上层 UI 判断具体设备型号。
struct DeviceCapabilities {
  // 是否支持键盘扩展。
  bool supports_keyboard_expansion = false;
};

}  // namespace lilygo_box::hal
