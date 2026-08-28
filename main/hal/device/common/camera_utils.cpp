/*
 * @Description: 摄像头预览公共参数与生命周期辅助实现
 * @Author: LILYGO_L
 * @Date: 2026-08-28 00:00:00
 * @LastEditTime: 2026-08-28 00:00:00
 * @License: GPL 3.0
 */
#include "hal/device/common/camera_utils.h"

#include <cerrno>

#include "freertos/task.h"

namespace lilygo_box::hal::camera_utils {

static_assert(kStartupTimeoutMs >= kSensorReadyPollIntervalMs);

uint32_t StartupElapsedMs(TickType_t start_tick) {
  return static_cast<uint32_t>(
      (xTaskGetTickCount() - start_tick) * portTICK_PERIOD_MS);
}

bool StartupTimedOut(TickType_t start_tick) {
  return StartupElapsedMs(start_tick) >= kStartupTimeoutMs;
}

uint32_t StartupRemainingMs(TickType_t start_tick) {
  const uint32_t elapsed_ms = StartupElapsedMs(start_tick);
  return elapsed_ms < kStartupTimeoutMs ? kStartupTimeoutMs - elapsed_ms : 0;
}

bool IsRetryableIoError(int error) {
  switch (error) {
    case EAGAIN:
    case EBUSY:
    case EIO:
    case ENODEV:
    case ENOENT:
    case ENXIO:
    case ETIMEDOUT:
      return true;
    default:
      return false;
  }
}

bool IsRetryableVideoError(esp_err_t error) {
  return error == ESP_FAIL || error == ESP_ERR_NOT_FOUND ||
         error == ESP_ERR_TIMEOUT;
}

int NormalizePreviewRotationAngle(int angle) {
  angle %= 360;
  if (angle < 0) {
    angle += 360;
  }
  switch (angle) {
    case 90:
    case 180:
    case 270:
      return angle;
    default:
      return 0;
  }
}

ppa_srm_rotation_angle_t ToPreviewPpaRotation(int angle) {
  switch (NormalizePreviewRotationAngle(angle)) {
    case 90:
      return PPA_SRM_ROTATION_ANGLE_270;
    case 180:
      return PPA_SRM_ROTATION_ANGLE_180;
    case 270:
      return PPA_SRM_ROTATION_ANGLE_90;
    default:
      return PPA_SRM_ROTATION_ANGLE_0;
  }
}

}  // namespace lilygo_box::hal::camera_utils
