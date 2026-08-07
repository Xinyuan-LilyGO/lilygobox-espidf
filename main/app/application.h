/*
 * @Description: 系统应用生命周期与启动协调接口
 * @Author: LILYGO_L
 * @Date: 2026-05-10 13:27:05
 * @LastEditTime: 2026-07-17 09:16:03
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

  /**
   * @brief 让设备进入深度睡眠级关断状态并重启
   */
  void RestartDevice();

 private:
  /**
   * @brief 显示电池启动提示并等待完整画面传输到屏幕
   * @param icon 图标文本
   * @param icon_color 图标颜色
   * @param message 提示文本
   * @param battery_percent 电池填充百分比，负数表示显示普通图标
   * @return 页面创建成功返回 true，否则返回 false
   */
  bool ShowBatteryStartupWarning(const char* icon, uint32_t icon_color,
      const char* message, int battery_percent);

  /**
   * @brief 创建正常启动页并等待完整画面刷新到屏幕
   * @return 页面创建和刷新成功返回 true，否则返回 false
   */
  bool StartStartupScreen();

  /**
   * @brief 启动后自动连接 WLAN 的后台任务入口
   * @param context Application 实例
   */
  static void StartupWifiAutoConnectTaskEntry(void* context);

  /**
   * @brief 根据启动时载入 RAM 的 WLAN 偏好执行自动连接
   */
  void RunStartupWifiAutoConnectTask();

  /**
   * @brief 锁屏后台任务入口
   * @param context Application 实例
   */
  static void ScreenLockTaskEntry(void* context);

  /**
   * @brief 监控触摸并执行自动锁屏和锁屏页面双击亮屏或熄屏流程
   */
  void RunScreenLockTask();

  /**
   * @brief 请求锁屏后台任务立即锁定并熄灭屏幕
   */
  void RequestScreenLock();

  /**
   * @brief 立即进入锁屏熄屏状态
   * @return 锁屏成功返回 true，否则返回 false
   */
  bool LockScreenNow();

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
   * @brief 让设备进入深度睡眠级关断状态
   */
  void PowerOffDevice();

  /**
   * @brief 锁屏页面亮屏态下按超时流程重新进入休眠
   * @return 进入休眠成功返回 true，否则返回 false
   */
  bool SleepAwakeLockScreenWithTimeout();

  /**
   * @brief 立即熄灭当前亮屏的锁屏页面
   * @return 进入休眠成功返回 true，否则返回 false
   */
  bool SleepLockScreenNow();

  /**
   * @brief 让物理屏幕进入轻度休眠
   * @return 屏幕进入休眠返回 true，否则恢复屏幕并返回 false
   */
  bool EnterScreenSleep();

  /**
   * @brief 为重启或关机冻结更新并完成最终存储落盘
   * @return 屏幕已关闭且缓存已全部持久化返回 true
   */
  bool PreparePowerActionStorage();

  /**
   * @brief 在屏幕转换事务内唤醒设备并恢复用户亮度
   * @return 屏幕与亮度均恢复且 LVGL 可以安全恢复时返回 true
   */
  bool RestoreScreenAfterSleep();

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
   * @brief 通过统一触摸入口读取亮屏触摸状态
   * @param point 触摸点输出
   * @param access_available 可选返回当前触摸源是否可访问
   * @return 检测到有效触摸返回 true
   */
  bool ReadScreenTouchWhileAwake(
      hal::TouchPoint* point, bool* access_available = nullptr);

  /**
   * @brief 在面板熄屏且 LVGL 刷新暂停时直接读取触摸控制器
   * @param point 触摸点输出
   * @return 检测到有效触摸返回 true
   */
  bool ReadScreenTouchWhileSleeping(hal::TouchPoint* point);

  /**
   * @brief 应用屏幕亮度并同步应用层当前值
   * @param percent 目标亮度百分比
   * @return 亮度设置成功返回 true
   */
  bool ApplyScreenBrightness(int percent);

  /**
   * @brief 从熄灭状态渐亮到目标亮度
   * @param target_percent 目标亮度百分比
   * @return 渐亮成功返回 true
   */
  bool StartScreenBacklight(int target_percent);

  /**
   * @brief 在短屏幕事务内修改亮屏亮度
   * @param percent 目标亮度百分比
   * @return 硬件仍可访问且亮度设置成功时返回 true
   */
  bool SetScreenBrightnessWhileAwake(int percent);

  /**
   * @brief 将屏幕亮度渐变到目标值
   * @param target_percent 目标亮度百分比
   * @param duration_ms 渐变持续时间
   * @return 全部渐变步骤成功返回 true
   */
  bool FadeScreenBrightnessTo(int target_percent, uint32_t duration_ms);

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
  std::atomic<bool> screen_lock_requested_{false};
  // 仅在驱动确认物理面板已完整熄屏后保持为 true。
  std::atomic<bool> screen_off_confirmed_{false};
  // 防止重启与关机流程并发进入最终熄屏和存储事务。
  std::atomic<bool> power_action_in_progress_{false};
};

}  // namespace lilygo_box
