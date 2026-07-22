/**
 * @Description: 声音偏好存储，内部维护内存缓存
 * @Author: LILYGO_L
 * @Date: 2026-06-25 00:00:00
 * @LastEditTime: 2026-07-16 22:35:14
 * @License: GPL 3.0
 */
#pragma once

namespace lilygo_box::app {

// 声音设置用户偏好。
struct SoundPreferences {
  // 系统输出音量百分比，范围为 0～100。
  int volume_percent = 90;
};

/**
 * @brief 初始化声音偏好缓存，从 NVS 加载到内存
 */
void InitSoundCache();

/**
 * @brief 读取声音偏好（纯内存，零 NVS 访问）
 * @return 声音偏好
 */
SoundPreferences GetSoundPreferences();

/**
 * @brief 比较并更新声音偏好，存在变化时立即写入 NVS
 * @param preferences 新的声音偏好
 * @return 无变化或 NVS 提交成功返回 true，否则返回 false
 */
bool UpdateSoundPreferences(const SoundPreferences& preferences);

}  // namespace lilygo_box::app
