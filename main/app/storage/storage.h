/*
 * @Description: NVS 与 LittleFS 持久化统一管理，提供 RAM 缓存和熄屏落盘
 * @Author: LILYGO_L
 * @Date: 2026-07-03 00:00:00
 * @LastEditTime: 2026-07-17 18:40:56
 * @License: GPL 3.0
 */
#pragma once

namespace lilygo_box::app {

/**
 * @brief 初始化存储协调器并将 NVS 偏好和 RF 聊天记录加载到 RAM
 */
void InitStorage();

/**
 * @brief 判断是否存在尚未写入 NVS 或 LittleFS 的 RAM 修改
 * @return 存在待落盘修改返回 true
 */
bool HasPendingStorageWrites();

/**
 * @brief 在物理屏幕完全关闭后同步提交全部 RAM 修改
 *
 * 调用方必须先暂停 LVGL 硬件刷新，并确认 EnterDeviceSleep() 返回成功；
 * 本函数返回前不得唤醒屏幕。失败项目保留在 RAM，
 * 等待熄屏重试。
 *
 * @return 所有待处理修改均已持久化返回 true
 */
bool FlushPendingStorageAfterScreenOff();

/**
 * @brief 在重启或关机前冻结新的 RAM 缓存更新
 * @return 冻结成功返回 true
 */
bool FreezeStorageUpdatesForShutdown();

/**
 * @brief 终止流程取消后恢复接受 RAM 缓存更新
 */
void ResumeStorageUpdatesAfterShutdownFailure();

/**
 * @brief 在物理屏幕关闭后同步清除默认 NVS 和应用 LittleFS 并重启设备
 *
 * 调用方必须先确保物理屏幕已经完全关闭。
 * @return 擦除失败并返回调用方时返回 false，成功后设备会立即重启
 */
bool FactoryResetAfterScreenOff();

}  // namespace lilygo_box::app
