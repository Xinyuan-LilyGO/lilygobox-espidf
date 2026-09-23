/*
 * @Description: 开发者偏好存储实现
 * @Author: LILYGO_L
 * @Date: 2026-09-23 00:00:00
 * @LastEditTime: 2026-09-23 17:48:58
 * @License: GPL 3.0
 */
#include "app/storage/developer_storage.h"

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

constexpr char kDeveloperNvsNamespace[] = "settings";
constexpr char kDeveloperNvsKey[] = "developer";
constexpr size_t kDeveloperTlvCapacity = 32;

enum class DeveloperField : uint16_t {
  kLogLevel = 1,
  kCppBusDriverLogLevel = 2,
  kLilygoDeviceDriverLogLevel = 3,
};

bool AreDeveloperPreferencesEqual(
    const DeveloperPreferences& left, const DeveloperPreferences& right) {
  return left.log_level == right.log_level &&
         left.cpp_bus_driver_log_level == right.cpp_bus_driver_log_level &&
         left.lilygo_device_driver_log_level ==
             right.lilygo_device_driver_log_level;
}

bool DecodeDeveloperPreferences(
    const storage::TlvBuffer& buffer, DeveloperPreferences* preferences) {
  if (preferences == nullptr) {
    return false;
  }

  DeveloperPreferences decoded = *preferences;
  storage::TlvReader reader(
      storage::TlvDomain::kDeveloper, buffer.data.get(), buffer.size);
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
    switch (static_cast<DeveloperField>(field.tag())) {
      case DeveloperField::kLogLevel: {
        uint8_t level = 0;
        if (!field.ReadUint8(&level) ||
            level > static_cast<uint8_t>(LogLevel::kNone)) {
          return false;
        }
        decoded.log_level = static_cast<LogLevel>(level);
        break;
      }
      case DeveloperField::kCppBusDriverLogLevel: {
        uint8_t level = 0;
        if (!field.ReadUint8(&level) ||
            level >
                static_cast<uint8_t>(cpp_bus_driver::Logger::LogLevel::kNone)) {
          return false;
        }
        decoded.cpp_bus_driver_log_level =
            static_cast<cpp_bus_driver::Logger::LogLevel>(level);
        break;
      }
      case DeveloperField::kLilygoDeviceDriverLogLevel: {
        uint8_t level = 0;
        if (!field.ReadUint8(&level) ||
            level > static_cast<uint8_t>(lilygo_device_driver::LogLevel::kNone)) {
          return false;
        }
        decoded.lilygo_device_driver_log_level =
            static_cast<lilygo_device_driver::LogLevel>(level);
        break;
      }
      default:
        break;
    }
  }
}

bool EncodeDeveloperPreferences(const DeveloperPreferences& preferences,
    uint8_t* output, size_t capacity, size_t* encoded_size) {
  storage::TlvWriter writer(storage::TlvDomain::kDeveloper, output, capacity);
  return writer.WriteUint8(
             static_cast<uint16_t>(DeveloperField::kLogLevel),
             static_cast<uint8_t>(preferences.log_level)) &&
         writer.WriteUint8(
             static_cast<uint16_t>(DeveloperField::kCppBusDriverLogLevel),
             static_cast<uint8_t>(preferences.cpp_bus_driver_log_level)) &&
         writer.WriteUint8(
             static_cast<uint16_t>(DeveloperField::kLilygoDeviceDriverLogLevel),
             static_cast<uint8_t>(preferences.lilygo_device_driver_log_level)) &&
         writer.Finalize(encoded_size);
}

void ApplyLogLevels(const DeveloperPreferences& preferences) {
  SetMinimumLogLevel(preferences.log_level);
  cpp_bus_driver::Logger::SetMinimumLogLevel(
      preferences.cpp_bus_driver_log_level);
  lilygo_device_driver::SetMinimumLogLevel(
      preferences.lilygo_device_driver_log_level);
}

NvsStorageCache<DeveloperPreferences> g_developer_cache(
    StorageDomain::kDeveloper, AreDeveloperPreferencesEqual);

}  // namespace

void InitDeveloperCache() {
  DeveloperPreferences loaded;
  loaded.log_level = GetMinimumLogLevel();
  loaded.cpp_bus_driver_log_level = cpp_bus_driver::Logger::GetMinimumLogLevel();
  loaded.lilygo_device_driver_log_level =
      lilygo_device_driver::GetMinimumLogLevel();
  nvs_handle_t handle = 0;
  if (OpenApplicationNvs(kDeveloperNvsNamespace, NVS_READONLY, &handle) ==
      ESP_OK) {
    storage::TlvBuffer buffer;
    esp_err_t error = ESP_OK;
    const storage::TlvLoadResult result = storage::LoadTlvBuffer(handle,
        kDeveloperNvsKey, storage::TlvDomain::kDeveloper,
        kDeveloperTlvCapacity, &buffer, &error);
    if (result == storage::TlvLoadResult::kLoaded &&
        !DecodeDeveloperPreferences(buffer, &loaded)) {
      LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
          "Developer TLV payload is invalid\n");
    } else if (result == storage::TlvLoadResult::kInvalid) {
      LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
          "Developer TLV container is invalid\n");
    } else if (result == storage::TlvLoadResult::kError) {
      LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
          "Load developer TLV failed: %s\n", esp_err_to_name(error));
    }
    nvs_close(handle);
  }
  if (!g_developer_cache.Initialize(loaded)) {
    LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
        "Initialize developer storage cache failed\n");
    return;
  }
  ApplyLogLevels(loaded);
}

DeveloperPreferences GetDeveloperPreferences() {
  DeveloperPreferences preferences;
  preferences.log_level = GetMinimumLogLevel();
  preferences.cpp_bus_driver_log_level =
      cpp_bus_driver::Logger::GetMinimumLogLevel();
  preferences.lilygo_device_driver_log_level =
      lilygo_device_driver::GetMinimumLogLevel();
  g_developer_cache.Read(&preferences);
  return preferences;
}

bool UpdateDeveloperPreferences(const DeveloperPreferences& preferences) {
  if (preferences.log_level > LogLevel::kNone ||
      preferences.cpp_bus_driver_log_level >
          cpp_bus_driver::Logger::LogLevel::kNone ||
      preferences.lilygo_device_driver_log_level >
          lilygo_device_driver::LogLevel::kNone) {
    return false;
  }
  const bool persisted = g_developer_cache.UpdateAndPersist(preferences);
  // 提交失败时保留现有缓存重试机制，运行期仍采用已接受的配置。
  ApplyLogLevels(GetDeveloperPreferences());
  return persisted;
}

StorageStageResult StageDeveloperStorage(nvs_handle_t handle) {
  const DeveloperPreferences* preferences = nullptr;
  if (!g_developer_cache.BeginFlush(&preferences)) {
    return StorageStageResult::kClean;
  }
  std::array<uint8_t, kDeveloperTlvCapacity> buffer = {};
  size_t encoded_size = 0;
  if (!EncodeDeveloperPreferences(
          *preferences, buffer.data(), buffer.size(), &encoded_size) ||
      nvs_set_blob(handle, kDeveloperNvsKey, buffer.data(), encoded_size) !=
          ESP_OK) {
    return StorageStageResult::kFailed;
  }
  return StorageStageResult::kStaged;
}

void FinishDeveloperStorage(bool committed) {
  g_developer_cache.FinishFlush(committed);
}

}  // namespace lilygo_box::app
