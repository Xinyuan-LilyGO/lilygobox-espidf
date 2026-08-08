/*
 * @Description: Diagnostic error description mapping
 * @Author: LILYGO_L
 * @Date: 2026-08-04 00:00:00
 * @LastEditTime: 2026-08-04 00:00:00
 * @License: GPL 3.0
 */
#include "app/diagnostics/diagnostic_error.h"

#include "app/diagnostics/camera_error.h"

namespace lilygo_box {

DiagnosticError GetCameraDiagnosticError(CameraError error) {
  switch (error) {
    case CameraError::kNone:
      return {"CAM-E00", "No error"};
    case CameraError::kScreenNotReady:
      return {"CAM-E01", "Screen is not ready"};
    case CameraError::kPowerEnableFailed:
      return {"CAM-E02", "Camera power could not be enabled"};
    case CameraError::kVideoInitFailed:
      return {"CAM-E03", "Video subsystem initialization failed"};
    case CameraError::kSensorNotDetected:
      return {"CAM-E04", "Camera sensor did not respond on SCCB/I2C"};
    case CameraError::kVideoDeviceOpenFailed:
      return {"CAM-E05", "Camera video device is unavailable"};
    case CameraError::kSensorRestoreFailed:
      return {"CAM-E06", "Camera sensor configuration failed"};
    case CameraError::kFormatConfigurationFailed:
      return {"CAM-E07", "Camera format configuration failed"};
    case CameraError::kBufferAllocationFailed:
      return {"CAM-E08", "Camera buffer allocation failed"};
    case CameraError::kBufferMappingFailed:
      return {"CAM-E09", "Camera buffer mapping failed"};
    case CameraError::kProcessingInitFailed:
      return {"CAM-E10", "Camera processing initialization failed"};
    case CameraError::kOutputBufferAllocationFailed:
      return {"CAM-E11", "Camera output buffer allocation failed"};
    case CameraError::kStreamStartFailed:
      return {"CAM-E12", "Camera stream could not be started"};
    case CameraError::kPreviewTaskCreateFailed:
      return {"CAM-E13", "Camera preview task could not be created"};
    case CameraError::kProviderUnavailable:
      return {"CAM-E14", "Camera provider is unavailable"};
    default:
      return {"CAM-E99", "Unknown camera error"};
  }
}

}  // namespace lilygo_box
