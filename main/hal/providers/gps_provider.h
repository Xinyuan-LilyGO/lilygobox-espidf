/*
 * @Description: GPS 时间、定位状态与模块控制接口
 * @Author: LILYGO_L
 * @Date: 2026-05-14 00:20:00
 * @LastEditTime: 2026-05-14 00:20:00
 * @License: GPL 3.0
 */
#pragma once

#include <cstddef>
#include <cstdint>

namespace lilygo_box::hal {

// GNSS 语句中解析出的 UTC 时间。
struct GpsTime {
  // 时间字段是否有效。
  bool ready = false;
  // UTC 小时。
  uint8_t hour = 0;
  // UTC 分钟。
  uint8_t minute = 0;
  // UTC 秒，保留小数部分。
  float second = 0.0F;
};

// GNSS 语句中解析出的 UTC 日期。
struct GpsDate {
  // 日期字段是否有效。
  bool ready = false;
  // UTC 日期中的日。
  uint8_t day = 0;
  // UTC 日期中的月。
  uint8_t month = 0;
  // UTC 日期中的年。
  uint16_t year = 0;
};

// GNSS 经纬度坐标，保留原始度分格式和方向。
struct GpsCoordinate {
  // 坐标字段是否有效。
  bool ready = false;
  // 坐标度数部分。
  uint8_t degrees = 0;
  // 坐标分钟部分。
  float minutes = 0.0F;
  // 合成后的度分数值。
  double degrees_minutes = 0.0;
  // N/S/E/W 方向字符串。
  char direction[3] = {};
};

// 星座和卫星列表使用固定大小快照，便于通过 FreeRTOS 队列传递。
enum class GpsConstellation : uint8_t {
  kUnknown,
  kGps,
  kBeidou,
  kGlonass,
  kGalileo,
  kQzss,
  kSbas,
  kNavic,
  kCount
};
inline constexpr size_t kGpsConstellationCount =
    static_cast<size_t>(GpsConstellation::kCount);
inline constexpr size_t kGpsSatelliteDisplayCount = 20;

struct GpsSatellite {
  GpsConstellation constellation = GpsConstellation::kUnknown;
  uint16_t id = 0;
  bool cn0_ready = false;
  uint8_t cn0 = 0;
  bool used_ready = false;
  bool used = false;
};

struct GpsConstellationStatus {
  bool used_ready = false;
  bool visible_ready = false;
  uint16_t used = 0;
  uint16_t visible = 0;
};

// GPS 运行状态和最近一次 GNSS 解析结果。
struct GpsStatus {
  // GPS 模块是否已唤醒运行。
  bool running = false;
  // 本轮是否读取到串口数据。
  bool data_ready = false;
  // 最近一次 NMEA 解析是否成功。
  bool parse_success = false;
  // 当前定位状态是否有效。
  bool positioned = false;
  // 最近一次从串口读取的字节数。
  size_t bytes_read = 0;
  // UI 建议轮询间隔，单位为毫秒。
  uint32_t update_interval_ms = 1000;
  // RMC 定位状态文本。
  char location_status[8] = {};
  // RMC 模式指示文本。
  char mode_indicator[8] = {};
  // RMC 导航状态文本。
  char navigational_status[8] = {};
  // UTC 时间。
  GpsTime utc;
  // UTC 日期。
  GpsDate date;
  // 纬度坐标。
  GpsCoordinate latitude;
  // 经度坐标。
  GpsCoordinate longitude;
  // 速度字段是否有效。
  bool speed_ready = false;
  // 地面速度，单位为节。
  float speed_knots = 0.0F;
  // 地面速度，单位为 km/h。
  float speed_kmh = 0.0F;
  // 航向字段是否有效。
  bool course_ready = false;
  // 地面航向，单位为度。
  float course_degree = 0.0F;
  // 定位质量字段是否有效。
  bool fix_quality_ready = false;
  // GGA 定位质量。
  uint8_t fix_quality = 0;
  // 定位模式字段是否有效。
  bool fix_mode_ready = false;
  // GSA 定位模式。
  uint8_t fix_mode = 0;
  // 使用卫星数字段是否有效。
  bool satellites_used_ready = false;
  // 当前参与定位的卫星数。
  uint8_t satellites_used = 0;
  // 可见卫星数字段是否有效。
  bool satellites_in_view_ready = false;
  // 当前可见卫星数。
  uint16_t satellites_in_view = 0;
  // 已解析的卫星信息条目数量。
  size_t satellite_info_count = 0;
  // 按载噪比降序排列的前 20 颗卫星；统计数量不受此显示上限限制。
  GpsSatellite satellites[kGpsSatelliteDisplayCount] = {};
  size_t satellite_display_count = 0;
  GpsConstellationStatus constellations[kGpsConstellationCount] = {};
  GpsConstellation strongest_satellite_constellation = GpsConstellation::kUnknown;
  // 最强卫星字段是否有效。
  bool strongest_satellite_ready = false;
  // 最强卫星 ID。
  uint16_t strongest_satellite_id = 0;
  // 最强卫星载噪比。
  int16_t strongest_satellite_cn0 = 0;
  // HDOP 字段是否有效。
  bool hdop_ready = false;
  // 水平精度因子。
  float hdop = 0.0F;
  // PDOP 字段是否有效。
  bool pdop_ready = false;
  // 位置精度因子。
  float pdop = 0.0F;
  // VDOP 字段是否有效。
  bool vdop_ready = false;
  // 垂直精度因子。
  float vdop = 0.0F;
  // 海拔字段是否有效。
  bool altitude_ready = false;
  // 海拔高度。
  float altitude = 0.0F;
  // 海拔单位。
  char altitude_unit[4] = {};
};

class GpsProvider {
 public:
  virtual ~GpsProvider() = default;

  virtual bool SetGpsEnabled(bool enabled) = 0;

  /**
   * @brief 读取 GPS 状态和最新 GNSS 解析数据
   * @param status GPS 状态输出地址
   * @return 读取成功返回 true，否则返回 false
   */
  virtual bool ReadGpsStatus(GpsStatus* status) = 0;
};

}  // namespace lilygo_box::hal
