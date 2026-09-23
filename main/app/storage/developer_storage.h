/*
 * @Description: 开发者偏好存储
 * @Author: LILYGO_L
 * @Date: 2026-09-23 00:00:00
 * @LastEditTime: 2026-09-23 17:49:29
 * @License: GPL 3.0
 */
#pragma once

#include "base/logger.h"
#include "cpp_bus_driver.h"
#include "lilygo_device_driver.h"

namespace lilygo_box::app {

struct DeveloperPreferences {
  // 各模块最低日志输出等级；启动加载时沿用各自 logger 当前默认值。
  LogLevel log_level = LogLevel::kInfo;
  cpp_bus_driver::Logger::LogLevel cpp_bus_driver_log_level =
      cpp_bus_driver::Logger::LogLevel::kInfo;
  lilygo_device_driver::LogLevel lilygo_device_driver_log_level =
      lilygo_device_driver::LogLevel::kInfo;
};

/**
 * @brief 初始化开发者偏好缓存，从 NVS 加载并应用日志等级
 */
void InitDeveloperCache();

/**
 * @brief 读取开发者偏好
 * @return 当前开发者偏好
 */
DeveloperPreferences GetDeveloperPreferences();

/**
 * @brief 比较并更新开发者偏好，立即写入 NVS 并应用缓存中的日志等级
 * @param preferences 新的开发者偏好
 * @return 无变化或 NVS 提交成功返回 true，否则返回 false
 */
bool UpdateDeveloperPreferences(const DeveloperPreferences& preferences);

}  // namespace lilygo_box::app
