/*
 * @Description: App-level WiFi control helpers
 * @Author: LILYGO_L
 * @Date: 2026-06-25 00:00:00
 * @LastEditTime: 2026-06-25 00:00:00
 * @License: GPL 3.0
 */
#pragma once

#include <cstdint>

#include "app/storage/wifi_storage.h"
#include "hal/providers/wifi_provider.h"

namespace lilygo_box::app {

enum class WifiAutoConnectResult {
  kUnavailable,
  kDisabled,
  kNoSavedTarget,
  kWaitingForDriver,
  kWaitingForScan,
  kNetworkNotFound,
  kAlreadyConnected,
  kStarted,
  kFailed,
};

struct WifiAutoConnectOptions {
  // WiFi 驱动未初始化时是否先启动初始化流程。
  bool start_driver_if_needed = true;
  // 是否等待 WiFi 驱动初始化完成后再连接。
  bool wait_for_driver = false;
  // 等待 WiFi 驱动初始化完成的最长时间，单位为毫秒。
  uint32_t wait_timeout_ms = 0;
  // 等待期间轮询 WiFi 状态的间隔，单位为毫秒。
  uint32_t poll_interval_ms = 200;
  // 等待扫描完成的最长时间，单位为毫秒。
  uint32_t scan_timeout_ms = 10 * 1000;
};

/**
 * @brief 从长期 RAM 缓存中查找 WLAN 自动连接目标
 * @param target 自动连接目标输出地址
 * @return 找到可连接目标返回 true，否则返回 false
 */
bool FindWifiAutoConnectTarget(WifiSavedNetwork* target);

/**
 * @brief 按保存偏好启动 WLAN，并在存在目标热点时尝试自动连接
 * @param wifi WiFi 状态和控制提供者
 * @param options 自动连接控制选项
 * @return 自动连接处理结果
 */
WifiAutoConnectResult TryStartWifiAutoConnect(
    hal::WifiProvider* wifi, const WifiAutoConnectOptions& options);

}  // namespace lilygo_box::app
