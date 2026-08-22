/*
 * @Description: NVS 与 LittleFS 即时持久化统一管理
 * @Author: LILYGO_L
 * @Date: 2026-07-03 00:00:00
 * @LastEditTime: 2026-07-18 00:00:00
 * @License: GPL 3.0
 */
#pragma once

#include "hal/providers/radio/radio_types.h"

namespace lilygo_box::app {

/**
 * @brief 初始化存储协调器并将 NVS 偏好和 Radio 聊天记录加载到 RAM
 * @param supported_radio_chips 当前板型允许持久化的可选射频芯片
 * @param primary_radio_chip 自动识别到的主射频芯片
 */
void InitStorage(radio::ChipMask supported_radio_chips,
    radio::ChipType primary_radio_chip);

/**
 * @brief 判断是否存在尚未写入 NVS 或 LittleFS 的 RAM 修改
 * @return 存在待落盘修改返回 true
 */
bool HasPendingStorageWrites();

/**
 * @brief 在关机或重启前最终提交全部待处理数据
 *
 * 调用方必须先冻结新的存储更新并关闭物理屏幕。NVS 设置与 LittleFS
 * 聊天记录均已即时保存，此函数只负责对失败的数据执行最终重试。
 *
 * @return 所有待处理数据均已持久化返回 true
 */
bool FlushPendingStorageBeforeShutdown();

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
 * @brief 在物理屏幕关闭后擦除全部 NVS 和 LittleFS 分区
 *
 * 擦除成功后立即重启设备。
 *
 * 调用方必须先确保物理屏幕已经完全关闭。
 * @return 擦除失败并返回调用方时返回 false，成功后设备会立即重启
 */
bool FactoryResetAfterScreenOff();

}  // namespace lilygo_box::app
