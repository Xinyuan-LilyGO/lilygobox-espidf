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
  kLr2021 = 2,
  kLr1121 = 3,
  kCc1101 = 4,
  kNrf24l01 = 5,
};

using ChipMask = uint32_t;

constexpr ChipMask ChipMaskFor(ChipType chip) {
  const uint8_t value = static_cast<uint8_t>(chip);
  return value == 0 || value >= 32 ? 0U : (1U << value);
}

enum class ProtocolType : uint8_t {
  kUnknown = 0,
  kLora = 1,
  kGfsk = 2,
  kEnhancedShockBurst = 3,
};

enum class Lr2021CodingRate : uint8_t {
  kStandard4_5 = 1,
  kStandard4_6 = 2,
  kStandard4_7 = 3,
  kStandard4_8 = 4,
  kLongInterleaver4_5 = 5,
  kLongInterleaver4_6 = 6,
  kLongInterleaver4_8 = 7,
  kLongInterleaverConvolutional4_6 = 8,
  kLongInterleaverConvolutional4_8 = 9,
};

inline constexpr uint32_t kLr2021BandwidthsHz[] = {
    31250, 41670, 62500, 83340, 101563, 125000,
    203000, 250000, 406000, 500000, 812000, 1000000};

constexpr bool IsLr2021FrequencySupported(uint32_t frequency_hz) {
  const bool low_frequency =
      frequency_hz >= 150000000U && frequency_hz <= 960000000U;
  const bool high_frequency =
      frequency_hz >= 2400000000U && frequency_hz <= 2500000000U;
  return low_frequency || high_frequency;
}

constexpr uint32_t GetLr2021MaximumBandwidthHz(uint32_t frequency_hz) {
  if (!IsLr2021FrequencySupported(frequency_hz)) {
    return 0;
  }
  return frequency_hz < 434000000U ? 500000U : 1000000U;
}

constexpr bool IsLr2021BandwidthSupported(
    uint32_t frequency_hz, uint32_t bandwidth_hz) {
  for (const uint32_t candidate : kLr2021BandwidthsHz) {
    if (bandwidth_hz == candidate) {
      return bandwidth_hz <= GetLr2021MaximumBandwidthHz(frequency_hz);
    }
  }
  return false;
}

inline constexpr Lr2021CodingRate kLr2021CodingRates[] = {
    Lr2021CodingRate::kStandard4_5,
    Lr2021CodingRate::kStandard4_6,
    Lr2021CodingRate::kStandard4_7,
    Lr2021CodingRate::kStandard4_8,
    Lr2021CodingRate::kLongInterleaver4_5,
    Lr2021CodingRate::kLongInterleaver4_6,
    Lr2021CodingRate::kLongInterleaver4_8,
    Lr2021CodingRate::kLongInterleaverConvolutional4_6,
    Lr2021CodingRate::kLongInterleaverConvolutional4_8,
};

constexpr bool IsLr2021CodingRate(Lr2021CodingRate coding_rate) {
  const uint8_t value = static_cast<uint8_t>(coding_rate);
  return value >= static_cast<uint8_t>(Lr2021CodingRate::kStandard4_5) &&
         value <= static_cast<uint8_t>(
                      Lr2021CodingRate::kLongInterleaverConvolutional4_8);
}

constexpr Lr2021CodingRate StandardLr2021CodingRate(uint8_t denominator) {
  return denominator >= 5 && denominator <= 8
      ? static_cast<Lr2021CodingRate>(denominator - 4)
      : Lr2021CodingRate::kStandard4_5;
}

constexpr uint8_t Lr2021CodingRateDenominator(
    Lr2021CodingRate coding_rate) {
  switch (coding_rate) {
    case Lr2021CodingRate::kStandard4_5:
    case Lr2021CodingRate::kLongInterleaver4_5:
      return 5;
    case Lr2021CodingRate::kStandard4_6:
    case Lr2021CodingRate::kLongInterleaver4_6:
    case Lr2021CodingRate::kLongInterleaverConvolutional4_6:
      return 6;
    case Lr2021CodingRate::kStandard4_7:
      return 7;
    case Lr2021CodingRate::kStandard4_8:
    case Lr2021CodingRate::kLongInterleaver4_8:
    case Lr2021CodingRate::kLongInterleaverConvolutional4_8:
      return 8;
  }
  return 5;
}

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
