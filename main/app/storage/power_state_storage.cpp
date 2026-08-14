/*
 * @Description: 系统电源状态持久化实现
 * @Author: LILYGO_L
 * @Date: 2026-08-14 00:00:00
 * @LastEditTime: 2026-08-14 00:00:00
 * @License: GPL 3.0
 */
#include "app/storage/power_state_storage.h"

#include <array>
#include <cstddef>
#include <cstdint>

#include "app/storage/storage_internal.h"
#include "app/storage/tlv_storage.h"
#include "base/logger.h"
#include "esp_err.h"
#include "nvs.h"

namespace lilygo_box::app {
namespace {

constexpr char kPowerStateNvsNamespace[] = "settings";
constexpr char kPowerStateNvsKey[] = "power_state";
constexpr size_t kPowerStateTlvCapacity = 32;

enum class PowerStateField : uint16_t {
  kPowerOffRequested = 1,
};

bool ArePowerOffStatesEqual(const bool& left, const bool& right) {
  return left == right;
}

bool DecodePowerState(
    const storage::TlvBuffer& buffer, bool* power_off_requested) {
  if (power_off_requested == nullptr) {
    return false;
  }

  bool decoded = false;
  storage::TlvReader reader(
      storage::TlvDomain::kPowerState, buffer.data.get(), buffer.size);
  storage::TlvField field;
  while (true) {
    const storage::TlvReadResult result = reader.Next(&field);
    if (result == storage::TlvReadResult::kEnd) {
      *power_off_requested = decoded;
      return true;
    }
    if (result == storage::TlvReadResult::kInvalid) {
      return false;
    }
    if (static_cast<PowerStateField>(field.tag()) ==
        PowerStateField::kPowerOffRequested && !field.ReadBool(&decoded)) {
      return false;
    }
  }
}

bool EncodePowerState(bool power_off_requested,
    uint8_t* output, size_t capacity, size_t* encoded_size) {
  storage::TlvWriter writer(
      storage::TlvDomain::kPowerState, output, capacity);
  return writer.WriteBool(
             static_cast<uint16_t>(PowerStateField::kPowerOffRequested),
             power_off_requested) &&
      writer.Finalize(encoded_size);
}

NvsStorageCache<bool> g_power_state_cache(
    StorageDomain::kPowerState, ArePowerOffStatesEqual);

}  // namespace

bool InitPowerStateStorage() {
  if (!EnsureStorageCoordinatorInitialized() ||
      !EnsureApplicationNvsInitialized()) {
    return false;
  }

  bool power_off_requested = false;
  if (g_power_state_cache.Read(&power_off_requested)) {
    return true;
  }

  bool load_succeeded = true;
  nvs_handle_t handle = 0;
  const esp_err_t open_result = OpenApplicationNvs(
      kPowerStateNvsNamespace, NVS_READONLY, &handle);
  if (open_result == ESP_OK) {
    storage::TlvBuffer buffer;
    esp_err_t error = ESP_OK;
    const storage::TlvLoadResult result = storage::LoadTlvBuffer(handle,
        kPowerStateNvsKey, storage::TlvDomain::kPowerState,
        kPowerStateTlvCapacity, &buffer, &error);
    if (result == storage::TlvLoadResult::kLoaded &&
        !DecodePowerState(buffer, &power_off_requested)) {
      LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
          "Power-state TLV payload is invalid\n");
      load_succeeded = false;
    } else if (result == storage::TlvLoadResult::kInvalid) {
      LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
          "Power-state TLV container is invalid\n");
      load_succeeded = false;
    } else if (result == storage::TlvLoadResult::kError) {
      LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
          "Load power-state TLV failed: %s\n", esp_err_to_name(error));
      load_succeeded = false;
    }
    nvs_close(handle);
  } else if (open_result != ESP_ERR_NVS_NOT_FOUND) {
    LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
        "Open power-state NVS failed: %s\n", esp_err_to_name(open_result));
    load_succeeded = false;
  }

  if (!g_power_state_cache.Initialize(power_off_requested)) {
    LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
        "Initialize power-state storage cache failed\n");
    return false;
  }
  return load_succeeded;
}

bool ReadPowerOffRequested(bool* requested) {
  return g_power_state_cache.Read(requested);
}

bool WritePowerOffRequested(bool requested) {
  return g_power_state_cache.UpdateAndPersist(requested);
}

StorageStageResult StagePowerStateStorage(nvs_handle_t handle) {
  const bool* power_off_requested = nullptr;
  if (!g_power_state_cache.BeginFlush(&power_off_requested)) {
    return StorageStageResult::kClean;
  }

  std::array<uint8_t, kPowerStateTlvCapacity> buffer = {};
  size_t encoded_size = 0;
  if (!EncodePowerState(*power_off_requested, buffer.data(),
          buffer.size(), &encoded_size) ||
      nvs_set_blob(handle, kPowerStateNvsKey,
          buffer.data(), encoded_size) != ESP_OK) {
    return StorageStageResult::kFailed;
  }
  return StorageStageResult::kStaged;
}

void FinishPowerStateStorage(bool committed) {
  g_power_state_cache.FinishFlush(committed);
}

}  // namespace lilygo_box::app
