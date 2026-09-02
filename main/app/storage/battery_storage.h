/*
 * @Description: Battery capacity preference storage
 * @Author: LILYGO_L
 * @Date: 2026-09-01 00:00:00
 * @LastEditTime: 2026-09-01 00:00:00
 * @License: GPL 3.0
 */
#pragma once

#include <cstdint>

namespace lilygo_box::app {

inline constexpr int kMinimumBatteryCapacityMah = 1;
inline constexpr int kMaximumBatteryCapacityMah = UINT16_MAX;
inline constexpr int kDefaultBatteryCapacityMah = 1000;

// 用户配置的电池额定容量。
struct BatteryPreferences {
  int capacity_mah = kDefaultBatteryCapacityMah;
};

/**
 * @brief 提前初始化电池容量缓存并从 NVS 加载配置
 * @return 缓存可用且持久化数据读取正常时返回 true
 */
bool InitBatteryStorage();

/**
 * @brief 读取当前电池容量偏好
 * @return 已规范化的电池容量偏好
 */
BatteryPreferences GetBatteryPreferences();

/**
 * @brief 更新电池容量偏好并立即持久化到 NVS
 * @param preferences 新的电池容量偏好
 * @return 无变化或 NVS 提交成功返回 true，否则返回 false
 */
bool UpdateBatteryPreferences(const BatteryPreferences& preferences);

}  // namespace lilygo_box::app
