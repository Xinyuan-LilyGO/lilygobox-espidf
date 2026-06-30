/*
 * @Description: Settings haptic NVS storage helpers
 * @Author: LILYGO_L
 * @Date: 2026-06-25 00:00:00
 * @LastEditTime: 2026-06-25 00:00:00
 * @License: GPL 3.0
 */
#pragma once

namespace lilygo_box::app {

// 振动设置用户偏好。
struct HapticPreferences {
  // 系统触感开关。
  bool enabled = true;
  // 振动强度百分比，范围 0~100。
  int strength_percent = 45;
};

/**
 * @brief 将振动设置偏好写入 ESP32-P4 NVS
 * @param preferences 振动设置偏好
 * @return 保存成功返回 true，否则返回 false
 */
bool SaveHapticPreferencesToNvs(const HapticPreferences& preferences);

/**
 * @brief 从 ESP32-P4 NVS 读取振动设置偏好
 * @param preferences 振动设置偏好输出地址
 * @return 读取成功返回 true，没有保存内容或读取异常返回 false
 */
bool LoadHapticPreferencesFromNvs(HapticPreferences* preferences);

}  // namespace lilygo_box::app
