/**
 * @Description: 显示偏好缓存与实时 NVS 持久化
 * @Author: LILYGO_L
 * @Date: 2026-06-25 00:00:00
 * @LastEditTime: 2026-07-16 22:35:14
 * @License: GPL 3.0
 */
#pragma once

namespace lilygo_box::app {

inline constexpr int kUserDisplayBrightnessMinPercent = 10;
inline constexpr int kUserDisplayBrightnessMaxPercent = 100;
// 自动锁屏等待时间使用该值时，屏幕不会因空闲超时而锁定。
inline constexpr int kDisplayLockTimeoutDisabledSeconds = 0;

// 显示设置用户偏好。
struct DisplayPreferences {
  // 用户屏幕亮度百分比，范围为 10～100；0 仅供系统黑屏使用。
  int brightness_percent = 90;
  // 自动锁屏等待秒数，0 表示关闭自动锁屏。
  int lock_timeout_seconds = 5 * 60;
  // 屏幕旋转角度，仅允许 0、90、180 或 270。
  int screen_rotation_angle = 0;
  // 锁屏页面是否允许双击触摸屏亮屏或熄屏。
  bool double_tap_to_wake = true;
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
 * @brief 比较并更新显示偏好，存在变化时立即写入 NVS
 * @param preferences 新的显示偏好
 * @return 无变化或 NVS 提交成功返回 true，否则返回 false
 */
bool UpdateDisplayPreferences(const DisplayPreferences& preferences);

}  // namespace lilygo_box::app
