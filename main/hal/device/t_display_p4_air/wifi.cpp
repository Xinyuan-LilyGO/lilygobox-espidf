/*
 * @Description: T-Display-P4-Air WiFi 协处理器实现
 * @Author: LILYGO_L
 * @Date: 2026-08-28 00:00:00
 * @LastEditTime: 2026-09-02 17:53:44
 * @License: GPL 3.0
 */
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <new>

#include "base/logger.h"
#include "esp_err.h"
#include "esp_event.h"
#include "esp_hosted.h"
#include "esp_hosted_transport_config.h"
#include "esp_netif.h"
#include "esp_sntp.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "esp_wifi_default.h"
#include "esp_wifi_remote.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "hal/device/common/device_utils.h"
#include "hal/device/common/wifi_utils.h"
#include "hal/device/t_display_p4_air/device.h"

namespace lilygo_box::hal {
namespace {

constexpr uint32_t kWifiInitTaskStackBytes = 6 * 1024;
constexpr UBaseType_t kWifiInitTaskPriority = 3;
constexpr uint32_t kWifiScanTaskStackBytes = 6 * 1024;
constexpr UBaseType_t kWifiScanTaskPriority = 3;
constexpr uint32_t kWifiConnectTaskStackBytes = 6 * 1024;
constexpr UBaseType_t kWifiConnectTaskPriority = 3;
constexpr uint32_t kWifiHardwareReadyTimeoutMs = 8000;
constexpr uint32_t kWifiHardwareReadyPollMs = 50;
constexpr uint32_t kWifiCoprocessorBootDelayMs = 500;
constexpr uint32_t kWifiScanTimeoutMs = 8000;
constexpr uint32_t kWifiScanStateRetryIntervalMs = 500;
constexpr const char* kFactoryWifiSsid = "LilyGo-AABB";
constexpr const char* kFactoryWifiPassword = "xinyuandianzi";
constexpr const char* kWifiSntpServer = "pool.ntp.org";
constexpr int kWifiSntpMaxAttemptCount = 3;
constexpr uint32_t kWifiSntpAttemptIntervalMs =
    kWifiInternetCheckTimeoutMs / kWifiSntpMaxAttemptCount;
static_assert(kWifiSntpAttemptIntervalMs * kWifiSntpMaxAttemptCount ==
              kWifiInternetCheckTimeoutMs);
constexpr int kWifiMaxReconnectCount = 8;

/**
 * @brief 获取当前接收 SNTP 时间同步回调的设备实例
 * @return 保存回调目标设备的原子指针
 */
std::atomic<TDisplayP4AirDevice*>& WifiTimeSyncOwner() {
  static std::atomic<TDisplayP4AirDevice*> owner{nullptr};
  return owner;
}

/**
 * @brief 控制板载 WiFi 协处理器电源
 * @param driver 当前板级驱动
 * @param enabled true 上电，false 断电
 * @return GPIO 状态切换成功返回 true，否则返回 false
 */
bool SetWifiCoprocessorPowerEnabled(
    TDisplayP4AirBoardDriver& driver, bool enabled) {
  return driver.SetEsp32c5PowerEnabled(enabled);
}

}  // namespace

bool TDisplayP4AirDevice::SetWifiEnabled(bool enabled) {
  if (!enabled) {
    wifi_time_test_.requested.store(false);
    wifi_.connect_cancel_requested.store(true);
    wifi_.stop_requested.store(true);
    wifi_.scan_requested.store(false);
    if (wifi_time_test_.active.load()) {
      StopWifiTimeTest();
    } else {
      StopWifiInternetCheck();
    }

    if (!wifi_.driver_initialized.load()) {
      if (wifi_.init_task_running.load()) {
        const bool power_disabled =
            SetWifiCoprocessorPowerEnabled(driver_, false);
        LogMessage(power_disabled ? LogLevel::kDebug : LogLevel::kError,
            __FILE__, __LINE__,
            power_disabled
                ? "WiFi power disabled while initialization is stopping\n"
                : "Disable WiFi power during initialization failed\n");
        return power_disabled;
      }
      esp_event_handler_unregister(
          WIFI_EVENT, ESP_EVENT_ANY_ID, WifiEventHandler);
      esp_event_handler_unregister(
          IP_EVENT, IP_EVENT_STA_GOT_IP, WifiGotIpEventHandler);
      esp_wifi_deinit();
      if (wifi_.netif != nullptr) {
        esp_netif_destroy_default_wifi(wifi_.netif);
        wifi_.netif = nullptr;
      }
      if (wifi_.hosted_bridge_initialized.exchange(false)) {
        esp_hosted_deinit();
      }
      wifi_.scan_running.store(false);
      wifi_.scan_task_running.store(false);
      wifi_.connect_task_running.store(false);
      wifi_.running.store(false);
      wifi_.connected.store(false);
      wifi_.got_ip.store(false);
      return SetWifiCoprocessorPowerEnabled(driver_, false);
    }

    if (wifi_.scan_running.load() || wifi_.scan_task_running.load()) {
      const esp_err_t scan_result = esp_wifi_scan_stop();
      if (scan_result != ESP_OK && scan_result != ESP_ERR_WIFI_NOT_STARTED &&
          scan_result != ESP_ERR_INVALID_STATE &&
          scan_result != ESP_ERR_WIFI_STATE) {
        SetWifiFailure(scan_result);
        SetWifiCoprocessorPowerEnabled(driver_, false);
        return false;
      }
    }
    esp_wifi_disconnect();
    wifi_config_t empty_config = {};
    esp_wifi_set_config(WIFI_IF_STA, &empty_config);
    esp_err_t result = esp_wifi_stop();
    if (result != ESP_OK && result != ESP_ERR_WIFI_NOT_STARTED) {
      SetWifiFailure(result);
      SetWifiCoprocessorPowerEnabled(driver_, false);
      return false;
    }

    result = esp_wifi_set_mode(WIFI_MODE_NULL);
    if (result != ESP_OK) {
      SetWifiFailure(result);
      SetWifiCoprocessorPowerEnabled(driver_, false);
      return false;
    }

    esp_event_handler_unregister(
        WIFI_EVENT, ESP_EVENT_ANY_ID, WifiEventHandler);
    esp_event_handler_unregister(
        IP_EVENT, IP_EVENT_STA_GOT_IP, WifiGotIpEventHandler);
    result = esp_wifi_deinit();
    if (result != ESP_OK && result != ESP_ERR_WIFI_NOT_INIT) {
      SetWifiFailure(result);
      SetWifiCoprocessorPowerEnabled(driver_, false);
      return false;
    }
    wifi_.driver_initialized.store(false);
    if (wifi_.netif != nullptr) {
      esp_netif_destroy_default_wifi(wifi_.netif);
      wifi_.netif = nullptr;
    }
    if (wifi_.hosted_bridge_initialized.exchange(false)) {
      result = static_cast<esp_err_t>(esp_hosted_deinit());
      if (result != ESP_OK) {
        SetWifiFailure(result);
        SetWifiCoprocessorPowerEnabled(driver_, false);
        return false;
      }
    }

    wifi_.running.store(false);
    wifi_.connect_task_running.store(false);
    wifi_.connected.store(false);
    wifi_.got_ip.store(false);
    wifi_.start_failed.store(false);
    wifi_.last_error.store(ESP_OK);
    wifi_.disconnect_reason.store(0);
    wifi_.retry_count.store(0);
    wifi_.scan_running.store(false);
    wifi_.scan_task_running.store(false);
    wifi_.scan_failed.store(false);
    wifi_.scan_network_count.store(0);
    wifi_.scan_generation.fetch_add(1);
    wifi_.ip_address.store(0);
    wifi_.netmask.store(0);
    wifi_.gateway.store(0);
    return SetWifiCoprocessorPowerEnabled(driver_, false);
  }

  wifi_.stop_requested.store(false);
  if (wifi_.driver_initialized.load() && wifi_.running.load()) {
    return true;
  }

  if (!SetWifiCoprocessorPowerEnabled(driver_, true)) {
    SetWifiFailure(ESP_FAIL);
    return false;
  }

  bool expected = false;
  if (!wifi_.init_task_running.compare_exchange_strong(expected, true)) {
    return true;
  }

  wifi_.start_failed.store(false);
  wifi_.last_error.store(ESP_OK);
  const BaseType_t result = xTaskCreate(WifiInitTaskEntry, "wifi_init",
      kWifiInitTaskStackBytes, this, kWifiInitTaskPriority, nullptr);
  if (result != pdPASS) {
    wifi_.init_task_running.store(false);
    SetWifiFailure(ESP_ERR_NO_MEM);
    SetWifiCoprocessorPowerEnabled(driver_, false);
    return false;
  }
  return true;
}

bool TDisplayP4AirDevice::StartWifiScan() {
  wifi_.stop_requested.store(false);
  if (!wifi_.driver_initialized.load()) {
    wifi_.scan_requested.store(true);
    wifi_.scan_running.store(true);
    if (SetWifiEnabled(true)) {
      return true;
    }
    wifi_.scan_requested.store(false);
    wifi_.scan_running.store(false);
    wifi_.scan_failed.store(true);
    return false;
  }

  wifi_.scan_requested.store(false);
  bool expected = false;
  if (!wifi_.scan_task_running.compare_exchange_strong(expected, true)) {
    return true;
  }

  wifi_.scan_failed.store(false);
  wifi_.last_error.store(ESP_OK);
  wifi_.scan_running.store(true);
  const BaseType_t result = xTaskCreate(WifiScanTaskEntry, "wifi_scan",
      kWifiScanTaskStackBytes, this, kWifiScanTaskPriority, nullptr);
  if (result != pdPASS) {
    wifi_.scan_task_running.store(false);
    wifi_.scan_running.store(false);
    wifi_.scan_failed.store(true);
    wifi_.last_error.store(ESP_ERR_NO_MEM);
    wifi_.scan_generation.fetch_add(1);
    return false;
  }
  return true;
}

bool TDisplayP4AirDevice::ReadWifiScanStatus(WifiScanStatus* status) {
  if (status == nullptr) {
    return false;
  }

  *status = WifiScanStatus();
  status->scan_running = wifi_.scan_running.load();
  if (wifi_.scan_results_mutex != nullptr) {
    xSemaphoreTake(wifi_.scan_results_mutex, portMAX_DELAY);
  }
  status->scan_failed = wifi_.scan_failed.load();
  status->last_error = wifi_.last_error.load();
  status->generation = wifi_.scan_generation.load();
  status->network_count =
      std::min(wifi_.scan_network_count.load(), kMaxWifiScanNetworkCount);
  for (size_t i = 0; i < status->network_count; ++i) {
    status->networks[i] = wifi_.scan_networks[i];
  }
  if (wifi_.scan_results_mutex != nullptr) {
    xSemaphoreGive(wifi_.scan_results_mutex);
  }
  return true;
}

bool TDisplayP4AirDevice::ConnectWifi(const char* ssid, const char* password) {
  if (ssid == nullptr || ssid[0] == '\0' || wifi_.stop_requested.load()) {
    return false;
  }

  if (!wifi_.driver_initialized.load()) {
    if (!SetWifiEnabled(true)) {
      return false;
    }
    return false;
  }

  bool expected = false;
  if (!wifi_.connect_task_running.compare_exchange_strong(expected, true)) {
    return false;
  }

  std::snprintf(wifi_.connect_ssid, sizeof(wifi_.connect_ssid), "%s", ssid);
  std::snprintf(wifi_.connect_password, sizeof(wifi_.connect_password), "%s",
      password == nullptr ? "" : password);
  wifi_.connect_cancel_requested.store(false);
  const BaseType_t result = xTaskCreate(WifiConnectTaskEntry, "wifi_connect",
      kWifiConnectTaskStackBytes, this, kWifiConnectTaskPriority, nullptr);
  if (result != pdPASS) {
    wifi_.connect_task_running.store(false);
    SetWifiFailure(ESP_ERR_NO_MEM);
    return false;
  }
  return true;
}

bool TDisplayP4AirDevice::CancelWifiConnection() {
  wifi_.connect_cancel_requested.store(true);
  wifi_.connect_task_running.store(false);
  StopWifiInternetCheck();
  if (!wifi_.driver_initialized.load()) {
    wifi_.connected.store(false);
    wifi_.got_ip.store(false);
    wifi_.start_failed.store(false);
    wifi_.last_error.store(ESP_OK);
    return true;
  }

  esp_wifi_disconnect();
  wifi_config_t empty_config = {};
  esp_wifi_set_config(WIFI_IF_STA, &empty_config);

  wifi_.connected.store(false);
  wifi_.got_ip.store(false);
  wifi_.start_failed.store(false);
  wifi_.last_error.store(ESP_OK);
  wifi_.disconnect_reason.store(0);
  wifi_.retry_count.store(0);
  wifi_.ip_address.store(0);
  wifi_.netmask.store(0);
  wifi_.gateway.store(0);
  wifi_time_test_.synced.store(false);
  wifi_time_test_.sntp_unix_time.store(0);
  wifi_time_test_.sntp_sync_monotonic_ms.store(0);
  return true;
}

bool TDisplayP4AirDevice::RequestWifiInternetCheck() {
  if (!wifi_.driver_initialized.load() || !wifi_.got_ip.load() ||
      wifi_time_test_.active.load()) {
    return false;
  }

  wifi_time_test_.synced.store(false);
  wifi_time_test_.sntp_unix_time.store(0);
  wifi_time_test_.sntp_sync_monotonic_ms.store(0);
  if (!wifi_time_test_.sync_started.load() || !esp_sntp_enabled()) {
    return StartWifiSntp() == ESP_OK;
  }
  if (StartWifiSntpAttemptTimer() != ESP_OK || !esp_sntp_restart()) {
    StopWifiInternetCheck();
    return false;
  }
  return true;
}

void TDisplayP4AirDevice::StopWifiInternetCheck() {
  wifi_time_test_.sync_started.store(false);
  wifi_time_test_.sntp_attempt_count.store(0);
  if (wifi_time_test_.sntp_attempt_timer != nullptr &&
      esp_timer_is_active(wifi_time_test_.sntp_attempt_timer)) {
    esp_timer_stop(wifi_time_test_.sntp_attempt_timer);
  }
  esp_sntp_set_time_sync_notification_cb(nullptr);
  TDisplayP4AirDevice* owner = this;
  WifiTimeSyncOwner().compare_exchange_strong(owner, nullptr);
  if (esp_sntp_enabled()) {
    esp_sntp_stop();
  }
}

bool TDisplayP4AirDevice::StartWifiTimeTest() {
  wifi_.stop_requested.store(false);
  wifi_time_test_.requested.store(true);
  if (!wifi_.driver_initialized.load()) {
    return SetWifiEnabled(true);
  }

  const int result = StartWifiTimeTestInternal();
  if (result != ESP_OK) {
    SetWifiFailure(result);
    LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
        "WiFi time test start failed: %s (%#X)\n",
        esp_err_to_name(static_cast<esp_err_t>(result)),
        static_cast<unsigned>(result));
    return false;
  }
  return true;
}

bool TDisplayP4AirDevice::StopWifiTimeTest() {
  wifi_time_test_.requested.store(false);
  const bool was_active = wifi_time_test_.active.exchange(false);
  if (!wifi_.driver_initialized.load()) {
    return true;
  }
  if (!was_active && !wifi_time_test_.sync_started.load()) {
    return true;
  }

  StopWifiInternetCheck();
  wifi_time_test_.synced.store(false);
  wifi_time_test_.sntp_unix_time.store(0);
  wifi_time_test_.sntp_sync_monotonic_ms.store(0);
  wifi_.start_failed.store(false);
  wifi_.last_error.store(ESP_OK);
  wifi_.disconnect_reason.store(0);
  wifi_.retry_count.store(0);

  esp_wifi_disconnect();
  wifi_.connected.store(false);
  wifi_.got_ip.store(false);
  wifi_.ip_address.store(0);
  wifi_.netmask.store(0);
  wifi_.gateway.store(0);

  wifi_config_t empty_config = {};
  esp_wifi_set_storage(WIFI_STORAGE_RAM);
  esp_wifi_set_config(WIFI_IF_STA, &empty_config);

  if (wifi_time_test_.previous_sta_config_valid) {
    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_set_storage(WIFI_STORAGE_RAM);
    esp_wifi_set_config(WIFI_IF_STA, &wifi_time_test_.previous_sta_config);
  }

  if (wifi_time_test_.previous_mode_valid) {
    esp_wifi_set_mode(wifi_time_test_.previous_mode);
  } else {
    esp_wifi_set_mode(WIFI_MODE_NULL);
  }

  if (wifi_time_test_.previous_running) {
    const esp_err_t start_result = esp_wifi_start();
    if (start_result != ESP_OK) {
      SetWifiFailure(start_result);
      return false;
    }
    wifi_.running.store(true);
    if (wifi_time_test_.previous_connected) {
      wifi_.connect_task_running.store(true);
      const esp_err_t connect_result = esp_wifi_connect();
      if (connect_result != ESP_OK) {
        wifi_.connect_task_running.store(false);
        SetWifiFailure(connect_result);
        return false;
      }
    }
  } else {
    const esp_err_t stop_result = esp_wifi_stop();
    if (stop_result != ESP_OK && stop_result != ESP_ERR_WIFI_NOT_STARTED) {
      SetWifiFailure(stop_result);
      return false;
    }
    wifi_.running.store(false);
    wifi_.connected.store(false);
    wifi_.got_ip.store(false);
    wifi_.ip_address.store(0);
    wifi_.netmask.store(0);
    wifi_.gateway.store(0);
  }

  wifi_time_test_.previous_running = false;
  wifi_time_test_.previous_connected = false;
  wifi_time_test_.previous_mode_valid = false;
  wifi_time_test_.previous_sta_config_valid = false;
  wifi_time_test_.previous_mode = WIFI_MODE_NULL;
  wifi_time_test_.previous_sta_config = {};
  return true;
}

bool TDisplayP4AirDevice::ReadWifiStatus(WifiStatus* status) {
  if (status == nullptr) {
    return false;
  }

  *status = WifiStatus();
  status->init_task_running = wifi_.init_task_running.load();
  status->connect_task_running = wifi_.connect_task_running.load();
  status->driver_initialized = wifi_.driver_initialized.load();
  status->running = wifi_.running.load();
  status->connected = wifi_.connected.load();
  status->got_ip = wifi_.got_ip.load();
  status->start_failed = wifi_.start_failed.load();
  status->time_test_running = wifi_time_test_.active.load();
  status->time_sync_started = wifi_time_test_.sync_started.load();
  status->retry_count = wifi_.retry_count.load();
  status->last_error = wifi_.last_error.load();
  status->disconnect_reason = wifi_.disconnect_reason.load();
  status->rssi = wifi_.rssi.load();
  status->channel = wifi_.channel.load();
  status->mac_address = wifi_.mac_address.load();
  status->ip_address = wifi_.ip_address.load();
  status->netmask = wifi_.netmask.load();
  status->gateway = wifi_.gateway.load();
  status->connection_generation = wifi_.connection_generation.load();

  if (status->time_test_running) {
    std::strncpy(status->ssid, kFactoryWifiSsid, sizeof(status->ssid) - 1);
  }

  if (status->connected) {
    wifi_ap_record_t ap_info = {};
    if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
      std::memcpy(status->ssid, ap_info.ssid,
          std::min(sizeof(status->ssid) - 1, sizeof(ap_info.ssid)));
      status->rssi = ap_info.rssi;
      status->channel = ap_info.primary;
      wifi_.rssi.store(status->rssi);
      wifi_.channel.store(status->channel);
    }
  }

  const int64_t synced_unix_time = wifi_time_test_.sntp_unix_time.load();
  status->time_synced =
      wifi_time_test_.synced.load() &&
      synced_unix_time > device_utils::kValidUnixTimeThreshold;
  status->unix_time = status->time_synced ? synced_unix_time : 0;
  const int64_t sync_monotonic_ms =
      wifi_time_test_.sntp_sync_monotonic_ms.load();
  if (status->time_synced && sync_monotonic_ms > 0) {
    const int64_t elapsed_ms = esp_timer_get_time() / 1000 - sync_monotonic_ms;
    if (elapsed_ms > 0) {
      status->time_sync_age_s = static_cast<uint32_t>(elapsed_ms / 1000);
    }
  }
  return true;
}

void TDisplayP4AirDevice::WifiInitTaskEntry(void* context) {
  auto* self = static_cast<TDisplayP4AirDevice*>(context);
  if (self != nullptr) {
    self->RunWifiInitTask();
  }
  vTaskDelete(nullptr);
}

esp_err_t TDisplayP4AirDevice::WifiCoprocessorResetCallback(
    void* context, bool level) {
  auto* self = static_cast<TDisplayP4AirDevice*>(context);
  if (self == nullptr) {
    return ESP_ERR_INVALID_ARG;
  }

  // 关闭请求发出后拒绝 ESP-Hosted 再次拉高 EN，确保协处理器保持断电状态。
  const bool enabled = level && !self->wifi_.stop_requested.load();
  return SetWifiCoprocessorPowerEnabled(self->driver_, enabled) ? ESP_OK
                                                                : ESP_FAIL;
}

void TDisplayP4AirDevice::RunWifiInitTask() {
  if (!WaitForWifiHardwareReady()) {
    const bool stopping = wifi_.stop_requested.load();
    wifi_.init_task_running.store(false);
    if (stopping) {
      SetWifiEnabled(false);
      return;
    }
    LogMessage(
        LogLevel::kWarning, __FILE__, __LINE__, "WiFi hardware is not ready\n");
    SetWifiEnabled(false);
    SetWifiFailure(ESP_ERR_TIMEOUT);
    return;
  }

  const int result = InitializeWifiStack();
  if (result != ESP_OK) {
    const bool stopping = wifi_.stop_requested.load();
    wifi_.init_task_running.store(false);
    SetWifiEnabled(false);
    if (stopping) {
      return;
    }
    LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
        "WiFi init failed: %s (%#X)\n",
        esp_err_to_name(static_cast<esp_err_t>(result)),
        static_cast<unsigned>(result));
    SetWifiFailure(result);
    return;
  }

  wifi_.init_task_running.store(false);
  if (wifi_.stop_requested.load()) {
    SetWifiEnabled(false);
    return;
  }
  if (wifi_.scan_requested.exchange(false) && !StartWifiScan()) {
    LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
        "Start deferred WiFi scan failed\n");
  }
  if (wifi_time_test_.requested.load()) {
    const int test_result = StartWifiTimeTestInternal();
    if (test_result != ESP_OK) {
      SetWifiFailure(test_result);
      LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
          "WiFi time test start failed: %s (%#X)\n",
          esp_err_to_name(static_cast<esp_err_t>(test_result)),
          static_cast<unsigned>(test_result));
    }
  }
}

void TDisplayP4AirDevice::WifiScanTaskEntry(void* context) {
  auto* self = static_cast<TDisplayP4AirDevice*>(context);
  if (self != nullptr) {
    self->RunWifiScanTask();
  }
  vTaskDelete(nullptr);
}

void TDisplayP4AirDevice::WifiConnectTaskEntry(void* context) {
  auto* self = static_cast<TDisplayP4AirDevice*>(context);
  if (self != nullptr) {
    self->RunWifiConnectTask();
  }
  vTaskDelete(nullptr);
}

void TDisplayP4AirDevice::RunWifiScanTask() {
  if (!wifi_.running.load()) {
    const int prepare_result = PrepareWifiStation();
    if (prepare_result != ESP_OK) {
      wifi_.scan_failed.store(true);
      wifi_.last_error.store(prepare_result);
      wifi_.scan_network_count.store(0);
      wifi_.scan_generation.fetch_add(1);
      wifi_.scan_running.store(false);
      wifi_.scan_task_running.store(false);
      return;
    }
  }

  if (wifi_.stop_requested.load()) {
    wifi_.scan_running.store(false);
    wifi_.scan_task_running.store(false);
    wifi_.scan_network_count.store(0);
    wifi_.scan_generation.fetch_add(1);
    return;
  }

  // hosted STA 在关联热点期间可能暂时拒绝扫描，等待连接状态稳定后重试。
  esp_err_t scan_result = ESP_ERR_WIFI_STATE;
  uint32_t retry_elapsed_ms = 0;
  while (!wifi_.stop_requested.load()) {
    scan_result = esp_wifi_scan_start(nullptr, false);
    if (scan_result == ESP_OK) {
      return;
    }
    if (scan_result != ESP_ERR_WIFI_STATE ||
        retry_elapsed_ms >= kWifiScanTimeoutMs) {
      break;
    }
    vTaskDelay(pdMS_TO_TICKS(kWifiScanStateRetryIntervalMs));
    retry_elapsed_ms += kWifiScanStateRetryIntervalMs;
  }

  if (wifi_.stop_requested.load()) {
    wifi_.scan_running.store(false);
    wifi_.scan_task_running.store(false);
    wifi_.scan_network_count.store(0);
    wifi_.scan_generation.fetch_add(1);
    return;
  }

  wifi_.scan_failed.store(true);
  wifi_.last_error.store(scan_result);
  wifi_.scan_network_count.store(0);
  wifi_.scan_generation.fetch_add(1);
  wifi_.scan_running.store(false);
  wifi_.scan_task_running.store(false);
}

void TDisplayP4AirDevice::RunWifiConnectTask() {
  char ssid[kWifiSsidMaxLength + 1] = {};
  char password[kWifiPasswordMaxLength + 1] = {};
  std::snprintf(ssid, sizeof(ssid), "%s", wifi_.connect_ssid);
  std::snprintf(password, sizeof(password), "%s", wifi_.connect_password);

  const auto finish = [this](esp_err_t error) {
    if (error != ESP_OK) {
      SetWifiFailure(error);
    }
    wifi_.connect_task_running.store(false);
  };

  if (ssid[0] == '\0') {
    finish(ESP_ERR_INVALID_ARG);
    return;
  }

  uint32_t wait_scan_ms = 0;
  while (wifi_.scan_running.load() || wifi_.scan_task_running.load()) {
    if (wifi_.connect_cancel_requested.load()) {
      wifi_.connect_task_running.store(false);
      return;
    }
    if (wait_scan_ms >= kWifiScanTimeoutMs) {
      wifi_.scan_running.store(false);
      wifi_.scan_task_running.store(false);
      esp_wifi_scan_stop();
      wifi_.scan_failed.store(true);
      wifi_.last_error.store(ESP_ERR_TIMEOUT);
      wifi_.scan_generation.fetch_add(1);
      finish(ESP_ERR_TIMEOUT);
      return;
    }
    vTaskDelay(pdMS_TO_TICKS(kWifiHardwareReadyPollMs));
    wait_scan_ms += kWifiHardwareReadyPollMs;
  }

  const int prepare_result = PrepareWifiStation();
  if (prepare_result != ESP_OK) {
    finish(static_cast<esp_err_t>(prepare_result));
    return;
  }

  if (wifi_.connect_cancel_requested.load()) {
    wifi_.connect_task_running.store(false);
    return;
  }

  if (wifi_.connected.load() || wifi_.got_ip.load()) {
    // 切换热点前先断开当前连接。
    esp_wifi_disconnect();
  }

  if (wifi_.connect_cancel_requested.load()) {
    wifi_.connect_task_running.store(false);
    return;
  }

  wifi_config_t wifi_config = {};
  const size_t ssid_length =
      std::min(std::strlen(ssid), sizeof(wifi_config.sta.ssid));
  std::memcpy(wifi_config.sta.ssid, ssid, ssid_length);
  if (password[0] != '\0') {
    const size_t password_length =
        std::min(std::strlen(password), sizeof(wifi_config.sta.password));
    std::memcpy(wifi_config.sta.password, password, password_length);
  }

  wifi_.start_failed.store(false);
  wifi_.last_error.store(ESP_OK);
  wifi_.disconnect_reason.store(0);
  wifi_.retry_count.store(0);
  wifi_.connected.store(false);
  wifi_.got_ip.store(false);
  wifi_.ip_address.store(0);
  wifi_.netmask.store(0);
  wifi_.gateway.store(0);

  const esp_err_t config_result =
      esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
  if (config_result != ESP_OK) {
    finish(config_result);
    return;
  }

  if (wifi_.connect_cancel_requested.load()) {
    wifi_.connect_task_running.store(false);
    return;
  }

  const esp_err_t connect_result = esp_wifi_connect();
  if (connect_result != ESP_OK) {
    finish(connect_result);
    return;
  }
  // 保持连接进行中状态，直到取得 DHCP 地址或收到断开事件。
}

bool TDisplayP4AirDevice::WaitForWifiHardwareReady() {
  uint32_t elapsed_ms = 0;
  while (!wifi_.stop_requested.load() && !driver_.IsXl9535Ready() &&
         elapsed_ms < kWifiHardwareReadyTimeoutMs) {
    vTaskDelay(pdMS_TO_TICKS(kWifiHardwareReadyPollMs));
    elapsed_ms += kWifiHardwareReadyPollMs;
  }

  if (wifi_.stop_requested.load() || !driver_.IsXl9535Ready()) {
    return false;
  }

  for (elapsed_ms = 0;
      elapsed_ms < kWifiCoprocessorBootDelayMs && !wifi_.stop_requested.load();
      elapsed_ms += kWifiHardwareReadyPollMs) {
    const uint32_t delay_ms = std::min(
        kWifiHardwareReadyPollMs, kWifiCoprocessorBootDelayMs - elapsed_ms);
    vTaskDelay(pdMS_TO_TICKS(delay_ms));
  }
  return !wifi_.stop_requested.load();
}

int TDisplayP4AirDevice::InitializeWifiStack() {
  if (wifi_.driver_initialized.load()) {
    return PrepareWifiStation();
  }

  if (wifi_.stop_requested.load()) {
    return ESP_ERR_INVALID_STATE;
  }

  if (!wifi_.hosted_bridge_initialized.load()) {
    const esp_hosted_transport_err_t reset_callback_result =
        esp_hosted_sdio_set_reset_callback(WifiCoprocessorResetCallback, this);
    if (reset_callback_result != ESP_TRANSPORT_OK) {
      return static_cast<int>(reset_callback_result);
    }
    if (wifi_.stop_requested.load()) {
      return ESP_ERR_INVALID_STATE;
    }
    const esp_err_t hosted_result = esp_hosted_init();
    if (hosted_result != ESP_OK && hosted_result != ESP_ERR_INVALID_STATE) {
      return hosted_result;
    }
    wifi_.hosted_bridge_initialized.store(true);
    if (wifi_.stop_requested.load()) {
      return ESP_ERR_INVALID_STATE;
    }
  }

  esp_err_t result = esp_netif_init();
  if (result != ESP_OK && result != ESP_ERR_INVALID_STATE) {
    return result;
  }

  result = esp_event_loop_create_default();
  if (result != ESP_OK && result != ESP_ERR_INVALID_STATE) {
    return result;
  }

  if (wifi_.netif == nullptr) {
    wifi_.netif = esp_netif_create_default_wifi_sta();
    if (wifi_.netif == nullptr) {
      return ESP_ERR_NO_MEM;
    }
  }

  wifi_init_config_t config = WIFI_INIT_CONFIG_DEFAULT();
  // 账号密码由 ESP32-P4 侧管理，C5 只接收 RAM 中的临时 WiFi 配置。
  config.nvs_enable = false;
  result = esp_wifi_init(&config);
  if (result != ESP_OK && result != ESP_ERR_WIFI_INIT_STATE) {
    return result;
  }

  result = esp_wifi_set_storage(WIFI_STORAGE_RAM);
  if (result != ESP_OK) {
    return result;
  }

  result = esp_event_handler_register(
      WIFI_EVENT, ESP_EVENT_ANY_ID, WifiEventHandler, this);
  if (result != ESP_OK) {
    return result;
  }

  result = esp_event_handler_register(
      IP_EVENT, IP_EVENT_STA_GOT_IP, WifiGotIpEventHandler, this);
  if (result != ESP_OK) {
    return result;
  }

  result = esp_wifi_set_mode(WIFI_MODE_STA);
  if (result != ESP_OK) {
    return result;
  }

  wifi_config_t empty_config = {};
  result = esp_wifi_set_config(WIFI_IF_STA, &empty_config);
  if (result != ESP_OK) {
    return result;
  }

  result = esp_wifi_start();
  if (result != ESP_OK) {
    return result;
  }
  wifi_.driver_initialized.store(true);
  wifi_.running.store(true);
  wifi_.connected.store(false);
  wifi_.got_ip.store(false);
  wifi_.start_failed.store(false);
  wifi_.last_error.store(ESP_OK);
  return ESP_OK;
}

int TDisplayP4AirDevice::PrepareWifiStation() {
  if (!wifi_.driver_initialized.load()) {
    return ESP_ERR_WIFI_NOT_INIT;
  }

  if (wifi_.running.load()) {
    wifi_.start_failed.store(false);
    wifi_.last_error.store(ESP_OK);
    return ESP_OK;
  }

  esp_err_t result = esp_wifi_set_storage(WIFI_STORAGE_RAM);
  if (result != ESP_OK) {
    return result;
  }

  result = esp_wifi_set_mode(WIFI_MODE_STA);
  if (result != ESP_OK) {
    return result;
  }

  result = esp_wifi_start();
  if (result != ESP_OK) {
    return result;
  }

  wifi_.running.store(true);
  wifi_.start_failed.store(false);
  wifi_.last_error.store(ESP_OK);
  return ESP_OK;
}

void TDisplayP4AirDevice::CopyWifiScanResultsFromDriver() {
  uint16_t available_count = 0;
  esp_err_t result = esp_wifi_scan_get_ap_num(&available_count);
  if (result != ESP_OK) {
    wifi_.scan_failed.store(true);
    wifi_.last_error.store(result);
    wifi_.scan_network_count.store(0);
    wifi_.scan_generation.fetch_add(1);
    return;
  }

  uint16_t record_count = static_cast<uint16_t>(
      std::min<size_t>(available_count, kMaxWifiScanNetworkCount));
  std::unique_ptr<wifi_ap_record_t[]> records(
      new (std::nothrow) wifi_ap_record_t[kMaxWifiScanNetworkCount]());
  std::unique_ptr<WifiNetworkInfo[]> networks(
      new (std::nothrow) WifiNetworkInfo[kMaxWifiScanNetworkCount]());
  if (records == nullptr || networks == nullptr) {
    wifi_.scan_failed.store(true);
    wifi_.last_error.store(ESP_ERR_NO_MEM);
    wifi_.scan_network_count.store(0);
    wifi_.scan_generation.fetch_add(1);
    return;
  }

  if (record_count > 0) {
    result = esp_wifi_scan_get_ap_records(&record_count, records.get());
    if (result != ESP_OK) {
      wifi_.scan_failed.store(true);
      wifi_.last_error.store(result);
      wifi_.scan_network_count.store(0);
      wifi_.scan_generation.fetch_add(1);
      return;
    }
  } else {
    wifi_.scan_network_count.store(0);
    wifi_.scan_generation.fetch_add(1);
    return;
  }

  size_t network_count = 0;
  for (uint16_t i = 0;
      i < record_count && network_count < kMaxWifiScanNetworkCount; ++i) {
    const auto* ssid = reinterpret_cast<const char*>(records[i].ssid);
    if (ssid == nullptr || ssid[0] == '\0') {
      continue;
    }

    bool duplicate = false;
    for (size_t existing = 0; existing < network_count; ++existing) {
      if (std::strncmp(networks[existing].ssid, ssid,
              sizeof(networks[existing].ssid)) == 0) {
        duplicate = true;
        break;
      }
    }
    if (duplicate) {
      continue;
    }

    WifiNetworkInfo info;
    std::snprintf(info.ssid, sizeof(info.ssid), "%s", ssid);
    info.rssi = records[i].rssi;
    info.channel = records[i].primary;
    info.secure = wifi_utils::IsSecureAuthMode(records[i].authmode);
    info.is_5g = wifi_utils::IsFiveGChannel(records[i].primary);
    networks[network_count++] = info;
  }

  if (wifi_.scan_results_mutex != nullptr) {
    xSemaphoreTake(wifi_.scan_results_mutex, portMAX_DELAY);
  }
  for (size_t i = 0; i < kMaxWifiScanNetworkCount; ++i) {
    wifi_.scan_networks[i] = networks[i];
  }
  wifi_.scan_network_count.store(network_count);
  wifi_.scan_failed.store(false);
  wifi_.last_error.store(ESP_OK);
  wifi_.scan_generation.fetch_add(1);
  if (wifi_.scan_results_mutex != nullptr) {
    xSemaphoreGive(wifi_.scan_results_mutex);
  }
}

int TDisplayP4AirDevice::StartWifiTimeTestInternal() {
  if (!wifi_.driver_initialized.load()) {
    return ESP_ERR_WIFI_NOT_INIT;
  }

  if (wifi_time_test_.active.load()) {
    return ESP_OK;
  }

  wifi_.connect_cancel_requested.store(true);
  wifi_.connect_task_running.store(false);

  wifi_time_test_.previous_running = wifi_.running.load();
  wifi_time_test_.previous_connected = wifi_.connected.load();
  wifi_time_test_.previous_mode_valid =
      esp_wifi_get_mode(&wifi_time_test_.previous_mode) == ESP_OK;
  wifi_time_test_.previous_sta_config_valid =
      esp_wifi_get_config(WIFI_IF_STA, &wifi_time_test_.previous_sta_config) ==
      ESP_OK;

  // 进入 CIT WiFi 时间测试前先停止设置页当前 WiFi，避免沿用旧热点。
  if (wifi_time_test_.previous_running) {
    esp_wifi_disconnect();
    const esp_err_t stop_result = esp_wifi_stop();
    if (stop_result != ESP_OK && stop_result != ESP_ERR_WIFI_NOT_STARTED) {
      return stop_result;
    }
  }
  StopWifiInternetCheck();

  wifi_.start_failed.store(false);
  wifi_.last_error.store(ESP_OK);
  wifi_.disconnect_reason.store(0);
  wifi_.retry_count.store(0);
  wifi_.running.store(false);
  wifi_time_test_.synced.store(false);
  wifi_time_test_.sync_started.store(false);
  wifi_time_test_.sntp_unix_time.store(0);
  wifi_time_test_.sntp_sync_monotonic_ms.store(0);
  wifi_.connected.store(false);
  wifi_.got_ip.store(false);
  wifi_.ip_address.store(0);
  wifi_.netmask.store(0);
  wifi_.gateway.store(0);
  // 后续任何失败都走 StopWifiTimeTest，确保原 WiFi 配置能恢复。
  wifi_time_test_.active.store(true);

  esp_err_t result = esp_wifi_set_storage(WIFI_STORAGE_RAM);
  if (result != ESP_OK) {
    StopWifiTimeTest();
    return result;
  }

  result = esp_wifi_set_mode(WIFI_MODE_STA);
  if (result != ESP_OK) {
    StopWifiTimeTest();
    return result;
  }

  wifi_config_t wifi_config = {};
  std::strncpy(reinterpret_cast<char*>(wifi_config.sta.ssid), kFactoryWifiSsid,
      sizeof(wifi_config.sta.ssid));
  std::strncpy(reinterpret_cast<char*>(wifi_config.sta.password),
      kFactoryWifiPassword, sizeof(wifi_config.sta.password));
  result = esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
  if (result != ESP_OK) {
    StopWifiTimeTest();
    return result;
  }

  result = esp_wifi_start();
  if (result != ESP_OK) {
    StopWifiTimeTest();
    return result;
  }
  wifi_.running.store(true);

  wifi_.connect_task_running.store(true);
  result = esp_wifi_connect();
  if (result != ESP_OK) {
    wifi_.connect_task_running.store(false);
    StopWifiTimeTest();
    return result;
  }
  return ESP_OK;
}

int TDisplayP4AirDevice::StartWifiSntp() {
  if (wifi_time_test_.sync_started.load()) {
    return ESP_OK;
  }

  StopWifiInternetCheck();
  wifi_time_test_.sntp_unix_time.store(0);
  wifi_time_test_.sntp_sync_monotonic_ms.store(0);
  wifi_time_test_.synced.store(false);
  WifiTimeSyncOwner().store(this);
  esp_sntp_set_time_sync_notification_cb([](struct timeval* time_value) {
    auto* owner = WifiTimeSyncOwner().load();
    if (owner == nullptr || time_value == nullptr) {
      return;
    }

    const int64_t unix_time = static_cast<int64_t>(time_value->tv_sec);
    if (unix_time <= device_utils::kValidUnixTimeThreshold) {
      return;
    }

    owner->wifi_time_test_.sntp_unix_time.store(unix_time);
    owner->wifi_time_test_.sntp_sync_monotonic_ms.store(
        esp_timer_get_time() / 1000);
    owner->wifi_time_test_.synced.store(true);
    owner->StopWifiInternetCheck();
  });
  // 客户端取时使用轮询模式；成功回调或第三次检测结束后停止客户端。
  esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);
  esp_sntp_set_sync_mode(SNTP_SYNC_MODE_IMMED);
  esp_sntp_setservername(0, kWifiSntpServer);
  const int timer_result = StartWifiSntpAttemptTimer();
  if (timer_result != ESP_OK) {
    StopWifiInternetCheck();
    return timer_result;
  }
  wifi_time_test_.sync_started.store(true);
  esp_sntp_init();
  return ESP_OK;
}

int TDisplayP4AirDevice::StartWifiSntpAttemptTimer() {
  if (wifi_time_test_.sntp_attempt_timer == nullptr) {
    esp_timer_create_args_t timer_config = {};
    timer_config.callback = WifiSntpAttemptTimerCallback;
    timer_config.arg = this;
    timer_config.dispatch_method = ESP_TIMER_TASK;
    timer_config.name = "sntp_attempt";
    const esp_err_t create_result =
        esp_timer_create(&timer_config, &wifi_time_test_.sntp_attempt_timer);
    if (create_result != ESP_OK) {
      return create_result;
    }
  }

  constexpr uint64_t kMicrosecondsPerMillisecond = 1000;
  const uint64_t interval_us =
      static_cast<uint64_t>(kWifiSntpAttemptIntervalMs) *
      kMicrosecondsPerMillisecond;
  wifi_time_test_.sntp_attempt_count.store(1);
  return esp_timer_is_active(wifi_time_test_.sntp_attempt_timer)
             ? esp_timer_restart(
                   wifi_time_test_.sntp_attempt_timer, interval_us)
             : esp_timer_start_periodic(
                   wifi_time_test_.sntp_attempt_timer, interval_us);
}

void TDisplayP4AirDevice::WifiSntpAttemptTimerCallback(void* argument) {
  auto* self = static_cast<TDisplayP4AirDevice*>(argument);
  if (self == nullptr) {
    return;
  }

  const int attempt_count = self->wifi_time_test_.sntp_attempt_count.load();
  if (self->wifi_time_test_.synced.load() || !self->wifi_.got_ip.load() ||
      attempt_count >= kWifiSntpMaxAttemptCount) {
    self->StopWifiInternetCheck();
    return;
  }

  self->wifi_time_test_.sntp_attempt_count.store(attempt_count + 1);
  if (!esp_sntp_enabled() || !esp_sntp_restart()) {
    self->StopWifiInternetCheck();
  }
}

void TDisplayP4AirDevice::SetWifiFailure(int error) {
  StopWifiInternetCheck();
  wifi_.init_task_running.store(false);
  wifi_.connect_task_running.store(false);
  wifi_.start_failed.store(true);
  wifi_.last_error.store(error);
  wifi_.connected.store(false);
  wifi_.got_ip.store(false);
  wifi_time_test_.synced.store(false);
  wifi_time_test_.sntp_unix_time.store(0);
  wifi_time_test_.sntp_sync_monotonic_ms.store(0);
  wifi_.ip_address.store(0);
  wifi_.netmask.store(0);
  wifi_.gateway.store(0);
}

void TDisplayP4AirDevice::WifiEventHandler(
    void* arg, const char* event_base, int32_t event_id, void* event_data) {
  (void)event_base;
  auto* self = static_cast<TDisplayP4AirDevice*>(arg);
  if (self == nullptr) {
    return;
  }

  switch (event_id) {
    case WIFI_EVENT_SCAN_DONE:
      if (self->wifi_.scan_running.load() ||
          self->wifi_.scan_task_running.load()) {
        if (self->wifi_.running.load()) {
          self->CopyWifiScanResultsFromDriver();
        } else {
          self->wifi_.scan_network_count.store(0);
          self->wifi_.scan_failed.store(false);
          self->wifi_.last_error.store(ESP_OK);
          self->wifi_.scan_generation.fetch_add(1);
        }
      }
      self->wifi_.scan_running.store(false);
      self->wifi_.scan_task_running.store(false);
      break;
    case WIFI_EVENT_STA_START:
      self->wifi_.running.store(true);
      self->wifi_.start_failed.store(false);
      self->wifi_.last_error.store(ESP_OK);
      break;
    case WIFI_EVENT_STA_CONNECTED: {
      self->wifi_.connected.store(true);
      self->wifi_.got_ip.store(false);
      self->wifi_.retry_count.store(0);
      wifi_ap_record_t ap_info = {};
      if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
        self->wifi_.rssi.store(ap_info.rssi);
        self->wifi_.channel.store(ap_info.primary);
      }
      uint8_t mac_address[6] = {};
      if (esp_wifi_get_mac(WIFI_IF_STA, mac_address) == ESP_OK) {
        self->wifi_.mac_address.store(wifi_utils::PackMacAddress(mac_address));
      }
      break;
    }
    case WIFI_EVENT_STA_DISCONNECTED: {
      self->wifi_.connect_task_running.store(false);
      self->wifi_.connected.store(false);
      self->wifi_.got_ip.store(false);
      self->wifi_time_test_.synced.store(false);
      self->wifi_time_test_.sntp_unix_time.store(0);
      self->wifi_time_test_.sntp_sync_monotonic_ms.store(0);
      self->wifi_.ip_address.store(0);
      self->wifi_.netmask.store(0);
      self->wifi_.gateway.store(0);
      self->StopWifiInternetCheck();
      if (event_data != nullptr) {
        const auto* disconnected =
            static_cast<wifi_event_sta_disconnected_t*>(event_data);
        self->wifi_.disconnect_reason.store(disconnected->reason);
      }

      if (self->wifi_time_test_.active.load()) {
        const int retry_count = self->wifi_.retry_count.fetch_add(1) + 1;
        if (retry_count <= kWifiMaxReconnectCount) {
          self->wifi_.connect_task_running.store(true);
          const esp_err_t connect_result = esp_wifi_connect();
          if (connect_result != ESP_OK) {
            self->SetWifiFailure(connect_result);
          }
        } else {
          self->wifi_.start_failed.store(true);
          self->wifi_.last_error.store(ESP_ERR_WIFI_CONN);
        }
      }
      break;
    }
    case WIFI_EVENT_STA_STOP:
      self->wifi_.connect_task_running.store(false);
      self->wifi_.running.store(false);
      self->wifi_.connected.store(false);
      self->wifi_.got_ip.store(false);
      self->wifi_.scan_running.store(false);
      self->wifi_time_test_.synced.store(false);
      self->wifi_time_test_.sntp_unix_time.store(0);
      self->wifi_time_test_.sntp_sync_monotonic_ms.store(0);
      self->wifi_.ip_address.store(0);
      self->wifi_.netmask.store(0);
      self->wifi_.gateway.store(0);
      self->StopWifiInternetCheck();
      break;
    default:
      break;
  }
}

void TDisplayP4AirDevice::WifiGotIpEventHandler(
    void* arg, const char* event_base, int32_t event_id, void* event_data) {
  (void)event_base;
  (void)event_id;
  auto* self = static_cast<TDisplayP4AirDevice*>(arg);
  auto* event = static_cast<ip_event_got_ip_t*>(event_data);
  if (self == nullptr || event == nullptr) {
    return;
  }

  self->wifi_.connected.store(true);
  self->wifi_.connect_task_running.store(false);
  self->wifi_.ip_address.store(event->ip_info.ip.addr);
  self->wifi_.netmask.store(event->ip_info.netmask.addr);
  self->wifi_.gateway.store(event->ip_info.gw.addr);
  self->wifi_.connection_generation.fetch_add(1);
  self->wifi_.got_ip.store(true);
  const int result = self->StartWifiSntp();
  if (result != ESP_OK) {
    self->SetWifiFailure(result);
  }
}

}  // namespace lilygo_box::hal
