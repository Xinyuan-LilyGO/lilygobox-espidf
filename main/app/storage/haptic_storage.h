/**
 * @Description: 振动偏好存储，内部维护内存缓存
 * @Author: LILYGO_L
 * @Date: 2026-06-25 00:00:00
 * @LastEditTime: 2026-07-16 22:35:14
 * @License: GPL 3.0
 */
#pragma once

namespace lilygo_box::app {

// 振动设置用户偏好。
struct HapticPreferences {
  // 是否启用系统触感反馈。
  bool enabled = true;
  // 触感强度百分比，范围为 0～100。
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
 * @brief 比较并更新振动偏好，存在变化时立即写入 NVS
 * @param preferences 新的振动偏好
 * @return 无变化或 NVS 提交成功返回 true，否则返回 false
 */
bool UpdateHapticPreferences(const HapticPreferences& preferences);

}  // namespace lilygo_box::app
