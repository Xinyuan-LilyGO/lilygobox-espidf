/*
 * @Description: App-level WiFi control helpers
 * @Author: LILYGO_L
 * @Date: 2026-06-25 00:00:00
 * @LastEditTime: 2026-06-25 00:00:00
 * @License: GPL 3.0
 */
#include "app/wifi_manager.h"

#include <cstddef>
#include <cstring>

#include "app/storage/wifi_storage.h"
#include "base/logger.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace lilygo_box::app {
namespace {

/**
 * @brief 读取 WiFi 状态，失败时返回默认未连接状态
 * @param wifi WiFi 状态提供者
 * @return 当前 WiFi 状态
 */
hal::WifiStatus ReadWifiStatusOrDefault(hal::WifiProvider* wifi) {
  hal::WifiStatus status;
  if (wifi != nullptr) {
    wifi->ReadWifiStatus(&status);
  }
  return status;
}

/**
 * @brief 根据选项等待 WiFi 驱动初始化完成
 * @param wifi WiFi 状态和控制提供者
 * @param options 自动连接控制选项
 * @return 驱动已就绪返回 true，否则返回 false
 */
bool WaitForWifiDriverReady(
    hal::WifiProvider* wifi, const WifiAutoConnectOptions& options) {
  if (!options.wait_for_driver) {
    return false;
  }

  const TickType_t deadline =
      xTaskGetTickCount() + pdMS_TO_TICKS(options.wait_timeout_ms);
  while (static_cast<int32_t>(xTaskGetTickCount() - deadline) < 0) {
    const hal::WifiStatus status = ReadWifiStatusOrDefault(wifi);
    if (status.connected || status.got_ip) {
      return true;
    }
    if (status.driver_initialized && !status.init_task_running) {
      return true;
    }
    vTaskDelay(pdMS_TO_TICKS(options.poll_interval_ms));
  }
  return false;
}

}  // namespace

bool LoadWifiAutoConnectTarget(WifiSavedNetwork* target) {
  if (target == nullptr) {
    return false;
  }

  WifiPreferences preferences = GetWifiPreferences();
  if (!preferences.enabled_requested ||
      preferences.auto_connect_ssid[0] == '\0') {
    return false;
  }

  WifiSavedNetwork saved_networks[kWifiSavedNetworkCapacity] = {};
  size_t saved_network_count = 0;
  if (!LoadWifiSavedNetworksFromNvs(
          saved_networks, kWifiSavedNetworkCapacity, &saved_network_count)) {
    return false;
  }

  for (size_t i = 0; i < saved_network_count; ++i) {
    if (std::strcmp(
            saved_networks[i].ssid, preferences.auto_connect_ssid) == 0) {
      if (saved_networks[i].secure &&
          saved_networks[i].password[0] == '\0') {
        return false;
      }
      *target = saved_networks[i];
      return true;
    }
  }
  return false;
}

WifiAutoConnectResult TryStartWifiAutoConnect(
    hal::WifiProvider* wifi, const WifiAutoConnectOptions& options) {
  if (wifi == nullptr) {
    LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
        "WiFi auto connect provider is unavailable\n");
    return WifiAutoConnectResult::kUnavailable;
  }

  WifiSavedNetwork target;
  if (!LoadWifiAutoConnectTarget(&target)) {
    return WifiAutoConnectResult::kDisabled;
  }

  hal::WifiStatus status = ReadWifiStatusOrDefault(wifi);
  if (status.connected || status.got_ip) {
    return WifiAutoConnectResult::kAlreadyConnected;
  }

  if (!status.driver_initialized || status.init_task_running) {
    if (options.start_driver_if_needed && !status.driver_initialized &&
        !wifi->StartWifi()) {
      LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
          "StartWifi failed before auto connect\n");
      return WifiAutoConnectResult::kFailed;
    }
    if (!WaitForWifiDriverReady(wifi, options)) {
      LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
          "WiFi driver is not ready for auto connect\n");
      return WifiAutoConnectResult::kWaitingForDriver;
    }
    status = ReadWifiStatusOrDefault(wifi);
  }

  if (status.connected || status.got_ip) {
    return WifiAutoConnectResult::kAlreadyConnected;
  }
  if (!status.driver_initialized || status.init_task_running) {
    LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
        "WiFi driver remains unavailable after auto connect wait\n");
    return WifiAutoConnectResult::kWaitingForDriver;
  }

  if (!wifi->ConnectWifi(target.ssid, target.password)) {
    LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
        "ConnectWifi failed for auto connect target ssid=%s\n", target.ssid);
    return WifiAutoConnectResult::kFailed;
  }
  return WifiAutoConnectResult::kStarted;
}

}  // namespace lilygo_box::app
