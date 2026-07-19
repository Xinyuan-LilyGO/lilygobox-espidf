/*
 * @Description: Radio 芯片与协议公共类型
 * @Author: LILYGO_L
 * @Date: 2026-07-16 00:00:00
 * @LastEditTime: 2026-07-19 01:30:46
 * @License: GPL 3.0
 */
#pragma once

#include <cstdint>

namespace lilygo_box::radio {

enum class ChipType : uint8_t {
  kUnknown = 0,
  kSx1262 = 1,
};

enum class ProtocolType : uint8_t {
  kUnknown = 0,
  kLora = 1,
};

enum class AntennaType : uint8_t {
  // RF1 板载天线。
  kInternal = 0,
  // RF2 外置天线接口。
  kExternal = 1,
};

}  // namespace lilygo_box::radio
