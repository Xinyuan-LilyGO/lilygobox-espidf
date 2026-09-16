/*
 * @Description: T-Display-P4 V2 LR2021 射频 Provider 实现
 * @Author: LILYGO_L
 * @Date: 2026-09-16 00:00:00
 * @LastEditTime: 2026-09-16 00:00:00
 * @License: GPL 3.0
 */
#include "hal/device/t_display_p4/device.h"

#include <algorithm>
#include <cstdint>

#include "base/logger.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

namespace lilygo_box::hal {
namespace gpio = lilygo_device_driver::t_display_p4::gpio;
namespace {

constexpr int16_t kLr2021UnavailableRssiDbm = -255;
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

bool BuildLrConfig(const LoraRadioConfig& source, uint8_t payload_size,
    usp_cpp_bus_driver::Lr20xx::LoraConfig* target) {
  const bool low_frequency_band = source.frequency_hz >= 150000000U &&
                                  source.frequency_hz <= 960000000U;
  const bool high_frequency_band = source.frequency_hz >= 2400000000U &&
                                   source.frequency_hz <= 2500000000U;
  if (target == nullptr || (!low_frequency_band && !high_frequency_band) ||
      source.preamble_length == 0 ||
      source.spreading_factor < 5 || source.spreading_factor > 12 ||
      source.lr2021_rx_boost_mode > 7 ||
      !radio::IsLr2021BandwidthSupported(source.frequency_hz,
          source.bandwidth_hz) ||
      !radio::IsLr2021CodingRate(source.lr2021_coding_rate)) {
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
  if (radio == nullptr || !BuildLrConfig(config, UINT8_MAX, &driver_config)) {
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
  if (!BuildLrConfig(config, static_cast<uint8_t>(payload_size),
          &driver_config)) {
    return 0;
  }
  const uint32_t time_on_air_ms = lr20xx_radio_lora_get_time_on_air_in_ms(
      &driver_config.packet, &driver_config.modulation);
  const uint64_t timeout_ms = std::max<uint64_t>(1000U,
      static_cast<uint64_t>(time_on_air_ms) * 5U / 4U + 500U);
  return static_cast<int64_t>(timeout_ms * 1000U);
}

}  // namespace

// 读取 V2 板载 LR2021 支持的频段、协议和负载上限。
bool TDisplayP4Device::ReadRadioCapabilities(RadioCapabilities* capabilities) {
  if (capabilities == nullptr) return false;
  *capabilities = RadioCapabilities();
  // 仅暴露已完成初始化的板载 LR2021。
  if (!driver_.IsLr2021Ready()) return true;
  auto& entry = capabilities->entries[capabilities->count++];
  entry.chip = radio::ChipType::kLr2021;
  entry.protocol = radio::ProtocolType::kLora;
  entry.maximum_payload_size = kRadioPayloadCapacity;
  entry.frequency_bands[0] = {150000000U, 960000000U};
  entry.frequency_bands[1] = {2400000000U, 2500000000U};
  entry.frequency_band_count = 2;
  capabilities->supports_external_antenna = false;
  return true;
}

TDisplayP4Device::RadioState* TDisplayP4Device::RadioStateForChip(
    radio::ChipType chip) {
  return chip == radio::ChipType::kLr2021 ? &radio_ : nullptr;
}

TDisplayP4Device::RadioState* TDisplayP4Device::FindRadioState(
    uint32_t client_token) {
  return client_token != 0 && radio_.active_client_token == client_token
             ? &radio_
             : nullptr;
}

// 配置 LR2021 并启动单包接收。
bool TDisplayP4Device::ActivateRadio(const RadioConfig& config) {
  // 切换配置前先释放旧会话，避免在持有同一互斥锁时递归停用。
  if (radio_.active && !DeactivateRadioState(&radio_)) {
    return false;
  }
  if (config.chip != radio::ChipType::kLr2021 ||
      config.protocol != radio::ProtocolType::kLora || config.client_token == 0 ||
      radio_.mutex == nullptr ||
      xSemaphoreTake(radio_.mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
    return false;
  }
  bool result = driver_.IsLr2021Ready() || driver_.InitLr2021();
  usp_cpp_bus_driver::Lr20xx::LoraConfig driver_config;
  if (result) {
    result = BuildLrConfig(config.lora, UINT8_MAX, &driver_config);
  }
  if (result) {
    result = driver_.SetLr2021OperatingMode(
        lilygo_device_driver::TDisplayP4Driver::Lr2021OperatingMode::kStandby);
  }
  auto* radio = driver_.chip().lr2021.get();
  if (result) {
    result = radio != nullptr && radio->Configure(driver_config) &&
             StartReceive(radio, config.lora);
  }
  if (!result) {
    driver_.SetLr2021OperatingMode(
        lilygo_device_driver::TDisplayP4Driver::Lr2021OperatingMode::kSleep);
  }
  radio_.active = result;
  radio_.transmitting = false;
  radio_.chip_error = !result;
  radio_.active_client_token = result ? config.client_token : 0;
  radio_.chip = result ? config.chip : radio::ChipType::kUnknown;
  radio_.protocol = result ? config.protocol : radio::ProtocolType::kUnknown;
  radio_.lora_config = config.lora;
  radio_.transmit_request_token = 0;
  radio_.transmit_deadline_us = 0;
  radio_.pending_event = RadioEvent();
  xSemaphoreGive(radio_.mutex);
  return result;
}

bool TDisplayP4Device::DeactivateRadio() {
  return !radio_.active && radio_.active_client_token == 0
             ? true
             : DeactivateRadioState(&radio_);
}

bool TDisplayP4Device::DeactivateRadio(uint32_t client_token) {
  return client_token == 0 ? DeactivateRadio()
                            : (FindRadioState(client_token) != nullptr &&
                                  DeactivateRadioState(&radio_));
}

bool TDisplayP4Device::DeactivateRadioState(RadioState* state) {
  if (state == nullptr || state->mutex == nullptr ||
      xSemaphoreTake(state->mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
    return false;
  }
  bool result = true;
  auto* radio = driver_.chip().lr2021.get();
  if (radio != nullptr && driver_.IsLr2021Ready()) {
    result &= radio->Invoke(lr20xx_system_set_dio_irq_cfg, LR20XX_SYSTEM_DIO_11,
        LR20XX_SYSTEM_IRQ_NONE) == LR20XX_STATUS_OK;
    result &= radio->Invoke(lr20xx_system_clear_irq_status,
        LR20XX_SYSTEM_IRQ_ALL_MASK) == LR20XX_STATUS_OK;
    result &= driver_.SetLr2021OperatingMode(
        lilygo_device_driver::TDisplayP4Driver::Lr2021OperatingMode::kStandby);
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
  return result;
}

bool TDisplayP4Device::SendRadio(
    const uint8_t* data, size_t size, uint64_t request_token) {
  return radio_.active &&
         SendRadio(radio_.active_client_token, data, size, request_token);
}

bool TDisplayP4Device::SendRadio(uint32_t client_token, const uint8_t* data,
    size_t size, uint64_t request_token) {
  RadioState* state = FindRadioState(client_token);
  if (state == nullptr || data == nullptr || size == 0 ||
      size > kRadioPayloadCapacity || request_token == 0 ||
      xSemaphoreTake(state->mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
    return false;
  }
  if (state->transmitting || !driver_.IsLr2021Ready()) {
    xSemaphoreGive(state->mutex);
    return false;
  }
  usp_cpp_bus_driver::Lr20xx::LoraConfig config;
  auto* radio = driver_.chip().lr2021.get();
  const bool result =
      BuildLrConfig(state->lora_config, static_cast<uint8_t>(size), &config) &&
      radio != nullptr &&
      radio->Invoke(lr20xx_system_set_standby_mode,
          LR20XX_SYSTEM_STANDBY_MODE_RC) == LR20XX_STATUS_OK &&
      radio->Invoke(lr20xx_system_clear_irq_status,
          LR20XX_SYSTEM_IRQ_ALL_MASK) == LR20XX_STATUS_OK &&
      radio->Invoke(lr20xx_radio_fifo_clear_tx) == LR20XX_STATUS_OK &&
      radio->Invoke(lr20xx_radio_lora_set_packet_params, &config.packet) ==
          LR20XX_STATUS_OK &&
      radio->WriteBuffer(data, size) && radio->StartTransmit(0);
  bool receive_recovered = result;
  if (!result) {
    // 发送启动失败后重新接收，恢复失败才将会话标记为芯片错误。
    receive_recovered =
        radio != nullptr && StartReceive(radio, state->lora_config);
  }
  state->active = receive_recovered;
  state->transmitting = result;
  state->chip_error = !receive_recovered;
  state->transmit_request_token = result ? request_token : 0;
  state->transmit_deadline_us =
      result ? esp_timer_get_time() +
                   CalculateTransmitTimeoutUs(state->lora_config, size)
             : 0;
  xSemaphoreGive(state->mutex);
  return result;
}

bool TDisplayP4Device::PollRadioEvent(RadioEvent* event) {
  return PollRadioState(&radio_, event);
}

// 处理 LR2021 IRQ、收发完成事件和发送超时恢复。
bool TDisplayP4Device::PollRadioState(RadioState* state, RadioEvent* event) {
  if (state == nullptr || event == nullptr || state->mutex == nullptr ||
      xSemaphoreTake(state->mutex, pdMS_TO_TICKS(20)) != pdTRUE) {
    return false;
  }
  *event = RadioEvent();
  event->client_token = state->active_client_token;
  event->request_token = state->transmit_request_token;
  if (!state->active || !driver_.IsLr2021Ready()) {
    xSemaphoreGive(state->mutex);
    return true;
  }
  auto* radio = driver_.chip().lr2021.get();
  lr20xx_system_irq_mask_t irq = LR20XX_SYSTEM_IRQ_NONE;
  // LR2021 DIO11 通过板上 GPIO5 直接接入 P4，低电平时无需访问 SPI。
  const bool irq_pending =
      tool_ != nullptr && tool_->GpioRead(gpio::lr2021::kInt);
  if (irq_pending &&
      (radio == nullptr ||
          radio->Invoke(lr20xx_system_get_and_clear_irq_status, &irq) !=
              LR20XX_STATUS_OK)) {
    state->active = false;
    state->chip_error = true;
    event->type = RadioEventType::kChipError;
    event->failure_reason = RadioFailureReason::kIrqReadFailed;
    xSemaphoreGive(state->mutex);
    return false;
  }
  if (irq == LR20XX_SYSTEM_IRQ_NONE) {
    if (state->transmitting &&
        esp_timer_get_time() >= state->transmit_deadline_us) {
      state->transmitting = false;
      state->transmit_request_token = 0;
      state->transmit_deadline_us = 0;
      state->chip_error = !StartReceive(radio, state->lora_config);
      state->active = !state->chip_error;
      event->type = RadioEventType::kTransmitFailed;
      event->failure_reason = RadioFailureReason::kSoftwareTimeout;
    }
    xSemaphoreGive(state->mutex);
    return !state->chip_error;
  }
  const bool tx_done = (irq & LR20XX_SYSTEM_IRQ_TX_DONE) != 0;
  const bool timed_out = (irq & LR20XX_SYSTEM_IRQ_TIMEOUT) != 0;
  const bool rx_done = (irq & LR20XX_SYSTEM_IRQ_RX_DONE) != 0;
  const bool rx_error = (irq & (LR20XX_SYSTEM_IRQ_CRC_ERROR |
      LR20XX_SYSTEM_IRQ_LEN_ERROR | LR20XX_SYSTEM_IRQ_LORA_HEADER_ERROR)) != 0;
  bool result = true;
  if (state->transmitting && (tx_done || timed_out)) {
    state->transmitting = false;
    state->transmit_request_token = 0;
    state->transmit_deadline_us = 0;
    result = StartReceive(radio, state->lora_config);
    event->type = tx_done ? RadioEventType::kTransmitComplete
                          : RadioEventType::kTransmitFailed;
    event->failure_reason = tx_done ? RadioFailureReason::kNone
                                    : RadioFailureReason::kHardwareTimeout;
  } else if (!state->transmitting && rx_done && !rx_error) {
    lr20xx_radio_lora_packet_status_t packet_status = {};
    const bool read_ok =
        radio->Invoke(lr20xx_radio_lora_get_packet_status, &packet_status) ==
            LR20XX_STATUS_OK && packet_status.packet_length_bytes > 0 &&
        packet_status.packet_length_bytes <= kRadioPayloadCapacity &&
        radio->ReadBuffer(event->payload, packet_status.packet_length_bytes);
    result = StartReceive(radio, state->lora_config);
    if (read_ok && result) {
      event->type = RadioEventType::kPacketReceived;
      event->payload_size = packet_status.packet_length_bytes;
      event->rssi_valid = packet_status.rssi_pkt_in_dbm !=
                          kLr2021UnavailableRssiDbm;
      event->rssi_quarter_dbm = packet_status.rssi_pkt_in_dbm * 4 -
          static_cast<int16_t>(packet_status.rssi_pkt_half_dbm_count) * 2;
      event->snr_valid = true;
      event->snr_quarter_db = packet_status.snr_pkt_raw;
    }
  } else if (!state->transmitting) {
    result = StartReceive(radio, state->lora_config);
  }
  if (!result) {
    state->active = false;
    state->chip_error = true;
    event->type = event->type == RadioEventType::kNone
                      ? RadioEventType::kChipError : event->type;
    event->failure_reason = RadioFailureReason::kReceiveRestartFailed;
  }
  xSemaphoreGive(state->mutex);
  return result;
}

bool TDisplayP4Device::ReadRadioStatus(RadioStatus* status) {
  return ReadRadioStateStatus(&radio_, status);
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
  status->active_client_token = state->active_client_token;
  status->hardware_ready = driver_.IsLr2021Ready();
  status->transmitting = state->transmitting;
  status->state = state->chip_error || (state->active && !status->hardware_ready)
                      ? RadioLinkState::kChipError
                      : (state->active ? RadioLinkState::kActive
                                       : RadioLinkState::kInactive);
  xSemaphoreGive(state->mutex);
  return true;
}

}  // namespace lilygo_box::hal
