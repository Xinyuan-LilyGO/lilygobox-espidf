/*
 * @Description: IMU 传感器状态与姿态数据接口
 * @Author: LILYGO_L
 * @Date: 2026-05-14 00:20:00
 * @LastEditTime: 2026-05-14 00:20:00
 * @License: GPL 3.0
 */
#pragma once

#include <cstdint>

namespace lilygo_box::hal {

struct ImuStatus {
  // 单调采样时间，单位 us；用于应用层按实际间隔积分角速度。
  int64_t sample_time_us = 0;
  float acceleration_g[3] = {};
  // 与 acceleration_g 使用同一坐标基的三轴角速度，单位 deg/s。
  bool angular_velocity_ready = false;
  float angular_velocity_dps[3] = {};
  bool ready = false;
  float pitch_deg = 0.0F;
  float yaw_deg = 0.0F;
  float roll_deg = 0.0F;
  // 未校准三轴磁场，单位 uT，与 pitch/roll 使用同一传感器坐标系。
  // 供应用层指南针校准使用，不改变现有姿态数据的语义。
  bool magnetic_field_ready = false;
  float magnetic_field_ut[3] = {};
};

class ImuProvider {
 public:
  virtual ~ImuProvider() = default;

  virtual bool SetImuEnabled(bool enabled) = 0;

  /**
   * @brief 读取 IMU 运动状态
   * @param status IMU 状态输出地址
   * @return 读取到有效 IMU 状态返回 true，否则返回 false
   */
  virtual bool ReadImuStatus(ImuStatus* status) = 0;

  /**
   * @brief 将融合偏航角换算为当前板子的磁北方位角
   * @param yaw_deg 融合输出的偏航角，单位度
   * @return 按板级方向约定换算的方位角，范围 [0, 360)
   */
  virtual float ConvertImuHeading(float yaw_deg) const = 0;
};

}  // namespace lilygo_box::hal
