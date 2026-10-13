/*
 * @Description: 设备名称读取与更新接口
 * @Author: LILYGO_L
 * @Date: 2026-05-18 12:08:00
 * @LastEditTime: 2026-10-13 12:00:00
 * @License: GPL 3.0
 */
#pragma once

#include <cstddef>

namespace lilygo_box::app {

inline constexpr size_t kMaxDeviceNameLength = 64;

/**
 * @brief 在界面与网络任务启动前加载名称，并生成 LilygoBox 与完整 MAC 默认名称
 * @return 名称缓存初始化成功返回 true，存储不可用时返回 false
 */
bool InitDeviceIdentity();

/**
 * @brief 获取本机显示名称，优先使用持久化的用户名称
 * @return 用户名称或 LilygoBox 与完整 MAC 默认名称
 */
const char* ConfiguredDeviceName();

/**
 * @brief 复制设备名称的线程安全快照，供网络任务读取
 * @param output 名称输出缓冲区
 * @param capacity 输出缓冲区容量，包含结尾空字符
 */
void CopyConfiguredDeviceName(char* output, size_t capacity);

/**
 * @brief 设置本机设备名称并通过统一存储事务保存到 NVS
 * @param name 新设备名称
 * @return 设置成功返回 true，否则返回 false
 */
bool SetConfiguredDeviceName(const char* name);

}  // namespace lilygo_box::app
