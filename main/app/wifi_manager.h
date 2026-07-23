/*
 * @Description: App-level WiFi control helpers
 * @Author: LILYGO_L
 * @Date: 2026-06-25 00:00:00
 * @LastEditTime: 2026-07-22 00:00:00
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
  kPaused,
  kNoSavedTarget,
  kNoVisibleTarget,
  kWaitingForDriver,
  kAlreadyConnected,
  kConnected,
  kFailed,
};

struct WifiAutoConnectOptions {
  // 驱动未初始化时是否先启动初始化流程。
  bool start_driver_if_needed = true;
  // 是否等待驱动初始化完成。
  bool wait_for_driver = false;
  // 等待驱动初始化完成的最长时间，单位为毫秒。
  uint32_t wait_timeout_ms = 0;
  // 开机自动连接等待扫描结果的最长时间，单位为毫秒。
  uint32_t scan_timeout_ms = 10 * 1000;
  // 单个保存网络等待连接结果的最长时间，单位为毫秒。
  uint32_t connection_timeout_ms = 10 * 1000;
  // 单个保存网络最多尝试的次数，包含首次连接。
  uint32_t attempts_per_network = 3;
  // 同一保存网络两次连接尝试之间的等待时间，单位为毫秒。
  uint32_t retry_interval_ms = 1500;
  // 等待期间轮询 WiFi 状态的间隔，单位为毫秒。
  uint32_t poll_interval_ms = 200;
};

/**
 * @brief 暂停或恢复后台自动连接，避免与用户手动连接互相抢占
 * @param paused true 暂停，false 恢复
 */
void SetWifiAutoConnectPaused(bool paused);

/**
 * @brief 判断后台自动连接当前是否暂停
 * @return 已暂停返回 true
 */
bool IsWifiAutoConnectPaused();

/**
 * @brief 扫描并按信号强度尝试当前可见的自动连接网络
 * @param wifi WiFi 状态和控制提供者
 * @param options 自动连接控制选项
 * @return 自动连接处理结果
 */
WifiAutoConnectResult TryStartWifiAutoConnect(
    hal::WifiProvider* wifi, const WifiAutoConnectOptions& options);

}  // namespace lilygo_box::app
