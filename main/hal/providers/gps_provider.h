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
  uint16_t year = 0;
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
  uint32_t update_interval_ms = 1000;
  char location_status[8] = {};
  char mode_indicator[8] = {};
  char navigational_status[8] = {};
  GpsTime utc;
  GpsDate date;
  GpsCoordinate latitude;
  GpsCoordinate longitude;
  bool speed_ready = false;
  float speed_knots = 0.0F;
  float speed_kmh = 0.0F;
  bool course_ready = false;
  float course_degree = 0.0F;
  bool fix_quality_ready = false;
  uint8_t fix_quality = 0;
  bool fix_mode_ready = false;
  uint8_t fix_mode = 0;
  bool satellites_used_ready = false;
  uint8_t satellites_used = 0;
  bool satellites_in_view_ready = false;
  uint8_t satellites_in_view = 0;
  size_t satellite_info_count = 0;
  bool strongest_satellite_ready = false;
  uint16_t strongest_satellite_id = 0;
  int16_t strongest_satellite_cn0 = 0;
  bool hdop_ready = false;
  float hdop = 0.0F;
  bool pdop_ready = false;
  float pdop = 0.0F;
  bool vdop_ready = false;
  float vdop = 0.0F;
  bool altitude_ready = false;
  float altitude = 0.0F;
  char altitude_unit[4] = {};
};

class GpsProvider {
 public:
  virtual ~GpsProvider() = default;

  /**
   * @brief 启动 GPS 并唤醒定位模块
   * @return 启动成功返回 true，否则返回 false
   */
  virtual bool StartGps() = 0;

  /**
   * @brief 停止 GPS 并让定位模块进入睡眠
   * @return 停止成功返回 true，否则返回 false
   */
  virtual bool StopGps() = 0;

  /**
   * @brief 读取 GPS 状态和最新 GNSS 解析数据
   * @param status GPS 状态输出地址
   * @return 读取成功返回 true，否则返回 false
   */
  virtual bool ReadGpsStatus(GpsStatus* status) = 0;
};

}  // namespace lilygo_box::hal
