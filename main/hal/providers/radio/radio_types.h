/*
 * @Description: Radio Provider 芯片与协议公共类型
 * @Author: LILYGO_L
 * @Date: 2026-07-16 00:00:00
 * @LastEditTime: 2026-07-30 18:00:00
 * @License: GPL 3.0
 */
#pragma once

#include <cstdint>

namespace lilygo_box::radio {

enum class ChipType : uint8_t {
  kUnknown = 0,
  kSx1262 = 1,
  kLr1121 = 2,
  kCc1101 = 3,
  kNrf24l01 = 4,
};

enum class ProtocolType : uint8_t {
  kUnknown = 0,
  kLora = 1,
  kGfsk = 2,
  kEnhancedShockBurst = 3,
};

enum class AntennaType : uint8_t {
  // RF1 板载天线。
  kInternal = 0,
  // RF2 外置天线接口。
  kExternal = 1,
};

// CC1101 使用 26 MHz 晶振时，由 CHANBW_E/CHANBW_M 表达的硬件带宽。
inline constexpr uint32_t kCc1101ReceiveBandwidthsHz[] = {
    58036, 67708, 81250, 101563,
    116071, 135417, 162500, 203125,
    232143, 270833, 325000, 406250,
    464286, 541667, 650000, 812500};

}  // namespace lilygo_box::radio
