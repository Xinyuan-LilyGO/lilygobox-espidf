/*
 * @Description: MSC 与 ECM 共用的 USB Host 生命周期实现
 * @Author: LILYGO_L
 * @License: GPL 3.0
 */
#include "hal/usb/usb_host_service.h"

#include <atomic>
#include <cstdint>
#include <mutex>

#include "base/logger.h"
#include "esp_intr_alloc.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"
#include "usb/usb_host.h"

namespace lilygo_box::hal::usb_host_service {
namespace {
std::mutex mutex;
unsigned users = 0;
std::atomic<bool> installed{false};
std::atomic<bool> stopping{false};

#if defined(CONFIG_LILYGO_DEVICE_DRIVER_T_DISPLAY_P4) && \
    defined(CONFIG_LILYGO_DEVICE_DRIVER_DEVICE_VERSION_V2)
#if !CONFIG_USB_HOST_ENABLE_ENUM_FILTER_CALLBACK || \
    !CONFIG_USB_HOST_HUBS_SUPPORTED
#error "P4 V2 Ethernet requires USB Host hubs and enumeration filter callback"
#endif

/**
 * @brief 为 RTL8152B 选择 ECM 配置，其他 USB 设备使用默认配置
 * @param descriptor USB 设备描述符
 * @param value 选中的配置编号输出地址
 * @return 配置可用返回 true，否则返回 false
 */
bool SelectConfiguration(const usb_device_desc_t* descriptor, uint8_t* value) {
  if (descriptor == nullptr || value == nullptr) {
    return false;
  }
  if (descriptor->idVendor == 0x0BDA && descriptor->idProduct == 0x8152) {
    // RTL8152B 配置 1 是厂商协议，配置 2 才是 CDC-ECM。
    if (descriptor->bNumConfigurations < 2) {
      return false;
    }
    *value = 2;
  } else {
    *value = 1;
  }
  return true;
}
#endif

/**
 * @brief 持续处理共享 Host 事件，并在最后一个使用者退出后卸载 Host
 */
void HostTask(void*) {
  uint32_t ready = 0;
  xTaskNotifyWait(0, UINT32_MAX, &ready, portMAX_DELAY);
  if (ready != 1) {
    vTaskDelete(nullptr);
    return;
  }
  while (true) {
    uint32_t flags = 0;
    usb_host_lib_handle_events(pdMS_TO_TICKS(20), &flags);
    if (!stopping.load()) {
      continue;
    }
    // 不依赖一次性的 NO_CLIENTS/ALL_FREE 通知，兼容无设备和已拔出的情况。
    usb_host_lib_info_t info = {};
    if (usb_host_lib_info(&info) != ESP_OK || info.num_clients != 0) {
      continue;
    }
    const esp_err_t result = usb_host_device_free_all();
    if (result == ESP_OK && usb_host_uninstall() == ESP_OK) {
      installed.store(false);
      break;
    }
  }
  vTaskDelete(nullptr);
}
}  // namespace

esp_err_t Acquire() {
  std::lock_guard<std::mutex> lock(mutex);
  if (installed.load() && stopping.load()) {
    return ESP_ERR_INVALID_STATE;
  }
  if (!installed.load()) {
    // 先预留事件任务，避免 Host 安装后因内存不足留下无人处理的实例。
    TaskHandle_t task = nullptr;
    if (xTaskCreate(HostTask, "usb_host", 4096, nullptr, 5, &task) != pdPASS) {
      return ESP_ERR_NO_MEM;
    }
    usb_host_config_t config = {};
    config.intr_flags = ESP_INTR_FLAG_LEVEL1;
#if defined(CONFIG_LILYGO_DEVICE_DRIVER_T_DISPLAY_P4) && \
    defined(CONFIG_LILYGO_DEVICE_DRIVER_DEVICE_VERSION_V2)
    config.enum_filter_cb = SelectConfiguration;
#endif
    const esp_err_t result = usb_host_install(&config);
    if (result != ESP_OK) {
      xTaskNotify(task, 2, eSetValueWithOverwrite);
      return result;
    }
    stopping.store(false);
    installed.store(true);
    xTaskNotify(task, 1, eSetValueWithOverwrite);
  }
  ++users;
  return ESP_OK;
}

esp_err_t Release() {
  std::lock_guard<std::mutex> lock(mutex);
  if (users == 0) {
    return ESP_ERR_INVALID_STATE;
  }
  if (--users != 0) {
    return ESP_OK;
  }
  stopping.store(true);
  for (unsigned elapsed = 0; elapsed < 3000; elapsed += 20) {
    if (!installed.load()) {
      return ESP_OK;
    }
    vTaskDelay(pdMS_TO_TICKS(20));
  }
  // 超时后保留事件泵继续清理，不能遗留一个无人服务的 Host。
  LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
      "USB Host shutdown is still pending\n");
  return ESP_ERR_TIMEOUT;
}
}  // namespace lilygo_box::hal::usb_host_service
