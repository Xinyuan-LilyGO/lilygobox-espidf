/*
 * @Description: Settings WLAN NVS storage helpers
 * @Author: LILYGO_L
 * @Date: 2026-06-23 00:00:00
 * @LastEditTime: 2026-06-25 10:47:16
 * @License: GPL 3.0
 */
#pragma once

#include <cstddef>

#include "hal/providers/wifi_provider.h"

namespace lilygo_box::app {

constexpr size_t kWifiSavedNetworkCapacity = 10;

// WLAN 已保存网络凭据，保存用户确认连接后的 SSID 与连接元数据
struct WifiSavedNetwork {
  // 已保存热点 SSID，用来在 Saved WLAN 与附近 WLAN 间去重。
  char ssid[hal::kWifiSsidMaxLength + 1] = {};
  // 用户确认连接时输入的密码，开放网络保持为空字符串。
  char password[hal::kWifiPasswordMaxLength + 1] = {};
  // 热点是否需要密码，用于重连和详情页安全性显示。
  bool secure = false;
  // 热点是否位于 5 GHz 频段，只使用扫描结果或连接状态更新。
  bool is_5g = false;
  // 最近一次已知 RSSI，用于 Saved WLAN 行和详情页展示。
  int rssi = 0;
};

// WLAN 用户偏好，保存开关状态和自动连接目标
struct WifiPreferences {
  // 用户期望的 WLAN 开关状态。
  bool enabled_requested = false;
  // 自动连接的 SSID，空字符串表示关闭自动连接。
  char auto_connect_ssid[hal::kWifiSsidMaxLength + 1] = {};
};

/**
 * @brief 将已保存 WLAN 凭据写入 ESP32-P4 NVS
 * @param networks 已保存 WLAN 凭据数组
 * @param count 已保存 WLAN 凭据数量
 * @return 保存成功返回 true，否则返回 false
 */
bool SaveWifiSavedNetworksToNvs(
    const WifiSavedNetwork* networks, size_t count);

/**
 * @brief 从 ESP32-P4 NVS 读取已保存 WLAN 凭据
 * @param networks 已保存 WLAN 凭据输出数组
 * @param capacity 输出数组容量
 * @param count 实际读取到的 WLAN 凭据数量
 * @return 读取成功或没有保存内容返回 true，读取异常返回 false
 */
bool LoadWifiSavedNetworksFromNvs(
    WifiSavedNetwork* networks, size_t capacity, size_t* count);

/**
 * @brief 将 WLAN 开关和自动连接偏好写入 ESP32-P4 NVS
 * @param preferences WLAN 用户偏好
 * @return 保存成功返回 true，否则返回 false
 */
bool SaveWifiPreferencesToNvs(const WifiPreferences& preferences);

/**
 * @brief 从 ESP32-P4 NVS 读取 WLAN 开关和自动连接偏好
 * @param preferences WLAN 用户偏好输出地址
 * @return 读取成功返回 true，没有保存内容或读取异常返回 false
 */
bool LoadWifiPreferencesFromNvs(WifiPreferences* preferences);

}  // namespace lilygo_box::app
