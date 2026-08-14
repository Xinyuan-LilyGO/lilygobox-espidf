/*
 * @Description: 整机底层驱动初始化抽象接口
 * @Author: LILYGO_L
 * @Date: 2026-05-15 18:00:00
 * @LastEditTime: 2026-05-15 18:00:00
 * @License: GPL 3.0
 */
#pragma once

#include <cstdint>

namespace lilygo_box::hal {

enum class PowerOffAction {
  kFailed,
  kShowChargingScreen,
  kEnterDeepSleep,
  kWaitForPowerCut,
};

// 已关机设备重新得到处理器执行机会时应采取的启动动作。
enum class PowerOffBootAction {
  kContinueStartup,
  kShowChargingScreen,
  kEnterDeepSleep,
  kWaitForPowerCut,
  kFailed,
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
   * @brief 关机充电界面结束后重新进入设备关机状态
   * @return 默认复用完整关机流程
   */
  virtual PowerOffAction RequestPowerOffFromChargingScreen() {
    return RequestPowerOff();
  }

  /**
   * @brief 判断设备是否需要持久记录关机状态以支持关机充电
   * @return 支持关机充电启动策略时返回 true
   */
  virtual bool SupportsPowerOffCharging() const { return false; }

  /**
   * @brief 在完整设备初始化前处理持久关机状态下的启动原因
   * @param power_off_requested 上次是否已完成关机请求持久化
   * @return 本次启动应执行的动作
   */
  virtual PowerOffBootAction ResolvePowerOffBoot(bool power_off_requested) {
    (void)power_off_requested;
    return PowerOffBootAction::kContinueStartup;
  }

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
