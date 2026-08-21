/*
 * @Description: 启动器、状态栏、应用窗口与系统覆盖层管理接口
 * @Author: LILYGO_L
 * @Date: 2026-05-10 13:27:05
 * @LastEditTime: 2026-07-30 18:00:00
 * @License: GPL 3.0
 */
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>

#include "app/app_catalog.h"
#include "app/system_status_cache.h"
#include "hal/device_capabilities.h"
#include "hal/providers/rtc_provider.h"
#include "hal/providers/screen_provider.h"
#include "lvgl.h"
#include "ui/input/edge_back_gesture.h"
#include "ui/theme/theme_provider.h"
#include "ui/widgets/prompt/prompt_dialog.h"
#include "ui/widgets/status_bar.h"
#include "ui/widgets/volume_overlay.h"

namespace lilygo_box::hal {
class AudioProvider;
class BatteryManagementProvider;
class CameraProvider;
class CellularProvider;
class DeviceDiagnosticsProvider;
class DeviceInfoProvider;
class EthernetProvider;
class GpsProvider;
class HapticProvider;
class ImuProvider;
class InfraredProvider;
class KeyboardExpansionProvider;
class LvglPort;
class NfcProvider;
class OtgProvider;
class RtcProvider;
class RadioProvider;
class WifiProvider;
class StorageProvider;
}  // namespace lilygo_box::hal

namespace lilygo_box::ui {

class UiManager final {
 public:
  UiManager() = default;

  /**
   * @brief 初始化 launcher 和根屏幕 UI
   * @param screen 屏幕接口
   * @param lvgl_port LVGL 显示接口
   * @param device_capabilities 当前板级实现提供的可选功能能力
   * @param diagnostics 诊断接口
   * @param device_info 设备信息接口
   * @param gps GPS 接口
   * @param audio 音频接口
   * @param haptic 振动接口
   * @param battery_management 电池接口
   * @param camera 相机接口
   * @param rtc RTC 接口
   * @param radio 射频接口
   * @param keyboard_expansion 键盘扩展扫描和生命周期接口
   * @param imu IMU 接口
   * @param ethernet 以太网接口
   * @param wifi hosted WiFi 接口
   * @param storage 存储接口
   * @param otg OTG 反向供电接口
   * @param nfc NFC 接口
   * @param infrared 红外接口
   * @param cellular 蜂窝接口
   * @return 成功返回 true
   */
  bool Init(hal::ScreenProvider* screen,
      hal::LvglPort* lvgl_port,
      const hal::DeviceCapabilities& device_capabilities,
      hal::DeviceDiagnosticsProvider* diagnostics,
      hal::DeviceInfoProvider* device_info,
      hal::GpsProvider* gps,
      hal::AudioProvider* audio,
      hal::HapticProvider* haptic,
      hal::BatteryManagementProvider* battery_management,
      hal::CameraProvider* camera,
      hal::RtcProvider* rtc,
      hal::RadioProvider* radio,
      hal::KeyboardExpansionProvider* keyboard_expansion,
      hal::ImuProvider* imu,
      hal::EthernetProvider* ethernet,
      hal::WifiProvider* wifi,
      hal::StorageProvider* storage, hal::OtgProvider* otg,
      hal::NfcProvider* nfc,
      hal::InfraredProvider* infrared, hal::CellularProvider* cellular);

  /**
   * @brief 设置系统重启和关机操作回调
   * @param restart_callback 重启设备回调
   * @param power_off_callback 关闭设备回调
   */
  void SetSystemPowerCallbacks(std::function<void()> restart_callback,
      std::function<void()> power_off_callback);

  /**
   * @brief 设置立即锁屏请求回调
   * @param callback 由应用层处理的锁屏请求回调
   */
  void SetScreenLockCallback(std::function<void()> callback);

  /**
   * @brief 设置屏幕亮度调整回调
   * @param callback 由应用层统一处理的亮度调整回调
   */
  void SetScreenBrightnessCallback(std::function<bool(int)> callback);

  /**
   * @brief 在屏幕右上侧显示音量快捷浮层
   * @param volume_percent 当前音量百分比
   * @param callback 音量变化回调，第二个参数表示是否保存最终值
   * @return 浮层创建并开始显示时返回 true
   */
  bool ShowVolumeOverlay(int volume_percent,
      VolumeOverlay::VolumeChangeCallback callback);

  /**
   * @brief 启动系统启动界面动画
   * @return 启动成功返回 true，否则返回 false
   */
  bool StartStartupScreenAnimation();

  /**
   * @brief 显示启动阶段的电池保护提示页
   * @param icon 图标文本
   * @param icon_color 图标颜色
   * @param message 提示文本
   * @param battery_percent 电池填充百分比，负数表示显示普通图标
   * @return 显示成功返回 true，否则返回 false
   */
  bool ShowBatteryStartupWarning(const char* icon, uint32_t icon_color,
      const char* message, int battery_percent);

  /**
   * @brief 显示关机充电电量界面
   * @param battery_percent 当前电量百分比
   * @param critical 电量是否低于允许正常开机的阈值
   * @param full_charged 电池是否已经充满
   * @return 显示成功时返回 true
   */
  bool ShowPowerOffChargingScreen(
      int battery_percent, bool critical, bool full_charged);

  /**
   * @brief 设置系统启动界面进度
   * @param percent 进度百分比，范围 0 到 100
   * @return 设置成功返回 true，否则返回 false
   */
  bool SetStartupScreenProgress(int percent);

  /**
   * @brief 判断系统启动界面是否仍在显示或执行动画
   * @return 启动界面仍存在返回 true，否则返回 false
   */
  bool IsStartupScreenActive() const;

  /**
   * @brief 在启动界面结束后显示首次开机欢迎页
   * @param completion_callback 关闭页面前更新 RAM 完成标志的回调
   * @return 页面已经显示或进入待显示状态返回 true，否则返回 false
   */
  bool ShowFirstBootWelcome(
      std::function<bool()> completion_callback);

  /**
   * @brief 判断首次开机欢迎页是否正在等待显示或已经显示
   * @return 首次开机欢迎流程仍在进行返回 true，否则返回 false
   */
  bool IsFirstBootWelcomeActive() const;

  /**
   * @brief 显示键盘扩展不可用提示
   * @return 提示框创建成功返回 true，否则返回 false
   */
  bool ShowKeyboardExpansionUnavailablePrompt();

  /**
   * @brief 关闭当前键盘扩展不可用提示
   */
  void CloseKeyboardExpansionUnavailablePrompt();

  /**
   * @brief 刷新当前设置页面中的键盘扩展控件
   */
  void RefreshActiveSettingsKeyboardExpansion();

  /**
   * @brief 设置全局状态栏文字和图标颜色
   * @param color 文字和图标颜色，格式为 0xRRGGBB
   */
  void SetStatusBarTextColor(uint32_t color);

  /**
   * @brief 设置全局状态栏是否显示
   * @param visible true 显示，false 隐藏
   */
  void SetStatusBarVisible(bool visible);

  /**
   * @brief 立即刷新系统状态并同步更新状态栏和主界面显示
   */
  void RefreshSystemStatusNow();

  /**
   * @brief 显示锁屏覆盖页面
   * @return 显示成功返回 true，否则返回 false
   */
  bool ShowLockScreen();

  /**
   * @brief 隐藏并销毁锁屏覆盖页面
   */
  void HideLockScreen();

  /**
   * @brief 根据视觉上滑拖拽距离更新锁屏页面位置
   * @param offset Y 轴偏移，负数表示向上移动
   */
  void SetLockScreenDragOffset(int offset);

  /**
   * @brief 播放锁屏页面回弹动画
   */
  void ResetLockScreenDrag();

  /**
   * @brief 播放锁屏页面上滑退出动画
   */
  void PlayLockScreenUnlockAnimation();

  /**
   * @brief 显示系统电源菜单覆盖层
   * @param restart_callback 点击重启按钮时调用的回调
   * @param power_off_callback 点击关机按钮时调用的回调
   * @param dismiss_callback 点击遮罩或滑动退出时调用的回调
   * @return 显示成功返回 true，否则返回 false
   */
  bool ShowPowerMenu(std::function<void()> restart_callback,
      std::function<void()> power_off_callback,
      std::function<void()> dismiss_callback);

  /**
   * @brief 判断系统电源操作页是否正在显示
   * @return 正在显示时返回 true
   */
  bool IsPowerMenuVisible() const { return power_menu_ != nullptr; }

  /**
   * @brief 隐藏关机菜单覆盖层
   */
  void HidePowerMenu();

 private:
  struct AppButtonContext {
    UiManager* manager = nullptr;
    const app::AppEntry* app_entry = nullptr;
    lv_obj_t* icon_button = nullptr;
    lv_obj_t* icon_surface = nullptr;
    int normal_icon_size = 0;
  };

  /**
   * @brief 处理 app 图标点击事件
   * @param event LVGL 事件对象
   */
  static void AppButtonEventCallback(lv_event_t* event);

  /**
   * @brief 处理 app 图标回弹结束后的延迟打开
   * @param timer LVGL 定时器对象
   */
  static void AppButtonOpenDelayCallback(lv_timer_t* timer);

  /**
   * @brief 强制恢复 app 图标按压反馈到原始尺寸
   * @param context app 图标按钮上下文
   */
  static void ResetAppIconPressedFeedback(AppButtonContext* context);

  /**
   * @brief 处理返回按钮点击事件
   * @param event LVGL 事件对象
   */
  static void BackButtonEventCallback(lv_event_t* event);

  /**
   * @brief 处理 launcher 和 app 页面手势事件
   * @param event LVGL 事件对象
   */
  static void AppBackSwipeEventCallback(lv_event_t* event);

  /**
   * @brief 处理 launcher 页面滚动事件
   * @param event LVGL 事件对象
   */
  static void PageScrollEventCallback(lv_event_t* event);

  /**
   * @brief 处理应用级系统状态刷新定时器
   * @param timer LVGL 定时器
   */
  static void SystemStatusRefreshTimerCallback(lv_timer_t* timer);

  /**
   * @brief 处理键盘扩展不可用提示关闭事件
   * @param context UiManager 指针
   */
  static void KeyboardExpansionUnavailablePromptDismissedCallback(
      void* context);

  /**
   * @brief 处理根屏幕尺寸变化或刷新事件，用于旋转后重新计算布局
   * @param event LVGL 事件对象
   */
  static void RootLayoutRefreshEventCallback(lv_event_t* event);

  /**
   * @brief 设置系统启动界面进度条宽度
   * @param user_data UI 管理器对象
   * @param width 进度条宽度
   */
  static void SetStartupProgressWidth(void* user_data, int32_t width);

  /**
   * @brief 设置系统启动界面透明度
   * @param user_data UI 管理器对象
   * @param opacity 透明度
   */
  static void SetStartupScreenOpacity(void* user_data, int32_t opacity);

  /**
   * @brief 设置首次开机欢迎页整体透明度
   * @param user_data UiManager 指针
   * @param opacity 透明度
   */
  static void SetFirstBootWelcomeOpacity(void* user_data, int32_t opacity);

  /**
   * @brief 处理系统启动界面进度条动画完成事件
   * @param animation LVGL 动画对象
   */
  static void StartupProgressCompletedCallback(lv_anim_t* animation);

  /**
   * @brief 处理系统启动界面淡出动画完成事件
   * @param animation LVGL 动画对象
   */
  static void StartupFadeCompletedCallback(lv_anim_t* animation);

  /**
   * @brief 处理首次开机欢迎页淡出完成事件
   * @param animation LVGL 动画对象
   */
  static void FirstBootWelcomeFadeCompletedCallback(lv_anim_t* animation);

  /**
   * @brief 获取当前 LVGL 逻辑布局宽度，旋转后会随 display 更新
   * @return 当前布局宽度
   */
  int LayoutWidth() const;

  /**
   * @brief 获取当前 LVGL 逻辑布局高度，旋转后会随 display 更新
   * @return 当前布局高度
   */
  int LayoutHeight() const;

  /**
   * @brief 根屏幕尺寸变化后重新创建依赖宽高的布局
   */
  void RelayoutForScreenSize();

  /**
   * @brief 创建不包含任何启动内容的纯黑背景
   * @param parent 父对象
   * @return 创建成功返回背景对象，否则返回 nullptr
   */
  lv_obj_t* CreateStartupBackground(lv_obj_t* parent);

  /**
   * @brief 创建系统启动界面
   * @param parent 父对象
   * @return 创建成功返回启动界面对象，否则返回 nullptr
   */
  lv_obj_t* CreateStartupScreen(lv_obj_t* parent);

  /**
   * @brief 启动系统启动界面进度条动画
   * @param target_percent 目标进度百分比
   * @return 启动成功返回 true，否则返回 false
   */
  bool StartStartupProgressAnimation(int target_percent);

  /**
   * @brief 启动系统启动界面淡出动画
   * @return 启动成功返回 true，否则返回 false
   */
  bool StartStartupFadeOut();

  /**
   * @brief 删除系统启动界面
   */
  void DestroyStartupScreen();

  /**
   * @brief 创建已经进入待显示状态的首次开机欢迎页
   * @return 调用后页面存在返回 true，否则返回 false
   */
  bool CreateFirstBootWelcomeScreen();

  /**
   * @brief 启动首次开机欢迎页淡出动画
   * @return 启动成功返回 true，否则返回 false
   */
  bool StartFirstBootWelcomeFadeOut();

  /**
   * @brief 删除首次开机欢迎页并恢复主界面状态栏
   */
  void DestroyFirstBootWelcomeScreen();

  /**
   * @brief 更新首次开机 RAM 完成标志并关闭欢迎页
   * @return 标志缓存且页面关闭成功返回 true，否则返回 false
   */
  bool CompleteFirstBootWelcome();

  /**
   * @brief 创建当前 app 页面
   * @param app_entry launcher app 入口
   * @return 创建成功返回 true，否则返回 false
   */
  bool CreateActiveAppView(const app::AppEntry& app_entry);

  /**
   * @brief 创建 launcher 根容器
   * @param parent 父对象
   * @return 创建成功返回对象指针，否则返回 nullptr
   */
  lv_obj_t* CreateLauncher(lv_obj_t* parent);

  /**
   * @brief 创建可拖动的 launcher 页面容器
   * @param parent 父对象
   * @return 创建成功返回对象指针，否则返回 nullptr
   */
  lv_obj_t* CreatePageScroller(lv_obj_t* parent);

  /**
   * @brief 创建主屏时间日期区域
   * @param parent 父对象
   * @return 创建成功返回对象指针，否则返回 nullptr
   */
  lv_obj_t* CreateClockGroup(lv_obj_t* parent);

  /**
   * @brief 刷新应用级系统状态信息
   */
  void RefreshSystemStatus();

  /**
   * @brief 将系统音量同步到当前打开的设置页面
   * @param volume_percent 当前音量百分比
   */
  void UpdateActiveSettingsVolume(int volume_percent);

  /**
   * @brief 根据 RTC 状态刷新状态栏和主界面时间显示
   * @param status RTC 状态
   */
  void UpdateClockLabels(const hal::RtcStatus& status);

  /**
   * @brief 根据电池管理状态刷新状态栏电池显示
   * @param status 电池状态
   */
  void UpdateBatteryStatus(const hal::BatteryManagementStatus& status);

  /**
   * @brief 根据 WiFi 状态刷新状态栏 WiFi 图标显示
   * @param status WiFi 状态
   */
  void UpdateWifiStatus(const hal::WifiStatus& status);

  /**
   * @brief 根据键盘扩展状态刷新状态栏键盘图标
   */
  void UpdateKeyboardExpansionStatus();

  /**
   * @brief 创建主屏 app 图标网格
   * @param parent 父对象
   * @return 创建成功返回对象指针，否则返回 nullptr
   */
  lv_obj_t* CreateAppGrid(lv_obj_t* parent);

  /**
   * @brief 创建一个 app 图标单元
   * @param parent 父对象
   * @param context app 按钮上下文
   * @param cell_width 图标单元宽度
   * @return 创建成功返回对象指针，否则返回 nullptr
   */
  lv_obj_t* CreateAppIcon(
      lv_obj_t* parent, AppButtonContext* context, int cell_width);

  /**
   * @brief 创建底部常驻 dock
   * @param parent 父对象
   * @return 创建成功返回对象指针，否则返回 nullptr
   */
  lv_obj_t* CreateDock(lv_obj_t* parent);

  /**
   * @brief 创建一个 dock 图标单元
   * @param parent 父对象
   * @param context app 按钮上下文
   * @param cell_width 图标单元宽度
   * @return 创建成功返回对象指针，否则返回 nullptr
   */
  lv_obj_t* CreateDockIcon(
      lv_obj_t* parent, AppButtonContext* context, int cell_width);

  /**
   * @brief 创建页面指示器
   * @param parent 父对象
   * @return 创建成功返回对象指针，否则返回 nullptr
   */
  lv_obj_t* CreatePageIndicator(lv_obj_t* parent);

  /**
   * @brief 更新页面指示器状态
   * @param page_index 页面索引
   */
  void UpdatePageIndicator(size_t page_index);

  /**
   * @brief 显示指定 launcher app 页面
   * @param app_entry launcher app 入口
   * @return 显示成功返回 true，否则返回 false
   */
  bool ShowAppView(const app::AppEntry& app_entry);

  /**
   * @brief 返回 launcher 页面
   */
  void ShowLauncher();

  /**
   * @brief 通知当前 app 锁屏显示状态变化
   * @param visible true 表示锁屏已显示，false 表示锁屏已隐藏
   */
  void NotifyLockScreenVisibilityChanged(bool visible);

  hal::ScreenProvider* screen_ = nullptr;
  // 向需要安全熄屏的系统页面传递统一 LVGL 刷新生命周期。
  hal::LvglPort* lvgl_port_ = nullptr;
  hal::DeviceCapabilities device_capabilities_;
  hal::DeviceDiagnosticsProvider* diagnostics_provider_ = nullptr;
  hal::DeviceInfoProvider* device_info_provider_ = nullptr;
  hal::GpsProvider* gps_provider_ = nullptr;
  hal::AudioProvider* audio_provider_ = nullptr;
  hal::HapticProvider* haptic_provider_ = nullptr;
  hal::BatteryManagementProvider* battery_management_provider_ = nullptr;
  hal::CameraProvider* camera_provider_ = nullptr;
  hal::RtcProvider* rtc_provider_ = nullptr;
  hal::RadioProvider* radio_provider_ = nullptr;
  hal::KeyboardExpansionProvider* keyboard_expansion_provider_ = nullptr;
  hal::ImuProvider* imu_provider_ = nullptr;
  hal::EthernetProvider* ethernet_provider_ = nullptr;
  hal::WifiProvider* wifi_provider_ = nullptr;
  hal::StorageProvider* storage_provider_ = nullptr;
  hal::OtgProvider* otg_provider_ = nullptr;
  hal::NfcProvider* nfc_provider_ = nullptr;
  hal::InfraredProvider* infrared_provider_ = nullptr;
  hal::CellularProvider* cellular_provider_ = nullptr;
  lv_obj_t* root_screen_ = nullptr;
  StatusBar status_bar_;
  lv_obj_t* startup_background_ = nullptr;
  lv_obj_t* startup_screen_ = nullptr;
  lv_obj_t* startup_progress_fill_ = nullptr;
  lv_obj_t* first_boot_welcome_screen_ = nullptr;
  lv_obj_t* lock_screen_ = nullptr;
  lv_obj_t* power_menu_ = nullptr;
  PromptDialogState keyboard_expansion_unavailable_prompt_;
  VolumeOverlay volume_overlay_;
  int startup_progress_percent_ = 0;
  int startup_progress_target_percent_ = 0;
  int startup_progress_pending_percent_ = 0;
  int layout_width_ = 0;
  int layout_height_ = 0;
  bool startup_progress_animating_ = false;
  bool first_boot_welcome_pending_ = false;
  bool first_boot_welcome_closing_ = false;
  bool relayouting_ = false;
  std::function<bool()> first_boot_welcome_completion_callback_;
  const app::AppEntry* active_app_entry_ = nullptr;
  lv_obj_t* launcher_container_ = nullptr;
  lv_obj_t* page_scroller_ = nullptr;
  lv_obj_t* home_page_ = nullptr;
  lv_obj_t* home_time_label_ = nullptr;
  lv_obj_t* home_date_label_ = nullptr;
  lv_obj_t* home_week_label_ = nullptr;
  lv_obj_t* reserved_page_ = nullptr;
  lv_obj_t* active_view_container_ = nullptr;
  std::function<void(bool visible)> active_view_lock_screen_callback_;
  std::function<void()> screen_lock_callback_;
  std::function<bool(int)> screen_brightness_callback_;
  std::function<void()> restart_device_callback_;
  std::function<void()> power_off_device_callback_;
  EdgeBackSwipeState app_back_swipe_ = {};
  lv_timer_t* system_status_refresh_timer_ = nullptr;
  app::SystemStatusCache system_status_cache_;
  theme::ThemeProvider theme_provider_;
  lv_obj_t* page_indicator_ = nullptr;
  lv_obj_t* first_page_dot_ = nullptr;
  lv_obj_t* second_page_dot_ = nullptr;
  std::array<AppButtonContext, app::kMaxAppEntryCount> button_contexts_;
  std::array<AppButtonContext, app::kMaxAppEntryCount> dock_button_contexts_;
  size_t button_context_count_ = 0;
  size_t dock_button_context_count_ = 0;
  size_t page_index_ = 0;
  char clock_time_text_[6] = "09:15";
  char home_date_text_[24] = "June 21th";
  char home_week_text_[8] = "Sat";
};

}  // namespace lilygo_box::ui
