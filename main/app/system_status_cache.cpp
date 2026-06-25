/*
 * @Description: System status runtime cache
 * @Author: LILYGO_L
 * @Date: 2026-06-24 00:00:00
 * @LastEditTime: 2026-06-24 00:00:00
 * @License: GPL 3.0
 */
#include "app/system_status_cache.h"

namespace lilygo_box::app {

void SystemStatusCache::Init(hal::RtcProvider* rtc, hal::BmuProvider* bmu) {
  rtc_ = rtc;
  bmu_ = bmu;
  rtc_status_ = hal::RtcStatus();
  bmu_status_ = hal::BmuStatus();
  rtc_status_valid_ = false;
  bmu_status_valid_ = false;
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

void SystemStatusCache::RefreshSystemStatus() {
  RefreshClock();
  RefreshBattery();
}

}  // namespace lilygo_box::app
