/*
 * @Description: Camera diagnostic errors
 * @Author: LILYGO_L
 * @Date: 2026-08-04 00:00:00
 * @LastEditTime: 2026-08-04 00:00:00
 * @License: GPL 3.0
 */
#pragma once

#include <cstdint>

#include "app/diagnostics/diagnostic_error.h"

namespace lilygo_box {

enum class CameraError : uint8_t {
  kNone = 0,
  kScreenNotReady = 1,
  kPowerEnableFailed = 2,
  kVideoInitFailed = 3,
  kSensorNotDetected = 4,
  kVideoDeviceOpenFailed = 5,
  kSensorRestoreFailed = 6,
  kFormatConfigurationFailed = 7,
  kBufferAllocationFailed = 8,
  kBufferMappingFailed = 9,
  kProcessingInitFailed = 10,
  kOutputBufferAllocationFailed = 11,
  kStreamStartFailed = 12,
  kPreviewTaskCreateFailed = 13,
  kProviderUnavailable = 14,
};

/**
 * @brief 获取摄像头错误对应的诊断信息
 * @param error 摄像头错误
 * @return 包含错误码和文字说明的诊断信息
 */
DiagnosticError GetCameraDiagnosticError(CameraError error);

}  // namespace lilygo_box
