/*
 * @Description: IMU 传感器状态与姿态数据接口
 * @Author: LILYGO_L
 * @Date: 2026-05-14 00:20:00
 * @LastEditTime: 2026-05-14 00:20:00
 * @License: GPL 3.0
 */
#pragma once

namespace lilygo_box::hal {

struct ImuStatus {
  bool ready = false;
  float pitch_deg = 0.0F;
  float yaw_deg = 0.0F;
  float roll_deg = 0.0F;
};

class ImuProvider {
 public:
  virtual ~ImuProvider() = default;

  /**
   * @brief 读取 IMU 运动状态
   * @param status IMU 状态输出地址
   * @return 读取到有效 IMU 状态返回 true，否则返回 false
   */
  virtual bool ReadImuStatus(ImuStatus* status) = 0;
};

}  // namespace lilygo_box::hal
