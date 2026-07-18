/*
 * @Description: LittleFS 内部存储挂载与容量查询实现
 * @Author: LILYGO_L
 * @Date: 2026-07-17 15:20:00
 * @LastEditTime: 2026-07-18 00:00:00
 * @License: GPL 3.0
 */
#include "app/storage/littlefs_storage.h"

#include "base/logger.h"
#include "esp_err.h"
#include "esp_littlefs.h"
#include "esp_partition.h"

namespace lilygo_box::app {
namespace {

constexpr char kLittleFsPartitionLabel[] = "storage";
constexpr char kLittleFsBasePath[] = "/littlefs";

}  // namespace

bool InitLittleFsStorage() {
  if (esp_littlefs_mounted(kLittleFsPartitionLabel)) {
    return true;
  }

  esp_vfs_littlefs_conf_t config = {};
  config.base_path = kLittleFsBasePath;
  config.partition_label = kLittleFsPartitionLabel;
  config.format_if_mount_failed = true;
  config.grow_on_mount = true;
  const esp_err_t result = esp_vfs_littlefs_register(&config);
  if (result != ESP_OK) {
    LogMessage(LogLevel::kError, __FILE__, __LINE__,
        "Mount LittleFS storage failed: %s\n", esp_err_to_name(result));
    return false;
  }

  size_t total_bytes = 0;
  size_t used_bytes = 0;
  if (GetLittleFsStorageInfo(&total_bytes, &used_bytes)) {
    const size_t free_bytes =
        total_bytes >= used_bytes ? total_bytes - used_bytes : 0;
    const size_t usage_percent =
        total_bytes == 0 ? 0 : used_bytes * 100U / total_bytes;
    LogMessage(LogLevel::kInfo, __FILE__, __LINE__,
        "LittleFS storage mounted: partition=%s, path=%s, mode=rw, "
        "used=%u bytes, free=%u bytes, total=%u bytes, usage=%u%%\n",
        kLittleFsPartitionLabel, kLittleFsBasePath,
        static_cast<unsigned>(used_bytes), static_cast<unsigned>(free_bytes),
        static_cast<unsigned>(total_bytes),
        static_cast<unsigned>(usage_percent));
  }
  return true;
}

bool IsLittleFsStorageMounted() {
  return esp_littlefs_mounted(kLittleFsPartitionLabel);
}

const char* LittleFsStorageBasePath() { return kLittleFsBasePath; }

bool GetLittleFsStorageInfo(size_t* total_bytes, size_t* used_bytes) {
  if (total_bytes == nullptr || used_bytes == nullptr ||
      !IsLittleFsStorageMounted()) {
    return false;
  }
  return esp_littlefs_info(kLittleFsPartitionLabel, total_bytes, used_bytes) ==
         ESP_OK;
}

bool EraseAllLittleFsStorage() {
  esp_partition_iterator_t iterator = esp_partition_find(
      ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_LITTLEFS, nullptr);
  if (iterator == nullptr) {
    LogMessage(LogLevel::kError, __FILE__, __LINE__,
        "No LittleFS partition found during factory reset\n");
    return false;
  }

  bool success = true;
  while (iterator != nullptr) {
    const esp_partition_t* partition = esp_partition_get(iterator);
    if (partition == nullptr) {
      success = false;
      iterator = esp_partition_next(iterator);
      continue;
    }

    if (esp_littlefs_mounted(partition->label)) {
      const esp_err_t unregister_result =
          esp_vfs_littlefs_unregister(partition->label);
      if (unregister_result != ESP_OK) {
        LogMessage(LogLevel::kError, __FILE__, __LINE__,
            "Unmount LittleFS partition %s failed: %s\n", partition->label,
            esp_err_to_name(unregister_result));
        success = false;
        iterator = esp_partition_next(iterator);
        continue;
      }
    }

    const esp_err_t erase_result =
        esp_partition_erase_range(partition, 0, partition->size);
    if (erase_result != ESP_OK) {
      LogMessage(LogLevel::kError, __FILE__, __LINE__,
          "Erase LittleFS partition %s failed: %s\n", partition->label,
          esp_err_to_name(erase_result));
      success = false;
    }
    iterator = esp_partition_next(iterator);
  }
  esp_partition_iterator_release(iterator);
  return success;
}

}  // namespace lilygo_box::app
