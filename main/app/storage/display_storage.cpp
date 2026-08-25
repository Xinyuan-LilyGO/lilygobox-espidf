/**
 * @Description: 显示偏好存储实现
 * @Author: LILYGO_L
 * @Date: 2026-06-25 00:00:00
 * @LastEditTime: 2026-07-22 00:00:00
 * @License: GPL 3.0
 */
#include "app/storage/display_storage.h"

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

constexpr const char* kDisplayNvsNamespace = "settings";
constexpr const char* kDisplayNvsKey = "display_config";
constexpr size_t kDisplayTlvCapacity = 128;
constexpr int kMaximumLockTimeoutSeconds = 24 * 60 * 60;

// 已分配字段编号只允许保留，禁止改号或复用。
enum class DisplayField : uint16_t {
  kBrightnessPercent = 1,
  kLockTimeoutSeconds = 2,
  kScreenRotationAngle = 3,
  kLockScreenDoubleTapToTurnScreenOnAndOff = 4,
  kDarkThemeEnabled = 5,
};

int NormalizeScreenRotationAngle(int angle) {
  switch (angle) {
    case 0:
    case 90:
    case 180:
    case 270:
      return angle;
    default:
      return 0;
  }
}

DisplayPreferences NormalizeDisplayPreferences(
    const DisplayPreferences& source) {
  DisplayPreferences result;
  result.brightness_percent = std::clamp(source.brightness_percent,
      kUserDisplayBrightnessMinPercent,
      kUserDisplayBrightnessMaxPercent);
  result.lock_timeout_seconds = std::clamp(
      source.lock_timeout_seconds, kDisplayLockTimeoutDisabledSeconds,
      kMaximumLockTimeoutSeconds);
  result.screen_rotation_angle =
      NormalizeScreenRotationAngle(source.screen_rotation_angle);
  result.lock_screen_double_tap_to_turn_screen_on_and_off =
      source.lock_screen_double_tap_to_turn_screen_on_and_off;
  result.dark_theme_enabled = source.dark_theme_enabled;
  return result;
}

bool AreDisplayPreferencesEqual(
    const DisplayPreferences& left, const DisplayPreferences& right) {
  return left.brightness_percent == right.brightness_percent &&
      left.lock_timeout_seconds == right.lock_timeout_seconds &&
      left.screen_rotation_angle == right.screen_rotation_angle &&
      left.lock_screen_double_tap_to_turn_screen_on_and_off ==
          right.lock_screen_double_tap_to_turn_screen_on_and_off &&
      left.dark_theme_enabled == right.dark_theme_enabled;
}

bool DecodeDisplayPreferences(const storage::TlvBuffer& buffer,
    DisplayPreferences* preferences) {
  if (preferences == nullptr) {
    return false;
  }
  DisplayPreferences decoded;
  storage::TlvReader reader(
      storage::TlvDomain::kDisplay, buffer.data.get(), buffer.size);
  storage::TlvField field;
  while (true) {
    const storage::TlvReadResult result = reader.Next(&field);
    if (result == storage::TlvReadResult::kEnd) {
      *preferences = NormalizeDisplayPreferences(decoded);
      return true;
    }
    if (result == storage::TlvReadResult::kInvalid) {
      return false;
    }
    switch (static_cast<DisplayField>(field.tag())) {
      case DisplayField::kBrightnessPercent: {
        uint8_t value = 0;
        if (!field.ReadUint8(&value)) {
          return false;
        }
        decoded.brightness_percent = value;
        break;
      }
      case DisplayField::kLockTimeoutSeconds: {
        uint32_t value = 0;
        if (!field.ReadUint32(&value)) {
          return false;
        }
        decoded.lock_timeout_seconds = static_cast<int>(std::min(value,
            static_cast<uint32_t>(kMaximumLockTimeoutSeconds)));
        break;
      }
      case DisplayField::kScreenRotationAngle: {
        int32_t value = 0;
        if (!field.ReadInt32(&value)) {
          return false;
        }
        decoded.screen_rotation_angle = static_cast<int>(value);
        break;
      }
      case DisplayField::kLockScreenDoubleTapToTurnScreenOnAndOff: {
        if (!field.ReadBool(
                &decoded.lock_screen_double_tap_to_turn_screen_on_and_off)) {
          return false;
        }
        break;
      }
      case DisplayField::kDarkThemeEnabled: {
        if (!field.ReadBool(&decoded.dark_theme_enabled)) {
          return false;
        }
        break;
      }
      default:
        // 未知字段由旧固件跳过，新增参数不需要提升容器版本。
        break;
    }
  }
}

bool EncodeDisplayPreferences(const DisplayPreferences& preferences,
    uint8_t* output, size_t capacity, size_t* encoded_size) {
  const DisplayPreferences normalized =
      NormalizeDisplayPreferences(preferences);
  storage::TlvWriter writer(
      storage::TlvDomain::kDisplay, output, capacity);
  return writer.WriteUint8(
             static_cast<uint16_t>(DisplayField::kBrightnessPercent),
             static_cast<uint8_t>(normalized.brightness_percent)) &&
      writer.WriteUint32(
          static_cast<uint16_t>(DisplayField::kLockTimeoutSeconds),
          static_cast<uint32_t>(normalized.lock_timeout_seconds)) &&
      writer.WriteInt32(
          static_cast<uint16_t>(DisplayField::kScreenRotationAngle),
          static_cast<int32_t>(normalized.screen_rotation_angle)) &&
      writer.WriteBool(
          static_cast<uint16_t>(
              DisplayField::kLockScreenDoubleTapToTurnScreenOnAndOff),
          normalized.lock_screen_double_tap_to_turn_screen_on_and_off) &&
      writer.WriteBool(
          static_cast<uint16_t>(DisplayField::kDarkThemeEnabled),
          normalized.dark_theme_enabled) &&
      writer.Finalize(encoded_size);
}

NvsStorageCache<DisplayPreferences> g_display_cache(
    StorageDomain::kDisplay, AreDisplayPreferencesEqual);

}  // namespace

void InitDisplayCache() {
  DisplayPreferences loaded;
  nvs_handle_t handle = 0;
  if (OpenApplicationNvs(
          kDisplayNvsNamespace, NVS_READONLY, &handle) == ESP_OK) {
    storage::TlvBuffer buffer;
    esp_err_t error = ESP_OK;
    const storage::TlvLoadResult result = storage::LoadTlvBuffer(handle,
        kDisplayNvsKey, storage::TlvDomain::kDisplay,
        kDisplayTlvCapacity, &buffer, &error);
    if (result == storage::TlvLoadResult::kLoaded &&
        !DecodeDisplayPreferences(buffer, &loaded)) {
      LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
          "Display TLV payload is invalid\n");
    } else if (result == storage::TlvLoadResult::kInvalid) {
      LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
          "Display TLV container is invalid\n");
    } else if (result == storage::TlvLoadResult::kError) {
      LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
          "Load display TLV failed: %s\n", esp_err_to_name(error));
    }
    nvs_close(handle);
  }
  if (!g_display_cache.Initialize(
          NormalizeDisplayPreferences(loaded))) {
    LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
        "Initialize display storage cache failed\n");
  }
}

DisplayPreferences GetDisplayPreferences() {
  DisplayPreferences preferences;
  g_display_cache.Read(&preferences);
  return preferences;
}

bool UpdateDisplayPreferences(const DisplayPreferences& preferences) {
  return g_display_cache.UpdateAndPersist(
      NormalizeDisplayPreferences(preferences));
}

StorageStageResult StageDisplayStorage(nvs_handle_t handle) {
  const DisplayPreferences* preferences = nullptr;
  if (!g_display_cache.BeginFlush(&preferences)) {
    return StorageStageResult::kClean;
  }
  std::array<uint8_t, kDisplayTlvCapacity> buffer = {};
  size_t encoded_size = 0;
  if (!EncodeDisplayPreferences(*preferences, buffer.data(),
          buffer.size(), &encoded_size) ||
      nvs_set_blob(handle, kDisplayNvsKey,
          buffer.data(), encoded_size) != ESP_OK) {
    return StorageStageResult::kFailed;
  }
  return StorageStageResult::kStaged;
}

void FinishDisplayStorage(bool committed) {
  g_display_cache.FinishFlush(committed);
}

}  // namespace lilygo_box::app
