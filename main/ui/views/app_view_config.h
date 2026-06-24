/*
 * @Description: None
 * @Author: LILYGO_L
 * @Date: 2026-05-10 13:27:05
 * @LastEditTime: 2026-05-10 13:27:05
 * @License: GPL 3.0
 */
#pragma once

#include <functional>

#include "lvgl.h"

namespace lilygo_box::app {
class SystemStatusCache;
}  // namespace lilygo_box::app

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
class ScreenProvider;
class WifiProvider;
}  // namespace lilygo_box::hal

namespace lilygo_box::ui {

// app 视图创建时需要的尺寸、设备服务和返回入口上下文。
struct AppViewConfig {
  // 视图宽度，单位为像素。
  int width = 0;
  // 视图高度，单位为像素。
  int height = 0;
  // 屏幕读写和尺寸信息提供者。
  hal::ScreenProvider* screen = nullptr;
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
  hal::BmuProvider* bmu = nullptr;
  // 实时时钟状态提供者。
  hal::RtcProvider* rtc = nullptr;
  // 姿态传感器状态提供者。
  hal::ImuProvider* imu = nullptr;
  // 以太网状态和控制提供者。
  hal::EthernetProvider* ethernet = nullptr;
  // WiFi 状态和控制提供者。
  hal::WifiProvider* wifi = nullptr;
  // 系统状态运行缓存，不负责 NVS 持久化。
  app::SystemStatusCache* system_status = nullptr;
  // app 内部返回按钮触发的 LVGL 回调。
  lv_event_cb_t back_callback = nullptr;
  // 传给返回回调的用户上下文。
  void* back_context = nullptr;
  // 设置状态栏文字和图标颜色。
  std::function<void(uint32_t color)> set_status_bar_text_color;
  // 设置状态栏是否显示。
  std::function<void(bool visible)> set_status_bar_visible;
};

}  // namespace lilygo_box::ui
