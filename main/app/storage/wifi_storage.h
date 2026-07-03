/**
 * @Description: WLAN 偏好存储，内部维护内存缓存
 * @Author: LILYGO_L
 * @Date: 2026-06-23 00:00:00
 * @LastEditTime: 2026-07-03 00:00:00
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
 * @brief 将已保存 WLAN 凭据写入 NVS
 * @param networks 已保存 WLAN 凭据数组
 * @param count 已保存 WLAN 凭据数量
 * @return 保存成功返回 true
 */
bool SaveWifiSavedNetworksToNvs(
    const WifiSavedNetwork* networks, size_t count);

/**
 * @brief 从 NVS 读取已保存 WLAN 凭据
 * @param networks 已保存 WLAN 凭据输出数组
 * @param capacity 输出数组容量
 * @param count 实际读取到的数量
 * @return 读取成功返回 true
 */
bool LoadWifiSavedNetworksFromNvs(
    WifiSavedNetwork* networks, size_t capacity, size_t* count);

/**
 * @brief 初始化 WLAN 偏好缓存，从 NVS 加载到内存
 */
void InitWifiCache();

/**
 * @brief 读取 WLAN 偏好（纯内存，零 NVS 访问）
 * @return WLAN 偏好
 */
WifiPreferences GetWifiPreferences();

/**
 * @brief 更新 WLAN 偏好并持久化到 NVS
 * @param preferences 新的 WLAN 偏好
 * @return 更新成功返回 true
 */
bool UpdateWifiPreferences(const WifiPreferences& preferences);

}  // namespace lilygo_box::app
