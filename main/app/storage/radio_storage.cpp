/*
 * @Description: Radio 配置列表与按芯片激活状态持久化实现
 * @Author: LILYGO_L
 * @Date: 2026-07-16 00:00:00
 * @LastEditTime: 2026-07-30 18:00:00
 * @License: GPL 3.0
 */
#include "app/storage/radio_storage.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <memory>
#include <new>

#include "app/storage/storage_internal.h"
#include "app/storage/tlv_storage.h"
#include "base/logger.h"
#include "esp_err.h"
#include "nvs.h"

namespace lilygo_box::app {
namespace {

constexpr const char* kRadioProfilesNvsNamespace = "settings";
constexpr const char* kRadioProfilesNvsKey = "radio_profiles";
constexpr size_t kRadioProfileTlvCapacity = 512;
constexpr size_t kRadioProfilesTlvCapacity = 4096;
constexpr size_t kRadioChipStateCapacity =
    static_cast<size_t>(radio::ChipType::kNrf24l01) + 1;

// 已分配字段编号只允许保留，禁止改号或复用。
enum class RadioProfilesField : uint16_t {
  kActiveProfileId = 1,
  kNextProfileId = 2,
  kProfile = 3,
};

// 子配置字段同样保持永久稳定，未知字段由旧固件跳过。
enum class RadioProfileField : uint16_t {
  kId = 1,
  kName = 2,
  kChip = 3,
  kProtocol = 4,
  kFrequencyHz = 5,
  kBandwidthHz = 6,
  kPreambleLength = 7,
  kSpreadingFactor = 8,
  kCodingRateDenominator = 9,
  kSyncWord = 10,
  kOutputPowerDbm = 11,
  kCrcEnabled = 12,
  kInvertIq = 13,
  kRxBoosted = 14,
  kAntenna = 15,
  kAutoSendEnabled = 16,
  kAutoSendText = 17,
  kAutoSendIntervalMs = 18,
  kGfskDataRateBps = 19,
  kGfskFrequencyDeviationHz = 20,
  kGfskReceiveBandwidthHz = 21,
  kGfskSyncWord = 22,
  kGfskWhiteningEnabled = 23,
  kGfskFecEnabled = 24,
  kEsbChannel = 25,
  kEsbDataRateBps = 26,
  kEsbAddress = 27,
  kEsbAddressWidth = 28,
  kEsbCrcLengthBits = 29,
  kEsbRetransmitCount = 30,
  kEsbRetransmitDelayUs = 31,
  kEsbAutoAckEnabled = 32,
  kEsbDynamicPayloadEnabled = 33,
  kActive = 34,
};

void ResetProfile(RadioProfile* profile) {
  if (profile == nullptr) {
    return;
  }
  *profile = RadioProfile{};
}

void ResetPreferences(RadioPreferences* preferences) {
  if (preferences == nullptr) {
    return;
  }
  *preferences = RadioPreferences{};
}

bool HasProfileIdBefore(
    const RadioPreferences& preferences, size_t end, uint32_t id) {
  for (size_t index = 0; index < end; ++index) {
    if (preferences.profiles[index].id == id) {
      return true;
    }
  }
  return false;
}

/**
 * @brief 判断芯片是否支持指定 LoRa 带宽
 * @param chip 射频芯片类型
 * @param frequency_hz 工作频率
 * @param bandwidth_hz LoRa 带宽
 * @return 带宽有效返回 true
 */

bool IsSupportedBandwidth(
    radio::ChipType chip, uint32_t frequency_hz,uint32_t bandwidth_hz) {
  const bool common_bandwidth = bandwidth_hz == 62500 || bandwidth_hz == 125000 ||
      bandwidth_hz == 250000 || bandwidth_hz == 500000;
  if (chip == radio::ChipType::kLr1121 && frequency_hz >= 2400000000U) {
    return bandwidth_hz == 200000 || bandwidth_hz == 400000 ||
           bandwidth_hz == 800000;
  }
  return common_bandwidth;
}

/**
 * @brief 判断射频芯片类型是否由当前存储格式支持
 * @param chip 射频芯片类型
 * @return 芯片类型有效返回 true
 */
bool IsSupportedChip(radio::ChipType chip) {
  return chip == radio::ChipType::kSx1262 ||
         chip == radio::ChipType::kLr1121 ||
         chip == radio::ChipType::kCc1101 ||
         chip == radio::ChipType::kNrf24l01;
}

/**
 * @brief 判断芯片与 LoRa 工作频率组合是否有效
 * @param chip 射频芯片类型
 * @param frequency_hz 工作频率
 * @return 频率可用返回 true
 */
bool IsSupportedFrequency(radio::ChipType chip, uint32_t frequency_hz) {
  if (chip == radio::ChipType::kCc1101) {
    return (frequency_hz >= 300000000U && frequency_hz <= 348000000U) ||
           (frequency_hz >= 387000000U && frequency_hz <= 464000000U) ||
           (frequency_hz >= 779000000U && frequency_hz <= 928000000U);
  }
  if (chip == radio::ChipType::kNrf24l01) {
    return frequency_hz >= 2400000000U && frequency_hz <= 2525000000U;
  }
  const bool sub_ghz =
      frequency_hz >= 150000000U && frequency_hz <= 960000000U;
  const bool lr1121_hf = chip == radio::ChipType::kLr1121 &&
                         frequency_hz >= 2400000000U &&
                         frequency_hz <= 2500000000U;
  return sub_ghz || lr1121_hf;
}

/**
 * @brief 获取当前芯片和频段允许的最大输出功率
 * @param chip 射频芯片类型
 * @param frequency_hz 工作频率
 * @return 最大输出功率，单位 dBm
 */
int8_t MaximumOutputPowerDbm(radio::ChipType chip, uint32_t frequency_hz) {
  if (chip == radio::ChipType::kCc1101) {
    return 10;
  }
  if (chip == radio::ChipType::kNrf24l01) {
    return 0;
  }
  const bool lr1121_hf =
      chip == radio::ChipType::kLr1121 && frequency_hz >= 2400000000U;
  return lr1121_hf ? 13 : 22;
}

/**
 * @brief 判断 CC1101 发射功率是否可由当前 PATABLE 表达
 * @param power_dbm 发射功率，单位为 dBm
 * @return 功率受支持时返回 true
 */
bool IsCc1101OutputPower(int8_t power_dbm) {
  constexpr int8_t kOutputPowers[] = {-30, -20, -15, -10, 0, 5, 7, 10};
  return std::find(std::begin(kOutputPowers), std::end(kOutputPowers),
             power_dbm) != std::end(kOutputPowers);
}

/**
 * @brief 判断 nRF24L01 发射功率是否受支持
 * @param power_dbm 发射功率，单位为 dBm
 * @return 功率受支持时返回 true
 */
bool IsNrf24l01OutputPower(int8_t power_dbm) {
  return power_dbm == -18 || power_dbm == -12 || power_dbm == -6 ||
         power_dbm == 0;
}

/**
 * @brief 判断 Enhanced ShockBurst 空中速率是否受支持
 * @param data_rate_bps 空中数据速率，单位为 bit/s
 * @return 速率受支持时返回 true
 */
bool IsEnhancedShockBurstDataRate(uint32_t data_rate_bps) {
  return data_rate_bps == 250000 || data_rate_bps == 1000000 ||
         data_rate_bps == 2000000;
}

/**
 * @brief 判断 CC1101 前导码长度是否可由寄存器表达
 * @param length_bits 前导码长度，单位为 bit
 * @return 长度受支持时返回 true
 */
bool IsCc1101PreambleLength(uint16_t length_bits) {
  constexpr uint16_t kPreambleLengths[] = {
      16, 24, 32, 48, 64, 96, 128, 192};
  return std::find(std::begin(kPreambleLengths),
             std::end(kPreambleLengths), length_bits) !=
         std::end(kPreambleLengths);
}

/**
 * @brief 将 CC1101 接收带宽归一化为 26 MHz 晶振支持的最近硬件挡位
 * @param bandwidth_hz 待归一化的接收带宽，单位为 Hz
 * @return 最近的硬件接收带宽，单位为 Hz
 */
uint32_t NormalizeCc1101ReceiveBandwidth(uint32_t bandwidth_hz) {
  uint32_t selected = radio::kCc1101ReceiveBandwidthsHz[0];
  uint32_t smallest_difference = bandwidth_hz > selected
      ? bandwidth_hz - selected
      : selected - bandwidth_hz;
  for (uint32_t candidate : radio::kCc1101ReceiveBandwidthsHz) {
    const uint32_t difference = bandwidth_hz > candidate
        ? bandwidth_hz - candidate
        : candidate - bandwidth_hz;
    if (difference < smallest_difference) {
      selected = candidate;
      smallest_difference = difference;
    }
  }
  return selected;
}

/**
 * @brief 将 Enhanced ShockBurst 地址编码为五字节 TLV 数据
 * @param address 数值形式的空中地址
 * @param output 五字节地址输出
 */
void EncodeEnhancedShockBurstAddress(uint64_t address, uint8_t* output) {
  for (size_t index = 0; index < 5; ++index) {
    output[index] = static_cast<uint8_t>(address >> (index * 8));
  }
}

/**
 * @brief 从 TLV 字段解码 Enhanced ShockBurst 地址
 * @param field 地址字段
 * @param address 地址输出
 * @return 字段格式有效时返回 true
 */
bool DecodeEnhancedShockBurstAddress(
    const storage::TlvField& field, uint64_t* address) {
  if (address == nullptr || field.size() != 5) {
    return false;
  }
  uint64_t decoded = 0;
  for (size_t index = 0; index < 5; ++index) {
    decoded |= static_cast<uint64_t>(field.data()[index]) << (index * 8);
  }
  *address = decoded;
  return true;
}

uint32_t NextUnusedProfileId(
    const RadioPreferences& preferences, size_t end, uint32_t start) {
  uint32_t candidate = start == 0 ? 1 : start;
  while (HasProfileIdBefore(preferences, end, candidate)) {
    ++candidate;
    if (candidate == 0) {
      candidate = 1;
    }
  }
  return candidate;
}

template <size_t Capacity>
void NormalizeString(char (&value)[Capacity]) {
  value[Capacity - 1] = '\0';
  size_t length = 0;
  while (length < Capacity && value[length] != '\0') {
    ++length;
  }
  std::fill(value + length + 1, value + Capacity, '\0');
}

void NormalizePreferences(RadioPreferences* preferences) {
  if (preferences == nullptr) {
    return;
  }
  RadioPreferences& result = *preferences;
  result.profile_count = std::min(result.profile_count, kRadioProfileCapacity);
  uint32_t maximum_id = 0;
  std::array<bool, kRadioChipStateCapacity> active_chips = {};
  for (size_t index = 0; index < result.profile_count; ++index) {
    RadioProfile& profile = result.profiles[index];
    NormalizeString(profile.name);
    NormalizeString(profile.auto_send_text);
    if (profile.name[0] == '\0') {
      std::snprintf(profile.name, sizeof(profile.name),
          "Radio profile %u", static_cast<unsigned>(index + 1));
      NormalizeString(profile.name);
    }
    if (!IsSupportedChip(profile.chip)) {
      profile.chip = radio::ChipType::kSx1262;
    }
    const size_t chip_index = static_cast<size_t>(profile.chip);
    if (chip_index >= std::size(active_chips) ||
        (profile.active && active_chips[chip_index])) {
      profile.active = false;
    } else if (profile.active) {
      active_chips[chip_index] = true;
    }
    if (profile.chip == radio::ChipType::kCc1101) {
      profile.protocol = radio::ProtocolType::kGfsk;
    } else if (profile.chip == radio::ChipType::kNrf24l01) {
      profile.protocol = radio::ProtocolType::kEnhancedShockBurst;
    } else {
      profile.protocol = radio::ProtocolType::kLora;
    }
    if (!IsSupportedFrequency(profile.chip, profile.frequency_hz)) {
      profile.frequency_hz = profile.chip == radio::ChipType::kNrf24l01
                                 ? 2400000000U
                                 : 868000000U;
    }
    if (profile.protocol == radio::ProtocolType::kLora &&
        !IsSupportedBandwidth(
            profile.chip, profile.frequency_hz, profile.bandwidth_hz)) {
      profile.bandwidth_hz = profile.chip == radio::ChipType::kLr1121 &&
                                     profile.frequency_hz >= 2400000000U
                                 ? 200000
                                 : 125000;
    }
    if (profile.protocol == radio::ProtocolType::kLora) {
      if (profile.preamble_length == 0) {
        profile.preamble_length = 8;
      }
      if (profile.spreading_factor < 5 ||
          profile.spreading_factor > 12) {
        profile.spreading_factor = 7;
      }
      if (profile.coding_rate_denominator < 5 ||
          profile.coding_rate_denominator > 8) {
        profile.coding_rate_denominator = 5;
      }
      profile.output_power_dbm = std::clamp<int8_t>(
          profile.output_power_dbm, -9,
          MaximumOutputPowerDbm(profile.chip, profile.frequency_hz));
    } else if (profile.protocol == radio::ProtocolType::kGfsk) {
      if (!IsCc1101PreambleLength(profile.preamble_length)) {
        profile.preamble_length = 32;
      }
      profile.gfsk_data_rate_bps = std::clamp<uint32_t>(
          profile.gfsk_data_rate_bps, 600, 250000);
      profile.gfsk_frequency_deviation_hz = std::clamp<uint32_t>(
          profile.gfsk_frequency_deviation_hz, 1600, 380000);
      profile.gfsk_receive_bandwidth_hz = NormalizeCc1101ReceiveBandwidth(
          profile.gfsk_receive_bandwidth_hz);
      if (!IsCc1101OutputPower(profile.output_power_dbm)) {
        profile.output_power_dbm = 10;
      }
    } else {
      profile.esb_channel = std::min<uint8_t>(profile.esb_channel, 125);
      profile.frequency_hz =
          (2400U + static_cast<uint32_t>(profile.esb_channel)) * 1000000U;
      if (!IsEnhancedShockBurstDataRate(profile.esb_data_rate_bps)) {
        profile.esb_data_rate_bps = 250000;
      }
      profile.esb_address_width = std::clamp<uint8_t>(
          profile.esb_address_width, 3, 5);
      const uint64_t address_mask = profile.esb_address_width == 5
          ? 0xFFFFFFFFFFULL
          : ((1ULL << (profile.esb_address_width * 8)) - 1ULL);
      profile.esb_address &= address_mask;
      if (profile.esb_address == 0) {
        profile.esb_address = 0xE7E7E7E7E7ULL & address_mask;
      }
      if (!IsNrf24l01OutputPower(profile.output_power_dbm)) {
        profile.output_power_dbm = 0;
      }
      if (profile.esb_crc_length_bits != 8 &&
          profile.esb_crc_length_bits != 16) {
        profile.esb_crc_length_bits = 16;
      }
      profile.esb_retransmit_count =
          std::min<uint8_t>(profile.esb_retransmit_count, 15);
      profile.esb_retransmit_delay_us = std::clamp<uint16_t>(
          profile.esb_retransmit_delay_us, 250, 4000);
      profile.esb_retransmit_delay_us = static_cast<uint16_t>(
          (profile.esb_retransmit_delay_us / 250) * 250);
      if (profile.esb_data_rate_bps == 250000 &&
          profile.esb_auto_ack_enabled && profile.esb_retransmit_count != 0) {
        profile.esb_retransmit_delay_us =
            std::max<uint16_t>(profile.esb_retransmit_delay_us, 500);
      }
      if (!profile.esb_auto_ack_enabled) {
        profile.esb_dynamic_payload_enabled = false;
      }
    }
    if (profile.antenna != radio::AntennaType::kInternal &&
        profile.antenna != radio::AntennaType::kExternal) {
      profile.antenna = radio::AntennaType::kInternal;
    }
    profile.auto_send_interval_ms = std::clamp(
        profile.auto_send_interval_ms, kRadioAutoSendMinimumIntervalMs,
        kRadioAutoSendMaximumIntervalMs);
    if (profile.auto_send_text[0] == '\0') {
      profile.auto_send_enabled = false;
    }
    if (profile.id == 0 ||
        HasProfileIdBefore(result, index, profile.id)) {
      profile.id = NextUnusedProfileId(
          result, index, maximum_id + 1);
    }
    maximum_id = std::max(maximum_id, profile.id);
  }
  const uint32_t next_start = std::max(
      result.next_profile_id, maximum_id + 1);
  result.next_profile_id = NextUnusedProfileId(
      result, result.profile_count, next_start);
  for (size_t index = result.profile_count;
       index < kRadioProfileCapacity; ++index) {
    ResetProfile(&result.profiles[index]);
  }
}

bool RadioProfileEqual(
    const RadioProfile& left, const RadioProfile& right) {
  return left.id == right.id && left.active == right.active &&
      std::strcmp(left.name, right.name) == 0 &&
      left.chip == right.chip && left.protocol == right.protocol &&
      left.frequency_hz == right.frequency_hz &&
      left.bandwidth_hz == right.bandwidth_hz &&
      left.preamble_length == right.preamble_length &&
      left.spreading_factor == right.spreading_factor &&
      left.coding_rate_denominator == right.coding_rate_denominator &&
      left.sync_word == right.sync_word &&
      left.output_power_dbm == right.output_power_dbm &&
      left.crc_enabled == right.crc_enabled &&
      left.invert_iq == right.invert_iq &&
      left.rx_boosted == right.rx_boosted &&
      left.gfsk_data_rate_bps == right.gfsk_data_rate_bps &&
      left.gfsk_frequency_deviation_hz ==
          right.gfsk_frequency_deviation_hz &&
      left.gfsk_receive_bandwidth_hz == right.gfsk_receive_bandwidth_hz &&
      left.gfsk_sync_word == right.gfsk_sync_word &&
      left.gfsk_whitening_enabled == right.gfsk_whitening_enabled &&
      left.gfsk_fec_enabled == right.gfsk_fec_enabled &&
      left.esb_channel == right.esb_channel &&
      left.esb_data_rate_bps == right.esb_data_rate_bps &&
      left.esb_address == right.esb_address &&
      left.esb_address_width == right.esb_address_width &&
      left.esb_crc_length_bits == right.esb_crc_length_bits &&
      left.esb_retransmit_count == right.esb_retransmit_count &&
      left.esb_retransmit_delay_us == right.esb_retransmit_delay_us &&
      left.esb_auto_ack_enabled == right.esb_auto_ack_enabled &&
      left.esb_dynamic_payload_enabled == right.esb_dynamic_payload_enabled &&
      left.antenna == right.antenna &&
      left.auto_send_enabled == right.auto_send_enabled &&
      std::strcmp(left.auto_send_text, right.auto_send_text) == 0 &&
      left.auto_send_interval_ms == right.auto_send_interval_ms;
}

bool RadioPreferencesEqual(
    const RadioPreferences& left, const RadioPreferences& right) {
  if (left.profile_count != right.profile_count ||
      left.next_profile_id != right.next_profile_id) {
    return false;
  }
  for (size_t index = 0; index < left.profile_count; ++index) {
    if (!RadioProfileEqual(left.profiles[index], right.profiles[index])) {
      return false;
    }
  }
  return true;
}

bool EncodeRadioProfile(const RadioProfile& profile,
    uint8_t* output, size_t capacity, size_t* encoded_size) {
  storage::TlvWriter writer(
      storage::TlvDomain::kRadioProfile, output, capacity);
  uint8_t esb_address[5] = {};
  EncodeEnhancedShockBurstAddress(profile.esb_address, esb_address);
  return writer.WriteUint32(
             static_cast<uint16_t>(RadioProfileField::kId), profile.id) &&
      writer.WriteBool(static_cast<uint16_t>(RadioProfileField::kActive),
          profile.active) &&
      writer.WriteString(static_cast<uint16_t>(RadioProfileField::kName),
          profile.name, sizeof(profile.name)) &&
      writer.WriteUint8(static_cast<uint16_t>(RadioProfileField::kChip),
          static_cast<uint8_t>(profile.chip)) &&
      writer.WriteUint8(static_cast<uint16_t>(RadioProfileField::kProtocol),
          static_cast<uint8_t>(profile.protocol)) &&
      writer.WriteUint32(
          static_cast<uint16_t>(RadioProfileField::kFrequencyHz),
          profile.frequency_hz) &&
      writer.WriteUint32(
          static_cast<uint16_t>(RadioProfileField::kBandwidthHz),
          profile.bandwidth_hz) &&
      writer.WriteUint16(
          static_cast<uint16_t>(RadioProfileField::kPreambleLength),
          profile.preamble_length) &&
      writer.WriteUint8(
          static_cast<uint16_t>(RadioProfileField::kSpreadingFactor),
          profile.spreading_factor) &&
      writer.WriteUint8(
          static_cast<uint16_t>(
              RadioProfileField::kCodingRateDenominator),
          profile.coding_rate_denominator) &&
      writer.WriteUint8(
          static_cast<uint16_t>(RadioProfileField::kSyncWord),
          profile.sync_word) &&
      writer.WriteInt8(
          static_cast<uint16_t>(RadioProfileField::kOutputPowerDbm),
          profile.output_power_dbm) &&
      writer.WriteBool(
          static_cast<uint16_t>(RadioProfileField::kCrcEnabled),
          profile.crc_enabled) &&
      writer.WriteBool(
          static_cast<uint16_t>(RadioProfileField::kInvertIq),
          profile.invert_iq) &&
      writer.WriteBool(
          static_cast<uint16_t>(RadioProfileField::kRxBoosted),
          profile.rx_boosted) &&
      writer.WriteUint8(
          static_cast<uint16_t>(RadioProfileField::kAntenna),
          static_cast<uint8_t>(profile.antenna)) &&
      writer.WriteBool(
          static_cast<uint16_t>(RadioProfileField::kAutoSendEnabled),
          profile.auto_send_enabled) &&
      writer.WriteString(
          static_cast<uint16_t>(RadioProfileField::kAutoSendText),
          profile.auto_send_text, sizeof(profile.auto_send_text)) &&
      writer.WriteUint32(
          static_cast<uint16_t>(RadioProfileField::kAutoSendIntervalMs),
          profile.auto_send_interval_ms) &&
      writer.WriteUint32(
          static_cast<uint16_t>(RadioProfileField::kGfskDataRateBps),
          profile.gfsk_data_rate_bps) &&
      writer.WriteUint32(static_cast<uint16_t>(
          RadioProfileField::kGfskFrequencyDeviationHz),
          profile.gfsk_frequency_deviation_hz) &&
      writer.WriteUint32(static_cast<uint16_t>(
          RadioProfileField::kGfskReceiveBandwidthHz),
          profile.gfsk_receive_bandwidth_hz) &&
      writer.WriteUint16(
          static_cast<uint16_t>(RadioProfileField::kGfskSyncWord),
          profile.gfsk_sync_word) &&
      writer.WriteBool(static_cast<uint16_t>(
          RadioProfileField::kGfskWhiteningEnabled),
          profile.gfsk_whitening_enabled) &&
      writer.WriteBool(
          static_cast<uint16_t>(RadioProfileField::kGfskFecEnabled),
          profile.gfsk_fec_enabled) &&
      writer.WriteUint8(
          static_cast<uint16_t>(RadioProfileField::kEsbChannel),
          profile.esb_channel) &&
      writer.WriteUint32(
          static_cast<uint16_t>(RadioProfileField::kEsbDataRateBps),
          profile.esb_data_rate_bps) &&
      writer.WriteBytes(
          static_cast<uint16_t>(RadioProfileField::kEsbAddress),
          esb_address, sizeof(esb_address)) &&
      writer.WriteUint8(
          static_cast<uint16_t>(RadioProfileField::kEsbAddressWidth),
          profile.esb_address_width) &&
      writer.WriteUint8(
          static_cast<uint16_t>(RadioProfileField::kEsbCrcLengthBits),
          profile.esb_crc_length_bits) &&
      writer.WriteUint8(
          static_cast<uint16_t>(RadioProfileField::kEsbRetransmitCount),
          profile.esb_retransmit_count) &&
      writer.WriteUint16(
          static_cast<uint16_t>(RadioProfileField::kEsbRetransmitDelayUs),
          profile.esb_retransmit_delay_us) &&
      writer.WriteBool(
          static_cast<uint16_t>(RadioProfileField::kEsbAutoAckEnabled),
          profile.esb_auto_ack_enabled) &&
      writer.WriteBool(static_cast<uint16_t>(
          RadioProfileField::kEsbDynamicPayloadEnabled),
          profile.esb_dynamic_payload_enabled) &&
      writer.Finalize(encoded_size);
}

bool DecodeRadioProfile(
    const uint8_t* data, size_t size, RadioProfile* profile) {
  if (profile == nullptr) {
    return false;
  }
  RadioProfile decoded;
  storage::TlvReader reader(
      storage::TlvDomain::kRadioProfile, data, size);
  storage::TlvField field;
  while (true) {
    const storage::TlvReadResult result = reader.Next(&field);
    if (result == storage::TlvReadResult::kEnd) {
      *profile = decoded;
      return true;
    }
    if (result == storage::TlvReadResult::kInvalid) {
      return false;
    }
    switch (static_cast<RadioProfileField>(field.tag())) {
      case RadioProfileField::kId:
        if (!field.ReadUint32(&decoded.id)) {
          return false;
        }
        break;
      case RadioProfileField::kActive:
        if (!field.ReadBool(&decoded.active)) {
          return false;
        }
        break;
      case RadioProfileField::kName:
        if (!field.CopyString(decoded.name, sizeof(decoded.name))) {
          return false;
        }
        break;
      case RadioProfileField::kChip: {
        uint8_t value = 0;
        if (!field.ReadUint8(&value)) {
          return false;
        }
        decoded.chip = static_cast<radio::ChipType>(value);
        break;
      }
      case RadioProfileField::kProtocol: {
        uint8_t value = 0;
        if (!field.ReadUint8(&value)) {
          return false;
        }
        decoded.protocol = static_cast<radio::ProtocolType>(value);
        break;
      }
      case RadioProfileField::kFrequencyHz:
        if (!field.ReadUint32(&decoded.frequency_hz)) {
          return false;
        }
        break;
      case RadioProfileField::kBandwidthHz:
        if (!field.ReadUint32(&decoded.bandwidth_hz)) {
          return false;
        }
        break;
      case RadioProfileField::kPreambleLength:
        if (!field.ReadUint16(&decoded.preamble_length)) {
          return false;
        }
        break;
      case RadioProfileField::kSpreadingFactor:
        if (!field.ReadUint8(&decoded.spreading_factor)) {
          return false;
        }
        break;
      case RadioProfileField::kCodingRateDenominator:
        if (!field.ReadUint8(&decoded.coding_rate_denominator)) {
          return false;
        }
        break;
      case RadioProfileField::kSyncWord:
        if (!field.ReadUint8(&decoded.sync_word)) {
          return false;
        }
        break;
      case RadioProfileField::kOutputPowerDbm:
        if (!field.ReadInt8(&decoded.output_power_dbm)) {
          return false;
        }
        break;
      case RadioProfileField::kCrcEnabled:
        if (!field.ReadBool(&decoded.crc_enabled)) {
          return false;
        }
        break;
      case RadioProfileField::kInvertIq:
        if (!field.ReadBool(&decoded.invert_iq)) {
          return false;
        }
        break;
      case RadioProfileField::kRxBoosted:
        if (!field.ReadBool(&decoded.rx_boosted)) {
          return false;
        }
        break;
      case RadioProfileField::kAntenna: {
        uint8_t value = 0;
        if (!field.ReadUint8(&value)) {
          return false;
        }
        decoded.antenna = static_cast<radio::AntennaType>(value);
        break;
      }
      case RadioProfileField::kAutoSendEnabled:
        if (!field.ReadBool(&decoded.auto_send_enabled)) {
          return false;
        }
        break;
      case RadioProfileField::kAutoSendText:
        if (!field.CopyString(decoded.auto_send_text,
                sizeof(decoded.auto_send_text))) {
          return false;
        }
        break;
      case RadioProfileField::kAutoSendIntervalMs:
        if (!field.ReadUint32(&decoded.auto_send_interval_ms)) {
          return false;
        }
        break;
      case RadioProfileField::kGfskDataRateBps:
        if (!field.ReadUint32(&decoded.gfsk_data_rate_bps)) {
          return false;
        }
        break;
      case RadioProfileField::kGfskFrequencyDeviationHz:
        if (!field.ReadUint32(&decoded.gfsk_frequency_deviation_hz)) {
          return false;
        }
        break;
      case RadioProfileField::kGfskReceiveBandwidthHz:
        if (!field.ReadUint32(&decoded.gfsk_receive_bandwidth_hz)) {
          return false;
        }
        break;
      case RadioProfileField::kGfskSyncWord:
        if (!field.ReadUint16(&decoded.gfsk_sync_word)) {
          return false;
        }
        break;
      case RadioProfileField::kGfskWhiteningEnabled:
        if (!field.ReadBool(&decoded.gfsk_whitening_enabled)) {
          return false;
        }
        break;
      case RadioProfileField::kGfskFecEnabled:
        if (!field.ReadBool(&decoded.gfsk_fec_enabled)) {
          return false;
        }
        break;
      case RadioProfileField::kEsbChannel:
        if (!field.ReadUint8(&decoded.esb_channel)) {
          return false;
        }
        break;
      case RadioProfileField::kEsbDataRateBps:
        if (!field.ReadUint32(&decoded.esb_data_rate_bps)) {
          return false;
        }
        break;
      case RadioProfileField::kEsbAddress:
        if (!DecodeEnhancedShockBurstAddress(field, &decoded.esb_address)) {
          return false;
        }
        break;
      case RadioProfileField::kEsbAddressWidth:
        if (!field.ReadUint8(&decoded.esb_address_width)) {
          return false;
        }
        break;
      case RadioProfileField::kEsbCrcLengthBits:
        if (!field.ReadUint8(&decoded.esb_crc_length_bits)) {
          return false;
        }
        break;
      case RadioProfileField::kEsbRetransmitCount:
        if (!field.ReadUint8(&decoded.esb_retransmit_count)) {
          return false;
        }
        break;
      case RadioProfileField::kEsbRetransmitDelayUs:
        if (!field.ReadUint16(&decoded.esb_retransmit_delay_us)) {
          return false;
        }
        break;
      case RadioProfileField::kEsbAutoAckEnabled:
        if (!field.ReadBool(&decoded.esb_auto_ack_enabled)) {
          return false;
        }
        break;
      case RadioProfileField::kEsbDynamicPayloadEnabled:
        if (!field.ReadBool(&decoded.esb_dynamic_payload_enabled)) {
          return false;
        }
        break;
      default:
        break;
    }
  }
}

bool EncodeRadioPreferences(const RadioPreferences& preferences,
    uint8_t* output, size_t capacity, size_t* encoded_size) {
  storage::TlvWriter writer(
      storage::TlvDomain::kRadioProfiles, output, capacity);
  uint32_t legacy_active_profile_id = 0;
  for (size_t index = 0; index < preferences.profile_count; ++index) {
    if (preferences.profiles[index].active) {
      legacy_active_profile_id = preferences.profiles[index].id;
      break;
    }
  }
  if (!writer.WriteUint32(
          static_cast<uint16_t>(RadioProfilesField::kActiveProfileId),
          legacy_active_profile_id) ||
      !writer.WriteUint32(
          static_cast<uint16_t>(RadioProfilesField::kNextProfileId),
          preferences.next_profile_id)) {
    return false;
  }
  std::array<uint8_t, kRadioProfileTlvCapacity> profile_buffer = {};
  for (size_t index = 0; index < preferences.profile_count; ++index) {
    size_t profile_size = 0;
    if (!EncodeRadioProfile(preferences.profiles[index],
            profile_buffer.data(), profile_buffer.size(), &profile_size) ||
        !writer.WriteBytes(
            static_cast<uint16_t>(RadioProfilesField::kProfile),
            profile_buffer.data(), profile_size)) {
      return false;
    }
  }
  return writer.Finalize(encoded_size);
}

bool DecodeRadioPreferences(const storage::TlvBuffer& buffer,
    RadioPreferences* preferences) {
  if (preferences == nullptr) {
    return false;
  }
  ResetPreferences(preferences);
  uint32_t legacy_active_profile_id = 0;
  storage::TlvReader reader(storage::TlvDomain::kRadioProfiles,
      buffer.data.get(), buffer.size);
  storage::TlvField field;
  while (true) {
    const storage::TlvReadResult result = reader.Next(&field);
    if (result == storage::TlvReadResult::kEnd) {
      bool has_explicit_active_profile = false;
      for (size_t index = 0; index < preferences->profile_count; ++index) {
        has_explicit_active_profile |= preferences->profiles[index].active;
      }
      if (!has_explicit_active_profile && legacy_active_profile_id != 0) {
        for (size_t index = 0; index < preferences->profile_count; ++index) {
          if (preferences->profiles[index].id == legacy_active_profile_id) {
            preferences->profiles[index].active = true;
            break;
          }
        }
      }
      NormalizePreferences(preferences);
      return true;
    }
    if (result == storage::TlvReadResult::kInvalid) {
      return false;
    }
    switch (static_cast<RadioProfilesField>(field.tag())) {
      case RadioProfilesField::kActiveProfileId:
        if (!field.ReadUint32(&legacy_active_profile_id)) {
          return false;
        }
        break;
      case RadioProfilesField::kNextProfileId:
        if (!field.ReadUint32(&preferences->next_profile_id)) {
          return false;
        }
        break;
      case RadioProfilesField::kProfile:
        if (preferences->profile_count >= kRadioProfileCapacity) {
          break;
        }
        if (!DecodeRadioProfile(field.data(), field.size(),
                &preferences->profiles[preferences->profile_count])) {
          return false;
        }
        ++preferences->profile_count;
        break;
      default:
        break;
    }
  }
}

NvsStorageCache<RadioPreferences> g_radio_profiles_cache(
    StorageDomain::kRadioProfiles, RadioPreferencesEqual);

void LogRadioStorageError(const char* operation, esp_err_t error) {
  LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
      "Radio profile NVS %s failed, error=%s\n", operation,
      esp_err_to_name(error));
}

}  // namespace

void InitRadioCache() {
  auto preferences = std::unique_ptr<RadioPreferences>(
      new (std::nothrow) RadioPreferences());
  if (preferences == nullptr) {
    LogMessage(LogLevel::kError, __FILE__, __LINE__,
        "Allocate Radio profile initialization buffer failed\n");
    return;
  }
  nvs_handle_t handle = 0;
  const esp_err_t open_result = OpenApplicationNvs(
      kRadioProfilesNvsNamespace, NVS_READONLY, &handle);
  if (open_result == ESP_OK) {
    storage::TlvBuffer buffer;
    esp_err_t error = ESP_OK;
    const storage::TlvLoadResult result = storage::LoadTlvBuffer(handle,
        kRadioProfilesNvsKey, storage::TlvDomain::kRadioProfiles,
        kRadioProfilesTlvCapacity, &buffer, &error);
    nvs_close(handle);
    if (result == storage::TlvLoadResult::kLoaded &&
        !DecodeRadioPreferences(buffer, preferences.get())) {
      LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
          "Radio profile TLV payload is invalid\n");
      ResetPreferences(preferences.get());
    } else if (result == storage::TlvLoadResult::kInvalid) {
      LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
          "Radio profile TLV container is invalid\n");
    } else if (result == storage::TlvLoadResult::kError) {
      LogRadioStorageError("load", error);
    }
  } else if (open_result != ESP_ERR_NVS_NOT_FOUND) {
    LogRadioStorageError("open", open_result);
  }
  NormalizePreferences(preferences.get());
  if (!g_radio_profiles_cache.Initialize(*preferences)) {
    LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
        "Initialize Radio profile cache failed\n");
  }
}

bool GetRadioPreferences(RadioPreferences* preferences) {
  if (preferences == nullptr) {
    return false;
  }
  if (!g_radio_profiles_cache.Read(preferences)) {
    ResetPreferences(preferences);
    return false;
  }
  return true;
}

bool UpdateRadioPreferences(const RadioPreferences& preferences) {
  auto normalized = std::unique_ptr<RadioPreferences>(
      new (std::nothrow) RadioPreferences(preferences));
  if (normalized == nullptr) {
    return false;
  }
  NormalizePreferences(normalized.get());
  return g_radio_profiles_cache.UpdateAndPersist(*normalized);
}

StorageStageResult StageRadioStorage(nvs_handle_t handle) {
  const RadioPreferences* preferences = nullptr;
  if (!g_radio_profiles_cache.BeginFlush(&preferences)) {
    return StorageStageResult::kClean;
  }
  auto buffer = std::unique_ptr<uint8_t[]>(
      new (std::nothrow) uint8_t[kRadioProfilesTlvCapacity]);
  if (buffer == nullptr) {
    return StorageStageResult::kFailed;
  }
  size_t encoded_size = 0;
  if (!EncodeRadioPreferences(*preferences, buffer.get(),
          kRadioProfilesTlvCapacity, &encoded_size)) {
    return StorageStageResult::kFailed;
  }
  const esp_err_t result = nvs_set_blob(
      handle, kRadioProfilesNvsKey, buffer.get(), encoded_size);
  if (result != ESP_OK) {
    LogRadioStorageError("stage", result);
    return StorageStageResult::kFailed;
  }
  return StorageStageResult::kStaged;
}

void FinishRadioStorage(bool committed) {
  g_radio_profiles_cache.FinishFlush(committed);
}

}  // namespace lilygo_box::app
