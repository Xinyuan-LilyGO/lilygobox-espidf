/*
 * @Description: System status runtime cache
 * @Author: LILYGO_L
 * @Date: 2026-06-24 00:00:00
 * @LastEditTime: 2026-09-02 17:51:13
 * @License: GPL 3.0
 */
#include "app/system_status_cache.h"

#include <sys/time.h>

#include <cstdio>
#include <cstring>
#include <ctime>

#include "base/logger.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace lilygo_box::app {
namespace {

constexpr std::time_t kValidNetworkUnixTime = 1700000000;
constexpr int kRtcReadAttempts = 3;
constexpr uint32_t kRtcReadRetryIntervalMs = 10;

/**
 * @brief 将日历时间写入 ESP32-P4 系统时钟
 * @param calendar_time 本地日历时间
 * @return 系统时钟设置成功返回 true，否则返回 false
 */
bool SetSystemClock(std::tm calendar_time) {
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
 * @brief 使用当前固件的编译时间初始化 ESP32-P4 系统时钟
 * @return 系统时钟初始化成功返回 true，否则返回 false
 */
bool InitializeSystemClockFromBuildTime() {
  char month_name[4] = {};
  int day = 0;
  int year = 0;
  int hour = 0;
  int minute = 0;
  int second = 0;
  if (std::sscanf(__DATE__, "%3s %d %d", month_name, &day, &year) != 3 ||
      std::sscanf(__TIME__, "%d:%d:%d", &hour, &minute, &second) != 3) {
    return false;
  }

  constexpr const char* kMonthNames[] = {
      "Jan",
      "Feb",
      "Mar",
      "Apr",
      "May",
      "Jun",
      "Jul",
      "Aug",
      "Sep",
      "Oct",
      "Nov",
      "Dec",
  };
  int month = -1;
  for (int index = 0;
      index < static_cast<int>(sizeof(kMonthNames) / sizeof(kMonthNames[0]));
      ++index) {
    if (std::strcmp(month_name, kMonthNames[index]) == 0) {
      month = index;
      break;
    }
  }
  if (month < 0) {
    return false;
  }

  std::tm calendar_time = {};
  calendar_time.tm_year = year - 1900;
  calendar_time.tm_mon = month;
  calendar_time.tm_mday = day;
  calendar_time.tm_hour = hour;
  calendar_time.tm_min = minute;
  calendar_time.tm_sec = second;
  return SetSystemClock(calendar_time);
}

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
  return SetSystemClock(calendar_time);
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

void SystemStatusCache::Init(hal::RtcProvider* rtc,
    hal::BatteryManagementProvider* battery_management,
    hal::WifiProvider* wifi) {
  battery_management_ = battery_management;
  wifi_ = wifi;
  rtc_status_ = hal::RtcStatus();
  battery_management_status_ = hal::BatteryManagementStatus();
  wifi_status_ = hal::WifiStatus();
  rtc_status_valid_ = false;
  battery_management_status_valid_ = false;
  wifi_status_valid_ = false;
  system_clock_initialized_ = false;
  rtc_clock_integrity_ = false;

  hal::RtcStatus startup_rtc_status;
  bool rtc_read = false;
  if (rtc != nullptr) {
    for (int attempt = 0; attempt < kRtcReadAttempts; ++attempt) {
      if (rtc->ReadRtcStatus(&startup_rtc_status)) {
        rtc_read = true;
        break;
      }
      if (attempt + 1 < kRtcReadAttempts) {
        vTaskDelay(pdMS_TO_TICKS(kRtcReadRetryIntervalMs));
      }
    }
  }

  const bool rtc_reliable = rtc_read && startup_rtc_status.clock_integrity &&
                            IsValidRtcTime(startup_rtc_status);
  if (rtc_reliable && InitializeSystemClock(startup_rtc_status)) {
    system_clock_initialized_ = true;
    rtc_clock_integrity_ = true;
  } else {
    if (rtc == nullptr) {
      LogMessage(LogLevel::kError, __FILE__, __LINE__,
          "RTC provider is unavailable; using firmware build time %s %s\n",
          __DATE__, __TIME__);
    } else if (!rtc_read) {
      LogMessage(LogLevel::kError, __FILE__, __LINE__,
          "RTC read failed after %d attempts; using firmware build time "
          "%s %s\n",
          kRtcReadAttempts, __DATE__, __TIME__);
    } else if (!rtc_reliable) {
      LogMessage(LogLevel::kError, __FILE__, __LINE__,
          "RTC time is unreliable: %04u-%02u-%02u %02u:%02u:%02u, "
          "integrity=%s; using firmware build time %s %s\n",
          static_cast<unsigned>(startup_rtc_status.year),
          static_cast<unsigned>(startup_rtc_status.month),
          static_cast<unsigned>(startup_rtc_status.day),
          static_cast<unsigned>(startup_rtc_status.hour),
          static_cast<unsigned>(startup_rtc_status.minute),
          static_cast<unsigned>(startup_rtc_status.second),
          startup_rtc_status.clock_integrity ? "true" : "false", __DATE__,
          __TIME__);
    } else {
      LogMessage(LogLevel::kError, __FILE__, __LINE__,
          "Initialize system clock from RTC failed; using firmware build time "
          "%s %s\n",
          __DATE__, __TIME__);
    }

    system_clock_initialized_ = InitializeSystemClockFromBuildTime();
    if (!system_clock_initialized_) {
      LogMessage(LogLevel::kError, __FILE__, __LINE__,
          "Initialize system clock from firmware build time failed\n");
    }
  }
  RefreshClock();
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
  if (battery_management_ == nullptr) {
    battery_management_status_valid_ = false;
    return false;
  }

  hal::BatteryManagementStatus status;
  if (!battery_management_->ReadBatteryManagementStatus(&status) ||
      !status.ready) {
    battery_management_status_valid_ = false;
    return false;
  }

  battery_management_status_ = status;
  battery_management_status_valid_ = true;
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
  RefreshBattery();
  RefreshWifi();
}

}  // namespace lilygo_box::app
