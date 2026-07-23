/*
 * @Description: USB Host MSC 存储管理器
 * @Author: LILYGO_L
 * @License: GPL 3.0
 */
#include "hal/usb/usb_storage_manager.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <utility>

#include "base/logger.h"
#include "esp_err.h"
#include "esp_intr_alloc.h"
#include "esp_vfs_fat.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "usb/msc_host.h"
#include "usb/msc_host_vfs.h"
#include "usb/usb_host.h"

namespace lilygo_box::hal {
namespace {

constexpr char kUsbMountPathPrefix[] = "/usb";
constexpr int kUsbStorageTaskStackBytes = 6144;
constexpr int kUsbStorageTaskPriority = 4;
constexpr int kMscEventTaskStackBytes = 4096;
constexpr int kMscEventTaskPriority = 5;
constexpr int kUsbEventPollMs = 20;
constexpr int kUsbStopTimeoutMs = 4000;
constexpr int kUsbStopPollMs = 20;
constexpr size_t kUsbEventQueueLength =
    kMaxUsbStorageDeviceCount * 2 + 2;

enum class UsbStorageEventType : uint8_t {
  kConnected,
  kDisconnected,
};

struct UsbStorageEvent {
  UsbStorageEventType type = UsbStorageEventType::kConnected;
  uint8_t usb_address = 0;
  msc_host_device_handle_t device_handle = nullptr;
};

struct UsbStorageDeviceEntry {
  uint32_t id = 0;
  uint8_t usb_address = 0;
  msc_host_device_handle_t device_handle = nullptr;
  msc_host_vfs_handle_t vfs_handle = nullptr;
  char name[kUsbStorageNameSize] = {};
  char base_path[kUsbStorageBasePathSize] = {};
};

}  // namespace

struct UsbStorageManagerState {
  explicit UsbStorageManagerState(
      UsbStorageManager::HostStoppedCallback host_stopped_callback_value)
      : host_stopped_callback(std::move(host_stopped_callback_value)) {}

  UsbStorageManager::HostStoppedCallback host_stopped_callback;
  SemaphoreHandle_t mutex = nullptr;
  QueueHandle_t event_queue = nullptr;
  std::atomic<bool> start_requested{false};
  std::atomic<bool> stop_requested{false};
  std::atomic<bool> running{false};
  std::atomic<bool> start_failed{false};
  std::atomic<uint32_t> generation{0};
  uint32_t next_device_id = 1;
  std::array<UsbStorageDeviceEntry, kMaxUsbStorageDeviceCount> devices = {};
};

namespace {

class SemaphoreLock {
 public:
  explicit SemaphoreLock(SemaphoreHandle_t semaphore)
      : semaphore_(semaphore),
        locked_(semaphore_ != nullptr &&
                xSemaphoreTake(semaphore_, portMAX_DELAY) == pdTRUE) {}

  ~SemaphoreLock() {
    if (locked_) {
      xSemaphoreGive(semaphore_);
    }
  }

  bool locked() const { return locked_; }

 private:
  SemaphoreHandle_t semaphore_ = nullptr;
  bool locked_ = false;
};

int FindFreeDeviceSlot(const UsbStorageManagerState& state) {
  for (size_t index = 0; index < state.devices.size(); ++index) {
    if (state.devices[index].device_handle == nullptr) {
      return static_cast<int>(index);
    }
  }
  return -1;
}

int FindDeviceSlot(
    const UsbStorageManagerState& state,
    msc_host_device_handle_t device_handle) {
  for (size_t index = 0; index < state.devices.size(); ++index) {
    if (state.devices[index].device_handle == device_handle) {
      return static_cast<int>(index);
    }
  }
  return -1;
}

void MarkSnapshotChanged(UsbStorageManagerState* state) {
  if (state != nullptr) {
    state->generation.fetch_add(1);
  }
}

void ReleaseUsbDevice(UsbStorageManagerState* state, int slot) {
  if (state == nullptr || slot < 0 ||
      slot >= static_cast<int>(state->devices.size())) {
    return;
  }

  msc_host_device_handle_t device_handle = nullptr;
  msc_host_vfs_handle_t vfs_handle = nullptr;
  {
    SemaphoreLock lock(state->mutex);
    if (!lock.locked()) {
      return;
    }
    UsbStorageDeviceEntry& device = state->devices[slot];
    device_handle = device.device_handle;
    vfs_handle = device.vfs_handle;
    if (device_handle == nullptr) {
      return;
    }
    device = UsbStorageDeviceEntry{};
    MarkSnapshotChanged(state);
  }

  if (vfs_handle != nullptr) {
    const esp_err_t result = msc_host_vfs_unregister(vfs_handle);
    if (result != ESP_OK) {
      LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
          "Unmount USB storage failed: %s\n", esp_err_to_name(result));
    }
  }
  const esp_err_t result = msc_host_uninstall_device(device_handle);
  if (result != ESP_OK) {
    LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
        "Uninstall USB storage device failed: %s\n",
        esp_err_to_name(result));
  }
}

void ReleaseAllUsbDevices(UsbStorageManagerState* state) {
  if (state == nullptr) {
    return;
  }
  for (size_t index = 0; index < state->devices.size(); ++index) {
    ReleaseUsbDevice(state, static_cast<int>(index));
  }
}

void MountUsbDevice(UsbStorageManagerState* state, uint8_t usb_address) {
  if (state == nullptr || state->stop_requested.load()) {
    return;
  }

  int slot = -1;
  {
    SemaphoreLock lock(state->mutex);
    if (lock.locked()) {
      slot = FindFreeDeviceSlot(*state);
    }
  }
  if (slot < 0) {
    LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
        "No free USB storage slot\n");
    return;
  }

  msc_host_device_handle_t device_handle = nullptr;
  esp_err_t result =
      msc_host_install_device(usb_address, &device_handle);
  if (result != ESP_OK) {
    LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
        "Install USB storage device failed: %s\n",
        esp_err_to_name(result));
    return;
  }

  char base_path[kUsbStorageBasePathSize] = {};
  std::snprintf(
      base_path, sizeof(base_path), "%s%d", kUsbMountPathPrefix, slot);
  esp_vfs_fat_mount_config_t mount_config = {};
  mount_config.format_if_mount_failed = false;
  mount_config.max_files = 5;
  mount_config.allocation_unit_size = 8192;
  msc_host_vfs_handle_t vfs_handle = nullptr;
  result = msc_host_vfs_register(
      device_handle, base_path, &mount_config, &vfs_handle);
  if (result != ESP_OK) {
    LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
        "Mount USB storage failed: %s\n", esp_err_to_name(result));
    msc_host_uninstall_device(device_handle);
    return;
  }

  {
    SemaphoreLock lock(state->mutex);
    if (!lock.locked() || state->stop_requested.load()) {
      msc_host_vfs_unregister(vfs_handle);
      msc_host_uninstall_device(device_handle);
      return;
    }
    UsbStorageDeviceEntry& device = state->devices[slot];
    device.id = state->next_device_id++;
    if (state->next_device_id == 0) {
      state->next_device_id = 1;
    }
    device.usb_address = usb_address;
    device.device_handle = device_handle;
    device.vfs_handle = vfs_handle;
    std::snprintf(
        device.name, sizeof(device.name), "USB Drive %d", slot + 1);
    std::snprintf(
        device.base_path, sizeof(device.base_path), "%s", base_path);
    MarkSnapshotChanged(state);
  }

  LogMessage(LogLevel::kInfo, __FILE__, __LINE__,
      "USB storage mounted at %s\n", base_path);
}

void HandleUsbStorageEvent(
    UsbStorageManagerState* state, const UsbStorageEvent& event) {
  if (state == nullptr) {
    return;
  }
  if (event.type == UsbStorageEventType::kConnected) {
    MountUsbDevice(state, event.usb_address);
    return;
  }

  int slot = -1;
  {
    SemaphoreLock lock(state->mutex);
    if (lock.locked()) {
      slot = FindDeviceSlot(*state, event.device_handle);
    }
  }
  if (slot >= 0) {
    ReleaseUsbDevice(state, slot);
  }
}

void MscEventCallback(const msc_host_event_t* event, void* context) {
  auto* state = static_cast<UsbStorageManagerState*>(context);
  if (state == nullptr || event == nullptr ||
      state->event_queue == nullptr) {
    return;
  }

  UsbStorageEvent storage_event;
  switch (event->event) {
    case msc_host_event_t::MSC_DEVICE_CONNECTED:
      storage_event.type = UsbStorageEventType::kConnected;
      storage_event.usb_address = event->device.address;
      break;
    case msc_host_event_t::MSC_DEVICE_DISCONNECTED:
      storage_event.type = UsbStorageEventType::kDisconnected;
      storage_event.device_handle = event->device.handle;
      break;
    default:
      return;
  }
  if (xQueueSend(state->event_queue, &storage_event, 0) != pdTRUE) {
    LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
        "USB storage event queue is full\n");
  }
}

void FinishUsbHost(UsbStorageManagerState* state, bool msc_installed,
    bool host_installed) {
  ReleaseAllUsbDevices(state);
  if (msc_installed) {
    const esp_err_t result = msc_host_uninstall();
    if (result != ESP_OK) {
      LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
          "Uninstall USB MSC host failed: %s\n", esp_err_to_name(result));
    }
  }

  if (host_installed) {
    bool no_clients = false;
    for (int elapsed_ms = 0; elapsed_ms < kUsbStopTimeoutMs;
        elapsed_ms += kUsbStopPollMs) {
      uint32_t event_flags = 0;
      usb_host_lib_handle_events(
          pdMS_TO_TICKS(kUsbStopPollMs), &event_flags);
      if ((event_flags & USB_HOST_LIB_EVENT_FLAGS_NO_CLIENTS) != 0) {
        no_clients = true;
        usb_host_device_free_all();
      }
      if (no_clients &&
          (event_flags & USB_HOST_LIB_EVENT_FLAGS_ALL_FREE) != 0) {
        break;
      }
    }
    const esp_err_t result = usb_host_uninstall();
    if (result != ESP_OK) {
      LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
          "Uninstall USB host failed: %s\n", esp_err_to_name(result));
    }
  }
}

void UsbStorageTaskEntry(void* context) {
  auto* state = static_cast<UsbStorageManagerState*>(context);
  if (state == nullptr) {
    vTaskDelete(nullptr);
    return;
  }

  bool host_installed = false;
  bool msc_installed = false;

  usb_host_config_t host_config = {};
  host_config.intr_flags = ESP_INTR_FLAG_LEVEL1;
  esp_err_t result = usb_host_install(&host_config);
  if (result == ESP_OK) {
    host_installed = true;
  } else {
    LogMessage(LogLevel::kError, __FILE__, __LINE__,
        "Install USB host failed: %s\n", esp_err_to_name(result));
  }

  if (host_installed) {
    const msc_host_driver_config_t msc_config = {
        .create_backround_task = true,
        .task_priority = kMscEventTaskPriority,
        .stack_size = kMscEventTaskStackBytes,
        .core_id = tskNO_AFFINITY,
        .callback = MscEventCallback,
        .callback_arg = state,
    };
    result = msc_host_install(&msc_config);
    if (result == ESP_OK) {
      msc_installed = true;
    } else {
      LogMessage(LogLevel::kError, __FILE__, __LINE__,
          "Install USB MSC host failed: %s\n", esp_err_to_name(result));
    }
  }

  if (host_installed && msc_installed) {
    state->running.store(true);
    state->start_failed.store(false);
    MarkSnapshotChanged(state);
    while (!state->stop_requested.load()) {
      uint32_t event_flags = 0;
      usb_host_lib_handle_events(
          pdMS_TO_TICKS(kUsbEventPollMs), &event_flags);

      UsbStorageEvent event;
      while (xQueueReceive(state->event_queue, &event, 0) == pdTRUE) {
        HandleUsbStorageEvent(state, event);
      }
    }
  } else {
    state->start_failed.store(true);
    MarkSnapshotChanged(state);
  }

  state->running.store(false);
  FinishUsbHost(state, msc_installed, host_installed);
  if (state->host_stopped_callback) {
    state->host_stopped_callback();
  }
  MarkSnapshotChanged(state);
  state->start_requested.store(false);
  vTaskDelete(nullptr);
}

}  // namespace

UsbStorageManager::UsbStorageManager(
    HostStoppedCallback host_stopped_callback)
    : state_(std::make_unique<UsbStorageManagerState>(
          std::move(host_stopped_callback))) {
  state_->mutex = xSemaphoreCreateMutex();
  state_->event_queue =
      xQueueCreate(kUsbEventQueueLength, sizeof(UsbStorageEvent));
}

UsbStorageManager::~UsbStorageManager() {
  Stop();
  if (state_->event_queue != nullptr) {
    vQueueDelete(state_->event_queue);
  }
  if (state_->mutex != nullptr) {
    vSemaphoreDelete(state_->mutex);
  }
}

bool UsbStorageManager::Start() {
  if (state_->running.load() || state_->start_requested.load()) {
    return true;
  }
  if (state_->mutex == nullptr || state_->event_queue == nullptr) {
    state_->start_failed.store(true);
    return false;
  }

  xQueueReset(state_->event_queue);
  state_->stop_requested.store(false);
  state_->start_failed.store(false);
  state_->start_requested.store(true);
  const BaseType_t result = xTaskCreate(UsbStorageTaskEntry, "usb_storage",
      kUsbStorageTaskStackBytes, state_.get(), kUsbStorageTaskPriority,
      nullptr);
  if (result != pdPASS) {
    state_->start_requested.store(false);
    state_->start_failed.store(true);
    MarkSnapshotChanged(state_.get());
    return false;
  }
  return true;
}

bool UsbStorageManager::Stop() {
  if (!state_->start_requested.load()) {
    return true;
  }

  state_->stop_requested.store(true);
  for (int elapsed_ms = 0; elapsed_ms < kUsbStopTimeoutMs;
      elapsed_ms += kUsbStopPollMs) {
    if (!state_->start_requested.load()) {
      return true;
    }
    vTaskDelay(pdMS_TO_TICKS(kUsbStopPollMs));
  }
  return !state_->start_requested.load();
}

bool UsbStorageManager::ReadSnapshot(
    UsbStorageSnapshot* snapshot) const {
  if (snapshot == nullptr || state_->mutex == nullptr) {
    return false;
  }

  UsbStorageSnapshot value;
  {
    SemaphoreLock lock(state_->mutex);
    if (!lock.locked()) {
      return false;
    }
    value.generation = state_->generation.load();
    value.monitor_running =
        state_->running.load() || state_->start_requested.load();
    value.start_failed = state_->start_failed.load();
    for (const UsbStorageDeviceEntry& device : state_->devices) {
      if (device.device_handle == nullptr ||
          value.device_count >= kMaxUsbStorageDeviceCount) {
        continue;
      }
      UsbStorageDeviceInfo& info = value.devices[value.device_count++];
      info.id = device.id;
      info.usb_address = device.usb_address;
      std::memcpy(info.name, device.name, sizeof(info.name));
      std::memcpy(
          info.base_path, device.base_path, sizeof(info.base_path));
    }
  }
  *snapshot = value;
  return true;
}

}  // namespace lilygo_box::hal
