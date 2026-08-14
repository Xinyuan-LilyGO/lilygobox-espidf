/*
 * @Description: 系统电源状态持久化
 * @Author: LILYGO_L
 * @Date: 2026-08-14 00:00:00
 * @LastEditTime: 2026-08-14 00:00:00
 * @License: GPL 3.0
 */
#pragma once

namespace lilygo_box::app {

/**
 * @brief 提前初始化关机启动判定所需的存储和电源状态缓存
 * @return 电源状态缓存可用且持久化数据读取正常时返回 true
 */
bool InitPowerStateStorage();

/**
 * @brief 读取用户是否已经请求关机
 * @param requested 输出持久化的用户关机状态
 * @return 电源状态缓存可读取时返回 true
 */
bool ReadPowerOffRequested(bool* requested);

/**
 * @brief 持久化用户关机状态
 * @param requested 是否保持逻辑关机状态
 * @return 无变化或 TLV 数据提交成功时返回 true
 */
bool WritePowerOffRequested(bool requested);

}  // namespace lilygo_box::app
