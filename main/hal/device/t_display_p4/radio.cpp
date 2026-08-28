/*
 * @Description: T-Display-P4 射频硬件实现
 * @Author: LILYGO_L
 * @Date: 2026-08-28 00:00:00
 * @LastEditTime: 2026-08-28 00:00:00
 * @License: GPL 3.0
 */
#include "hal/device/t_display_p4/device.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <iterator>

#include "base/logger.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

namespace lilygo_box::hal {
namespace device = lilygo_device_driver::t_display_p4::device;
namespace gpio = lilygo_device_driver::t_display_p4::gpio;
namespace keyboard_gpio =
    lilygo_device_driver::t_display_p4::keyboard_expansion::gpio;
namespace {

// Radio 发送硬件超时的最小值和额外保护时间。
constexpr uint32_t kMinimumRadioTransmitTimeoutMs = 1000;
constexpr uint32_t kRadioTransmitTimeoutMarginMs = 500;
constexpr uint32_t kRadioTransmitWatchdogGraceMs = 1000;

constexpr size_t kRadioIrqTextCapacity = 160;

// SX1262 IRQ 位与日志名称映射。
struct RadioIrqDescription {
  uint16_t mask;
  const char* name;
};

constexpr std::array<RadioIrqDescription, 10> kRadioIrqDescriptions = {{
    {static_cast<uint16_t>(SX126X_IRQ_TX_DONE), "TX_DONE"},
    {static_cast<uint16_t>(SX126X_IRQ_RX_DONE), "RX_DONE"},
    {static_cast<uint16_t>(SX126X_IRQ_PREAMBLE_DETECTED),
        "PREAMBLE_DETECTED"},
    {static_cast<uint16_t>(SX126X_IRQ_SYNC_WORD_VALID), "SYNC_WORD_VALID"},
    {static_cast<uint16_t>(SX126X_IRQ_HEADER_VALID), "HEADER_VALID"},
    {static_cast<uint16_t>(SX126X_IRQ_HEADER_ERROR), "HEADER_ERROR"},
    {static_cast<uint16_t>(SX126X_IRQ_CRC_ERROR), "CRC_ERROR"},
    {static_cast<uint16_t>(SX126X_IRQ_CAD_DONE), "CAD_DONE"},
    {static_cast<uint16_t>(SX126X_IRQ_CAD_DETECTED), "CAD_DETECTED"},
    {static_cast<uint16_t>(SX126X_IRQ_TIMEOUT), "TIMEOUT"},
}};

/**
 * @brief 将 SX1262 IRQ 位掩码格式化为可读名称和十六进制数值
 * @param irq_mask SX1262 IRQ 位掩码
 * @param output 输出文本缓冲区
 * @param output_size 输出文本缓冲区大小
 */
void FormatRadioIrqMask(uint16_t irq_mask, char* output, size_t output_size) {
  if (output == nullptr || output_size == 0) {
    return;
  }
  output[0] = '\0';
  size_t used = 0;
  bool has_name = false;

  // 追加一个 IRQ 名称并维护输出缓冲区的已用长度。
  const auto append_name = [&](const char* name) {
    const int result = std::snprintf(output + used, output_size - used,
        "%s%s", has_name ? " | " : "", name);
    if (result < 0 || static_cast<size_t>(result) >= output_size - used) {
      output[output_size - 1] = '\0';
      return false;
    }
    used += static_cast<size_t>(result);
    has_name = true;
    return true;
  };

  uint16_t unknown_mask = irq_mask;
  for (const RadioIrqDescription& description : kRadioIrqDescriptions) {
    if ((irq_mask & description.mask) == 0) {
      continue;
    }
    if (!append_name(description.name)) {
      return;
    }
    unknown_mask &= static_cast<uint16_t>(~description.mask);
  }
  if (unknown_mask != 0 && !append_name("UNKNOWN")) {
    return;
  }
  if (!has_name && !append_name("NONE")) {
    return;
  }

  std::snprintf(output + used, output_size - used, " (0x%04X)",
      static_cast<unsigned>(irq_mask));
}

/**
 * @brief 将应用层 LoRa 带宽转换为 SX1262 枚举
 * @param bandwidth_hz 带宽，单位为 Hz
 * @param bandwidth SX1262 带宽枚举输出地址
 * @return 带宽受支持返回 true
 */
bool SelectLoraBandwidth(uint32_t bandwidth_hz,
    sx126x_lora_bw_t* bandwidth) {
  if (bandwidth == nullptr) {
    return false;
  }
  switch (bandwidth_hz) {
    case 62500:
      *bandwidth = SX126X_LORA_BW_062;
      return true;
    case 125000:
      *bandwidth = SX126X_LORA_BW_125;
      return true;
    case 250000:
      *bandwidth = SX126X_LORA_BW_250;
      return true;
    case 500000:
      *bandwidth = SX126X_LORA_BW_500;
      return true;
    default:
      return false;
  }
}

/**
 * @brief 将应用层扩频因子转换为 SX1262 LoRa 枚举
 * @param value 应用层扩频因子
 * @param spreading_factor SX1262 扩频因子输出地址
 * @return 扩频因子受支持返回 true
 */
bool SelectLoraSpreadingFactor(
    uint8_t value, sx126x_lora_sf_t* spreading_factor) {
  if (spreading_factor == nullptr) {
    return false;
  }
  switch (value) {
    case 5:
      *spreading_factor = SX126X_LORA_SF5;
      return true;
    case 6:
      *spreading_factor = SX126X_LORA_SF6;
      return true;
    case 7:
      *spreading_factor = SX126X_LORA_SF7;
      return true;
    case 8:
      *spreading_factor = SX126X_LORA_SF8;
      return true;
    case 9:
      *spreading_factor = SX126X_LORA_SF9;
      return true;
    case 10:
      *spreading_factor = SX126X_LORA_SF10;
      return true;
    case 11:
      *spreading_factor = SX126X_LORA_SF11;
      return true;
    case 12:
      *spreading_factor = SX126X_LORA_SF12;
      return true;
    default:
      return false;
  }
}

/**
 * @brief 将应用层编码率分母转换为 SX1262 LoRa 枚举
 * @param denominator 应用层编码率分母
 * @param coding_rate SX1262 编码率输出地址
 * @return 编码率受支持返回 true
 */
bool SelectLoraCodingRate(
    uint8_t denominator, sx126x_lora_cr_t* coding_rate) {
  if (coding_rate == nullptr) {
    return false;
  }
  switch (denominator) {
    case 5:
      *coding_rate = SX126X_LORA_CR_4_5;
      return true;
    case 6:
      *coding_rate = SX126X_LORA_CR_4_6;
      return true;
    case 7:
      *coding_rate = SX126X_LORA_CR_4_7;
      return true;
    case 8:
      *coding_rate = SX126X_LORA_CR_4_8;
      return true;
    default:
      return false;
  }
}

/**
 * @brief 根据中心频率选择 SX1262 镜像校准频率范围
 * @param frequency_hz 中心频率，单位为 Hz
 * @param minimum_mhz 校准范围下限输出，单位为 MHz
 * @param maximum_mhz 校准范围上限输出，单位为 MHz
 */
void SelectImageCalibration(uint32_t frequency_hz,
    uint16_t* minimum_mhz, uint16_t* maximum_mhz) {
  const uint32_t frequency_mhz = frequency_hz / 1000000;
  if (frequency_mhz >= 902) {
    *minimum_mhz = 902;
    *maximum_mhz = 928;
  } else if (frequency_mhz >= 863) {
    *minimum_mhz = 863;
    *maximum_mhz = 870;
  } else if (frequency_mhz >= 779) {
    *minimum_mhz = 779;
    *maximum_mhz = 787;
  } else if (frequency_mhz >= 470) {
    *minimum_mhz = 470;
    *maximum_mhz = 510;
  } else {
    *minimum_mhz = 430;
    *maximum_mhz = 440;
  }
}

/**
 * @brief 校验应用层 LoRa 参数并转换为 SX1262 驱动配置
 * @param source 应用层 LoRa 配置
 * @param target SX1262 驱动配置输出地址
 * @return 参数有效且转换成功时返回 true
 */
bool BuildSx1262Config(const LoraRadioConfig& source,
    usp_cpp_bus_driver::Sx126x::LoraConfig* target) {
  sx126x_lora_sf_t spreading_factor;
  sx126x_lora_bw_t bandwidth;
  sx126x_lora_cr_t coding_rate;
  if (target == nullptr || source.frequency_hz < 150000000 ||
      source.frequency_hz > 960000000 || source.preamble_length == 0 ||
      source.output_power_dbm < -9 || source.output_power_dbm > 22 ||
      !SelectLoraSpreadingFactor(
          source.spreading_factor, &spreading_factor) ||
      !SelectLoraBandwidth(source.bandwidth_hz, &bandwidth) ||
      !SelectLoraCodingRate(source.coding_rate_denominator, &coding_rate)) {
    return false;
  }
  target->frequency_hz = source.frequency_hz;
  target->spreading_factor = spreading_factor;
  target->bandwidth = bandwidth;
  target->coding_rate = coding_rate;
  target->preamble_length = source.preamble_length;
  target->sync_word = source.sync_word;
  target->output_power_dbm = source.output_power_dbm;
  target->crc_enabled = source.crc_enabled;
  target->invert_iq = source.invert_iq;
  target->rx_boosted = source.rx_boosted;
  SelectImageCalibration(source.frequency_hz,
      &target->image_calibration_min_mhz, &target->image_calibration_max_mhz);
  return true;
}

struct LoraTransmitTiming {
  // 根据当前调制参数计算的理论空中时间。
  uint32_t time_on_air_ms = 0;
  // 写入 SX1262 SetTx 命令的硬件超时，0 表示禁用硬件超时。
  uint32_t hardware_timeout_ms = 0;
  // MCU 等待 TX_DONE 或 TIMEOUT 事件的最长时间。
  uint32_t watchdog_timeout_ms = 0;
};

/**
 * @brief 根据 LoRa 符号时间判断是否启用低数据率优化
 * @param config 当前 LoRa 配置
 * @return 单个符号时间不小于 16 ms 时返回 true
 */
bool ShouldEnableLoraLdro(const LoraRadioConfig& config) {
  const uint64_t symbol_time_numerator = uint64_t{1} << config.spreading_factor;
  return symbol_time_numerator * 1000U >=
         static_cast<uint64_t>(config.bandwidth_hz) * 16U;
}

/**
 * @brief 计算指定 LoRa 数据包的空中时间和安全发送超时
 * @param config 当前 LoRa 配置
 * @param payload_size 待发送负载长度
 * @param timing 发送时间参数输出
 * @return 配置和负载有效且时间计算成功时返回 true
 */
bool CalculateLoraTransmitTiming(const LoraRadioConfig& config,
    size_t payload_size, LoraTransmitTiming* timing) {
  if (timing == nullptr || payload_size == 0 || payload_size > UINT8_MAX ||
      config.preamble_length == 0) {
    return false;
  }
  sx126x_lora_sf_t spreading_factor;
  sx126x_lora_bw_t bandwidth;
  sx126x_lora_cr_t coding_rate;
  if (!SelectLoraSpreadingFactor(
          config.spreading_factor, &spreading_factor) ||
      !SelectLoraBandwidth(config.bandwidth_hz, &bandwidth) ||
      !SelectLoraCodingRate(config.coding_rate_denominator, &coding_rate)) {
    return false;
  }
  const sx126x_mod_params_lora_t modulation_params = {
      .sf = spreading_factor,
      .bw = bandwidth,
      .cr = coding_rate,
      .ldro = static_cast<uint8_t>(ShouldEnableLoraLdro(config)),
  };
  const sx126x_pkt_params_lora_t packet_params = {
      .preamble_len_in_symb = config.preamble_length,
      .header_type = SX126X_LORA_PKT_EXPLICIT,
      .pld_len_in_bytes = static_cast<uint8_t>(payload_size),
      .crc_is_on = config.crc_enabled,
      .invert_iq_is_on = config.invert_iq,
  };
  const uint32_t time_on_air_ms =
      sx126x_get_lora_time_on_air_in_ms(&packet_params, &modulation_params);
  if (time_on_air_ms == 0) {
    return false;
  }
  const uint32_t margin_ms =
      std::max(kRadioTransmitTimeoutMarginMs, time_on_air_ms / 4);
  const uint64_t requested_timeout_ms =
      std::max<uint64_t>(kMinimumRadioTransmitTimeoutMs,
          static_cast<uint64_t>(time_on_air_ms) + margin_ms);
  *timing = LoraTransmitTiming{};
  timing->time_on_air_ms = time_on_air_ms;
  timing->hardware_timeout_ms =
      requested_timeout_ms <= SX126X_MAX_TIMEOUT_IN_MS
          ? static_cast<uint32_t>(requested_timeout_ms)
          : 0;
  timing->watchdog_timeout_ms = static_cast<uint32_t>(std::min<uint64_t>(
      requested_timeout_ms + kRadioTransmitWatchdogGraceMs, UINT32_MAX));
  return true;
}

/**
 * @brief 将公共 GFSK 参数转换为 CC1101 驱动配置
 * @param source 公共 GFSK 参数
 * @param target CC1101 驱动配置输出
 * @return 参数有效时返回 true
 */
bool BuildCc1101Config(const GfskRadioConfig& source,
    cpp_bus_driver::Cc1101::Config* target) {
  if (target == nullptr || source.frequency_hz == 0 ||
      source.data_rate_bps == 0 || source.frequency_deviation_hz == 0 ||
      source.receive_bandwidth_hz == 0 ||
      source.preamble_length_bits == 0) {
    return false;
  }
  *target = cpp_bus_driver::Cc1101::Config{};
  target->frequency_mhz = static_cast<double>(source.frequency_hz) / 1000000.0;
  target->data_rate_kbaud =
      static_cast<double>(source.data_rate_bps) / 1000.0;
  target->frequency_deviation_khz =
      static_cast<double>(source.frequency_deviation_hz) / 1000.0;
  target->receive_bandwidth_khz =
      static_cast<double>(source.receive_bandwidth_hz) / 1000.0;
  target->output_power_dbm = source.output_power_dbm;
  target->preamble_length_bits = source.preamble_length_bits;
  target->sync_word_high = static_cast<uint8_t>(source.sync_word >> 8);
  target->sync_word_low = static_cast<uint8_t>(source.sync_word);
  target->modulation = cpp_bus_driver::Cc1101::Modulation::kGfsk;
  target->encoding = source.whitening_enabled
      ? cpp_bus_driver::Cc1101::Encoding::kWhitening
      : cpp_bus_driver::Cc1101::Encoding::kNrz;
  target->maximum_packet_length = 60;
  target->packet_length_mode = source.fec_enabled
      ? cpp_bus_driver::Cc1101::PacketLengthMode::kFixed
      : cpp_bus_driver::Cc1101::PacketLengthMode::kVariable;
  target->crc_enabled = source.crc_enabled;
  target->crc_autoflush = source.crc_enabled;
  target->append_status = true;
  target->fec_enabled = source.fec_enabled;
  return true;
}

/**
 * @brief 根据中心频率选择键盘扩展板上的 CC1101 射频通路
 * @param frequency_hz 中心频率，单位为 Hz
 * @param rf_switch 射频开关输出
 * @return 频率属于板级支持频段时返回 true
 */
bool SelectCc1101RfSwitch(uint32_t frequency_hz,
    lilygo_device_driver::TDisplayP4Driver::Cc1101RfSwitch* rf_switch) {
  if (rf_switch == nullptr) {
    return false;
  }
  if (frequency_hz >= 300000000U && frequency_hz <= 348000000U) {
    *rf_switch = lilygo_device_driver::TDisplayP4Driver::
        Cc1101RfSwitch::k315Mhz;
    return true;
  }
  if (frequency_hz >= 387000000U && frequency_hz <= 464000000U) {
    *rf_switch = lilygo_device_driver::TDisplayP4Driver::
        Cc1101RfSwitch::k434Mhz;
    return true;
  }
  if (frequency_hz >= 779000000U && frequency_hz <= 928000000U) {
    *rf_switch = lilygo_device_driver::TDisplayP4Driver::
        Cc1101RfSwitch::k868_915Mhz;
    return true;
  }
  return false;
}

/**
 * @brief 将空中数据速率转换为 nRF24L01 驱动枚举
 * @param data_rate_bps 空中数据速率，单位为 bit/s
 * @param data_rate 驱动数据速率输出
 * @return 速率受芯片支持时返回 true
 */
bool SelectNrf24l01DataRate(uint32_t data_rate_bps,
    cpp_bus_driver::Nrf24l01x::DataRate* data_rate) {
  if (data_rate == nullptr) {
    return false;
  }
  switch (data_rate_bps) {
    case 250000:
      *data_rate = cpp_bus_driver::Nrf24l01x::DataRate::k250Kbps;
      return true;
    case 1000000:
      *data_rate = cpp_bus_driver::Nrf24l01x::DataRate::k1Mbps;
      return true;
    case 2000000:
      *data_rate = cpp_bus_driver::Nrf24l01x::DataRate::k2Mbps;
      return true;
    default:
      return false;
  }
}

/**
 * @brief 将发射功率转换为 nRF24L01 驱动枚举
 * @param output_power_dbm 发射功率，单位为 dBm
 * @param output_power 驱动发射功率输出
 * @return 功率受芯片支持时返回 true
 */
bool SelectNrf24l01OutputPower(int8_t output_power_dbm,
    cpp_bus_driver::Nrf24l01x::OutputPower* output_power) {
  if (output_power == nullptr) {
    return false;
  }
  switch (output_power_dbm) {
    case -18:
      *output_power = cpp_bus_driver::Nrf24l01x::OutputPower::kMinus18Dbm;
      return true;
    case -12:
      *output_power = cpp_bus_driver::Nrf24l01x::OutputPower::kMinus12Dbm;
      return true;
    case -6:
      *output_power = cpp_bus_driver::Nrf24l01x::OutputPower::kMinus6Dbm;
      return true;
    case 0:
      *output_power = cpp_bus_driver::Nrf24l01x::OutputPower::kZeroDbm;
      return true;
    default:
      return false;
  }
}

/**
 * @brief 获取 nRF24L01 发射结果的日志说明
 * @param result nRF24L01 发射结果
 * @return 静态结果说明
 */
const char* Nrf24l01TransmitResultName(
    cpp_bus_driver::Nrf24l01x::TransmitResult result) {
  using TransmitResult = cpp_bus_driver::Nrf24l01x::TransmitResult;
  switch (result) {
    case TransmitResult::kSuccess:
      return "success";
    case TransmitResult::kMaximumRetransmit:
      return "maximum retransmit";
    case TransmitResult::kTimeout:
      return "timeout";
    case TransmitResult::kInvalidArgument:
      return "invalid argument";
    case TransmitResult::kBusError:
      return "bus or GPIO error";
  }
  return "unknown";
}

/**
 * @brief 将公共 Enhanced ShockBurst 参数转换为 nRF24L01 驱动配置
 * @param source 公共 Enhanced ShockBurst 参数
 * @param target nRF24L01 驱动配置输出
 * @return 参数有效时返回 true
 */
bool BuildNrf24l01Config(const EnhancedShockBurstRadioConfig& source,
    cpp_bus_driver::Nrf24l01x::Config* target) {
  cpp_bus_driver::Nrf24l01x::DataRate data_rate;
  cpp_bus_driver::Nrf24l01x::OutputPower output_power;
  if (target == nullptr || source.channel > 125 ||
      source.address_width < 3 || source.address_width > 5 ||
      (source.crc_length_bits != 8 && source.crc_length_bits != 16) ||
      (source.dynamic_payload_enabled && !source.auto_ack_enabled) ||
      source.retransmit_count > 15 || source.retransmit_delay_us < 250 ||
      source.retransmit_delay_us > 4000 ||
      source.retransmit_delay_us % 250 != 0 ||
      !SelectNrf24l01DataRate(source.data_rate_bps, &data_rate) ||
      !SelectNrf24l01OutputPower(source.output_power_dbm, &output_power)) {
    return false;
  }
  *target = cpp_bus_driver::Nrf24l01x::Config{};
  target->operation_mode =
      cpp_bus_driver::Nrf24l01x::OperationMode::kPrimaryReceiver;
  target->power_mode = cpp_bus_driver::Nrf24l01x::PowerMode::kPowerUp;
  target->crc_mode = source.crc_length_bits == 16
      ? cpp_bus_driver::Nrf24l01x::CrcMode::k16Bit
      : cpp_bus_driver::Nrf24l01x::CrcMode::k8Bit;
  target->output_power = output_power;
  target->data_rate = data_rate;
  target->address_width = static_cast<cpp_bus_driver::Nrf24l01x::AddressWidth>(
      source.address_width);
  target->rf_channel = source.channel;
  target->retransmit_count = source.retransmit_count;
  target->retransmit_delay_us = source.retransmit_delay_us;
  target->enabled_pipe_mask = 0x01;
  target->auto_ack_pipe_mask = source.auto_ack_enabled ? 0x01 : 0;
  target->dynamic_payload_enabled = source.dynamic_payload_enabled;
  target->dynamic_payload_pipe_mask =
      source.dynamic_payload_enabled && source.auto_ack_enabled ? 0x01 : 0;
  target->rx_payload_width[0] = source.dynamic_payload_enabled ? 0 : 32;
  return true;
}

/**
 * @brief 按 nRF24L01 寄存器写入顺序编码五字节地址
 * @param address 数值形式的空中地址
 * @param output 五字节地址输出
 */
void EncodeNrf24l01Address(uint64_t address, uint8_t* output) {
  for (size_t index = 0; index < 5; ++index) {
    output[index] = static_cast<uint8_t>(address >> (index * 8));
  }
}

struct Lr2021LfPaTableEntry {
  int8_t half_power;
  uint8_t pa_duty_cycle;
  uint8_t pa_lf_slices;
};

struct Lr2021HfPaTableEntry {
  int8_t half_power;
  uint8_t pa_hf_duty_cycle;
};

constexpr Lr2021LfPaTableEntry kLr2021Pa915MhzTable[] = {
    {44, 7, 6}, {42, 7, 7}, {41, 6, 6}, {39, 6, 6}, {38, 5, 6},
    {36, 5, 6}, {36, 4, 4}, {33, 5, 4}, {34, 4, 2}, {31, 4, 3},
    {30, 5, 1}, {32, 2, 2}, {32, 2, 1},
};

constexpr Lr2021LfPaTableEntry kLr2021Pa490MhzTable[] = {
    {40, 7, 7}, {38, 7, 7}, {36, 7, 6}, {34, 7, 6}, {32, 7, 6},
    {31, 7, 4}, {31, 6, 4}, {29, 7, 2}, {30, 5, 3}, {29, 5, 2},
    {31, 4, 2},
};

constexpr Lr2021HfPaTableEntry kLr2021Pa2445MhzTable[] = {
    {24, 16}, {24, 26}, {24, 30}, {22, 30}, {21, 31},
    {18, 30}, {16, 30}, {15, 31}, {10, 25}, {8, 25},
    {7, 28}, {6, 30}, {4, 30},
};

static_assert(std::size(kLr2021Pa915MhzTable) == 13);
static_assert(std::size(kLr2021Pa490MhzTable) == 11);
static_assert(std::size(kLr2021Pa2445MhzTable) == 13);

/**
 * @brief 将应用层 LoRa 带宽转换为 LR2021 枚举
 * @param bandwidth_hz 带宽，单位为 Hz
 * @param bandwidth LR2021 带宽枚举输出地址
 * @return 带宽受支持返回 true
 */
bool SelectLr2021Bandwidth(
    uint32_t bandwidth_hz, lr20xx_radio_lora_bw_t* bandwidth) {
  if (bandwidth == nullptr) {
    return false;
  }
  switch (bandwidth_hz) {
    case 31250:
      *bandwidth = LR20XX_RADIO_LORA_BW_31;
      return true;
    case 41670:
      *bandwidth = LR20XX_RADIO_LORA_BW_41;
      return true;
    case 62500:
      *bandwidth = LR20XX_RADIO_LORA_BW_62;
      return true;
    case 83340:
      *bandwidth = LR20XX_RADIO_LORA_BW_83;
      return true;
    case 101563:
      *bandwidth = LR20XX_RADIO_LORA_BW_101;
      return true;
    case 125000:
      *bandwidth = LR20XX_RADIO_LORA_BW_125;
      return true;
    case 203000:
      *bandwidth = LR20XX_RADIO_LORA_BW_203;
      return true;
    case 250000:
      *bandwidth = LR20XX_RADIO_LORA_BW_250;
      return true;
    case 406000:
      *bandwidth = LR20XX_RADIO_LORA_BW_406;
      return true;
    case 500000:
      *bandwidth = LR20XX_RADIO_LORA_BW_500;
      return true;
    case 812000:
      *bandwidth = LR20XX_RADIO_LORA_BW_812;
      return true;
    case 1000000:
      *bandwidth = LR20XX_RADIO_LORA_BW_1000;
      return true;
    default:
      return false;
  }
}

/**
 * @brief 根据频率和目标功率选择 LR2021 PA 参数
 * @param source 应用层 LoRa 配置
 * @param pa LR2021 PA 配置输出地址
 * @param output_power_half_dbm 半 dBm 单位的发射功率输出地址
 * @return 频率和功率受支持返回 true
 */
bool SelectLr2021Power(const LoraRadioConfig& source,
    lr20xx_radio_common_pa_cfg_t* pa, int8_t* output_power_half_dbm) {
  if (pa == nullptr || output_power_half_dbm == nullptr) {
    return false;
  }
  const bool high_frequency = source.frequency_hz >= 1600000000U;
  if (high_frequency) {
    if (source.output_power_dbm < -19 || source.output_power_dbm > 5) {
      return false;
    }
    const int8_t table_power = std::max<int8_t>(source.output_power_dbm, 0);
    const Lr2021HfPaTableEntry& power =
        kLr2021Pa2445MhzTable[12 - table_power];
    *pa = {
        .pa_sel = LR20XX_RADIO_COMMON_PA_SEL_HF,
        .pa_lf_mode = LR20XX_RADIO_COMMON_PA_LF_MODE_FSM,
        .pa_lf_duty_cycle = 7,
        .pa_lf_slices = 6,
        .pa_hf_duty_cycle = power.pa_hf_duty_cycle,
    };
    *output_power_half_dbm = source.output_power_dbm < 0
        ? static_cast<int8_t>(source.output_power_dbm * 2)
        : power.half_power;
    return true;
  }
  if (source.output_power_dbm < -9 || source.output_power_dbm > 22) {
    return false;
  }
  const bool low_band = source.frequency_hz < 700000000U;
  const int8_t minimum_table_power = 10;
  const int8_t maximum_table_power = low_band ? 20 : 22;
  const int8_t table_power = std::clamp<int8_t>(
      source.output_power_dbm, minimum_table_power, maximum_table_power);
  const Lr2021LfPaTableEntry& power = low_band
      ? kLr2021Pa490MhzTable[20 - table_power]
      : kLr2021Pa915MhzTable[22 - table_power];
  *pa = {
      .pa_sel = LR20XX_RADIO_COMMON_PA_SEL_LF,
      .pa_lf_mode = LR20XX_RADIO_COMMON_PA_LF_MODE_FSM,
      .pa_lf_duty_cycle = power.pa_duty_cycle,
      .pa_lf_slices = power.pa_lf_slices,
      .pa_hf_duty_cycle = 16,
  };
  *output_power_half_dbm =
      source.output_power_dbm < minimum_table_power ||
          source.output_power_dbm > maximum_table_power
      ? static_cast<int8_t>(source.output_power_dbm * 2)
      : power.half_power;
  return true;
}

/**
 * @brief 创建 LR2021 LoRa 数据包参数
 * @param source 应用层 LoRa 配置
 * @param payload_size 数据包负载长度
 * @return LR2021 LoRa 数据包参数
 */
lr20xx_radio_lora_pkt_params_t MakeLr2021PacketConfig(
    const LoraRadioConfig& source, uint8_t payload_size) {
  return {
      .preamble_len_in_symb = source.preamble_length,
      .pkt_mode = LR20XX_RADIO_LORA_PKT_EXPLICIT,
      .pld_len_in_bytes = payload_size,
      .crc = source.crc_enabled ? LR20XX_RADIO_LORA_CRC_ENABLED
                                : LR20XX_RADIO_LORA_CRC_DISABLED,
      .iq = source.invert_iq ? LR20XX_RADIO_LORA_IQ_INVERTED
                             : LR20XX_RADIO_LORA_IQ_STANDARD,
  };
}

/**
 * @brief 校验应用层 LoRa 参数并转换为 LR2021 驱动配置
 * @param source 应用层 LoRa 配置
 * @param payload_size 数据包负载长度
 * @param target LR2021 驱动配置输出地址
 * @return 参数有效且转换成功返回 true
 */
bool BuildLr2021Config(const LoraRadioConfig& source, uint8_t payload_size,
    usp_cpp_bus_driver::Lr20xx::LoraConfig* target) {
  if (target == nullptr ||
      !radio::IsLr2021BandwidthSupported(
          source.frequency_hz, source.bandwidth_hz) ||
      source.preamble_length == 0 || source.lr2021_rx_boost_mode > 7) {
    return false;
  }
  lr20xx_radio_lora_bw_t bandwidth;
  lr20xx_radio_common_pa_cfg_t pa = {};
  int8_t output_power_half_dbm = 0;
  const uint8_t coding_rate =
      static_cast<uint8_t>(source.lr2021_coding_rate);
  if (!SelectLr2021Bandwidth(source.bandwidth_hz, &bandwidth) ||
      !SelectLr2021Power(source, &pa, &output_power_half_dbm) ||
      source.spreading_factor < 5 || source.spreading_factor > 12 ||
      !radio::IsLr2021CodingRate(source.lr2021_coding_rate)) {
    return false;
  }
  *target = usp_cpp_bus_driver::Lr20xx::LoraConfig{};
  target->frequency_hz = source.frequency_hz;
  target->modulation.sf = static_cast<lr20xx_radio_lora_sf_t>(
      source.spreading_factor);
  target->modulation.bw = bandwidth;
  target->modulation.cr =
      static_cast<lr20xx_radio_lora_cr_t>(coding_rate);
  target->modulation.ppm = ShouldEnableLoraLdro(source)
      ? LR20XX_RADIO_LORA_PPM_1_4
      : LR20XX_RADIO_LORA_NO_PPM;
  target->packet = MakeLr2021PacketConfig(source, payload_size);
  target->sync_word = source.sync_word;
  target->rx_path = source.frequency_hz >= 1600000000U
      ? LR20XX_RADIO_COMMON_RX_PATH_HF
      : LR20XX_RADIO_COMMON_RX_PATH_LF;
  target->rx_boost_mode =
      static_cast<lr20xx_radio_common_rx_path_boost_mode_t>(
          source.lr2021_rx_boost_mode);
  target->pa = pa;
  target->output_power_half_dbm = output_power_half_dbm;
  target->ramp_time = LR20XX_RADIO_COMMON_RAMP_48_US;
  return true;
}

constexpr lr20xx_system_irq_mask_t kLr2021RadioIrqMask =
    LR20XX_SYSTEM_IRQ_TX_DONE | LR20XX_SYSTEM_IRQ_RX_DONE |
    LR20XX_SYSTEM_IRQ_TIMEOUT | LR20XX_SYSTEM_IRQ_CRC_ERROR |
    LR20XX_SYSTEM_IRQ_LEN_ERROR | LR20XX_SYSTEM_IRQ_LORA_HEADER_ERROR;

/**
 * @brief 清空 LR2021 RX FIFO 并启动单包接收
 * @param radio LR2021 驱动实例
 * @param config 应用层 LoRa 配置
 * @return 接收启动成功返回 true
 */
bool StartLr2021Receive(usp_cpp_bus_driver::Lr20xx* radio,
    const LoraRadioConfig& config) {
  const lr20xx_radio_lora_pkt_params_t packet_config =
      MakeLr2021PacketConfig(config, UINT8_MAX);
  return radio != nullptr &&
      radio->Invoke(lr20xx_radio_fifo_clear_rx) == LR20XX_STATUS_OK &&
      radio->Invoke(lr20xx_radio_lora_set_packet_params, &packet_config) ==
          LR20XX_STATUS_OK &&
      radio->StartReceive(0);
}

/**
 * @brief 完整配置 LR2021 LoRa 接收参数和 DIO11 IRQ 后启动接收
 * @param radio LR2021 驱动实例
 * @param config 应用层 LoRa 配置
 * @return 配置和接收启动成功返回 true
 */
bool ConfigureLr2021Receive(usp_cpp_bus_driver::Lr20xx* radio,
    const LoraRadioConfig& config) {
  usp_cpp_bus_driver::Lr20xx::LoraConfig driver_config;
  return radio != nullptr &&
      BuildLr2021Config(config, UINT8_MAX, &driver_config) &&
      radio->Configure(driver_config) &&
      radio->Invoke(lr20xx_system_clear_irq_status,
          LR20XX_SYSTEM_IRQ_ALL_MASK) == LR20XX_STATUS_OK &&
      radio->Invoke(lr20xx_system_set_dio_irq_cfg, LR20XX_SYSTEM_DIO_11,
          kLr2021RadioIrqMask) == LR20XX_STATUS_OK &&
      StartLr2021Receive(radio, config);
}

}  // namespace

bool TDisplayP4Device::ReadRadioCapabilities(RadioCapabilities* capabilities) {
  if (capabilities == nullptr) {
    return false;
  }
  *capabilities = RadioCapabilities();
  radio::ChipType primary_chip = radio::ChipType::kUnknown;
  switch (driver_.radio_type()) {
    case device::RadioType::kSx1262:
      primary_chip = radio::ChipType::kSx1262;
      break;
    case device::RadioType::kLr2021:
      primary_chip = radio::ChipType::kLr2021;
      break;
    case device::RadioType::kUnknown:
      break;
  }
  if (primary_chip != radio::ChipType::kUnknown) {
    RadioCapability& capability =
        capabilities->entries[capabilities->count++];
    capability.chip = primary_chip;
    capability.protocol = radio::ProtocolType::kLora;
    capability.maximum_payload_size = kRadioPayloadCapacity;
    capability.frequency_bands[0] = {
        .minimum_hz = 150000000U,
        .maximum_hz = 960000000U,
    };
    capability.frequency_band_count = 1;
    if (primary_chip == radio::ChipType::kLr2021) {
      capability.frequency_bands[1] = {
          .minimum_hz = 2400000000U,
          .maximum_hz = 2500000000U,
      };
      capability.frequency_band_count = 2;
    }
  }
  if (keyboard_expansion_.state.load() == KeyboardExpansionState::kReady &&
      driver_.IsCc1101Ready()) {
    RadioCapability& cc1101 = capabilities->entries[capabilities->count++];
    cc1101.chip = radio::ChipType::kCc1101;
    cc1101.protocol = radio::ProtocolType::kGfsk;
    cc1101.maximum_payload_size = 60;
    cc1101.frequency_bands[0] = {
        .minimum_hz = 300000000U,
        .maximum_hz = 348000000U,
    };
    cc1101.frequency_bands[1] = {
        .minimum_hz = 387000000U,
        .maximum_hz = 464000000U,
    };
    cc1101.frequency_bands[2] = {
        .minimum_hz = 779000000U,
        .maximum_hz = 928000000U,
    };
    cc1101.frequency_band_count = 3;
  }
  if (keyboard_expansion_.state.load() == KeyboardExpansionState::kReady &&
      driver_.IsNrf24l01Ready()) {
    RadioCapability& nrf24l01 =
        capabilities->entries[capabilities->count++];
    nrf24l01.chip = radio::ChipType::kNrf24l01;
    nrf24l01.protocol = radio::ProtocolType::kEnhancedShockBurst;
    nrf24l01.maximum_payload_size =
        cpp_bus_driver::Nrf24l01x::kMaximumPayloadLength;
    nrf24l01.frequency_bands[0] = {
        .minimum_hz = 2400000000U,
        .maximum_hz = 2525000000U,
    };
    nrf24l01.frequency_band_count = 1;
  }
  capabilities->supports_external_antenna = true;
  return true;
}

bool TDisplayP4Device::InitializeCc1101ReceiveInterrupt() {
  if (cc1101_radio_.receive_interrupt_initialized) {
    return true;
  }
  if (tool_ == nullptr || !driver_.IsCc1101Ready()) {
    return false;
  }

  cc1101_radio_.receive_interrupt_pending.store(
      false, std::memory_order_relaxed);
  if (!tool_->InitGpioInterrupt(keyboard_gpio::t_mix_rf::cc1101::kGdo0,
          cpp_bus_driver::Tool::InterruptMode::kFalling,
          Cc1101ReceiveInterruptHandler, this,
          cpp_bus_driver::Tool::GpioStatus::kDisable)) {
    return false;
  }
  cc1101_radio_.receive_interrupt_initialized = true;
  return true;
}

bool TDisplayP4Device::DeinitializeCc1101ReceiveInterrupt() {
  cc1101_radio_.receive_interrupt_pending.store(
      false, std::memory_order_relaxed);
  if (!cc1101_radio_.receive_interrupt_initialized) {
    return true;
  }

  const bool result = tool_ != nullptr && tool_->DeinitGpioInterrupt(
      keyboard_gpio::t_mix_rf::cc1101::kGdo0);
  cc1101_radio_.receive_interrupt_initialized = false;
  cc1101_radio_.receive_interrupt_pending.store(
      false, std::memory_order_relaxed);
  return result;
}

void TDisplayP4Device::Cc1101ReceiveInterruptHandler(void* context) {
  if (context == nullptr) {
    return;
  }
  auto* device = static_cast<TDisplayP4Device*>(context);
  device->cc1101_radio_.receive_interrupt_pending.store(
      true, std::memory_order_release);
}

TDisplayP4Device::RadioState* TDisplayP4Device::RadioStateForChip(
    radio::ChipType chip) {
  switch (chip) {
    case radio::ChipType::kSx1262:
    case radio::ChipType::kLr2021:
      return &radio_;
    case radio::ChipType::kCc1101:
      return &cc1101_radio_;
    case radio::ChipType::kNrf24l01:
      return &nrf24l01_radio_;
    default:
      return nullptr;
  }
}

TDisplayP4Device::RadioState* TDisplayP4Device::FindRadioState(
    uint32_t client_token) {
  if (client_token == 0) {
    return nullptr;
  }
  RadioState* states[] = {&radio_, &cc1101_radio_, &nrf24l01_radio_};
  for (RadioState* state : states) {
    if (state->active_client_token == client_token) {
      return state;
    }
  }
  return nullptr;
}

bool TDisplayP4Device::ActivateRadio(const RadioConfig& config) {
  RadioState* state = RadioStateForChip(config.chip);
  const bool primary_chip_matches =
      (config.chip == radio::ChipType::kSx1262 &&
           driver_.radio_type() ==
               device::RadioType::kSx1262) ||
          (config.chip == radio::ChipType::kLr2021 &&
              driver_.radio_type() ==
                  device::RadioType::kLr2021);
  const bool expansion_chip_matches =
      config.chip == radio::ChipType::kCc1101 ||
      config.chip == radio::ChipType::kNrf24l01;
  if (state == nullptr || (!primary_chip_matches && !expansion_chip_matches)) {
    return false;
  }
  if (state->mutex == nullptr ||
      xSemaphoreTake(state->mutex, pdMS_TO_TICKS(50)) != pdTRUE) {
    LogMessage(LogLevel::kError, __FILE__, __LINE__,
        "Radio activate failed: mutex unavailable, profile=%lu\n",
        static_cast<unsigned long>(config.client_token));
    return false;
  }
  bool previous_session_stopped = true;
  if (state->active && state->chip == radio::ChipType::kCc1101) {
    previous_session_stopped &= DeinitializeCc1101ReceiveInterrupt();
  }
  if (state->active) {
    if (state->chip == radio::ChipType::kSx1262 &&
        driver_.IsSx1262Ready()) {
      auto* radio = driver_.chip().sx1262.get();
      previous_session_stopped = radio != nullptr &&
          radio->Invoke(sx126x_set_standby, SX126X_STANDBY_CFG_RC) ==
              SX126X_STATUS_OK;
    } else if (state->chip == radio::ChipType::kLr2021 &&
               driver_.IsLr2021Ready()) {
      auto* radio = driver_.chip().lr2021.get();
      previous_session_stopped = radio != nullptr &&
          radio->Invoke(lr20xx_system_set_dio_irq_cfg,
              LR20XX_SYSTEM_DIO_11, LR20XX_SYSTEM_IRQ_NONE) ==
              LR20XX_STATUS_OK &&
          driver_.SetLr2021OperatingMode(
              lilygo_device_driver::TDisplayP4Driver::
                  Lr2021OperatingMode::kStandby);
    } else if (state->chip == radio::ChipType::kCc1101 &&
               driver_.IsCc1101Ready()) {
      auto* radio = driver_.chip().cc1101.get();
      previous_session_stopped &= radio != nullptr && radio->Standby() &&
          driver_.SetCc1101OperatingMode(
              lilygo_device_driver::TDisplayP4Driver::
                  Cc1101OperatingMode::kSleep);
    } else if (state->chip == radio::ChipType::kNrf24l01 &&
               driver_.IsNrf24l01Ready()) {
      auto* radio = driver_.chip().nrf24l01.get();
      previous_session_stopped = radio != nullptr && radio->StopReceive() &&
          driver_.SetNrf24l01OperatingMode(
              lilygo_device_driver::TDisplayP4Driver::
                  Nrf24l01OperatingMode::kSleep);
    }
  }
  if (!previous_session_stopped) {
    state->active = false;
    state->transmitting = false;
    state->chip_error = true;
    xSemaphoreGive(state->mutex);
    LogMessage(LogLevel::kError, __FILE__, __LINE__,
        "Radio activate failed: previous session could not stop\n");
    return false;
  }
  bool result = false;
  if (config.chip == radio::ChipType::kSx1262 &&
      config.protocol == radio::ProtocolType::kLora) {
    usp_cpp_bus_driver::Sx126x::LoraConfig driver_config;
    const bool antenna_supported =
        config.antenna == radio::AntennaType::kInternal ||
        config.antenna == radio::AntennaType::kExternal;
    result = antenna_supported && BuildSx1262Config(
        config.lora, &driver_config);
    if (result) {
      auto* antenna_switch = driver_.chip().xl9535.get();
      const uint8_t antenna_level =
          config.antenna == radio::AntennaType::kExternal ? 0 : 1;
      result = driver_.IsXl9535Ready() && antenna_switch != nullptr &&
               antenna_switch->GpioWrite(
                   gpio::xl9535::kSky13453Vctl, antenna_level);
    }
    if (result) {
      result = driver_.SetSx1262OperatingMode(
          lilygo_device_driver::TDisplayP4Driver::
              Sx1262OperatingMode::kStandby);
    }
    if (result) {
      auto* radio = driver_.chip().sx1262.get();
      result = radio != nullptr && radio->Configure(driver_config) &&
               radio->StartReceive();
    }
    if (!result) {
      driver_.SetSx1262OperatingMode(
          lilygo_device_driver::TDisplayP4Driver::
              Sx1262OperatingMode::kSleep);
    }
  } else if (config.chip == radio::ChipType::kLr2021 &&
             config.protocol == radio::ProtocolType::kLora) {
    const bool antenna_supported =
        config.antenna == radio::AntennaType::kInternal ||
        config.antenna == radio::AntennaType::kExternal;
    result = antenna_supported && driver_.IsLr2021Ready();
    if (result) {
      auto* antenna_switch = driver_.chip().xl9535.get();
      const uint8_t antenna_level =
          config.antenna == radio::AntennaType::kExternal ? 0 : 1;
      result = driver_.IsXl9535Ready() && antenna_switch != nullptr &&
          antenna_switch->GpioWrite(
              gpio::xl9535::kSky13453Vctl, antenna_level);
    }
    if (result) {
      result = driver_.SetLr2021OperatingMode(
          lilygo_device_driver::TDisplayP4Driver::
              Lr2021OperatingMode::kStandby);
    }
    if (result) {
      result = ConfigureLr2021Receive(
          driver_.chip().lr2021.get(), config.lora);
    }
    if (!result) {
      driver_.SetLr2021OperatingMode(
          lilygo_device_driver::TDisplayP4Driver::
              Lr2021OperatingMode::kSleep);
    }
  } else if (config.chip == radio::ChipType::kCc1101 &&
             config.protocol == radio::ProtocolType::kGfsk &&
             config.antenna == radio::AntennaType::kInternal &&
             keyboard_expansion_.state.load() ==
                 KeyboardExpansionState::kReady) {
    cpp_bus_driver::Cc1101::Config driver_config;
    lilygo_device_driver::TDisplayP4Driver::Cc1101RfSwitch rf_switch;
    result = driver_.IsCc1101Ready() &&
             BuildCc1101Config(config.gfsk, &driver_config) &&
             SelectCc1101RfSwitch(config.gfsk.frequency_hz, &rf_switch) &&
             driver_.SetCc1101RfSwitch(rf_switch) &&
             driver_.SetCc1101OperatingMode(
                 lilygo_device_driver::TDisplayP4Driver::
                     Cc1101OperatingMode::kStandby);
    if (result) {
      auto* radio = driver_.chip().cc1101.get();
      result = radio != nullptr && radio->Configure(driver_config);
      if (result) {
        result = InitializeCc1101ReceiveInterrupt();
      }
      if (result) {
        cc1101_radio_.receive_interrupt_pending.store(
            false, std::memory_order_relaxed);
        result = radio->StartReceive();
      }
    }
    if (!result) {
      DeinitializeCc1101ReceiveInterrupt();
      driver_.SetCc1101OperatingMode(
          lilygo_device_driver::TDisplayP4Driver::
              Cc1101OperatingMode::kSleep);
    }
  } else if (config.chip == radio::ChipType::kNrf24l01 &&
             config.protocol ==
                 radio::ProtocolType::kEnhancedShockBurst &&
             config.antenna == radio::AntennaType::kInternal &&
             keyboard_expansion_.state.load() ==
                 KeyboardExpansionState::kReady) {
    cpp_bus_driver::Nrf24l01x::Config driver_config;
    result = driver_.IsNrf24l01Ready() && BuildNrf24l01Config(
        config.enhanced_shock_burst, &driver_config) &&
        driver_.SetNrf24l01OperatingMode(
            lilygo_device_driver::TDisplayP4Driver::
                Nrf24l01OperatingMode::kStandby);
    if (result) {
      uint8_t address[5] = {};
      EncodeNrf24l01Address(
          config.enhanced_shock_burst.address, address);
      auto* radio = driver_.chip().nrf24l01.get();
      const size_t address_width =
          config.enhanced_shock_burst.address_width;
      result = radio != nullptr && radio->Configure(driver_config) &&
               radio->SetAddress(cpp_bus_driver::Nrf24l01x::Address::kPipe0,
                   address, address_width) &&
               radio->SetAddress(
                   cpp_bus_driver::Nrf24l01x::Address::kTransmit,
                   address, address_width) &&
               radio->StartReceive();
    }
    if (!result) {
      driver_.SetNrf24l01OperatingMode(
          lilygo_device_driver::TDisplayP4Driver::
              Nrf24l01OperatingMode::kSleep);
    }
  }
  state->active = result;
  state->transmitting = false;
  state->chip_error = !result;
  state->active_client_token = config.client_token;
  state->transmit_request_token = 0;
  state->transmit_deadline_us = 0;
  state->lora_config = config.lora;
  state->gfsk_config = config.gfsk;
  state->enhanced_shock_burst_config = config.enhanced_shock_burst;
  state->chip = config.chip;
  state->protocol = config.protocol;
  state->pending_event = RadioEvent();
  xSemaphoreGive(state->mutex);
  LogMessage(result ? LogLevel::kDebug : LogLevel::kError, __FILE__, __LINE__,
      "Radio activate %s: profile=%lu, chip=%u, protocol=%u\n",
      result ? "succeeded" : "failed",
      static_cast<unsigned long>(config.client_token),
      static_cast<unsigned>(config.chip),
      static_cast<unsigned>(config.protocol));
  return result;
}

bool TDisplayP4Device::DeactivateRadio() {
  bool result = true;
  RadioState* states[] = {&radio_, &cc1101_radio_, &nrf24l01_radio_};
  for (RadioState* state : states) {
    if (state->active || state->active_client_token != 0) {
      result &= DeactivateRadioState(state);
    }
  }
  return result;
}

bool TDisplayP4Device::DeactivateRadio(uint32_t client_token) {
  if (client_token == 0) {
    return DeactivateRadio();
  }
  RadioState* state = FindRadioState(client_token);
  return state != nullptr && DeactivateRadioState(state);
}

bool TDisplayP4Device::DeactivateRadioState(RadioState* state) {
  if (state->mutex == nullptr ||
      xSemaphoreTake(state->mutex, pdMS_TO_TICKS(2000)) != pdTRUE) {
    LogMessage(LogLevel::kError, __FILE__, __LINE__,
        "Radio deactivate failed: mutex unavailable\n");
    return false;
  }
  bool result = true;
  if (state->chip == radio::ChipType::kCc1101) {
    result &= DeinitializeCc1101ReceiveInterrupt();
  }
  if (state->chip == radio::ChipType::kSx1262 && driver_.IsSx1262Ready()) {
    auto* radio = driver_.chip().sx1262.get();
    if (state->active) {
      result = radio != nullptr &&
               radio->Invoke(sx126x_set_standby, SX126X_STANDBY_CFG_RC) ==
                   SX126X_STATUS_OK &&
               radio->ClearIrqStatus(SX126X_IRQ_ALL);
    }
    result &= driver_.SetSx1262OperatingMode(
        lilygo_device_driver::TDisplayP4Driver::
            Sx1262OperatingMode::kStandby);
  } else if (state->chip == radio::ChipType::kLr2021 &&
             driver_.IsLr2021Ready()) {
    auto* radio = driver_.chip().lr2021.get();
    if (state->active) {
      result = radio != nullptr &&
          radio->Invoke(lr20xx_system_set_dio_irq_cfg,
              LR20XX_SYSTEM_DIO_11, LR20XX_SYSTEM_IRQ_NONE) ==
              LR20XX_STATUS_OK &&
          radio->Invoke(lr20xx_system_clear_irq_status,
              LR20XX_SYSTEM_IRQ_ALL_MASK) == LR20XX_STATUS_OK;
    }
    result &= driver_.SetLr2021OperatingMode(
        lilygo_device_driver::TDisplayP4Driver::
            Lr2021OperatingMode::kStandby);
  } else if (state->chip == radio::ChipType::kCc1101 &&
             keyboard_expansion_.state.load() ==
                 KeyboardExpansionState::kReady &&
             driver_.IsCc1101Ready()) {
    auto* radio = driver_.chip().cc1101.get();
    result &= radio != nullptr && radio->Standby() && radio->FlushRx() &&
              radio->FlushTx();
    result &= driver_.SetCc1101OperatingMode(
        lilygo_device_driver::TDisplayP4Driver::
            Cc1101OperatingMode::kSleep);
  } else if (state->chip == radio::ChipType::kNrf24l01 &&
             keyboard_expansion_.state.load() ==
                 KeyboardExpansionState::kReady &&
             driver_.IsNrf24l01Ready()) {
    auto* radio = driver_.chip().nrf24l01.get();
    result = radio != nullptr && radio->StopReceive() && radio->FlushRx() &&
             radio->FlushTx();
    result &= driver_.SetNrf24l01OperatingMode(
        lilygo_device_driver::TDisplayP4Driver::
            Nrf24l01OperatingMode::kSleep);
  }
  state->active = false;
  state->transmitting = false;
  state->chip_error = !result;
  state->active_client_token = 0;
  state->transmit_request_token = 0;
  state->transmit_deadline_us = 0;
  state->chip = radio::ChipType::kUnknown;
  state->protocol = radio::ProtocolType::kUnknown;
  state->pending_event = RadioEvent();
  xSemaphoreGive(state->mutex);
  LogMessage(result ? LogLevel::kInfo : LogLevel::kError, __FILE__, __LINE__,
      "Radio deactivate %s\n", result ? "succeeded" : "failed");
  return result;
}

bool TDisplayP4Device::SendRadio(
    const uint8_t* data, size_t size, uint64_t request_token) {
  RadioState* selected = nullptr;
  RadioState* states[] = {&radio_, &cc1101_radio_, &nrf24l01_radio_};
  for (RadioState* state : states) {
    if (!state->active) {
      continue;
    }
    if (selected != nullptr) {
      return false;
    }
    selected = state;
  }
  return selected != nullptr && SendRadio(
      selected->active_client_token, data, size, request_token);
}

bool TDisplayP4Device::SendRadio(uint32_t client_token,
    const uint8_t* data, size_t size, uint64_t request_token) {
  RadioState* state = FindRadioState(client_token);
  if (state == nullptr) {
    return false;
  }
  if (data == nullptr || size == 0 || size > kRadioPayloadCapacity ||
      request_token == 0) {
    LogMessage(LogLevel::kError, __FILE__, __LINE__,
        "Radio send rejected: invalid request, message=%lu, size=%u bytes\n",
        static_cast<unsigned long>(static_cast<uint32_t>(request_token)),
        static_cast<unsigned>(size));
    return false;
  }
  if (state->mutex == nullptr ||
      xSemaphoreTake(state->mutex, pdMS_TO_TICKS(50)) != pdTRUE) {
    LogMessage(LogLevel::kError, __FILE__, __LINE__,
        "Radio send rejected: radio is busy, message=%lu\n",
        static_cast<unsigned long>(static_cast<uint32_t>(request_token)));
    return false;
  }
  if (!state->active) {
    LogMessage(LogLevel::kError, __FILE__, __LINE__,
        "Radio send rejected: profile %lu is inactive, message=%lu\n",
        static_cast<unsigned long>(state->active_client_token),
        static_cast<unsigned long>(static_cast<uint32_t>(request_token)));
    xSemaphoreGive(state->mutex);
    return false;
  }
  if (state->transmitting) {
    LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
        "Radio send rejected: message %lu is still transmitting, "
        "new message=%lu\n",
        static_cast<unsigned long>(
            static_cast<uint32_t>(state->transmit_request_token)),
        static_cast<unsigned long>(static_cast<uint32_t>(request_token)));
    xSemaphoreGive(state->mutex);
    return false;
  }
  bool result = false;
  bool send_failure_is_chip_error = true;
  uint32_t estimated_time_ms = 0;
  if (state->chip == radio::ChipType::kSx1262 &&
      state->protocol == radio::ProtocolType::kLora &&
      driver_.IsSx1262Ready()) {
    LoraTransmitTiming timing;
    if (CalculateLoraTransmitTiming(state->lora_config, size, &timing)) {
      auto* radio = driver_.chip().sx1262.get();
      result = radio != nullptr && radio->StartTransmit(
          data, size, timing.hardware_timeout_ms);
      if (result) {
        state->transmit_deadline_us = esp_timer_get_time() +
            static_cast<int64_t>(timing.watchdog_timeout_ms) * 1000;
        estimated_time_ms = timing.time_on_air_ms;
      }
    }
  } else if (state->chip == radio::ChipType::kLr2021 &&
             state->protocol == radio::ProtocolType::kLora &&
             driver_.IsLr2021Ready()) {
    LoraTransmitTiming timing;
    usp_cpp_bus_driver::Lr20xx::LoraConfig driver_config;
    if (CalculateLoraTransmitTiming(state->lora_config, size, &timing) &&
        BuildLr2021Config(state->lora_config, static_cast<uint8_t>(size),
            &driver_config)) {
      auto* radio = driver_.chip().lr2021.get();
      result = radio != nullptr &&
          radio->Invoke(lr20xx_system_clear_irq_status,
              LR20XX_SYSTEM_IRQ_ALL_MASK) == LR20XX_STATUS_OK &&
          radio->Invoke(lr20xx_radio_fifo_clear_tx) == LR20XX_STATUS_OK &&
          radio->Invoke(lr20xx_radio_lora_set_packet_params,
              &driver_config.packet) == LR20XX_STATUS_OK &&
          radio->WriteBuffer(data, size) &&
          radio->StartTransmit(timing.hardware_timeout_ms);
      if (result) {
        state->transmit_deadline_us = esp_timer_get_time() +
            static_cast<int64_t>(timing.watchdog_timeout_ms) * 1000;
        estimated_time_ms = timing.time_on_air_ms;
      }
    }
  } else if (state->chip == radio::ChipType::kCc1101 &&
             state->protocol == radio::ProtocolType::kGfsk &&
             driver_.IsCc1101Ready() && size <= 60) {
    auto* radio = driver_.chip().cc1101.get();
    std::array<uint8_t, 60> fixed_payload = {};
    const uint8_t* transmit_data = data;
    size_t transmit_size = size;
    if (state->gfsk_config.fec_enabled) {
      std::copy_n(data, size, fixed_payload.begin());
      transmit_data = fixed_payload.data();
      transmit_size = fixed_payload.size();
    }
    state->receive_interrupt_pending.store(
        false, std::memory_order_relaxed);
    const bool transmitted = radio != nullptr &&
        radio->Transmit(transmit_data, transmit_size);
    // GDO0 在发送结束时也会产生下降沿，重新进入 RX 前丢弃该通知。
    state->receive_interrupt_pending.store(
        false, std::memory_order_relaxed);
    const bool receive_restarted = radio != nullptr && radio->StartReceive();
    result = transmitted && receive_restarted;
    send_failure_is_chip_error = !receive_restarted;
  } else if (state->chip == radio::ChipType::kNrf24l01 &&
             state->protocol ==
                 radio::ProtocolType::kEnhancedShockBurst &&
             driver_.IsNrf24l01Ready() &&
             size <= cpp_bus_driver::Nrf24l01x::kMaximumPayloadLength) {
    auto* radio = driver_.chip().nrf24l01.get();
    if (radio != nullptr) {
      std::array<uint8_t,
          cpp_bus_driver::Nrf24l01x::kMaximumPayloadLength> fixed_payload = {};
      const uint8_t* transmit_data = data;
      size_t transmit_size = size;
      if (!state->enhanced_shock_burst_config.dynamic_payload_enabled) {
        std::copy_n(data, size, fixed_payload.begin());
        transmit_data = fixed_payload.data();
        transmit_size = fixed_payload.size();
      }
      const cpp_bus_driver::Nrf24l01x::TransmitResult transmit_result =
          radio->Transmit(transmit_data, transmit_size, false, 250);
      result = transmit_result ==
          cpp_bus_driver::Nrf24l01x::TransmitResult::kSuccess;
      if (result) {
        result = radio->StartReceive();
      } else {
        const bool receive_restarted = radio->StartReceive();
        send_failure_is_chip_error =
            transmit_result ==
                cpp_bus_driver::Nrf24l01x::TransmitResult::kBusError ||
            transmit_result ==
                cpp_bus_driver::Nrf24l01x::TransmitResult::kInvalidArgument ||
            !receive_restarted;
        LogMessage(LogLevel::kError, __FILE__, __LINE__,
            "nRF24L01 transmit failed: result=%s, auto_ack=%s, "
            "channel=%u, data_rate=%lu, receive_recovery=%s\n",
            Nrf24l01TransmitResultName(transmit_result),
            state->enhanced_shock_burst_config.auto_ack_enabled
                ? "enabled"
                : "disabled",
            static_cast<unsigned>(
                state->enhanced_shock_burst_config.channel),
            static_cast<unsigned long>(
                state->enhanced_shock_burst_config.data_rate_bps),
            receive_restarted ? "succeeded" : "failed");
      }
    }
  }
  state->transmitting = result;
  state->chip_error = !result && send_failure_is_chip_error;
  state->transmit_request_token = result ? request_token : 0;
  if (result && state->chip != radio::ChipType::kSx1262 &&
      state->chip != radio::ChipType::kLr2021) {
    state->pending_event = RadioEvent();
    state->pending_event.type = RadioEventType::kTransmitComplete;
    state->pending_event.client_token = state->active_client_token;
    state->pending_event.request_token = request_token;
  }
  if (!result) {
    state->transmit_deadline_us = 0;
  }
  const uint32_t profile_id = state->active_client_token;
  xSemaphoreGive(state->mutex);
  if (result) {
    LogMessage(LogLevel::kDebug, __FILE__, __LINE__,
        "Radio send started: profile %lu, %u bytes, estimated %lu ms\n",
        static_cast<unsigned long>(profile_id), static_cast<unsigned>(size),
        static_cast<unsigned long>(estimated_time_ms));
  } else {
    LogMessage(LogLevel::kError, __FILE__, __LINE__,
        "Radio send start failed: profile=%lu, message=%lu, size=%u bytes\n",
        static_cast<unsigned long>(profile_id),
        static_cast<unsigned long>(static_cast<uint32_t>(request_token)),
        static_cast<unsigned>(size));
  }
  return result;
}

bool TDisplayP4Device::PollRadioEvent(RadioEvent* event) {
  if (event == nullptr) {
    return false;
  }
  *event = RadioEvent();
  RadioState* states[] = {&radio_, &cc1101_radio_, &nrf24l01_radio_};
  bool result = true;
  for (size_t offset = 0; offset < std::size(states); ++offset) {
    const size_t index = (radio_poll_index_ + offset) % std::size(states);
    RadioEvent candidate;
    const bool poll_result = PollRadioState(states[index], &candidate);
    result &= poll_result;
    if (candidate.type != RadioEventType::kNone) {
      *event = candidate;
      radio_poll_index_ = static_cast<uint8_t>(
          (index + 1) % std::size(states));
      return poll_result;
    }
  }
  radio_poll_index_ = static_cast<uint8_t>(
      (radio_poll_index_ + 1) % std::size(states));
  return result;
}

bool TDisplayP4Device::PollRadioState(
    RadioState* state, RadioEvent* event) {
  if (event == nullptr) {
    return false;
  }
  *event = RadioEvent();
  if (state->mutex == nullptr ||
      xSemaphoreTake(state->mutex, pdMS_TO_TICKS(20)) != pdTRUE) {
    LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
        "Radio event poll failed: mutex unavailable\n");
    return false;
  }
  event->client_token = state->active_client_token;
  event->request_token = state->transmit_request_token;
  if (!state->active) {
    xSemaphoreGive(state->mutex);
    return true;
  }
  if (state->pending_event.type != RadioEventType::kNone) {
    *event = state->pending_event;
    state->pending_event = RadioEvent();
    state->transmitting = false;
    state->transmit_request_token = 0;
    xSemaphoreGive(state->mutex);
    return true;
  }
  const bool hardware_ready =
      (state->chip == radio::ChipType::kSx1262 &&
          driver_.IsSx1262Ready()) ||
      (state->chip == radio::ChipType::kLr2021 &&
          driver_.IsLr2021Ready()) ||
      (state->chip == radio::ChipType::kCc1101 &&
          keyboard_expansion_.state.load() ==
              KeyboardExpansionState::kReady &&
          driver_.IsCc1101Ready()) ||
      (state->chip == radio::ChipType::kNrf24l01 &&
          keyboard_expansion_.state.load() ==
              KeyboardExpansionState::kReady &&
          driver_.IsNrf24l01Ready());
  if (!hardware_ready) {
    if (state->chip == radio::ChipType::kCc1101) {
      DeinitializeCc1101ReceiveInterrupt();
    }
    state->active = false;
    state->transmitting = false;
    state->chip_error = true;
    event->type = RadioEventType::kChipError;
    event->failure_reason = RadioFailureReason::kHardwareUnavailable;
    state->transmit_request_token = 0;
    state->transmit_deadline_us = 0;
    xSemaphoreGive(state->mutex);
    LogMessage(LogLevel::kError, __FILE__, __LINE__,
        "Radio event failed: chip is unavailable, profile=%lu, message=%lu\n",
        static_cast<unsigned long>(event->client_token),
        static_cast<unsigned long>(
            static_cast<uint32_t>(event->request_token)));
    return false;
  }

  if (state->chip == radio::ChipType::kCc1101) {
    if (!state->receive_interrupt_pending.exchange(
            false, std::memory_order_acq_rel)) {
      xSemaphoreGive(state->mutex);
      return true;
    }
    auto* radio = driver_.chip().cc1101.get();
    cpp_bus_driver::Cc1101::PacketMetrics metrics;
    size_t received = 0;
    const bool packet_received = radio != nullptr &&
        radio->ReadReceivedPacket(event->payload, 60, &received, &metrics);
    const bool receive_restarted = radio != nullptr && radio->StartReceive();
    state->active = receive_restarted;
    state->chip_error = !receive_restarted;
    if (!receive_restarted) {
      event->type = RadioEventType::kChipError;
      event->failure_reason = RadioFailureReason::kReceiveRestartFailed;
    } else if (packet_received) {
      if (state->gfsk_config.fec_enabled) {
        while (received > 0 && event->payload[received - 1] == 0) {
          --received;
        }
      }
      event->type = RadioEventType::kPacketReceived;
      event->payload_size = received;
      event->rssi_dbm = static_cast<int8_t>(std::clamp(
          metrics.rssi_dbm, -128.0F, 127.0F));
      event->snr_db = 0;
      event->rssi_valid = true;
      event->snr_valid = false;
    }
    xSemaphoreGive(state->mutex);
    return receive_restarted;
  }

  if (state->chip == radio::ChipType::kNrf24l01) {
    auto* radio = driver_.chip().nrf24l01.get();
    bool fifo_empty = true;
    if (radio == nullptr || !radio->RxFifoEmpty(&fifo_empty)) {
      state->active = false;
      state->chip_error = true;
      event->type = RadioEventType::kChipError;
      event->failure_reason = RadioFailureReason::kIrqReadFailed;
      xSemaphoreGive(state->mutex);
      return false;
    }
    if (!fifo_empty) {
      size_t received = 0;
      const bool received_ok = radio->ReadRxPayload(
          event->payload, cpp_bus_driver::Nrf24l01x::kMaximumPayloadLength,
          &received);
      bool fifo_empty_after_read = true;
      const bool status_ok = received_ok &&
          radio->RxFifoEmpty(&fifo_empty_after_read) &&
          (!fifo_empty_after_read || radio->ClearIrqFlag(
              cpp_bus_driver::Nrf24l01x::IrqSource::kRxDataReady));
      if (status_ok) {
        if (!state->enhanced_shock_burst_config.dynamic_payload_enabled) {
          while (received > 0 && event->payload[received - 1] == 0) {
            --received;
          }
        }
        event->type = RadioEventType::kPacketReceived;
        event->payload_size = received;
        event->rssi_valid = false;
        event->snr_valid = false;
      } else {
        state->active = false;
        state->chip_error = true;
        event->type = RadioEventType::kChipError;
        event->failure_reason = RadioFailureReason::kIrqClearFailed;
      }
      xSemaphoreGive(state->mutex);
      return status_ok;
    }
    xSemaphoreGive(state->mutex);
    return true;
  }

  if (state->chip == radio::ChipType::kLr2021) {
    auto* radio = driver_.chip().lr2021.get();
    auto* io_expander = driver_.chip().xl9535.get();
    lr20xx_system_irq_mask_t irq_mask = LR20XX_SYSTEM_IRQ_NONE;
    const uint8_t dio1_level =
        io_expander == nullptr
            ? UINT8_MAX
            : io_expander->GpioRead(gpio::xl9535::kRadioDio1);
    if (radio == nullptr || dio1_level == UINT8_MAX ||
        (dio1_level == 1 &&
            radio->Invoke(lr20xx_system_get_and_clear_irq_status,
                &irq_mask) != LR20XX_STATUS_OK)) {
      state->active = false;
      state->transmitting = false;
      state->chip_error = true;
      event->type = RadioEventType::kChipError;
      event->failure_reason = RadioFailureReason::kIrqReadFailed;
      state->transmit_request_token = 0;
      state->transmit_deadline_us = 0;
      xSemaphoreGive(state->mutex);
      return false;
    }
    if (irq_mask == LR20XX_SYSTEM_IRQ_NONE) {
      if (state->transmitting && state->transmit_deadline_us > 0 &&
          esp_timer_get_time() >= state->transmit_deadline_us) {
        const bool recovered =
            radio->Invoke(lr20xx_system_clear_irq_status,
                LR20XX_SYSTEM_IRQ_ALL_MASK) == LR20XX_STATUS_OK &&
            StartLr2021Receive(radio, state->lora_config);
        state->transmitting = false;
        state->active = recovered;
        state->chip_error = !recovered;
        state->transmit_request_token = 0;
        state->transmit_deadline_us = 0;
        event->type = RadioEventType::kTransmitFailed;
        event->failure_reason = RadioFailureReason::kSoftwareTimeout;
        xSemaphoreGive(state->mutex);
        return recovered;
      }
      xSemaphoreGive(state->mutex);
      return true;
    }

    const bool timed_out =
        (irq_mask & LR20XX_SYSTEM_IRQ_TIMEOUT) != 0;
    const bool tx_done =
        (irq_mask & LR20XX_SYSTEM_IRQ_TX_DONE) != 0;
    const bool rx_done =
        (irq_mask & LR20XX_SYSTEM_IRQ_RX_DONE) != 0;
    const bool receive_error =
        (irq_mask & (LR20XX_SYSTEM_IRQ_CRC_ERROR |
            LR20XX_SYSTEM_IRQ_LEN_ERROR |
            LR20XX_SYSTEM_IRQ_LORA_HEADER_ERROR)) != 0;
    bool result = true;
    if (state->transmitting && (tx_done || timed_out)) {
      state->transmitting = false;
      state->transmit_request_token = 0;
      state->transmit_deadline_us = 0;
      const bool receive_restarted =
          StartLr2021Receive(radio, state->lora_config);
      state->active = receive_restarted;
      state->chip_error = !receive_restarted;
      event->type = tx_done ? RadioEventType::kTransmitComplete
                            : RadioEventType::kTransmitFailed;
      event->failure_reason = tx_done
          ? (receive_restarted ? RadioFailureReason::kNone
                               : RadioFailureReason::kReceiveRestartFailed)
          : RadioFailureReason::kHardwareTimeout;
      result = receive_restarted;
    } else if (state->transmitting) {
      result = true;
    } else if (rx_done && !receive_error) {
      lr20xx_radio_lora_packet_status_t metrics = {};
      const bool packet_read =
          radio->Invoke(lr20xx_radio_lora_get_packet_status, &metrics) ==
              LR20XX_STATUS_OK &&
          metrics.packet_length_bytes > 0 &&
          metrics.packet_length_bytes <= kRadioPayloadCapacity &&
          radio->ReadBuffer(event->payload, metrics.packet_length_bytes);
      result = StartLr2021Receive(radio, state->lora_config);
      if (packet_read && result) {
        event->type = RadioEventType::kPacketReceived;
        event->payload_size = metrics.packet_length_bytes;
        const int16_t rssi_dbm = metrics.rssi_pkt_in_dbm -
            static_cast<int16_t>(metrics.rssi_pkt_half_dbm_count) / 2;
        event->rssi_dbm = static_cast<int8_t>(
            std::clamp<int16_t>(rssi_dbm, INT8_MIN, INT8_MAX));
        event->snr_db = static_cast<int8_t>(metrics.snr_pkt_raw / 4);
        event->rssi_valid = true;
        event->snr_valid = true;
      }
    } else {
      result = StartLr2021Receive(radio, state->lora_config);
    }
    if (!result) {
      state->active = false;
      state->transmitting = false;
      state->chip_error = true;
      state->transmit_request_token = 0;
      state->transmit_deadline_us = 0;
      if (event->type == RadioEventType::kNone) {
        event->type = RadioEventType::kChipError;
      }
      if (event->failure_reason == RadioFailureReason::kNone) {
        event->failure_reason = RadioFailureReason::kReceiveRestartFailed;
      }
    }
    xSemaphoreGive(state->mutex);
    return result;
  }

  auto* radio = driver_.chip().sx1262.get();
  sx126x_irq_mask_t irq_mask = SX126X_IRQ_NONE;
  if (radio == nullptr || !radio->GetIrqStatus(irq_mask)) {
    state->active = false;
    state->transmitting = false;
    state->chip_error = true;
    event->type = RadioEventType::kChipError;
    event->failure_reason = RadioFailureReason::kIrqReadFailed;
    state->transmit_request_token = 0;
    state->transmit_deadline_us = 0;
    xSemaphoreGive(state->mutex);
    LogMessage(LogLevel::kError, __FILE__, __LINE__,
        "Radio event failed: cannot read IRQ, profile=%lu, message=%lu\n",
        static_cast<unsigned long>(event->client_token),
        static_cast<unsigned long>(
            static_cast<uint32_t>(event->request_token)));
    return false;
  }
  if (irq_mask == SX126X_IRQ_NONE) {
    if (state->transmitting && state->transmit_deadline_us > 0 &&
        esp_timer_get_time() >= state->transmit_deadline_us) {
      const bool recovered = radio->StartReceive();
      state->transmitting = false;
      state->active = recovered;
      state->chip_error = !recovered;
      state->transmit_request_token = 0;
      state->transmit_deadline_us = 0;
      event->type = RadioEventType::kTransmitFailed;
      event->failure_reason = RadioFailureReason::kSoftwareTimeout;
      xSemaphoreGive(state->mutex);
      LogMessage(LogLevel::kError, __FILE__, __LINE__,
          "Radio send failed: software timeout, profile=%lu, message=%lu, "
          "receive recovery=%s\n",
          static_cast<unsigned long>(event->client_token),
          static_cast<unsigned long>(
              static_cast<uint32_t>(event->request_token)),
          recovered ? "succeeded" : "failed");
      return recovered;
    }
    xSemaphoreGive(state->mutex);
    return true;
  }

  const bool timed_out = (irq_mask & SX126X_IRQ_TIMEOUT) != 0;
  const bool tx_done = (irq_mask & SX126X_IRQ_TX_DONE) != 0;
  const bool rx_done = (irq_mask & SX126X_IRQ_RX_DONE) != 0;
  const bool receive_error =
      (irq_mask & (SX126X_IRQ_HEADER_ERROR | SX126X_IRQ_CRC_ERROR)) != 0;
  char irq_text[kRadioIrqTextCapacity] = {};
  bool irq_text_ready = false;
  const auto irq_text_for_log = [&]() -> const char* {
    if (!irq_text_ready) {
      FormatRadioIrqMask(irq_mask, irq_text, sizeof(irq_text));
      irq_text_ready = true;
    }
    return irq_text;
  };
  bool result = radio->ClearIrqStatus(irq_mask);
  if (!result) {
    state->active = false;
    state->transmitting = false;
    state->chip_error = true;
    state->transmit_request_token = 0;
    state->transmit_deadline_us = 0;
    event->type = RadioEventType::kChipError;
    event->failure_reason = RadioFailureReason::kIrqClearFailed;
    xSemaphoreGive(state->mutex);
    LogMessage(LogLevel::kError, __FILE__, __LINE__,
        "Radio event failed: cannot clear IRQ %s, message=%lu\n",
        irq_text_for_log(),
        static_cast<unsigned long>(
            static_cast<uint32_t>(event->request_token)));
    return false;
  }
  if (state->transmitting && (tx_done || timed_out)) {
    state->transmitting = false;
    state->transmit_request_token = 0;
    state->transmit_deadline_us = 0;
    const bool receive_restarted = radio->StartReceive();
    state->active = receive_restarted;
    state->chip_error = !receive_restarted;
    if (timed_out) {
      event->type = RadioEventType::kTransmitFailed;
      event->failure_reason = RadioFailureReason::kHardwareTimeout;
    } else {
      event->type = RadioEventType::kTransmitComplete;
      if (!receive_restarted) {
        event->failure_reason = RadioFailureReason::kReceiveRestartFailed;
      }
    }
    result = receive_restarted;
    if (event->type == RadioEventType::kTransmitComplete && receive_restarted) {
      LogMessage(LogLevel::kDebug, __FILE__, __LINE__,
          "Radio send completed: profile %lu\n",
          static_cast<unsigned long>(event->client_token));
    } else if (event->type == RadioEventType::kTransmitFailed) {
      LogMessage(LogLevel::kError, __FILE__, __LINE__,
          "Radio send failed: hardware timeout, profile=%lu, message=%lu, "
          "receive recovery=%s\n",
          static_cast<unsigned long>(event->client_token),
          static_cast<unsigned long>(
              static_cast<uint32_t>(event->request_token)),
          receive_restarted ? "succeeded" : "failed");
    } else {
      LogMessage(LogLevel::kError, __FILE__, __LINE__,
          "Radio send completed, but receive restart failed: profile=%lu, "
          "message=%lu\n",
          static_cast<unsigned long>(event->client_token),
          static_cast<unsigned long>(
              static_cast<uint32_t>(event->request_token)));
    }
  } else if (state->transmitting) {
    LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
        "Radio send ignored unrelated IRQ %s, message=%lu\n",
        irq_text_for_log(),
        static_cast<unsigned long>(
            static_cast<uint32_t>(event->request_token)));
  } else if (rx_done && !receive_error) {
    uint8_t received_size = 0;
    usp_cpp_bus_driver::Sx126x::PacketMetrics metrics;
    result = radio->ReadPacket(
        event->payload, kRadioPayloadCapacity, received_size, &metrics);
    result = result && radio->StartReceive();
    if (result) {
      event->type = RadioEventType::kPacketReceived;
      event->payload_size = received_size;
      event->rssi_dbm = metrics.rssi_dbm;
      event->snr_db = metrics.snr_db;
    }
  } else {
    result = radio->StartReceive();
    if (receive_error) {
      LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
          "Radio RX packet rejected: IRQ=%s\n", irq_text_for_log());
    }
  }

  if (!result) {
    state->active = false;
    state->transmitting = false;
    state->chip_error = true;
    if (event->type != RadioEventType::kTransmitComplete) {
      event->type = RadioEventType::kChipError;
    }
    if (event->failure_reason == RadioFailureReason::kNone) {
      event->failure_reason = RadioFailureReason::kReceiveRestartFailed;
    }
    state->transmit_request_token = 0;
    state->transmit_deadline_us = 0;
    LogMessage(LogLevel::kError, __FILE__, __LINE__,
        "Radio event processing failed: profile=%lu, message=%lu, IRQ=%s\n",
        static_cast<unsigned long>(event->client_token),
        static_cast<unsigned long>(
            static_cast<uint32_t>(event->request_token)),
        irq_text_for_log());
  }
  xSemaphoreGive(state->mutex);
  return result;
}

bool TDisplayP4Device::ReadRadioStatus(RadioStatus* status) {
  RadioState* selected = nullptr;
  RadioState* states[] = {&radio_, &cc1101_radio_, &nrf24l01_radio_};
  for (RadioState* state : states) {
    if (!state->active && state->active_client_token == 0) {
      continue;
    }
    if (selected != nullptr) {
      return false;
    }
    selected = state;
  }
  return selected != nullptr && ReadRadioStateStatus(selected, status);
}

bool TDisplayP4Device::ReadRadioStatus(
    uint32_t client_token, RadioStatus* status) {
  return ReadRadioStateStatus(FindRadioState(client_token), status);
}

bool TDisplayP4Device::ReadRadioStateStatus(
    RadioState* state, RadioStatus* status) {
  if (state == nullptr || status == nullptr || state->mutex == nullptr ||
      xSemaphoreTake(state->mutex, pdMS_TO_TICKS(20)) != pdTRUE) {
    return false;
  }
  *status = RadioStatus();
  switch (state->chip) {
    case radio::ChipType::kSx1262:
      status->hardware_ready = driver_.IsSx1262Ready();
      break;
    case radio::ChipType::kLr2021:
      status->hardware_ready = driver_.IsLr2021Ready();
      break;
    case radio::ChipType::kCc1101:
      status->hardware_ready =
          keyboard_expansion_.state.load() ==
              KeyboardExpansionState::kReady &&
          driver_.IsCc1101Ready();
      break;
    case radio::ChipType::kNrf24l01:
      status->hardware_ready =
          keyboard_expansion_.state.load() ==
              KeyboardExpansionState::kReady &&
          driver_.IsNrf24l01Ready();
      break;
    default:
      status->hardware_ready = false;
      break;
  }
  status->transmitting = state->transmitting;
  status->active_client_token = state->active_client_token;
  if (state->chip_error || (state->active && !status->hardware_ready)) {
    status->state = RadioLinkState::kChipError;
  } else if (state->active) {
    status->state = RadioLinkState::kActive;
  } else {
    status->state = RadioLinkState::kInactive;
  }
  xSemaphoreGive(state->mutex);
  return true;
}

}  // namespace lilygo_box::hal
