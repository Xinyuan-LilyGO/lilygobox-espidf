/*
 * @Description: RF 芯片与协议公共类型
 * @Author: LILYGO_L
 * @Date: 2026-07-16 00:00:00
 * @LastEditTime: 2026-07-16 00:00:00
 * @License: GPL 3.0
 */
#pragma once

#include <cstdint>

namespace lilygo_box::rf {

enum class ChipType : uint8_t {
  kUnknown = 0,
  kSx1262 = 1,
};

enum class ProtocolType : uint8_t {
  kUnknown = 0,
  kLora = 1,
};

}  // namespace lilygo_box::rf
