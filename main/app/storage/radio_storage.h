/*
 * @Description: Radio 配置列表与按芯片激活状态持久化接口
 * @Author: LILYGO_L
 * @Date: 2026-07-16 00:00:00
 * @LastEditTime: 2026-07-30 18:00:00
 * @License: GPL 3.0
 */
#pragma once

#include <cstddef>
#include <cstdint>

#include "hal/providers/radio/radio_types.h"

namespace lilygo_box::app {

inline constexpr size_t kRadioProfileCapacity = 8;
inline constexpr size_t kRadioProfileNameCapacity = 40;
inline constexpr size_t kRadioAutoSendTextCapacity = 128;
inline constexpr uint32_t kRadioAutoSendMinimumIntervalMs = 100;
inline constexpr uint32_t kRadioAutoSendMaximumIntervalMs = 60000;

struct RadioProfile {
  // 配置的稳定唯一标识，用于聊天记录和激活状态关联。
  uint32_t id = 0;
  // 是否激活该配置；同一物理射频芯片最多只能激活一条配置。
  bool active = false;
  // 用户可编辑的配置名称。
  char name[kRadioProfileNameCapacity] = {};
  // 当前配置使用的物理射频芯片。
  radio::ChipType chip = radio::ChipType::kSx1262;
  // 当前配置使用的空中协议。
  radio::ProtocolType protocol = radio::ProtocolType::kLora;
  // 当前协议的中心频率，单位为 Hz。
  uint32_t frequency_hz = 868000000;
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
  // 当前射频芯片的发射功率，单位为 dBm。
  int8_t output_power_dbm = 22;
  // 是否启用数据包 CRC 校验。
  bool crc_enabled = true;
  // 是否反转 LoRa IQ 极性。
  bool invert_iq = false;
  // 是否使用射频芯片增强接收模式。
  bool rx_boosted = true;
  // CC1101 GFSK 空中数据速率，单位为 bit/s。
  uint32_t gfsk_data_rate_bps = 4800;
  // CC1101 GFSK 单边频偏，单位为 Hz。
  uint32_t gfsk_frequency_deviation_hz = 5000;
  // CC1101 接收滤波带宽，单位为 Hz。
  uint32_t gfsk_receive_bandwidth_hz =
      radio::kCc1101ReceiveBandwidthsHz[0];
  // CC1101 16 位同步字。
  uint16_t gfsk_sync_word = 0x12AD;
  // CC1101 是否启用数据白化。
  bool gfsk_whitening_enabled = false;
  // CC1101 是否启用卷积编码 FEC。
  bool gfsk_fec_enabled = false;
  // nRF24L01 信道号，实际频率为 2400 MHz + channel。
  uint8_t esb_channel = 0;
  // nRF24L01 空中数据速率，单位为 bit/s。
  uint32_t esb_data_rate_bps = 250000;
  // nRF24L01 3～5 字节空中地址。
  uint64_t esb_address = 0xE7E7E7E7E7ULL;
  // nRF24L01 空中地址宽度，单位为字节。
  uint8_t esb_address_width = 5;
  // nRF24L01 CRC 长度，单位为 bit。
  uint8_t esb_crc_length_bits = 16;
  // nRF24L01 自动重发次数。
  uint8_t esb_retransmit_count = 0;
  // nRF24L01 自动重发间隔，单位为 us。
  uint16_t esb_retransmit_delay_us = 750;
  // nRF24L01 是否启用自动应答；广播模式默认不等待接收端应答。
  bool esb_auto_ack_enabled = false;
  // nRF24L01 是否启用动态负载长度。
  bool esb_dynamic_payload_enabled = false;
  // 天线路径，默认使用板载天线；是否支持软件切换外置天线由设备能力决定。
  radio::AntennaType antenna = radio::AntennaType::kInternal;
  // 是否按照设定周期自动发送测试字符。
  bool auto_send_enabled = false;
  // 自动发送使用的测试字符，长度受射频负载容量限制。
  char auto_send_text[kRadioAutoSendTextCapacity] = "LilygoBox radio test";
  // 两次自动发送之间的周期，单位为毫秒。
  uint32_t auto_send_interval_ms = 1000;
};

struct RadioPreferences {
  // 用户创建的 Radio 配置列表。
  RadioProfile profiles[kRadioProfileCapacity] = {};
  // 当前有效配置数量。
  size_t profile_count = 0;
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
 * @brief 比较并更新 Radio 配置，存在变化时立即写入 NVS
 * @param preferences 新的 Radio 配置
 * @return 无变化或 NVS 提交成功返回 true，否则返回 false
 */
bool UpdateRadioPreferences(const RadioPreferences& preferences);

}  // namespace lilygo_box::app
