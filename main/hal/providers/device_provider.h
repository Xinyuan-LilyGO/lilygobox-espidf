/*
 * @Description: None
 * @Author: LILYGO_L
 * @Date: 2026-05-15 18:00:00
 * @LastEditTime: 2026-05-15 18:00:00
 * @License: GPL 3.0
 */
#pragma once

namespace lilygo_box::hal {

class DeviceProvider {
 public:
  virtual ~DeviceProvider() = default;

  /**
   * @brief 初始化整机底层驱动到屏幕可以使用的状态
   * @return 初始化成功返回 true，否则返回 false
   */
  virtual bool InitDevice() = 0;
};

}  // namespace lilygo_box::hal
