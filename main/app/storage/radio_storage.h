/*
 * @Description: SX1262 LoRa 配置列表与唯一激活项持久化接口
 * @Author: LILYGO_L
 * @Date: 2026-07-16 00:00:00
 * @LastEditTime: 2026-07-18 00:00:00
 * @License: GPL 3.0
 */
#pragma once

#include <cstddef>
#include <cstdint>

#include "base/radio_types.h"

namespace lilygo_box::app {

inline constexpr size_t kRadioProfileCapacity = 8;
inline constexpr size_t kRadioProfileNameCapacity = 40;

struct RadioProfile {
  // 配置的稳定唯一标识，用于聊天记录和激活状态关联。
  uint32_t id = 0;
  // 用户可编辑的配置名称。
  char name[kRadioProfileNameCapacity] = {};
  // 当前配置使用的物理射频芯片。
  radio::ChipType chip = radio::ChipType::kSx1262;
  // 当前配置使用的空中协议。
  radio::ProtocolType protocol = radio::ProtocolType::kLora;
  // LoRa 中心频率，单位为 Hz。
  uint32_t frequency_hz = 915000000;
  // LoRa 信号带宽，单位为 Hz。
  uint32_t bandwidth_hz = 125000;
  // LoRa 前导码符号数量。
  uint16_t preamble_length = 8;
  // LoRa 扩频因子，支持 SF5～SF12。
  uint8_t spreading_factor = 7;
  // LoRa 编码率分母，对应 4/5～4/8。
  uint8_t coding_rate_denominator = 5;
  // LoRa 网络同步字。
  uint8_t sync_word = 0x12;
  // SX1262 发射功率，单位为 dBm。
  int8_t output_power_dbm = 22;
  // 是否启用数据包 CRC 校验。
  bool crc_enabled = true;
  // 是否反转 LoRa IQ 极性。
  bool invert_iq = false;
  // 是否使用 SX1262 增强接收模式。
  bool rx_boosted = true;
};

struct RadioPreferences {
  // 用户创建的 SX1262 LoRa 配置列表。
  RadioProfile profiles[kRadioProfileCapacity] = {};
  // 当前有效配置数量。
  size_t profile_count = 0;
  // 唯一激活配置的 ID，0 表示全部未激活。
  uint32_t active_profile_id = 0;
  // 创建下一条配置时使用的候选 ID。
  uint32_t next_profile_id = 1;
};

/**
 * @brief 从 NVS 初始化 Radio 配置长期 RAM 缓存
 */
void InitRadioCache();

/**
 * @brief 从长期 RAM 缓存读取 Radio 配置
 * @param preferences Radio 配置输出地址，不允许为空
 * @return 读取成功返回 true，否则返回 false
 */
bool GetRadioPreferences(RadioPreferences* preferences);

/**
 * @brief 更新 Radio 配置长期 RAM 缓存并标记延迟落盘
 *
 * 本函数不会立即访问 NVS，数据将在屏幕完全关闭后统一持久化。
 * @param preferences 新的 Radio 配置
 * @return 缓存更新成功返回 true，否则返回 false
 */
bool UpdateRadioPreferences(const RadioPreferences& preferences);

}  // namespace lilygo_box::app
