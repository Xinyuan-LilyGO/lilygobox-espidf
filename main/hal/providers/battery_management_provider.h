/*
 * @Description: 电池管理、充电状态与电源控制接口
 * @Author: LILYGO_L
 * @Date: 2026-05-14 00:20:00
 * @LastEditTime: 2026-05-14 00:20:00
 * @License: GPL 3.0
 */
#pragma once

namespace lilygo_box::hal {

// 电池管理芯片功能支持情况。
struct BatteryManagementCapabilities {
  // 是否支持平均电流和平均功率。
  bool average_measurements = false;
  // 是否支持容量数据。
  bool capacity = false;
  // 是否支持读取剩余充放电时间。
  bool remaining_time = false;
  // 是否支持循环次数。
  bool cycle_count = false;
};

// 电池管理芯片读取到的电池包、容量、电流和温度状态。
struct BatteryManagementStatus {
  // 电池管理功能支持情况。
  BatteryManagementCapabilities capabilities;
  // 电池管理功能是否已经初始化并可读取。
  bool ready = false;
  // 是否检测到电池包。
  bool pack_present = false;
  // 是否处于充电状态；外部电源有效且电池已充满时同样为 true。
  bool charging = false;
  // 电池是否已充满。
  bool full_charged = false;
  // 电池是否已放空。
  bool full_discharged = false;
  // 电池电压，单位为 mV。
  int voltage_mv = 0;
  // 当前电流，单位为 mA。
  int current_ma = 0;
  // 平均电流，单位为 mA。
  int average_current_ma = 0;
  // 平均功率，单位为 mW。
  int average_power_mw = 0;
  // 当前电量百分比。
  int charge_percent = 0;
  // 电池健康度百分比。
  int health_percent = 0;
  // 设计容量，单位为 mAh。
  int design_capacity_mah = 0;
  // 剩余容量，单位为 mAh。
  int remaining_capacity_mah = 0;
  // 满充容量，单位为 mAh。
  int full_charge_capacity_mah = 0;
  // 预计放空剩余时间，单位为分钟。
  int time_to_empty_min = 0;
  // 预计充满剩余时间，单位为分钟。
  int time_to_full_min = 0;
  // 电池循环次数。
  int cycle_count = 0;
  // 电池包温度，单位为摄氏度。
  float pack_temperature_c = 0.0F;
  // 电池管理芯片温度，单位为摄氏度。
  float chip_temperature_c = 0.0F;
};

class BatteryManagementProvider {
 public:
  virtual ~BatteryManagementProvider() = default;

  /**
   * @brief 读取电池管理状态
   * @param status 电池管理状态输出地址
   * @return 读取到有效电池管理状态返回 true，否则返回 false
   */
  virtual bool ReadBatteryManagementStatus(BatteryManagementStatus* status) = 0;

  /**
   * @brief 读取当前有效的电池电量百分比
   * @param percent 电量百分比输出地址，范围为 0 到 100
   * @return 检测到电池且电量读取成功返回 true，否则返回 false
   */
  virtual bool ReadBatteryLevel(int* percent) = 0;
};

}  // namespace lilygo_box::hal
