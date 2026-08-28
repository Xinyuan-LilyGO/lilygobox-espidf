/*
 * @Description: T-Display-P4-Air IMU 与磁力计硬件实现
 * @Author: LILYGO_L
 * @Date: 2026-08-28 00:00:00
 * @LastEditTime: 2026-08-28 00:00:00
 * @License: GPL 3.0
 */
#include "hal/device/t_display_p4_air/device.h"

#include <cmath>
#include <cstdint>

#include "base/logger.h"
#include "bhy2_parse.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

namespace lilygo_box::hal {
namespace {

constexpr float kRadiansToDegrees = 57.2957795F;
constexpr float kDegreesToRadians = 0.0174532925F;
constexpr float kBhi260apAccelerometerScale = 1.0F / 4096.0F;
constexpr float kBhi260apSampleRateHz = 100.0F;
constexpr uint32_t kBhi260apReportLatencyMs = 0;
constexpr uint32_t kImuHardwareReadyTimeoutMs = 5000;
constexpr uint32_t kImuHardwareReadyPollMs = 20;

}  // namespace

void TDisplayP4AirDevice::Bhi260apAccelerationCallback(
    const struct bhy2_fifo_parse_data_info* callback_info, void* context) {
  auto* self = static_cast<TDisplayP4AirDevice*>(context);
  if (self == nullptr || callback_info == nullptr ||
      callback_info->data_ptr == nullptr || callback_info->data_size < 6) {
    return;
  }

  struct bhy2_data_xyz data = {};
  bhy2_parse_xyz(callback_info->data_ptr, &data);
  self->imu_.acceleration[0] = data.x * kBhi260apAccelerometerScale;
  self->imu_.acceleration[1] = data.y * kBhi260apAccelerometerScale;
  self->imu_.acceleration[2] = data.z * kBhi260apAccelerometerScale;
  self->imu_.acceleration_ready = true;
}

bool TDisplayP4AirDevice::SetImuEnabled(bool enabled) {
  if (imu_.mutex == nullptr ||
      xSemaphoreTake(imu_.mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
    LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
        "Set IMU enabled state failed: mutex unavailable\n");
    return false;
  }

  bool result = true;
  if (!enabled) {
    if (imu_.configured && driver_.IsBhi260apReady() &&
        driver_.chip().bhi260ap != nullptr) {
      result &= driver_.chip().bhi260ap->ConfigureSensor(
          BHY2_SENSOR_ID_ACC_PASS, 0.0F, kBhi260apReportLatencyMs);
    }
    result &= driver_.SetBhi260apSleep(true);
    result &= driver_.SetQmc6310nSleep(true);
    imu_.configured = false;
    imu_.acceleration_ready = false;
    imu_.magnetic_field_ready = false;
    imu_enabled_.store(false);
    xSemaphoreGive(imu_.mutex);
    return result;
  }

  if (imu_enabled_.load() && imu_.configured &&
      driver_.IsBhi260apReady() && driver_.IsQmc6310nReady() &&
      driver_.chip().bhi260ap != nullptr &&
      driver_.chip().qmc6310n != nullptr) {
    xSemaphoreGive(imu_.mutex);
    return true;
  }

  uint32_t elapsed_ms = 0;
  while (driver_.chip().bhi260ap != nullptr &&
         driver_.chip().qmc6310n != nullptr &&
         (!driver_.IsBhi260apReady() || !driver_.IsQmc6310nReady()) &&
         elapsed_ms < kImuHardwareReadyTimeoutMs) {
    vTaskDelay(pdMS_TO_TICKS(kImuHardwareReadyPollMs));
    elapsed_ms += kImuHardwareReadyPollMs;
  }
  if (!driver_.IsBhi260apReady() || !driver_.IsQmc6310nReady() ||
      driver_.chip().bhi260ap == nullptr ||
      driver_.chip().qmc6310n == nullptr) {
    LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
        "Enable IMU failed: BHI260AP ready=%u, QMC6310N ready=%u\n",
        static_cast<unsigned int>(driver_.IsBhi260apReady()),
        static_cast<unsigned int>(driver_.IsQmc6310nReady()));
    xSemaphoreGive(imu_.mutex);
    return false;
  }

  auto& bhi260ap = *driver_.chip().bhi260ap;
  result = driver_.SetBhi260apSleep(false);
  if (!result) {
    LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
        "Enable IMU failed: wake BHI260AP failed\n");
  }
  if (result) {
    result = driver_.SetQmc6310nSleep(false);
    if (!result) {
      LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
          "Enable IMU failed: wake QMC6310N failed\n");
    }
  }
  if (result) {
    result = bhi260ap.RegisterFifoCallback(
        BHY2_SENSOR_ID_ACC_PASS, Bhi260apAccelerationCallback, this);
    if (!result) {
      LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
          "Enable IMU failed: register BHI260AP FIFO callback failed "
          "(error code: %d)\n",
          static_cast<int>(bhi260ap.last_error()));
    }
  }
  if (result) {
    result = bhi260ap.ProcessFifo();
    if (!result) {
      LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
          "Enable IMU failed: process BHI260AP FIFO failed "
          "(error code: %d)\n",
          static_cast<int>(bhi260ap.last_error()));
    }
  }
  if (result) {
    result = bhi260ap.UpdateVirtualSensorList();
    if (!result) {
      LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
          "Enable IMU failed: update BHI260AP virtual sensor list failed "
          "(error code: %d)\n",
          static_cast<int>(bhi260ap.last_error()));
    }
  }
  if (result) {
    result = bhi260ap.ConfigureSensor(BHY2_SENSOR_ID_ACC_PASS,
        kBhi260apSampleRateHz, kBhi260apReportLatencyMs);
    if (!result) {
      LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
          "Enable IMU failed: configure BHI260AP accelerometer failed "
          "(error code: %d)\n",
          static_cast<int>(bhi260ap.last_error()));
    }
  }
  imu_.configured = result;
  imu_.acceleration_ready = false;
  imu_.magnetic_field_ready = false;
  imu_enabled_.store(result);
  if (!result) {
    driver_.SetBhi260apSleep(true);
    driver_.SetQmc6310nSleep(true);
  }
  xSemaphoreGive(imu_.mutex);
  return result;
}

bool TDisplayP4AirDevice::ReadImuStatus(ImuStatus* status) {
  if (status == nullptr) {
    return false;
  }

  *status = ImuStatus();

  if (!imu_enabled_.load() || !imu_.configured) {
    return false;
  }
  if (!driver_.IsBhi260apReady() || !driver_.IsQmc6310nReady() ||
      driver_.chip().bhi260ap == nullptr ||
      driver_.chip().qmc6310n == nullptr) {
    LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
        "Read IMU status failed: BHI260AP ready=%u, QMC6310N ready=%u\n",
        static_cast<unsigned int>(driver_.IsBhi260apReady()),
        static_cast<unsigned int>(driver_.IsQmc6310nReady()));
    return false;
  }
  if (imu_.mutex == nullptr ||
      xSemaphoreTake(imu_.mutex, pdMS_TO_TICKS(20)) != pdTRUE) {
    return false;
  }

  bool result = driver_.chip().bhi260ap->ProcessFifo();
  if (!result) {
    LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
        "Read IMU status failed: process BHI260AP FIFO failed "
        "(error code: %d)\n",
        static_cast<int>(driver_.chip().bhi260ap->last_error()));
  }
  MagnetometerData magnetic_data;
  if (driver_.chip().qmc6310n->readData(magnetic_data)) {
    imu_.magnetic_field[0] = magnetic_data.magnetic_field.x;
    imu_.magnetic_field[1] = magnetic_data.magnetic_field.y;
    imu_.magnetic_field[2] = magnetic_data.magnetic_field.z;
    imu_.magnetic_field_ready = true;
  } else if (!imu_.magnetic_field_ready) {
    result = false;
    LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
        "Read IMU status failed: QMC6310N has no valid data\n");
  }

  if (!result || !imu_.acceleration_ready ||
      !imu_.magnetic_field_ready) {
    xSemaphoreGive(imu_.mutex);
    return false;
  }

  const float acceleration_z = -imu_.acceleration[2];
  const float pitch =
      std::atan2(-imu_.acceleration[0],
          std::sqrt(imu_.acceleration[1] * imu_.acceleration[1] +
                    acceleration_z * acceleration_z)) *
      kRadiansToDegrees;
  const float roll =
      std::atan2(imu_.acceleration[1], acceleration_z) * kRadiansToDegrees;
  const float pitch_radians = pitch * kDegreesToRadians;
  const float roll_radians = roll * kDegreesToRadians;
  const float magnetic_x_horizontal =
      imu_.magnetic_field[0] * std::cos(pitch_radians) +
      imu_.magnetic_field[2] * std::sin(pitch_radians);
  const float magnetic_y_horizontal =
      imu_.magnetic_field[0] * std::sin(roll_radians) *
          std::sin(pitch_radians) -
      imu_.magnetic_field[2] * std::sin(roll_radians) *
          std::cos(pitch_radians) +
      imu_.magnetic_field[1] * std::cos(roll_radians);
  float yaw = std::atan2(magnetic_y_horizontal, magnetic_x_horizontal) *
              kRadiansToDegrees;
  if (yaw < 0.0F) {
    yaw += 360.0F;
  }

  status->ready = true;
  status->pitch_deg = pitch;
  status->yaw_deg = yaw;
  status->roll_deg = roll;
  xSemaphoreGive(imu_.mutex);
  return true;
}

}  // namespace lilygo_box::hal
