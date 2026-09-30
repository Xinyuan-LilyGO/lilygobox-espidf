/*
 * @Description: 指南针校准参数 TLV 编解码与 NVS 缓存
 * @Author: LILYGO_L
 * @Date: 2026-09-29 00:00:00
 * @LastEditTime: 2026-09-29 00:00:00
 * @License: GPL 3.0
 */
#include "app/storage/compass_storage.h"

#include <array>
#include <cmath>

#include "app/storage/storage_internal.h"
#include "app/storage/tlv_storage.h"
#include "base/logger.h"

namespace lilygo_box::app {
namespace {

constexpr char kCompassNvsKey[] = "compass_cal";
constexpr size_t kCompassTlvCapacity = 128;
// 偏置以 nT、比例以百万分之一编码，避免直接存储结构体布局或浮点字节序。
constexpr float kOffsetUnits = 1000.0F;
constexpr float kScaleUnits = 1000000.0F;

// 三轴磁场偏置及比例的 TLV 字段编号。
enum class CompassField : uint16_t {
  kOffsetX = 1,
  kOffsetY = 2,
  kOffsetZ = 3,
  kScaleX = 4,
  kScaleY = 5,
  kScaleZ = 6,
};

/**
 * @brief 验证校准记录的有效状态、有限性及参数范围
 * @param data 待验证的校准记录
 * @return 参数可以安全使用时返回 true
 */
bool ValidCalibration(const CompassCalibrationData& data) {
  if (!data.ready) {
    return false;
  }
  for (int i = 0; i < 3; ++i) {
    if (!std::isfinite(data.offset[i]) || std::fabs(data.offset[i]) > 1000.0F ||
        !std::isfinite(data.scale[i]) || data.scale[i] < 0.5F || data.scale[i] > 2.0F) {
      return false;
    }
  }
  return true;
}

/**
 * @brief 比较两个校准缓存，避免无变化时重复写入 Flash
 * @param left 左侧记录
 * @param right 右侧记录
 * @return 完全一致时返回 true
 */
bool EqualCalibration(const CompassCalibrationData& left,
    const CompassCalibrationData& right) {
  if (left.ready != right.ready) {
    return false;
  }
  for (int i = 0; i < 3; ++i) {
    if (left.offset[i] != right.offset[i] || left.scale[i] != right.scale[i]) {
      return false;
    }
  }
  return true;
}

NvsStorageCache<CompassCalibrationData> g_compass_cache(
    StorageDomain::kCompass, EqualCalibration);

/**
 * @brief 解码校准 TLV，拒绝缺字段、重复字段及无效参数
 * @param buffer 已通过 TLV 容器和 CRC 检查的缓冲区
 * @param data 成功时接收完整记录
 * @return 全部必需字段及数值有效时返回 true
 */
bool DecodeCalibration(const storage::TlvBuffer& buffer,
    CompassCalibrationData& data) {
  CompassCalibrationData decoded;
  uint16_t seen = 0;
  storage::TlvReader reader(storage::TlvDomain::kCompass,
      buffer.data.get(), buffer.size);
  storage::TlvField field;
  while (true) {
    const auto result = reader.Next(&field);
    if (result == storage::TlvReadResult::kInvalid) {
      return false;
    }
    if (result == storage::TlvReadResult::kEnd) {
      decoded.ready = seen == 0x3F;  // 六个校准参数全部存在。
      if (!ValidCalibration(decoded)) {
        return false;
      }
      data = decoded;
      return true;
    }
    const uint16_t tag = field.tag();
    if (tag < 1 || tag > 6) {
      continue;
    }
    const uint16_t bit = 1U << (tag - 1);
    if ((seen & bit) != 0) {
      return false;
    }
    seen |= bit;
    int32_t value = 0;
    if (!field.ReadInt32(&value)) {
      return false;
    }
    if (tag <= static_cast<uint16_t>(CompassField::kOffsetZ)) {
      decoded.offset[tag - static_cast<uint16_t>(CompassField::kOffsetX)] =
          value / kOffsetUnits;
    } else {
      decoded.scale[tag - static_cast<uint16_t>(CompassField::kScaleX)] =
          value / kScaleUnits;
    }
  }
}

}  // namespace

bool InitCompassStorage() {
  if (!EnsureStorageCoordinatorInitialized() || !EnsureApplicationNvsInitialized()) {
    return false;
  }
  CompassCalibrationData loaded;
  if (g_compass_cache.Read(&loaded)) {
    return true;
  }
  bool success = true;
  nvs_handle_t handle = 0;
  const esp_err_t opened = OpenApplicationNvs("settings", NVS_READONLY, &handle);
  if (opened == ESP_OK) {
    storage::TlvBuffer buffer;
    esp_err_t error = ESP_OK;
    const auto result = storage::LoadTlvBuffer(handle, kCompassNvsKey,
        storage::TlvDomain::kCompass, kCompassTlvCapacity, &buffer, &error);
    if ((result == storage::TlvLoadResult::kLoaded &&
            !DecodeCalibration(buffer, loaded)) ||
        result == storage::TlvLoadResult::kInvalid ||
        result == storage::TlvLoadResult::kError) {
      loaded = {};
      success = false;
      LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
          "Compass calibration unavailable: invalid/incompatible record or NVS "
          "read error (%s)\n", esp_err_to_name(error));
    }
    nvs_close(handle);
  } else if (opened != ESP_ERR_NVS_NOT_FOUND) {
    success = false;
    LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
        "Open compass calibration NVS failed: %s\n", esp_err_to_name(opened));
  }
  return g_compass_cache.Initialize(loaded) && success;
}

CompassCalibrationData GetCompassCalibration() {
  CompassCalibrationData data;
  g_compass_cache.Read(&data);
  return data;
}

bool SaveCompassCalibration(const CompassCalibrationData& calibration) {
  return ValidCalibration(calibration) &&
         g_compass_cache.UpdateAndPersist(calibration);
}

StorageStageResult StageCompassStorage(nvs_handle_t handle) {
  const CompassCalibrationData* data = nullptr;
  if (!g_compass_cache.BeginFlush(&data)) {
    return StorageStageResult::kClean;
  }
  std::array<uint8_t, kCompassTlvCapacity> buffer = {};
  storage::TlvWriter writer(storage::TlvDomain::kCompass,
      buffer.data(), buffer.size());
  bool result = true;
  for (int i = 0; i < 3; ++i) {
    result &= writer.WriteInt32(static_cast<uint16_t>(CompassField::kOffsetX) + i,
        static_cast<int32_t>(std::lround(data->offset[i] * kOffsetUnits)));
    result &= writer.WriteInt32(static_cast<uint16_t>(CompassField::kScaleX) + i,
        static_cast<int32_t>(std::lround(data->scale[i] * kScaleUnits)));
  }
  size_t size = 0;
  if (!result || !writer.Finalize(&size) ||
      nvs_set_blob(handle, kCompassNvsKey, buffer.data(), size) != ESP_OK) {
    return StorageStageResult::kFailed;
  }
  return StorageStageResult::kStaged;
}

void FinishCompassStorage(bool committed) {
  g_compass_cache.FinishFlush(committed);
}

}  // namespace lilygo_box::app
