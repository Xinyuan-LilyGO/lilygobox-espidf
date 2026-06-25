/*
 * @Description: System status runtime cache
 * @Author: LILYGO_L
 * @Date: 2026-06-24 00:00:00
 * @LastEditTime: 2026-06-25 13:45:58
 * @License: GPL 3.0
 */
#include "app/system_status_cache.h"

namespace lilygo_box::app {
namespace {

constexpr uint32_t kBatteryRefreshIntervalTicks = 2;
constexpr uint32_t kWifiRefreshIntervalTicks = 3;

}  // namespace

void SystemStatusCache::Init(
    hal::RtcProvider* rtc, hal::BmuProvider* bmu, hal::WifiProvider* wifi) {
  rtc_ = rtc;
  bmu_ = bmu;
  wifi_ = wifi;
  rtc_status_ = hal::RtcStatus();
  bmu_status_ = hal::BmuStatus();
  wifi_status_ = hal::WifiStatus();
  refresh_count_ = 0;
  rtc_status_valid_ = false;
  bmu_status_valid_ = false;
  wifi_status_valid_ = false;
}

bool SystemStatusCache::RefreshClock() {
  if (rtc_ == nullptr) {
    rtc_status_valid_ = false;
    return false;
  }

  hal::RtcStatus status;
  if (!rtc_->ReadRtcStatus(&status) || !status.ready) {
    return false;
  }

  rtc_status_ = status;
  rtc_status_valid_ = true;
  return true;
}

bool SystemStatusCache::RefreshBattery() {
  if (bmu_ == nullptr) {
    bmu_status_valid_ = false;
    return false;
  }

  hal::BmuStatus status;
  if (!bmu_->ReadBmuStatus(&status) || !status.ready) {
    return false;
  }

  bmu_status_ = status;
  bmu_status_valid_ = true;
  return true;
}

bool SystemStatusCache::RefreshWifi() {
  if (wifi_ == nullptr) {
    wifi_status_valid_ = false;
    return false;
  }

  hal::WifiStatus status;
  if (!wifi_->ReadWifiStatus(&status)) {
    wifi_status_ = hal::WifiStatus();
    wifi_status_valid_ = true;
    return false;
  }

  wifi_status_ = status;
  wifi_status_valid_ = true;
  return true;
}

void SystemStatusCache::RefreshSystemStatus() {
  RefreshClock();

  if (refresh_count_ % kBatteryRefreshIntervalTicks == 0) {
    RefreshBattery();
  }
  if (refresh_count_ % kWifiRefreshIntervalTicks == 0) {
    RefreshWifi();
  }
  ++refresh_count_;
}

}  // namespace lilygo_box::app
