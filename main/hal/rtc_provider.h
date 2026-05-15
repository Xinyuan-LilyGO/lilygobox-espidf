/*
 * @Description: None
 * @Author: LILYGO_L
 * @Date: 2026-05-15 10:40:00
 * @LastEditTime: 2026-05-15 10:40:00
 * @License: GPL 3.0
 */
#pragma once

#include <cstdint>

namespace lilygo_box::hal {

struct RtcStatus {
  // RTC 芯片是否已经初始化完成
  bool ready = false;
  // PCF8563 时钟完整性标志是否正常
  bool clock_integrity = false;
  // 完整年份，例如 2026
  uint16_t year = 0;
  // 月份，范围 1~12
  uint8_t month = 0;
  // 日期，范围 1~31
  uint8_t day = 0;
  // 星期，范围 0~6
  uint8_t week = 0;
  // 小时，范围 0~23
  uint8_t hour = 0;
  // 分钟，范围 0~59
  uint8_t minute = 0;
  // 秒，范围 0~59
  uint8_t second = 0;
};

class RtcProvider {
 public:
  virtual ~RtcProvider() = default;

  /**
   * @brief 读取 RTC 日期时间和时钟完整性状态
   * @param status RTC 状态输出地址
   * @return 读取到有效 RTC 数据返回 true，否则返回 false
   * @Date 2026-05-15 10:40:00
   */
  virtual bool ReadRtcStatus(RtcStatus* status) = 0;
};

}  // namespace lilygo_box::hal
