/*
 * @Description: System status runtime cache
 * @Author: LILYGO_L
 * @Date: 2026-06-24 00:00:00
 * @LastEditTime: 2026-06-24 00:00:00
 * @License: GPL 3.0
 */
#pragma once

#include <cstdint>

#include "hal/providers/bmu_provider.h"
#include "hal/providers/rtc_provider.h"

namespace lilygo_box::app {

class SystemStatusCache final {
 public:
  SystemStatusCache() = default;

  /**
   * @brief 初始化系统状态缓存使用的硬件状态提供者
   * @param rtc RTC 状态提供者
   * @param bmu BMU 电池状态提供者
   */
  void Init(hal::RtcProvider* rtc, hal::BmuProvider* bmu);

  /**
   * @brief 刷新 RTC 时间缓存
   * @return 刷新成功返回 true，否则返回 false
   */
  bool RefreshClock();

  /**
   * @brief 刷新 BMU 电池状态缓存
   * @return 刷新成功返回 true，否则返回 false
   */
  bool RefreshBattery();

  /**
   * @brief 刷新系统状态缓存
   */
  void RefreshSystemStatus();

  /**
   * @brief 获取 RTC 时间缓存
   * @return RTC 时间状态
   */
  const hal::RtcStatus& rtc_status() const { return rtc_status_; }

  /**
   * @brief 获取 BMU 电池状态缓存
   * @return BMU 电池状态
   */
  const hal::BmuStatus& bmu_status() const { return bmu_status_; }

  /**
   * @brief 判断 RTC 时间缓存是否有效
   * @return RTC 缓存有效返回 true，否则返回 false
   */
  bool rtc_status_valid() const { return rtc_status_valid_; }

  /**
   * @brief 判断 BMU 电池状态缓存是否有效
   * @return BMU 缓存有效返回 true，否则返回 false
   */
  bool bmu_status_valid() const { return bmu_status_valid_; }

 private:
  hal::RtcProvider* rtc_ = nullptr;
  hal::BmuProvider* bmu_ = nullptr;
  hal::RtcStatus rtc_status_ = {};
  hal::BmuStatus bmu_status_ = {};
  bool rtc_status_valid_ = false;
  bool bmu_status_valid_ = false;
};

}  // namespace lilygo_box::app
