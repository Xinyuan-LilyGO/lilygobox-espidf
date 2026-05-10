/*
 * @Description: None
 * @Author: LILYGO_L
 * @Date: 2026-05-10 13:27:05
 * @LastEditTime: 2026-05-10 13:31:43
 * @License: GPL 3.0
 */
#pragma once

namespace lilygo_box::hal {

struct PowerDiagnostics {
  bool ready = false;
  bool battery_present = false;
  bool charging = false;
  int voltage_mv = 0;
  int current_ma = 0;
  int charge_percent = 0;
};

struct MotionDiagnostics {
  bool ready = false;
  float acceleration_x_g = 0.0F;
  float acceleration_y_g = 0.0F;
  float acceleration_z_g = 0.0F;
};

struct DeviceDiagnostics {
  PowerDiagnostics power;
  MotionDiagnostics motion;
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
