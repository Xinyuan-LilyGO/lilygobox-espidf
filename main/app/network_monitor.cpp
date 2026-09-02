/*
 * @Description: 应用层网络可用性监控实现
 * @Author: LILYGO_L
 * @License: GPL 3.0
 */
#include "app/network_monitor.h"

#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "hal/providers/wifi_provider.h"

namespace lilygo_box::app {
namespace {

constexpr uint32_t kTaskStackBytes = 6 * 1024;
constexpr UBaseType_t kTaskPriority = 3;
constexpr uint32_t kPollIntervalMs = 1000;
constexpr uint32_t kWaitPollIntervalMs = 100;

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

bool NetworkMonitor::EnsureInternetAccess(uint32_t timeout_ms) {
  if (!initialized_.load() || wifi_ == nullptr) {
    return false;
  }

  hal::WifiStatus wifi_status;
  if (!wifi_->ReadWifiStatus(&wifi_status) || !wifi_status.got_ip ||
      wifi_status.time_test_running) {
    return false;
  }

  InternetAccessState state = internet_state_.load();
  if (state == InternetAccessState::kAvailable) {
    return true;
  }

  const int64_t now_ms = esp_timer_get_time() / 1000;
  const uint32_t initial_generation = check_generation_.load();
  if (state != InternetAccessState::kChecking) {
    RequestInternetAccessRecheck();
  }

  const int64_t deadline_ms = now_ms + timeout_ms;
  while (esp_timer_get_time() / 1000 < deadline_ms) {
    if (!wifi_->ReadWifiStatus(&wifi_status) || !wifi_status.got_ip ||
        wifi_status.time_test_running) {
      return false;
    }

    state = internet_state_.load();
    if (state == InternetAccessState::kAvailable) {
      return true;
    }
    if (state == InternetAccessState::kLocalOnly &&
        check_generation_.load() != initial_generation) {
      return false;
    }
    vTaskDelay(pdMS_TO_TICKS(kWaitPollIntervalMs));
  }
  return internet_state_.load() == InternetAccessState::kAvailable;
}

void NetworkMonitor::RequestInternetAccessRecheck() {
  if (!initialized_.load() || wifi_ == nullptr ||
      internet_state_.load() == InternetAccessState::kChecking) {
    return;
  }

  bool expected = false;
  if (recheck_requested_.compare_exchange_strong(expected, true)) {
    InternetAccessState available = InternetAccessState::kAvailable;
    if (internet_state_.compare_exchange_strong(
            available, InternetAccessState::kLocalOnly)) {
      check_monotonic_ms_.store(esp_timer_get_time() / 1000);
    }
  }
}

void NetworkMonitor::TaskEntry(void* argument) {
  auto* monitor = static_cast<NetworkMonitor*>(argument);
  if (monitor != nullptr) {
    monitor->RunTask();
  }
  vTaskDelete(nullptr);
}

void NetworkMonitor::RunTask() {
  bool had_ip = false;
  uint32_t connection_generation = 0;
  bool verification_active = false;
  int64_t verification_started_ms = 0;
  while (true) {
    hal::WifiStatus wifi_status;
    if (wifi_ == nullptr || !wifi_->ReadWifiStatus(&wifi_status) ||
        !wifi_status.got_ip || wifi_status.time_test_running) {
      internet_state_.store(InternetAccessState::kUnknown);
      check_monotonic_ms_.store(0);
      recheck_requested_.store(false);
      had_ip = false;
      connection_generation = 0;
      verification_active = false;
      verification_started_ms = 0;
      vTaskDelay(pdMS_TO_TICKS(kPollIntervalMs));
      continue;
    }

    const int64_t now_ms = esp_timer_get_time() / 1000;
    if (!had_ip || connection_generation != wifi_status.connection_generation) {
      had_ip = true;
      connection_generation = wifi_status.connection_generation;
      recheck_requested_.store(false);
      verification_active = true;
      verification_started_ms = now_ms;
      internet_state_.store(InternetAccessState::kChecking);
      check_monotonic_ms_.store(0);
    }

    if (recheck_requested_.exchange(false)) {
      // 首次连接验证已经在进行时合并业务请求，不能重新计算超时时间。
      if (!verification_active) {
        const InternetAccessState previous_state = internet_state_.load();
        verification_active = wifi_->RequestWifiInternetCheck();
        wifi_status.time_synced = false;
        verification_started_ms = now_ms;
        if (verification_active &&
            previous_state != InternetAccessState::kLocalOnly &&
            previous_state != InternetAccessState::kAvailable) {
          internet_state_.store(InternetAccessState::kChecking);
          check_monotonic_ms_.store(0);
        } else {
          internet_state_.store(InternetAccessState::kLocalOnly);
          if (previous_state != InternetAccessState::kLocalOnly) {
            check_monotonic_ms_.store(now_ms);
          }
        }
        if (!verification_active) {
          check_generation_.fetch_add(1);
        }
      }
    }

    if (verification_active) {
      if (wifi_status.time_synced) {
        internet_state_.store(InternetAccessState::kAvailable);
        check_monotonic_ms_.store(now_ms);
        check_generation_.fetch_add(1);
        verification_active = false;
        verification_started_ms = 0;
      } else if (now_ms - verification_started_ms >=
                 static_cast<int64_t>(hal::kWifiInternetCheckTimeoutMs)) {
        internet_state_.store(InternetAccessState::kLocalOnly);
        check_monotonic_ms_.store(now_ms);
        check_generation_.fetch_add(1);
        verification_active = false;
        verification_started_ms = 0;
      }
    }
    vTaskDelay(pdMS_TO_TICKS(kPollIntervalMs));
  }
}

}  // namespace lilygo_box::app
