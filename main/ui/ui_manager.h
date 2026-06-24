/*
 * @Description: None
 * @Author: LILYGO_L
 * @Date: 2026-05-10 13:27:05
 * @LastEditTime: 2026-05-12 23:01:06
 * @License: GPL 3.0
 */
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "app/app_catalog.h"
#include "app/system_status_cache.h"
#include "hal/providers/rtc_provider.h"
#include "hal/providers/screen_provider.h"
#include "lvgl.h"
#include "ui/input/edge_back_gesture.h"
#include "ui/widgets/status_bar.h"

namespace lilygo_box::hal {
class AudioProvider;
class BmuProvider;
class DeviceDiagnosticsProvider;
class DeviceInfoProvider;
class EthernetProvider;
class GpsProvider;
class HapticProvider;
class ImuProvider;
class RtcProvider;
class WifiProvider;
}  // namespace lilygo_box::hal

namespace lilygo_box::ui {

class UiManager final {
 public:
  UiManager() = default;

  /**
   * @brief 初始化 launcher 和根屏幕 UI
   * @param screen 屏幕设备对象
   * @param diagnostics 设备诊断提供者指针
   * @param device_info 设备信息提供者指针
   * @param gps GPS 提供者指针
   * @param audio 音频提供者指针
   * @param haptic 振动提供者指针
   * @param bmu BMU 电池管理提供者指针
   * @param rtc RTC 提供者指针
   * @param imu IMU 提供者指针
   * @param ethernet 以太网提供者指针
   * @param wifi hosted WiFi 提供者指针
   * @return 初始化成功返回 true，否则返回 false
   */
  bool Init(hal::ScreenProvider* screen,
      hal::DeviceDiagnosticsProvider* diagnostics,
      hal::DeviceInfoProvider* device_info,
      hal::GpsProvider* gps,
      hal::AudioProvider* audio,
      hal::HapticProvider* haptic,
      hal::BmuProvider* bmu,
      hal::RtcProvider* rtc,
      hal::ImuProvider* imu,
      hal::EthernetProvider* ethernet,
      hal::WifiProvider* wifi);

  /**
   * @brief 启动系统启动界面动画
   * @return 启动成功返回 true，否则返回 false
   */
  bool StartStartupScreenAnimation();

  /**
   * @brief 设置系统启动界面进度
   * @param percent 进度百分比，范围 0 到 100
   * @return 设置成功返回 true，否则返回 false
   */
  bool SetStartupScreenProgress(int percent);

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

 private:
  struct AppButtonContext {
    UiManager* manager = nullptr;
    const app::AppEntry* app_entry = nullptr;
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
   * @brief 从 RTC 读取时间并刷新所有时间显示
   */

  /**
   * @brief 从 BMU 读取电池信息并刷新状态栏电量显示
   */

  /**
   * @brief 刷新应用级系统状态信息
   */
  void RefreshSystemStatus();

  /**
   * @brief 根据 RTC 状态刷新状态栏和主界面时间显示
   * @param status RTC 状态
   */
  void UpdateClockLabels(const hal::RtcStatus& status);

  /**
   * @brief 根据 BMU 状态刷新状态栏电池显示
   * @param status BMU 状态
   */
  void UpdateBatteryStatus(const hal::BmuStatus& status);

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

  hal::ScreenProvider* screen_ = nullptr;
  hal::DeviceDiagnosticsProvider* diagnostics_provider_ = nullptr;
  hal::DeviceInfoProvider* device_info_provider_ = nullptr;
  hal::GpsProvider* gps_provider_ = nullptr;
  hal::AudioProvider* audio_provider_ = nullptr;
  hal::HapticProvider* haptic_provider_ = nullptr;
  hal::BmuProvider* bmu_provider_ = nullptr;
  hal::RtcProvider* rtc_provider_ = nullptr;
  hal::ImuProvider* imu_provider_ = nullptr;
  hal::EthernetProvider* ethernet_provider_ = nullptr;
  hal::WifiProvider* wifi_provider_ = nullptr;
  lv_obj_t* root_screen_ = nullptr;
  StatusBar status_bar_;
  lv_obj_t* startup_screen_ = nullptr;
  lv_obj_t* startup_progress_fill_ = nullptr;
  int startup_progress_percent_ = 0;
  int startup_progress_target_percent_ = 0;
  int startup_progress_pending_percent_ = 0;
  bool startup_progress_animating_ = false;
  lv_obj_t* launcher_container_ = nullptr;
  lv_obj_t* page_scroller_ = nullptr;
  lv_obj_t* home_page_ = nullptr;
  lv_obj_t* home_time_label_ = nullptr;
  lv_obj_t* home_date_label_ = nullptr;
  lv_obj_t* home_week_label_ = nullptr;
  lv_obj_t* reserved_page_ = nullptr;
  lv_obj_t* active_view_container_ = nullptr;
  EdgeBackSwipeState app_back_swipe_ = {};
  lv_timer_t* system_status_refresh_timer_ = nullptr;
  app::SystemStatusCache system_status_cache_;
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
