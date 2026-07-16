/*
 * @Description: SX1262 LoRa 配置列表与唯一激活项持久化接口
 * @Author: LILYGO_L
 * @Date: 2026-07-16 00:00:00
 * @LastEditTime: 2026-07-16 00:00:00
 * @License: GPL 3.0
 */
#pragma once

#include <cstddef>
#include <cstdint>

#include "base/rf_types.h"

namespace lilygo_box::app {

inline constexpr size_t kRfProfileCapacity = 8;
inline constexpr size_t kRfProfileNameCapacity = 40;

struct RfProfile {
  uint32_t id = 0;
  char name[kRfProfileNameCapacity] = {};
  rf::ChipType chip = rf::ChipType::kSx1262;
  rf::ProtocolType protocol = rf::ProtocolType::kLora;
  uint32_t frequency_hz = 915000000;
  uint32_t bandwidth_hz = 125000;
  uint16_t preamble_length = 8;
  uint8_t spreading_factor = 7;
  uint8_t coding_rate_denominator = 5;
  uint8_t sync_word = 0x12;
  int8_t output_power_dbm = 22;
  bool crc_enabled = true;
  bool invert_iq = false;
  bool rx_boosted = true;
};

struct RfPreferences {
  RfProfile profiles[kRfProfileCapacity] = {};
  size_t profile_count = 0;
  uint32_t active_profile_id = 0;
  uint32_t next_profile_id = 1;
};

void InitRfCache();
RfPreferences GetRfPreferences();
bool UpdateRfPreferences(const RfPreferences& preferences);

}  // namespace lilygo_box::app
