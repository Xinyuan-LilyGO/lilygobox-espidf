#include "hal/device/t_display_p4/v2/fast_charge.h"

#include <atomic>
#include <memory>

#include "base/logger.h"
#include "base/task.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "hal/device/t_display_p4/device.h"

namespace lilygo_box::hal::p4_v2_fast_charge {
namespace {

constexpr uint32_t kStopTimeoutMs = 5000;

struct Service {
  std::unique_ptr<cpp_bus_driver::Axp517Sink> sink;
  SemaphoreHandle_t mutex = nullptr;
  SemaphoreHandle_t stopped = nullptr;
  TaskHandle_t task = nullptr;
  std::atomic<bool> stop_requested{false};
  Snapshot snapshot;
};

Service& GetService() {
  static Service* const service = new Service;
  return *service;
}

void Run(void* context) {
  auto& service = *static_cast<Service*>(context);
  auto& driver = lilygo_device_driver::TDisplayP4Driver::GetInstance();

  while (!service.stop_requested.load() &&
         (!driver.IsAxp517Ready() || driver.chip().axp517 == nullptr)) {
    vTaskDelay(pdMS_TO_TICKS(50));
  }

  if (!service.stop_requested.load()) {
    const auto* board_config = driver.battery_info().default_pd_config;
    if (board_config != nullptr) {
      auto config = *board_config;
      // PD negotiates USB input power only; preserve the driver's battery
      // charge-current configuration unchanged.
      config.manage_charge_current = false;
      service.sink = std::make_unique<cpp_bus_driver::Axp517Sink>(
          *driver.chip().axp517, config);

      while (!service.stop_requested.load()) {
        const uint64_t now_ms =
            static_cast<uint64_t>(esp_timer_get_time() / 1000);
        const bool success = service.sink->Poll(now_ms, true);
        const auto& sink_status = service.sink->status();
        const auto state = sink_status.state;
        xSemaphoreTake(service.mutex, portMAX_DELAY);
        service.snapshot.state =
            state == cpp_bus_driver::Axp517Sink::State::kError
                ? Snapshot::State::kFailed
            : state == cpp_bus_driver::Axp517Sink::State::kReady
                ? (sink_status.pps ? Snapshot::State::kPps
                                   : Snapshot::State::kPd)
            : !sink_status.attached ? Snapshot::State::kDisconnected
                                    : Snapshot::State::kNegotiating;
        service.snapshot.voltage_mv = sink_status.voltage_mv;
        service.snapshot.current_ma = sink_status.current_ma;
        xSemaphoreGive(service.mutex);
        const uint32_t delay_ms =
            !success ? 100
            : state == cpp_bus_driver::Axp517Sink::State::kReady ? 10
            : state == cpp_bus_driver::Axp517Sink::State::kDisabled ? 20
                                                                    : 2;
        const TickType_t delay = pdMS_TO_TICKS(delay_ms);
        vTaskDelay(delay > 0 ? delay : 1);
      }

      // Release any negotiated high-voltage contract before driver teardown.
      service.sink->Poll(
          static_cast<uint64_t>(esp_timer_get_time() / 1000), false);
      service.sink.reset();
    }
  }

  xSemaphoreTake(service.mutex, portMAX_DELAY);
  service.snapshot.state = Snapshot::State::kUnavailable;
  service.snapshot.voltage_mv = 0;
  service.snapshot.current_ma = 0;
  xSemaphoreGive(service.mutex);

  service.task = nullptr;
  xSemaphoreGive(service.stopped);
  vTaskDelete(nullptr);
}

}  // namespace

bool Start() {
  auto& service = GetService();
  if (service.task != nullptr) {
    return true;
  }
  if (service.stopped == nullptr) {
    service.stopped = xSemaphoreCreateBinary();
  }
  if (service.mutex == nullptr) {
    service.mutex = xSemaphoreCreateMutex();
  }
  if (service.stopped == nullptr || service.mutex == nullptr) {
    return false;
  }
  xSemaphoreTake(service.mutex, portMAX_DELAY);
  service.snapshot = {.state = Snapshot::State::kUnavailable};
  xSemaphoreGive(service.mutex);
  xSemaphoreTake(service.stopped, 0);
  service.stop_requested.store(false);
  if (tasks::CreateInternal(Run, "p4_pd_sink", 6144, &service,
          tskIDLE_PRIORITY + 3, &service.task) != pdPASS) {
    service.task = nullptr;
    return false;
  }
  return true;
}

bool Stop() {
  auto& service = GetService();
  if (service.task == nullptr) {
    return true;
  }
  service.stop_requested.store(true);
  if (xSemaphoreTake(service.stopped, pdMS_TO_TICKS(kStopTimeoutMs)) !=
      pdTRUE) {
    return false;
  }
  return true;
}

bool ReadSnapshot(Snapshot* snapshot) {
  auto& service = GetService();
  if (snapshot == nullptr || service.mutex == nullptr) {
    return false;
  }
  xSemaphoreTake(service.mutex, portMAX_DELAY);
  *snapshot = service.snapshot;
  xSemaphoreGive(service.mutex);
  return true;
}

}  // namespace lilygo_box::hal::p4_v2_fast_charge
