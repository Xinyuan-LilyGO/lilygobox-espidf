/*
 * @Description: Battery capacity preference storage implementation
 * @Author: LILYGO_L
 * @Date: 2026-09-01 00:00:00
 * @LastEditTime: 2026-09-02 17:51:19
 * @License: GPL 3.0
 */
#include "app/storage/battery_storage.h"

#include <algorithm>
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

constexpr char kBatteryNvsNamespace[] = "settings";
constexpr char kBatteryNvsKey[] = "battery_config";
constexpr size_t kBatteryTlvCapacity = 32;

// 已分配字段编号只允许保留，禁止改号或复用。
enum class BatteryField : uint16_t {
  kCapacityMah = 1,
};

BatteryPreferences NormalizeBatteryPreferences(
    const BatteryPreferences& source) {
  BatteryPreferences result;
  result.capacity_mah = std::clamp(source.capacity_mah,
      kMinimumBatteryCapacityMah, kMaximumBatteryCapacityMah);
  return result;
}

bool AreBatteryPreferencesEqual(
    const BatteryPreferences& left, const BatteryPreferences& right) {
  return left.capacity_mah == right.capacity_mah;
}

bool DecodeBatteryPreferences(
    const storage::TlvBuffer& buffer, BatteryPreferences* preferences) {
  if (preferences == nullptr) {
    return false;
  }
  BatteryPreferences decoded;
  storage::TlvReader reader(
      storage::TlvDomain::kBattery, buffer.data.get(), buffer.size);
  storage::TlvField field;
  while (true) {
    const storage::TlvReadResult result = reader.Next(&field);
    if (result == storage::TlvReadResult::kEnd) {
      *preferences = NormalizeBatteryPreferences(decoded);
      return true;
    }
    if (result == storage::TlvReadResult::kInvalid) {
      return false;
    }
    if (static_cast<BatteryField>(field.tag()) == BatteryField::kCapacityMah) {
      uint16_t capacity_mah = 0;
      if (!field.ReadUint16(&capacity_mah)) {
        return false;
      }
      decoded.capacity_mah = capacity_mah;
    }
  }
}

bool EncodeBatteryPreferences(const BatteryPreferences& preferences,
    uint8_t* output, size_t capacity, size_t* encoded_size) {
  const BatteryPreferences normalized =
      NormalizeBatteryPreferences(preferences);
  storage::TlvWriter writer(storage::TlvDomain::kBattery, output, capacity);
  return writer.WriteUint16(static_cast<uint16_t>(BatteryField::kCapacityMah),
             static_cast<uint16_t>(normalized.capacity_mah)) &&
         writer.Finalize(encoded_size);
}

NvsStorageCache<BatteryPreferences> g_battery_cache(
    StorageDomain::kBattery, AreBatteryPreferencesEqual);

}  // namespace

bool InitBatteryStorage() {
  if (!EnsureStorageCoordinatorInitialized() ||
      !EnsureApplicationNvsInitialized()) {
    return false;
  }

  BatteryPreferences loaded;
  if (g_battery_cache.Read(&loaded)) {
    return true;
  }

  bool load_succeeded = true;
  nvs_handle_t handle = 0;
  const esp_err_t open_result =
      OpenApplicationNvs(kBatteryNvsNamespace, NVS_READONLY, &handle);
  if (open_result == ESP_OK) {
    storage::TlvBuffer buffer;
    esp_err_t error = ESP_OK;
    const storage::TlvLoadResult result =
        storage::LoadTlvBuffer(handle, kBatteryNvsKey,
            storage::TlvDomain::kBattery, kBatteryTlvCapacity, &buffer, &error);
    if (result == storage::TlvLoadResult::kLoaded &&
        !DecodeBatteryPreferences(buffer, &loaded)) {
      LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
          "Battery TLV payload is invalid\n");
      load_succeeded = false;
    } else if (result == storage::TlvLoadResult::kInvalid) {
      LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
          "Battery TLV container is invalid\n");
      load_succeeded = false;
    } else if (result == storage::TlvLoadResult::kError) {
      LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
          "Load battery TLV failed: %s\n", esp_err_to_name(error));
      load_succeeded = false;
    }
    nvs_close(handle);
  } else if (open_result != ESP_ERR_NVS_NOT_FOUND) {
    LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
        "Open battery NVS failed: %s\n", esp_err_to_name(open_result));
    load_succeeded = false;
  }

  if (!g_battery_cache.Initialize(NormalizeBatteryPreferences(loaded))) {
    LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
        "Initialize battery storage cache failed\n");
    return false;
  }
  return load_succeeded;
}

BatteryPreferences GetBatteryPreferences() {
  BatteryPreferences preferences;
  g_battery_cache.Read(&preferences);
  return NormalizeBatteryPreferences(preferences);
}

bool UpdateBatteryPreferences(const BatteryPreferences& preferences) {
  return g_battery_cache.UpdateAndPersist(
      NormalizeBatteryPreferences(preferences));
}

StorageStageResult StageBatteryStorage(nvs_handle_t handle) {
  const BatteryPreferences* preferences = nullptr;
  if (!g_battery_cache.BeginFlush(&preferences)) {
    return StorageStageResult::kClean;
  }
  std::array<uint8_t, kBatteryTlvCapacity> buffer = {};
  size_t encoded_size = 0;
  if (!EncodeBatteryPreferences(
          *preferences, buffer.data(), buffer.size(), &encoded_size) ||
      nvs_set_blob(handle, kBatteryNvsKey, buffer.data(), encoded_size) !=
          ESP_OK) {
    return StorageStageResult::kFailed;
  }
  return StorageStageResult::kStaged;
}

void FinishBatteryStorage(bool committed) {
  g_battery_cache.FinishFlush(committed);
}

}  // namespace lilygo_box::app
