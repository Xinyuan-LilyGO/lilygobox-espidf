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
#include "freertos/FreeRTOS.h"
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
   * @brief 完成存储落盘后执行不切断设备电源轨的软件重启
   */
  void RestartDevice();

 private:
  // 锁屏页面与物理屏幕完成状态转换后的稳定状态。
  enum class ScreenLockState : uint8_t {
    kUnlocked,
    kAwake,
    kAsleep,
  };

  // 电源键轮询任务只投递事件，屏幕状态始终由锁屏任务串行修改。
  enum class PowerButtonAction : uint8_t {
    kNone,
    kShortPress,
    kShowPowerMenu,
  };

  // 由系统状态变化触发的用户活动原因。
  enum class SystemActivityReason : uint8_t {
    kNone,
    kChargingStarted,
    kChargingStopped,
    kKeyboardConnected,
    kKeyboardDisconnected,
  };

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
   * @brief 显示一次关机充电界面并等待画面完整刷新
   * @param battery_percent 当前电量百分比
   * @param critical 电量是否低于开机阈值
   * @param full_charged 电池是否已经充满
   * @return 页面创建成功时返回 true
   */
  bool ShowPowerOffChargingScreen(
      int battery_percent, bool critical, bool full_charged);

  /**
   * @brief 显示关机充电状态并在超时后重新进入关机
   */
  void RunPowerOffChargingScreen();

  /**
   * @brief 从完整关机准备状态恢复显示链路以显示充电状态
   * @return 屏幕和刷新链路恢复成功时返回 true
   */
  bool WakeScreenForPowerOffCharging();

  /**
   * @brief 关机充电界面显示结束后重新进入运输模式或深度睡眠
   */
  void ReturnToPowerOffStateAfterChargingScreen();

  /**
   * @brief 创建正常启动页并等待完整画面刷新到屏幕
   * @return 页面创建和刷新成功返回 true，否则返回 false
   */
  bool StartStartupScreen();

  /**
   * @brief 根据用户偏好和外部电源状态协调 OTG 反向供电
   * @return 状态读取和所需硬件操作成功时返回 true
   */
  bool UpdateOtgPowerPolicy();

  /**
   * @brief 检查开机或自动重连触发的键盘扩展扫描结果
   */
  void UpdateKeyboardExpansionScan();

  /**
   * @brief 处理运行期间键盘扩展断开提示
   */
  void HandleKeyboardExpansionDisconnection();

  /**
   * @brief 处理键盘扩展重新连接信号并启动自动扫描
   */
  void UpdateKeyboardExpansionConnection();

  /**
   * @brief 在启动页结束后显示键盘扩展不可用提示
   */
  void ShowPendingKeyboardExpansionUnavailableNotice();

  /**
   * @brief 处理状态栏缓存发布的电池管理状态
   * @param status 最新有效电池状态
   */
  void HandleBatteryManagementStatusUpdate(
      const hal::BatteryManagementStatus& status);

  /**
   * @brief 请求锁屏任务处理一次系统活动
   * @param reason 系统状态变化原因
   *
   * 正常亮屏时重置自动锁屏计时，预熄屏时恢复亮度，熄屏时唤醒锁屏。
   */
  void RequestSystemActivity(SystemActivityReason reason);

  /**
   * @brief 取出一个待处理的系统活动请求
   * @return 待处理原因；没有请求时返回 kNone
   */
  SystemActivityReason ConsumeSystemActivity();

  /**
   * @brief 在屏幕转换期间确认待处理的键盘扩展中断是否为真实拔出
   * @return 驱动已经确认键盘扩展断开时返回 true
   */
  bool ConfirmKeyboardExpansionDisconnectionForScreenTransition();

  /**
   * @brief 取出屏幕转换活动并补充确认真实键盘拔出
   * @return 已确认的系统活动原因；没有活动时返回 kNone
   */
  SystemActivityReason ConsumeScreenTransitionActivity();

  /**
   * @brief 获取系统活动原因的日志名称
   * @param reason 系统状态变化原因
   * @return 静态原因名称
   */
  static const char* SystemActivityReasonName(SystemActivityReason reason);

  /**
   * @brief 应用一次已经确认的屏幕活动
   * @param last_touch_ms 正常界面自动锁屏计时
   * @param lock_screen_last_interaction_ms 锁屏界面熄屏计时
   * @param restore_brightness_percent 非负时恢复到指定亮度
   * @return 所需的唤醒或亮度恢复操作成功时返回 true
   *
   * 触摸手势与系统状态变化共用此最终处理，但触摸识别仍由调用方完成。
   */
  bool ApplyScreenActivity(
      uint32_t* last_touch_ms,
      uint32_t* lock_screen_last_interaction_ms,
      int restore_brightness_percent = -1);

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
   * @brief 设备物理电源键的后台轮询任务入口
   * @param context Application 实例
   */
  static void PowerButtonTaskEntry(void* context);

  /**
   * @brief 物理音量加减按键的后台轮询任务入口
   * @param context Application 实例
   */
  static void VolumeButtonTaskEntry(void* context);

  /**
   * @brief 监控触摸并执行自动锁屏和锁屏页面双击亮屏或熄屏流程
   */
  void RunScreenLockTask();

  /**
   * @brief 处理设备物理电源键事件
   */
  void RunPowerButtonTask();

  /**
   * @brief 轮询物理音量加减按键并处理按下和长按连续调节
   */
  void RunVolumeButtonTask();

  /**
   * @brief 应用物理按键计算出的音量并显示快捷浮层
   * @param volume_percent 目标音量百分比
   * @return 音量成功应用到硬件时返回 true
   */
  bool HandleVolumeButtonValue(int volume_percent);

  /**
   * @brief 应用扬声器音量并按需保存最终设置
   * @param percent 目标音量百分比
   * @param commit true 表示保存最终设置，false 仅更新硬件
   * @return 音量成功应用到硬件时返回 true
   */
  bool ApplySpeakerVolume(int percent, bool commit);

  /**
   * @brief 处理电源键短按，切换锁屏、熄屏和唤醒状态
   */
  void HandlePowerButtonShortPress();

  /**
   * @brief 显示由物理电源键长按触发的重启/关机操作页
   */
  void ShowPowerMenuFromPhysicalButton();

  /**
   * @brief 请求锁屏后台任务立即锁定并熄灭屏幕
   */
  void RequestScreenLock();

  /**
   * @brief 等待键盘扩展连接更新和扫描事务结束
   * @return 事务在限定时间内结束时返回 true，否则返回 false
   */
  bool WaitForKeyboardExpansionConnectionIdle();

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
   * @return 屏幕完成唤醒并提交锁屏亮屏状态时返回 true
   */
  bool WakeScreenFromLock();

  void RestartSystem();

  /**
   * @brief 完成存储落盘后执行设备完整关机
   */
  void PowerOffDevice();

  /**
   * @brief 锁屏页面亮屏态下按超时流程重新进入休眠
   * @return 进入休眠成功返回 true，否则返回 false
   */
  bool SleepAwakeLockScreenWithTimeout(
      uint32_t* last_touch_ms,
      uint32_t* lock_screen_last_interaction_ms);

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
   * @param access_available 可选返回当前触摸源是否可访问
   * @return 检测到有效触摸返回 true
   */
  bool ReadScreenTouchWhileSleeping(
      hal::TouchPoint* point, bool* access_available = nullptr);

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
  std::atomic<ScreenLockState> screen_lock_state_{ScreenLockState::kUnlocked};
  std::atomic<bool> screen_lock_requested_{false};
  // 锁屏页面创建到锁屏状态发布期间禁止显示普通提示框。
  std::atomic<bool> screen_lock_transition_in_progress_{false};
  // 连接更新可能同步清理扩展硬件；锁屏转换等待该事务结束后再休眠。
  std::atomic<bool> keyboard_expansion_connection_update_in_progress_{false};
  // 仅在驱动确认物理面板已完整熄屏后保持为 true。
  std::atomic<bool> screen_off_confirmed_{false};
  // 防止重启与关机流程并发进入最终熄屏和存储事务。
  std::atomic<bool> power_action_in_progress_{false};
  // 启动页结束前只跟踪按键状态，不向界面投递事件。
  std::atomic<bool> physical_button_events_enabled_{false};
  // 由电源键轮询任务写入，由锁屏任务交换取走。
  std::atomic<PowerButtonAction> pending_power_button_action_{
      PowerButtonAction::kNone};
  // 主循环只投递系统活动，计时和屏幕状态统一由锁屏任务串行处理。
  std::atomic<SystemActivityReason> pending_system_activity_reason_{
      SystemActivityReason::kNone};
  // 预熄屏任务已直接应用拔出活动时，主循环只补做状态栏和提示刷新。
  std::atomic<bool> keyboard_expansion_disconnection_activity_reported_{
      false};
  // 状态栏电池缓存首次更新只建立充电状态基准，不生成系统活动。
  std::atomic<bool> battery_charging_state_known_{false};
  std::atomic<bool> battery_charging_{false};
  // UI 状态刷新任务只投递低电量事件，关机仍由应用主循环串行执行。
  std::atomic<bool> battery_depleted_pending_{false};
  // 物理电源键调出的操作页显示期间，暂停锁屏页手势处理。
  std::atomic<bool> physical_power_menu_active_{false};
  // 当前是否由持久关机状态进入一次性的关机充电界面。
  bool power_off_charging_boot_ = false;
  // 外部电源接入期间是否临时暂停用户请求的 OTG 反向供电。
  bool otg_suspended_for_external_power_ = false;
  // 应用层记录的 OTG 硬件状态是否有效。
  bool otg_hardware_state_known_ = false;
  // 最近一次成功应用到驱动的 OTG 开关状态。
  bool otg_hardware_enabled_ = false;
  // 最近一次确认外部电源移除时的系统节拍。
  TickType_t otg_external_power_removed_tick_ = 0;
  // 键盘扩展扫描任务仍在进行，由主循环与锁屏任务共同读取。
  std::atomic<bool> keyboard_expansion_scan_pending_{false};
  // 键盘扩展不可用后等待系统主界面显示一次连接提示。
  bool keyboard_expansion_unavailable_notice_pending_ = false;
  // 同一次物理断开只刷新一次状态并生成一次通知。
  bool keyboard_expansion_disconnection_handled_ = false;
  // 避免连接监听异常期间重复输出相同警告。
  bool keyboard_expansion_connection_update_failed_ = false;
};

}  // namespace lilygo_box
