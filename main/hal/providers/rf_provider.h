/*
 * @Description: SX1262 LoRa 射频状态、配置与收发 Provider 接口
 * @Author: LILYGO_L
 * @Date: 2026-07-16 00:00:00
 * @LastEditTime: 2026-07-17 18:40:56
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

enum class RfFailureReason : uint8_t {
  // 当前事件没有错误。
  kNone,
  // 射频硬件未初始化或已离线。
  kHardwareUnavailable,
  // 读取 SX1262 中断状态失败。
  kIrqReadFailed,
  // 清除 SX1262 中断状态失败。
  kIrqClearFailed,
  // SX1262 报告发送超时。
  kHardwareTimeout,
  // 软件看门狗等待发送结果超时。
  kSoftwareTimeout,
  // 发送完成后恢复连续接收失败。
  kReceiveRestartFailed,
};

struct LoraRadioConfig {
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

struct RfRadioConfig {
  // RF 配置的稳定 ID，用于关联异步事件。
  uint32_t client_token = 0;
  // 当前配置使用的物理射频芯片。
  rf::ChipType chip = rf::ChipType::kUnknown;
  // 当前配置使用的空中协议。
  rf::ProtocolType protocol = rf::ProtocolType::kUnknown;
  // LoRa 协议参数。
  LoraRadioConfig lora;
};

struct RfCapability {
  // 当前能力项对应的物理射频芯片。
  rf::ChipType chip = rf::ChipType::kUnknown;
  // 当前能力项支持的空中协议。
  rf::ProtocolType protocol = rf::ProtocolType::kUnknown;
  // 当前芯片和协议组合允许的最大负载长度。
  size_t maximum_payload_size = 0;
};

struct RfCapabilities {
  // 当前设备支持的射频芯片和协议组合。
  RfCapability entries[kRfCapabilityCapacity] = {};
  // entries 数组中的有效能力项数量。
  size_t count = 0;
};

struct RfStatus {
  // 当前射频会话状态。
  RfLinkState state = RfLinkState::kInactive;
  // 当前激活 RF 配置的稳定 ID。
  uint32_t active_client_token = 0;
  // SX1262 驱动是否可用。
  bool hardware_ready = false;
  // SX1262 是否正在发送数据。
  bool transmitting = false;
};

struct RfEvent {
  // 本次轮询得到的事件类型。
  RfEventType type = RfEventType::kNone;
  // 事件所属 RF 配置的稳定 ID。
  uint32_t client_token = 0;
  // 发送请求对应的消息唯一序号，接收事件保持为 0。
  uint64_t request_token = 0;
  // 发送失败或芯片错误的具体原因。
  RfFailureReason failure_reason = RfFailureReason::kNone;
  // 接收事件的数据负载。
  uint8_t payload[kRfPayloadCapacity] = {};
  // 接收数据负载的有效长度。
  size_t payload_size = 0;
  // 接收数据包的信号强度。
  int8_t rssi_dbm = 0;
  // 接收数据包的信噪比。
  int8_t snr_db = 0;
};

class RfProvider {
 public:
  virtual ~RfProvider() = default;

  /**
   * @brief 读取当前设备支持的射频芯片、协议和负载能力
   * @param capabilities 射频能力输出地址
   * @return 能力信息读取成功时返回 true
   */
  virtual bool ReadRfCapabilities(RfCapabilities* capabilities) = 0;

  /**
   * @brief 配置并激活指定射频会话，同时进入接收状态
   * @param config 待激活的射频配置
   * @return 配置成功且接收已启动时返回 true
   */
  virtual bool ActivateRf(const RfRadioConfig& config) = 0;

  /**
   * @brief 停止当前射频会话并将射频芯片切换到待机状态
   * @return 停止成功或射频硬件无需处理时返回 true
   */
  virtual bool DeactivateRf() = 0;

  /**
   * @brief 启动一条可与异步完成事件准确关联的射频发送
   * @param data 待发送数据
   * @param size 数据长度
   * @param request_token 调用方提供的发送请求唯一序号
   * @return 发送命令成功启动时返回 true
   */
  virtual bool SendRf(
      const uint8_t* data, size_t size, uint64_t request_token) = 0;

  /**
   * @brief 非阻塞轮询射频收发完成、接收数据和芯片错误事件
   * @param event 射频事件输出地址，无事件时类型保持为 kNone
   * @return 轮询及必要的硬件状态处理成功时返回 true
   */
  virtual bool PollRfEvent(RfEvent* event) = 0;

  /**
   * @brief 读取当前射频会话、硬件和发送状态
   * @param status 射频状态输出地址
   * @return 状态读取成功时返回 true
   */
  virtual bool ReadRfStatus(RfStatus* status) = 0;
};

}  // namespace lilygo_box::hal
