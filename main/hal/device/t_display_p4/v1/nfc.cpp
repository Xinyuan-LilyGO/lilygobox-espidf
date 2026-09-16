/*
 * @Description: T-Display-P4 键盘扩展 ST25R3916 NFC Provider 实现
 * @Author: LILYGO_L
 * @Date: 2026-08-21 00:00:00
 * @LastEditTime: 2026-09-02 17:53:08
 * @License: GPL 3.0
 */
#include <algorithm>
#include <array>
#include <cstring>

#include "base/logger.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "hal/device/common/st25r3916_nfc.h"
#include "hal/device/t_display_p4/device.h"

extern "C" {
#include "rfal_nfc.h"
}

namespace lilygo_box::hal {
namespace {

constexpr uint32_t kNfcTaskStopTimeoutMs = 2000;
constexpr uint32_t kNfcTaskStopPollMs = 20;

}  // namespace

/**
 * @brief 启动或停止键盘扩展 ST25R3916 NFC 后台轮询
 * @param enabled true 启动轮询，false 停止轮询
 * @return 目标状态已满足或切换成功返回 true
 */
bool TDisplayP4Device::SetNfcPollingEnabled(bool enabled) {
  if (!enabled) {
    nfc_.stop_requested.store(true);
    for (uint32_t elapsed_ms = 0;
        elapsed_ms < kNfcTaskStopTimeoutMs && nfc_.task_active.load();
        elapsed_ms += kNfcTaskStopPollMs) {
      vTaskDelay(pdMS_TO_TICKS(kNfcTaskStopPollMs));
    }
    return !nfc_.task_active.load();
  }

  if (nfc_.task_active.load()) {
    return true;
  }
  if (nfc_.mutex == nullptr ||
      keyboard_expansion_.state.load() != KeyboardExpansionState::kReady ||
      keyboard_expansion_.st25r3916.load() !=
          KeyboardExpansionComponentState::kReady ||
      !driver_.IsSt25r3916Ready() || driver_.chip().st25r3916 == nullptr) {
    LogMessage(LogLevel::kError, __FILE__, __LINE__,
        "Start keyboard expansion NFC polling failed: hardware is "
        "unavailable\n");
    return false;
  }

  if (xSemaphoreTake(nfc_.mutex, pdMS_TO_TICKS(20)) != pdTRUE) {
    return false;
  }
  nfc_.status = NfcStatus();
  nfc_.status.hardware_ready = true;
  nfc_.status.polling = true;
  xSemaphoreGive(nfc_.mutex);

  nfc_.stop_requested.store(false);
  nfc_.task_active.store(true);
  const BaseType_t task_result =
      xTaskCreate(NfcPollingTaskEntry, "keyboard_nfc",
          kNfcPollingTaskStackBytes, this, kNfcPollingTaskPriority, nullptr);
  if (task_result != pdPASS) {
    nfc_.task_active.store(false);
    if (xSemaphoreTake(nfc_.mutex, pdMS_TO_TICKS(20)) == pdTRUE) {
      nfc_.status.polling = false;
      nfc_.status.last_error = ESP_ERR_NO_MEM;
      xSemaphoreGive(nfc_.mutex);
    }
    LogMessage(LogLevel::kError, __FILE__, __LINE__,
        "Create keyboard expansion NFC polling task failed\n");
    return false;
  }
  return true;
}

/**
 * @brief 读取键盘扩展 ST25R3916 NFC 当前状态
 * @param status NFC 状态输出地址
 * @return 状态读取成功返回 true
 */
bool TDisplayP4Device::ReadNfcStatus(NfcStatus* status) {
  if (status == nullptr || nfc_.mutex == nullptr ||
      xSemaphoreTake(nfc_.mutex, pdMS_TO_TICKS(20)) != pdTRUE) {
    return false;
  }
  *status = nfc_.status;
  status->hardware_ready =
      keyboard_expansion_.state.load() == KeyboardExpansionState::kReady &&
      keyboard_expansion_.st25r3916.load() ==
          KeyboardExpansionComponentState::kReady &&
      driver_.IsSt25r3916Ready();
  status->polling = nfc_.task_active.load() && !nfc_.stop_requested.load();
  xSemaphoreGive(nfc_.mutex);
  return true;
}

/**
 * @brief 进入键盘扩展 ST25R3916 NFC 后台轮询任务
 * @param context TDisplayP4Device 实例指针
 */
void TDisplayP4Device::NfcPollingTaskEntry(void* context) {
  auto* self = static_cast<TDisplayP4Device*>(context);
  if (self != nullptr) {
    self->RunNfcPollingTask();
  }
  vTaskDelete(nullptr);
}

/**
 * @brief 执行 NFC 发现、卡片状态更新和退出清理
 */
void TDisplayP4Device::RunNfcPollingTask() {
  auto* const nfc_driver = driver_.chip().st25r3916.get();
  const bool activated =
      nfc_driver != nullptr &&
      keyboard_expansion_.state.load() == KeyboardExpansionState::kReady &&
      driver_.IsSt25r3916Ready() &&
      driver_.SetSt25r3916OperatingMode(lilygo_device_driver::TDisplayP4Driver::
              St25r3916OperatingMode::kActive);
  if (!activated) {
    if (xSemaphoreTake(nfc_.mutex, pdMS_TO_TICKS(20)) == pdTRUE) {
      nfc_.status.hardware_ready = false;
      nfc_.status.polling = false;
      nfc_.status.last_error = RFAL_ERR_INTERNAL;
      xSemaphoreGive(nfc_.mutex);
    }
    nfc_.task_active.store(false);
    return;
  }

  rfalNfcDiscoverParam parameters =
      st25r3916_nfc::CreateNfcDiscoveryParameters();
  ReturnCode result = rfalNfcDiscover(&parameters);
  if (result != RFAL_ERR_NONE) {
    if (xSemaphoreTake(nfc_.mutex, pdMS_TO_TICKS(20)) == pdTRUE) {
      nfc_.status.polling = false;
      nfc_.status.last_error = static_cast<int>(result);
      xSemaphoreGive(nfc_.mutex);
    }
    driver_.SetSt25r3916OperatingMode(
        lilygo_device_driver::TDisplayP4Driver::St25r3916OperatingMode::kSleep);
    nfc_.task_active.store(false);
    return;
  }

  TickType_t last_card_tick = xTaskGetTickCount();
  bool card_present = false;
  NfcTechnology last_technology = NfcTechnology::kUnknown;
  std::array<uint8_t, kNfcIdentifierCapacity> last_identifier = {};
  size_t last_identifier_length = 0;
  while (!nfc_.stop_requested.load() &&
         keyboard_expansion_.state.load() == KeyboardExpansionState::kReady) {
    nfc_driver->NfcWorker();
    const auto platform_error = nfc_driver->platform_error();
    if (platform_error !=
        stsw_st25rfal002_cpp_bus_driver::PlatformError::kNone) {
      if (xSemaphoreTake(nfc_.mutex, pdMS_TO_TICKS(20)) == pdTRUE) {
        nfc_.status.last_error = st25r3916_nfc::kPlatformErrorBase +
                                 static_cast<int>(platform_error);
        xSemaphoreGive(nfc_.mutex);
      }
      break;
    }

    const rfalNfcState nfc_state = rfalNfcGetState();
    if (rfalNfcIsDevActivated(nfc_state)) {
      rfalNfcDevice* active_device = nullptr;
      result = rfalNfcGetActiveDevice(&active_device);
      if (result != RFAL_ERR_NONE || active_device == nullptr) {
        const ReturnCode error =
            result == RFAL_ERR_NONE ? RFAL_ERR_INTERNAL : result;
        if (xSemaphoreTake(nfc_.mutex, pdMS_TO_TICKS(20)) == pdTRUE) {
          nfc_.status.last_error = static_cast<int>(error);
          xSemaphoreGive(nfc_.mutex);
        }
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
          detected_status.identifier_length = identifier_length;
          if (identifier_length > 0) {
            std::memcpy(detected_status.identifier, active_device->nfcid,
                identifier_length);
          }
          st25r3916_nfc::PopulateNfcTagDetails(
              *active_device, &detected_status);
        }

        if (xSemaphoreTake(nfc_.mutex, pdMS_TO_TICKS(20)) == pdTRUE) {
          if (same_card) {
            nfc_.status.card_present = true;
            nfc_.status.last_error = 0;
          } else {
            detected_status.detection_count = nfc_.status.detection_count + 1;
            nfc_.status = detected_status;
          }
          xSemaphoreGive(nfc_.mutex);
          card_present = true;
          last_card_tick = xTaskGetTickCount();
          if (!same_card) {
            last_technology = technology;
            last_identifier_length = identifier_length;
            last_identifier.fill(0);
            if (identifier_length > 0) {
              std::memcpy(last_identifier.data(), active_device->nfcid,
                  identifier_length);
            }
          }
        }
      }

      vTaskDelay(pdMS_TO_TICKS(st25r3916_nfc::kActiveDeviceHoldMs));
      result = rfalNfcDeactivate(RFAL_NFC_DEACTIVATE_DISCOVERY);
      if (result != RFAL_ERR_NONE) {
        if (xSemaphoreTake(nfc_.mutex, pdMS_TO_TICKS(20)) == pdTRUE) {
          nfc_.status.last_error = static_cast<int>(result);
          xSemaphoreGive(nfc_.mutex);
        }
        vTaskDelay(pdMS_TO_TICKS(st25r3916_nfc::kDiscoveryRestartDelayMs));
      }
    } else if (card_present && rfalNfcIsInDiscovery(nfc_state) &&
               xTaskGetTickCount() - last_card_tick >=
                   pdMS_TO_TICKS(st25r3916_nfc::kCardRemovalTimeoutMs) &&
               xSemaphoreTake(nfc_.mutex, pdMS_TO_TICKS(20)) == pdTRUE) {
      nfc_.status.card_present = false;
      xSemaphoreGive(nfc_.mutex);
      card_present = false;
    }
    vTaskDelay(pdMS_TO_TICKS(1));
  }

  const bool expansion_ready =
      keyboard_expansion_.state.load() == KeyboardExpansionState::kReady &&
      driver_.IsSt25r3916Ready();
  if (expansion_ready) {
    rfalNfcDeactivate(RFAL_NFC_DEACTIVATE_IDLE);
    for (int worker_count = 0; worker_count < 20; ++worker_count) {
      nfc_driver->NfcWorker();
      if (rfalNfcGetState() == RFAL_NFC_STATE_IDLE) {
        break;
      }
      vTaskDelay(pdMS_TO_TICKS(1));
    }
    driver_.SetSt25r3916OperatingMode(
        lilygo_device_driver::TDisplayP4Driver::St25r3916OperatingMode::kSleep);
  }

  if (xSemaphoreTake(nfc_.mutex, pdMS_TO_TICKS(20)) == pdTRUE) {
    nfc_.status.polling = false;
    nfc_.status.card_present = false;
    nfc_.status.hardware_ready = expansion_ready;
    xSemaphoreGive(nfc_.mutex);
  }
  nfc_.task_active.store(false);
}

}  // namespace lilygo_box::hal
