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
#include <cstring>

#include "esp_timer.h"

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

/**
 * @brief 优先按已知 system ID 识别星座，否则回退到 talker 标识
 * @param talker 非空的 NMEA talker 字符串，如 GP、BD、GL 或 GN
 * @param system_id NMEA 系统编号，默认为 0，表示未提供
 * @return 识别出的星座；system ID 和 talker 均无法识别时返回 kUnknown
 */
GpsConstellation Constellation(const char* talker, uint8_t system_id = 0) {
  switch (system_id) {
    case 1:
      return GpsConstellation::kGps;
    case 2:
      return GpsConstellation::kGlonass;
    case 3:
      return GpsConstellation::kGalileo;
    case 4:
      return GpsConstellation::kBeidou;
    case 5:
      return GpsConstellation::kQzss;
    case 6:
      return GpsConstellation::kNavic;
    default:
      break;
  }
  if (std::strcmp(talker, "GP") == 0) {
    return GpsConstellation::kGps;
  }
  if (std::strcmp(talker, "BD") == 0 || std::strcmp(talker, "GB") == 0) {
    return GpsConstellation::kBeidou;
  }
  if (std::strcmp(talker, "GL") == 0) {
    return GpsConstellation::kGlonass;
  }
  if (std::strcmp(talker, "GA") == 0) {
    return GpsConstellation::kGalileo;
  }
  if (std::strcmp(talker, "GQ") == 0 || std::strcmp(talker, "QZ") == 0) {
    return GpsConstellation::kQzss;
  }
  if (std::strcmp(talker, "GI") == 0) {
    return GpsConstellation::kNavic;
  }
  return GpsConstellation::kUnknown;
}

/**
 * @brief 根据卫星编号补充星座并统一 GSA/GSV 使用的卫星标识
 * @param constellation 从 system ID 或 talker 获得的星座
 * @param id NMEA 语句中的原始卫星编号
 * @return 填入星座与归一化编号的卫星记录，其余字段保持默认值
 */
GpsSatellite SatelliteIdentity(GpsConstellation constellation, uint16_t id) {
  // GP 也可能携带 SBAS/QZSS；GN 的星座需要从全局编号推断。
  if (constellation == GpsConstellation::kUnknown ||
      constellation == GpsConstellation::kGps) {
    if (id >= 1 && id <= 32) {
      constellation = GpsConstellation::kGps;
    } else if ((id >= 33 && id <= 64) || (id >= 120 && id <= 158)) {
      constellation = GpsConstellation::kSbas;
    } else if (id >= 65 && id <= 96) {
      constellation = GpsConstellation::kGlonass;
    } else if (id >= 193 && id <= 200) {
      constellation = GpsConstellation::kQzss;
    } else if (id >= 201 && id <= 263) {
      constellation = GpsConstellation::kBeidou;
    } else if (id >= 301 && id <= 336) {
      constellation = GpsConstellation::kGalileo;
    } else if (id >= 401 && id <= 414) {
      constellation = GpsConstellation::kNavic;
    }
  }
  // 统一同一颗卫星的局部/全局编号，确保 GSA 与 GSV 能对应。
  if (constellation == GpsConstellation::kBeidou && id >= 201 && id <= 263) {
    id -= 200;
  }
  if (constellation == GpsConstellation::kGalileo && id >= 301 && id <= 336) {
    id -= 300;
  }
  if (constellation == GpsConstellation::kGlonass && id >= 1 && id <= 32) {
    id += 64;
  }
  if (constellation == GpsConstellation::kQzss && id >= 193 && id <= 202) {
    id -= 192;
  }
  if (constellation == GpsConstellation::kNavic && id >= 401 && id <= 414) {
    id -= 400;
  }
  if (constellation == GpsConstellation::kSbas && id >= 33 && id <= 64) {
    id += 87;
  }
  GpsSatellite satellite;
  satellite.constellation = constellation;
  satellite.id = id;
  return satellite;
}

/**
 * @brief 比较两条已归一化的记录是否属于同一颗卫星
 * @param lhs 第一条卫星记录
 * @param rhs 第二条卫星记录
 * @return 星座和卫星编号均相同时返回 true，否则返回 false
 */
bool SameSatellite(const GpsSatellite& lhs, const GpsSatellite& rhs) {
  return lhs.constellation == rhs.constellation && lhs.id == rhs.id;
}

/**
 * @brief 识别 GSA 所属星座，元数据无法识别时尝试由卫星编号推断
 * @param gsa 待识别的 GSA 语句
 * @return 元数据或所有卫星编号一致指向的星座；无法确定单一星座时返回 kUnknown
 */
GpsConstellation GsaConstellation(const NmeaParser::Gsa& gsa) {
  auto result = Constellation(gsa.metadata.talker_id.c_str(),
      gsa.system_id.valid ? gsa.system_id.value : 0);
  if (result != GpsConstellation::kUnknown || gsa.satellite_ids.empty()) {
    return result;
  }
  // 老版本 GN GSA 没有 system ID，但可能按星座分别输出。
  result = SatelliteIdentity(GpsConstellation::kUnknown, gsa.satellite_ids[0])
               .constellation;
  for (uint16_t id : gsa.satellite_ids) {
    if (SatelliteIdentity(GpsConstellation::kUnknown, id).constellation != result) {
      return GpsConstellation::kUnknown;
    }
  }
  return result;
}

/**
 * @brief 查找匹配分组，无匹配时新增槽位，达到 16 组上限时选取最旧槽位
 * @tparam Groups 元素带有 received_ms 字段的分组容器类型
 * @tparam Match 分组匹配谓词类型
 * @param groups 待查找或扩展的分组缓存
 * @param match 判断缓存分组是否匹配的谓词
 * @return 匹配、新建或最旧分组的可写引用，由调用方填入数据和接收时间
 */
template <typename Groups, typename Match>
auto& FindGroup(Groups& groups, Match match) {
  auto found = std::find_if(groups.begin(), groups.end(), match);
  if (found != groups.end()) {
    return *found;
  }
  constexpr size_t kMaxGroups = 16;
  if (groups.size() == kMaxGroups) {
    return *std::min_element(groups.begin(), groups.end(),
        [](const auto& lhs, const auto& rhs) {
          return lhs.received_ms < rhs.received_ms;
        });
  }
  groups.emplace_back();
  return groups.back();
}

}  // namespace

/**
 * @brief 清空卫星分组、使用数量和去重缓存，供新一次 GPS 采集使用
 */
void SatelliteTracker::Reset() {
  gga_used_received_ms_ = -1;
  gga_used_ = 0;
  view_groups_.clear();
  used_groups_.clear();
  visible_.clear();
  used_.clear();
}

/**
 * @brief 合并有效 GGA、GSA 和完整 GSV 分组，并刷新卫星状态快照
 * @param update 本次流式解析更新，未上报的分组保留到过期
 * @param status 卫星状态输出地址；为 nullptr 时只更新内部缓存
 */
void SatelliteTracker::ApplyUpdate(const NmeaParser::Update& update,
    GpsStatus* status) {
  const int64_t now_ms = esp_timer_get_time() / 1000;
  if (update.gga.satellites_used.valid) {
    gga_used_ = update.gga.satellites_used.value;
    gga_used_received_ms_ = now_ms;
  }
  for (const auto& gsv : update.gsv) {
    if (!gsv.valid || !gsv.complete) {
      continue;
    }
    auto& group = FindGroup(view_groups_, [&gsv](const auto& candidate) {
      const auto& previous = candidate.data;
      return std::strcmp(previous.metadata.talker_id.c_str(),
                 gsv.metadata.talker_id.c_str()) == 0 &&
             previous.signal_id.valid == gsv.signal_id.valid &&
             (!gsv.signal_id.valid ||
                 previous.signal_id.value == gsv.signal_id.value);
    });
    group.data = gsv;
    group.received_ms = now_ms;
  }
  for (const auto& gsa : update.gsa) {
    if (!gsa.valid) {
      continue;
    }
    const auto constellation = GsaConstellation(gsa);
    // 无法确定单一星座且 GSA 报告无定位时，清空旧的参与定位分组。
    if (constellation == GpsConstellation::kUnknown &&
        gsa.navigation_mode.valid && gsa.navigation_mode.value == 1) {
      used_groups_.clear();
    }
    auto& group = FindGroup(used_groups_,
        [&gsa, constellation](const auto& candidate) {
          return candidate.constellation == constellation &&
                 std::strcmp(candidate.data.metadata.talker_id.c_str(),
                     gsa.metadata.talker_id.c_str()) == 0;
        });
    group.data = gsa;
    group.constellation = constellation;
    group.received_ms = now_ms;
  }
  Refresh(status);
}

/**
 * @brief 清理过期数据并重新生成去重统计及信号最强的前 20 颗卫星
 * @param status 待更新的 GPS 状态，提供采样间隔并接收卫星统计；为 nullptr 时不处理
 */
void SatelliteTracker::Refresh(GpsStatus* status) {
  if (status == nullptr) {
    return;
  }
  const int64_t now_ms = esp_timer_get_time() / 1000;
  // 至少保留 30 秒或十个定位周期，容纳 L76K 每五次定位上报的 GSV。
  const int64_t max_age_ms = std::max<int64_t>(30000,
      static_cast<int64_t>(status->update_interval_ms) * 10);
  const auto expired = [now_ms, max_age_ms](const auto& group) {
    return now_ms - group.received_ms > max_age_ms;
  };
  view_groups_.erase(std::remove_if(view_groups_.begin(), view_groups_.end(), expired),
      view_groups_.end());
  used_groups_.erase(std::remove_if(used_groups_.begin(), used_groups_.end(), expired),
      used_groups_.end());
  visible_.clear();
  used_.clear();
  for (auto& constellation : status->constellations) {
    constellation = {};
  }
  for (const auto& group : used_groups_) {
    if (group.constellation == GpsConstellation::kUnknown) {
      for (auto& constellation : status->constellations) {
        constellation.used_ready = true;
      }
    } else {
      status->constellations[static_cast<size_t>(group.constellation)].used_ready = true;
    }
    if (group.data.navigation_mode.valid && group.data.navigation_mode.value == 1) {
      continue;
    }
    for (uint16_t id : group.data.satellite_ids) {
      if (id == 0) {
        continue;
      }
      const auto satellite = SatelliteIdentity(group.constellation, id);
      auto& constellation = status->constellations[static_cast<size_t>(satellite.constellation)];
      constellation.used_ready = true;
      if (std::none_of(used_.begin(), used_.end(), [&satellite](const auto& item) {
            return SameSatellite(item, satellite);
          })) {
        used_.push_back(satellite);
        ++constellation.used;
      }
    }
  }
  for (const auto& group : view_groups_) {
    const auto system = Constellation(group.data.metadata.talker_id.c_str());
    status->constellations[static_cast<size_t>(system)].visible_ready = true;
    for (const auto& source : group.data.satellites) {
      if (!source.id.valid || source.id.value == 0) {
        continue;
      }
      auto satellite = SatelliteIdentity(system, source.id.value);
      satellite.cn0_ready = source.carrier_to_noise_db_hz.valid;
      satellite.cn0 = source.carrier_to_noise_db_hz.value;
      const auto found = std::find_if(visible_.begin(), visible_.end(),
          [&satellite](const auto& item) { return SameSatellite(item, satellite); });
      if (found == visible_.end()) {
        visible_.push_back(satellite);
      } else if (satellite.cn0_ready &&
          (!found->cn0_ready || satellite.cn0 > found->cn0)) {
        // 多信号频段同一卫星只计一次，显示最强信号。
        found->cn0_ready = true;
        found->cn0 = satellite.cn0;
      }
    }
  }
  for (auto& satellite : visible_) {
    auto& constellation = status->constellations[static_cast<size_t>(satellite.constellation)];
    constellation.visible_ready = true;
    ++constellation.visible;
    satellite.used_ready = constellation.used_ready;
    satellite.used = std::any_of(used_.begin(), used_.end(),
        [&satellite](const auto& item) { return SameSatellite(item, satellite); });
  }
  std::sort(visible_.begin(), visible_.end(), [](const auto& lhs, const auto& rhs) {
    if (lhs.cn0_ready != rhs.cn0_ready) {
      return lhs.cn0_ready;
    }
    if (lhs.cn0 != rhs.cn0) {
      return lhs.cn0 > rhs.cn0;
    }
    if (lhs.constellation != rhs.constellation) {
      return lhs.constellation < rhs.constellation;
    }
    return lhs.id < rhs.id;
  });
  // GGA 给出完整定位总数；没有新鲜 GGA 时使用去重后的 GSA 统计。
  const bool gga_used_ready = gga_used_received_ms_ >= 0 &&
      now_ms - gga_used_received_ms_ <= max_age_ms;
  status->satellites_used_ready = gga_used_ready || !used_groups_.empty();
  status->satellites_used = gga_used_ready
      ? gga_used_ : static_cast<uint8_t>(used_.size());
  status->satellites_in_view_ready = !view_groups_.empty();
  status->satellites_in_view = static_cast<uint16_t>(visible_.size());
  status->satellite_info_count = visible_.size();
  status->satellite_display_count =
      std::min(visible_.size(), kGpsSatelliteDisplayCount);
  for (auto& satellite : status->satellites) {
    satellite = {};
  }
  std::copy_n(visible_.begin(), status->satellite_display_count, status->satellites);
  status->strongest_satellite_ready =
      !visible_.empty() && visible_.front().cn0_ready;
  status->strongest_satellite_constellation = status->strongest_satellite_ready
      ? visible_.front().constellation : GpsConstellation::kUnknown;
  status->strongest_satellite_id =
      status->strongest_satellite_ready ? visible_.front().id : 0;
  status->strongest_satellite_cn0 =
      status->strongest_satellite_ready ? visible_.front().cn0 : 0;
}

void ApplyUpdate(const NmeaParser::Update& update,
    SatelliteTracker& satellites, GpsStatus* status) {
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
  satellites.ApplyUpdate(update, status);
}

}  // namespace lilygo_box::hal::gnss_utils
