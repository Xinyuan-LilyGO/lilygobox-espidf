/*
 * @Description: T-Display-P4 V2 BHI260AP 与 QMC6309 姿态实现
 * @Author: LILYGO_L
 * @Date: 2026-09-16 00:00:00
 * @LastEditTime: 2026-09-16 00:00:00
 * @License: GPL 3.0
 */
#include <cmath>
#include <mutex>

#include "SensorQMC6309.hpp"
#include "bhy2_parse.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "hal/device/t_display_p4/device.h"

namespace lilygo_box::hal {
namespace {

constexpr float kAccelerometerScale = 1.0F / 4096.0F;
constexpr float kDegreesToRadians = 0.0174532925F;
constexpr float kRadiansToDegrees = 57.2957795F;
constexpr float kSampleRateHz = 100.0F;
constexpr uint32_t kReportLatencyMs = 0;
constexpr uint32_t kHardwareReadyTimeoutMs = 5000;
constexpr uint32_t kHardwareReadyPollMs = 20;

// FIFO 回调在 ProcessFifo 调用线程执行，所有访问均由 g_imu_mutex 保护。
float g_acceleration[3] = {};
bool g_acceleration_ready = false;
bool g_bhi_configured = false;
std::mutex g_imu_mutex;

/**
 * @brief 将 BHI260AP 的三轴加速度 FIFO 数据转换为重力加速度单位
 * @param info 传感器 FIFO 数据及长度
 */
void ParseAcceleration(const struct bhy2_fifo_parse_data_info* info, void*) {
  if (info == nullptr || info->data_ptr == nullptr || info->data_size < 6) {
    return;
  }
  struct bhy2_data_xyz data = {};
  bhy2_parse_xyz(info->data_ptr, &data);
  g_acceleration[0] = data.x * kAccelerometerScale;
  g_acceleration[1] = data.y * kAccelerometerScale;
  g_acceleration[2] = data.z * kAccelerometerScale;
  g_acceleration_ready = true;
}

/**
 * @brief 唤醒 BHI260AP 并在首次读取时配置加速度虚拟传感器
 * @param driver V2 板级驱动
 * @return FIFO 回调与传感器均配置成功时返回 true
 */
bool ConfigureBhi260ap(lilygo_device_driver::TDisplayP4Driver& driver) {
  if (!driver.IsBhi260apReady() || !driver.SetBhi260apSleep(false)) {
    return false;
  }
  auto* sensor = driver.chip().bhi260ap.get();
  if (sensor == nullptr) {
    return false;
  }
  if (g_bhi_configured) {
    return true;
  }
  if (!sensor->RegisterFifoCallback(BHY2_SENSOR_ID_ACC_PASS,
          ParseAcceleration) ||
      !sensor->ProcessFifo() || !sensor->UpdateVirtualSensorList() ||
      !sensor->ConfigureSensor(BHY2_SENSOR_ID_ACC_PASS, kSampleRateHz,
          kReportLatencyMs)) {
    return false;
  }
  g_bhi_configured = true;
  return true;
}

}  // namespace

bool TDisplayP4Device::SetImuEnabled(bool enabled) {
  std::lock_guard<std::mutex> lock(g_imu_mutex);
  if (!enabled) {
    bool result = true;
    if (g_bhi_configured && driver_.chip().bhi260ap != nullptr) {
      result &= driver_.chip().bhi260ap->ConfigureSensor(
          BHY2_SENSOR_ID_ACC_PASS, 0.0F, kReportLatencyMs);
    }
    result &= driver_.SetBhi260apSleep(true);
    result &= driver_.SetQmc6309Sleep(true);
    g_bhi_configured = false;
    g_acceleration_ready = false;
    imu_enabled_.store(false);
    return result;
  }

  uint32_t elapsed_ms = 0;
  while ((!driver_.IsBhi260apReady() || !driver_.IsQmc6309Ready()) &&
         elapsed_ms < kHardwareReadyTimeoutMs) {
    vTaskDelay(pdMS_TO_TICKS(kHardwareReadyPollMs));
    elapsed_ms += kHardwareReadyPollMs;
  }
  if (!driver_.IsBhi260apReady() || !driver_.IsQmc6309Ready()) {
    imu_enabled_.store(false);
    return false;
  }
  bool result = driver_.SetBhi260apSleep(false);
  result &= driver_.SetQmc6309Sleep(false);
  imu_enabled_.store(result);
  if (result) {
    g_acceleration_ready = false;
  }
  return result;
}

bool TDisplayP4Device::ReadImuStatus(ImuStatus* status) {
  if (status == nullptr) {
    return false;
  }
  *status = ImuStatus();
  std::lock_guard<std::mutex> lock(g_imu_mutex);
  if (!imu_enabled_.load() || !ConfigureBhi260ap(driver_) ||
      !driver_.IsQmc6309Ready() || driver_.chip().qmc6309 == nullptr) {
    return false;
  }
  auto* bhi260ap = driver_.chip().bhi260ap.get();
  if (bhi260ap == nullptr || !bhi260ap->ProcessFifo() ||
      !g_acceleration_ready) {
    return false;
  }
  MagnetometerData magnetic_data = {};
  if (!driver_.chip().qmc6309->readData(magnetic_data) ||
      magnetic_data.overflow) {
    return false;
  }
  const float acceleration_z = -g_acceleration[2];
  const float pitch = std::atan2(-g_acceleration[0],
      std::sqrt(g_acceleration[1] * g_acceleration[1] +
          acceleration_z * acceleration_z)) * kRadiansToDegrees;
  const float roll = std::atan2(g_acceleration[1], acceleration_z) *
                     kRadiansToDegrees;
  const float pitch_rad = pitch * kDegreesToRadians;
  const float roll_rad = roll * kDegreesToRadians;
  const float mx = magnetic_data.magnetic_field.x * std::cos(pitch_rad) +
                   magnetic_data.magnetic_field.z * std::sin(pitch_rad);
  const float my = magnetic_data.magnetic_field.x * std::sin(roll_rad) *
                       std::sin(pitch_rad) -
                   magnetic_data.magnetic_field.z * std::sin(roll_rad) *
                       std::cos(pitch_rad) +
                   magnetic_data.magnetic_field.y * std::cos(roll_rad);
  float yaw = std::atan2(my, mx) * kRadiansToDegrees;
  if (yaw < 0.0F) {
    yaw += 360.0F;
  }
  status->ready = true;
  status->pitch_deg = pitch;
  status->roll_deg = roll;
  status->yaw_deg = yaw;
  g_acceleration_ready = false;
  return true;
}

}  // namespace lilygo_box::hal
