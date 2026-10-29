/*
 * @Description: ESP32 系统 RTC 时钟读写公共实现声明
 * @Author: LILYGO_L
 * @Date: 2026-10-28 00:00:00
 * @LastEditTime: 2026-10-28 00:00:00
 * @License: GPL 3.0
 */
#pragma once

#include <cstdint>

#include "hal/providers/rtc_provider.h"

namespace lilygo_box::hal::rtc_utils {

/**
 * @brief 读取 ESP32 系统时钟并转换为 RTC 状态
 * @param status RTC 状态输出地址
 * @return 系统时间有效时返回 true，否则返回 false
 */
bool ReadSystemClockStatus(RtcStatus* status);

/**
 * @brief 将 UTC Unix 时间戳写入 ESP32 系统时钟
 * @param unix_time UTC Unix 时间戳
 * @return 写入成功返回 true，否则返回 false
 */
bool WriteSystemClockUnixTime(int64_t unix_time);

}  // namespace lilygo_box::hal::rtc_utils