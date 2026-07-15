/*
 * @Description: 偏好存储统一管理，提供初始化和异步写入
 * @Author: LILYGO_L
 * @Date: 2026-07-03 00:00:00
 * @LastEditTime: 2026-07-15 15:53:19
 * @License: GPL 3.0
 */
#pragma once

#include <functional>

namespace lilygo_box::app {

/**
 * @brief 初始化所有偏好缓存
 * 启动时调用一次，依次从 NVS 加载 display / haptic / sound 到内存缓存。
 * 之后所有 Get* 调用零 NVS 访问。
 */
void InitStorage();

/**
 * @brief 在独立任务中异步执行存储操作
 * @param name 任务名称
 * @param handler 存储操作回调
 * @return 启动成功返回 true
 */
bool StartStorageTask(const char* name, std::function<void()> handler);

/**
 * @brief 异步清除默认 NVS 并重启设备
 * @return 任务启动成功返回 true，否则返回 false
 */
bool StartFactoryReset();

}  // namespace lilygo_box::app
