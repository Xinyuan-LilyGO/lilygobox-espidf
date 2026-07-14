/*
 * @Description: 设备名称读取与更新接口
 * @Author: LILYGO_L
 * @Date: 2026-05-18 12:08:00
 * @LastEditTime: 2026-05-18 12:08:00
 * @License: GPL 3.0
 */
#pragma once

#include <cstddef>

namespace lilygo_box::app {

inline constexpr size_t kMaxDeviceNameLength = 31;

/**
 * @brief 获取用户设置的本机设备名称
 * @return 已设置的设备名称字符串，未设置时返回空字符串
 */
const char* ConfiguredDeviceName();

/**
 * @brief 设置当前本机设备名称
 * @param name 新设备名称
 * @return 设置成功返回 true，否则返回 false
 */
bool SetConfiguredDeviceName(const char* name);

}  // namespace lilygo_box::app
