/*
 * @Description: 应用页面尺寸、硬件服务与回调依赖配置
 * @Author: LILYGO_L
 * @Date: 2026-05-10 13:27:05
 * @LastEditTime: 2026-07-30 18:00:00
 * @License: GPL 3.0
 */
#pragma once

#include <functional>

#include "hal/device_capabilities.h"
#include "lvgl.h"

namespace lilygo_box::app {
class SystemStatusCache;
}  // namespace lilygo_box::app

namespace lilygo_box::ui::theme {
class ThemeProvider;
}  // namespace lilygo_box::ui::theme

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
class ScreenProvider;
class StorageProvider;
class WifiProvider;
}  // namespace lilygo_box::hal

namespace lilygo_box::ui {

// app 视图创建时需要的尺寸、设备服务和返回入口上下文。
struct AppViewConfig {
  // 视图宽度，单位为像素。
  int width = 0;
  // 视图高度，单位为像素。
  int height = 0;
  // 当前板级实现提供的可选功能能力。
  hal::DeviceCapabilities device_capabilities;
  // 屏幕读写和尺寸信息提供者。
  hal::ScreenProvider* screen = nullptr;
  // LVGL 刷新、屏幕电源转换与短硬件访问协调器。
  hal::LvglPort* lvgl_port = nullptr;
  // 设备诊断信息提供者。
  hal::DeviceDiagnosticsProvider* diagnostics = nullptr;
  // 设备基础信息提供者。
  hal::DeviceInfoProvider* device_info = nullptr;
  // GPS 状态和控制提供者。
  hal::GpsProvider* gps = nullptr;
  // 音频播放和采样提供者。
  hal::AudioProvider* audio = nullptr;
  // 振动反馈提供者。
  hal::HapticProvider* haptic = nullptr;
  // 电池管理状态提供者。
  hal::BatteryManagementProvider* battery_management = nullptr;
  // 摄像头预览提供者。
  hal::CameraProvider* camera = nullptr;
  // 实时时钟状态提供者。
  hal::RtcProvider* rtc = nullptr;
  // Radio 状态、配置与收发提供者。
  hal::RadioProvider* radio = nullptr;
  // 可选键盘扩展的扫描和生命周期提供者。
  hal::KeyboardExpansionProvider* keyboard_expansion = nullptr;
  // 姿态传感器状态提供者。
  hal::ImuProvider* imu = nullptr;
  // 以太网状态和控制提供者。
  hal::EthernetProvider* ethernet = nullptr;
  // WiFi 状态和控制提供者。
  hal::WifiProvider* wifi = nullptr;
  // SD 卡和本机存储状态提供者。
  hal::StorageProvider* storage = nullptr;
  // OTG 反向供电状态和控制提供者。
  hal::OtgProvider* otg = nullptr;
  // NFC 读卡器状态和控制提供者。
  hal::NfcProvider* nfc = nullptr;
  // 红外 NEC 收发和状态提供者。
  hal::InfraredProvider* infrared = nullptr;
  // 蜂窝通信状态和 AT 指令提供者。
  hal::CellularProvider* cellular = nullptr;
  // 系统状态运行缓存，不负责 NVS 持久化。
  app::SystemStatusCache* system_status = nullptr;
  // 当前 app 可读取的主题提供器。
  theme::ThemeProvider* theme_provider = nullptr;
  // app 内部返回按钮触发的 LVGL 回调。
  lv_event_cb_t back_callback = nullptr;
  // 传给返回回调的用户上下文。
  void* back_context = nullptr;
  // 设置状态栏文字和图标颜色。
  std::function<void(uint32_t color)> set_status_bar_text_color;
  // 设置状态栏是否显示。
  std::function<void(bool visible)> set_status_bar_visible;
  // 注册锁屏显示状态变化回调，active app 销毁时传入空回调清除注册。
  std::function<void(std::function<void(bool visible)> callback)>
      set_lock_screen_visibility_callback;
  // 请求应用层立即进入锁屏状态。
  std::function<void()> request_screen_lock;
  // 请求应用层设置屏幕亮度。
  std::function<bool(int percent)> set_screen_brightness;
  // 显示系统重新启动和关机选项。
  std::function<bool()> show_power_options;
};

}  // namespace lilygo_box::ui
