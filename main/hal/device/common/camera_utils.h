/*
 * @Description: 摄像头预览公共参数与生命周期辅助接口
 * @Author: LILYGO_L
 * @Date: 2026-08-28 00:00:00
 * @LastEditTime: 2026-08-28 00:00:00
 * @License: GPL 3.0
 */
#pragma once

#include <cstdint>

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "hal/ppa/ppa_srm_helper.h"

namespace lilygo_box::hal::camera_utils {

inline constexpr uint32_t kPreviewTaskStackBytes = 6 * 1024;
inline constexpr UBaseType_t kPreviewTaskPriority = 5;
inline constexpr uint32_t kBufferCount = 2;
inline constexpr uint32_t kFrameIntervalMs = 10;
inline constexpr uint32_t kStopWaitTimeoutMs = 5000;
inline constexpr uint32_t kSensorReadyPollIntervalMs = 100;
inline constexpr uint32_t kStartupTimeoutMs = 3000;
inline constexpr uint32_t kPowerCycleOffDelayMs = 20;
inline constexpr uint32_t kOutputClearFrameCount = 3;
inline constexpr uint32_t kWarmupFrameCount = 5;

/**
 * @brief 获取摄像头启动流程已经消耗的时间
 * @param start_tick 启动流程开始时的系统节拍
 * @return 已消耗时间，单位为毫秒
 */
uint32_t StartupElapsedMs(TickType_t start_tick);

/**
 * @brief 判断摄像头启动流程是否已经达到总超时
 * @param start_tick 启动流程开始时的系统节拍
 * @return 达到总超时返回 true，否则返回 false
 */
bool StartupTimedOut(TickType_t start_tick);

/**
 * @brief 获取摄像头启动流程剩余时间
 * @param start_tick 启动流程开始时的系统节拍
 * @return 剩余时间，单位为毫秒
 */
uint32_t StartupRemainingMs(TickType_t start_tick);

/**
 * @brief 判断系统错误是否可能由摄像头瞬时硬件状态引起
 * @param error errno 错误值
 * @return 适合通过完整摄像头电源重启恢复返回 true
 */
bool IsRetryableIoError(int error);

/**
 * @brief 判断 ESP Video 初始化错误是否适合重新上电重试
 * @param error ESP-IDF 错误值
 * @return 适合通过完整摄像头电源重启恢复返回 true
 */
bool IsRetryableVideoError(esp_err_t error);

/**
 * @brief 将屏幕旋转角度规整到摄像头预览支持的范围
 * @param angle 屏幕旋转角度
 * @return 规整后的角度
 */
int NormalizePreviewRotationAngle(int angle);

/**
 * @brief 将屏幕旋转角度转换为 PPA 旋转角度
 * @param angle 屏幕旋转角度
 * @return PPA 旋转角度
 */
ppa_srm_rotation_angle_t ToPreviewPpaRotation(int angle);

}  // namespace lilygo_box::hal::camera_utils
