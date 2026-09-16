/*
 * @Description: GNSS 流式解析结果与应用状态转换实现
 * @Author: LILYGO_L
 * @Date: 2026-09-16 00:00:00
 * @LastEditTime: 2026-09-16 00:00:00
 * @License: GPL 3.0
 */
#include "hal/device/common/gnss_utils.h"

#include <algorithm>
#include <cstdio>

namespace lilygo_box::hal::gnss_utils {
namespace {

using NmeaParser = cpp_bus_driver::NmeaParser;
constexpr float kKilometersPerNauticalMile = 1.852F;

/**
 * @brief 将解析器的有效坐标转换为界面使用的度分格式
 * @param source NMEA 坐标
 * @param target 应用坐标
 */
void ApplyCoordinate(
    const NmeaParser::Coordinate& source, GpsCoordinate* target) {
  if (!source.valid) {
    return;
  }
  target->ready = true;
  target->degrees = static_cast<uint8_t>(source.degrees);
  target->minutes = static_cast<float>(source.minutes);
  target->degrees_minutes = source.degrees * 100.0 + source.minutes;
  target->direction[0] = source.direction;
  target->direction[1] = '\0';
}

/**
 * @brief 将有效的 NMEA 单字符状态写入固定缓冲区
 * @param value 可选状态字段
 * @param target 至少容纳两个字符的目标缓冲区
 */
void ApplyIndicator(
    const NmeaParser::OptionalValue<char>& value, char* target) {
  if (value.valid) {
    target[0] = value.value;
    target[1] = '\0';
  }
}

}  // namespace

void ApplyUpdate(const NmeaParser::Update& update, GpsStatus* status) {
  if (status == nullptr) {
    return;
  }
  const auto& rmc = update.rmc;
  const auto& gga = update.gga;
  const auto& vtg = update.vtg;
  ApplyIndicator(rmc.data_status, status->location_status);
  ApplyIndicator(rmc.positioning_mode.valid ? rmc.positioning_mode
                                            : vtg.positioning_mode,
      status->mode_indicator);
  ApplyIndicator(rmc.navigational_status, status->navigational_status);

  const auto& utc = rmc.utc.valid ? rmc.utc
                   : update.zda.utc.valid ? update.zda.utc
                                          : gga.utc;
  if (utc.valid) {
    status->utc.ready = true;
    status->utc.hour = utc.hour;
    status->utc.minute = utc.minute;
    status->utc.second = static_cast<float>(utc.second);
  }
  const auto& date = rmc.date.valid ? rmc.date : update.zda.date;
  if (date.valid) {
    status->date.ready = true;
    status->date.day = date.day;
    status->date.month = date.month;
    status->date.year = date.two_digit_year ? 2000 + date.year : date.year;
  }
  ApplyCoordinate(rmc.position.latitude.valid ? rmc.position.latitude
                                               : gga.position.latitude,
      &status->latitude);
  ApplyCoordinate(rmc.position.longitude.valid ? rmc.position.longitude
                                                : gga.position.longitude,
      &status->longitude);
  if (rmc.data_status.valid) {
    status->positioned = rmc.data_status.value == 'A' &&
                         status->latitude.ready && status->longitude.ready;
  } else if (gga.fix_quality.valid) {
    status->positioned = gga.fix_quality.value != 0 &&
                         status->latitude.ready && status->longitude.ready;
  }

  if (rmc.speed_over_ground_knots.valid) {
    status->speed_ready = true;
    status->speed_knots = rmc.speed_over_ground_knots.value;
    status->speed_kmh = status->speed_knots * kKilometersPerNauticalMile;
  } else if (vtg.speed_kilometers_per_hour.valid || vtg.speed_knots.valid) {
    status->speed_ready = true;
    status->speed_kmh = vtg.speed_kilometers_per_hour.valid
                            ? vtg.speed_kilometers_per_hour.value
                            : vtg.speed_knots.value * kKilometersPerNauticalMile;
    status->speed_knots = status->speed_kmh / kKilometersPerNauticalMile;
  }
  const auto& course = rmc.course_over_ground_degrees.valid
                           ? rmc.course_over_ground_degrees
                           : vtg.true_course_degrees;
  if (course.valid) {
    status->course_ready = true;
    status->course_degree = course.value;
  }
  if (gga.fix_quality.valid) {
    status->fix_quality_ready = true;
    status->fix_quality = gga.fix_quality.value;
  }
  if (gga.satellites_used.valid) {
    status->satellites_used_ready = true;
    status->satellites_used = gga.satellites_used.value;
  }
  if (gga.hdop.valid) {
    status->hdop_ready = true;
    status->hdop = gga.hdop.value;
  }
  if (gga.altitude_meters.valid) {
    status->altitude_ready = true;
    status->altitude = static_cast<float>(gga.altitude_meters.value);
    std::snprintf(status->altitude_unit, sizeof(status->altitude_unit), "M");
  }
  for (const auto& gsa : update.gsa) {
    if (gsa.navigation_mode.valid) {
      status->fix_mode_ready = true;
      status->fix_mode = gsa.navigation_mode.value;
    }
    if (gsa.hdop.valid && !gga.hdop.valid) {
      status->hdop_ready = true;
      status->hdop = gsa.hdop.value;
    }
    if (gsa.pdop.valid) {
      status->pdop_ready = true;
      status->pdop = gsa.pdop.value;
    }
    if (gsa.vdop.valid) {
      status->vdop_ready = true;
      status->vdop = gsa.vdop.value;
    }
  }
  if (!update.gsv.empty()) {
    status->satellites_in_view_ready = true;
    status->satellites_in_view = 0;
    status->satellite_info_count = 0;
    status->strongest_satellite_ready = false;
    for (const auto& gsv : update.gsv) {
      status->satellites_in_view =
          std::max(status->satellites_in_view, gsv.total_satellite_count);
      status->satellite_info_count += gsv.satellites.size();
      for (const auto& satellite : gsv.satellites) {
        if (satellite.id.valid && satellite.carrier_to_noise_db_hz.valid &&
            (!status->strongest_satellite_ready ||
                satellite.carrier_to_noise_db_hz.value >
                    status->strongest_satellite_cn0)) {
          status->strongest_satellite_ready = true;
          status->strongest_satellite_id = satellite.id.value;
          status->strongest_satellite_cn0 =
              satellite.carrier_to_noise_db_hz.value;
        }
      }
    }
  }
}

}  // namespace lilygo_box::hal::gnss_utils
