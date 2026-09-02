/*
 * @Description: T-Display-P4-Air ST25R3916 NFC 硬件实现
 * @Author: LILYGO_L
 * @Date: 2026-08-28 00:00:00
 * @LastEditTime: 2026-09-02 17:53:36
 * @License: GPL 3.0
 */
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>

#include "base/logger.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "hal/device/common/st25r3916_nfc.h"
#include "hal/device/t_display_p4_air/device.h"

extern "C" {
#include "rfal_chip.h"
#include "rfal_nfc.h"
#include "rfal_nfca.h"
#include "st25r3916_com.h"
}

namespace lilygo_box::hal {
namespace gpio = lilygo_device_driver::t_display_p4_air::gpio;
namespace {

constexpr uint32_t kNfcPollingTaskStackBytes = 6 * 1024;
constexpr UBaseType_t kNfcPollingTaskPriority = 3;
constexpr uint32_t kNfcTaskStopTimeoutMs = 2000;
// 以下参数只用于 Debug 日志和射频诊断。
constexpr uint32_t kNfcDebugStatusLogIntervalMs = 5000;
constexpr uint32_t kNfcDebugDiagnosticGuardTimeoutMs = 20;

/**
 * @brief 在 Debug 日志启用时执行 NFC 射频诊断
 * @param nfc_driver ST25R3916 驱动
 */
void RunNfcDebugDiagnostics(
    stsw_st25rfal002_cpp_bus_driver::St25r3916x& nfc_driver) {
  if (!ShouldLog(LogLevel::kDebug)) {
    return;
  }

  LogMessage(LogLevel::kDebug, __FILE__, __LINE__,
      "NFC reader detected (identity: 0x%02X, revision: %u, chip: %s)\n",
      static_cast<unsigned>(nfc_driver.chip_identity()),
      static_cast<unsigned>(nfc_driver.chip_revision()),
      nfc_driver.is_st25r3916b() ? "ST25R3916B" : "ST25R3916");

  const ReturnCode init_result = rfalNfcaPollerInitialize();
  ReturnCode field_result = RFAL_ERR_INVALID_HANDLE;
  ReturnCode amplitude_result = RFAL_ERR_INVALID_HANDLE;
  ReturnCode probe_result = RFAL_ERR_INVALID_HANDLE;
  uint8_t amplitude = 0;
  uint8_t op_control = 0;
  uint8_t aux_display = 0;
  uint8_t tx_driver = 0;
  uint8_t field_threshold = 0;
  rfalNfcaSensRes sens_res = {};

  if (init_result == RFAL_ERR_NONE) {
    field_result = rfalFieldOnAndStartGT();
  }
  if (field_result == RFAL_ERR_NONE) {
    const TickType_t guard_start_tick = xTaskGetTickCount();
    while (!rfalIsGTExpired() &&
           xTaskGetTickCount() - guard_start_tick <
               pdMS_TO_TICKS(kNfcDebugDiagnosticGuardTimeoutMs)) {
      nfc_driver.Worker();
      vTaskDelay(pdMS_TO_TICKS(1));
    }
    amplitude_result = rfalChipMeasureAmplitude(&amplitude);
    probe_result =
        rfalNfcaPollerTechnologyDetection(RFAL_COMPLIANCE_MODE_ISO, &sens_res);
  }

  st25r3916ReadRegister(ST25R3916_REG_OP_CONTROL, &op_control);
  st25r3916ReadRegister(ST25R3916_REG_AUX_DISPLAY, &aux_display);
  st25r3916ReadRegister(ST25R3916_REG_TX_DRIVER, &tx_driver);
  st25r3916ReadRegister(ST25R3916_REG_FIELD_THRESHOLD_ACTV, &field_threshold);
  rfalFieldOff();

  LogMessage(LogLevel::kDebug, __FILE__, __LINE__,
      "NFC RF diagnostic (init: %u, field: %u, amplitude result: %u, "
      "amplitude: %u, NFC-A probe: %u, ATQA: %02X%02X, op: 0x%02X, "
      "aux: 0x%02X, tx driver: 0x%02X, field threshold: 0x%02X)\n",
      static_cast<unsigned>(init_result), static_cast<unsigned>(field_result),
      static_cast<unsigned>(amplitude_result), static_cast<unsigned>(amplitude),
      static_cast<unsigned>(probe_result),
      static_cast<unsigned>(sens_res.anticollisionInfo),
      static_cast<unsigned>(sens_res.platformInfo),
      static_cast<unsigned>(op_control), static_cast<unsigned>(aux_display),
      static_cast<unsigned>(tx_driver), static_cast<unsigned>(field_threshold));
}

}  // namespace

bool TDisplayP4AirDevice::SetNfcPollingEnabled(bool enabled) {
  if (!enabled) {
    nfc_.stop_requested.store(true);
    for (uint32_t elapsed_ms = 0;
        elapsed_ms < kNfcTaskStopTimeoutMs && nfc_.task_active.load();
        elapsed_ms += kPowerOffTaskPollMs) {
      vTaskDelay(pdMS_TO_TICKS(kPowerOffTaskPollMs));
    }
    return !nfc_.task_active.load();
  }

  if (nfc_.task_active.load()) {
    return true;
  }
  if (nfc_.mutex == nullptr || driver_.chip().st25r3916 == nullptr) {
    LogMessage(LogLevel::kError, __FILE__, __LINE__,
        "Start NFC polling failed: resources are unavailable\n");
    return false;
  }

  if (xSemaphoreTake(nfc_.mutex, pdMS_TO_TICKS(20)) != pdTRUE) {
    return false;
  }
  nfc_.status = NfcStatus();
  nfc_.status.polling = true;
  xSemaphoreGive(nfc_.mutex);

  nfc_.stop_requested.store(false);
  nfc_.task_active.store(true);
  const BaseType_t task_result = xTaskCreate(NfcPollingTaskEntry, "nfc_poll",
      kNfcPollingTaskStackBytes, this, kNfcPollingTaskPriority, nullptr);
  if (task_result != pdPASS) {
    nfc_.task_active.store(false);
    if (xSemaphoreTake(nfc_.mutex, pdMS_TO_TICKS(20)) == pdTRUE) {
      nfc_.status.polling = false;
      nfc_.status.last_error = ESP_ERR_NO_MEM;
      xSemaphoreGive(nfc_.mutex);
    }
    LogMessage(LogLevel::kError, __FILE__, __LINE__,
        "Create NFC polling task failed\n");
    return false;
  }
  LogMessage(
      LogLevel::kDebug, __FILE__, __LINE__, "NFC polling task started\n");
  return true;
}

bool TDisplayP4AirDevice::ReadNfcStatus(NfcStatus* status) {
  if (status == nullptr) {
    return false;
  }
  if (nfc_.mutex == nullptr ||
      xSemaphoreTake(nfc_.mutex, pdMS_TO_TICKS(20)) != pdTRUE) {
    return false;
  }
  *status = nfc_.status;
  status->hardware_ready = driver_.IsSt25r3916Ready();
  status->polling = nfc_.task_active.load() && !nfc_.stop_requested.load();
  xSemaphoreGive(nfc_.mutex);
  return true;
}

void TDisplayP4AirDevice::NfcPollingTaskEntry(void* context) {
  auto* self = static_cast<TDisplayP4AirDevice*>(context);
  if (self != nullptr) {
    self->RunNfcPollingTask();
  }
  vTaskDelete(nullptr);
}

void TDisplayP4AirDevice::RunNfcPollingTask() {
  auto* const nfc_driver = driver_.chip().st25r3916.get();
  const bool initialized = nfc_driver != nullptr && driver_.InitSt25r3916() &&
                           driver_.IsSt25r3916Ready();
  if (!initialized) {
    const auto& driver_status = driver_.status().st25r3916;
    int error = RFAL_ERR_INTERNAL;
    if (driver_status.result != RFAL_ERR_NONE) {
      error = static_cast<int>(driver_status.result);
    } else if (driver_status.platform_error !=
               stsw_st25rfal002_cpp_bus_driver::PlatformError::kNone) {
      error = st25r3916_nfc::kPlatformErrorBase +
              static_cast<int>(driver_status.platform_error);
    }
    if (xSemaphoreTake(nfc_.mutex, pdMS_TO_TICKS(20)) == pdTRUE) {
      nfc_.status.hardware_ready = false;
      nfc_.status.polling = false;
      nfc_.status.last_error = error;
      xSemaphoreGive(nfc_.mutex);
    }
    LogMessage(LogLevel::kError, __FILE__, __LINE__,
        "Start NFC polling failed (RFAL: %u, platform: %u)\n",
        static_cast<unsigned>(driver_status.result),
        static_cast<unsigned>(driver_status.platform_error));
    nfc_.task_active.store(false);
    return;
  }

  if (xSemaphoreTake(nfc_.mutex, pdMS_TO_TICKS(20)) == pdTRUE) {
    nfc_.status.hardware_ready = true;
    nfc_.status.last_error = 0;
    xSemaphoreGive(nfc_.mutex);
  }

  const bool debug_logging_enabled = ShouldLog(LogLevel::kDebug);
  RunNfcDebugDiagnostics(*nfc_driver);

  rfalNfcDiscoverParam parameters =
      st25r3916_nfc::CreateNfcDiscoveryParameters();
  ReturnCode result = rfalNfcDiscover(&parameters);
  if (result != RFAL_ERR_NONE) {
    if (xSemaphoreTake(nfc_.mutex, pdMS_TO_TICKS(20)) == pdTRUE) {
      nfc_.status.hardware_ready = false;
      nfc_.status.polling = false;
      nfc_.status.last_error = static_cast<int>(result);
      xSemaphoreGive(nfc_.mutex);
    }
    LogMessage(LogLevel::kError, __FILE__, __LINE__,
        "Start NFC discovery failed (RFAL: %u)\n",
        static_cast<unsigned>(result));
    driver_.DeinitSt25r3916();
    nfc_.task_active.store(false);
    return;
  }
  LogMessage(LogLevel::kDebug, __FILE__, __LINE__,
      "NFC discovery started (duration: %u ms, removal: %u ms, "
      "hold: %u ms)\n",
      static_cast<unsigned>(st25r3916_nfc::kDiscoveryDurationMs),
      static_cast<unsigned>(st25r3916_nfc::kCardRemovalTimeoutMs),
      static_cast<unsigned>(st25r3916_nfc::kActiveDeviceHoldMs));

  TickType_t last_card_tick = xTaskGetTickCount();
  TickType_t last_debug_log_tick = last_card_tick;
  rfalNfcState previous_debug_state = rfalNfcGetState();
  uint32_t debug_discovery_cycle_count = 0;
  bool card_present = false;
  NfcTechnology last_technology = NfcTechnology::kUnknown;
  std::array<uint8_t, kNfcIdentifierCapacity> last_identifier = {};
  size_t last_identifier_length = 0;
  while (!nfc_.stop_requested.load()) {
    nfc_driver->NfcWorker();
    const auto platform_error = nfc_driver->platform_error();
    if (platform_error !=
        stsw_st25rfal002_cpp_bus_driver::PlatformError::kNone) {
      if (xSemaphoreTake(nfc_.mutex, pdMS_TO_TICKS(20)) == pdTRUE) {
        nfc_.status.last_error = st25r3916_nfc::kPlatformErrorBase +
                                 static_cast<int>(platform_error);
        xSemaphoreGive(nfc_.mutex);
      }
      LogMessage(LogLevel::kError, __FILE__, __LINE__,
          "NFC platform failure (platform: %u)\n",
          static_cast<unsigned>(platform_error));
      break;
    }

    const rfalNfcState state = rfalNfcGetState();
    if (rfalNfcIsDevActivated(state)) {
      rfalNfcDevice* active_device = nullptr;
      result = rfalNfcGetActiveDevice(&active_device);
      if (result != RFAL_ERR_NONE || active_device == nullptr) {
        const ReturnCode error =
            result == RFAL_ERR_NONE ? RFAL_ERR_INTERNAL : result;
        if (xSemaphoreTake(nfc_.mutex, pdMS_TO_TICKS(20)) == pdTRUE) {
          nfc_.status.last_error = static_cast<int>(error);
          xSemaphoreGive(nfc_.mutex);
        }
        LogMessage(LogLevel::kError, __FILE__, __LINE__,
            "Read active NFC device failed (RFAL: %u, device: %s)\n",
            static_cast<unsigned>(error),
            active_device == nullptr ? "null" : "ready");
      } else {
        const size_t identifier_length =
            active_device->nfcid == nullptr
                ? 0
                : std::min<size_t>(
                      active_device->nfcidLen, kNfcIdentifierCapacity);
        const NfcTechnology technology =
            st25r3916_nfc::ToNfcTechnology(active_device->type);
        const bool same_card =
            card_present && last_technology == technology &&
            last_identifier_length == identifier_length &&
            (identifier_length == 0 ||
                std::memcmp(last_identifier.data(), active_device->nfcid,
                    identifier_length) == 0);

        NfcStatus detected_status;
        if (!same_card) {
          detected_status.hardware_ready = true;
          detected_status.polling = true;
          detected_status.card_present = true;
          detected_status.technology = technology;
          detected_status.identifier_length = identifier_length;
          if (identifier_length > 0) {
            std::memcpy(detected_status.identifier, active_device->nfcid,
                identifier_length);
          }
          st25r3916_nfc::PopulateNfcTagDetails(
              *active_device, &detected_status);
        }

        uint32_t detection_count = 0;
        bool status_updated = false;
        if (xSemaphoreTake(nfc_.mutex, pdMS_TO_TICKS(20)) == pdTRUE) {
          if (same_card) {
            nfc_.status.card_present = true;
            nfc_.status.last_error = 0;
          } else {
            detected_status.detection_count = nfc_.status.detection_count + 1;
            nfc_.status = detected_status;
          }
          detection_count = nfc_.status.detection_count;
          xSemaphoreGive(nfc_.mutex);
          status_updated = true;
        }

        if (status_updated) {
          card_present = true;
          last_card_tick = xTaskGetTickCount();
        }
        if (status_updated && !same_card) {
          last_technology = technology;
          last_identifier_length = identifier_length;
          last_identifier.fill(0);
          if (identifier_length > 0) {
            std::memcpy(last_identifier.data(), active_device->nfcid,
                identifier_length);
          }
          LogMessage(LogLevel::kDebug, __FILE__, __LINE__,
              "NFC tag detected (type: %u, identifier length: %u, "
              "count: %u, NDEF: %s, content error: %d)\n",
              static_cast<unsigned>(active_device->type),
              static_cast<unsigned>(active_device->nfcidLen),
              static_cast<unsigned>(detection_count),
              detected_status.ndef_present ? "present" : "none",
              detected_status.content_error);
        }
      }
      vTaskDelay(pdMS_TO_TICKS(st25r3916_nfc::kActiveDeviceHoldMs));

      result = rfalNfcDeactivate(RFAL_NFC_DEACTIVATE_DISCOVERY);
      if (result != RFAL_ERR_NONE &&
          xSemaphoreTake(nfc_.mutex, pdMS_TO_TICKS(20)) == pdTRUE) {
        nfc_.status.last_error = static_cast<int>(result);
        xSemaphoreGive(nfc_.mutex);
      }
      if (result != RFAL_ERR_NONE) {
        LogMessage(LogLevel::kError, __FILE__, __LINE__,
            "Restart NFC discovery failed (RFAL: %u)\n",
            static_cast<unsigned>(result));
        vTaskDelay(pdMS_TO_TICKS(st25r3916_nfc::kDiscoveryRestartDelayMs));
      }
    } else if (card_present && rfalNfcIsInDiscovery(state) &&
               xTaskGetTickCount() - last_card_tick >=
                   pdMS_TO_TICKS(st25r3916_nfc::kCardRemovalTimeoutMs) &&
               xSemaphoreTake(nfc_.mutex, pdMS_TO_TICKS(20)) == pdTRUE) {
      nfc_.status.card_present = false;
      xSemaphoreGive(nfc_.mutex);
      card_present = false;
    }

    if (debug_logging_enabled) {
      const TickType_t now = xTaskGetTickCount();
      if (state != previous_debug_state) {
        if (state == RFAL_NFC_STATE_START_DISCOVERY) {
          ++debug_discovery_cycle_count;
        }
        previous_debug_state = state;
      }
      if (now - last_debug_log_tick >=
          pdMS_TO_TICKS(kNfcDebugStatusLogIntervalMs)) {
        LogMessage(LogLevel::kDebug, __FILE__, __LINE__,
            "NFC discovery active (state: %u, cycles: %u, interrupt: %s)\n",
            static_cast<unsigned>(state),
            static_cast<unsigned>(debug_discovery_cycle_count),
            tool_ != nullptr && tool_->GpioRead(gpio::st25r3916::kInt) ? "high"
                                                                       : "low");
        last_debug_log_tick = now;
      }
    }
    vTaskDelay(pdMS_TO_TICKS(1));
  }

  rfalNfcDeactivate(RFAL_NFC_DEACTIVATE_IDLE);
  for (int worker_count = 0; worker_count < 20; ++worker_count) {
    nfc_driver->NfcWorker();
    if (rfalNfcGetState() == RFAL_NFC_STATE_IDLE) {
      break;
    }
    vTaskDelay(pdMS_TO_TICKS(1));
  }
  if (xSemaphoreTake(nfc_.mutex, pdMS_TO_TICKS(20)) == pdTRUE) {
    nfc_.status.polling = false;
    nfc_.status.card_present = false;
    nfc_.status.hardware_ready = false;
    xSemaphoreGive(nfc_.mutex);
  }
  const bool deinitialized = driver_.DeinitSt25r3916();
  nfc_.task_active.store(false);
  LogMessage(deinitialized ? LogLevel::kDebug : LogLevel::kError, __FILE__,
      __LINE__,
      deinitialized ? "NFC polling task stopped\n"
                    : "NFC polling task cleanup failed\n");
}

}  // namespace lilygo_box::hal
