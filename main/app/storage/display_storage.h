/**
 * @Description: 显示偏好存储，内部维护内存缓存，零 NVS 读取
 * @Author: LILYGO_L
 * @Date: 2026-06-25 00:00:00
 * @LastEditTime: 2026-07-03 00:00:00
 * @License: GPL 3.0
 */
#pragma once

namespace lilygo_box::app {

// 显示设置用户偏好。
struct DisplayPreferences {
  int brightness_percent = 70;
  int lock_timeout_seconds = 5 * 60;
  int screen_rotation_angle = 0;
};

/**
 * @brief 初始化显示偏好缓存，从 NVS 加载到内存
 */
void InitDisplayCache();

/**
 * @brief 读取显示偏好（纯内存，零 NVS 访问）
 * @return 显示偏好
 */
DisplayPreferences GetDisplayPreferences();

/**
 * @brief 更新显示偏好并持久化到 NVS
 * @param preferences 新的显示偏好
 * @return 更新成功返回 true
 */
bool UpdateDisplayPreferences(const DisplayPreferences& preferences);

}  // namespace lilygo_box::app
