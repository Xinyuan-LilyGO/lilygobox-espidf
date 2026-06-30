/**
 * @Description: Settings display NVS storage helpers
 * @Author: LILYGO_L
 * @Date: 2026-06-25 00:00:00
 * @LastEditTime: 2026-06-25 00:00:00
 * @License: GPL 3.0
 */
#pragma once

namespace lilygo_box::app {

// 显示设置用户偏好。
struct DisplayPreferences {
  // 屏幕亮度百分比，范围 0~100，硬件层会保留最低亮度保护。
  int brightness_percent = 70;
  // 自动锁屏等待时间，单位秒，0 表示关闭自动锁屏。
  int lock_timeout_seconds = 5 * 60;
};

/**
 * @brief 将显示设置偏好写入 ESP32-P4 NVS
 * @param preferences 显示设置偏好
 * @return 保存成功返回 true，否则返回 false
 */
bool SaveDisplayPreferencesToNvs(const DisplayPreferences& preferences);

/**
 * @brief 从 ESP32-P4 NVS 读取显示设置偏好
 * @param preferences 显示设置偏好输出地址
 * @return 读取成功返回 true，没有保存内容或读取异常返回 false
 */
bool LoadDisplayPreferencesFromNvs(DisplayPreferences* preferences);

}  // namespace lilygo_box::app
