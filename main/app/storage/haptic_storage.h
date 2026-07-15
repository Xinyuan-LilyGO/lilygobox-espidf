/**
 * @Description: 振动偏好存储，内部维护内存缓存
 * @Author: LILYGO_L
 * @Date: 2026-06-25 00:00:00
 * @LastEditTime: 2026-07-03 00:00:00
 * @License: GPL 3.0
 */
#pragma once

namespace lilygo_box::app {

// 振动设置用户偏好。
struct HapticPreferences {
  bool enabled = true;
  int strength_percent = 90;
};

/**
 * @brief 初始化振动偏好缓存，从 NVS 加载到内存
 */
void InitHapticCache();

/**
 * @brief 读取振动偏好（纯内存，零 NVS 访问）
 * @return 振动偏好
 */
HapticPreferences GetHapticPreferences();

/**
 * @brief 更新振动偏好并持久化到 NVS
 * @param preferences 新的振动偏好
 * @return 更新成功返回 true
 */
bool UpdateHapticPreferences(const HapticPreferences& preferences);

}  // namespace lilygo_box::app
