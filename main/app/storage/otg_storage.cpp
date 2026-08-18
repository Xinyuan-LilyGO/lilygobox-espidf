/*
 * @Description: OTG reverse-power preference storage implementation
 * @Author: LILYGO_L
 * @Date: 2026-08-18 00:00:00
 * @LastEditTime: 2026-08-18 00:00:00
 * @License: GPL 3.0
 */
#include "app/storage/otg_storage.h"

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

constexpr const char* kOtgNvsNamespace = "settings";
constexpr const char* kOtgNvsKey = "otg_config";
constexpr size_t kOtgTlvCapacity = 32;

// 已分配字段编号只允许保留，禁止改号或复用。
enum class OtgField : uint16_t {
  kEnabled = 1,
};

bool AreOtgPreferencesEqual(
    const OtgPreferences& left, const OtgPreferences& right) {
  return left.enabled == right.enabled;
}

bool DecodeOtgPreferences(
    const storage::TlvBuffer& buffer, OtgPreferences* preferences) {
  if (preferences == nullptr) {
    return false;
  }

  OtgPreferences decoded;
  storage::TlvReader reader(
      storage::TlvDomain::kOtg, buffer.data.get(), buffer.size);
  storage::TlvField field;
  while (true) {
    const storage::TlvReadResult result = reader.Next(&field);
    if (result == storage::TlvReadResult::kEnd) {
      *preferences = decoded;
      return true;
    }
    if (result == storage::TlvReadResult::kInvalid) {
      return false;
    }
    switch (static_cast<OtgField>(field.tag())) {
      case OtgField::kEnabled:
        if (!field.ReadBool(&decoded.enabled)) {
          return false;
        }
        break;
      default:
        break;
    }
  }
}

bool EncodeOtgPreferences(const OtgPreferences& preferences,
    uint8_t* output, size_t capacity, size_t* encoded_size) {
  storage::TlvWriter writer(storage::TlvDomain::kOtg, output, capacity);
  return writer.WriteBool(
             static_cast<uint16_t>(OtgField::kEnabled),
             preferences.enabled) &&
      writer.Finalize(encoded_size);
}

NvsStorageCache<OtgPreferences> g_otg_cache(
    StorageDomain::kOtg, AreOtgPreferencesEqual);

}  // namespace

void InitOtgCache() {
  OtgPreferences loaded;
  nvs_handle_t handle = 0;
  if (OpenApplicationNvs(kOtgNvsNamespace, NVS_READONLY, &handle) == ESP_OK) {
    storage::TlvBuffer buffer;
    esp_err_t error = ESP_OK;
    const storage::TlvLoadResult result = storage::LoadTlvBuffer(handle,
        kOtgNvsKey, storage::TlvDomain::kOtg,
        kOtgTlvCapacity, &buffer, &error);
    if (result == storage::TlvLoadResult::kLoaded &&
        !DecodeOtgPreferences(buffer, &loaded)) {
      LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
          "OTG TLV payload is invalid\n");
    } else if (result == storage::TlvLoadResult::kInvalid) {
      LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
          "OTG TLV container is invalid\n");
    } else if (result == storage::TlvLoadResult::kError) {
      LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
          "Load OTG TLV failed: %s\n", esp_err_to_name(error));
    }
    nvs_close(handle);
  }
  if (!g_otg_cache.Initialize(loaded)) {
    LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
        "Initialize OTG storage cache failed\n");
  }
}

OtgPreferences GetOtgPreferences() {
  OtgPreferences preferences;
  g_otg_cache.Read(&preferences);
  return preferences;
}

bool UpdateOtgPreferences(const OtgPreferences& preferences) {
  return g_otg_cache.UpdateAndPersist(preferences);
}

StorageStageResult StageOtgStorage(nvs_handle_t handle) {
  const OtgPreferences* preferences = nullptr;
  if (!g_otg_cache.BeginFlush(&preferences)) {
    return StorageStageResult::kClean;
  }
  std::array<uint8_t, kOtgTlvCapacity> buffer = {};
  size_t encoded_size = 0;
  if (!EncodeOtgPreferences(*preferences, buffer.data(), buffer.size(),
          &encoded_size) ||
      nvs_set_blob(handle, kOtgNvsKey,
          buffer.data(), encoded_size) != ESP_OK) {
    return StorageStageResult::kFailed;
  }
  return StorageStageResult::kStaged;
}

void FinishOtgStorage(bool committed) {
  g_otg_cache.FinishFlush(committed);
}

}  // namespace lilygo_box::app
