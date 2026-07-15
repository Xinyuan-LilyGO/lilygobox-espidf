/*
 * @Description: 系统应用生命周期与启动协调接口
 * @Author: LILYGO_L
 * @Date: 2026-05-10 13:27:05
 * @LastEditTime: 2026-05-12 23:05:00
 * @License: GPL 3.0
 */
#pragma once

#include <atomic>
#include <cstdint>

#include "app/storage/display_storage.h"
#include "hal/lvgl_port.h"
#include "hal/device_provider_factory.h"
#include "ui/ui_manager.h"

namespace lilygo_box {

class Application final {
 public:
  Application();

  /**
   * @brief 初始化当前设备、LVGL 和本地 UI
   * @return 初始化成功返回 true，否则返回 false
   */
  bool Init();

  /**
   * @brief 运行应用主循环
   */
  void Run();

 private:
  /**
   * @brief 启动后自动连接 WLAN 的后台任务入口
   * @param context Application 实例
   */
  static void StartupWifiAutoConnectTaskEntry(void* context);

  /**
   * @brief 根据 NVS 中保存的 WLAN 偏好执行启动自动连接
   */
  void RunStartupWifiAutoConnectTask();

  /**
   * @brief 锁屏后台任务入口
   * @param context Application 实例
   */
  static void ScreenLockTaskEntry(void* context);

  /**
   * @brief 监控触摸和 BOOT 键并执行锁屏流程
   */
  void RunScreenLockTask();

  /**
   * @brief 显示锁屏页面并让设备进入休眠
   * @return 进入休眠成功返回 true，否则返回 false
   */
  bool EnterScreenLockSleep();

  /**
   * @brief 恢复屏幕亮度并显示已准备好的锁屏页面
   */
  void WakeScreenFromLock();

  /**
   * @brief 长按锁屏键时唤醒锁屏并显示关机菜单
   * @return 显示成功返回 true，否则返回 false
   */
  bool ShowPowerMenuFromLockButton();

  /**
   * @brief 单击锁屏键前清理已经显示的关机菜单
   * @return 关闭了关机菜单返回 true，否则返回 false
   */
  bool HidePowerMenuFromLockButton();

  /**
   * @brief 让设备进入深度睡眠级关断状态并重启
   */
  void RestartDevice();

  /**
   * @brief 让设备进入深度睡眠级关断状态
   */
  void PowerOffDevice();

  /**
   * @brief 处理关机菜单被遮罩点击或滑动手势关闭后的状态恢复
   */
  void HandlePowerMenuDismissed();

  /**
   * @brief 锁屏页面亮屏态下立即进入休眠
   * @return 进入休眠成功返回 true，否则返回 false
   */
  bool SleepAwakeLockScreenNow();

  /**
   * @brief 锁屏页面亮屏态下按超时流程重新进入休眠
   * @return 进入休眠成功返回 true，否则返回 false
   */
  bool SleepAwakeLockScreenWithTimeout();

  /**
   * @brief 退出锁屏页面并恢复 LVGL 输入
   */
  void UnlockScreen();

  /**
   * @brief 判断触摸轨迹是否满足上滑解锁手势
   * @param start 起始触摸点
   * @param current 当前触摸点
   * @return 满足上滑解锁返回 true，否则返回 false
   */
  bool IsUnlockSwipe(const hal::TouchPoint& start,
      const hal::TouchPoint& current) const;

  /**
   * @brief 将屏幕亮度渐变到目标值
   * @param target_percent 目标亮度百分比
   */
  void FadeScreenBrightnessTo(int target_percent);

  /**
   * @brief 读取当前显示偏好，读取失败时使用默认值
   * @return 显示偏好
   */
  app::DisplayPreferences LoadDisplayPreferencesOrDefault() const;

  hal::DeviceProviderContext device_provider_context_;
  hal::LvglPort lvgl_port_;
  ui::UiManager ui_manager_;
  std::atomic<int> current_screen_brightness_percent_{90};
  std::atomic<bool> screen_locked_{false};
  std::atomic<bool> lock_screen_awake_{false};
  std::atomic<bool> power_menu_visible_{false};
};

}  // namespace lilygo_box
