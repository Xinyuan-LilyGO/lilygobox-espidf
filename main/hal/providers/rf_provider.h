/*
 * @Description: SX1262 LoRa 射频状态、配置与收发 Provider 接口
 * @Author: LILYGO_L
 * @Date: 2026-07-16 00:00:00
 * @LastEditTime: 2026-07-16 00:00:00
 * @License: GPL 3.0
 */
#pragma once

#include <cstddef>
#include <cstdint>

#include "base/rf_types.h"

namespace lilygo_box::hal {

inline constexpr size_t kRfPayloadCapacity = 255;
inline constexpr size_t kRfCapabilityCapacity = 8;

enum class RfLinkState {
  kInactive,
  kActive,
  kChipError,
};

enum class RfEventType {
  kNone,
  kPacketReceived,
  kTransmitComplete,
  kTransmitFailed,
  kChipError,
};

struct LoraRadioConfig {
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

struct RfRadioConfig {
  uint32_t client_token = 0;
  rf::ChipType chip = rf::ChipType::kUnknown;
  rf::ProtocolType protocol = rf::ProtocolType::kUnknown;
  LoraRadioConfig lora;
};

struct RfCapability {
  rf::ChipType chip = rf::ChipType::kUnknown;
  rf::ProtocolType protocol = rf::ProtocolType::kUnknown;
  size_t maximum_payload_size = 0;
};

struct RfCapabilities {
  RfCapability entries[kRfCapabilityCapacity] = {};
  size_t count = 0;
};

struct RfStatus {
  RfLinkState state = RfLinkState::kInactive;
  uint32_t active_client_token = 0;
  bool hardware_ready = false;
  bool transmitting = false;
};

struct RfEvent {
  RfEventType type = RfEventType::kNone;
  uint32_t client_token = 0;
  uint8_t payload[kRfPayloadCapacity] = {};
  size_t payload_size = 0;
  int8_t rssi_dbm = 0;
  int8_t snr_db = 0;
};

class RfProvider {
 public:
  virtual ~RfProvider() = default;

  virtual bool ReadRfCapabilities(RfCapabilities* capabilities) = 0;
  virtual bool ActivateRf(const RfRadioConfig& config) = 0;
  virtual bool DeactivateRf() = 0;
  virtual bool SendRf(const uint8_t* data, size_t size) = 0;
  virtual bool PollRfEvent(RfEvent* event) = 0;
  virtual bool ReadRfStatus(RfStatus* status) = 0;
};

}  // namespace lilygo_box::hal
