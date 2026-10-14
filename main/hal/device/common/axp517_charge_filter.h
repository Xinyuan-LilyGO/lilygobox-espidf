/*
 * @Description: 使用电流死区稳定 AXP517 充电状态，过滤低电流波动。
 * @Author: LILYGO_L
 * @Date: 2026-10-14 00:00:00
 * @LastEditTime: 2026-10-14 00:00:00
 * @License: GPL 3.0
 */
#pragma once

#include <mutex>

namespace lilygo_box::hal {

inline constexpr int kAxp517CurrentDeadZoneMa = 30;

// 每个设备保留独立状态；多任务读取时串行更新滤波状态。
class Axp517ChargeFilter {
 public:
  enum class State { kIdle, kCharging, kFull };

  /**
   * @brief 根据电源、芯片状态和净电流更新稳定充电状态
   */
  State Update(bool pack_present, bool external_power, bool chip_charging,
      bool chip_full, float current_ma) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!pack_present || !external_power) {
      initialized_ = false;
      state_ = State::kIdle;
      return state_;
    }
    // 芯片确认充电完成优先，不能仅凭显示电量为 100% 判定。
    if (chip_full) {
      initialized_ = true;
      state_ = State::kFull;
      return state_;
    }
    if (!initialized_) {
      initialized_ = true;
      state_ = chip_charging && current_ma >= -kAxp517CurrentDeadZoneMa
                   ? State::kCharging
                   : State::kIdle;
      return state_;
    }
    // 死区内保留原状态，超出死区立即更新。
    if (current_ma >= -kAxp517CurrentDeadZoneMa &&
        current_ma <= kAxp517CurrentDeadZoneMa) {
      return state_;
    }
    state_ = chip_charging && current_ma > kAxp517CurrentDeadZoneMa
                 ? State::kCharging
                 : State::kIdle;
    return state_;
  }

 private:
  std::mutex mutex_;
  bool initialized_ = false;
  State state_ = State::kIdle;
};

}  // namespace lilygo_box::hal
