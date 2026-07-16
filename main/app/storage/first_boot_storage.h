/*
 * @Description: 首次开机欢迎页完成标志存储接口
 * @Author: LILYGO_L
 * @Date: 2026-07-15 00:00:00
 * @LastEditTime: 2026-07-16 22:35:14
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
 * @brief 仅更新 RAM 完成标记，屏幕完全关闭后统一写入 NVS
 * @return RAM 缓存接收成功返回 true
 */
bool MarkFirstBootCompleted();

}  // namespace lilygo_box::app
