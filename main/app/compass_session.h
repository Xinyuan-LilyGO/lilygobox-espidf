/*
 * @Description: 指南针后台采样、磁场校准与状态快照接口
 * @Author: LILYGO_L
 * @Date: 2026-09-28 00:00:00
 * @LastEditTime: 2026-09-28 00:00:00
 * @License: GPL 3.0
 */
#pragma once

#include <atomic>
#include <cstdint>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "hal/providers/gps_provider.h"
#include "hal/providers/imu_provider.h"

namespace lilygo_box::app {

enum class CompassCalibration { kNeeded, kCollecting, kReady, kFailed };

// 仅包含值类型，可通过长度为一的队列传递最新状态。
struct CompassSnapshot {
  bool imu_started = false;
  bool imu_failed = false;
  bool heading_ready = false;
  float heading_deg = 0.0F;
  bool attitude_ready = false;
  int64_t attitude_time_ms = 0;
  float pitch_deg = 0.0F;
  float roll_deg = 0.0F;
  int64_t heading_time_ms = 0;
  CompassCalibration calibration = CompassCalibration::kNeeded;
  bool calibration_verifying = false;
  bool calibration_save_failed = false;
  int64_t calibration_completed_time_ms = 0;
  bool gps_started = false;
  bool gps_failed = false;
  int64_t gps_time_ms = 0;
  hal::GpsStatus gps;
};

// 生命周期由指南针页面管理；后台任务不访问 LVGL 对象。
class CompassSession final {
 public:
  /**
   * @brief 创建指南针采集会话，保存设备接口但不启动任务
   * @param imu IMU 数据与控制接口，为 nullptr 时报告传感器不可用
   * @param gps GPS 数据与控制接口，为 nullptr 时报告 GPS 不可用
   */
  CompassSession(hal::ImuProvider* imu, hal::GpsProvider* gps);

  /**
   * @brief 请求停止采集，等待后台硬件收尾并释放任务通信资源
   */
  ~CompassSession();

  CompassSession(const CompassSession&) = delete;
  CompassSession& operator=(const CompassSession&) = delete;

  /**
   * @brief 启动后台采集任务
   * @return 任务创建成功返回 true，通信资源或任务创建失败返回 false
   */
  bool Start();

  /**
   * @brief 非阻塞读取最新指南针状态快照
   * @param snapshot 状态快照输出地址
   * @return 读取到新快照返回 true，参数无效或暂无新快照返回 false
   */
  bool Read(CompassSnapshot* snapshot);

  /**
   * @brief 请求后台暂停或恢复传感器采集
   * @param paused true 表示暂停采集、关闭传感器并取消校准；false 表示恢复采集
   */
  void SetPaused(bool paused);

  /**
   * @brief 请求后台开始或取消本轮三轴磁场校准
   * @param enabled true 表示开始校准，false 表示取消校准并保留原有有效参数
   */
  void SetCalibrating(bool enabled);

 private:
  /**
   * @brief 运行后台采集任务，硬件收尾后通知页面并删除任务
   * @param context 指向当前 CompassSession 实例的任务上下文
   */
  static void TaskEntry(void* context);

  /**
   * @brief 执行设备启停、磁场采集与 GPS 轮询
   */
  void Run();

  hal::ImuProvider* imu_;
  hal::GpsProvider* gps_;
  QueueHandle_t snapshots_ = nullptr;
  SemaphoreHandle_t stopped_ = nullptr;
  bool task_started_ = false;
  std::atomic<bool> stop_{false};
  std::atomic<bool> paused_{false};
  // 0 无请求，1 开始校准，-1 取消校准。
  std::atomic<int> calibration_request_{0};
};

}  // namespace lilygo_box::app
