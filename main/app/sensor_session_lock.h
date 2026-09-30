/*
 * @Description: 应用间 IMU 与 GPS 完整采集会话的互斥访问
 * @Author: LILYGO_L
 * @Date: 2026-09-28 00:00:00
 * @LastEditTime: 2026-09-28 00:00:00
 * @License: GPL 3.0
 */
#pragma once

#include <mutex>

namespace lilygo_box::app {

/**
 * @brief 获取 IMU 会话锁，后台任务从启用前持有到禁用后
 *
 * 单次 I2C 事务锁无法防止旧页面的异步收尾关闭新页面正在使用的设备。
 * 此锁仅由后台采集任务使用，不在 LVGL 采样回调中获取。
 * @return 应用共享的 IMU 会话互斥锁引用
 */
inline std::mutex& ImuSessionMutex() {
  static std::mutex mutex;
  return mutex;
}

/**
 * @brief 获取 GPS 会话锁；同时使用两种传感器时先获取 IMU 锁
 * @return 应用共享的 GPS 会话互斥锁引用
 */
inline std::mutex& GpsSessionMutex() {
  static std::mutex mutex;
  return mutex;
}

}  // namespace lilygo_box::app
