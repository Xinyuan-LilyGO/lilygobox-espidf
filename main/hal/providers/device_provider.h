/*
 * @Description: 整机底层驱动初始化抽象接口
 * @Author: LILYGO_L
 * @Date: 2026-05-15 18:00:00
 * @LastEditTime: 2026-05-15 18:00:00
 * @License: GPL 3.0
 */
#pragma once

namespace lilygo_box::hal {

enum class PowerOffAction {
  kFailed,
  kEnterDeepSleep,
  kWaitForPowerCut,
};

class DeviceProvider {
 public:
  virtual ~DeviceProvider() = default;

  /**
   * @brief 初始化整机底层驱动到屏幕可以使用的状态
   * @return 初始化成功返回 true，否则返回 false
   */
  virtual bool InitDevice() = 0;

  /**
   * @brief 完成设备关机准备并返回最终关机动作。
   * @return 设备对应的最终关机动作，失败时返回 kFailed。
   */
  virtual PowerOffAction RequestPowerOff() = 0;

  /**
   * @brief 读取设备物理电源键的稳定前原始状态
   * @param pressed 输出按键是否处于按下状态
   * @return 设备支持电源键并成功读取时返回 true
   */
  virtual bool ReadPowerButtonPressed(bool* pressed) {
    if (pressed != nullptr) {
      *pressed = false;
    }
    return false;
  }
};

}  // namespace lilygo_box::hal
