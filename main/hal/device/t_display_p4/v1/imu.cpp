/*
 * @Description: T-Display-P4 V1 新旧 IMU 数据读取与姿态实现
 * @Author: LILYGO_L
 * @Date: 2026-08-28 00:00:00
 * @LastEditTime: 2026-09-02 17:53:04
 * @License: GPL 3.0
 */
#include <cmath>
#include <cstdint>

#include "esp_timer.h"

#include "hal/device/t_display_p4/device.h"

namespace lilygo_box::hal {
namespace {

constexpr float kDegreesToRadians = 0.0174532925F;
constexpr float kRadiansToDegrees = 57.2957795F;

using lilygo_device_driver::t_display_p4::device::ImuType;

struct Vector3 {
  float x = 0.0F;
  float y = 0.0F;
  float z = 0.0F;
};

struct ImuSample {
  Vector3 acceleration_g;
  Vector3 magnetic_field_ut;
  Vector3 angular_velocity_dps;
  bool magnetic_field_ready = false;
};

Vector3 g_magnetic_field;
bool g_magnetic_ready = false;

constexpr uint8_t kQmiStatus0 = 0x2E;
constexpr uint8_t kQmiAccelData = 0x35;
constexpr uint8_t kQmcStatus = 0x09;
constexpr uint8_t kQmcData = 0x01;

int16_t DecodeAxis(const uint8_t* bytes) {
  const uint16_t value = static_cast<uint16_t>(bytes[0]) |
                         (static_cast<uint16_t>(bytes[1]) << 8);
  return static_cast<int16_t>(value < 0x8000 ? static_cast<int32_t>(value)
                                          : static_cast<int32_t>(value) - 65536);
}

bool ReadIcm20948(cpp_bus_driver::Icm20948& sensor, ImuSample& sample) {
  cpp_bus_driver::Icm20948::RawData raw;
  if (!sensor.ReadRawData(raw)) {
    return false;
  }
  // 对应板级默认 ±2g 量程；AK09916 的灵敏度为 0.15 uT/LSB。
  sample.acceleration_g = {raw.acceleration.x / 16384.0F,
      raw.acceleration.y / 16384.0F, raw.acceleration.z / 16384.0F};
  // 对应板级默认 ±250 deg/s 量程。
  sample.angular_velocity_dps = {raw.angular_velocity.x / 131.0F,
      raw.angular_velocity.y / 131.0F, raw.angular_velocity.z / 131.0F};
  sample.magnetic_field_ready = raw.magnetometer_data_ready &&
                               !raw.magnetometer_overflow;
  // 内置 AK09916 与 ICM20948 六轴的坐标方向不同，按 (X, -Y, -Z) 对齐。
  // 在缓存、姿态计算和状态输出前完成轴向转换。
  sample.magnetic_field_ut = {raw.magnetic_field.x * 0.15F,
      -raw.magnetic_field.y * 0.15F, -raw.magnetic_field.z * 0.15F};
  return true;
}

/**
 * @brief 一次读取 QMI8658 六轴寄存器并转换为 g 和 deg/s
 * @param sensor 已初始化的 QMI8658 实例
 * @param sample 接收本轮加速度及角速度
 * @return 六轴数据就绪且通信成功返回 true，否则返回 false
 */
bool ReadQmi8658Motion(SensorQMI8658& sensor, ImuSample& sample) {
  const int status = sensor.readReg(kQmiStatus0);
  if (status < 0 || (status & 0x03) != 0x03) {
    return false;
  }
  // SensorLib 的 getAccelRaw 仅检查 -1，这里检查所有负值通信错误。
  uint8_t raw[12] = {};
  if (sensor.readRegBuff(kQmiAccelData, raw, sizeof(raw)) < 0) {
    return false;
  }
  const float scale = sensor.getAccelerometerScales();
  sample.acceleration_g = {DecodeAxis(raw) * scale, DecodeAxis(raw + 2) * scale,
      DecodeAxis(raw + 4) * scale};
  const float gyro_scale = sensor.getGyroscopeScales();
  sample.angular_velocity_dps = {DecodeAxis(raw + 6) * gyro_scale,
      DecodeAxis(raw + 8) * gyro_scale, DecodeAxis(raw + 10) * gyro_scale};
  return true;
}

bool ReadQmc6309MagneticField(SensorQMC6309& sensor, Vector3& magnetic) {
  // DRDY/OVFL 会随状态读取清除，单次读取同时检查就绪和溢出。
  const int status = sensor.readReg(kQmcStatus);
  if (status < 0 || (status & 0x01) == 0 || (status & 0x02) != 0) {
    return false;
  }
  uint8_t raw[6] = {};
  if (sensor.readRegBuff(kQmcData, raw, sizeof(raw)) < 0) {
    return false;
  }
  // 对应板级默认 FS_8G 量程：4000 LSB/Gauss，即 0.025 uT/LSB。
  constexpr float kMagneticScale = 0.025F;
  magnetic = {DecodeAxis(raw) * kMagneticScale,
      DecodeAxis(raw + 2) * kMagneticScale,
      DecodeAxis(raw + 4) * kMagneticScale};
  return true;
}

}  // namespace

bool TDisplayP4Device::SetImuEnabled(bool enabled) {
  g_magnetic_ready = false;
  const bool result = driver_.SetImuSleep(!enabled);
  imu_enabled_.store(enabled && result);
  return result;
}

bool TDisplayP4Device::ReadImuStatus(ImuStatus* status) {
  if (status == nullptr) {
    return false;
  }

  *status = ImuStatus();
  if (!imu_enabled_.load() || !driver_.IsImuReady()) {
    return false;
  }

  ImuSample data;
  const auto& chip = driver_.chip();
  switch (driver_.imu_type()) {
    case ImuType::kIcm20948:
      if (chip.icm20948 == nullptr || !ReadIcm20948(*chip.icm20948, data)) {
        return false;
      }
      break;
    case ImuType::kQmi8658Qmc6309:
      if (chip.qmi8658 == nullptr || chip.qmc6309 == nullptr ||
          !ReadQmi8658Motion(*chip.qmi8658, data)) {
        return false;
      }
      data.magnetic_field_ready =
          ReadQmc6309MagneticField(*chip.qmc6309, data.magnetic_field_ut);
      break;
    default:
      return false;
  }

  if (data.magnetic_field_ready) {
    g_magnetic_field = data.magnetic_field_ut;
    g_magnetic_ready = true;
  }
  if (!g_magnetic_ready) {
    return false;
  }
  const auto& acceleration = data.acceleration_g;
  const float acceleration_magnitude_squared = acceleration.x * acceleration.x +
                                               acceleration.y * acceleration.y +
                                               acceleration.z * acceleration.z;
  const auto& magnetic = g_magnetic_field;
  const float magnetic_magnitude_squared = magnetic.x * magnetic.x +
                                           magnetic.y * magnetic.y +
                                           magnetic.z * magnetic.z;
  if (acceleration_magnitude_squared < 0.0001F ||
      magnetic_magnitude_squared < 0.0001F) {
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

  status->sample_time_us = esp_timer_get_time();
  status->acceleration_g[0] = acceleration.x;
  status->acceleration_g[1] = acceleration.y;
  status->acceleration_g[2] = acceleration.z;
  status->angular_velocity_ready = true;
  status->angular_velocity_dps[0] = data.angular_velocity_dps.x;
  status->angular_velocity_dps[1] = data.angular_velocity_dps.y;
  status->angular_velocity_dps[2] = data.angular_velocity_dps.z;
  status->ready = true;
  status->pitch_deg = pitch;
  status->yaw_deg = yaw;
  status->roll_deg = roll;
  status->magnetic_field_ready = data.magnetic_field_ready;
  status->magnetic_field_ut[0] = magnetic.x;
  status->magnetic_field_ut[1] = magnetic.y;
  status->magnetic_field_ut[2] = magnetic.z;
  return true;
}

float TDisplayP4Device::ConvertImuHeading(float yaw_deg) const {
  float heading = std::fmod(-yaw_deg, 360.0F);
  if (heading < 0.0F) {
    heading += 360.0F;
  }
  return heading;
}

}  // namespace lilygo_box::hal
