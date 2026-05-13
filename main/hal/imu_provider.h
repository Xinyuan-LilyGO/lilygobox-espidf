/*
 * @Description: None
 * @Author: LILYGO_L
 * @Date: 2026-05-14 00:20:00
 * @LastEditTime: 2026-05-14 00:20:00
 * @License: GPL 3.0
 */
#pragma once

namespace lilygo_box::hal {

struct ImuStatus {
  bool ready = false;
  float acceleration_x_g = 0.0F;
  float acceleration_y_g = 0.0F;
  float acceleration_z_g = 0.0F;
};

class ImuProvider {
 public:
  virtual ~ImuProvider() = default;

  /**
   * @brief 读取 IMU 运动状态
   * @param status IMU 状态输出地址
   * @return 读取到有效 IMU 状态返回 true，否则返回 false
   * @Date 2026-05-14 00:20:00
   */
  virtual bool ReadImuStatus(ImuStatus* status) = 0;
};

}  // namespace lilygo_box::hal
