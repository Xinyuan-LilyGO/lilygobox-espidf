/*
 * @Description: T-Display-P4-Air LR2021 射频 Provider 实现
 * @Author: LILYGO_L
 * @Date: 2026-09-16 00:00:00
 * @LastEditTime: 2026-10-28 09:24:28
 * @License: GPL 3.0
 */
#include "hal/device/t_display_p4_air/device.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <iterator>

#include "base/logger.h"
#include "hal/device/common/lr2021_utils.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

namespace lilygo_box::hal {
namespace gpio = lilygo_device_driver::t_display_p4_air::gpio;
namespace {

constexpr int16_t kLr2021UnavailableRssiDbm = -255;


}  // namespace

// 读取 Air 板载 LR2021 支持的频段、协议和负载上限。

bool TDisplayP4AirDevice::ReadRadioCapabilities(RadioCapabilities* capabilities) {
  if (capabilities == nullptr) return false;
  *capabilities = RadioCapabilities();
  if (driver_.IsLr2021Ready()) {
    auto& entry = capabilities->entries[capabilities->count++];
    entry.chip = radio::ChipType::kLr2021;
    entry.protocol = radio::ProtocolType::kLora;
    entry.maximum_payload_size = kRadioPayloadCapacity;
    entry.frequency_bands[0] = {150000000U, 960000000U};
    entry.frequency_bands[1] = {2400000000U, 2500000000U};
    entry.frequency_band_count = 2;
  }
  capabilities->supports_external_antenna = false;
  return true;
}


bool TDisplayP4AirDevice::ActivateRadio(const RadioConfig& config) {
  if (config.chip != radio::ChipType::kLr2021 ||
      config.protocol != radio::ProtocolType::kLora || config.client_token == 0) {
    return false;
  }
  if (radio_.active && !DeactivateRadio()) return false;
  if (radio_.mutex == nullptr ||
      xSemaphoreTake(radio_.mutex, pdMS_TO_TICKS(100)) != pdTRUE) return false;
  bool result = driver_.IsLr2021Ready() || driver_.InitLr2021();
  usp_cpp_bus_driver::Lr20xx::LoraConfig driver_config;
  if (result) result = lr2021_utils::BuildConfig(config.lora, UINT8_MAX, &driver_config);
  if (result) {
    result = driver_.SetLr2021OperatingMode(lilygo_device_driver::TDisplayP4AirDriver::Lr2021OperatingMode::kStandby);
  }
  auto* radio = driver_.chip().lr2021.get();
  if (result) result = radio != nullptr && radio->Configure(driver_config) && lr2021_utils::StartReceive(radio, config.lora);
  if (!result) driver_.SetLr2021OperatingMode(lilygo_device_driver::TDisplayP4AirDriver::Lr2021OperatingMode::kSleep);
  radio_.active_client_token = result ? config.client_token : 0;
  radio_.lora_config = config.lora;
  radio_.active = result;
  radio_.transmitting = false;
  radio_.chip_error = !result;
  radio_.transmit_request_token = 0;
  radio_.transmit_deadline_us = 0;
  xSemaphoreGive(radio_.mutex);
  return result;
}

bool TDisplayP4AirDevice::DeactivateRadio() {
  if (radio_.mutex == nullptr ||
      xSemaphoreTake(radio_.mutex, pdMS_TO_TICKS(100)) != pdTRUE) return false;
  auto* radio = driver_.chip().lr2021.get();
  bool result = true;
  if (radio != nullptr && driver_.IsLr2021Ready()) {
    result &= radio->Invoke(lr20xx_system_set_dio_irq_cfg,
        LR20XX_SYSTEM_DIO_11, LR20XX_SYSTEM_IRQ_NONE) == LR20XX_STATUS_OK;
    result &= radio->Invoke(lr20xx_system_clear_irq_status,
        LR20XX_SYSTEM_IRQ_ALL_MASK) == LR20XX_STATUS_OK;
    result &= driver_.SetLr2021OperatingMode(
        lilygo_device_driver::TDisplayP4AirDriver::Lr2021OperatingMode::kStandby);
  }
  radio_.active_client_token = 0;
  radio_.transmit_request_token = 0;
  radio_.transmit_deadline_us = 0;
  radio_.active = false;
  radio_.transmitting = false;
  radio_.chip_error = false;
  xSemaphoreGive(radio_.mutex);
  return result;
}

bool TDisplayP4AirDevice::SendRadio(const uint8_t* data, size_t size, uint64_t request_token) {
  if (data == nullptr || size == 0 || size > kRadioPayloadCapacity || request_token == 0 ||
      radio_.mutex == nullptr || xSemaphoreTake(radio_.mutex, pdMS_TO_TICKS(100)) != pdTRUE) return false;
  auto* radio = driver_.chip().lr2021.get();
  usp_cpp_bus_driver::Lr20xx::LoraConfig driver_config;
  bool result = radio_.active && !radio_.transmitting && radio != nullptr &&
      lr2021_utils::BuildConfig(radio_.lora_config, static_cast<uint8_t>(size), &driver_config) &&
      radio->Invoke(lr20xx_system_set_standby_mode,
          LR20XX_SYSTEM_STANDBY_MODE_RC) == LR20XX_STATUS_OK &&
      radio->Invoke(lr20xx_system_clear_irq_status,
          LR20XX_SYSTEM_IRQ_ALL_MASK) == LR20XX_STATUS_OK &&
      radio->Invoke(lr20xx_radio_fifo_clear_tx) == LR20XX_STATUS_OK &&
      radio->Invoke(lr20xx_radio_lora_set_packet_params, &driver_config.packet) ==
          LR20XX_STATUS_OK &&
      radio->WriteBuffer(data, size) && radio->StartTransmit(0);
  const bool receive_recovered = result ||
      (radio != nullptr && lr2021_utils::StartReceive(radio, radio_.lora_config));
  radio_.active = receive_recovered;
  radio_.chip_error = !receive_recovered;
  if (result) {
    radio_.transmitting = true;
    radio_.transmit_request_token = request_token;
    radio_.transmit_deadline_us = esp_timer_get_time() +
        lr2021_utils::CalculateTransmitTimeoutUs(radio_.lora_config, size);
  }
  xSemaphoreGive(radio_.mutex);
  return result;
}

bool TDisplayP4AirDevice::PollRadioEvent(RadioEvent* event) {
  if (event == nullptr || radio_.mutex == nullptr ||
      xSemaphoreTake(radio_.mutex, pdMS_TO_TICKS(20)) != pdTRUE) return false;
  *event = RadioEvent();
  event->client_token = radio_.active_client_token;
  event->request_token = radio_.transmit_request_token;
  auto* radio = driver_.chip().lr2021.get();
  if (!radio_.active || radio == nullptr) { xSemaphoreGive(radio_.mutex); return true; }
  lr20xx_system_irq_mask_t irq = LR20XX_SYSTEM_IRQ_NONE;
  const bool irq_pending = tool_ != nullptr && tool_->GpioRead(gpio::lr2021::kInt);
  if (irq_pending && (radio->Invoke(
          lr20xx_system_get_and_clear_irq_status, &irq) != LR20XX_STATUS_OK)) {
    radio_.chip_error = true; event->type = RadioEventType::kChipError;
    event->failure_reason = RadioFailureReason::kIrqReadFailed;
    xSemaphoreGive(radio_.mutex); return false;
  }
  if (irq == LR20XX_SYSTEM_IRQ_NONE) {
    if (radio_.transmitting && esp_timer_get_time() >= radio_.transmit_deadline_us) {
      radio_.transmitting = false; radio_.transmit_request_token = 0; radio_.transmit_deadline_us = 0;
      radio_.chip_error = !lr2021_utils::StartReceive(radio, radio_.lora_config);
      event->type = RadioEventType::kTransmitFailed;
      event->failure_reason = RadioFailureReason::kSoftwareTimeout;
      xSemaphoreGive(radio_.mutex); return !radio_.chip_error;
    }
    xSemaphoreGive(radio_.mutex); return true;
  }
  const bool tx_done = (irq & LR20XX_SYSTEM_IRQ_TX_DONE) != 0;
  const bool timed_out = (irq & LR20XX_SYSTEM_IRQ_TIMEOUT) != 0;
  const bool rx_done = (irq & LR20XX_SYSTEM_IRQ_RX_DONE) != 0;
  const bool rx_error = (irq & (LR20XX_SYSTEM_IRQ_CRC_ERROR | LR20XX_SYSTEM_IRQ_LEN_ERROR | LR20XX_SYSTEM_IRQ_LORA_HEADER_ERROR)) != 0;
  bool result = radio->Invoke(lr20xx_system_clear_irq_status, irq) == LR20XX_STATUS_OK;
  if (radio_.transmitting && (tx_done || timed_out)) {
    radio_.transmitting = false; radio_.transmit_request_token = 0; radio_.transmit_deadline_us = 0;
    result = result && lr2021_utils::StartReceive(radio, radio_.lora_config);
    event->type = tx_done ? RadioEventType::kTransmitComplete : RadioEventType::kTransmitFailed;
    event->failure_reason = tx_done ? RadioFailureReason::kNone : RadioFailureReason::kHardwareTimeout;
  } else if (!radio_.transmitting && rx_done && !rx_error) {
    lr20xx_radio_lora_packet_status_t packet_status = {};
    const bool read_ok = radio->Invoke(lr20xx_radio_lora_get_packet_status, &packet_status) == LR20XX_STATUS_OK &&
        packet_status.packet_length_bytes > 0 && packet_status.packet_length_bytes <= kRadioPayloadCapacity &&
        radio->ReadBuffer(event->payload, packet_status.packet_length_bytes);
    result = result && lr2021_utils::StartReceive(radio, radio_.lora_config);
    if (read_ok && result) {
      event->type = RadioEventType::kPacketReceived;
      event->payload_size = packet_status.packet_length_bytes;
      event->rssi_valid = packet_status.rssi_pkt_in_dbm != kLr2021UnavailableRssiDbm;
      event->rssi_quarter_dbm = packet_status.rssi_pkt_in_dbm * 4 - static_cast<int16_t>(packet_status.rssi_pkt_half_dbm_count) * 2;
      event->snr_valid = true; event->snr_quarter_db = packet_status.snr_pkt_raw;
    }
  } else if (!radio_.transmitting) result = result && lr2021_utils::StartReceive(radio, radio_.lora_config);
  radio_.chip_error = !result;
  if (!result && event->type == RadioEventType::kNone) event->type = RadioEventType::kChipError;
  xSemaphoreGive(radio_.mutex);
  return result;
}

bool TDisplayP4AirDevice::ReadRadioStatus(RadioStatus* status) {
  if (status == nullptr || radio_.mutex == nullptr ||
      xSemaphoreTake(radio_.mutex, pdMS_TO_TICKS(20)) != pdTRUE) return false;
  *status = RadioStatus();
  status->hardware_ready = driver_.IsLr2021Ready();
  status->transmitting = radio_.transmitting;
  status->active_client_token = radio_.active_client_token;
  status->state = radio_.chip_error || (radio_.active && !status->hardware_ready)
      ? RadioLinkState::kChipError : (radio_.active ? RadioLinkState::kActive : RadioLinkState::kInactive);
  xSemaphoreGive(radio_.mutex);
  return true;
}

}  // namespace lilygo_box::hal
