/*
 * @Description: None
 * @Author: LILYGO_L
 * @Date: 2026-05-14 00:20:00
 * @LastEditTime: 2026-05-14 00:20:00
 * @License: GPL 3.0
 */
#pragma once

#include <cstddef>
#include <cstdint>

namespace lilygo_box::hal {

struct GpsTime {
  bool ready = false;
  uint8_t hour = 0;
  uint8_t minute = 0;
  float second = 0.0F;
};

struct GpsDate {
  bool ready = false;
  uint8_t day = 0;
  uint8_t month = 0;
  uint8_t year = 0;
};

struct GpsCoordinate {
  bool ready = false;
  uint8_t degrees = 0;
  float minutes = 0.0F;
  double degrees_minutes = 0.0;
  char direction[3] = {};
};

struct GpsStatus {
  bool running = false;
  bool data_ready = false;
  bool parse_success = false;
  bool positioned = false;
  size_t bytes_read = 0;
  char location_status[8] = {};
  GpsTime utc;
  GpsDate date;
  GpsCoordinate latitude;
  GpsCoordinate longitude;
};

class GpsProvider {
 public:
  virtual ~GpsProvider() = default;

  /**
   * @brief 启动 GPS 并唤醒定位模块
   * @return 启动成功返回 true，否则返回 false
   * @Date 2026-05-14 00:20:00
   */
  virtual bool StartGps() = 0;

  /**
   * @brief 停止 GPS 并让定位模块进入睡眠
   * @return 停止成功返回 true，否则返回 false
   * @Date 2026-05-14 00:20:00
   */
  virtual bool StopGps() = 0;

  /**
   * @brief 读取 GPS 状态和最新 RMC 解析数据
   * @param status GPS 状态输出地址
   * @return 读取成功返回 true，否则返回 false
   * @Date 2026-05-14 00:20:00
   */
  virtual bool ReadGpsStatus(GpsStatus* status) = 0;
};

}  // namespace lilygo_box::hal
