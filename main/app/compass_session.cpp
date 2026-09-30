/*
 * @Description: 指南针磁北方位、三轴校准与后台设备采集实现
 * @Author: LILYGO_L
 * @Date: 2026-09-28 00:00:00
 * @LastEditTime: 2026-09-28 00:00:00
 * @License: GPL 3.0
 */
#include "app/compass_session.h"

#include "app/compass_fusion.h"
#include "app/storage/compass_storage.h"

#include <algorithm>
#include <cmath>
#include <type_traits>

#include "app/sensor_session_lock.h"
#include "base/logger.h"
#include "esp_timer.h"
#include "freertos/task.h"

namespace lilygo_box::app {
namespace {

constexpr uint32_t kSamplePeriodMs = 20;
// 时间仅用于终止未完成的校准，成功由采样质量和参数稳定性决定。
constexpr int64_t kCalibrationTimeoutMs = 60000;
constexpr int64_t kCalibrationSampleIntervalMs = 50;
constexpr int64_t kCalibrationCheckIntervalMs = 250;
constexpr unsigned kCalibrationSampleCapacity = 128;
constexpr unsigned kCalibrationMinimumSamples = 32;
constexpr unsigned kCalibrationStableChecks = 4;
// 拟合后仍检查磁场一致性；这些门槛需结合各板实测验证。
constexpr float kCalibrationMaximumRmsError = 0.03F;
constexpr float kCalibrationMaximumSampleError = 0.10F;
constexpr int64_t kHeadingStaleMs = 1500;
constexpr int64_t kGpsStaleMs = 10000;

using MagneticCalibration = CompassCalibrationData;

// 固定容量保存近期有效磁场，避免固定姿态的重复读数占满样本。
struct CalibrationSamples {
  float values[kCalibrationSampleCapacity][3] = {};
  unsigned count = 0;
  unsigned next = 0;
  unsigned updates = 0;
  unsigned checked_updates = 0;
  unsigned stable_checks = 0;
  int64_t last_sample_ms = 0;
  MagneticCalibration anchor;
};

/**
 * @brief 验证采样有效标记、姿态有限性及磁场幅值
 * @param sample 待检查的 IMU 采样
 * @return 采样有效且磁场幅值处于允许范围时返回 true，否则返回 false
 */
bool ValidSample(const hal::ImuStatus& sample) {
  if (!sample.ready || !sample.magnetic_field_ready ||
      !std::isfinite(sample.pitch_deg) || !std::isfinite(sample.roll_deg)) {
    return false;
  }
  float magnitude_squared = 0.0F;
  for (float axis : sample.magnetic_field_ut) {
    if (!std::isfinite(axis)) {
      return false;
    }
    magnitude_squared += axis * axis;
  }
  return magnitude_squared > 0.01F && magnitude_squared < 1000000.0F;
}

/**
 * @brief 保存有实际磁场变化的采样，限制采样频率及内存占用
 * @param samples 本轮采样缓冲区
 * @param field 当前三轴磁场，单位为 uT，须已通过有效性检查
 * @param now 当前单调时钟时间，单位为毫秒
 */
void AddCalibrationSample(CalibrationSamples& samples, const float* field,
    int64_t now) {
  if (samples.count != 0) {
    if (now - samples.last_sample_ms < kCalibrationSampleIntervalMs) {
      return;
    }
    const unsigned previous = (samples.next + kCalibrationSampleCapacity - 1) %
                              kCalibrationSampleCapacity;
    float change_squared = 0.0F;
    for (int i = 0; i < 3; ++i) {
      const float change = field[i] - samples.values[previous][i];
      change_squared += change * change;
    }
    if (change_squared < 4.0F) {
      return;
    }
  }
  std::copy_n(field, 3, samples.values[samples.next]);
  samples.next = (samples.next + 1) % kCalibrationSampleCapacity;
  samples.count = std::min(samples.count + 1, kCalibrationSampleCapacity);
  ++samples.updates;
  samples.last_sample_ms = now;
}

/**
 * @brief 用部分主元消元求解六元方程，拒绝无法可靠估参的退化采样
 * @param matrix 六行七列的增广矩阵，求解过程会修改其内容
 * @param solution 六个拟合系数的输出数组
 * @return 方程有数值可靠的解返回 true，否则返回 false
 */
bool SolveCalibration(float matrix[6][7], float solution[6]) {
  float largest = 0.0F;
  for (int i = 0; i < 6; ++i) {
    largest = std::max(largest, std::fabs(matrix[i][i]));
  }
  for (int column = 0; column < 6; ++column) {
    int pivot = column;
    for (int row = column + 1; row < 6; ++row) {
      if (std::fabs(matrix[row][column]) > std::fabs(matrix[pivot][column])) {
        pivot = row;
      }
    }
    if (!std::isfinite(matrix[pivot][column]) ||
        std::fabs(matrix[pivot][column]) <= largest * 0.0001F) {
      return false;
    }
    for (int j = column; j < 7; ++j) {
      std::swap(matrix[column][j], matrix[pivot][j]);
    }
    const float divisor = matrix[column][column];
    for (int j = column; j < 7; ++j) {
      matrix[column][j] /= divisor;
    }
    for (int row = 0; row < 6; ++row) {
      if (row == column) {
        continue;
      }
      const float factor = matrix[row][column];
      for (int j = column; j < 7; ++j) {
        matrix[row][j] -= factor * matrix[column][j];
      }
    }
  }
  for (int i = 0; i < 6; ++i) {
    solution[i] = matrix[i][6];
    if (!std::isfinite(solution[i])) {
      return false;
    }
  }
  return true;
}

/**
 * @brief 拟合轴对齐椭球，估计磁场偏置及三轴比例并检查修正误差
 * @param samples 本轮有效磁场采样
 * @param candidate 候选参数，验证通过前不影响正在使用的校准结果
 * @param rms_error 修正后的相对幅值均方根误差，尚未计算时为 -1
 * @param reason 本次评估结果的静态文字说明，用于调试日志
 * @return 样本足以拟合且磁场误差达标返回 true，否则返回 false
 */
bool EvaluateCalibration(const CalibrationSamples& samples,
    MagneticCalibration& candidate, float& rms_error, const char*& reason) {
  rms_error = -1.0F;
  reason = "more samples needed";
  if (samples.count < kCalibrationMinimumSamples) {
    return false;
  }
  float mean[3] = {};
  for (unsigned n = 0; n < samples.count; ++n) {
    for (int i = 0; i < 3; ++i) {
      mean[i] += samples.values[n][i] / samples.count;
    }
  }
  // 对平移后的磁场归一化，避免平方项与一次项的量级差影响求解。
  constexpr float kFitUnitUt = 50.0F;
  float matrix[6][7] = {};
  for (unsigned n = 0; n < samples.count; ++n) {
    const float x = (samples.values[n][0] - mean[0]) / kFitUnitUt;
    const float y = (samples.values[n][1] - mean[1]) / kFitUnitUt;
    const float z = (samples.values[n][2] - mean[2]) / kFitUnitUt;
    const float terms[6] = {x * x, y * y, z * z, x, y, z};
    for (int i = 0; i < 6; ++i) {
      matrix[i][6] += terms[i];
      for (int j = 0; j < 6; ++j) {
        matrix[i][j] += terms[i] * terms[j];
      }
    }
  }
  float coefficients[6];
  reason = "change tilt: samples do not constrain a 3D fit";
  if (!SolveCalibration(matrix, coefficients)) {
    return false;
  }
  float center[3];
  float level = 1.0F;
  reason = "invalid ellipsoid: rotate more or move away from interference";
  for (int i = 0; i < 3; ++i) {
    if (coefficients[i] <= 0.0F) {
      return false;
    }
    center[i] = -coefficients[i + 3] / (2.0F * coefficients[i]);
    level += coefficients[i] * center[i] * center[i];
  }
  float radius[3];
  float field_strength = 0.0F;
  for (int i = 0; i < 3; ++i) {
    radius[i] = kFitUnitUt * std::sqrt(level / coefficients[i]);
    candidate.offset[i] = mean[i] + center[i] * kFitUnitUt;
    if (!std::isfinite(radius[i]) || !std::isfinite(candidate.offset[i]) ||
        radius[i] < 10.0F || radius[i] > 150.0F) {
      return false;
    }
    field_strength += radius[i] / 3.0F;
  }
  reason = "magnetic field outside expected range";
  if (field_strength < 15.0F || field_strength > 100.0F) {
    return false;
  }
  for (int i = 0; i < 3; ++i) {
    candidate.scale[i] = field_strength / radius[i];
    if (candidate.scale[i] < 0.5F || candidate.scale[i] > 2.0F) {
      return false;
    }
  }
  float direction_mean[3] = {};
  float covariance[3][3] = {};
  float squared_error = 0.0F;
  float maximum_error = 0.0F;
  for (unsigned n = 0; n < samples.count; ++n) {
    float vector[3];
    float length_squared = 0.0F;
    for (int i = 0; i < 3; ++i) {
      vector[i] = (samples.values[n][i] - candidate.offset[i]) / radius[i];
      length_squared += vector[i] * vector[i];
    }
    const float length = std::sqrt(length_squared);
    if (!std::isfinite(length) || length < 0.1F) {
      reason = "invalid corrected field magnitude";
      return false;
    }
    const float error = std::fabs(length - 1.0F);
    squared_error += error * error;
    maximum_error = std::max(maximum_error, error);
    for (float& axis : vector) {
      axis /= length;
    }
    for (int i = 0; i < 3; ++i) {
      direction_mean[i] += vector[i] / samples.count;
      for (int j = 0; j < 3; ++j) {
        covariance[i][j] += vector[i] * vector[j] / samples.count;
      }
    }
  }
  rms_error = std::sqrt(squared_error / samples.count);
  reason = "magnetic fit error too large";
  if (!std::isfinite(rms_error) || rms_error > kCalibrationMaximumRmsError ||
      maximum_error > kCalibrationMaximumSampleError) {
    return false;
  }
  // 防止仅凭很短的一段轨迹外推参数；无需等到采样均匀覆盖整个球面。
  for (int i = 0; i < 3; ++i) {
    for (int j = 0; j < 3; ++j) {
      covariance[i][j] -= direction_mean[i] * direction_mean[j];
    }
  }
  const auto& c = covariance;
  const float determinant =
      c[0][0] * (c[1][1] * c[2][2] - c[1][2] * c[2][1]) -
      c[0][1] * (c[1][0] * c[2][2] - c[1][2] * c[2][0]) +
      c[0][2] * (c[1][0] * c[2][1] - c[1][1] * c[2][0]);
  reason = "change tilt: magnetic directions too concentrated";
  if (determinant < 0.002F) {
    return false;
  }
  reason = "checking parameter stability";
  return true;
}

/**
 * @brief 比较候选参数与本轮验证基准，避免连续小幅漂移被误认为稳定
 * @param candidate 最新候选校准参数
 * @param anchor 本轮连续验证开始时的候选参数
 * @return 各轴偏置变化不超过 1.5 uT、比例变化不超过 3% 返回 true
 */
bool CalibrationStable(const MagneticCalibration& candidate,
    const MagneticCalibration& anchor) {
  for (int i = 0; i < 3; ++i) {
    if (std::fabs(candidate.offset[i] - anchor.offset[i]) > 1.5F ||
        std::fabs(candidate.scale[i] - anchor.scale[i]) >
            anchor.scale[i] * 0.03F) {
      return false;
    }
  }
  return true;
}

}  // namespace

CompassSession::CompassSession(hal::ImuProvider* imu, hal::GpsProvider* gps)
    : imu_(imu), gps_(gps) {}

CompassSession::~CompassSession() {
  stop_.store(true);
  if (task_started_) {
    // 任务不获取 LVGL 锁；必须等关停完成，防止退出后关闭 CIT 的新会话。
    xSemaphoreTake(stopped_, portMAX_DELAY);
  }
  if (snapshots_ != nullptr) {
    vQueueDelete(snapshots_);
  }
  if (stopped_ != nullptr) {
    vSemaphoreDelete(stopped_);
  }
}

bool CompassSession::Start() {
  static_assert(std::is_trivially_copyable<CompassSnapshot>::value);
  snapshots_ = xQueueCreate(1, sizeof(CompassSnapshot));
  stopped_ = xSemaphoreCreateBinary();
  if (snapshots_ == nullptr || stopped_ == nullptr) {
    return false;
  }
  task_started_ = xTaskCreate(TaskEntry, "compass", 8 * 1024, this,
                      tskIDLE_PRIORITY + 1, nullptr) == pdPASS;
  return task_started_;
}

bool CompassSession::Read(CompassSnapshot* snapshot) {
  return snapshot != nullptr && snapshots_ != nullptr &&
         xQueueReceive(snapshots_, snapshot, 0) == pdTRUE;
}

void CompassSession::SetPaused(bool paused) {
  if (paused) {
    calibration_request_.store(-1);
  }
  paused_.store(paused);
}

void CompassSession::SetCalibrating(bool enabled) {
  calibration_request_.store(enabled ? 1 : -1);
}

void CompassSession::TaskEntry(void* context) {
  auto* session = static_cast<CompassSession*>(context);
  session->Run();
  xSemaphoreGive(session->stopped_);
  vTaskDelete(nullptr);
}

void CompassSession::Run() {
  // CIT 离开时异步关停；等待其完整释放后再启用，退出时先关停再释放锁。
  std::unique_lock<std::mutex> imu_lock(ImuSessionMutex());
  std::unique_lock<std::mutex> gps_lock(GpsSessionMutex());
  if (!InitCompassStorage()) {
    LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
        "Compass calibration storage unavailable; manual calibration remains usable\n");
  }
  MagneticCalibration calibration = GetCompassCalibration();
  CompassFusion fusion;
  CompassSnapshot snapshot;
  snapshot.calibration = calibration.ready ? CompassCalibration::kReady
                                          : CompassCalibration::kNeeded;
  LogMessage(LogLevel::kInfo, __FILE__, __LINE__,
      calibration.ready ? "Compass calibration restored\n"
                        : "No saved compass calibration\n");
  bool activated = false;
  bool collecting = false;
  // 无有效校准参数时，本次进入应用自动校准一次；取消或失败后由用户重试。
  bool auto_calibration_pending = !calibration.ready;
  int64_t calibration_started = 0;
  int64_t next_gps_read = 0;
  int64_t next_fusion_debug_ms = 0;
  CalibrationSamples calibration_samples;
  int64_t next_calibration_check = 0;
  float calibration_error = -1.0F;
  const char* calibration_reason = "waiting for samples";
  int64_t next_calibration_log = 0;
  float last_gps_second = -1.0F;
  uint8_t last_gps_minute = 255;

  // 所有硬件操作均在此任务执行，避免 I2C/UART 等待阻塞 LVGL 刷新。
  while (!stop_.load()) {
    if (paused_.load()) {
      if (activated) {
        if (imu_ != nullptr) {
          imu_->SetImuEnabled(false);
        }
        if (gps_ != nullptr) {
          gps_->SetGpsEnabled(false);
        }
        activated = false;
        collecting = false;
        fusion.Reset();
        snapshot = CompassSnapshot();
        snapshot.calibration = calibration.ready ? CompassCalibration::kReady
                                                  : CompassCalibration::kNeeded;
        xQueueOverwrite(snapshots_, &snapshot);
        calibration_request_.store(0);
        last_gps_second = -1.0F;
        last_gps_minute = 255;
      }
      vTaskDelay(pdMS_TO_TICKS(kSamplePeriodMs));
      continue;
    }
    if (!activated) {
      snapshot.imu_started = imu_ != nullptr && imu_->SetImuEnabled(true);
      snapshot.imu_failed = !snapshot.imu_started;
      if (!stop_.load() && !paused_.load()) {
        snapshot.gps_started = gps_ != nullptr && gps_->SetGpsEnabled(true);
        snapshot.gps_failed = !snapshot.gps_started;
      }
      activated = true;
      LogMessage(LogLevel::kDebug, __FILE__, __LINE__,
          "Compass sensors started (IMU: %s, GPS: %s)\n",
          snapshot.imu_started ? "ready" : "unavailable",
          snapshot.gps_started ? "ready" : "unavailable");
      xQueueOverwrite(snapshots_, &snapshot);
      continue;
    }

    const int64_t now = esp_timer_get_time() / 1000;
    const bool was_heading_ready = snapshot.heading_ready;
    int request = calibration_request_.exchange(0);
    if (auto_calibration_pending && snapshot.imu_started && request == 0) {
      request = 1;
      LogMessage(LogLevel::kDebug, __FILE__, __LINE__,
          "Compass calibration starting automatically: no saved calibration\n");
    }
    if (request != 0) {
      auto_calibration_pending = false;
    }
    if (request == 1 && snapshot.imu_started) {
      collecting = true;
      calibration_samples = {};
      calibration_error = -1.0F;
      calibration_reason = "waiting for samples";
      next_calibration_log = now;
      calibration_started = now;
      next_calibration_check = now;
      snapshot.calibration = CompassCalibration::kCollecting;
      snapshot.calibration_verifying = false;
      snapshot.calibration_completed_time_ms = 0;
      snapshot.calibration_save_failed = false;
      LogMessage(LogLevel::kDebug, __FILE__, __LINE__,
          "Compass calibration started (quality based, timeout: %lld ms)\n",
          static_cast<long long>(kCalibrationTimeoutMs));
    } else if (request == -1) {
      collecting = false;
      snapshot.calibration_verifying = false;
      snapshot.calibration_completed_time_ms = 0;
      snapshot.calibration_save_failed = false;
      snapshot.calibration = calibration.ready ? CompassCalibration::kReady
                                                : CompassCalibration::kNeeded;
    }

    hal::ImuStatus imu;
    const bool imu_read = snapshot.imu_started && imu_->ReadImuStatus(&imu);
    if (imu_read && imu.ready) {
      float magnetic[3];
      const bool magnetic_ready = ValidSample(imu);
      if (magnetic_ready) {
        for (int i = 0; i < 3; ++i) {
          magnetic[i] = (imu.magnetic_field_ut[i] - calibration.offset[i]) *
                        calibration.scale[i];
        }
        if (collecting) {
          AddCalibrationSample(calibration_samples, imu.magnetic_field_ut, now);
        }
      }
      if (fusion.Update(imu, magnetic_ready ? magnetic : nullptr)) {
        float yaw_deg = 0.0F;
        fusion.ReadAngles(snapshot.pitch_deg, snapshot.roll_deg, yaw_deg);
        snapshot.heading_deg = imu_->ConvertImuHeading(yaw_deg);
        snapshot.attitude_ready = true;
        snapshot.attitude_time_ms = imu.sample_time_us / 1000;
        snapshot.heading_ready = fusion.HeadingReady(imu.sample_time_us);
        if (snapshot.heading_ready) {
          snapshot.heading_time_ms = imu.sample_time_us / 1000;
        }
        // 限频记录进入融合的板级轴向数据，用于核对陀螺仪预测与磁场变化方向。
        if (ShouldLog(LogLevel::kDebug) && now >= next_fusion_debug_ms) {
          next_fusion_debug_ms = now + 100;
          LogMessage(LogLevel::kDebug, __FILE__, __LINE__,
              "Compass fusion input (t_us: %lld, accel_g: %.3f %.3f %.3f, "
              "gyro_new: %s, gyro_dps: %.2f %.2f %.2f, mag_new: %s, "
              "mag_ut: %.2f %.2f %.2f, calibrated: %s, "
              "heading_valid: %s, heading_deg: %.2f)\n",
              static_cast<long long>(imu.sample_time_us),
              static_cast<double>(imu.acceleration_g[0]),
              static_cast<double>(imu.acceleration_g[1]),
              static_cast<double>(imu.acceleration_g[2]),
              imu.angular_velocity_ready ? "yes" : "no",
              static_cast<double>(imu.angular_velocity_dps[0]),
              static_cast<double>(imu.angular_velocity_dps[1]),
              static_cast<double>(imu.angular_velocity_dps[2]),
              magnetic_ready ? "yes" : "no",
              static_cast<double>(imu.magnetic_field_ut[0]),
              static_cast<double>(imu.magnetic_field_ut[1]),
              static_cast<double>(imu.magnetic_field_ut[2]),
              calibration.ready ? "yes" : "no",
              snapshot.heading_ready ? "yes" : "no",
              static_cast<double>(snapshot.heading_deg));
        }
      }
    }
    if (now - snapshot.attitude_time_ms > kHeadingStaleMs) {
      snapshot.attitude_ready = false;
    }
    if (now - snapshot.heading_time_ms > kHeadingStaleMs) {
      snapshot.heading_ready = false;
    }
    if (collecting && now >= next_calibration_check) {
      next_calibration_check = now + kCalibrationCheckIntervalMs;
      const bool new_samples =
          calibration_samples.updates != calibration_samples.checked_updates;
      const bool fresh_samples = calibration_samples.count != 0 &&
          now - calibration_samples.last_sample_ms <= kHeadingStaleMs;
      // HAL 某次读取可能暂无数据；评估已收到的新采样，不要求检查瞬间读取成功。
      // 暂无新采样时保留验证进度，但绝不重复计算同一批数据来增加成功次数。
      if (new_samples && fresh_samples) {
        MagneticCalibration candidate;
        const bool quality_ready = EvaluateCalibration(calibration_samples,
            candidate, calibration_error, calibration_reason);
        calibration_samples.checked_updates = calibration_samples.updates;
        if (quality_ready) {
          if (calibration_samples.stable_checks == 0 ||
              !CalibrationStable(candidate, calibration_samples.anchor)) {
            calibration_samples.anchor = candidate;
            calibration_samples.stable_checks = 1;
          } else {
            ++calibration_samples.stable_checks;
          }
        } else {
          calibration_samples.stable_checks = 0;
        }
        snapshot.calibration_verifying = quality_ready;
        if (calibration_samples.stable_checks >= kCalibrationStableChecks) {
          candidate.ready = true;
          calibration = candidate;
          fusion.Reset();
          snapshot.calibration_save_failed = !SaveCompassCalibration(calibration);
          if (snapshot.calibration_save_failed) {
            LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
                "Compass calibration is active in RAM but NVS save failed\n");
          }
          collecting = false;
          snapshot.calibration = CompassCalibration::kReady;
          snapshot.calibration_verifying = false;
          snapshot.calibration_completed_time_ms = now;
          snapshot.heading_ready = false;
          LogMessage(LogLevel::kInfo, __FILE__, __LINE__,
              "Compass calibration completed (elapsed: %lld ms, samples: %u, "
              "RMS error: %.3f)\n", static_cast<long long>(now - calibration_started),
              calibration_samples.count, static_cast<double>(calibration_error));
        }
      } else if (!fresh_samples) {
        calibration_samples.stable_checks = 0;
        snapshot.calibration_verifying = false;
        calibration_reason = "waiting for fresh movement samples";
      }
      if (collecting && now >= next_calibration_log) {
        next_calibration_log = now + 5000;
        LogMessage(LogLevel::kDebug, __FILE__, __LINE__,
            "Compass calibration pending: %s (samples: %u, stable: %u/%u, "
            "RMS error: %.3f; -1 means not evaluated)\n", calibration_reason,
            calibration_samples.count, calibration_samples.stable_checks,
            kCalibrationStableChecks, static_cast<double>(calibration_error));
      }
    }
    if (collecting && now - calibration_started >= kCalibrationTimeoutMs) {
      collecting = false;
      snapshot.calibration = CompassCalibration::kFailed;
      snapshot.calibration_verifying = false;
      LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
          "Compass calibration timed out: %s (samples: %u, RMS error: %.3f); "
          "previous calibration retained\n", calibration_reason,
          calibration_samples.count, static_cast<double>(calibration_error));
    }

    if (was_heading_ready != snapshot.heading_ready) {
      LogMessage(LogLevel::kDebug, __FILE__, __LINE__,
          "Compass heading %s\n", snapshot.heading_ready ? "available" : "unavailable");
    }

    if (snapshot.gps_started && now >= next_gps_read) {
      hal::GpsStatus gps;
      if (gps_->ReadGpsStatus(&gps)) {
        // HAL 会返回缓存；只有 UTC 前进才认为接收到新的定位数据。
        if (gps.utc.ready && (gps.utc.second != last_gps_second ||
                                 gps.utc.minute != last_gps_minute)) {
          snapshot.gps_time_ms = now;
          last_gps_second = gps.utc.second;
          last_gps_minute = gps.utc.minute;
        }
        snapshot.gps = gps;
      }
      next_gps_read = now + 1000;
    }
    if (snapshot.gps_time_ms == 0 || now - snapshot.gps_time_ms > kGpsStaleMs) {
      snapshot.gps.positioned = false;
    }
    xQueueOverwrite(snapshots_, &snapshot);
    vTaskDelay(pdMS_TO_TICKS(kSamplePeriodMs));
  }
  if (activated) {
    if (imu_ != nullptr) {
      imu_->SetImuEnabled(false);
    }
    if (gps_ != nullptr) {
      gps_->SetGpsEnabled(false);
    }
  }
  LogMessage(LogLevel::kDebug, __FILE__, __LINE__, "Compass sensors stopped\n");
}

}  // namespace lilygo_box::app
