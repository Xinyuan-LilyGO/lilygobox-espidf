/*
 * @Description: Radio 射频状态、配置与收发 Provider 接口
 * @Author: LILYGO_L
 * @Date: 2026-07-16 00:00:00
 * @LastEditTime: 2026-07-30 18:00:00
 * @License: GPL 3.0
 */
#pragma once

#include <cstddef>
#include <cstdint>

#include "hal/providers/radio/radio_types.h"

namespace lilygo_box::hal {

inline constexpr size_t kRadioPayloadCapacity = 255;
inline constexpr size_t kRadioCapabilityCapacity = 8;
inline constexpr size_t kRadioFrequencyBandCapacity = 3;

enum class RadioLinkState {
  kInactive,
  kActive,
  kChipError,
};

enum class RadioEventType {
  kNone,
  kPacketReceived,
  kTransmitComplete,
  kTransmitFailed,
  kChipError,
};

enum class RadioFailureReason : uint8_t {
  // 当前事件没有错误。
  kNone,
  // 射频硬件未初始化或已离线。
  kHardwareUnavailable,
  // 读取射频芯片中断状态失败。
  kIrqReadFailed,
  // 清除射频芯片中断状态失败。
  kIrqClearFailed,
  // 射频芯片报告发送超时。
  kHardwareTimeout,
  // 软件看门狗等待发送结果超时。
  kSoftwareTimeout,
  // 发送完成后恢复连续接收失败。
  kReceiveRestartFailed,
};

struct LoraRadioConfig {
  // LoRa 中心频率，单位为 Hz。
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
  // 射频芯片发射功率，单位为 dBm。
  int8_t output_power_dbm = 22;
  // 是否启用数据包 CRC 校验。
  bool crc_enabled = true;
  // 是否反转 LoRa IQ 极性。
  bool invert_iq = false;
  // 是否使用射频芯片增强接收模式。
  bool rx_boosted = true;
};

struct RadioConfig {
  // Radio 配置的稳定 ID，用于关联异步事件。
  uint32_t client_token = 0;
  // 当前配置使用的物理射频芯片。
  radio::ChipType chip = radio::ChipType::kUnknown;
  // 当前配置使用的空中协议。
  radio::ProtocolType protocol = radio::ProtocolType::kUnknown;
  // 当前配置使用的物理天线路径。
  radio::AntennaType antenna = radio::AntennaType::kInternal;
  // LoRa 协议参数。
  LoraRadioConfig lora;
};

struct RadioFrequencyBand {
  // 当前频段允许的最低中心频率，单位为 Hz。
  uint32_t minimum_hz = 0;
  // 当前频段允许的最高中心频率，单位为 Hz。
  uint32_t maximum_hz = 0;
};

struct RadioCapability {
  // 当前能力项对应的物理射频芯片。
  radio::ChipType chip = radio::ChipType::kUnknown;
  // 当前能力项支持的空中协议。
  radio::ProtocolType protocol = radio::ProtocolType::kUnknown;
  // 当前芯片和协议组合允许的最大负载长度。
  size_t maximum_payload_size = 0;
  // 当前板级射频路径实际支持的频段。
  RadioFrequencyBand frequency_bands[kRadioFrequencyBandCapacity] = {};
  // frequency_bands 数组中的有效频段数量。
  size_t frequency_band_count = 0;
};

struct RadioCapabilities {
  // 当前设备支持的射频芯片和协议组合。
  RadioCapability entries[kRadioCapabilityCapacity] = {};
  // entries 数组中的有效能力项数量。
  size_t count = 0;
  // 当前设备是否提供可由软件切换的外置天线路径。
  bool supports_external_antenna = false;
};

struct RadioStatus {
  // 当前射频会话状态。
  RadioLinkState state = RadioLinkState::kInactive;
  // 当前激活 Radio 配置的稳定 ID。
  uint32_t active_client_token = 0;
  // 当前射频芯片驱动是否可用。
  bool hardware_ready = false;
  // 当前射频芯片是否正在发送数据。
  bool transmitting = false;
};

struct RadioEvent {
  // 本次轮询得到的事件类型。
  RadioEventType type = RadioEventType::kNone;
  // 事件所属 Radio 配置的稳定 ID。
  uint32_t client_token = 0;
  // 发送请求对应的消息唯一序号，接收事件保持为 0。
  uint64_t request_token = 0;
  // 发送失败或芯片错误的具体原因。
  RadioFailureReason failure_reason = RadioFailureReason::kNone;
  // 接收事件的数据负载。
  uint8_t payload[kRadioPayloadCapacity] = {};
  // 接收数据负载的有效长度。
  size_t payload_size = 0;
  // 接收数据包的信号强度。
  int8_t rssi_dbm = 0;
  // 接收数据包的信噪比。
  int8_t snr_db = 0;
};

class RadioProvider {
 public:
  virtual ~RadioProvider() = default;

  /**
   * @brief 读取当前设备支持的射频芯片、协议、频段和负载能力
   * @param capabilities 射频能力输出地址
   * @return 能力信息读取成功时返回 true
   */
  virtual bool ReadRadioCapabilities(RadioCapabilities* capabilities) = 0;

  /**
   * @brief 配置并激活指定射频会话，同时进入接收状态
   * @param config 待激活的射频配置
   * @return 配置成功且接收已启动时返回 true
   */
  virtual bool ActivateRadio(const RadioConfig& config) = 0;

  /**
   * @brief 停止当前射频会话并将射频芯片切换到待机状态
   * @return 停止成功或射频硬件无需处理时返回 true
   */
  virtual bool DeactivateRadio() = 0;

  /**
   * @brief 启动一条可与异步完成事件准确关联的射频发送
   * @param data 待发送数据
   * @param size 数据长度
   * @param request_token 调用方提供的发送请求唯一序号
   * @return 发送命令成功启动时返回 true
   */
  virtual bool SendRadio(
      const uint8_t* data, size_t size, uint64_t request_token) = 0;

  /**
   * @brief 非阻塞轮询射频收发完成、接收数据和芯片错误事件
   * @param event 射频事件输出地址，无事件时类型保持为 kNone
   * @return 轮询及必要的硬件状态处理成功时返回 true
   */
  virtual bool PollRadioEvent(RadioEvent* event) = 0;

  /**
   * @brief 读取当前射频会话、硬件和发送状态
   * @param status 射频状态输出地址
   * @return 状态读取成功时返回 true
   */
  virtual bool ReadRadioStatus(RadioStatus* status) = 0;
};

}  // namespace lilygo_box::hal
