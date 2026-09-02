/*
 * @Description: T-Display-P4 ICM20948 IMU 实现
 * @Author: LILYGO_L
 * @Date: 2026-08-28 00:00:00
 * @LastEditTime: 2026-09-02 17:53:04
 * @License: GPL 3.0
 */
#include <cmath>

#include "hal/device/t_display_p4/device.h"

namespace lilygo_box::hal {
namespace {

constexpr float kDegreesToRadians = 0.0174532925F;
constexpr float kRadiansToDegrees = 57.2957795F;

}  // namespace

bool TDisplayP4Device::SetImuEnabled(bool enabled) {
  const bool result = driver_.SetIcm20948Sleep(!enabled);
  imu_enabled_.store(enabled && result);
  return result;
}

bool TDisplayP4Device::ReadImuStatus(ImuStatus* status) {
  if (status == nullptr) {
    return false;
  }

  *status = ImuStatus();
  auto& icm20948 = driver_.chip().icm20948;
  if (!imu_enabled_.load() || !driver_.IsIcm20948Ready() ||
      icm20948 == nullptr) {
    return false;
  }

  cpp_bus_driver::Icm20948::SensorData data;
  if (!icm20948->ReadData(data)) {
    return false;
  }

  const auto& acceleration = data.acceleration_g;
  const float acceleration_magnitude_squared = acceleration.x * acceleration.x +
                                               acceleration.y * acceleration.y +
                                               acceleration.z * acceleration.z;
  const auto& magnetic = data.magnetic_field_ut;
  const float magnetic_magnitude_squared = magnetic.x * magnetic.x +
                                           magnetic.y * magnetic.y +
                                           magnetic.z * magnetic.z;
  if (acceleration_magnitude_squared < 0.0001F ||
      magnetic_magnitude_squared < 0.0001F || data.magnetometer_overflow) {
    return false;
  }

  const float pitch =
      std::atan2(-acceleration.x, std::sqrt(acceleration.y * acceleration.y +
                                            acceleration.z * acceleration.z)) *
      kRadiansToDegrees;
  const float roll =
      std::atan2(acceleration.y, acceleration.z) * kRadiansToDegrees;
  const float pitch_radians = pitch * kDegreesToRadians;
  const float roll_radians = roll * kDegreesToRadians;
  const float magnetic_x_horizontal = magnetic.x * std::cos(pitch_radians) +
                                      magnetic.z * std::sin(pitch_radians);
  const float magnetic_y_horizontal =
      magnetic.x * std::sin(roll_radians) * std::sin(pitch_radians) +
      magnetic.y * std::cos(roll_radians) -
      magnetic.z * std::sin(roll_radians) * std::cos(pitch_radians);
  float yaw = std::atan2(magnetic_y_horizontal, magnetic_x_horizontal) *
              kRadiansToDegrees;
  if (yaw < 0.0F) {
    yaw += 360.0F;
  }

  status->ready = true;
  status->pitch_deg = pitch;
  status->yaw_deg = yaw;
  status->roll_deg = roll;
  return true;
}

}  // namespace lilygo_box::hal
