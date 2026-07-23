/*
 * @Description: App-level WiFi control helpers
 * @Author: LILYGO_L
 * @Date: 2026-06-25 00:00:00
 * @LastEditTime: 2026-07-22 00:00:00
 * @License: GPL 3.0
 */
#include "app/wifi_manager.h"

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstring>

#include "app/storage/wifi_storage.h"
#include "base/logger.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace lilygo_box::app {
namespace {

std::atomic<bool> g_wifi_auto_connect_paused{false};

enum class WifiConnectionWaitResult {
  kConnected,
  kPaused,
  kAuthenticationFailure,
  kTransientFailure,
  kTimeout,
};

enum class WifiScanFilterResult {
  kCompleted,
  kUnavailable,
  kPaused,
};

struct WifiAutoConnectCandidate {
  WifiSavedNetwork network;
  int rssi = kWifiUnknownRssi;
};

/**
 * @brief 判断断开原因是否表示密码或认证失败
 * @param reason ESP WiFi 断开原因
 * @return 密码或认证失败返回 true
 */
bool IsWifiAuthenticationFailure(int reason) {
  return reason == WIFI_REASON_AUTH_FAIL ||
      reason == WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT ||
      reason == WIFI_REASON_HANDSHAKE_TIMEOUT;
}

/**
 * @brief 获取自动连接结果的日志名称
 * @param result 自动连接等待结果
 * @return 静态日志名称
 */
const char* WifiConnectionWaitResultName(WifiConnectionWaitResult result) {
  switch (result) {
    case WifiConnectionWaitResult::kConnected:
      return "connected";
    case WifiConnectionWaitResult::kPaused:
      return "paused";
    case WifiConnectionWaitResult::kAuthenticationFailure:
      return "authentication_failure";
    case WifiConnectionWaitResult::kTransientFailure:
      return "transient_failure";
    case WifiConnectionWaitResult::kTimeout:
      return "timeout";
  }
  return "unknown";
}

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
 * @return 驱动已就绪返回 true
 */
bool WaitForWifiDriverReady(
    hal::WifiProvider* wifi, const WifiAutoConnectOptions& options) {
  if (!options.wait_for_driver) {
    return false;
  }

  const TickType_t deadline =
      xTaskGetTickCount() + pdMS_TO_TICKS(options.wait_timeout_ms);
  while (static_cast<int32_t>(xTaskGetTickCount() - deadline) < 0) {
    if (IsWifiAutoConnectPaused()) {
      return false;
    }
    const hal::WifiStatus status = ReadWifiStatusOrDefault(wifi);
    if (status.got_ip ||
        (status.driver_initialized && !status.init_task_running)) {
      return true;
    }
    vTaskDelay(pdMS_TO_TICKS(options.poll_interval_ms));
  }
  return false;
}

/**
 * @brief 启动一次 WiFi 扫描并等待新的扫描结果
 * @param wifi WiFi 状态提供者
 * @param options 自动连接控制选项
 * @param scan_status 完成后的扫描结果
 * @return 扫描完成、不可用或被暂停
 */
WifiScanFilterResult WaitForWifiScan(hal::WifiProvider* wifi,
    const WifiAutoConnectOptions& options,
    hal::WifiScanStatus* scan_status) {
  if (wifi == nullptr || scan_status == nullptr ||
      !wifi->ReadWifiScanStatus(scan_status)) {
    return WifiScanFilterResult::kUnavailable;
  }
  const uint32_t initial_generation = scan_status->generation;
  if (!wifi->StartWifiScan()) {
    return WifiScanFilterResult::kUnavailable;
  }

  const TickType_t deadline = xTaskGetTickCount() +
      pdMS_TO_TICKS(options.scan_timeout_ms);
  while (static_cast<int32_t>(xTaskGetTickCount() - deadline) < 0) {
    if (IsWifiAutoConnectPaused() ||
        ReadWifiStatusOrDefault(wifi).time_test_running) {
      return WifiScanFilterResult::kPaused;
    }
    if (!wifi->ReadWifiScanStatus(scan_status)) {
      return WifiScanFilterResult::kUnavailable;
    }
    if (scan_status->generation != initial_generation &&
        !scan_status->scan_running) {
      return scan_status->scan_failed
          ? WifiScanFilterResult::kUnavailable
          : WifiScanFilterResult::kCompleted;
    }
    vTaskDelay(pdMS_TO_TICKS(options.poll_interval_ms));
  }
  return WifiScanFilterResult::kUnavailable;
}

/**
 * @brief 使用扫描结果筛选自动连接目标并按信号强度排序
 * @param scan_status 最新扫描结果
 * @param candidates 自动连接候选数组
 * @param candidate_count 输入候选数量
 * @return 当前扫描结果中可见的候选数量
 */
size_t FilterVisibleWifiCandidates(const hal::WifiScanStatus& scan_status,
    WifiAutoConnectCandidate* candidates, size_t candidate_count) {
  if (candidates == nullptr) {
    return 0;
  }
  size_t visible_count = 0;
  for (size_t candidate_index = 0; candidate_index < candidate_count;
       ++candidate_index) {
    int strongest_rssi = kWifiUnknownRssi;
    bool visible = false;
    for (size_t scan_index = 0; scan_index < scan_status.network_count;
         ++scan_index) {
      const hal::WifiNetworkInfo& scanned =
          scan_status.networks[scan_index];
      if (std::strcmp(candidates[candidate_index].network.ssid,
              scanned.ssid) != 0) {
        continue;
      }
      strongest_rssi = visible
          ? std::max(strongest_rssi, scanned.rssi)
          : scanned.rssi;
      visible = true;
    }
    if (!visible) {
      continue;
    }
    candidates[candidate_index].rssi = strongest_rssi;
    if (visible_count != candidate_index) {
      candidates[visible_count] = candidates[candidate_index];
    }
    ++visible_count;
  }
  // 候选上限只有 10 项，使用插入排序可避免 GCC 对 std::sort
  // 内部 16 项阈值产生错误的数组越界诊断。
  for (size_t index = 1; index < visible_count; ++index) {
    const WifiAutoConnectCandidate candidate = candidates[index];
    size_t insert_index = index;
    while (insert_index > 0 &&
           candidates[insert_index - 1].rssi < candidate.rssi) {
      candidates[insert_index] = candidates[insert_index - 1];
      --insert_index;
    }
    candidates[insert_index] = candidate;
  }
  return visible_count;
}

/**
 * @brief 等待当前自动连接尝试结束
 * @param wifi WiFi 状态提供者
 * @param options 自动连接控制选项
 * @param final_status 最后一次读取到的 WiFi 状态
 * @return 当前连接尝试的结束原因
 */
WifiConnectionWaitResult WaitForWifiConnection(hal::WifiProvider* wifi,
    const WifiAutoConnectOptions& options, hal::WifiStatus* final_status) {
  const TickType_t deadline = xTaskGetTickCount() +
      pdMS_TO_TICKS(options.connection_timeout_ms);
  while (static_cast<int32_t>(xTaskGetTickCount() - deadline) < 0) {
    if (IsWifiAutoConnectPaused()) {
      return WifiConnectionWaitResult::kPaused;
    }
    const hal::WifiStatus status = ReadWifiStatusOrDefault(wifi);
    if (final_status != nullptr) {
      *final_status = status;
    }
    if (status.time_test_running) {
      return WifiConnectionWaitResult::kPaused;
    }
    if (status.got_ip) {
      return WifiConnectionWaitResult::kConnected;
    }
    if (!status.connect_task_running &&
        (status.start_failed || status.disconnect_reason != 0)) {
      return IsWifiAuthenticationFailure(status.disconnect_reason)
          ? WifiConnectionWaitResult::kAuthenticationFailure
          : WifiConnectionWaitResult::kTransientFailure;
    }
    vTaskDelay(pdMS_TO_TICKS(options.poll_interval_ms));
  }
  wifi->CancelWifiConnection();
  return WifiConnectionWaitResult::kTimeout;
}

/**
 * @brief 等待下一次自动连接重试并响应用户暂停请求
 * @param options 自动连接控制选项
 * @return 等待完成返回 true，期间被暂停返回 false
 */
bool WaitForWifiRetry(hal::WifiProvider* wifi,
    const WifiAutoConnectOptions& options) {
  uint32_t elapsed_ms = 0;
  const uint32_t poll_interval_ms = std::max<uint32_t>(
      1, options.poll_interval_ms);
  while (elapsed_ms < options.retry_interval_ms) {
    if (IsWifiAutoConnectPaused() ||
        ReadWifiStatusOrDefault(wifi).time_test_running) {
      return false;
    }
    const uint32_t delay_ms = std::min(
        poll_interval_ms, options.retry_interval_ms - elapsed_ms);
    vTaskDelay(pdMS_TO_TICKS(delay_ms));
    elapsed_ms += delay_ms;
  }
  return true;
}

/**
 * @brief 记录保存网络的自动连接失败信息
 * @param network 保存网络
 * @param attempt 当前尝试次数
 * @param attempt_limit 最大尝试次数
 * @param result 连接等待结果
 * @param status 最后一次 WiFi 状态
 */
void LogWifiAutoConnectFailure(const WifiSavedNetwork& network,
    uint32_t attempt, uint32_t attempt_limit,
    WifiConnectionWaitResult result, const hal::WifiStatus& status) {
  LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
      "WiFi auto-connect failed: ssid=%s, attempt=%lu/%lu, "
      "result=%s, reason=%d, error=%d\n",
      network.ssid, static_cast<unsigned long>(attempt),
      static_cast<unsigned long>(attempt_limit),
      WifiConnectionWaitResultName(result), status.disconnect_reason,
      status.last_error);
}

}  // namespace

void SetWifiAutoConnectPaused(bool paused) {
  g_wifi_auto_connect_paused.store(paused);
}

bool IsWifiAutoConnectPaused() {
  return g_wifi_auto_connect_paused.load();
}

WifiAutoConnectResult TryStartWifiAutoConnect(
    hal::WifiProvider* wifi, const WifiAutoConnectOptions& options) {
  if (wifi == nullptr) {
    return WifiAutoConnectResult::kUnavailable;
  }
  if (IsWifiAutoConnectPaused()) {
    return WifiAutoConnectResult::kPaused;
  }

  const WifiPreferences preferences = GetWifiPreferences();
  if (!preferences.enabled_requested) {
    return WifiAutoConnectResult::kDisabled;
  }

  hal::WifiStatus status = ReadWifiStatusOrDefault(wifi);
  if (status.time_test_running) {
    return WifiAutoConnectResult::kPaused;
  }
  if (status.got_ip) {
    return WifiAutoConnectResult::kAlreadyConnected;
  }
  if (status.connect_task_running) {
    return WifiAutoConnectResult::kPaused;
  }

  if (!status.driver_initialized || status.init_task_running) {
    if (options.start_driver_if_needed && !status.driver_initialized &&
        !wifi->SetWifiEnabled(true)) {
      return WifiAutoConnectResult::kFailed;
    }
    if (!WaitForWifiDriverReady(wifi, options)) {
      return IsWifiAutoConnectPaused()
          ? WifiAutoConnectResult::kPaused
          : WifiAutoConnectResult::kWaitingForDriver;
    }
    status = ReadWifiStatusOrDefault(wifi);
  }
  if (status.got_ip) {
    return WifiAutoConnectResult::kAlreadyConnected;
  }
  if (!status.driver_initialized || status.init_task_running) {
    return WifiAutoConnectResult::kWaitingForDriver;
  }

  WifiSavedNetwork saved_networks[kWifiSavedNetworkCapacity] = {};
  size_t saved_network_count = 0;
  if (!GetWifiSavedNetworks(saved_networks, kWifiSavedNetworkCapacity,
          &saved_network_count)) {
    return WifiAutoConnectResult::kFailed;
  }

  WifiAutoConnectCandidate candidates[kWifiSavedNetworkCapacity] = {};
  size_t candidate_count = 0;
  for (size_t index = 0; index < saved_network_count; ++index) {
    const WifiSavedNetwork& network = saved_networks[index];
    if (!network.auto_connect || network.ssid[0] == '\0' ||
        (network.secure && network.password[0] == '\0')) {
      continue;
    }
    candidates[candidate_count++].network = network;
  }
  if (candidate_count == 0) {
    return WifiAutoConnectResult::kNoSavedTarget;
  }

  hal::WifiScanStatus scan_status;
  const WifiScanFilterResult scan_result =
      WaitForWifiScan(wifi, options, &scan_status);
  if (scan_result == WifiScanFilterResult::kPaused) {
    return WifiAutoConnectResult::kPaused;
  }
  if (scan_result == WifiScanFilterResult::kCompleted) {
    candidate_count = FilterVisibleWifiCandidates(
        scan_status, candidates, candidate_count);
    if (candidate_count == 0) {
      return WifiAutoConnectResult::kNoVisibleTarget;
    }
  } else {
    LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
        "WiFi scan unavailable; use saved auto-connect order\n");
  }

  for (size_t index = 0; index < candidate_count; ++index) {
    const WifiSavedNetwork& network = candidates[index].network;
    if (IsWifiAutoConnectPaused()) {
      return WifiAutoConnectResult::kPaused;
    }
    const uint32_t attempt_limit =
        std::max<uint32_t>(1, options.attempts_per_network);
    for (uint32_t attempt = 1; attempt <= attempt_limit; ++attempt) {
      hal::WifiStatus final_status;
      WifiConnectionWaitResult wait_result =
          WifiConnectionWaitResult::kTransientFailure;
      if (wifi->ConnectWifi(network.ssid, network.password)) {
        wait_result = WaitForWifiConnection(
            wifi, options, &final_status);
      } else {
        final_status = ReadWifiStatusOrDefault(wifi);
        if (final_status.got_ip) {
          return WifiAutoConnectResult::kConnected;
        }
        if (final_status.connect_task_running) {
          wait_result = WaitForWifiConnection(
              wifi, options, &final_status);
        }
      }

      if (wait_result == WifiConnectionWaitResult::kConnected) {
        return WifiAutoConnectResult::kConnected;
      }
      if (wait_result == WifiConnectionWaitResult::kPaused ||
          IsWifiAutoConnectPaused()) {
        return WifiAutoConnectResult::kPaused;
      }

      LogWifiAutoConnectFailure(
          network, attempt, attempt_limit, wait_result, final_status);
      if (wait_result ==
              WifiConnectionWaitResult::kAuthenticationFailure ||
          attempt == attempt_limit) {
        break;
      }
      if (!WaitForWifiRetry(wifi, options)) {
        return WifiAutoConnectResult::kPaused;
      }
    }
  }

  LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
      "All saved WiFi auto-connect targets failed\n");
  return WifiAutoConnectResult::kFailed;
}

}  // namespace lilygo_box::app
