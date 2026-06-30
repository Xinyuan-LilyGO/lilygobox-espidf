/**
 * @Description: Settings sound and haptics NVS storage helpers
 * @Author: LILYGO_L
 * @Date: 2026-06-25 00:00:00
 * @LastEditTime: 2026-06-25 00:00:00
 * @License: GPL 3.0
 */
#pragma once

namespace lilygo_box::app {

// 声音与触感设置用户偏好。
struct SoundPreferences {
  // 扬声器音量百分比，范围 0~100。
  int volume_percent = 60;
  // 系统触感开关。
  bool haptics_enabled = true;
  // 振动强度百分比，范围 0~100。
  int haptic_strength_percent = 45;
};

/**
 * @brief 将声音与触感设置偏好写入 ESP32-P4 NVS
 * @param preferences 声音与触感设置偏好
 * @return 保存成功返回 true，否则返回 false
 */
bool SaveSoundPreferencesToNvs(const SoundPreferences& preferences);

/**
 * @brief 从 ESP32-P4 NVS 读取声音与触感设置偏好
 * @param preferences 声音与触感设置偏好输出地址
 * @return 读取成功返回 true，没有保存内容或读取异常返回 false
 */
bool LoadSoundPreferencesFromNvs(SoundPreferences* preferences);

}  // namespace lilygo_box::app
