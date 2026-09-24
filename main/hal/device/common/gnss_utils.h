/*
 * @Description: GNSS 流式解析结果与应用状态转换接口
 * @Author: LILYGO_L
 * @Date: 2026-09-16 00:00:00
 * @LastEditTime: 2026-09-16 00:00:00
 * @License: GPL 3.0
 */
#pragma once

#include <vector>

#include "hal/providers/gps_provider.h"
#include "parser/nmea_parser.h"

namespace lilygo_box::hal::gnss_utils {

/**
 * @brief 跨串口读取缓存卫星分组，生成去重后的卫星列表和各星座统计
 */
class SatelliteTracker {
 public:
  /**
   * @brief 清空卫星分组、使用数量和去重缓存，供新一次 GPS 采集使用
   */
  void Reset();
  /**
   * @brief 合并有效 GGA、GSA 和完整 GSV 分组，并刷新卫星状态快照
   * @param update 本次流式解析更新，未上报的分组保留到过期
   * @param status 卫星状态输出地址；为 nullptr 时只更新内部缓存
   */
  void ApplyUpdate(const cpp_bus_driver::NmeaParser::Update& update,
      GpsStatus* status);
  /**
   * @brief 清理过期数据并重新生成去重统计及信号最强的前 20 颗卫星
   * @param status 待更新的 GPS 状态，提供采样间隔并接收卫星统计；为 nullptr 时不处理
   */
  void Refresh(GpsStatus* status);

 private:
  struct ViewGroup {
    cpp_bus_driver::NmeaParser::Gsv data;
    int64_t received_ms = 0;
  };
  struct UsedGroup {
    cpp_bus_driver::NmeaParser::Gsa data;
    GpsConstellation constellation = GpsConstellation::kUnknown;
    int64_t received_ms = 0;
  };
  int64_t gga_used_received_ms_ = -1;
  uint8_t gga_used_ = 0;
  std::vector<ViewGroup> view_groups_;
  std::vector<UsedGroup> used_groups_;
  std::vector<GpsSatellite> visible_;
  std::vector<GpsSatellite> used_;
};

/**
 * @brief 将本次有效 NMEA 字段合并到 GPS 状态快照
 * @param update 流式解析器提供的本次更新
 * @param satellites 跨串口读取保留的卫星分组缓存
 * @param status 待更新的状态，保留未更新的定位字段并刷新卫星统计
 */
void ApplyUpdate(const cpp_bus_driver::NmeaParser::Update& update,
    SatelliteTracker& satellites, GpsStatus* status);

}  // namespace lilygo_box::hal::gnss_utils
