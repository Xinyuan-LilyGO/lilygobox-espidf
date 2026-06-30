/*
 * @Description: None
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
   * @brief 恢复屏幕亮度并退出锁屏状态
   */
  void WakeScreenFromLock();

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
  std::atomic<int> current_screen_brightness_percent_{100};
  std::atomic<bool> screen_locked_{false};
};

}  // namespace lilygo_box
