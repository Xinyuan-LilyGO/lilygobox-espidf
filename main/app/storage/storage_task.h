/*
 * @Description: Settings storage async task helpers
 * @Author: LILYGO_L
 * @Date: 2026-06-25 00:00:00
 * @LastEditTime: 2026-06-25 00:00:00
 * @License: GPL 3.0
 */
#pragma once

#include <functional>

namespace lilygo_box::app {

/**
 * @brief 在后台 FreeRTOS task 中执行一次存储操作
 * @param name 任务名称，传 nullptr 时使用默认名称
 * @param handler 存储处理函数，任务中调用
 * @return 任务创建成功返回 true，否则返回 false
 */
bool StartStorageTask(const char* name, std::function<void()> handler);

}  // namespace lilygo_box::app
