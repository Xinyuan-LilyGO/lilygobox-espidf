#include "hal/device/common/lr2021_utils.h"

#include <algorithm>

#include "lr20xx_radio_lora.h"
#include "lr20xx_radio_common.h"
#include "lr20xx_system.h"

namespace lilygo_box::hal::lr2021_utils {

constexpr lr20xx_system_irq_mask_t kRadioIrqMask =
    LR20XX_SYSTEM_IRQ_TX_DONE | LR20XX_SYSTEM_IRQ_RX_DONE |
    LR20XX_SYSTEM_IRQ_TIMEOUT | LR20XX_SYSTEM_IRQ_CRC_ERROR |
    LR20XX_SYSTEM_IRQ_LEN_ERROR | LR20XX_SYSTEM_IRQ_LORA_HEADER_ERROR;
struct Lr2021LfPaTableEntry {
  int8_t half_power;
  uint8_t pa_duty_cycle;
  uint8_t pa_lf_slices;
};
struct Lr2021HfPaTableEntry {
  int8_t half_power;
  uint8_t pa_hf_duty_cycle;
};
constexpr Lr2021LfPaTableEntry kPa915[] = {
    {44, 7, 6}, {42, 7, 7}, {41, 6, 6}, {39, 6, 6}, {38, 5, 6},
    {36, 5, 6}, {36, 4, 4}, {33, 5, 4}, {34, 4, 2}, {31, 4, 3},
    {30, 5, 1}, {32, 2, 2}, {32, 2, 1},
};
constexpr Lr2021LfPaTableEntry kPa490[] = {
    {40, 7, 7}, {38, 7, 7}, {36, 7, 6}, {34, 7, 6}, {32, 7, 6},
    {31, 7, 4}, {31, 6, 4}, {29, 7, 2}, {30, 5, 3}, {29, 5, 2},
    {31, 4, 2},
};
constexpr Lr2021HfPaTableEntry kPa2445[] = {
    {24, 16}, {24, 26}, {24, 30}, {22, 30}, {21, 31}, {18, 30},
    {16, 30}, {15, 31}, {10, 25}, {8, 25}, {7, 28}, {6, 30}, {4, 30},
};

bool SelectBandwidth(uint32_t value, lr20xx_radio_lora_bw_t* output) {
  if (output == nullptr) return false;
  switch (value) {
    case 31250:
      *output = LR20XX_RADIO_LORA_BW_31;
      return true;
    case 41670:
      *output = LR20XX_RADIO_LORA_BW_41;
      return true;
    case 62500:
      *output = LR20XX_RADIO_LORA_BW_62;
      return true;
    case 83340:
      *output = LR20XX_RADIO_LORA_BW_83;
      return true;
    case 101563:
      *output = LR20XX_RADIO_LORA_BW_101;
      return true;
    case 125000:
      *output = LR20XX_RADIO_LORA_BW_125;
      return true;
    case 203000:
      *output = LR20XX_RADIO_LORA_BW_203;
      return true;
    case 250000:
      *output = LR20XX_RADIO_LORA_BW_250;
      return true;
    case 406000:
      *output = LR20XX_RADIO_LORA_BW_406;
      return true;
    case 500000:
      *output = LR20XX_RADIO_LORA_BW_500;
      return true;
    case 812000:
      *output = LR20XX_RADIO_LORA_BW_812;
      return true;
    case 1000000:
      *output = LR20XX_RADIO_LORA_BW_1000;
      return true;
    default:
      return false;
  }
}

bool BuildConfig(const LoraRadioConfig& source, uint8_t payload_size,
    usp_cpp_bus_driver::Lr20xx::LoraConfig* target) {
  const bool low_frequency_band = source.frequency_hz >= 150000000U &&
                                  source.frequency_hz <= 960000000U;
  const bool high_frequency_band = source.frequency_hz >= 2400000000U &&
                                   source.frequency_hz <= 2500000000U;
  if (target == nullptr || (!low_frequency_band && !high_frequency_band) ||
      source.preamble_length == 0 ||
      source.spreading_factor < 5 || source.spreading_factor > 12 ||
      source.lr2021_rx_boost_mode > 7 ||
      !::lilygo_box::radio::IsLr2021BandwidthSupported(source.frequency_hz,
          source.bandwidth_hz) ||
      !::lilygo_box::radio::IsLr2021CodingRate(source.lr2021_coding_rate)) {
    return false;
  }
  lr20xx_radio_lora_bw_t bandwidth;
  if (!SelectBandwidth(source.bandwidth_hz, &bandwidth)) return false;
  lr20xx_radio_common_pa_cfg_t pa = {};
  int8_t output_power_half_dbm = 0;
  const bool high_frequency = source.frequency_hz >= 1600000000U;
  if (high_frequency) {
    if (source.output_power_dbm < -19 || source.output_power_dbm > 5) {
      return false;
    }
    const int8_t power_index = std::clamp<int8_t>(source.output_power_dbm, 0, 5);
    const auto& power = kPa2445[12 - power_index];
    pa = {.pa_sel = LR20XX_RADIO_COMMON_PA_SEL_HF,
          .pa_lf_mode = LR20XX_RADIO_COMMON_PA_LF_MODE_FSM,
          .pa_lf_duty_cycle = 7,
          .pa_lf_slices = 6,
          .pa_hf_duty_cycle = power.pa_hf_duty_cycle};
    output_power_half_dbm = source.output_power_dbm < 0
                                ? static_cast<int8_t>(source.output_power_dbm * 2)
                                : power.half_power;
  } else {
    if (source.output_power_dbm < -9 || source.output_power_dbm > 22) {
      return false;
    }
    const bool low_band = source.frequency_hz < 700000000U;
    const int8_t maximum = low_band ? 20 : 22;
    if (source.output_power_dbm > maximum) {
      return false;
    }
    const int8_t power_index =
        std::clamp<int8_t>(source.output_power_dbm, 10, maximum);
    const auto& power = low_band ? kPa490[20 - power_index]
                                 : kPa915[22 - power_index];
    pa = {.pa_sel = LR20XX_RADIO_COMMON_PA_SEL_LF,
          .pa_lf_mode = LR20XX_RADIO_COMMON_PA_LF_MODE_FSM,
          .pa_lf_duty_cycle = power.pa_duty_cycle,
          .pa_lf_slices = power.pa_lf_slices,
          .pa_hf_duty_cycle = 16};
    output_power_half_dbm = source.output_power_dbm < 10
                                ? static_cast<int8_t>(source.output_power_dbm * 2)
                                : power.half_power;
  }
  *target = usp_cpp_bus_driver::Lr20xx::LoraConfig{};
  target->frequency_hz = source.frequency_hz;
  target->modulation.sf = static_cast<lr20xx_radio_lora_sf_t>(
      source.spreading_factor);
  target->modulation.bw = bandwidth;
  target->modulation.cr = static_cast<lr20xx_radio_lora_cr_t>(
      static_cast<uint8_t>(source.lr2021_coding_rate));
  target->modulation.ppm =
      (static_cast<uint64_t>(1) << source.spreading_factor) * 1000U >=
              static_cast<uint64_t>(source.bandwidth_hz) * 16U
          ? LR20XX_RADIO_LORA_PPM_1_4
          : LR20XX_RADIO_LORA_NO_PPM;
  target->packet = {
      .preamble_len_in_symb = source.preamble_length,
      .pkt_mode = LR20XX_RADIO_LORA_PKT_EXPLICIT,
      .pld_len_in_bytes = payload_size,
      .crc = source.crc_enabled ? LR20XX_RADIO_LORA_CRC_ENABLED
                                : LR20XX_RADIO_LORA_CRC_DISABLED,
      .iq = source.invert_iq ? LR20XX_RADIO_LORA_IQ_INVERTED
                             : LR20XX_RADIO_LORA_IQ_STANDARD,
  };
  target->sync_word = source.sync_word;
  target->rx_path = source.frequency_hz >= 1600000000U
                        ? LR20XX_RADIO_COMMON_RX_PATH_HF
                        : LR20XX_RADIO_COMMON_RX_PATH_LF;
  target->rx_boost_mode = static_cast<lr20xx_radio_common_rx_path_boost_mode_t>(
      source.lr2021_rx_boost_mode);
  target->pa = pa;
  target->output_power_half_dbm = output_power_half_dbm;
  target->ramp_time = LR20XX_RADIO_COMMON_RAMP_48_US;
  return true;
}

bool StartReceive(usp_cpp_bus_driver::Lr20xx* radio,
    const LoraRadioConfig& config) {
  usp_cpp_bus_driver::Lr20xx::LoraConfig driver_config;
  if (radio == nullptr || !BuildConfig(config, UINT8_MAX, &driver_config)) {
    return false;
  }
  const auto& packet = driver_config.packet;
  // 先退出收发状态，再清理 FIFO 和 IRQ，避免上一次事件污染新的接收周期。
  return radio->Invoke(lr20xx_system_set_standby_mode,
             LR20XX_SYSTEM_STANDBY_MODE_RC) == LR20XX_STATUS_OK &&
         radio->Invoke(lr20xx_system_clear_irq_status,
             LR20XX_SYSTEM_IRQ_ALL_MASK) == LR20XX_STATUS_OK &&
         radio->Invoke(lr20xx_radio_fifo_clear_rx) == LR20XX_STATUS_OK &&
         radio->Invoke(lr20xx_radio_lora_set_packet_params, &packet) ==
             LR20XX_STATUS_OK &&
         radio->Invoke(lr20xx_system_set_dio_irq_cfg, LR20XX_SYSTEM_DIO_11,
             kRadioIrqMask) == LR20XX_STATUS_OK &&
         radio->StartReceive(0);
}

// 使用 Semtech 官方 LR20xx 空中时间实现，避免低带宽高扩频发送被固定超时误判。
int64_t CalculateTransmitTimeoutUs(const LoraRadioConfig& config,
    size_t payload_size) {
  usp_cpp_bus_driver::Lr20xx::LoraConfig driver_config;
  if (!BuildConfig(config, static_cast<uint8_t>(payload_size),
          &driver_config)) {
    return 0;
  }
  const uint32_t time_on_air_ms = lr20xx_radio_lora_get_time_on_air_in_ms(
      &driver_config.packet, &driver_config.modulation);
  const uint64_t timeout_ms = std::max<uint64_t>(1000U,
      static_cast<uint64_t>(time_on_air_ms) * 5U / 4U + 500U);
  return static_cast<int64_t>(timeout_ms * 1000U);
}


}  // namespace lilygo_box::hal::lr2021_utils
