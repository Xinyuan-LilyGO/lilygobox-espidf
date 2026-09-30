/*
 * @Description: 指南针四元数姿态积分及加速度/磁场互补修正
 * @Author: LILYGO_L
 * @Date: 2026-09-29 00:00:00
 * @LastEditTime: 2026-09-29 00:00:00
 * @License: GPL 3.0
 */
#include "app/compass_fusion.h"

#include <algorithm>
#include <cmath>

namespace lilygo_box::app {
namespace {

constexpr float kRadians = 0.017453292519943295F;
constexpr float kGravityGain = 2.5F;
constexpr float kMagneticTimeConstant = 0.6F;
constexpr int64_t kMaximumSampleGapUs = 250000;

/**
 * @brief 原地归一化四元数，防止长期积分数值漂移
 * @param q 长度为四的四元数数组，顺序 w/x/y/z
 */
void NormalizeQuaternion(float* q) {
  float squared = 0.0F;
  for (int i = 0; i < 4; ++i) {
    squared += q[i] * q[i];
  }
  const float inverse = 1.0F / std::sqrt(squared);
  for (int i = 0; i < 4; ++i) {
    q[i] *= inverse;
  }
}

}  // namespace

void CompassFusion::Reset() {
  q_[0] = 1.0F;
  q_[1] = q_[2] = q_[3] = 0.0F;
  initialized_ = false;
  last_sample_us_ = 0;
  last_magnetic_us_ = 0;
}

bool CompassFusion::Update(const hal::ImuStatus& sample,
    const float* magnetic_field) {
  if (!sample.ready || sample.sample_time_us <= last_sample_us_) {
    return false;
  }
  float acceleration_squared = 0.0F;
  for (int i = 0; i < 3; ++i) {
    if (!std::isfinite(sample.acceleration_g[i]) ||
        (sample.angular_velocity_ready &&
            !std::isfinite(sample.angular_velocity_dps[i]))) {
      return false;
    }
    acceleration_squared += sample.acceleration_g[i] * sample.acceleration_g[i];
  }
  const float magnitude = std::sqrt(acceleration_squared);
  if (!std::isfinite(magnitude) || magnitude < 0.1F) {
    return false;
  }
  float acceleration[3];
  for (int i = 0; i < 3; ++i) {
    acceleration[i] = sample.acceleration_g[i] / magnitude;
  }
  if (!initialized_ || sample.sample_time_us - last_sample_us_ > kMaximumSampleGapUs) {
    // 首帧或长时间中断后重新对齐重力，不将暂停时间积分成巨大转角。
    const float roll = std::atan2(acceleration[1], acceleration[2]);
    const float pitch = std::asin(std::clamp(-acceleration[0], -1.0F, 1.0F));
    const float cr = std::cos(roll * 0.5F), sr = std::sin(roll * 0.5F);
    const float cp = std::cos(pitch * 0.5F), sp = std::sin(pitch * 0.5F);
    q_[0] = cr * cp;
    q_[1] = sr * cp;
    q_[2] = cr * sp;
    q_[3] = -sr * sp;
    initialized_ = true;
    last_magnetic_us_ = 0;
  } else {
    const float dt = (sample.sample_time_us - last_sample_us_) * 0.000001F;
    const float w = q_[0], x = q_[1], y = q_[2], z = q_[3];
    const float gravity[3] = {
        2.0F * (x * z - w * y), 2.0F * (y * z + w * x),
        1.0F - 2.0F * (x * x + y * y)};
    // 运动加速度偏离 1g 时减小重力修正，优先使用陀螺仪短时预测。
    const float confidence = std::clamp(1.0F - std::fabs(magnitude - 1.0F) / 0.2F,
        0.0F, 1.0F);
    const float error[3] = {
        acceleration[1] * gravity[2] - acceleration[2] * gravity[1],
        acceleration[2] * gravity[0] - acceleration[0] * gravity[2],
        acceleration[0] * gravity[1] - acceleration[1] * gravity[0]};
    float rate[3];
    float rate_squared = 0.0F;
    for (int i = 0; i < 3; ++i) {
      rate[i] = (sample.angular_velocity_ready
          ? sample.angular_velocity_dps[i] * kRadians : 0.0F) +
          kGravityGain * confidence * error[i];
      rate_squared += rate[i] * rate[i];
    }
    const float speed = std::sqrt(rate_squared);
    if (speed > 0.000001F) {
      const float half_angle = speed * dt * 0.5F;
      const float factor = std::sin(half_angle) / speed;
      const float dw = std::cos(half_angle);
      const float dx = rate[0] * factor, dy = rate[1] * factor, dz = rate[2] * factor;
      q_[0] = w * dw - x * dx - y * dy - z * dz;
      q_[1] = w * dx + x * dw + y * dz - z * dy;
      q_[2] = w * dy - x * dz + y * dw + z * dx;
      q_[3] = w * dz + x * dy - y * dx + z * dw;
      NormalizeQuaternion(q_);
    }
  }
  last_sample_us_ = sample.sample_time_us;
  if (magnetic_field != nullptr && std::isfinite(magnetic_field[0]) &&
      std::isfinite(magnetic_field[1]) && std::isfinite(magnetic_field[2])) {
    const float w = q_[0], x = q_[1], y = q_[2], z = q_[3];
    // 将三轴磁场旋转到水平参考系，磁北只修正偏航，不污染重力倾角。
    const float north = (1.0F - 2.0F * (y * y + z * z)) * magnetic_field[0] +
        2.0F * (x * y - w * z) * magnetic_field[1] +
        2.0F * (x * z + w * y) * magnetic_field[2];
    const float east = 2.0F * (x * y + w * z) * magnetic_field[0] +
        (1.0F - 2.0F * (x * x + z * z)) * magnetic_field[1] +
        2.0F * (y * z - w * x) * magnetic_field[2];
    if (north * north + east * east > 0.01F) {
      const float magnetic_dt = (sample.sample_time_us - last_magnetic_us_) * 0.000001F;
      const float blend = last_magnetic_us_ == 0 ? 1.0F :
          1.0F - std::exp(-std::min(magnetic_dt, 0.25F) / kMagneticTimeConstant);
      const float half_correction = -std::atan2(east, north) * blend * 0.5F;
      const float c = std::cos(half_correction), s = std::sin(half_correction);
      q_[0] = c * w - s * z;
      q_[1] = c * x - s * y;
      q_[2] = c * y + s * x;
      q_[3] = c * z + s * w;
      NormalizeQuaternion(q_);
      last_magnetic_us_ = sample.sample_time_us;
    }
  }
  return true;
}

void CompassFusion::ReadAngles(float& pitch, float& roll, float& yaw) const {
  const float w = q_[0], x = q_[1], y = q_[2], z = q_[3];
  pitch = std::asin(std::clamp(2.0F * (w * y - z * x), -1.0F, 1.0F)) / kRadians;
  roll = std::atan2(2.0F * (w * x + y * z),
      1.0F - 2.0F * (x * x + y * y)) / kRadians;
  // 仅输出四元数的偏航角，板级方位方向由 HAL 换算。
  yaw = std::atan2(2.0F * (w * z + x * y),
      1.0F - 2.0F * (y * y + z * z)) / kRadians;
}

bool CompassFusion::HeadingReady(int64_t now_us) const {
  return initialized_ && last_magnetic_us_ != 0 && now_us >= last_magnetic_us_ &&
         now_us - last_magnetic_us_ <= 1500000;
}

}  // namespace lilygo_box::app
