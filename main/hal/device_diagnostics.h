/*
 * @Description: None
 * @Author: LILYGO_L
 * @Date: 2026-05-10 13:27:05
 * @LastEditTime: 2026-05-14 00:20:00
 * @License: GPL 3.0
 */
#pragma once

#include "hal/bmu_provider.h"
#include "hal/imu_provider.h"

namespace lilygo_box::hal {

struct DeviceDiagnostics {
  BmuStatus bmu;
  ImuStatus imu;
};

class DeviceDiagnosticsProvider {
 public:
  virtual ~DeviceDiagnosticsProvider() = default;

  /**
   * @brief 读取设备诊断快照
   * @param diagnostics 诊断数据输出地址
   * @return 读取到有效诊断数据返回 true，否则返回 false
   * @Date 2026-05-10 13:01:03
   */
  virtual bool ReadDiagnostics(DeviceDiagnostics* diagnostics) = 0;
};

}  // namespace lilygo_box::hal
