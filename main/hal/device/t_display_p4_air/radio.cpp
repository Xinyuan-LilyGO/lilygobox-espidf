/*
 * @Description: T-Display-P4-Air LR1121 射频硬件实现
 * @Author: LILYGO_L
 * @Date: 2026-08-28 00:00:00
 * @LastEditTime: 2026-08-28 00:00:00
 * @License: GPL 3.0
 */
#include "hal/device/t_display_p4_air/device.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <limits>

#include "base/logger.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

namespace lilygo_box::hal {
namespace {

// Radio 发送硬件超时的最小值和额外保护时间。
constexpr uint32_t kMinimumRadioTransmitTimeoutMs = 1000;
constexpr uint32_t kRadioTransmitTimeoutMarginMs = 500;
constexpr uint32_t kRadioTransmitWatchdogGraceMs = 1000;

constexpr size_t kRadioIrqTextCapacity = 160;

// LR1121 IRQ 位与日志名称映射。
struct RadioIrqDescription {
  lr11xx_system_irq_mask_t mask;
  const char* name;
};

constexpr std::array<RadioIrqDescription, 5> kRadioIrqDescriptions = {{
    {LR11XX_SYSTEM_IRQ_TX_DONE, "TX_DONE"},
    {LR11XX_SYSTEM_IRQ_RX_DONE, "RX_DONE"},
    {LR11XX_SYSTEM_IRQ_HEADER_ERROR, "HEADER_ERROR"},
    {LR11XX_SYSTEM_IRQ_CRC_ERROR, "CRC_ERROR"},
    {LR11XX_SYSTEM_IRQ_TIMEOUT, "TIMEOUT"},
}};
constexpr lr11xx_system_irq_mask_t kRadioEventIrqMask =
    LR11XX_SYSTEM_IRQ_TX_DONE | LR11XX_SYSTEM_IRQ_RX_DONE |
    LR11XX_SYSTEM_IRQ_HEADER_ERROR | LR11XX_SYSTEM_IRQ_CRC_ERROR |
    LR11XX_SYSTEM_IRQ_TIMEOUT;

/**
 * @brief 将 LR1121 IRQ 位掩码格式化为可读名称和十六进制数值
 * @param irq_mask LR1121 IRQ 位掩码
 * @param output 输出文本缓冲区
 * @param output_size 输出文本缓冲区大小
 */
void FormatRadioIrqMask(
    lr11xx_system_irq_mask_t irq_mask, char* output, size_t output_size) {
  if (output == nullptr || output_size == 0) {
    return;
  }
  output[0] = '\0';
  size_t used = 0;
  bool has_name = false;

  const auto append_name = [&](const char* name) {
    const int result = std::snprintf(
        output + used, output_size - used, "%s%s", has_name ? " | " : "", name);
    if (result < 0 || static_cast<size_t>(result) >= output_size - used) {
      output[output_size - 1] = '\0';
      return false;
    }
    used += static_cast<size_t>(result);
    has_name = true;
    return true;
  };

  lr11xx_system_irq_mask_t unknown_mask = irq_mask;
  for (const RadioIrqDescription& description : kRadioIrqDescriptions) {
    if ((irq_mask & description.mask) == 0) {
      continue;
    }
    if (!append_name(description.name)) {
      return;
    }
    unknown_mask &= ~description.mask;
  }
  if (unknown_mask != 0 && !append_name("UNKNOWN")) {
    return;
  }
  if (!has_name && !append_name("NONE")) {
    return;
  }

  std::snprintf(output + used, output_size - used, " (0x%08lX)",
      static_cast<unsigned long>(irq_mask));
}

/**
 * @brief 将应用层扩频因子转换为 LR1121 LoRa 枚举
 * @param value 应用层扩频因子
 * @param spreading_factor LR1121 扩频因子输出地址
 * @return 扩频因子受支持返回 true
 */
bool SelectLoraSpreadingFactor(
    uint8_t value, lr11xx_radio_lora_sf_t* spreading_factor) {
  if (spreading_factor == nullptr) {
    return false;
  }
  switch (value) {
    case 5:
      *spreading_factor = LR11XX_RADIO_LORA_SF5;
      return true;
    case 6:
      *spreading_factor = LR11XX_RADIO_LORA_SF6;
      return true;
    case 7:
      *spreading_factor = LR11XX_RADIO_LORA_SF7;
      return true;
    case 8:
      *spreading_factor = LR11XX_RADIO_LORA_SF8;
      return true;
    case 9:
      *spreading_factor = LR11XX_RADIO_LORA_SF9;
      return true;
    case 10:
      *spreading_factor = LR11XX_RADIO_LORA_SF10;
      return true;
    case 11:
      *spreading_factor = LR11XX_RADIO_LORA_SF11;
      return true;
    case 12:
      *spreading_factor = LR11XX_RADIO_LORA_SF12;
      return true;
    default:
      return false;
  }
}

/**
 * @brief 将应用层带宽转换为 LR1121 LoRa 带宽枚举
 * @param bandwidth_hz 应用层带宽，单位 Hz
 * @param bandwidth LR1121 带宽输出地址
 * @return 带宽受支持返回 true
 */
bool SelectLoraBandwidth(
    uint32_t bandwidth_hz, lr11xx_radio_lora_bw_t* bandwidth) {
  if (bandwidth == nullptr) {
    return false;
  }
  switch (bandwidth_hz) {
    case 62500:
      *bandwidth = LR11XX_RADIO_LORA_BW_62;
      return true;
    case 125000:
      *bandwidth = LR11XX_RADIO_LORA_BW_125;
      return true;
    case 200000:
      *bandwidth = LR11XX_RADIO_LORA_BW_200;
      return true;
    case 250000:
      *bandwidth = LR11XX_RADIO_LORA_BW_250;
      return true;
    case 400000:
      *bandwidth = LR11XX_RADIO_LORA_BW_400;
      return true;
    case 500000:
      *bandwidth = LR11XX_RADIO_LORA_BW_500;
      return true;
    case 800000:
      *bandwidth = LR11XX_RADIO_LORA_BW_800;
      return true;
    default:
      return false;
  }
}

/**
 * @brief 将应用层编码率分母转换为 LR1121 LoRa 枚举
 * @param denominator 应用层编码率分母
 * @param coding_rate LR1121 编码率输出地址
 * @return 编码率受支持返回 true
 */
bool SelectLoraCodingRate(
    uint8_t denominator, lr11xx_radio_lora_cr_t* coding_rate) {
  if (coding_rate == nullptr) {
    return false;
  }
  switch (denominator) {
    case 5:
      *coding_rate = LR11XX_RADIO_LORA_CR_4_5;
      return true;
    case 6:
      *coding_rate = LR11XX_RADIO_LORA_CR_4_6;
      return true;
    case 7:
      *coding_rate = LR11XX_RADIO_LORA_CR_4_7;
      return true;
    case 8:
      *coding_rate = LR11XX_RADIO_LORA_CR_4_8;
      return true;
    default:
      return false;
  }
}

bool ShouldEnableLoraLdro(const LoraRadioConfig& config);

/**
 * @brief 创建 LR1121 LoRa 数据包参数
 * @param source 应用层 LoRa 配置
 * @param payload_length 当前收发负载长度
 * @return LR1121 数据包参数
 */
lr11xx_radio_pkt_params_lora_t MakeLr1121PacketConfig(
    const LoraRadioConfig& source, uint8_t payload_length) {
  return {
      .preamble_len_in_symb = source.preamble_length,
      .header_type = LR11XX_RADIO_LORA_PKT_EXPLICIT,
      .pld_len_in_bytes = payload_length,
      .crc = source.crc_enabled ? LR11XX_RADIO_LORA_CRC_ON
                                : LR11XX_RADIO_LORA_CRC_OFF,
      .iq = source.invert_iq ? LR11XX_RADIO_LORA_IQ_INVERTED
                             : LR11XX_RADIO_LORA_IQ_STANDARD,
  };
}

/**
 * @brief 使用当前 LoRa 参数重新进入连续接收
 * @param lr1121 LR1121 驱动
 * @param config 应用层 LoRa 配置
 * @return 接收启动成功返回 true
 */
bool StartLr1121Receive(
    usp_cpp_bus_driver::Lr11xx& lr1121, const LoraRadioConfig& config) {
  const lr11xx_radio_pkt_params_lora_t packet =
      MakeLr1121PacketConfig(config, UINT8_MAX);
  return lr1121.Invoke(lr11xx_radio_set_lora_pkt_params, &packet) ==
             LR11XX_STATUS_OK &&
         lr1121.StartReceive(0);
}

struct Lr1121ImageCalibrationBand {
  uint16_t minimum_mhz = 0;
  uint16_t maximum_mhz = 0;
};

/**
 * @brief 选择覆盖目标 Sub-GHz 频率的 LR1121 镜像校准区间
 * @param frequency_hz 目标射频频率
 * @param band 校准区间输出地址
 * @return 目标位于 LR1121 Sub-GHz 路径且区间有效时返回 true
 */
bool SelectLr1121ImageCalibrationBand(
    uint32_t frequency_hz, Lr1121ImageCalibrationBand* band) {
  static constexpr Lr1121ImageCalibrationBand kStandardBands[] = {
      {430, 440},
      {470, 510},
      {779, 787},
      {863, 870},
      {902, 928},
  };
  if (band == nullptr || frequency_hz < 150000000U ||
      frequency_hz > 960000000U) {
    return false;
  }

  for (const Lr1121ImageCalibrationBand& standard_band : kStandardBands) {
    if (frequency_hz >=
            static_cast<uint32_t>(standard_band.minimum_mhz) * 1000000U &&
        frequency_hz <=
            static_cast<uint32_t>(standard_band.maximum_mhz) * 1000000U) {
      *band = standard_band;
      return true;
    }
  }

  // 非标准频段使用目标频率前后各 10 MHz 的窗口，频率变化超过
  // LR1121 手册要求的 10 MHz 阈值后会重新执行镜像校准。
  const uint16_t frequency_mhz = static_cast<uint16_t>(frequency_hz / 1000000U);
  band->minimum_mhz =
      frequency_mhz > 160 ? static_cast<uint16_t>(frequency_mhz - 10) : 150;
  band->maximum_mhz = static_cast<uint16_t>(
      std::min<uint32_t>(960U, static_cast<uint32_t>(frequency_mhz) + 10U));
  return band->minimum_mhz < band->maximum_mhz;
}

/**
 * @brief 确保 LR1121 已完成目标 Sub-GHz 区间的镜像校准
 * @param lr1121 LR1121 驱动
 * @param frequency_hz 目标射频频率
 * @param calibrated_minimum_mhz 已缓存校准区间下限
 * @param calibrated_maximum_mhz 已缓存校准区间上限
 * @return 无需校准或校准成功时返回 true
 */
bool EnsureLr1121ImageCalibration(usp_cpp_bus_driver::Lr11xx& lr1121,
    uint32_t frequency_hz, uint16_t* calibrated_minimum_mhz,
    uint16_t* calibrated_maximum_mhz) {
  if (frequency_hz >= 2400000000U && frequency_hz <= 2500000000U) {
    // CalibImage 只适用于 RFI_N/P_LF Sub-GHz 接收路径。
    return true;
  }
  if (calibrated_minimum_mhz == nullptr || calibrated_maximum_mhz == nullptr) {
    return false;
  }

  Lr1121ImageCalibrationBand band;
  if (!SelectLr1121ImageCalibrationBand(frequency_hz, &band)) {
    return false;
  }
  if (frequency_hz >=
          static_cast<uint32_t>(*calibrated_minimum_mhz) * 1000000U &&
      frequency_hz <=
          static_cast<uint32_t>(*calibrated_maximum_mhz) * 1000000U) {
    return true;
  }

  if (lr1121.Invoke(lr11xx_system_calibrate_image_in_mhz, band.minimum_mhz,
          band.maximum_mhz) != LR11XX_STATUS_OK) {
    return false;
  }
  *calibrated_minimum_mhz = band.minimum_mhz;
  *calibrated_maximum_mhz = band.maximum_mhz;
  return true;
}

/**
 * @brief 校验应用层 LoRa 参数并转换为板载射频驱动配置
 * @param source 应用层 LoRa 配置
 * @param target 板载射频驱动配置输出地址
 * @return 参数有效且转换成功时返回 true
 */
bool BuildRadioConfig(const LoraRadioConfig& source,
    usp_cpp_bus_driver::Lr11xx::LoraConfig* target) {
  const bool use_hf_path =
      source.frequency_hz >= 2400000000U && source.frequency_hz <= 2500000000U;
  const bool use_sub_ghz_path =
      source.frequency_hz >= 150000000U && source.frequency_hz <= 960000000U;
  const bool bandwidth_supported =
      use_hf_path
          ? (source.bandwidth_hz == 200000 || source.bandwidth_hz == 400000 ||
                source.bandwidth_hz == 800000)
          : (source.bandwidth_hz == 62500 || source.bandwidth_hz == 125000 ||
                source.bandwidth_hz == 250000 || source.bandwidth_hz == 500000);
  lr11xx_radio_lora_sf_t spreading_factor;
  lr11xx_radio_lora_bw_t bandwidth;
  lr11xx_radio_lora_cr_t coding_rate;
  if (target == nullptr || (!use_hf_path && !use_sub_ghz_path) ||
      !bandwidth_supported || source.preamble_length == 0 ||
      source.output_power_dbm < -9 ||
      source.output_power_dbm > (use_hf_path ? 13 : 22) ||
      !SelectLoraSpreadingFactor(
          source.spreading_factor, &spreading_factor) ||
      !SelectLoraBandwidth(source.bandwidth_hz, &bandwidth) ||
      !SelectLoraCodingRate(source.coding_rate_denominator, &coding_rate)) {
    return false;
  }

  *target = usp_cpp_bus_driver::Lr11xx::LoraConfig{
      .frequency_hz = source.frequency_hz,
      .modulation =
          {
              .sf = spreading_factor,
              .bw = bandwidth,
              .cr = coding_rate,
              .ldro = static_cast<uint8_t>(ShouldEnableLoraLdro(source)),
          },
      .packet = MakeLr1121PacketConfig(source, UINT8_MAX),
      .sync_word = source.sync_word,
      .rx_boosted = source.rx_boosted,
      .pa =
          {
              .pa_sel =
                  use_hf_path ? LR11XX_RADIO_PA_SEL_HF : LR11XX_RADIO_PA_SEL_HP,
              .pa_reg_supply = use_hf_path ? LR11XX_RADIO_PA_REG_SUPPLY_VREG
                                           : LR11XX_RADIO_PA_REG_SUPPLY_VBAT,
              .pa_duty_cycle = static_cast<uint8_t>(use_hf_path ? 0x00 : 0x04),
              .pa_hp_sel = static_cast<uint8_t>(use_hf_path ? 0x00 : 0x07),
          },
      .output_power_dbm = source.output_power_dbm,
      .ramp_time = LR11XX_RADIO_RAMP_48_US,
  };
  return true;
}

struct LoraTransmitTiming {
  // 根据当前调制参数计算的理论空中时间。
  uint32_t time_on_air_ms = 0;
  // 写入 LR1121 SetTx 命令的硬件超时。
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
  lr11xx_radio_lora_sf_t spreading_factor;
  lr11xx_radio_lora_bw_t bandwidth;
  lr11xx_radio_lora_cr_t coding_rate;
  if (!SelectLoraSpreadingFactor(
          config.spreading_factor, &spreading_factor) ||
      !SelectLoraBandwidth(config.bandwidth_hz, &bandwidth) ||
      !SelectLoraCodingRate(config.coding_rate_denominator, &coding_rate)) {
    return false;
  }
  const lr11xx_radio_mod_params_lora_t modulation_params = {
      .sf = spreading_factor,
      .bw = bandwidth,
      .cr = coding_rate,
      .ldro = static_cast<uint8_t>(ShouldEnableLoraLdro(config)),
  };
  const lr11xx_radio_pkt_params_lora_t packet_params = {
      .preamble_len_in_symb = config.preamble_length,
      .header_type = LR11XX_RADIO_LORA_PKT_EXPLICIT,
      .pld_len_in_bytes = static_cast<uint8_t>(payload_size),
      .crc = config.crc_enabled ? LR11XX_RADIO_LORA_CRC_ON
                                : LR11XX_RADIO_LORA_CRC_OFF,
      .iq = config.invert_iq ? LR11XX_RADIO_LORA_IQ_INVERTED
                             : LR11XX_RADIO_LORA_IQ_STANDARD,
  };
  const uint32_t time_on_air_ms = lr11xx_radio_get_lora_time_on_air_in_ms(
      &packet_params, &modulation_params);
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
  timing->hardware_timeout_ms = static_cast<uint32_t>(std::min<uint64_t>(
      requested_timeout_ms, std::numeric_limits<uint32_t>::max()));
  timing->watchdog_timeout_ms = static_cast<uint32_t>(std::min<uint64_t>(
      requested_timeout_ms + kRadioTransmitWatchdogGraceMs, UINT32_MAX));
  return true;
}

}  // namespace

bool TDisplayP4AirDevice::ReadRadioCapabilities(
    RadioCapabilities* capabilities) {
  if (capabilities == nullptr) {
    return false;
  }
  *capabilities = RadioCapabilities();
  RadioCapability& capability = capabilities->entries[0];
  capability.chip = radio::ChipType::kLr1121;
  capability.protocol = radio::ProtocolType::kLora;
  capability.maximum_payload_size = kRadioPayloadCapacity;
  capability.frequency_bands[0] = {
      .minimum_hz = 150000000U,
      .maximum_hz = 960000000U,
  };
  capability.frequency_bands[1] = {
      .minimum_hz = 2400000000U,
      .maximum_hz = 2500000000U,
  };
  capability.frequency_band_count = 2;
  capabilities->count = 1;
  capabilities->supports_external_antenna = false;
  return true;
}

bool TDisplayP4AirDevice::ActivateRadio(const RadioConfig& config) {
  if (radio_.mutex == nullptr ||
      xSemaphoreTake(radio_.mutex, pdMS_TO_TICKS(50)) != pdTRUE) {
    LogMessage(LogLevel::kError, __FILE__, __LINE__,
        "Radio activate failed: mutex unavailable, profile=%lu\n",
        static_cast<unsigned long>(config.client_token));
    return false;
  }
  usp_cpp_bus_driver::Lr11xx::LoraConfig driver_config;
  bool result = config.chip == radio::ChipType::kLr1121 &&
                config.protocol == radio::ProtocolType::kLora &&
                config.antenna == radio::AntennaType::kInternal &&
                BuildRadioConfig(config.lora, &driver_config);
  if (result && driver_.SetLr1121OperatingMode(
                    TDisplayP4AirBoardDriver::Lr1121OperatingMode::kStandby)) {
    auto* radio = driver_.chip().lr1121.get();
    result = radio != nullptr &&
             EnsureLr1121ImageCalibration(*radio, config.lora.frequency_hz,
                 &radio_.calibrated_image_minimum_mhz,
                 &radio_.calibrated_image_maximum_mhz) &&
             radio->Configure(driver_config) &&
             radio->Invoke(lr11xx_system_clear_irq_status,
                 LR11XX_SYSTEM_IRQ_ALL_MASK) == LR11XX_STATUS_OK &&
             radio->Invoke(lr11xx_system_set_dio_irq_params, kRadioEventIrqMask,
                 LR11XX_SYSTEM_IRQ_NONE) == LR11XX_STATUS_OK &&
             StartLr1121Receive(*radio, config.lora);
  } else {
    result = false;
  }
  if (!result) {
    driver_.SetLr1121OperatingMode(
        TDisplayP4AirBoardDriver::Lr1121OperatingMode::kSleep);
  }
  radio_.active = result;
  radio_.transmitting = false;
  radio_.chip_error = !result;
  radio_.active_client_token = config.client_token;
  radio_.transmit_request_token = 0;
  radio_.transmit_deadline_us = 0;
  radio_.lora_config = config.lora;
  xSemaphoreGive(radio_.mutex);
  LogMessage(result ? LogLevel::kDebug : LogLevel::kError, __FILE__, __LINE__,
      "Radio activate %s: profile=%lu, frequency=%lu Hz, SF=%u, "
      "bandwidth=%lu Hz, antenna=%s\n",
      result ? "succeeded" : "failed",
      static_cast<unsigned long>(config.client_token),
      static_cast<unsigned long>(config.lora.frequency_hz),
      static_cast<unsigned>(config.lora.spreading_factor),
      static_cast<unsigned long>(config.lora.bandwidth_hz),
      config.antenna == radio::AntennaType::kExternal ? "external"
                                                      : "internal");
  return result;
}

bool TDisplayP4AirDevice::DeactivateRadio() {
  if (radio_.mutex == nullptr ||
      xSemaphoreTake(radio_.mutex, pdMS_TO_TICKS(50)) != pdTRUE) {
    LogMessage(LogLevel::kError, __FILE__, __LINE__,
        "Radio deactivate failed: mutex unavailable\n");
    return false;
  }
  bool result = true;
  if (driver_.IsLr1121Ready()) {
    auto* radio = driver_.chip().lr1121.get();
    if (radio_.active) {
      result = radio != nullptr &&
               radio->Invoke(lr11xx_system_set_standby,
                   LR11XX_SYSTEM_STANDBY_CFG_RC) == LR11XX_STATUS_OK &&
               radio->Invoke(lr11xx_system_clear_irq_status,
                   LR11XX_SYSTEM_IRQ_ALL_MASK) == LR11XX_STATUS_OK;
    }
    result &= driver_.SetLr1121OperatingMode(
        TDisplayP4AirBoardDriver::Lr1121OperatingMode::kStandby);
  }
  radio_.active = false;
  radio_.transmitting = false;
  radio_.chip_error = !result;
  radio_.active_client_token = 0;
  radio_.transmit_request_token = 0;
  radio_.transmit_deadline_us = 0;
  xSemaphoreGive(radio_.mutex);
  LogMessage(result ? LogLevel::kInfo : LogLevel::kError, __FILE__, __LINE__,
      "Radio deactivate %s\n", result ? "succeeded" : "failed");
  return result;
}

bool TDisplayP4AirDevice::SendRadio(
    const uint8_t* data, size_t size, uint64_t request_token) {
  if (data == nullptr || size == 0 || size > kRadioPayloadCapacity ||
      request_token == 0) {
    LogMessage(LogLevel::kError, __FILE__, __LINE__,
        "Radio send rejected: invalid request, message=%lu, size=%u bytes\n",
        static_cast<unsigned long>(static_cast<uint32_t>(request_token)),
        static_cast<unsigned>(size));
    return false;
  }
  if (radio_.mutex == nullptr ||
      xSemaphoreTake(radio_.mutex, pdMS_TO_TICKS(50)) != pdTRUE) {
    LogMessage(LogLevel::kError, __FILE__, __LINE__,
        "Radio send rejected: radio is busy, message=%lu\n",
        static_cast<unsigned long>(static_cast<uint32_t>(request_token)));
    return false;
  }
  if (!radio_.active) {
    LogMessage(LogLevel::kError, __FILE__, __LINE__,
        "Radio send rejected: profile %lu is inactive, message=%lu\n",
        static_cast<unsigned long>(radio_.active_client_token),
        static_cast<unsigned long>(static_cast<uint32_t>(request_token)));
    xSemaphoreGive(radio_.mutex);
    return false;
  }
  if (radio_.transmitting) {
    LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
        "Radio send rejected: message %lu is still transmitting, "
        "new message=%lu\n",
        static_cast<unsigned long>(
            static_cast<uint32_t>(radio_.transmit_request_token)),
        static_cast<unsigned long>(static_cast<uint32_t>(request_token)));
    xSemaphoreGive(radio_.mutex);
    return false;
  }
  const bool hardware_ready = driver_.IsLr1121Ready();
  constexpr const char* kRadioChipName = "LR1121";
  if (!hardware_ready) {
    radio_.chip_error = true;
    LogMessage(LogLevel::kError, __FILE__, __LINE__,
        "Radio send rejected: %s is unavailable, message=%lu\n", kRadioChipName,
        static_cast<unsigned long>(static_cast<uint32_t>(request_token)));
    xSemaphoreGive(radio_.mutex);
    return false;
  }
  LoraTransmitTiming timing;
  if (!CalculateLoraTransmitTiming(radio_.lora_config, size, &timing)) {
    LogMessage(LogLevel::kError, __FILE__, __LINE__,
        "Radio send rejected: invalid LoRa timing, message=%lu, "
        "size=%u bytes\n",
        static_cast<unsigned long>(static_cast<uint32_t>(request_token)),
        static_cast<unsigned>(size));
    xSemaphoreGive(radio_.mutex);
    return false;
  }
  auto* radio = driver_.chip().lr1121.get();
  const lr11xx_radio_pkt_params_lora_t packet_config =
      MakeLr1121PacketConfig(radio_.lora_config, static_cast<uint8_t>(size));
  const bool result = radio != nullptr &&
                      radio->Invoke(lr11xx_system_clear_irq_status,
                          LR11XX_SYSTEM_IRQ_ALL_MASK) == LR11XX_STATUS_OK &&
                      radio->Invoke(lr11xx_radio_set_lora_pkt_params,
                          &packet_config) == LR11XX_STATUS_OK &&
                      radio->WriteBuffer(data, size) &&
                      radio->StartTransmit(timing.hardware_timeout_ms);
  radio_.transmitting = result;
  radio_.chip_error = !result;
  radio_.transmit_request_token = result ? request_token : 0;
  radio_.transmit_deadline_us =
      result ? esp_timer_get_time() +
                   static_cast<int64_t>(timing.watchdog_timeout_ms) * 1000
             : 0;
  const uint32_t profile_id = radio_.active_client_token;
  xSemaphoreGive(radio_.mutex);
  if (result) {
    LogMessage(LogLevel::kDebug, __FILE__, __LINE__,
        "Radio send started: profile %lu, %u bytes, estimated %lu ms\n",
        static_cast<unsigned long>(profile_id), static_cast<unsigned>(size),
        static_cast<unsigned long>(timing.time_on_air_ms));
  } else {
    LogMessage(LogLevel::kError, __FILE__, __LINE__,
        "Radio send start failed: profile=%lu, message=%lu, size=%u bytes\n",
        static_cast<unsigned long>(profile_id),
        static_cast<unsigned long>(static_cast<uint32_t>(request_token)),
        static_cast<unsigned>(size));
  }
  return result;
}

bool TDisplayP4AirDevice::PollRadioEvent(RadioEvent* event) {
  if (event == nullptr) {
    return false;
  }
  *event = RadioEvent();
  if (radio_.mutex == nullptr ||
      xSemaphoreTake(radio_.mutex, pdMS_TO_TICKS(20)) != pdTRUE) {
    LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
        "Radio event poll failed: mutex unavailable\n");
    return false;
  }
  event->client_token = radio_.active_client_token;
  event->request_token = radio_.transmit_request_token;
  if (!radio_.active) {
    xSemaphoreGive(radio_.mutex);
    return true;
  }
  if (!driver_.IsLr1121Ready() || driver_.chip().lr1121 == nullptr) {
    radio_.active = false;
    radio_.transmitting = false;
    radio_.chip_error = true;
    event->type = RadioEventType::kChipError;
    event->failure_reason = RadioFailureReason::kHardwareUnavailable;
    radio_.transmit_request_token = 0;
    radio_.transmit_deadline_us = 0;
    xSemaphoreGive(radio_.mutex);
    LogMessage(LogLevel::kError, __FILE__, __LINE__,
        "Radio event failed: LR1121 is unavailable, profile=%lu, "
        "message=%lu\n",
        static_cast<unsigned long>(event->client_token),
        static_cast<unsigned long>(
            static_cast<uint32_t>(event->request_token)));
    return false;
  }

  auto& lr1121 = *driver_.chip().lr1121;
  lr11xx_system_irq_mask_t irq_mask = LR11XX_SYSTEM_IRQ_NONE;
  if (lr1121.Invoke(lr11xx_system_get_irq_status, &irq_mask) !=
      LR11XX_STATUS_OK) {
    radio_.active = false;
    radio_.transmitting = false;
    radio_.chip_error = true;
    event->type = RadioEventType::kChipError;
    event->failure_reason = RadioFailureReason::kIrqReadFailed;
    radio_.transmit_request_token = 0;
    radio_.transmit_deadline_us = 0;
    xSemaphoreGive(radio_.mutex);
    LogMessage(LogLevel::kError, __FILE__, __LINE__,
        "Radio event failed: cannot read LR1121 IRQ, profile=%lu, "
        "message=%lu\n",
        static_cast<unsigned long>(event->client_token),
        static_cast<unsigned long>(
            static_cast<uint32_t>(event->request_token)));
    return false;
  }

  if (irq_mask == LR11XX_SYSTEM_IRQ_NONE) {
    if (radio_.transmitting && radio_.transmit_deadline_us > 0 &&
        esp_timer_get_time() >= radio_.transmit_deadline_us) {
      const bool recovered = StartLr1121Receive(lr1121, radio_.lora_config);
      radio_.transmitting = false;
      radio_.active = recovered;
      radio_.chip_error = !recovered;
      radio_.transmit_request_token = 0;
      radio_.transmit_deadline_us = 0;
      event->type = RadioEventType::kTransmitFailed;
      event->failure_reason = RadioFailureReason::kSoftwareTimeout;
      xSemaphoreGive(radio_.mutex);
      LogMessage(LogLevel::kError, __FILE__, __LINE__,
          "Radio send failed: software timeout, profile=%lu, message=%lu, "
          "receive recovery=%s\n",
          static_cast<unsigned long>(event->client_token),
          static_cast<unsigned long>(
              static_cast<uint32_t>(event->request_token)),
          recovered ? "succeeded" : "failed");
      return recovered;
    }
    xSemaphoreGive(radio_.mutex);
    return true;
  }

  const bool timed_out = (irq_mask & LR11XX_SYSTEM_IRQ_TIMEOUT) != 0;
  const bool tx_done = (irq_mask & LR11XX_SYSTEM_IRQ_TX_DONE) != 0;
  const bool rx_done = (irq_mask & LR11XX_SYSTEM_IRQ_RX_DONE) != 0;
  const bool receive_error = (irq_mask & (LR11XX_SYSTEM_IRQ_HEADER_ERROR |
                                             LR11XX_SYSTEM_IRQ_CRC_ERROR)) != 0;
  char irq_text[kRadioIrqTextCapacity] = {};
  FormatRadioIrqMask(irq_mask, irq_text, sizeof(irq_text));
  bool result = lr1121.Invoke(lr11xx_system_clear_irq_status, irq_mask) ==
                LR11XX_STATUS_OK;
  if (!result) {
    radio_.active = false;
    radio_.transmitting = false;
    radio_.chip_error = true;
    radio_.transmit_request_token = 0;
    radio_.transmit_deadline_us = 0;
    event->type = RadioEventType::kChipError;
    event->failure_reason = RadioFailureReason::kIrqClearFailed;
    xSemaphoreGive(radio_.mutex);
    LogMessage(LogLevel::kError, __FILE__, __LINE__,
        "Radio event failed: cannot clear IRQ %s, message=%lu\n", irq_text,
        static_cast<unsigned long>(
            static_cast<uint32_t>(event->request_token)));
    return false;
  }

  if (radio_.transmitting && (tx_done || timed_out)) {
    radio_.transmitting = false;
    radio_.transmit_request_token = 0;
    radio_.transmit_deadline_us = 0;
    const bool receive_restarted =
        StartLr1121Receive(lr1121, radio_.lora_config);
    radio_.active = receive_restarted;
    radio_.chip_error = !receive_restarted;
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
  } else if (radio_.transmitting) {
    LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
        "Radio send ignored unrelated IRQ %s, message=%lu\n", irq_text,
        static_cast<unsigned long>(
            static_cast<uint32_t>(event->request_token)));
  } else if (rx_done && !receive_error) {
    lr11xx_radio_rx_buffer_status_t buffer_status = {};
    lr11xx_radio_pkt_status_lora_t packet_status = {};
    result = lr1121.Invoke(lr11xx_radio_get_rx_buffer_status, &buffer_status) ==
                 LR11XX_STATUS_OK &&
             buffer_status.pld_len_in_bytes > 0 &&
             buffer_status.pld_len_in_bytes <= kRadioPayloadCapacity &&
             lr1121.ReadBuffer(buffer_status.buffer_start_pointer,
                 event->payload, buffer_status.pld_len_in_bytes) &&
             lr1121.Invoke(lr11xx_radio_get_lora_pkt_status, &packet_status) ==
                 LR11XX_STATUS_OK &&
             StartLr1121Receive(lr1121, radio_.lora_config);
    if (result) {
      event->type = RadioEventType::kPacketReceived;
      event->payload_size = buffer_status.pld_len_in_bytes;
      event->rssi_dbm = packet_status.rssi_pkt_in_dbm;
      event->snr_db = packet_status.snr_pkt_in_db;
    }
  } else {
    result = StartLr1121Receive(lr1121, radio_.lora_config);
    if (receive_error) {
      LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
          "Radio RX packet rejected: IRQ=%s\n", irq_text);
    }
  }

  if (!result) {
    radio_.active = false;
    radio_.transmitting = false;
    radio_.chip_error = true;
    if (event->type != RadioEventType::kTransmitComplete) {
      event->type = RadioEventType::kChipError;
    }
    if (event->failure_reason == RadioFailureReason::kNone) {
      event->failure_reason = RadioFailureReason::kReceiveRestartFailed;
    }
    radio_.transmit_request_token = 0;
    radio_.transmit_deadline_us = 0;
    LogMessage(LogLevel::kError, __FILE__, __LINE__,
        "Radio event processing failed: profile=%lu, message=%lu, IRQ=%s\n",
        static_cast<unsigned long>(event->client_token),
        static_cast<unsigned long>(static_cast<uint32_t>(event->request_token)),
        irq_text);
  }
  xSemaphoreGive(radio_.mutex);
  return result;
}

bool TDisplayP4AirDevice::ReadRadioStatus(RadioStatus* status) {
  if (status == nullptr || radio_.mutex == nullptr ||
      xSemaphoreTake(radio_.mutex, pdMS_TO_TICKS(20)) != pdTRUE) {
    return false;
  }
  status->hardware_ready = driver_.IsLr1121Ready();
  status->transmitting = radio_.transmitting;
  status->active_client_token = radio_.active_client_token;
  if (radio_.chip_error || (radio_.active && !status->hardware_ready)) {
    status->state = RadioLinkState::kChipError;
  } else if (radio_.active) {
    status->state = RadioLinkState::kActive;
  } else {
    status->state = RadioLinkState::kInactive;
  }
  xSemaphoreGive(radio_.mutex);
  return true;
}

}  // namespace lilygo_box::hal
