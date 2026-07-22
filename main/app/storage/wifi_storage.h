/**
 * @Description: WLAN 偏好缓存与实时 NVS 持久化
 * @Author: LILYGO_L
 * @Date: 2026-06-23 00:00:00
 * @LastEditTime: 2026-07-16 22:36:38
 * @License: GPL 3.0
 */
#pragma once

#include <cstddef>

#include "hal/providers/wifi_provider.h"

namespace lilygo_box::app {

constexpr size_t kWifiSavedNetworkCapacity = 10;
// 未获得扫描或连接结果时使用的未知信号强度。
inline constexpr int kWifiUnknownRssi = -100;

// WLAN 已保存网络凭据，保存用户确认连接后的 SSID 与连接元数据。
struct WifiSavedNetwork {
  // 已保存热点 SSID，用来在 Saved WLAN 与附近 WLAN 间去重。
  char ssid[hal::kWifiSsidMaxLength + 1] = {};
  // 用户确认连接时输入的密码，开放网络保持为空字符串。
  char password[hal::kWifiPasswordMaxLength + 1] = {};
  // 热点是否需要密码，用于重连和详情页安全性显示。
  bool secure = false;
  // 热点是否位于 5 GHz 频段，只使用扫描结果或连接状态更新。
  bool is_5g = false;
  // 最近一次扫描得到的 RSSI，仅用于运行期展示，不写入 NVS。
  int rssi = kWifiUnknownRssi;
};

// WLAN 用户偏好，保存开关状态和自动连接目标。
struct WifiPreferences {
  // 用户期望的 WLAN 开关状态。
  bool enabled_requested = false;
  // 自动连接的 SSID，空字符串表示关闭自动连接。
  char auto_connect_ssid[hal::kWifiSsidMaxLength + 1] = {};
};

/**
 * @brief 比较并更新已保存 WLAN 凭据，存在变化时立即写入 NVS
 * @param networks 已保存 WLAN 凭据数组
 * @param count 已保存 WLAN 凭据数量
 * @return 无变化或 NVS 提交成功返回 true，否则返回 false
 */
bool UpdateWifiSavedNetworks(
    const WifiSavedNetwork* networks, size_t count);

/**
 * @brief 从启动时初始化的长期 RAM 缓存读取 WLAN 凭据
 * @param networks 已保存 WLAN 凭据输出数组
 * @param capacity 输出数组容量
 * @param count 实际读取数量
 * @return 读取成功返回 true
 */
bool GetWifiSavedNetworks(
    WifiSavedNetwork* networks, size_t capacity, size_t* count);

/**
 * @brief 一次打开 NVS 并初始化 WLAN 偏好与凭据长期 RAM 缓存
 */
void InitWifiCache();

/**
 * @brief 从长期 RAM 缓存读取 WLAN 偏好
 * @return WLAN 偏好
 */
WifiPreferences GetWifiPreferences();

/**
 * @brief 判断缓存中是否存在有效的 WLAN 偏好
 * @return 存在有效偏好返回 true，否则返回 false
 */
bool HasWifiPreferences();

/**
 * @brief 比较并更新 WLAN 偏好，存在变化时立即写入 NVS
 * @param preferences 新的 WLAN 偏好
 * @return 无变化或 NVS 提交成功返回 true，否则返回 false
 */
bool UpdateWifiPreferences(const WifiPreferences& preferences);

}  // namespace lilygo_box::app
