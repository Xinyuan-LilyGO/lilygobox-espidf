/*
 * @Description: System status runtime cache
 * @Author: LILYGO_L
 * @Date: 2026-06-24 00:00:00
 * @LastEditTime: 2026-07-19 00:23:38
 * @License: GPL 3.0
 */
#include "app/system_status_cache.h"

#include <ctime>
#include <sys/time.h>

namespace lilygo_box::app {
namespace {

constexpr uint32_t kBatteryRefreshIntervalTicks = 2;
constexpr uint32_t kWifiRefreshIntervalTicks = 3;
constexpr std::time_t kValidNetworkUnixTime = 1700000000;

/**
 * @brief 检查 RTC 日期时间是否可以安全写入系统时钟
 * @param status RTC 启动时间
 * @return 日期时间字段有效返回 true，否则返回 false
 */
bool IsValidRtcTime(const hal::RtcStatus& status) {
  return status.ready && status.year >= 2000 && status.year <= 2099 &&
         status.month >= 1 && status.month <= 12 && status.day >= 1 &&
         status.day <= 31 && status.hour <= 23 && status.minute <= 59 &&
         status.second <= 59;
}

/**
 * @brief 使用启动时读取的 PCF8563 时间初始化系统时钟
 * @param status RTC 启动时间
 * @return 系统时钟初始化成功返回 true，否则返回 false
 */
bool InitializeSystemClock(const hal::RtcStatus& status) {
  if (!IsValidRtcTime(status)) {
    return false;
  }

  std::tm calendar_time = {};
  calendar_time.tm_year = static_cast<int>(status.year) - 1900;
  calendar_time.tm_mon = static_cast<int>(status.month) - 1;
  calendar_time.tm_mday = status.day;
  calendar_time.tm_hour = status.hour;
  calendar_time.tm_min = status.minute;
  calendar_time.tm_sec = status.second;
  calendar_time.tm_isdst = -1;
  const std::time_t unix_time = std::mktime(&calendar_time);
  if (unix_time == static_cast<std::time_t>(-1)) {
    return false;
  }

  timeval system_time = {};
  system_time.tv_sec = unix_time;
  return settimeofday(&system_time, nullptr) == 0;
}

/**
 * @brief 从系统时钟生成界面使用的时间状态
 * @param clock_integrity 启动时读取的 PCF8563 时钟完整性状态
 * @param status 系统时间状态输出地址
 * @return 系统时间读取成功返回 true，否则返回 false
 */
bool ReadSystemClock(bool clock_integrity, hal::RtcStatus* status) {
  if (status == nullptr) {
    return false;
  }

  const std::time_t unix_time = std::time(nullptr);
  std::tm calendar_time = {};
  if (unix_time == static_cast<std::time_t>(-1) ||
      localtime_r(&unix_time, &calendar_time) == nullptr) {
    return false;
  }

  *status = hal::RtcStatus();
  status->ready = true;
  status->clock_integrity = clock_integrity;
  status->year = static_cast<uint16_t>(calendar_time.tm_year + 1900);
  status->month = static_cast<uint8_t>(calendar_time.tm_mon + 1);
  status->day = static_cast<uint8_t>(calendar_time.tm_mday);
  status->week = static_cast<uint8_t>(calendar_time.tm_wday);
  status->hour = static_cast<uint8_t>(calendar_time.tm_hour);
  status->minute = static_cast<uint8_t>(calendar_time.tm_min);
  status->second = static_cast<uint8_t>(calendar_time.tm_sec);
  return true;
}

}  // namespace

void SystemStatusCache::Init(
    hal::RtcProvider* rtc, hal::BmuProvider* bmu, hal::WifiProvider* wifi) {
  bmu_ = bmu;
  wifi_ = wifi;
  rtc_status_ = hal::RtcStatus();
  bmu_status_ = hal::BmuStatus();
  wifi_status_ = hal::WifiStatus();
  refresh_count_ = 0;
  rtc_status_valid_ = false;
  bmu_status_valid_ = false;
  wifi_status_valid_ = false;
  system_clock_initialized_ = false;
  rtc_clock_integrity_ = false;

  hal::RtcStatus startup_rtc_status;
  if (rtc != nullptr && rtc->ReadRtcStatus(&startup_rtc_status) &&
      InitializeSystemClock(startup_rtc_status)) {
    system_clock_initialized_ = true;
    rtc_clock_integrity_ = startup_rtc_status.clock_integrity;
    RefreshClock();
  }
}

bool SystemStatusCache::RefreshClock() {
  if (!system_clock_initialized_) {
    const std::time_t unix_time = std::time(nullptr);
    if (unix_time < kValidNetworkUnixTime) {
      rtc_status_valid_ = false;
      return false;
    }
    // RTC 启动时间无效时，允许 SNTP 后续恢复本地系统时钟显示。
    system_clock_initialized_ = true;
    rtc_clock_integrity_ = true;
  }

  hal::RtcStatus status;
  if (!ReadSystemClock(rtc_clock_integrity_, &status)) {
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
