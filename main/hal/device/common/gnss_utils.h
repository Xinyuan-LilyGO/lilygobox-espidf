/*
 * @Description: GNSS 流式解析结果与应用状态转换接口
 * @Author: LILYGO_L
 * @Date: 2026-09-16 00:00:00
 * @LastEditTime: 2026-09-16 00:00:00
 * @License: GPL 3.0
 */
#pragma once

#include "hal/providers/gps_provider.h"
#include "parser/nmea_parser.h"

namespace lilygo_box::hal::gnss_utils {

/**
 * @brief 将本次有效 NMEA 字段合并到 GPS 状态快照
 * @param update 流式解析器提供的本次更新
 * @param status 待更新的状态，不覆盖本次未提供的字段
 */
void ApplyUpdate(
    const cpp_bus_driver::NmeaParser::Update& update, GpsStatus* status);

}  // namespace lilygo_box::hal::gnss_utils
