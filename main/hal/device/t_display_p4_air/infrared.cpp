/*
 * @Description: T-Display-P4-Air 红外收发硬件实现
 * @Author: LILYGO_L
 * @Date: 2026-08-28 00:00:00
 * @LastEditTime: 2026-09-02 17:53:34
 * @License: GPL 3.0
 */
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iterator>

#include "base/logger.h"
#include "driver/rmt_encoder.h"
#include "driver/rmt_rx.h"
#include "driver/rmt_tx.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "hal/device/t_display_p4_air/device.h"

namespace lilygo_box::hal {
namespace gpio = lilygo_device_driver::t_display_p4_air::gpio;
namespace {

constexpr uint32_t kInfraredRmtResolutionHz = 1000000;
constexpr int kNecDecodeMarginUs = 200;
constexpr uint32_t kInfraredReceiveMinimumNs = 1000;

constexpr uint32_t kInfraredReceiveMaximumNs = 12 * 1000 * 1000;
constexpr uint32_t kInfraredTransmitTimeoutMs = 1000;
constexpr uint16_t kNecLeaderMarkUs = 9000;
constexpr uint16_t kNecLeaderSpaceUs = 4500;
constexpr uint16_t kNecRepeatSpaceUs = 2250;
constexpr uint16_t kNecBitMarkUs = 560;
constexpr uint16_t kNecZeroSpaceUs = 560;
constexpr uint16_t kNecOneSpaceUs = 1690;
constexpr uint16_t kNecFrameEndSpaceUs = 10000;
constexpr size_t kNecDataBitCount = 32;
constexpr size_t kNecFrameSymbolCount = kNecDataBitCount + 2;

enum class NecDecodeResult {
  kInvalid,
  kFrame,
  kRepeat,
};

/**
 * @brief 判断 RMT symbol 持续时间是否处于 NEC 允许误差内
 * @param actual_us RMT 读取到的持续时间
 * @param expected_us NEC 协议期望持续时间
 * @return 持续时间匹配返回 true
 */
bool IsNecDuration(uint16_t actual_us, uint16_t expected_us) {
  const int difference =
      std::abs(static_cast<int>(actual_us) - static_cast<int>(expected_us));
  return difference <= kNecDecodeMarginUs;
}

/**
 * @brief 将一组 RMT symbol 解码为标准 NEC 地址和命令
 * @param symbols RMT symbol 数组
 * @param symbol_count symbol 有效数量
 * @param address NEC 地址输出地址
 * @param command NEC 命令输出地址
 * @return 普通帧、重复帧或无效帧
 */
NecDecodeResult DecodeNecSymbols(const rmt_symbol_word_t* symbols,
    size_t symbol_count, uint8_t* address, uint8_t* command) {
  if (symbols == nullptr || address == nullptr || command == nullptr ||
      symbol_count < 2 ||
      !IsNecDuration(symbols[0].duration0, kNecLeaderMarkUs)) {
    return NecDecodeResult::kInvalid;
  }
  if (IsNecDuration(symbols[0].duration1, kNecRepeatSpaceUs) &&
      IsNecDuration(symbols[1].duration0, kNecBitMarkUs)) {
    return NecDecodeResult::kRepeat;
  }
  if (symbol_count < kNecFrameSymbolCount ||
      !IsNecDuration(symbols[0].duration1, kNecLeaderSpaceUs)) {
    return NecDecodeResult::kInvalid;
  }

  uint32_t raw_data = 0;
  for (size_t bit = 0; bit < kNecDataBitCount; ++bit) {
    const rmt_symbol_word_t& symbol = symbols[bit + 1];
    if (!IsNecDuration(symbol.duration0, kNecBitMarkUs)) {
      return NecDecodeResult::kInvalid;
    }
    if (IsNecDuration(symbol.duration1, kNecOneSpaceUs)) {
      raw_data |= 1UL << bit;
    } else if (!IsNecDuration(symbol.duration1, kNecZeroSpaceUs)) {
      return NecDecodeResult::kInvalid;
    }
  }

  const uint8_t decoded_address = raw_data & 0xFFU;
  const uint8_t inverted_address = (raw_data >> 8) & 0xFFU;
  const uint8_t decoded_command = (raw_data >> 16) & 0xFFU;
  const uint8_t inverted_command = (raw_data >> 24) & 0xFFU;
  if (static_cast<uint8_t>(~decoded_address) != inverted_address ||
      static_cast<uint8_t>(~decoded_command) != inverted_command) {
    return NecDecodeResult::kInvalid;
  }
  *address = decoded_address;
  *command = decoded_command;
  return NecDecodeResult::kFrame;
}

}  // namespace

bool TDisplayP4AirDevice::SetInfraredReceiverEnabled(bool enabled) {
  if (!enabled) {
    infrared_.receiver_enabled.store(false);
    if (infrared_.mutex == nullptr ||
        xSemaphoreTake(infrared_.mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
      return false;
    }
    esp_err_t result = ESP_OK;
    if (infrared_.receive_channel_enabled &&
        infrared_.receive_channel != nullptr) {
      result = rmt_disable(infrared_.receive_channel);
      if (result == ESP_OK) {
        infrared_.receive_channel_enabled = false;
      }
    }
    infrared_.receive_pending.store(false);
    infrared_.receive_complete.store(false);
    infrared_.received_symbol_count.store(0);
    std::fill(std::begin(infrared_.receive_symbols),
        std::end(infrared_.receive_symbols), rmt_symbol_word_t{});
    infrared_.status.receiver_enabled = false;
    infrared_.status.frame_received = false;
    infrared_.status.repeat = false;
    infrared_.status.address = 0;
    infrared_.status.command = 0;
    infrared_.status.receive_count = 0;
    infrared_.status.decode_error_count = 0;
    infrared_.status.last_error = result;
    xSemaphoreGive(infrared_.mutex);
    return result == ESP_OK;
  }
  if (!InitializeInfraredHardware()) {
    return false;
  }
  if (xSemaphoreTake(infrared_.mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
    return false;
  }
  esp_err_t result = ESP_OK;
  if (!infrared_.receive_channel_enabled) {
    result = rmt_enable(infrared_.receive_channel);
    infrared_.receive_channel_enabled = result == ESP_OK;
  }
  infrared_.status.receiver_enabled = result == ESP_OK;
  infrared_.status.last_error = result;
  xSemaphoreGive(infrared_.mutex);
  if (result != ESP_OK) {
    return false;
  }
  infrared_.receiver_enabled.store(true);
  return StartInfraredReceive();
}

bool TDisplayP4AirDevice::SendInfraredNec(uint8_t address, uint8_t command) {
  if (!InitializeInfraredHardware()) {
    return false;
  }

  std::array<rmt_symbol_word_t, kNecFrameSymbolCount> symbols = {};
  symbols[0].level0 = 1;
  symbols[0].duration0 = kNecLeaderMarkUs;
  symbols[0].level1 = 0;
  symbols[0].duration1 = kNecLeaderSpaceUs;

  const uint32_t raw_data =
      static_cast<uint32_t>(address) |
      (static_cast<uint32_t>(static_cast<uint8_t>(~address)) << 8) |
      (static_cast<uint32_t>(command) << 16) |
      (static_cast<uint32_t>(static_cast<uint8_t>(~command)) << 24);
  for (size_t bit = 0; bit < kNecDataBitCount; ++bit) {
    rmt_symbol_word_t& symbol = symbols[bit + 1];
    symbol.level0 = 1;
    symbol.duration0 = kNecBitMarkUs;
    symbol.level1 = 0;
    symbol.duration1 =
        (raw_data & (1UL << bit)) != 0 ? kNecOneSpaceUs : kNecZeroSpaceUs;
  }
  symbols.back().level0 = 1;
  symbols.back().duration0 = kNecBitMarkUs;
  symbols.back().level1 = 0;
  symbols.back().duration1 = kNecFrameEndSpaceUs;

  rmt_transmit_config_t transmit_config = {};
  transmit_config.loop_count = 0;
  esp_err_t result =
      rmt_transmit(infrared_.transmit_channel, infrared_.copy_encoder,
          symbols.data(), sizeof(symbols), &transmit_config);
  if (result == ESP_OK) {
    result = rmt_tx_wait_all_done(
        infrared_.transmit_channel, kInfraredTransmitTimeoutMs);
  }
  if (xSemaphoreTake(infrared_.mutex, pdMS_TO_TICKS(20)) == pdTRUE) {
    infrared_.status.last_error = result;
    xSemaphoreGive(infrared_.mutex);
  }
  return result == ESP_OK;
}

bool TDisplayP4AirDevice::ReadInfraredStatus(InfraredStatus* status) {
  if (status == nullptr) {
    return false;
  }
  if (infrared_.mutex == nullptr ||
      xSemaphoreTake(infrared_.mutex, pdMS_TO_TICKS(20)) != pdTRUE) {
    return false;
  }

  if (infrared_.receive_complete.exchange(false)) {
    uint8_t address = infrared_.status.address;
    uint8_t command = infrared_.status.command;
    const NecDecodeResult decode_result =
        DecodeNecSymbols(infrared_.receive_symbols,
            infrared_.received_symbol_count.load(), &address, &command);
    if (decode_result == NecDecodeResult::kFrame) {
      infrared_.status.frame_received = true;
      infrared_.status.repeat = false;
      infrared_.status.address = address;
      infrared_.status.command = command;
      ++infrared_.status.receive_count;
      infrared_.status.last_error = 0;
    } else if (decode_result == NecDecodeResult::kRepeat &&
               infrared_.status.frame_received) {
      infrared_.status.repeat = true;
      ++infrared_.status.receive_count;
      infrared_.status.last_error = 0;
    } else {
      ++infrared_.status.decode_error_count;
    }
  }
  infrared_.status.receiver_enabled = infrared_.receiver_enabled.load();
  *status = infrared_.status;
  xSemaphoreGive(infrared_.mutex);

  if (infrared_.receiver_enabled.load()) {
    StartInfraredReceive();
  }
  return true;
}

bool IRAM_ATTR TDisplayP4AirDevice::InfraredReceiveDoneCallback(
    rmt_channel_handle_t channel, const rmt_rx_done_event_data_t* event_data,
    void* context) {
  (void)channel;
  auto* self = static_cast<TDisplayP4AirDevice*>(context);
  if (self == nullptr || event_data == nullptr) {
    return false;
  }
  if (!self->infrared_.receiver_enabled.load()) {
    self->infrared_.receive_pending.store(false);
    self->infrared_.receive_complete.store(false);
    return false;
  }
  self->infrared_.received_symbol_count.store(std::min<size_t>(
      event_data->num_symbols, std::size(self->infrared_.receive_symbols)));
  self->infrared_.receive_pending.store(false);
  self->infrared_.receive_complete.store(true);
  return false;
}

bool TDisplayP4AirDevice::InitializeInfraredHardware() {
  if (infrared_.mutex == nullptr) {
    return false;
  }
  if (xSemaphoreTake(infrared_.mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
    return false;
  }
  if (infrared_.status.hardware_ready) {
    xSemaphoreGive(infrared_.mutex);
    return true;
  }

  esp_err_t result = ESP_OK;
  rmt_rx_channel_config_t receive_config = {};
  receive_config.gpio_num = static_cast<gpio_num_t>(gpio::infrared::kRx);
  receive_config.clk_src = RMT_CLK_SRC_DEFAULT;
  receive_config.resolution_hz = kInfraredRmtResolutionHz;
  receive_config.mem_block_symbols = std::size(infrared_.receive_symbols);
  receive_config.flags.with_dma = false;
  result = rmt_new_rx_channel(&receive_config, &infrared_.receive_channel);

  if (result == ESP_OK) {
    rmt_rx_event_callbacks_t callbacks = {};
    callbacks.on_recv_done = InfraredReceiveDoneCallback;
    result = rmt_rx_register_event_callbacks(
        infrared_.receive_channel, &callbacks, this);
  }

  if (result == ESP_OK) {
    rmt_tx_channel_config_t transmit_config = {};
    transmit_config.gpio_num = static_cast<gpio_num_t>(gpio::infrared::kTx);
    transmit_config.clk_src = RMT_CLK_SRC_DEFAULT;
    transmit_config.resolution_hz = kInfraredRmtResolutionHz;
    transmit_config.mem_block_symbols = 64;
    transmit_config.trans_queue_depth = 4;
    transmit_config.flags.with_dma = false;
    result = rmt_new_tx_channel(&transmit_config, &infrared_.transmit_channel);
  }

  if (result == ESP_OK) {
    rmt_copy_encoder_config_t encoder_config = {};
    result = rmt_new_copy_encoder(&encoder_config, &infrared_.copy_encoder);
  }

  if (result == ESP_OK) {
    rmt_carrier_config_t carrier_config = {};
    carrier_config.frequency_hz = 38000;
    carrier_config.duty_cycle = 0.33F;
    carrier_config.flags.polarity_active_low = false;
    carrier_config.flags.always_on = false;
    result = rmt_apply_carrier(infrared_.transmit_channel, &carrier_config);
  }
  if (result == ESP_OK) {
    result = rmt_enable(infrared_.receive_channel);
    infrared_.receive_channel_enabled = result == ESP_OK;
  }
  if (result == ESP_OK) {
    result = rmt_enable(infrared_.transmit_channel);
  }

  if (result != ESP_OK) {
    if (infrared_.receive_channel != nullptr) {
      rmt_disable(infrared_.receive_channel);
      rmt_del_channel(infrared_.receive_channel);
      infrared_.receive_channel = nullptr;
    }
    infrared_.receive_channel_enabled = false;
    if (infrared_.transmit_channel != nullptr) {
      rmt_disable(infrared_.transmit_channel);
      rmt_del_channel(infrared_.transmit_channel);
      infrared_.transmit_channel = nullptr;
    }
    if (infrared_.copy_encoder != nullptr) {
      rmt_del_encoder(infrared_.copy_encoder);
      infrared_.copy_encoder = nullptr;
    }
  }
  infrared_.status.hardware_ready = result == ESP_OK;
  infrared_.status.last_error = result;
  xSemaphoreGive(infrared_.mutex);
  return result == ESP_OK;
}

bool TDisplayP4AirDevice::StartInfraredReceive() {
  if (!infrared_.receiver_enabled.load() ||
      infrared_.receive_channel == nullptr) {
    return false;
  }
  bool expected = false;
  if (!infrared_.receive_pending.compare_exchange_strong(expected, true)) {
    return true;
  }

  infrared_.received_symbol_count.store(0);
  rmt_receive_config_t receive_config = {};
  receive_config.signal_range_min_ns = kInfraredReceiveMinimumNs;
  receive_config.signal_range_max_ns = kInfraredReceiveMaximumNs;
  const esp_err_t result =
      rmt_receive(infrared_.receive_channel, infrared_.receive_symbols,
          sizeof(infrared_.receive_symbols), &receive_config);
  if (result != ESP_OK) {
    infrared_.receive_pending.store(false);
    if (xSemaphoreTake(infrared_.mutex, pdMS_TO_TICKS(20)) == pdTRUE) {
      infrared_.status.last_error = result;
      xSemaphoreGive(infrared_.mutex);
    }
  }
  return result == ESP_OK;
}

}  // namespace lilygo_box::hal
