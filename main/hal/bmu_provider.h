/*
 * @Description: None
 * @Author: LILYGO_L
 * @Date: 2026-05-14 00:20:00
 * @LastEditTime: 2026-05-14 00:20:00
 * @License: GPL 3.0
 */
#pragma once

namespace lilygo_box::hal {

struct BmuStatus {
  bool ready = false;
  bool pack_present = false;
  bool charging = false;
  bool discharging = false;
  bool full_charged = false;
  bool full_discharged = false;
  int voltage_mv = 0;
  int current_ma = 0;
  int average_current_ma = 0;
  int average_bmu_mw = 0;
  int charge_percent = 0;
  int health_percent = 0;
  int design_capacity_mah = 0;
  int remaining_capacity_mah = 0;
  int full_charge_capacity_mah = 0;
  int time_to_empty_min = 0;
  int time_to_full_min = 0;
  int cycle_count = 0;
  float pack_temperature_c = 0.0F;
  float gauge_temperature_c = 0.0F;
};

class BmuProvider {
 public:
  virtual ~BmuProvider() = default;

  /**
   * @brief 读取 BMU 电池管理状态
   * @param status BMU 状态输出地址
   * @return 读取到有效 BMU 状态返回 true，否则返回 false
   * @Date 2026-05-14 00:20:00
   */
  virtual bool ReadBmuStatus(BmuStatus* status) = 0;
};

}  // namespace lilygo_box::hal
