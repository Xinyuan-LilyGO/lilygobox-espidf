/*
 * @Description: 指南针九轴姿态融合，使用四元数积分与重力/磁北修正
 * @Author: LILYGO_L
 * @Date: 2026-09-29 00:00:00
 * @LastEditTime: 2026-09-29 00:00:00
 * @License: GPL 3.0
 */
#pragma once

#include <cstdint>

#include "hal/providers/imu_provider.h"

namespace lilygo_box::app {

class CompassFusion final {
 public:
  /**
   * @brief 清除积分状态，传感器重启或校准参数改变后调用
   */
  void Reset();

  /**
   * @brief 融合三轴角速度、加速度及已校准的磁场
   * @param sample 包含单调时间戳和统一坐标系原始运动数据的采样
   * @param magnetic_field 新鲜的已校准三轴磁场，暂无新磁场时传入 nullptr
   * @return 成功更新姿态返回 true，时间戳或运动数据无效时返回 false
   */
  bool Update(const hal::ImuStatus& sample, const float* magnetic_field);

  /**
   * @brief 读取融合后的俯仰角、横滚角及偏航角
   * @param pitch 俯仰角输出，单位度
   * @param roll 横滚角输出，单位度
   * @param yaw 偏航角输出，单位度，由 HAL 换算为板级磁北方位角
   */
  void ReadAngles(float& pitch, float& roll, float& yaw) const;

  /**
   * @brief 判断最近磁北修正是否仍有效，避免长期无磁场时显示漂移方位
   * @param now_us 当前单调时间，单位 us
   * @return 已取得磁北参考且最近 1.5 秒有有效磁场时返回 true
   */
  bool HeadingReady(int64_t now_us) const;

 private:
  float q_[4] = {1.0F, 0.0F, 0.0F, 0.0F};
  bool initialized_ = false;
  int64_t last_sample_us_ = 0;
  int64_t last_magnetic_us_ = 0;
};

}  // namespace lilygo_box::app
