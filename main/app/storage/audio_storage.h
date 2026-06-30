/*
 * @Description: Settings audio NVS storage helpers
 * @Author: LILYGO_L
 * @Date: 2026-06-25 00:00:00
 * @LastEditTime: 2026-06-25 00:00:00
 * @License: GPL 3.0
 */
#pragma once

namespace lilygo_box::app {

// 音频设置用户偏好。
struct AudioPreferences {
  // 扬声器音量百分比，范围 0~100。
  int volume_percent = 60;
};

/**
 * @brief 将音频设置偏好写入 ESP32-P4 NVS
 * @param preferences 音频设置偏好
 * @return 保存成功返回 true，否则返回 false
 */
bool SaveAudioPreferencesToNvs(const AudioPreferences& preferences);

/**
 * @brief 从 ESP32-P4 NVS 读取音频设置偏好
 * @param preferences 音频设置偏好输出地址
 * @return 读取成功返回 true，没有保存内容或读取异常返回 false
 */
bool LoadAudioPreferencesFromNvs(AudioPreferences* preferences);

}  // namespace lilygo_box::app
