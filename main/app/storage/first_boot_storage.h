/*
 * @Description: 首次开机欢迎页完成标志存储接口
 * @Author: LILYGO_L
 * @Date: 2026-07-15 00:00:00
 * @LastEditTime: 2026-07-15 00:00:00
 * @License: GPL 3.0
 */
#pragma once

namespace lilygo_box::app {

/**
 * @brief 从 NVS 加载首次开机欢迎页完成标志
 */
void InitFirstBootCache();

/**
 * @brief 判断首次开机欢迎流程是否已经完成
 * @return 已经完成返回 true，否则返回 false
 */
bool IsFirstBootCompleted();

/**
 * @brief 将首次开机欢迎流程完成标志持久化到 NVS
 * @return 写入并提交成功返回 true，否则返回 false
 */
bool MarkFirstBootCompleted();

}  // namespace lilygo_box::app
