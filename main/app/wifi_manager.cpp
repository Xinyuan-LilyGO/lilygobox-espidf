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
#include <memory>
#include <new>

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
 * @brief 判断扫描结果中是否包含指定热点
 * @param scan_status 最近一次扫描状态
 * @param ssid 目标热点名称
 * @return 扫描结果包含目标热点返回 true，否则返回 false
 */
bool ContainsWifiNetwork(
    const hal::WifiScanStatus& scan_status, const char* ssid) {
  if (ssid == nullptr || ssid[0] == '\0') {
    return false;
  }
  for (size_t i = 0; i < scan_status.network_count; ++i) {
    if (std::strcmp(scan_status.networks[i].ssid, ssid) == 0) {
      return true;
    }
  }
  return false;
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

  const WifiPreferences preferences = GetWifiPreferences();
  if (!preferences.enabled_requested ||
      preferences.auto_connect_ssid[0] == '\0') {
    return false;
  }

  std::unique_ptr<WifiSavedNetwork[]> saved_networks(
      new (std::nothrow) WifiSavedNetwork[kWifiSavedNetworkCapacity]());
  if (saved_networks == nullptr) {
    return false;
  }
  size_t saved_network_count = 0;
  if (!LoadWifiSavedNetworksFromNvs(
          saved_networks.get(), kWifiSavedNetworkCapacity,
          &saved_network_count)) {
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

  const WifiPreferences preferences = GetWifiPreferences();
  if (!preferences.enabled_requested) {
    return WifiAutoConnectResult::kDisabled;
  }
  WifiSavedNetwork target;
  const bool has_saved_target = LoadWifiAutoConnectTarget(&target);

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
  if (!has_saved_target) {
    return WifiAutoConnectResult::kNoSavedTarget;
  }

  hal::WifiScanStatus scan_status;
  if (!wifi->ReadWifiScanStatus(&scan_status) || !wifi->StartWifiScan()) {
    LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
        "StartWifiScan failed before auto connect\n");
    return WifiAutoConnectResult::kFailed;
  }
  const uint32_t initial_scan_generation = scan_status.generation;
  const TickType_t scan_deadline =
      xTaskGetTickCount() + pdMS_TO_TICKS(options.scan_timeout_ms);
  while (static_cast<int32_t>(xTaskGetTickCount() - scan_deadline) < 0) {
    if (!wifi->ReadWifiScanStatus(&scan_status)) {
      return WifiAutoConnectResult::kFailed;
    }
    const bool scan_completed =
        scan_status.generation != initial_scan_generation;
    if (scan_completed && !scan_status.scan_running) {
      if (scan_status.scan_failed) {
        return WifiAutoConnectResult::kFailed;
      }
      if (!ContainsWifiNetwork(scan_status, target.ssid)) {
        return WifiAutoConnectResult::kNetworkNotFound;
      }
      break;
    }
    vTaskDelay(pdMS_TO_TICKS(options.poll_interval_ms));
  }
  if (scan_status.generation == initial_scan_generation ||
      scan_status.scan_running) {
    LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
        "WiFi scan did not finish before auto connect timeout\n");
    return WifiAutoConnectResult::kWaitingForScan;
  }

  if (!wifi->ConnectWifi(target.ssid, target.password)) {
    LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
        "ConnectWifi failed for auto connect target ssid=%s\n", target.ssid);
    return WifiAutoConnectResult::kFailed;
  }
  return WifiAutoConnectResult::kStarted;
}

}  // namespace lilygo_box::app
