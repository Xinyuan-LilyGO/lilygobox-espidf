/*
 * @Description: 应用层网络可用性监控实现
 * @Author: LILYGO_L
 * @License: GPL 3.0
 */
#include "app/network_monitor.h"

#include "app/firmware_update_manager.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "hal/providers/wifi_provider.h"

namespace lilygo_box::app {
namespace {

constexpr uint32_t kTaskStackBytes = 6 * 1024;
constexpr UBaseType_t kTaskPriority = 3;
constexpr uint32_t kCheckTimeoutMs = 10 * 1000;
constexpr uint32_t kRetryIntervalMs = 30 * 1000;
constexpr uint32_t kRecheckIntervalMs = 10 * 60 * 1000;
constexpr uint32_t kPollIntervalMs = 1000;

}  // namespace

NetworkMonitor& NetworkMonitor::Instance() {
  static NetworkMonitor monitor;
  return monitor;
}

bool NetworkMonitor::Initialize(hal::WifiProvider* wifi) {
  if (initialized_.load()) {
    return true;
  }
  if (wifi == nullptr) {
    return false;
  }

  wifi_ = wifi;
  bool expected = false;
  if (!initialized_.compare_exchange_strong(expected, true)) {
    return true;
  }
  const BaseType_t result = xTaskCreate(TaskEntry, "network_monitor",
      kTaskStackBytes, this, kTaskPriority, nullptr);
  if (result != pdPASS) {
    initialized_.store(false);
    wifi_ = nullptr;
    return false;
  }
  return true;
}

NetworkMonitorStatus NetworkMonitor::GetStatus() const {
  NetworkMonitorStatus status;
  status.internet_state = internet_state_.load();
  const int64_t checked_ms = check_monotonic_ms_.load();
  if (checked_ms > 0) {
    const int64_t elapsed_ms = esp_timer_get_time() / 1000 - checked_ms;
    if (elapsed_ms > 0) {
      status.check_age_s = static_cast<uint32_t>(elapsed_ms / 1000);
    }
  }
  return status;
}

void NetworkMonitor::TaskEntry(void* argument) {
  auto* monitor = static_cast<NetworkMonitor*>(argument);
  if (monitor != nullptr) {
    monitor->RunTask();
  }
  vTaskDelete(nullptr);
}

void NetworkMonitor::RunTask() {
  int64_t next_check_ms = 0;
  while (true) {
    hal::WifiStatus wifi_status;
    if (wifi_ == nullptr || !wifi_->ReadWifiStatus(&wifi_status) ||
        !wifi_status.got_ip || wifi_status.time_test_running) {
      internet_state_.store(InternetAccessState::kUnknown);
      next_check_ms = 0;
      vTaskDelay(pdMS_TO_TICKS(kPollIntervalMs));
      continue;
    }

    const int64_t now_ms = esp_timer_get_time() / 1000;
    if (next_check_ms != 0 && now_ms < next_check_ms) {
      vTaskDelay(pdMS_TO_TICKS(kPollIntervalMs));
      continue;
    }

    internet_state_.store(InternetAccessState::kChecking);
    const bool available = CheckInternetAccess();
    if (!wifi_->ReadWifiStatus(&wifi_status) || !wifi_status.got_ip ||
        wifi_status.time_test_running) {
      internet_state_.store(InternetAccessState::kUnknown);
      next_check_ms = 0;
      continue;
    }

    internet_state_.store(available ? InternetAccessState::kAvailable
                                    : InternetAccessState::kLocalOnly);
    check_monotonic_ms_.store(esp_timer_get_time() / 1000);
    next_check_ms = now_ms +
        (available ? kRecheckIntervalMs : kRetryIntervalMs);
  }
}

bool NetworkMonitor::CheckInternetAccess() const {
  const char* url = FirmwareUpdateManager::ManifestUrl();
  if (url == nullptr || url[0] == '\0') {
    return false;
  }

  esp_http_client_config_t config = {};
  config.url = url;
  config.crt_bundle_attach = esp_crt_bundle_attach;
  config.timeout_ms = kCheckTimeoutMs;
  config.disable_auto_redirect = false;
  config.max_redirection_count = 5;
  config.buffer_size = 512;
  config.buffer_size_tx = 512;

  esp_http_client_handle_t client = esp_http_client_init(&config);
  if (client == nullptr) {
    return false;
  }

  esp_http_client_set_method(client, HTTP_METHOD_HEAD);
  const esp_err_t result = esp_http_client_perform(client);
  const int status_code = esp_http_client_get_status_code(client);
  esp_http_client_cleanup(client);
  return result == ESP_OK && status_code >= 200 && status_code < 400;
}

}  // namespace lilygo_box::app
