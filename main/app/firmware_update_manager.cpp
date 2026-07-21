/*
 * @Description: LilygoBox 主固件与无线固件组合 OTA 更新实现
 * @Author: LILYGO_L
 * @Date: 2026-07-20 00:00:00
 * @LastEditTime: 2026-07-21 14:51:29
 * @License: GPL 3.0
 */
#include "app/firmware_update_manager.h"

#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <limits>
#include <memory>
#include <sys/stat.h>
#include <unistd.h>

#include "app/storage/littlefs_storage.h"
#include "base/logger.h"
#include "cJSON.h"
#include "esp_app_desc.h"
#include "esp_app_format.h"
#include "esp_crt_bundle.h"
#include "esp_err.h"
#include "esp_hosted.h"
#include "esp_hosted_api_types.h"
#include "esp_hosted_ota.h"
#include "esp_http_client.h"
#include "esp_https_ota.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "hal/providers/wifi_provider.h"
#include "mbedtls/sha256.h"

namespace lilygo_box::app {
namespace {

#if defined(CONFIG_LILYGO_DEVICE_DRIVER_T_DISPLAY_P4)
constexpr char kCurrentDeviceId[] = "t-display-p4";
constexpr char kManifestUrl[] =
    "https://github.com/Xinyuan-LilyGO/lilygobox-espidf/releases/"
    "latest/download/lilygobox-t-display-p4-ota-manifest.json";
#else
constexpr char kCurrentDeviceId[] = "";
constexpr char kManifestUrl[] = "";
#endif
constexpr int kSupportedManifestVersion = 1;
constexpr char kBackupDownloadProxyPrefix[] =
    "https://gh-proxy.com/";
constexpr char kReleaseDownloadPrefix[] =
    "https://github.com/Xinyuan-LilyGO/lilygobox-espidf/releases/download/";
constexpr char kMainReleaseAssetName[] =
    "lilygobox-t-display-p4-esp32p4.bin";
constexpr char kWirelessReleaseAssetName[] =
    "lilygobox-t-display-p4-esp32c6.bin";
constexpr char kApplicationDirectory[] = "/littlefs/lilygobox";
constexpr char kOtaDirectory[] = "/littlefs/lilygobox/ota";
constexpr char kOtaStagingDirectory[] =
    "/littlefs/lilygobox/ota/staging";
constexpr char kCacheDirectory[] = "/littlefs/lilygobox/cache";
constexpr char kOtaCacheDirectory[] = "/littlefs/lilygobox/cache/ota";
constexpr char kSavedManifestPath[] =
    "/littlefs/lilygobox/ota/manifest.json";
constexpr char kSavedManifestTempPath[] =
    "/littlefs/lilygobox/ota/manifest.json.tmp";
constexpr char kInstalledManifestPath[] =
    "/littlefs/lilygobox/ota/installed.json";
constexpr char kInstalledManifestTempPath[] =
    "/littlefs/lilygobox/ota/installed.json.tmp";
constexpr char kWirelessFirmwarePath[] =
    "/littlefs/lilygobox/ota/staging/wireless-firmware.bin";
constexpr char kWirelessFirmwareTempPath[] =
    "/littlefs/lilygobox/cache/ota/wireless-firmware.bin.part";
constexpr char kPendingUpdatePath[] =
    "/littlefs/lilygobox/ota/pending-update";
constexpr char kWirelessFirmwareProjectName[] = "network_adapter";
constexpr esp_chip_id_t kExpectedWirelessChipId = ESP_CHIP_ID_ESP32C6;
constexpr int kFirmwareHttpTimeoutMs = 15000;
constexpr int kHttpBufferSize = 4096;
constexpr size_t kMaximumDownloadUrlLength = 512;
constexpr size_t kManifestMaximumSize = 8 * 1024;
constexpr size_t kMaximumFirmwareAssetSize = 64 * 1024 * 1024;
constexpr size_t kMinimumLittleFsFreeReserve = 256 * 1024;
constexpr size_t kImageSearchAlignment = 0x1000;
constexpr size_t kWirelessFirmwareChunkSize = 1500;
constexpr size_t kHashReadChunkSize = 4096;
constexpr size_t kSha256ByteCount = 32;
constexpr size_t kSha256TextLength = kSha256ByteCount * 2;
constexpr uint8_t kMaximumImageSegmentCount = 16;
constexpr uint32_t kRestartDelayMs = 1200;
constexpr uint32_t kWirelessReadyPollMs = 250;
constexpr uint32_t kWirelessReadyTimeoutMs = 15 * 1000;
constexpr uint32_t kManifestPrimaryTimeoutMs = 8 * 1000;
constexpr uint32_t kManifestBackupTimeoutMs = 15 * 1000;
constexpr uint32_t kFirmwareDownloadTimeoutMs = 10 * 60 * 1000;
constexpr uint32_t kWorkerTaskStackBytes = 16 * 1024;
constexpr UBaseType_t kWorkerTaskPriority = 5;

struct FirmwareReleaseManifest {
  char device_id[32] = {};
  char release_version[32] = {};
  char release_channel[16] = {};
  char release_time[32] = {};
  char main_version[32] = {};
  char main_url[384] = {};
  size_t main_size_bytes = 0;
  char main_sha256[kSha256TextLength + 1] = {};
  char wireless_version[32] = {};
  char wireless_url[384] = {};
  size_t wireless_size_bytes = 0;
  char wireless_sha256[kSha256TextLength + 1] = {};
  char notes[kFirmwareUpdateNoteCapacity][128] = {};
  size_t note_count = 0;
};

struct FirmwareImageInfo {
  size_t offset = 0;
  size_t size = 0;
  esp_app_desc_t app_desc = {};
};

struct ManifestDownloadContext {
  char* data = nullptr;
  size_t size = 0;
  TickType_t started_tick = 0;
  uint32_t timeout_ms = 0;
  bool overflow = false;
  bool timed_out = false;
};

struct FirmwareDownloadContext {
  FILE* file = nullptr;
  size_t downloaded_size = 0;
  size_t expected_size = 0;
  TickType_t started_tick = 0;
  bool write_failed = false;
  bool overflow = false;
  bool timed_out = false;
  bool pause_requested = false;
  bool cancel_requested = false;
};

// 本次更新优先使用的固件下载源。
enum class FirmwareDownloadSource {
  kPrimary,
  kBackup,
};

// 按本次首选顺序排列的固件下载地址。
struct FirmwareDownloadSourceList {
  const char* urls[2] = {};
  FirmwareDownloadSource sources[2] = {};
  size_t count = 0;
};

struct FirmwareUpdateManagerState {
  SemaphoreHandle_t mutex = nullptr;
  SemaphoreHandle_t http_client_mutex = nullptr;
  hal::WifiProvider* wifi = nullptr;
  esp_http_client_handle_t active_http_client = nullptr;
  FirmwareUpdateSnapshot snapshot;
  FirmwareReleaseManifest manifest;
  bool initialized = false;
  bool manifest_valid = false;
  bool worker_running = false;
  bool pause_requested = false;
  bool cancel_requested = false;
  FirmwareDownloadSource preferred_download_source =
      FirmwareDownloadSource::kPrimary;
};

FirmwareUpdateManagerState g_manager;

enum class WirelessUpdateResult {
  kNotRequired,
  kRestarting,
  kFailed,
};

enum class MainUpdateResult {
  kNotRequired,
  kPrepared,
  kPaused,
  kCancelled,
  kRestarting,
  kFailed,
};

enum class FirmwareDownloadResult {
  kCompleted,
  kPaused,
  kCancelled,
  kFailed,
};

enum class TransferRequest {
  kNone,
  kPause,
  kCancel,
};

// 固件清单解析结果，用于区分格式错误和不兼容版本。
enum class ManifestParseResult {
  kSuccess,
  kInvalid,
  kUnsupportedVersion,
};

/**
 * @brief 安全复制以空字符结尾的短文本
 * @param destination 目标缓冲区
 * @param destination_size 目标缓冲区长度
 * @param source 源文本
 */
void CopyText(char* destination, size_t destination_size,
    const char* source) {
  if (destination == nullptr || destination_size == 0) {
    return;
  }
  std::snprintf(destination, destination_size, "%s",
      source == nullptr ? "" : source);
}

/**
 * @brief 为版本号添加 v 前缀并安全写入目标缓冲区
 * @param destination 目标缓冲区
 * @param destination_size 目标缓冲区长度
 * @param version 不含前缀的版本号
 */
void CopyReleaseVersion(char* destination, size_t destination_size,
    const char* version) {
  if (destination == nullptr || destination_size == 0) {
    return;
  }
  const char* source =
      version == nullptr || version[0] == '\0' ? "unknown" : version;
  size_t destination_index = 0;
  if (destination_index + 1 < destination_size) {
    destination[destination_index++] = 'v';
  }
  size_t source_index = 0;
  while (destination_index + 1 < destination_size &&
         source[source_index] != '\0') {
    destination[destination_index++] = source[source_index++];
  }
  destination[destination_index] = '\0';
}

/**
 * @brief 获取固件更新状态互斥锁
 * @return 获取成功返回 true，否则返回 false
 */
bool LockManager() {
  return g_manager.mutex != nullptr &&
         xSemaphoreTake(g_manager.mutex, portMAX_DELAY) == pdTRUE;
}

/**
 * @brief 释放固件更新状态互斥锁
 */
void UnlockManager() {
  if (g_manager.mutex != nullptr) {
    xSemaphoreGive(g_manager.mutex);
  }
}

/**
 * @brief 记录当前正在执行传输的 HTTP 客户端
 * @param client HTTP 客户端
 */
void SetActiveHttpClient(esp_http_client_handle_t client) {
  if (g_manager.http_client_mutex == nullptr ||
      xSemaphoreTake(g_manager.http_client_mutex, portMAX_DELAY) != pdTRUE) {
    return;
  }
  g_manager.active_http_client = client;
  xSemaphoreGive(g_manager.http_client_mutex);
}

/**
 * @brief 清除当前正在执行传输的 HTTP 客户端
 */
void ClearActiveHttpClient() {
  if (g_manager.http_client_mutex == nullptr ||
      xSemaphoreTake(g_manager.http_client_mutex, portMAX_DELAY) != pdTRUE) {
    return;
  }
  g_manager.active_http_client = nullptr;
  xSemaphoreGive(g_manager.http_client_mutex);
}

/**
 * @brief 关闭当前 HTTP 连接以唤醒阻塞中的固件下载
 */
void CloseActiveHttpClient() {
  if (g_manager.http_client_mutex == nullptr ||
      xSemaphoreTake(g_manager.http_client_mutex, portMAX_DELAY) != pdTRUE) {
    return;
  }
  if (g_manager.active_http_client != nullptr) {
    esp_http_client_close(g_manager.active_http_client);
  }
  xSemaphoreGive(g_manager.http_client_mutex);
}

/**
 * @brief 记录 ESP HTTPS OTA 内部创建的 HTTP 客户端
 * @param client HTTP 客户端
 * @return 始终返回 ESP_OK
 */
esp_err_t FirmwareOtaHttpClientInitialized(
    esp_http_client_handle_t client) {
  SetActiveHttpClient(client);
  return ESP_OK;
}

/**
 * @brief 获取本次更新优先使用的固件下载源
 * @return 当前首选下载源
 */
FirmwareDownloadSource GetPreferredDownloadSource() {
  if (!LockManager()) {
    return FirmwareDownloadSource::kPrimary;
  }
  const FirmwareDownloadSource source =
      g_manager.preferred_download_source;
  UnlockManager();
  return source;
}

/**
 * @brief 记录本次更新优先使用的固件下载源
 * @param source 下载源
 */
void SetPreferredDownloadSource(FirmwareDownloadSource source) {
  if (!LockManager()) {
    return;
  }
  g_manager.preferred_download_source = source;
  UnlockManager();
}

/**
 * @brief 获取固件下载源的日志名称
 * @param source 下载源
 * @return 日志名称
 */
const char* FirmwareDownloadSourceName(FirmwareDownloadSource source) {
  return source == FirmwareDownloadSource::kBackup ? "backup" : "primary";
}

TransferRequest ReadTransferRequest() {
  if (!LockManager()) {
    return TransferRequest::kNone;
  }
  const TransferRequest request = g_manager.cancel_requested
      ? TransferRequest::kCancel
      : g_manager.pause_requested ? TransferRequest::kPause
                                  : TransferRequest::kNone;
  UnlockManager();
  return request;
}

/**
 * @brief 判断指定阶段是否正在执行网络或安装操作
 * @param stage 固件更新阶段
 * @return 正在执行返回 true，否则返回 false
 */
bool IsBusyStage(FirmwareUpdateStage stage) {
  switch (stage) {
    case FirmwareUpdateStage::kWaitingForNetwork:
    case FirmwareUpdateStage::kChecking:
    case FirmwareUpdateStage::kDownloadingWireless:
    case FirmwareUpdateStage::kInstallingWireless:
    case FirmwareUpdateStage::kDownloadingMain:
    case FirmwareUpdateStage::kRestarting:
      return true;
    default:
      return false;
  }
}

/**
 * @brief 更新界面可见的固件更新阶段、消息和进度
 * @param stage 新阶段
 * @param message 状态消息
 * @param progress_percent 进度百分比
 * @param manual_update_required 是否需要提示用户执行手动更新
 */
void SetStage(FirmwareUpdateStage stage, const char* message,
    int progress_percent = 0, bool manual_update_required = false) {
  if (!LockManager()) {
    return;
  }
  g_manager.snapshot.stage = stage;
  g_manager.snapshot.busy = IsBusyStage(stage);
  g_manager.snapshot.progress_percent =
      std::clamp(progress_percent, 0, 100);
  g_manager.snapshot.manual_update_required =
      stage == FirmwareUpdateStage::kFailed && manual_update_required;
  CopyText(g_manager.snapshot.message, sizeof(g_manager.snapshot.message),
      message);
  UnlockManager();
}

/**
 * @brief 将当前后台任务标记为已经结束
 */
void FinishWorker() {
  if (!LockManager()) {
    return;
  }
  g_manager.worker_running = false;
  g_manager.snapshot.busy = IsBusyStage(g_manager.snapshot.stage);
  UnlockManager();
}

/**
 * @brief 记录固件更新失败原因并结束忙碌状态
 * @param message 面向界面的失败原因
 * @param manual_update_required 是否需要提示用户执行手动更新
 */
void SetFailure(const char* message, bool manual_update_required = false) {
  SetStage(FirmwareUpdateStage::kFailed, message, 0,
      manual_update_required);
  LogMessage(LogLevel::kError, __FILE__, __LINE__,
      "Firmware update failed: %s\n", message == nullptr ? "unknown" : message);
}

/**
 * @brief 读取当前主固件应用版本
 * @param version 版本字符串输出缓冲区
 * @param version_size 输出缓冲区长度
 * @return 读取成功返回 true，否则返回 false
 */
bool ReadCurrentMainVersion(char* version, size_t version_size) {
  const esp_app_desc_t* description = esp_app_get_description();
  if (description == nullptr || description->version[0] == '\0') {
    return false;
  }
  CopyText(version, version_size, description->version);
  return true;
}

/**
 * @brief 读取当前无线固件的 ESP-Hosted 版本
 * @param version 版本字符串输出缓冲区
 * @param version_size 输出缓冲区长度
 * @return 读取成功返回 true，否则返回 false
 */
bool ReadCurrentWirelessVersion(char* version, size_t version_size) {
  esp_hosted_coprocessor_fwver_t hosted_version = {};
  const esp_err_t result =
      esp_hosted_get_coprocessor_fwversion(&hosted_version);
  if (result != ESP_OK) {
    LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
        "Read Wireless firmware version failed: %s\n",
        esp_err_to_name(result));
    return false;
  }
  std::snprintf(version, version_size, "%lu.%lu.%lu",
      static_cast<unsigned long>(hosted_version.major1),
      static_cast<unsigned long>(hosted_version.minor1),
      static_cast<unsigned long>(hosted_version.patch1));
  return true;
}

/**
 * @brief 判断当前 WLAN 是否已经获取 IP 地址
 * @return 网络可用于 HTTPS 下载返回 true，否则返回 false
 */
bool IsNetworkReady() {
  hal::WifiProvider* wifi = nullptr;
  if (LockManager()) {
    wifi = g_manager.wifi;
    UnlockManager();
  }
  hal::WifiStatus status;
  return wifi != nullptr && wifi->ReadWifiStatus(&status) && status.got_ip;
}

/**
 * @brief 判断当前编译目标是否配置了独立的固件更新设备标识
 * @return 已配置受支持设备返回 true，否则返回 false
 */
bool IsFirmwareUpdateDeviceSupported() {
  return kCurrentDeviceId[0] != '\0' && kManifestUrl[0] != '\0';
}

/**
 * @brief 计算从指定 FreeRTOS 时刻开始经过的毫秒数
 * @param started_tick 起始系统节拍
 * @return 已经过的毫秒数
 */
uint32_t ElapsedMilliseconds(TickType_t started_tick) {
  const TickType_t elapsed_ticks = xTaskGetTickCount() - started_tick;
  const uint64_t elapsed_ms =
      static_cast<uint64_t>(elapsed_ticks) * portTICK_PERIOD_MS;
  constexpr uint32_t maximum = std::numeric_limits<uint32_t>::max();
  return elapsed_ms > maximum ? maximum : static_cast<uint32_t>(elapsed_ms);
}

/**
 * @brief 在限定时间内等待无线协处理器控制通道可读取固件版本
 * @param version 版本字符串输出缓冲区
 * @param version_size 输出缓冲区长度
 * @param timeout_ms 最长等待时间，单位为毫秒
 * @return 成功读取版本返回 true，超时返回 false
 */
bool WaitForCurrentWirelessVersion(
    char* version, size_t version_size, uint32_t timeout_ms) {
  hal::WifiProvider* wifi = nullptr;
  if (LockManager()) {
    wifi = g_manager.wifi;
    UnlockManager();
  }
  // 这里只启动 ESP-Hosted/WLAN 驱动，不要求设备已经连接到无线路由器。
  if (wifi == nullptr || !wifi->StartWifi()) {
    return false;
  }
  const TickType_t started_tick = xTaskGetTickCount();
  do {
    esp_hosted_coprocessor_fwver_t hosted_version = {};
    if (esp_hosted_get_coprocessor_fwversion(&hosted_version) == ESP_OK) {
      std::snprintf(version, version_size, "%lu.%lu.%lu",
          static_cast<unsigned long>(hosted_version.major1),
          static_cast<unsigned long>(hosted_version.minor1),
          static_cast<unsigned long>(hosted_version.patch1));
      return true;
    }
    vTaskDelay(pdMS_TO_TICKS(kWirelessReadyPollMs));
  } while (ElapsedMilliseconds(started_tick) < timeout_ms);
  LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
      "Wait for Wireless firmware version timed out\n");
  return false;
}

/**
 * @brief 确认 OTA 启动的新主固件有效并取消回滚
 */
void ConfirmRunningMainFirmware() {
#if defined(CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE)
  const esp_partition_t* running_partition = esp_ota_get_running_partition();
  esp_ota_img_states_t ota_state = ESP_OTA_IMG_UNDEFINED;
  const esp_err_t state_result =
      esp_ota_get_state_partition(running_partition, &ota_state);
  if (state_result != ESP_OK || ota_state != ESP_OTA_IMG_PENDING_VERIFY) {
    return;
  }
  const esp_err_t result = esp_ota_mark_app_valid_cancel_rollback();
  if (result != ESP_OK) {
    LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
        "Mark running Main firmware valid failed: %s\n",
        esp_err_to_name(result));
  }
#endif
}

/**
 * @brief 确保指定的 LittleFS 目录存在
 * @param path 目录绝对路径
 * @return 目录可用返回 true，否则返回 false
 */
bool EnsureLittleFsDirectory(const char* path) {
  if (path == nullptr || path[0] == '\0') {
    return false;
  }
  errno = 0;
  if (mkdir(path, 0775) == 0) {
    return true;
  }
  if (errno == EEXIST) {
    struct stat info = {};
    if (stat(path, &info) == 0 && S_ISDIR(info.st_mode)) {
      return true;
    }
  }
  LogMessage(LogLevel::kError, __FILE__, __LINE__,
      "Create LittleFS directory failed: path=%s errno=%d\n", path, errno);
  return false;
}

/**
 * @brief 确保 LilygoBox OTA、暂存和缓存目录均已创建
 * @return 全部目录可用返回 true，否则返回 false
 */
bool EnsureOtaDirectories() {
  return IsLittleFsStorageMounted() &&
         EnsureLittleFsDirectory(kApplicationDirectory) &&
         EnsureLittleFsDirectory(kOtaDirectory) &&
         EnsureLittleFsDirectory(kOtaStagingDirectory) &&
         EnsureLittleFsDirectory(kCacheDirectory) &&
         EnsureLittleFsDirectory(kOtaCacheDirectory);
}

/**
 * @brief 删除 OTA 缓存目录中的单个文件或子目录
 * @param path 缓存目录内部的绝对路径
 * @param depth 当前子目录深度
 * @return 删除成功或路径不存在返回 true，否则返回 false
 */
bool RemoveOtaCacheEntry(const char* path, size_t depth) {
  if (path == nullptr || path[0] == '\0' || depth > 4) {
    return false;
  }
  const size_t cache_root_length = std::strlen(kOtaCacheDirectory);
  if (std::strncmp(path, kOtaCacheDirectory, cache_root_length) != 0 ||
      path[cache_root_length] != '/') {
    return false;
  }
  struct stat info = {};
  errno = 0;
  if (stat(path, &info) != 0) {
    return errno == ENOENT;
  }
  if (!S_ISDIR(info.st_mode)) {
    errno = 0;
    return std::remove(path) == 0 || errno == ENOENT;
  }
  std::unique_ptr<DIR, decltype(&closedir)> directory(
      opendir(path), &closedir);
  if (directory == nullptr) {
    return false;
  }
  bool success = true;
  while (dirent* entry = readdir(directory.get())) {
    if (std::strcmp(entry->d_name, ".") == 0 ||
        std::strcmp(entry->d_name, "..") == 0) {
      continue;
    }
    char child_path[256] = {};
    const int written = std::snprintf(child_path, sizeof(child_path),
        "%s/%s", path, entry->d_name);
    if (written <= 0 ||
        static_cast<size_t>(written) >= sizeof(child_path) ||
        !RemoveOtaCacheEntry(child_path, depth + 1)) {
      success = false;
    }
  }
  directory.reset();
  errno = 0;
  return (rmdir(path) == 0 || errno == ENOENT) && success;
}

/**
 * @brief 清空 OTA 下载缓存目录中的全部临时文件
 * @return 缓存为空或清理成功返回 true，否则返回 false
 */
bool ClearOtaDownloadCache() {
  if (!EnsureOtaDirectories()) {
    return false;
  }
  std::unique_ptr<DIR, decltype(&closedir)> directory(
      opendir(kOtaCacheDirectory), &closedir);
  if (directory == nullptr) {
    return false;
  }
  bool success = true;
  while (dirent* entry = readdir(directory.get())) {
    if (std::strcmp(entry->d_name, ".") == 0 ||
        std::strcmp(entry->d_name, "..") == 0) {
      continue;
    }
    char path[256] = {};
    const int written = std::snprintf(path, sizeof(path), "%s/%s",
        kOtaCacheDirectory, entry->d_name);
    if (written <= 0 || static_cast<size_t>(written) >= sizeof(path) ||
        !RemoveOtaCacheEntry(path, 0)) {
      success = false;
      LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
          "Remove OTA cache file failed: path=%s errno=%d\n", path, errno);
    }
  }
  return success;
}

/**
 * @brief 清理原子写入中断后遗留的清单临时文件
 */
void CleanupManifestTemporaryFiles() {
  std::remove(kSavedManifestTempPath);
  std::remove(kInstalledManifestTempPath);
}

/**
 * @brief 检查 LittleFS 是否有足够空间下载无线固件并保留安全余量
 * @param firmware_size 无线固件字节数
 * @return 剩余空间充足返回 true，否则返回 false
 */
bool HasWirelessFirmwareDownloadSpace(size_t firmware_size) {
  size_t total_bytes = 0;
  size_t used_bytes = 0;
  if (firmware_size == 0 ||
      !GetLittleFsStorageInfo(&total_bytes, &used_bytes) ||
      used_bytes > total_bytes) {
    return false;
  }
  const size_t free_bytes = total_bytes - used_bytes;
  const size_t reserve_bytes = std::max(
      kMinimumLittleFsFreeReserve, firmware_size / 10);
  if (firmware_size > std::numeric_limits<size_t>::max() - reserve_bytes) {
    return false;
  }
  const size_t required_bytes = firmware_size + reserve_bytes;
  if (free_bytes >= required_bytes) {
    return true;
  }
  LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
      "Not enough LittleFS space for Wireless firmware: free=%u "
      "required=%u firmware=%u reserve=%u\n",
      static_cast<unsigned>(free_bytes),
      static_cast<unsigned>(required_bytes),
      static_cast<unsigned>(firmware_size),
      static_cast<unsigned>(reserve_bytes));
  return false;
}

/**
 * @brief 判断清单提供的下载地址是否为 HTTPS 地址
 * @param url 待检查地址
 * @return 地址有效返回 true，否则返回 false
 */
bool IsHttpsUrl(const char* url) {
  return url != nullptr && std::strncmp(url, "https://", 8) == 0;
}

/**
 * @brief 为 GitHub 下载地址生成备用代理地址
 * @param source_url GitHub 原始下载地址
 * @param destination 备用地址输出缓冲区
 * @param destination_size 输出缓冲区长度
 * @return 地址生成成功返回 true，否则返回 false
 */
bool BuildBackupDownloadUrl(const char* source_url, char* destination,
    size_t destination_size) {
  constexpr char kGithubHttpsPrefix[] = "https://github.com/";
  if (source_url == nullptr || destination == nullptr ||
      destination_size == 0 ||
      std::strncmp(source_url, kGithubHttpsPrefix,
          sizeof(kGithubHttpsPrefix) - 1) != 0) {
    return false;
  }
  const int written = std::snprintf(destination, destination_size, "%s%s",
      kBackupDownloadProxyPrefix, source_url);
  return written > 0 && static_cast<size_t>(written) < destination_size;
}

/**
 * @brief 按清单成功来源生成固件下载地址尝试顺序
 * @param primary_url 固件主下载地址
 * @param backup_url 备用地址输出缓冲区
 * @param backup_url_size 备用地址输出缓冲区长度
 * @param list 下载地址顺序输出
 * @return 至少存在一个有效下载地址返回 true，否则返回 false
 */
bool PrepareFirmwareDownloadSources(const char* primary_url,
    char* backup_url, size_t backup_url_size,
    FirmwareDownloadSourceList* list) {
  if (primary_url == nullptr || primary_url[0] == '\0' ||
      backup_url == nullptr || backup_url_size == 0 || list == nullptr) {
    return false;
  }
  *list = {};
  const bool backup_available = BuildBackupDownloadUrl(
      primary_url, backup_url, backup_url_size);
  if (backup_available &&
      GetPreferredDownloadSource() == FirmwareDownloadSource::kBackup) {
    list->urls[0] = backup_url;
    list->sources[0] = FirmwareDownloadSource::kBackup;
    list->urls[1] = primary_url;
    list->sources[1] = FirmwareDownloadSource::kPrimary;
    list->count = 2;
    return true;
  }
  list->urls[0] = primary_url;
  list->sources[0] = FirmwareDownloadSource::kPrimary;
  list->count = 1;
  if (backup_available) {
    list->urls[1] = backup_url;
    list->sources[1] = FirmwareDownloadSource::kBackup;
    list->count = 2;
  }
  return true;
}

/**
 * @brief 判断 HTTP 下载失败是否适合切换到另一个下载源重试
 * @param result HTTP 客户端执行结果
 * @param status_code HTTP 状态码，未收到响应时为 0
 * @return 网络、限流或服务端错误返回 true，否则返回 false
 */
bool ShouldRetryWithAlternateSource(esp_err_t result, int status_code) {
  if (status_code == 408 || status_code == 429 || status_code >= 500) {
    return true;
  }
  return result != ESP_OK && (status_code == 0 || status_code == 200);
}

/**
 * @brief 解析严格的三段式固件版本号
 * @param text 版本字符串
 * @param parts 三个版本数字的输出缓冲区
 * @return 版本格式为“主版本.次版本.修订版本”返回 true，否则返回 false
 */
bool ParseSemanticVersion(const char* text, uint32_t parts[3]) {
  if (text == nullptr || text[0] == '\0') {
    return false;
  }
  uint32_t parsed_parts[3] = {};
  const char* cursor = text;
  for (size_t index = 0; index < 3; ++index) {
    if (*cursor < '0' || *cursor > '9') {
      return false;
    }
    uint64_t value = 0;
    do {
      value = value * 10 + static_cast<uint64_t>(*cursor - '0');
      if (value > std::numeric_limits<uint32_t>::max()) {
        return false;
      }
      ++cursor;
    } while (*cursor >= '0' && *cursor <= '9');
    parsed_parts[index] = static_cast<uint32_t>(value);
    if (index < 2) {
      if (*cursor != '.') {
        return false;
      }
      ++cursor;
    }
  }
  if (*cursor != '\0') {
    return false;
  }
  if (parts != nullptr) {
    for (size_t index = 0; index < 3; ++index) {
      parts[index] = parsed_parts[index];
    }
  }
  return true;
}

/**
 * @brief 比较两个严格的三段式固件版本号
 * @param left 左侧版本
 * @param right 右侧版本
 * @param valid 比较结果是否有效的输出地址
 * @return 左侧较新返回 1，相同返回 0，较旧返回 -1
 */
int CompareSemanticVersions(
    const char* left, const char* right, bool* valid = nullptr) {
  uint32_t left_parts[3] = {};
  uint32_t right_parts[3] = {};
  const bool parsed = ParseSemanticVersion(left, left_parts) &&
                      ParseSemanticVersion(right, right_parts);
  if (valid != nullptr) {
    *valid = parsed;
  }
  if (!parsed) {
    return 0;
  }
  for (size_t index = 0; index < 3; ++index) {
    if (left_parts[index] != right_parts[index]) {
      return left_parts[index] > right_parts[index] ? 1 : -1;
    }
  }
  return 0;
}

/**
 * @brief 判断清单目标版本是否高于设备当前版本
 * @param current_version 当前版本
 * @param target_version 清单目标版本
 * @param valid 比较结果是否有效的输出地址
 * @return 目标版本更高返回 true，否则返回 false
 */
bool IsVersionUpgrade(const char* current_version, const char* target_version,
    bool* valid = nullptr) {
  bool comparison_valid = false;
  const int comparison = CompareSemanticVersions(
      target_version, current_version, &comparison_valid);
  if (valid != nullptr) {
    *valid = comparison_valid;
  }
  return comparison_valid && comparison > 0;
}

/**
 * @brief 判断文本是否为完整的 SHA-256 十六进制摘要
 * @param text 待检查文本
 * @return 正好包含 64 个十六进制字符返回 true，否则返回 false
 */
bool IsSha256Text(const char* text) {
  if (text == nullptr || std::strlen(text) != kSha256TextLength) {
    return false;
  }
  for (size_t index = 0; index < kSha256TextLength; ++index) {
    const char value = text[index];
    if (!((value >= '0' && value <= '9') ||
            (value >= 'a' && value <= 'f') ||
            (value >= 'A' && value <= 'F'))) {
      return false;
    }
  }
  return true;
}

/**
 * @brief 将十六进制字符转换为数值
 * @param value 十六进制字符
 * @return 对应的 0 至 15 数值，无效字符返回 255
 */
uint8_t HexadecimalValue(char value) {
  if (value >= '0' && value <= '9') {
    return static_cast<uint8_t>(value - '0');
  }
  if (value >= 'a' && value <= 'f') {
    return static_cast<uint8_t>(value - 'a' + 10);
  }
  if (value >= 'A' && value <= 'F') {
    return static_cast<uint8_t>(value - 'A' + 10);
  }
  return 0xFF;
}

/**
 * @brief 比较二进制摘要和清单中的 SHA-256 文本
 * @param digest 32 字节二进制摘要
 * @param expected_sha256 清单摘要文本
 * @return 摘要完全一致返回 true，否则返回 false
 */
bool MatchesSha256(
    const uint8_t digest[kSha256ByteCount], const char* expected_sha256) {
  if (digest == nullptr || !IsSha256Text(expected_sha256)) {
    return false;
  }
  for (size_t index = 0; index < kSha256ByteCount; ++index) {
    const uint8_t high = HexadecimalValue(expected_sha256[index * 2]);
    const uint8_t low = HexadecimalValue(expected_sha256[index * 2 + 1]);
    if (high == 0xFF || low == 0xFF ||
        digest[index] != static_cast<uint8_t>((high << 4) | low)) {
      return false;
    }
  }
  return true;
}

/**
 * @brief 检查组件地址是否属于指定仓库的一个正式 Release
 * @param url 清单组件地址
 * @param asset_name 固定资源文件名
 * @return 仓库、三段式 tag 和文件名均有效返回 true，否则返回 false
 */
bool IsExpectedReleaseAssetUrl(const char* url, const char* asset_name) {
  if (!IsHttpsUrl(url) || asset_name == nullptr ||
      std::strncmp(url, kReleaseDownloadPrefix,
          sizeof(kReleaseDownloadPrefix) - 1) != 0) {
    return false;
  }
  const char* tag = url + sizeof(kReleaseDownloadPrefix) - 1;
  const char* separator = std::strchr(tag, '/');
  if (separator == nullptr || separator == tag || separator[1] == '\0' ||
      std::strchr(separator + 1, '/') != nullptr ||
      std::strcmp(separator + 1, asset_name) != 0) {
    return false;
  }
  const size_t tag_length = static_cast<size_t>(separator - tag);
  char release_tag[32] = {};
  if (tag_length >= sizeof(release_tag)) {
    return false;
  }
  std::memcpy(release_tag, tag, tag_length);
  release_tag[tag_length] = '\0';
  return release_tag[0] == 'v' &&
         ParseSemanticVersion(release_tag + 1, nullptr);
}

/**
 * @brief 从 JSON 对象读取必需的字符串字段
 * @param object JSON 对象
 * @param name 字段名
 * @param destination 目标缓冲区
 * @param destination_size 目标缓冲区长度
 * @return 字段存在且非空返回 true，否则返回 false
 */
bool ReadRequiredJsonString(const cJSON* object, const char* name,
    char* destination, size_t destination_size) {
  const cJSON* item = cJSON_GetObjectItemCaseSensitive(object, name);
  if (!cJSON_IsString(item) || item->valuestring == nullptr ||
      item->valuestring[0] == '\0') {
    return false;
  }
  CopyText(destination, destination_size, item->valuestring);
  return std::strlen(item->valuestring) < destination_size;
}

/**
 * @brief 校验带时区且精确到分钟的 ISO 8601 发布时间
 * @param text 发布时间文本
 * @return 格式和日期范围有效返回 true，否则返回 false
 */
bool IsReleaseTimeText(const char* text) {
  if (text == nullptr || std::strlen(text) != 22 || text[4] != '-' ||
      text[7] != '-' || text[10] != 'T' || text[13] != ':' ||
      (text[16] != '+' && text[16] != '-') || text[19] != ':') {
    return false;
  }
  constexpr int digit_positions[] = {
      0, 1, 2, 3, 5, 6, 8, 9, 11, 12, 14, 15, 17, 18, 20, 21};
  for (int position : digit_positions) {
    if (text[position] < '0' || text[position] > '9') {
      return false;
    }
  }
  const auto pair_value = [text](int position) {
    return (text[position] - '0') * 10 + text[position + 1] - '0';
  };
  const int year = (text[0] - '0') * 1000 + (text[1] - '0') * 100 +
                   (text[2] - '0') * 10 + text[3] - '0';
  const int month = pair_value(5);
  const int day = pair_value(8);
  const int hour = pair_value(11);
  const int minute = pair_value(14);
  const int offset_hour = pair_value(17);
  const int offset_minute = pair_value(20);
  if (year == 0 || month < 1 || month > 12 || hour > 23 || minute > 59 ||
      offset_hour > 14 || offset_minute > 59 ||
      (offset_hour == 14 && offset_minute != 0)) {
    return false;
  }
  constexpr int days_per_month[] = {
      31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  int maximum_day = days_per_month[month - 1];
  const bool leap_year =
      year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
  if (month == 2 && leap_year) {
    maximum_day = 29;
  }
  return day >= 1 && day <= maximum_day;
}

/**
 * @brief 判断固件发布频道是否受支持
 * @param channel 发布频道文本
 * @return stable、beta 或 dev 返回 true，否则返回 false
 */
bool IsReleaseChannelSupported(const char* channel) {
  return channel != nullptr &&
         (std::strcmp(channel, "stable") == 0 ||
             std::strcmp(channel, "beta") == 0 ||
             std::strcmp(channel, "dev") == 0);
}

/**
 * @brief 从 JSON 对象读取受限的正整数字节数
 * @param object JSON 对象
 * @param name 字段名
 * @param value 数值输出地址
 * @return 字段是允许范围内的正整数返回 true，否则返回 false
 */
bool ReadRequiredJsonSize(
    const cJSON* object, const char* name, size_t* value) {
  const cJSON* item = cJSON_GetObjectItemCaseSensitive(object, name);
  if (!cJSON_IsNumber(item) || value == nullptr || item->valuedouble <= 0 ||
      item->valuedouble > static_cast<double>(kMaximumFirmwareAssetSize)) {
    return false;
  }
  const size_t parsed = static_cast<size_t>(item->valuedouble);
  if (static_cast<double>(parsed) != item->valuedouble) {
    return false;
  }
  *value = parsed;
  return true;
}

/**
 * @brief 解析 GitHub Release 中的组合固件清单
 * @param json_text 清单 JSON 文本
 * @param manifest 清单解析结果
 * @return 成功、清单无效或清单版本不受支持
 */
ManifestParseResult ParseManifest(
    const char* json_text, FirmwareReleaseManifest* manifest) {
  if (json_text == nullptr || manifest == nullptr) {
    return ManifestParseResult::kInvalid;
  }
  std::unique_ptr<cJSON, decltype(&cJSON_Delete)> root(
      cJSON_Parse(json_text), &cJSON_Delete);
  if (root == nullptr || !cJSON_IsObject(root.get())) {
    return ManifestParseResult::kInvalid;
  }
  const cJSON* manifest_version =
      cJSON_GetObjectItemCaseSensitive(root.get(), "manifest_version");
  const cJSON* release_time =
      cJSON_GetObjectItemCaseSensitive(root.get(), "release_time");
  const cJSON* release_channel =
      cJSON_GetObjectItemCaseSensitive(root.get(), "channel");
  const cJSON* main =
      cJSON_GetObjectItemCaseSensitive(root.get(), "esp32p4");
  const cJSON* wireless =
      cJSON_GetObjectItemCaseSensitive(root.get(), "esp32c6");
  // 当前固定清单文件名永久属于版本 1；附加字段应被旧程序安全忽略。
  // 如果新字段会改变安装语义，必须启用新的清单文件名。
  if (!cJSON_IsNumber(manifest_version) ||
      manifest_version->valuedouble != manifest_version->valueint) {
    return ManifestParseResult::kInvalid;
  }
  if (manifest_version->valueint != kSupportedManifestVersion) {
    return ManifestParseResult::kUnsupportedVersion;
  }
  if (!cJSON_IsObject(main) || !cJSON_IsObject(wireless)) {
    return ManifestParseResult::kInvalid;
  }
  if (release_time != nullptr &&
      (!cJSON_IsString(release_time) || release_time->valuestring == nullptr ||
          !IsReleaseTimeText(release_time->valuestring))) {
    return ManifestParseResult::kInvalid;
  }

  FirmwareReleaseManifest parsed;
  CopyText(parsed.release_channel, sizeof(parsed.release_channel), "stable");
  if (release_channel != nullptr) {
    if (!cJSON_IsString(release_channel) ||
        release_channel->valuestring == nullptr ||
        !IsReleaseChannelSupported(release_channel->valuestring) ||
        std::strlen(release_channel->valuestring) >=
            sizeof(parsed.release_channel)) {
      return ManifestParseResult::kInvalid;
    }
    CopyText(parsed.release_channel, sizeof(parsed.release_channel),
        release_channel->valuestring);
  }
  if (!ReadRequiredJsonString(root.get(), "device_id", parsed.device_id,
          sizeof(parsed.device_id)) ||
      !ReadRequiredJsonString(root.get(), "release",
          parsed.release_version, sizeof(parsed.release_version)) ||
      !ReadRequiredJsonString(main, "version", parsed.main_version,
          sizeof(parsed.main_version)) ||
      !ReadRequiredJsonString(
          main, "url", parsed.main_url, sizeof(parsed.main_url)) ||
      !ReadRequiredJsonSize(
          main, "size_bytes", &parsed.main_size_bytes) ||
      !ReadRequiredJsonString(main, "sha256", parsed.main_sha256,
          sizeof(parsed.main_sha256)) ||
      !ReadRequiredJsonString(wireless, "version",
          parsed.wireless_version, sizeof(parsed.wireless_version)) ||
      !ReadRequiredJsonString(wireless, "url", parsed.wireless_url,
          sizeof(parsed.wireless_url)) ||
      !ReadRequiredJsonSize(
          wireless, "size_bytes", &parsed.wireless_size_bytes) ||
      !ReadRequiredJsonString(wireless, "sha256", parsed.wireless_sha256,
          sizeof(parsed.wireless_sha256)) ||
      parsed.release_version[0] != 'v' ||
      !ParseSemanticVersion(parsed.release_version + 1, nullptr) ||
      !ParseSemanticVersion(parsed.main_version, nullptr) ||
      !ParseSemanticVersion(parsed.wireless_version, nullptr) ||
      !IsSha256Text(parsed.main_sha256) ||
      !IsSha256Text(parsed.wireless_sha256) ||
      !IsExpectedReleaseAssetUrl(
          parsed.main_url, kMainReleaseAssetName) ||
      !IsExpectedReleaseAssetUrl(
          parsed.wireless_url, kWirelessReleaseAssetName)) {
    return ManifestParseResult::kInvalid;
  }
  if (release_time != nullptr) {
    CopyText(parsed.release_time, sizeof(parsed.release_time),
        release_time->valuestring);
  }

  const cJSON* notes =
      cJSON_GetObjectItemCaseSensitive(root.get(), "whats_new");
  if (cJSON_IsArray(notes)) {
    const cJSON* note = nullptr;
    cJSON_ArrayForEach(note, notes) {
      if (parsed.note_count >= kFirmwareUpdateNoteCapacity) {
        break;
      }
      if (!cJSON_IsString(note) || note->valuestring == nullptr ||
          note->valuestring[0] == '\0' ||
          std::strlen(note->valuestring) >= sizeof(parsed.notes[0])) {
        return ManifestParseResult::kInvalid;
      }
      CopyText(parsed.notes[parsed.note_count], sizeof(parsed.notes[0]),
          note->valuestring);
      ++parsed.note_count;
    }
  }
  *manifest = parsed;
  return ManifestParseResult::kSuccess;
}

/**
 * @brief 将有效清单原子保存到 OTA 专用目录
 * @param json_text 清单 JSON 文本
 * @return 保存成功返回 true，否则返回 false
 */
bool SaveManifest(const char* json_text) {
  if (!EnsureOtaDirectories() || json_text == nullptr) {
    return false;
  }
  std::remove(kSavedManifestTempPath);
  std::unique_ptr<FILE, decltype(&std::fclose)> file(
      std::fopen(kSavedManifestTempPath, "wb"), &std::fclose);
  if (file == nullptr) {
    return false;
  }
  const size_t size = std::strlen(json_text);
  const bool written = std::fwrite(json_text, 1, size, file.get()) == size &&
                       std::fflush(file.get()) == 0;
  file.reset();
  if (!written) {
    std::remove(kSavedManifestTempPath);
    return false;
  }
  std::remove(kSavedManifestPath);
  if (std::rename(kSavedManifestTempPath, kSavedManifestPath) != 0) {
    std::remove(kSavedManifestTempPath);
    return false;
  }
  return true;
}

/**
 * @brief 从 LittleFS 指定路径读取并解析固件清单
 * @param path 固件清单路径
 * @param manifest 清单解析结果
 * @return 读取并解析成功返回 true，否则返回 false
 */
bool LoadManifestFile(
    const char* path, FirmwareReleaseManifest* manifest) {
  std::unique_ptr<FILE, decltype(&std::fclose)> file(
      std::fopen(path, "rb"), &std::fclose);
  if (file == nullptr || std::fseek(file.get(), 0, SEEK_END) != 0) {
    return false;
  }
  const long file_size = std::ftell(file.get());
  if (file_size <= 0 ||
      static_cast<size_t>(file_size) > kManifestMaximumSize) {
    return false;
  }
  std::rewind(file.get());
  auto text = std::make_unique<char[]>(static_cast<size_t>(file_size) + 1);
  if (text == nullptr ||
      std::fread(text.get(), 1, static_cast<size_t>(file_size), file.get()) !=
          static_cast<size_t>(file_size)) {
    return false;
  }
  text[static_cast<size_t>(file_size)] = '\0';
  return ParseManifest(text.get(), manifest) ==
      ManifestParseResult::kSuccess;
}

/**
 * @brief 读取最近一次在线检查保存的固件清单
 * @param manifest 清单解析结果
 * @return 读取并解析成功返回 true，否则返回 false
 */
bool LoadSavedManifest(FirmwareReleaseManifest* manifest) {
  return LoadManifestFile(kSavedManifestPath, manifest);
}

/**
 * @brief 读取当前已安装版本对应的固件清单
 * @param manifest 清单解析结果
 * @return 读取并解析成功返回 true，否则返回 false
 */
bool LoadInstalledManifest(FirmwareReleaseManifest* manifest) {
  return LoadManifestFile(kInstalledManifestPath, manifest);
}

/**
 * @brief 将最近一次检查清单保存为当前已安装版本记录
 * @return 原子复制成功返回 true，否则返回 false
 */
bool SaveInstalledManifest() {
  if (!EnsureOtaDirectories()) {
    return false;
  }
  std::unique_ptr<FILE, decltype(&std::fclose)> source(
      std::fopen(kSavedManifestPath, "rb"), &std::fclose);
  std::remove(kInstalledManifestTempPath);
  std::unique_ptr<FILE, decltype(&std::fclose)> destination(
      std::fopen(kInstalledManifestTempPath, "wb"), &std::fclose);
  if (source == nullptr || destination == nullptr) {
    std::remove(kInstalledManifestTempPath);
    return false;
  }
  uint8_t buffer[1024] = {};
  bool copied = true;
  while (copied) {
    const size_t size = std::fread(buffer, 1, sizeof(buffer), source.get());
    if (size > 0 &&
        std::fwrite(buffer, 1, size, destination.get()) != size) {
      copied = false;
      break;
    }
    if (size < sizeof(buffer)) {
      copied = std::feof(source.get()) != 0;
      break;
    }
  }
  copied = copied && std::fflush(destination.get()) == 0;
  source.reset();
  destination.reset();
  if (!copied) {
    std::remove(kInstalledManifestTempPath);
    return false;
  }
  std::remove(kInstalledManifestPath);
  if (std::rename(
          kInstalledManifestTempPath, kInstalledManifestPath) != 0) {
    std::remove(kInstalledManifestTempPath);
    return false;
  }
  return true;
}

/**
 * @brief 在线检查覆盖缓存前保留当前已安装版本的日志
 */
void PreserveInstalledManifestBeforeCheck() {
  FirmwareReleaseManifest saved_manifest;
  char main_current[32] = {};
  char wireless_current[32] = {};
  if (!LoadSavedManifest(&saved_manifest) ||
      !ReadCurrentMainVersion(main_current, sizeof(main_current)) ||
      !ReadCurrentWirelessVersion(
          wireless_current, sizeof(wireless_current)) ||
      std::strcmp(saved_manifest.main_version, main_current) != 0 ||
      std::strcmp(saved_manifest.wireless_version, wireless_current) != 0) {
    return;
  }
  if (!SaveInstalledManifest()) {
    LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
        "Preserve installed firmware release notes failed\n");
  }
}

/**
 * @brief 接收固件清单 HTTP 响应内容
 * @param event HTTP 客户端事件
 * @return 接收成功返回 ESP_OK，否则返回 ESP_FAIL
 */
esp_err_t ManifestDownloadEventHandler(esp_http_client_event_t* event) {
  if (event == nullptr || event->user_data == nullptr) {
    return ESP_ERR_INVALID_ARG;
  }
  auto* context = static_cast<ManifestDownloadContext*>(event->user_data);
  if (ElapsedMilliseconds(context->started_tick) >=
      context->timeout_ms) {
    context->timed_out = true;
    return ESP_ERR_TIMEOUT;
  }
  if (event->event_id != HTTP_EVENT_ON_DATA || event->data_len <= 0) {
    return ESP_OK;
  }
  if (esp_http_client_get_status_code(event->client) != 200) {
    return ESP_OK;
  }
  const size_t incoming_size = static_cast<size_t>(event->data_len);
  if (context->data == nullptr ||
      incoming_size > kManifestMaximumSize - context->size) {
    context->overflow = true;
    return ESP_FAIL;
  }
  std::memcpy(context->data + context->size, event->data, incoming_size);
  context->size += incoming_size;
  context->data[context->size] = '\0';
  return ESP_OK;
}

/**
 * @brief 从指定地址执行一次固件清单下载
 * @param url 固件清单下载地址
 * @param buffer 清单内容输出缓冲区
 * @param timeout_ms 本次下载超时时间，单位为毫秒
 * @param context 本次下载状态
 * @param result HTTP 客户端执行结果
 * @param status_code HTTP 状态码
 * @return 收到完整非空的 HTTP 200 响应返回 true，否则返回 false
 */
bool DownloadManifestFromUrl(const char* url, char* buffer,
    uint32_t timeout_ms, ManifestDownloadContext* context,
    esp_err_t* result, int* status_code) {
  if (url == nullptr || buffer == nullptr || context == nullptr ||
      result == nullptr || status_code == nullptr) {
    return false;
  }
  buffer[0] = '\0';
  *context = {};
  context->data = buffer;
  context->started_tick = xTaskGetTickCount();
  context->timeout_ms = timeout_ms;
  *result = ESP_FAIL;
  *status_code = 0;

  esp_http_client_config_t config = {};
  config.url = url;
  config.crt_bundle_attach = esp_crt_bundle_attach;
  config.timeout_ms = static_cast<int>(timeout_ms);
  config.buffer_size = kHttpBufferSize;
  config.buffer_size_tx = kHttpBufferSize;
  config.event_handler = ManifestDownloadEventHandler;
  config.user_data = context;
  config.keep_alive_enable = true;
  config.max_redirection_count = 5;
  esp_http_client_handle_t client = esp_http_client_init(&config);
  if (client == nullptr) {
    return false;
  }
  *result = esp_http_client_perform(client);
  *status_code = esp_http_client_get_status_code(client);
  esp_http_client_cleanup(client);
  return *result == ESP_OK && *status_code == 200 && !context->overflow &&
         context->size > 0;
}

/**
 * @brief 从主下载源或备用源下载并解析最新固件清单
 * @param manifest 清单解析结果
 * @return 下载、解析和持久化均成功返回 true，否则返回 false
 */
bool DownloadManifest(FirmwareReleaseManifest* manifest) {
  auto buffer = std::make_unique<char[]>(kManifestMaximumSize + 1);
  if (buffer == nullptr) {
    return false;
  }
  char backup_url[kMaximumDownloadUrlLength] = {};
  const bool backup_available = BuildBackupDownloadUrl(
      kManifestUrl, backup_url, sizeof(backup_url));
  const char* download_urls[] = {kManifestUrl, backup_url};
  const uint32_t download_timeouts[] = {
      kManifestPrimaryTimeoutMs, kManifestBackupTimeoutMs};
  const FirmwareDownloadSource download_sources[] = {
      FirmwareDownloadSource::kPrimary,
      FirmwareDownloadSource::kBackup};
  ManifestDownloadContext context;
  esp_err_t result = ESP_FAIL;
  int status_code = 0;
  bool downloaded = false;
  FirmwareDownloadSource successful_source =
      FirmwareDownloadSource::kPrimary;
  const size_t source_count = backup_available ? 2 : 1;
  for (size_t source_index = 0; source_index < source_count;
       ++source_index) {
    downloaded = DownloadManifestFromUrl(download_urls[source_index],
        buffer.get(), download_timeouts[source_index], &context, &result,
        &status_code);
    if (downloaded) {
      successful_source = download_sources[source_index];
      break;
    }
    LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
        "Download firmware manifest failed: source=%s result=%s "
        "HTTP=%d size=%u\n",
        FirmwareDownloadSourceName(download_sources[source_index]),
        esp_err_to_name(result), status_code,
        static_cast<unsigned>(context.size));
    const bool may_retry = source_index == 0 && backup_available &&
        !context.overflow && IsNetworkReady() &&
        ShouldRetryWithAlternateSource(result, status_code);
    if (!may_retry) {
      break;
    }
    LogMessage(LogLevel::kInfo, __FILE__, __LINE__,
        "Retry firmware manifest through backup source\n");
  }
  if (!downloaded) {
    if (!IsNetworkReady()) {
      SetFailure("Wi-Fi lost while checking");
    } else if (context.timed_out ||
               ElapsedMilliseconds(context.started_tick) >=
                   context.timeout_ms) {
      SetFailure("Update check timed out");
    } else {
      SetFailure("Update information unavailable");
    }
    return false;
  }
  const ManifestParseResult parse_result =
      ParseManifest(buffer.get(), manifest);
  if (parse_result != ManifestParseResult::kSuccess) {
    if (parse_result == ManifestParseResult::kUnsupportedVersion) {
      SetFailure("Manual firmware update required", true);
    } else {
      SetFailure("Update information invalid");
    }
    return false;
  }
  if (std::strcmp(manifest->device_id, kCurrentDeviceId) != 0) {
    SetFailure("Update package is for another device");
    return false;
  }
  if (!SaveManifest(buffer.get())) {
    SetFailure("Cannot save update information");
    return false;
  }
  SetPreferredDownloadSource(successful_source);
  return true;
}

/**
 * @brief 将固件字节数格式化为界面使用的容量文本
 * @param size_bytes 固件字节数
 * @param destination 输出缓冲区
 * @param destination_size 输出缓冲区长度
 */
void FormatFirmwareSize(size_t size_bytes, char* destination,
    size_t destination_size) {
  if (destination == nullptr || destination_size == 0) {
    return;
  }
  constexpr double bytes_per_kilobyte = 1024.0;
  constexpr double bytes_per_megabyte = 1024.0 * 1024.0;
  if (size_bytes >= static_cast<size_t>(bytes_per_megabyte)) {
    std::snprintf(destination, destination_size, "%.1f MB",
        static_cast<double>(size_bytes) / bytes_per_megabyte);
  } else {
    std::snprintf(destination, destination_size, "%.0f KB",
        static_cast<double>(size_bytes) / bytes_per_kilobyte);
  }
}

/**
 * @brief 使用清单和当前版本刷新界面状态
 * @param manifest 已验证的固件清单
 * @param main_current 当前主固件版本
 * @param wireless_current 当前无线固件版本
 * @return 当前版本可比较并成功刷新状态返回 true，否则返回 false
 */
bool ApplyManifestSnapshot(const FirmwareReleaseManifest& manifest,
    const char* main_current, const char* wireless_current,
    bool save_as_installed = false) {
  bool main_version_valid = false;
  bool wireless_version_valid = false;
  const bool main_update_available = IsVersionUpgrade(
      main_current, manifest.main_version, &main_version_valid);
  const bool wireless_update_available = IsVersionUpgrade(
      wireless_current, manifest.wireless_version, &wireless_version_valid);
  if (!main_version_valid || !wireless_version_valid) {
    SetFailure("Installed firmware version invalid");
    return false;
  }
  const bool update_available =
      main_update_available || wireless_update_available;
  const size_t package_size_bytes =
      (main_update_available ? manifest.main_size_bytes : 0) +
      (wireless_update_available ? manifest.wireless_size_bytes : 0);
  FirmwareReleaseManifest installed_manifest;
  bool installed_manifest_valid = false;
  if (!update_available) {
    installed_manifest = manifest;
    installed_manifest_valid = true;
    if (save_as_installed && !SaveInstalledManifest()) {
      LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
          "Save installed firmware release notes failed\n");
    }
  } else if (LoadInstalledManifest(&installed_manifest) &&
             std::strcmp(installed_manifest.main_version, main_current) == 0 &&
             std::strcmp(
                 installed_manifest.wireless_version, wireless_current) == 0) {
    installed_manifest_valid = true;
  }
  if (!LockManager()) {
    return false;
  }
  g_manager.manifest = manifest;
  g_manager.manifest_valid = true;
  g_manager.snapshot.manifest_available = true;
  g_manager.snapshot.update_available = update_available;
  g_manager.snapshot.main_update_available = main_update_available;
  g_manager.snapshot.wireless_update_available =
      wireless_update_available;
  g_manager.snapshot.progress_percent = 0;
  CopyText(g_manager.snapshot.release_version,
      sizeof(g_manager.snapshot.release_version), manifest.release_version);
  CopyText(g_manager.snapshot.release_channel,
      sizeof(g_manager.snapshot.release_channel), manifest.release_channel);
  CopyText(g_manager.snapshot.release_time,
      sizeof(g_manager.snapshot.release_time), manifest.release_time);
  if (installed_manifest_valid) {
    CopyText(g_manager.snapshot.current_release_version,
        sizeof(g_manager.snapshot.current_release_version),
        installed_manifest.release_version);
    CopyText(g_manager.snapshot.current_release_channel,
        sizeof(g_manager.snapshot.current_release_channel),
        installed_manifest.release_channel);
    CopyText(g_manager.snapshot.current_release_time,
        sizeof(g_manager.snapshot.current_release_time),
        installed_manifest.release_time);
  } else {
    CopyReleaseVersion(g_manager.snapshot.current_release_version,
        sizeof(g_manager.snapshot.current_release_version), main_current);
    CopyText(g_manager.snapshot.current_release_channel,
        sizeof(g_manager.snapshot.current_release_channel), "stable");
    g_manager.snapshot.current_release_time[0] = '\0';
  }
  FormatFirmwareSize(package_size_bytes, g_manager.snapshot.package_size,
      sizeof(g_manager.snapshot.package_size));
  FormatFirmwareSize(manifest.main_size_bytes, g_manager.snapshot.main_size,
      sizeof(g_manager.snapshot.main_size));
  FormatFirmwareSize(manifest.wireless_size_bytes,
      g_manager.snapshot.wireless_size,
      sizeof(g_manager.snapshot.wireless_size));
  if (installed_manifest_valid) {
    FormatFirmwareSize(installed_manifest.main_size_bytes,
        g_manager.snapshot.current_main_size,
        sizeof(g_manager.snapshot.current_main_size));
    FormatFirmwareSize(installed_manifest.wireless_size_bytes,
        g_manager.snapshot.current_wireless_size,
        sizeof(g_manager.snapshot.current_wireless_size));
  } else {
    g_manager.snapshot.current_main_size[0] = '\0';
    g_manager.snapshot.current_wireless_size[0] = '\0';
  }
  CopyText(g_manager.snapshot.main_current_version,
      sizeof(g_manager.snapshot.main_current_version), main_current);
  CopyText(g_manager.snapshot.main_target_version,
      sizeof(g_manager.snapshot.main_target_version), manifest.main_version);
  CopyText(g_manager.snapshot.wireless_current_version,
      sizeof(g_manager.snapshot.wireless_current_version), wireless_current);
  CopyText(g_manager.snapshot.wireless_target_version,
      sizeof(g_manager.snapshot.wireless_target_version),
      manifest.wireless_version);
  g_manager.snapshot.note_count = manifest.note_count;
  for (size_t index = 0; index < kFirmwareUpdateNoteCapacity; ++index) {
    CopyText(g_manager.snapshot.notes[index],
        sizeof(g_manager.snapshot.notes[index]), manifest.notes[index]);
    CopyText(g_manager.snapshot.current_notes[index],
        sizeof(g_manager.snapshot.current_notes[index]),
        installed_manifest_valid ? installed_manifest.notes[index] : "");
  }
  g_manager.snapshot.current_note_count = installed_manifest_valid
      ? installed_manifest.note_count
      : 0;
  g_manager.snapshot.stage = update_available
      ? FirmwareUpdateStage::kUpdateAvailable
      : FirmwareUpdateStage::kUpToDate;
  g_manager.snapshot.busy = false;
  g_manager.snapshot.manual_update_required = false;
  CopyText(g_manager.snapshot.message, sizeof(g_manager.snapshot.message),
      update_available ? "New version available" : "Firmware is up to date");
  UnlockManager();
  return true;
}

/**
 * @brief 在线检查失败时恢复本地已安装版本及日志
 * @param failure_message 页面显示的检查失败原因
 * @return 本地记录与当前固件一致并恢复成功返回 true，否则返回 false
 */
bool ApplyInstalledManifestFallback(const char* failure_message) {
  FirmwareReleaseManifest installed_manifest;
  char main_current[32] = {};
  char wireless_current[32] = {};
  if (!LoadInstalledManifest(&installed_manifest) ||
      !ReadCurrentMainVersion(main_current, sizeof(main_current)) ||
      !ReadCurrentWirelessVersion(
          wireless_current, sizeof(wireless_current)) ||
      std::strcmp(installed_manifest.main_version, main_current) != 0 ||
      std::strcmp(installed_manifest.wireless_version, wireless_current) != 0 ||
      !ApplyManifestSnapshot(
          installed_manifest, main_current, wireless_current)) {
    return false;
  }
  SetFailure(failure_message);
  return true;
}

/**
 * @brief 将重启续跑清单恢复到界面快照
 * @param manifest 已验证的本地固件清单
 */
void ApplyPendingManifestSnapshot(
    const FirmwareReleaseManifest& manifest) {
  char main_current[32] = {};
  ReadCurrentMainVersion(main_current, sizeof(main_current));
  if (!LockManager()) {
    return;
  }
  g_manager.manifest = manifest;
  g_manager.manifest_valid = true;
  g_manager.snapshot.manifest_available = true;
  g_manager.snapshot.update_available = true;
  CopyText(g_manager.snapshot.release_version,
      sizeof(g_manager.snapshot.release_version), manifest.release_version);
  CopyText(g_manager.snapshot.release_channel,
      sizeof(g_manager.snapshot.release_channel), manifest.release_channel);
  CopyText(g_manager.snapshot.release_time,
      sizeof(g_manager.snapshot.release_time), manifest.release_time);
  const size_t maximum_pending_size =
      manifest.main_size_bytes + manifest.wireless_size_bytes;
  FormatFirmwareSize(maximum_pending_size, g_manager.snapshot.package_size,
      sizeof(g_manager.snapshot.package_size));
  FormatFirmwareSize(manifest.main_size_bytes, g_manager.snapshot.main_size,
      sizeof(g_manager.snapshot.main_size));
  FormatFirmwareSize(manifest.wireless_size_bytes,
      g_manager.snapshot.wireless_size,
      sizeof(g_manager.snapshot.wireless_size));
  CopyText(g_manager.snapshot.main_current_version,
      sizeof(g_manager.snapshot.main_current_version), main_current);
  CopyReleaseVersion(g_manager.snapshot.current_release_version,
      sizeof(g_manager.snapshot.current_release_version), main_current);
  CopyText(g_manager.snapshot.current_release_channel,
      sizeof(g_manager.snapshot.current_release_channel), "stable");
  g_manager.snapshot.current_release_time[0] = '\0';
  g_manager.snapshot.current_main_size[0] = '\0';
  g_manager.snapshot.current_wireless_size[0] = '\0';
  CopyText(g_manager.snapshot.main_target_version,
      sizeof(g_manager.snapshot.main_target_version), manifest.main_version);
  CopyText(g_manager.snapshot.wireless_target_version,
      sizeof(g_manager.snapshot.wireless_target_version),
      manifest.wireless_version);
  g_manager.snapshot.note_count = manifest.note_count;
  for (size_t index = 0; index < kFirmwareUpdateNoteCapacity; ++index) {
    CopyText(g_manager.snapshot.notes[index],
        sizeof(g_manager.snapshot.notes[index]), manifest.notes[index]);
  }
  UnlockManager();
}

/**
 * @brief 从固件文件指定偏移读取数据
 * @param file 已打开的固件文件
 * @param file_size 文件总长度
 * @param offset 读取偏移
 * @param destination 目标缓冲区
 * @param size 读取长度
 * @return 读取成功返回 true，否则返回 false
 */
bool ReadFirmwareFile(FILE* file, size_t file_size, size_t offset,
    void* destination, size_t size) {
  if (file == nullptr || destination == nullptr || offset > file_size ||
      size > file_size - offset ||
      std::fseek(file, static_cast<long>(offset), SEEK_SET) != 0) {
    return false;
  }
  return std::fread(destination, 1, size, file) == size;
}

/**
 * @brief 获取固件文件长度并恢复到文件开头
 * @param file 已打开的固件文件
 * @param file_size 文件长度输出地址
 * @return 文件非空且读取成功返回 true，否则返回 false
 */
bool GetFirmwareFileSize(FILE* file, size_t* file_size) {
  if (file == nullptr || file_size == nullptr ||
      std::fseek(file, 0, SEEK_END) != 0) {
    return false;
  }
  const long end_offset = std::ftell(file);
  if (end_offset <= 0) {
    return false;
  }
  std::rewind(file);
  *file_size = static_cast<size_t>(end_offset);
  return true;
}

/**
 * @brief 校验已打开文件的精确长度和 SHA-256
 * @param file 已打开的文件
 * @param file_size 实际文件长度
 * @param expected_size 清单要求的文件长度
 * @param expected_sha256 清单要求的 SHA-256
 * @return 长度和摘要均一致返回 true，否则返回 false
 */
bool VerifyFileIntegrity(FILE* file, size_t file_size, size_t expected_size,
    const char* expected_sha256) {
  if (file == nullptr || file_size != expected_size ||
      !IsSha256Text(expected_sha256) || std::fseek(file, 0, SEEK_SET) != 0) {
    return false;
  }
  auto buffer = std::make_unique<uint8_t[]>(kHashReadChunkSize);
  if (buffer == nullptr) {
    return false;
  }
  mbedtls_sha256_context sha256_context;
  mbedtls_sha256_init(&sha256_context);
  bool success = mbedtls_sha256_starts(&sha256_context, 0) == 0;
  size_t hashed_size = 0;
  while (success && hashed_size < file_size) {
    const size_t chunk_size =
        std::min(kHashReadChunkSize, file_size - hashed_size);
    success = std::fread(buffer.get(), 1, chunk_size, file) == chunk_size &&
              mbedtls_sha256_update(
                  &sha256_context, buffer.get(), chunk_size) == 0;
    hashed_size += success ? chunk_size : 0;
  }
  uint8_t digest[kSha256ByteCount] = {};
  success = success &&
            mbedtls_sha256_finish(&sha256_context, digest) == 0 &&
            MatchesSha256(digest, expected_sha256);
  mbedtls_sha256_free(&sha256_context);
  std::rewind(file);
  return success;
}

/**
 * @brief 校验主固件 OTA 分区中已写入镜像的精确长度范围和 SHA-256
 * @param partition 主固件目标 OTA 分区
 * @param image_size 清单要求的镜像长度
 * @param expected_sha256 清单要求的 SHA-256
 * @param interrupted_by 校验期间收到的传输控制请求输出地址
 * @return 指定镜像范围摘要一致返回 true，否则返回 false
 */
bool VerifyPartitionIntegrity(const esp_partition_t* partition,
    size_t image_size, const char* expected_sha256,
    TransferRequest* interrupted_by = nullptr) {
  if (interrupted_by != nullptr) {
    *interrupted_by = TransferRequest::kNone;
  }
  if (partition == nullptr || image_size == 0 ||
      image_size > partition->size || !IsSha256Text(expected_sha256)) {
    return false;
  }
  auto buffer = std::make_unique<uint8_t[]>(kHashReadChunkSize);
  if (buffer == nullptr) {
    return false;
  }
  mbedtls_sha256_context sha256_context;
  mbedtls_sha256_init(&sha256_context);
  bool success = mbedtls_sha256_starts(&sha256_context, 0) == 0;
  size_t hashed_size = 0;
  while (success && hashed_size < image_size) {
    if (interrupted_by != nullptr) {
      *interrupted_by = ReadTransferRequest();
      if (*interrupted_by != TransferRequest::kNone) {
        success = false;
        break;
      }
    }
    const size_t chunk_size =
        std::min(kHashReadChunkSize, image_size - hashed_size);
    success = esp_partition_read(
                  partition, hashed_size, buffer.get(), chunk_size) == ESP_OK &&
              mbedtls_sha256_update(
                  &sha256_context, buffer.get(), chunk_size) == 0;
    hashed_size += success ? chunk_size : 0;
  }
  uint8_t digest[kSha256ByteCount] = {};
  success = success &&
            mbedtls_sha256_finish(&sha256_context, digest) == 0 &&
            MatchesSha256(digest, expected_sha256);
  mbedtls_sha256_free(&sha256_context);
  return success;
}

/**
 * @brief 根据 ESP 镜像头计算应用镜像完整长度
 * @param file 已打开的固件文件
 * @param file_size 文件总长度
 * @param image_offset 应用镜像起始偏移
 * @param image_header 应用镜像头
 * @param image_size 应用镜像长度输出地址
 * @return 镜像结构有效返回 true，否则返回 false
 */
bool CalculateImageSize(FILE* file, size_t file_size, size_t image_offset,
    const esp_image_header_t& image_header, size_t* image_size) {
  if (image_size == nullptr || image_header.segment_count == 0 ||
      image_header.segment_count > kMaximumImageSegmentCount) {
    return false;
  }
  size_t cursor = image_offset + sizeof(esp_image_header_t);
  size_t total_size = sizeof(esp_image_header_t);
  for (uint8_t index = 0; index < image_header.segment_count; ++index) {
    esp_image_segment_header_t segment_header = {};
    if (!ReadFirmwareFile(file, file_size, cursor, &segment_header,
            sizeof(segment_header))) {
      return false;
    }
    cursor += sizeof(segment_header);
    total_size += sizeof(segment_header);
    if (cursor > file_size || segment_header.data_len > file_size - cursor) {
      return false;
    }
    cursor += segment_header.data_len;
    total_size += segment_header.data_len;
  }
  total_size += 16 - total_size % 16;
  if (image_header.hash_appended == 1) {
    total_size += 32;
  }
  if (image_offset > file_size || total_size > file_size - image_offset) {
    return false;
  }
  *image_size = total_size;
  return true;
}

/**
 * @brief 在独立或合并固件中查找无线协处理器的 network_adapter 镜像
 * @param file 已打开的固件文件
 * @param file_size 文件总长度
 * @param image_info 镜像信息输出地址
 * @return 找到匹配镜像返回 true，否则返回 false
 */
bool FindWirelessFirmwareImage(
    FILE* file, size_t file_size, FirmwareImageInfo* image_info) {
  if (file == nullptr || image_info == nullptr) {
    return false;
  }
  const size_t minimum_size = sizeof(esp_image_header_t) +
      sizeof(esp_image_segment_header_t) + sizeof(esp_app_desc_t);
  for (size_t offset = 0; offset + minimum_size <= file_size;
       offset += kImageSearchAlignment) {
    esp_image_header_t header = {};
    if (!ReadFirmwareFile(file, file_size, offset, &header, sizeof(header)) ||
        header.magic != ESP_IMAGE_HEADER_MAGIC ||
        header.chip_id != kExpectedWirelessChipId ||
        header.segment_count == 0 ||
        header.segment_count > kMaximumImageSegmentCount) {
      continue;
    }
    esp_app_desc_t description = {};
    const size_t description_offset = offset + sizeof(esp_image_header_t) +
                                      sizeof(esp_image_segment_header_t);
    if (!ReadFirmwareFile(file, file_size, description_offset, &description,
            sizeof(description)) ||
        description.magic_word != ESP_APP_DESC_MAGIC_WORD ||
        std::strncmp(description.project_name, kWirelessFirmwareProjectName,
            sizeof(kWirelessFirmwareProjectName)) != 0) {
      continue;
    }
    size_t image_size = 0;
    if (!CalculateImageSize(
            file, file_size, offset, header, &image_size)) {
      continue;
    }
    image_info->offset = offset;
    image_info->size = image_size;
    image_info->app_desc = description;
    return true;
  }
  return false;
}

/**
 * @brief 检查 LittleFS 中的无线固件文件
 * @param path 固件文件路径
 * @param manifest 已验证的固件清单
 * @param image_info 镜像信息输出地址
 * @return 长度、摘要、芯片、项目名和版本均匹配返回 true，否则返回 false
 */
bool InspectWirelessFirmware(const char* path,
    const FirmwareReleaseManifest& manifest,
    FirmwareImageInfo* image_info) {
  std::unique_ptr<FILE, decltype(&std::fclose)> file(
      std::fopen(path, "rb"), &std::fclose);
  size_t file_size = 0;
  FirmwareImageInfo parsed;
  if (file == nullptr || !GetFirmwareFileSize(file.get(), &file_size) ||
      !VerifyFileIntegrity(file.get(), file_size,
          manifest.wireless_size_bytes, manifest.wireless_sha256) ||
      !FindWirelessFirmwareImage(file.get(), file_size, &parsed) ||
      std::strncmp(parsed.app_desc.version, manifest.wireless_version,
          sizeof(parsed.app_desc.version)) != 0) {
    return false;
  }
  if (image_info != nullptr) {
    *image_info = parsed;
  }
  return true;
}

/**
 * @brief 将无线固件 HTTP 数据写入 LittleFS 临时文件
 * @param event HTTP 客户端事件
 * @return 写入成功返回 ESP_OK，否则返回 ESP_FAIL
 */
esp_err_t WirelessDownloadEventHandler(esp_http_client_event_t* event) {
  if (event == nullptr || event->user_data == nullptr) {
    return ESP_ERR_INVALID_ARG;
  }
  auto* context = static_cast<FirmwareDownloadContext*>(event->user_data);
  const TransferRequest request = ReadTransferRequest();
  if (request == TransferRequest::kCancel) {
    context->cancel_requested = true;
    return ESP_FAIL;
  }
  if (request == TransferRequest::kPause) {
    context->pause_requested = true;
    return ESP_FAIL;
  }
  if (ElapsedMilliseconds(context->started_tick) >=
      kFirmwareDownloadTimeoutMs) {
    context->timed_out = true;
    return ESP_ERR_TIMEOUT;
  }
  if (event->event_id != HTTP_EVENT_ON_DATA || event->data_len <= 0) {
    return ESP_OK;
  }
  if (esp_http_client_get_status_code(event->client) != 200) {
    return ESP_OK;
  }
  const size_t incoming_size = static_cast<size_t>(event->data_len);
  if (context->downloaded_size > context->expected_size ||
      incoming_size > context->expected_size - context->downloaded_size) {
    context->overflow = true;
    return ESP_FAIL;
  }
  if (context->file == nullptr ||
      std::fwrite(event->data, 1, event->data_len, context->file) !=
          incoming_size) {
    context->write_failed = true;
    return ESP_FAIL;
  }
  context->downloaded_size += incoming_size;
  const int progress = context->expected_size > 0
      ? static_cast<int>(context->downloaded_size * 100 /
            context->expected_size)
      : 0;
  SetStage(FirmwareUpdateStage::kDownloadingWireless,
      "Downloading Wireless firmware", progress);
  return ESP_OK;
}

/**
 * @brief 下载并验证无线固件到 LittleFS 的 OTA 专用目录
 * @param manifest 已验证的固件清单
 * @return 下载文件有效并安装成功返回 true，否则返回 false
 */
FirmwareDownloadResult DownloadWirelessFirmware(
    const FirmwareReleaseManifest& manifest) {
  if (!ClearOtaDownloadCache()) {
    SetFailure("Cannot prepare OTA download storage");
    return FirmwareDownloadResult::kFailed;
  }
  std::remove(kWirelessFirmwarePath);
  if (!HasWirelessFirmwareDownloadSpace(manifest.wireless_size_bytes)) {
    SetFailure("Not enough storage for Wireless firmware");
    return FirmwareDownloadResult::kFailed;
  }
  char backup_url[kMaximumDownloadUrlLength] = {};
  FirmwareDownloadSourceList download_sources;
  if (!PrepareFirmwareDownloadSources(manifest.wireless_url, backup_url,
          sizeof(backup_url), &download_sources)) {
    SetFailure("Wireless firmware download address invalid");
    return FirmwareDownloadResult::kFailed;
  }
  for (size_t source_index = 0; source_index < download_sources.count;
       ++source_index) {
    const TransferRequest request_before_download = ReadTransferRequest();
    if (request_before_download != TransferRequest::kNone) {
      std::remove(kWirelessFirmwareTempPath);
      return request_before_download == TransferRequest::kCancel
          ? FirmwareDownloadResult::kCancelled
          : FirmwareDownloadResult::kPaused;
    }
    std::remove(kWirelessFirmwareTempPath);
    std::unique_ptr<FILE, decltype(&std::fclose)> output(
        std::fopen(kWirelessFirmwareTempPath, "wb"), &std::fclose);
    if (output == nullptr) {
      SetFailure("Cannot create Wireless firmware file");
      return FirmwareDownloadResult::kFailed;
    }
    FirmwareDownloadContext context;
    context.file = output.get();
    context.expected_size = manifest.wireless_size_bytes;
    context.started_tick = xTaskGetTickCount();
    esp_http_client_config_t config = {};
    config.url = download_sources.urls[source_index];
    config.crt_bundle_attach = esp_crt_bundle_attach;
    config.timeout_ms = kFirmwareHttpTimeoutMs;
    config.buffer_size = kHttpBufferSize;
    config.buffer_size_tx = kHttpBufferSize;
    config.event_handler = WirelessDownloadEventHandler;
    config.user_data = &context;
    config.keep_alive_enable = true;
    config.max_redirection_count = 5;
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == nullptr) {
      output.reset();
      std::remove(kWirelessFirmwareTempPath);
      SetFailure("Cannot start Wireless firmware download");
      return FirmwareDownloadResult::kFailed;
    }
    SetActiveHttpClient(client);
    SetStage(FirmwareUpdateStage::kDownloadingWireless,
        "Downloading Wireless firmware", 0);
    const esp_err_t result = esp_http_client_perform(client);
    ClearActiveHttpClient();
    const int status_code = esp_http_client_get_status_code(client);
    const int64_t content_length =
        esp_http_client_get_content_length(client);
    if (std::fflush(output.get()) != 0) {
      context.write_failed = true;
    }
    output.reset();
    esp_http_client_cleanup(client);
    TransferRequest final_request = ReadTransferRequest();
    if (context.cancel_requested || context.pause_requested ||
        final_request != TransferRequest::kNone) {
      std::remove(kWirelessFirmwareTempPath);
      const bool cancelled = context.cancel_requested ||
                             final_request == TransferRequest::kCancel;
      return cancelled ? FirmwareDownloadResult::kCancelled
                       : FirmwareDownloadResult::kPaused;
    }
    const bool complete_length = content_length <= 0 ||
        context.downloaded_size == static_cast<size_t>(content_length);
    const bool transfer_valid = result == ESP_OK && status_code == 200 &&
        !context.write_failed && !context.overflow &&
        context.downloaded_size > 0 && complete_length &&
        context.downloaded_size == manifest.wireless_size_bytes;
    if (!transfer_valid) {
      std::remove(kWirelessFirmwareTempPath);
      LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
          "Download Wireless firmware failed: source=%s result=%s HTTP=%d "
          "size=%u\n",
          FirmwareDownloadSourceName(
              download_sources.sources[source_index]),
          esp_err_to_name(result), status_code,
          static_cast<unsigned>(context.downloaded_size));
      const bool may_retry = source_index == 0 &&
          download_sources.count > 1 &&
          !context.write_failed && !context.overflow && !context.timed_out &&
          IsNetworkReady() &&
          ShouldRetryWithAlternateSource(result, status_code);
      if (may_retry) {
        LogMessage(LogLevel::kInfo, __FILE__, __LINE__,
            "Retry Wireless firmware through alternate source\n");
        continue;
      }
      if (!IsNetworkReady()) {
        SetFailure("Wi-Fi lost during Wireless firmware download");
      } else if (context.timed_out ||
                 ElapsedMilliseconds(context.started_tick) >=
                     kFirmwareDownloadTimeoutMs) {
        SetFailure("Wireless firmware download timed out");
      } else {
        SetFailure("Downloaded Wireless firmware invalid");
      }
      return FirmwareDownloadResult::kFailed;
    }
    FirmwareImageInfo image_info;
    const bool image_valid = InspectWirelessFirmware(
        kWirelessFirmwareTempPath, manifest, &image_info);
    final_request = ReadTransferRequest();
    if (final_request != TransferRequest::kNone) {
      std::remove(kWirelessFirmwareTempPath);
      return final_request == TransferRequest::kCancel
          ? FirmwareDownloadResult::kCancelled
          : FirmwareDownloadResult::kPaused;
    }
    if (!image_valid) {
      std::remove(kWirelessFirmwareTempPath);
      SetFailure("Downloaded Wireless firmware invalid");
      return FirmwareDownloadResult::kFailed;
    }
    std::remove(kWirelessFirmwarePath);
    if (std::rename(kWirelessFirmwareTempPath, kWirelessFirmwarePath) != 0) {
      std::remove(kWirelessFirmwareTempPath);
      SetFailure("Cannot save Wireless firmware file");
      return FirmwareDownloadResult::kFailed;
    }
    SetPreferredDownloadSource(
        download_sources.sources[source_index]);
    return FirmwareDownloadResult::kCompleted;
  }
  SetFailure("Wireless firmware download failed");
  return FirmwareDownloadResult::kFailed;
}

/**
 * @brief 检查是否存在重启后继续更新主固件的标记
 * @return 标记存在返回 true，否则返回 false
 */
bool HasPendingUpdate() {
  std::unique_ptr<FILE, decltype(&std::fclose)> marker(
      std::fopen(kPendingUpdatePath, "rb"), &std::fclose);
  return marker != nullptr;
}

/**
 * @brief 写入重启后继续组合更新的标记
 * @return 写入成功返回 true，否则返回 false
 */
bool SetPendingUpdate() {
  if (HasPendingUpdate()) {
    return true;
  }
  if (!EnsureOtaDirectories()) {
    return false;
  }
  std::unique_ptr<FILE, decltype(&std::fclose)> marker(
      std::fopen(kPendingUpdatePath, "wb"), &std::fclose);
  if (marker == nullptr) {
    return false;
  }
  const bool written = std::fwrite("1", 1, 1, marker.get()) == 1 &&
                       std::fflush(marker.get()) == 0;
  marker.reset();
  if (!written) {
    std::remove(kPendingUpdatePath);
  }
  return written;
}

/**
 * @brief 清除组合更新续跑标记
 * @return 标记已清除或原本不存在返回 true，否则返回 false
 */
bool ClearPendingUpdate() {
  errno = 0;
  if (std::remove(kPendingUpdatePath) == 0 || errno == ENOENT) {
    return true;
  }
  LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
      "Remove pending firmware update marker failed: errno=%d\n", errno);
  return false;
}

/**
 * @brief 清理 OTA 产生的无线固件临时文件
 */
void CleanupWirelessFiles() {
  ClearOtaDownloadCache();
  std::remove(kWirelessFirmwarePath);
}

/**
 * @brief 启动时清理不再需要的 OTA 临时文件
 * @return OTA 存储目录可用且缓存清理成功返回 true，否则返回 false
 */
bool PrepareOtaStorageOnStartup() {
  if (!ClearOtaDownloadCache()) {
    return false;
  }
  CleanupManifestTemporaryFiles();
  bool staging_cleaned = true;
  if (!HasPendingUpdate()) {
    errno = 0;
    if (std::remove(kWirelessFirmwarePath) != 0 && errno != ENOENT) {
      LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
          "Remove stale Wireless firmware staging file failed: errno=%d\n",
          errno);
      staging_cleaned = false;
    }
  }
  return staging_cleaned;
}

bool RestoreRunningBootPartition() {
  const esp_partition_t* running_partition =
      esp_ota_get_running_partition();
  if (running_partition == nullptr) {
    return false;
  }
  const esp_partition_t* boot_partition = esp_ota_get_boot_partition();
  if (boot_partition != nullptr &&
      boot_partition->address == running_partition->address) {
    return true;
  }
  return esp_ota_set_boot_partition(running_partition) == ESP_OK;
}

bool CancelPreparedFirmware(bool finish_worker = false);

void SetDownloadPaused() {
  const bool marker_cleared = ClearPendingUpdate();
  if (!LockManager()) {
    return;
  }
  if (g_manager.cancel_requested) {
    UnlockManager();
    CancelPreparedFirmware(true);
    return;
  }
  g_manager.pause_requested = false;
  g_manager.cancel_requested = false;
  g_manager.worker_running = false;
  g_manager.snapshot.stage = marker_cleared
      ? FirmwareUpdateStage::kPaused
      : FirmwareUpdateStage::kFailed;
  g_manager.snapshot.busy = false;
  CopyText(g_manager.snapshot.message, sizeof(g_manager.snapshot.message),
      marker_cleared ? "Download paused"
                     : "Cannot pause firmware update safely");
  UnlockManager();
  if (!marker_cleared) {
    LogMessage(LogLevel::kError, __FILE__, __LINE__,
        "Firmware update pause marker cleanup failed\n");
  }
}

bool CancelPreparedFirmware(bool finish_worker) {
  const bool boot_restored = RestoreRunningBootPartition();
  const bool marker_cleared = ClearPendingUpdate();
  CleanupWirelessFiles();
  if (!LockManager()) {
    return false;
  }
  g_manager.pause_requested = false;
  g_manager.cancel_requested = false;
  if (finish_worker) {
    g_manager.worker_running = false;
  }
  const bool cancelled = boot_restored && marker_cleared;
  g_manager.snapshot.stage = cancelled
      ? FirmwareUpdateStage::kUpdateAvailable
      : FirmwareUpdateStage::kFailed;
  g_manager.snapshot.busy = false;
  g_manager.snapshot.progress_percent = 0;
  CopyText(g_manager.snapshot.message, sizeof(g_manager.snapshot.message),
      cancelled ? "Update cancelled"
                : "Cannot restore current firmware safely");
  UnlockManager();
  if (!cancelled) {
    LogMessage(LogLevel::kError, __FILE__, __LINE__,
        "Firmware update cancellation cleanup failed\n");
  }
  return cancelled;
}

/**
 * @brief 在两个固件处理阶段之间立即响应暂停或取消请求
 * @return 已处理传输控制请求返回 true，否则返回 false
 */
bool HandlePendingDownloadInterruption() {
  const TransferRequest request = ReadTransferRequest();
  if (request == TransferRequest::kCancel) {
    CancelPreparedFirmware(true);
    return true;
  }
  if (request == TransferRequest::kPause) {
    SetDownloadPaused();
    return true;
  }
  return false;
}

void FinishPreparedDownload() {
  if (!ClearPendingUpdate()) {
    if (!LockManager()) {
      return;
    }
    g_manager.pause_requested = false;
    g_manager.cancel_requested = false;
    g_manager.worker_running = false;
    g_manager.snapshot.stage = FirmwareUpdateStage::kFailed;
    g_manager.snapshot.busy = false;
    CopyText(g_manager.snapshot.message, sizeof(g_manager.snapshot.message),
        "Cannot finalize firmware download safely");
    UnlockManager();
    return;
  }
  if (!LockManager()) {
    return;
  }
  const TransferRequest request = g_manager.cancel_requested
      ? TransferRequest::kCancel
      : g_manager.pause_requested ? TransferRequest::kPause
                                  : TransferRequest::kNone;
  if (request == TransferRequest::kNone) {
    g_manager.pause_requested = false;
    g_manager.cancel_requested = false;
    g_manager.worker_running = false;
    g_manager.snapshot.stage = FirmwareUpdateStage::kReadyToInstall;
    g_manager.snapshot.busy = false;
    g_manager.snapshot.progress_percent = 100;
    CopyText(g_manager.snapshot.message, sizeof(g_manager.snapshot.message),
        "Ready to install and restart");
    UnlockManager();
    return;
  }
  UnlockManager();
  if (request == TransferRequest::kCancel) {
    CancelPreparedFirmware(true);
  } else {
    SetDownloadPaused();
  }
}

/**
 * @brief 判断当前无线固件是否支持显式激活 OTA 镜像
 * @param version 当前无线固件的 ESP-Hosted 版本
 * @return 版本不低于 2.6.0 返回 true，否则返回 false
 */
bool SupportsWirelessOtaActivate(
    const esp_hosted_coprocessor_fwver_t& version) {
  return version.major1 > 2 ||
         (version.major1 == 2 && version.minor1 >= 6);
}

/**
 * @brief 将 LittleFS 中的目标固件写入无线协处理器
 * @param manifest 已验证的固件清单
 * @param keep_marker_on_failure 失败时是否保留续跑标记
 * @return 更新结果
 */
WirelessUpdateResult UpdateWirelessFirmware(
    const FirmwareReleaseManifest& manifest, bool keep_marker_on_failure) {
  char current_version[32] = {};
  if (!ReadCurrentWirelessVersion(
          current_version, sizeof(current_version))) {
    SetFailure("Cannot read Wireless firmware version");
    return WirelessUpdateResult::kFailed;
  }
  bool version_valid = false;
  const bool update_required = IsVersionUpgrade(current_version,
      manifest.wireless_version, &version_valid);
  if (!version_valid) {
    SetFailure("Wireless firmware version invalid");
    return WirelessUpdateResult::kFailed;
  }
  if (!update_required) {
    return WirelessUpdateResult::kNotRequired;
  }
  FirmwareImageInfo image_info;
  if (!InspectWirelessFirmware(
          kWirelessFirmwarePath, manifest, &image_info)) {
    SetFailure("Stored Wireless firmware invalid");
    return WirelessUpdateResult::kFailed;
  }
  std::unique_ptr<FILE, decltype(&std::fclose)> file(
      std::fopen(kWirelessFirmwarePath, "rb"), &std::fclose);
  size_t file_size = 0;
  if (file == nullptr || !GetFirmwareFileSize(file.get(), &file_size) ||
      !SetPendingUpdate()) {
    SetFailure("Cannot prepare Wireless firmware update");
    return WirelessUpdateResult::kFailed;
  }
  auto chunk = std::make_unique<uint8_t[]>(kWirelessFirmwareChunkSize);
  if (chunk == nullptr) {
    if (!keep_marker_on_failure) {
      ClearPendingUpdate();
    }
    SetFailure("Not enough memory for Wireless firmware update");
    return WirelessUpdateResult::kFailed;
  }

  esp_hosted_coprocessor_fwver_t hosted_version = {};
  if (esp_hosted_get_coprocessor_fwversion(&hosted_version) != ESP_OK) {
    if (!keep_marker_on_failure) {
      ClearPendingUpdate();
    }
    SetFailure("Cannot read Wireless firmware OTA capability");
    return WirelessUpdateResult::kFailed;
  }
  SetStage(FirmwareUpdateStage::kInstallingWireless,
      "Installing Wireless firmware", 0);
  esp_err_t result = esp_hosted_slave_ota_begin();
  if (result != ESP_OK) {
    if (!keep_marker_on_failure) {
      ClearPendingUpdate();
    }
    SetFailure("Cannot start Wireless firmware update");
    return WirelessUpdateResult::kFailed;
  }
  size_t sent_size = 0;
  while (sent_size < image_info.size) {
    const size_t chunk_size = std::min(
        kWirelessFirmwareChunkSize, image_info.size - sent_size);
    if (!ReadFirmwareFile(file.get(), file_size,
            image_info.offset + sent_size, chunk.get(), chunk_size) ||
        esp_hosted_slave_ota_write(
            chunk.get(), static_cast<uint32_t>(chunk_size)) != ESP_OK) {
      esp_hosted_slave_ota_end();
      if (!keep_marker_on_failure) {
        ClearPendingUpdate();
      }
      SetFailure("Writing Wireless firmware failed");
      return WirelessUpdateResult::kFailed;
    }
    sent_size += chunk_size;
    SetStage(FirmwareUpdateStage::kInstallingWireless,
        "Installing Wireless firmware",
        static_cast<int>(sent_size * 100 / image_info.size));
  }
  result = esp_hosted_slave_ota_end();
  if (result != ESP_OK ||
      (SupportsWirelessOtaActivate(hosted_version) &&
          esp_hosted_slave_ota_activate() != ESP_OK)) {
    if (!keep_marker_on_failure) {
      ClearPendingUpdate();
    }
    SetFailure("Wireless firmware verification failed");
    return WirelessUpdateResult::kFailed;
  }
  SetStage(FirmwareUpdateStage::kRestarting,
      "Restarting to finish the update", 100);
  vTaskDelay(pdMS_TO_TICKS(kRestartDelayMs));
  esp_restart();
  return WirelessUpdateResult::kRestarting;
}

/**
 * @brief 检查下载到主固件 OTA 分区中的应用描述信息
 * @param new_app 新应用描述信息
 * @param manifest 已验证的固件清单
 * @return 项目名和版本均符合清单返回 true，否则返回 false
 */
bool ValidateMainFirmwareImage(const esp_app_desc_t& new_app,
    const FirmwareReleaseManifest& manifest) {
  const esp_app_desc_t* running_app = esp_app_get_description();
  return running_app != nullptr &&
         std::strncmp(new_app.project_name, running_app->project_name,
             sizeof(new_app.project_name)) == 0 &&
         std::strncmp(new_app.version, manifest.main_version,
             sizeof(new_app.version)) == 0;
}

/**
 * @brief 通过 HTTPS 将主固件写入备用 OTA 分区
 * @param manifest 已验证的固件清单
 * @param keep_marker_on_failure 失败时是否保留续跑标记
 * @return 更新结果
 */
MainUpdateResult UpdateMainFirmware(
    const FirmwareReleaseManifest& manifest, bool keep_marker_on_failure,
    bool activate_when_ready) {
  char current_version[32] = {};
  if (!ReadCurrentMainVersion(current_version, sizeof(current_version))) {
    SetFailure("Cannot read Main firmware version");
    return MainUpdateResult::kFailed;
  }
  bool version_valid = false;
  const bool update_required = IsVersionUpgrade(
      current_version, manifest.main_version, &version_valid);
  if (!version_valid) {
    SetFailure("Main firmware version invalid");
    return MainUpdateResult::kFailed;
  }
  if (!update_required) {
    return MainUpdateResult::kNotRequired;
  }
  if (activate_when_ready && !SetPendingUpdate()) {
    SetFailure("Cannot save Main firmware update state");
    return MainUpdateResult::kFailed;
  }
  const esp_partition_t* update_partition =
      esp_ota_get_next_update_partition(nullptr);
  if (update_partition == nullptr || manifest.main_size_bytes == 0 ||
      manifest.main_size_bytes > update_partition->size) {
    if (!keep_marker_on_failure) {
      ClearPendingUpdate();
    }
    SetFailure("Main firmware does not fit OTA partition");
    return MainUpdateResult::kFailed;
  }
  char backup_url[kMaximumDownloadUrlLength] = {};
  FirmwareDownloadSourceList download_sources;
  if (!PrepareFirmwareDownloadSources(manifest.main_url, backup_url,
          sizeof(backup_url), &download_sources)) {
    if (!keep_marker_on_failure) {
      ClearPendingUpdate();
    }
    SetFailure("Main firmware download address invalid");
    return MainUpdateResult::kFailed;
  }
  esp_https_ota_handle_t ota_handle = nullptr;
  esp_err_t result = ESP_FAIL;
  TransferRequest interrupted_by = TransferRequest::kNone;
  bool download_completed = false;
  for (size_t source_index = 0; source_index < download_sources.count;
       ++source_index) {
    interrupted_by = ReadTransferRequest();
    if (interrupted_by != TransferRequest::kNone) {
      ClearPendingUpdate();
      return interrupted_by == TransferRequest::kCancel
          ? MainUpdateResult::kCancelled
          : MainUpdateResult::kPaused;
    }
    esp_http_client_config_t http_config = {};
    http_config.url = download_sources.urls[source_index];
    http_config.crt_bundle_attach = esp_crt_bundle_attach;
    http_config.timeout_ms = kFirmwareHttpTimeoutMs;
    http_config.buffer_size = kHttpBufferSize;
    http_config.buffer_size_tx = kHttpBufferSize;
    http_config.keep_alive_enable = true;
    http_config.max_redirection_count = 5;
    esp_https_ota_config_t ota_config = {};
    ota_config.http_config = &http_config;
    ota_config.http_client_init_cb = FirmwareOtaHttpClientInitialized;

    SetStage(FirmwareUpdateStage::kDownloadingMain,
        "Downloading Main firmware", 0);
    ota_handle = nullptr;
    result = esp_https_ota_begin(&ota_config, &ota_handle);
    if (result != ESP_OK) {
      ClearActiveHttpClient();
      LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
          "Start Main firmware download failed: source=%s result=%s\n",
          FirmwareDownloadSourceName(
              download_sources.sources[source_index]),
          esp_err_to_name(result));
      interrupted_by = ReadTransferRequest();
      if (interrupted_by != TransferRequest::kNone) {
        ClearPendingUpdate();
        return interrupted_by == TransferRequest::kCancel
            ? MainUpdateResult::kCancelled
            : MainUpdateResult::kPaused;
      }
      if (source_index == 0 && download_sources.count > 1 &&
          IsNetworkReady()) {
        LogMessage(LogLevel::kInfo, __FILE__, __LINE__,
            "Retry Main firmware through alternate source\n");
        continue;
      }
      if (!keep_marker_on_failure) {
        ClearPendingUpdate();
      }
      SetFailure("Cannot start Main firmware download");
      return MainUpdateResult::kFailed;
    }
    interrupted_by = ReadTransferRequest();
    if (interrupted_by != TransferRequest::kNone) {
      ClearActiveHttpClient();
      esp_https_ota_abort(ota_handle);
      ota_handle = nullptr;
      ClearPendingUpdate();
      return interrupted_by == TransferRequest::kCancel
          ? MainUpdateResult::kCancelled
          : MainUpdateResult::kPaused;
    }
    esp_app_desc_t new_app = {};
    result = esp_https_ota_get_img_desc(ota_handle, &new_app);
    interrupted_by = ReadTransferRequest();
    if (interrupted_by != TransferRequest::kNone) {
      ClearActiveHttpClient();
      esp_https_ota_abort(ota_handle);
      ota_handle = nullptr;
      ClearPendingUpdate();
      return interrupted_by == TransferRequest::kCancel
          ? MainUpdateResult::kCancelled
          : MainUpdateResult::kPaused;
    }
    const bool image_description_valid = result == ESP_OK &&
        ValidateMainFirmwareImage(new_app, manifest);
    if (!image_description_valid) {
      ClearActiveHttpClient();
      esp_https_ota_abort(ota_handle);
      ota_handle = nullptr;
      interrupted_by = ReadTransferRequest();
      if (interrupted_by != TransferRequest::kNone) {
        ClearPendingUpdate();
        return interrupted_by == TransferRequest::kCancel
            ? MainUpdateResult::kCancelled
            : MainUpdateResult::kPaused;
      }
      const bool may_retry = result != ESP_OK && source_index == 0 &&
          download_sources.count > 1 && IsNetworkReady();
      if (may_retry) {
        LogMessage(LogLevel::kInfo, __FILE__, __LINE__,
            "Retry Main firmware image header through alternate source\n");
        continue;
      }
      if (!keep_marker_on_failure) {
        ClearPendingUpdate();
      }
      SetFailure("Downloaded Main firmware invalid");
      return MainUpdateResult::kFailed;
    }
    const TickType_t download_started_tick = xTaskGetTickCount();
    bool download_timed_out = false;
    bool download_size_invalid = false;
    interrupted_by = TransferRequest::kNone;
    do {
      interrupted_by = ReadTransferRequest();
      if (interrupted_by != TransferRequest::kNone) {
        result = ESP_FAIL;
        break;
      }
      if (ElapsedMilliseconds(download_started_tick) >=
          kFirmwareDownloadTimeoutMs) {
        download_timed_out = true;
        result = ESP_ERR_TIMEOUT;
        break;
      }
      result = esp_https_ota_perform(ota_handle);
      if (interrupted_by == TransferRequest::kNone) {
        interrupted_by = ReadTransferRequest();
      }
      if (interrupted_by != TransferRequest::kNone) {
        result = ESP_FAIL;
        break;
      }
      const int image_read = esp_https_ota_get_image_len_read(ota_handle);
      if (image_read >= 0 &&
          static_cast<size_t>(image_read) > manifest.main_size_bytes) {
        download_size_invalid = true;
        result = ESP_ERR_INVALID_SIZE;
        break;
      }
      const int progress = image_read >= 0
          ? static_cast<int>(static_cast<size_t>(image_read) * 100 /
                manifest.main_size_bytes)
          : 0;
      SetStage(FirmwareUpdateStage::kDownloadingMain,
          "Downloading Main firmware", progress);
    } while (result == ESP_ERR_HTTPS_OTA_IN_PROGRESS);
    if (interrupted_by != TransferRequest::kNone) {
      ClearActiveHttpClient();
      esp_https_ota_abort(ota_handle);
      ClearPendingUpdate();
      return interrupted_by == TransferRequest::kCancel
          ? MainUpdateResult::kCancelled
          : MainUpdateResult::kPaused;
    }
    const bool complete_data = result == ESP_OK &&
        esp_https_ota_is_complete_data_received(ota_handle);
    if (!complete_data) {
      ClearActiveHttpClient();
      esp_https_ota_abort(ota_handle);
      ota_handle = nullptr;
      LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
          "Download Main firmware failed: source=%s result=%s\n",
          FirmwareDownloadSourceName(
              download_sources.sources[source_index]),
          esp_err_to_name(result));
      const bool may_retry = source_index == 0 &&
          download_sources.count > 1 && !download_timed_out &&
          !download_size_invalid && IsNetworkReady();
      if (may_retry) {
        LogMessage(LogLevel::kInfo, __FILE__, __LINE__,
            "Retry Main firmware through alternate source\n");
        continue;
      }
      if (!keep_marker_on_failure) {
        ClearPendingUpdate();
      }
      if (!IsNetworkReady()) {
        SetFailure("Wi-Fi lost during Main firmware download");
      } else if (download_timed_out) {
        SetFailure("Main firmware download timed out");
      } else {
        SetFailure("Main firmware download failed");
      }
      return MainUpdateResult::kFailed;
    }
    const int downloaded_size = esp_https_ota_get_image_len_read(ota_handle);
    if (downloaded_size < 0 ||
        static_cast<size_t>(downloaded_size) != manifest.main_size_bytes) {
      ClearActiveHttpClient();
      esp_https_ota_abort(ota_handle);
      ota_handle = nullptr;
      if (!keep_marker_on_failure) {
        ClearPendingUpdate();
      }
      SetFailure("Downloaded Main firmware size mismatch");
      return MainUpdateResult::kFailed;
    }
    SetPreferredDownloadSource(
        download_sources.sources[source_index]);
    download_completed = true;
    break;
  }
  if (!download_completed || ota_handle == nullptr) {
    ClearActiveHttpClient();
    if (!keep_marker_on_failure) {
      ClearPendingUpdate();
    }
    SetFailure("Main firmware download failed");
    return MainUpdateResult::kFailed;
  }
  interrupted_by = ReadTransferRequest();
  if (interrupted_by != TransferRequest::kNone) {
    ClearActiveHttpClient();
    esp_https_ota_abort(ota_handle);
    ClearPendingUpdate();
    return interrupted_by == TransferRequest::kCancel
        ? MainUpdateResult::kCancelled
        : MainUpdateResult::kPaused;
  }
  ClearActiveHttpClient();
  result = esp_https_ota_finish(ota_handle);
  if (result != ESP_OK) {
    if (!keep_marker_on_failure) {
      ClearPendingUpdate();
    }
    SetFailure("Main firmware verification failed");
    return MainUpdateResult::kFailed;
  }
  interrupted_by = ReadTransferRequest();
  if (interrupted_by != TransferRequest::kNone) {
    if (interrupted_by == TransferRequest::kPause &&
        !RestoreRunningBootPartition()) {
      ClearPendingUpdate();
      SetFailure("Cannot restore Main firmware boot partition");
      return MainUpdateResult::kFailed;
    }
    ClearPendingUpdate();
    return interrupted_by == TransferRequest::kCancel
        ? MainUpdateResult::kCancelled
        : MainUpdateResult::kPaused;
  }
  const esp_partition_t* boot_partition = esp_ota_get_boot_partition();
  bool partition_valid = boot_partition != nullptr &&
      boot_partition->address == update_partition->address;
  if (partition_valid) {
    partition_valid = VerifyPartitionIntegrity(boot_partition,
        manifest.main_size_bytes, manifest.main_sha256, &interrupted_by);
  }
  if (interrupted_by != TransferRequest::kNone) {
    if (interrupted_by == TransferRequest::kPause &&
        !RestoreRunningBootPartition()) {
      ClearPendingUpdate();
      SetFailure("Cannot restore Main firmware boot partition");
      return MainUpdateResult::kFailed;
    }
    ClearPendingUpdate();
    return interrupted_by == TransferRequest::kCancel
        ? MainUpdateResult::kCancelled
        : MainUpdateResult::kPaused;
  }
  if (!partition_valid) {
    const esp_partition_t* running_partition = esp_ota_get_running_partition();
    const bool boot_restored = running_partition != nullptr &&
        esp_ota_set_boot_partition(running_partition) == ESP_OK;
    if (!keep_marker_on_failure) {
      ClearPendingUpdate();
    }
    SetFailure(boot_restored ? "Main firmware SHA-256 mismatch"
                             : "Cannot restore Main firmware boot partition");
    return MainUpdateResult::kFailed;
  }
  if (!activate_when_ready) {
    const esp_partition_t* running_partition =
        esp_ota_get_running_partition();
    if (running_partition == nullptr ||
        esp_ota_set_boot_partition(running_partition) != ESP_OK) {
      SetFailure("Cannot defer Main firmware installation");
      return MainUpdateResult::kFailed;
    }
    ClearPendingUpdate();
    if (ReadTransferRequest() == TransferRequest::kCancel) {
      return MainUpdateResult::kCancelled;
    }
    return MainUpdateResult::kPrepared;
  }
  SetStage(FirmwareUpdateStage::kRestarting,
      "Restarting into the new firmware", 100);
  vTaskDelay(pdMS_TO_TICKS(kRestartDelayMs));
  esp_restart();
  return MainUpdateResult::kRestarting;
}

/**
 * @brief 执行一次最新固件检查任务
 * @param context FreeRTOS 任务参数，本任务未使用
 */
void CheckTask(void* context) {
  static_cast<void>(context);
  PreserveInstalledManifestBeforeCheck();
  if (!IsNetworkReady()) {
    if (!ApplyInstalledManifestFallback("Wi-Fi is not connected")) {
      SetFailure("Wi-Fi is not connected");
    }
    FinishWorker();
    vTaskDelete(nullptr);
    return;
  }
  SetStage(FirmwareUpdateStage::kChecking,
      "Loading update information");
  FirmwareReleaseManifest manifest;
  char main_current[32] = {};
  char wireless_current[32] = {};
  if (!DownloadManifest(&manifest)) {
    bool manual_update_required = false;
    if (LockManager()) {
      manual_update_required =
          g_manager.snapshot.manual_update_required;
      UnlockManager();
    }
    if (!manual_update_required) {
      ApplyInstalledManifestFallback(
          "Unable to check updates; showing installed version");
    }
    FinishWorker();
    vTaskDelete(nullptr);
    return;
  }
  if (!ReadCurrentMainVersion(main_current, sizeof(main_current)) ||
      !ReadCurrentWirelessVersion(
          wireless_current, sizeof(wireless_current))) {
    SetFailure("Installed versions unavailable");
  } else {
    ApplyManifestSnapshot(
        manifest, main_current, wireless_current, true);
  }
  FinishWorker();
  vTaskDelete(nullptr);
}

/**
 * @brief 执行用户确认的主固件与无线固件组合更新任务
 * @param context FreeRTOS 任务参数，本任务未使用
 */
void UpdateTask(void* context) {
  static_cast<void>(context);
  FirmwareReleaseManifest manifest;
  if (LockManager()) {
    manifest = g_manager.manifest;
    UnlockManager();
  }
  if (!IsNetworkReady()) {
    SetFailure("Wi-Fi is not connected");
    FinishWorker();
    vTaskDelete(nullptr);
    return;
  }
  char main_current[32] = {};
  char wireless_current[32] = {};
  if (!ReadCurrentMainVersion(main_current, sizeof(main_current)) ||
      !ReadCurrentWirelessVersion(
          wireless_current, sizeof(wireless_current))) {
    SetFailure("Installed versions unavailable");
    FinishWorker();
    vTaskDelete(nullptr);
    return;
  }
  bool main_version_valid = false;
  bool wireless_version_valid = false;
  const bool main_update_required = IsVersionUpgrade(main_current,
      manifest.main_version, &main_version_valid);
  const bool wireless_update_required = IsVersionUpgrade(wireless_current,
      manifest.wireless_version, &wireless_version_valid);
  if (!main_version_valid || !wireless_version_valid) {
    SetFailure("Installed firmware version invalid");
    FinishWorker();
    vTaskDelete(nullptr);
    return;
  }

  // 无线固件镜像先完整落盘；暂停后恢复时可以直接复用已验证的文件。
  if (wireless_update_required &&
      !InspectWirelessFirmware(kWirelessFirmwarePath, manifest, nullptr)) {
    const FirmwareDownloadResult wireless_result =
        DownloadWirelessFirmware(manifest);
    if (wireless_result != FirmwareDownloadResult::kCompleted) {
      if (wireless_result == FirmwareDownloadResult::kPaused) {
        SetDownloadPaused();
      } else if (wireless_result == FirmwareDownloadResult::kCancelled) {
        CancelPreparedFirmware(true);
      } else {
        FinishWorker();
      }
      vTaskDelete(nullptr);
      return;
    }
  }
  if (HandlePendingDownloadInterruption()) {
    vTaskDelete(nullptr);
    return;
  }
  // 主固件镜像写入备用分区并校验，但确认安装前恢复当前启动分区。
  if (main_update_required) {
    const MainUpdateResult main_result =
        UpdateMainFirmware(manifest, false, false);
    if (main_result == MainUpdateResult::kPaused) {
      SetDownloadPaused();
    } else if (main_result == MainUpdateResult::kCancelled) {
      CancelPreparedFirmware(true);
    }
    if (main_result != MainUpdateResult::kNotRequired &&
        main_result != MainUpdateResult::kPrepared) {
      if (main_result != MainUpdateResult::kPaused &&
          main_result != MainUpdateResult::kCancelled) {
        FinishWorker();
      }
      vTaskDelete(nullptr);
      return;
    }
  }
  FinishPreparedDownload();
  vTaskDelete(nullptr);
}

void InstallTask(void* context) {
  static_cast<void>(context);
  FirmwareReleaseManifest manifest;
  if (LockManager()) {
    manifest = g_manager.manifest;
    UnlockManager();
  }
  char main_current[32] = {};
  char wireless_current[32] = {};
  if (!ReadCurrentMainVersion(main_current, sizeof(main_current)) ||
      !ReadCurrentWirelessVersion(
          wireless_current, sizeof(wireless_current))) {
    SetFailure("Installed versions unavailable");
    FinishWorker();
    vTaskDelete(nullptr);
    return;
  }
  bool main_version_valid = false;
  bool wireless_version_valid = false;
  const bool main_update_required = IsVersionUpgrade(main_current,
      manifest.main_version, &main_version_valid);
  const bool wireless_update_required = IsVersionUpgrade(wireless_current,
      manifest.wireless_version, &wireless_version_valid);
  if (!main_version_valid || !wireless_version_valid) {
    SetFailure("Installed firmware version invalid");
    FinishWorker();
    vTaskDelete(nullptr);
    return;
  }
  if (wireless_update_required &&
      !InspectWirelessFirmware(kWirelessFirmwarePath, manifest, nullptr)) {
    std::remove(kWirelessFirmwarePath);
    SetFailure("Prepared Wireless firmware is unavailable");
    FinishWorker();
    vTaskDelete(nullptr);
    return;
  }
  if (main_update_required) {
    const esp_partition_t* update_partition =
        esp_ota_get_next_update_partition(nullptr);
    esp_app_desc_t staged_app = {};
    const bool image_valid = update_partition != nullptr &&
        esp_ota_get_partition_description(
            update_partition, &staged_app) == ESP_OK &&
        ValidateMainFirmwareImage(staged_app, manifest) &&
        VerifyPartitionIntegrity(update_partition,
            manifest.main_size_bytes, manifest.main_sha256);
    if (!image_valid || !SetPendingUpdate() ||
        esp_ota_set_boot_partition(update_partition) != ESP_OK) {
      ClearPendingUpdate();
      RestoreRunningBootPartition();
      SetFailure("Cannot activate prepared Main firmware");
      FinishWorker();
      vTaskDelete(nullptr);
      return;
    }
    SetStage(FirmwareUpdateStage::kRestarting,
        "Restarting into the new firmware", 100);
    vTaskDelay(pdMS_TO_TICKS(kRestartDelayMs));
    esp_restart();
    vTaskDelete(nullptr);
    return;
  }
  if (wireless_update_required) {
    const WirelessUpdateResult wireless_result =
        UpdateWirelessFirmware(manifest, false);
    if (wireless_result != WirelessUpdateResult::kNotRequired) {
      FinishWorker();
      vTaskDelete(nullptr);
      return;
    }
  }
  if (!ClearPendingUpdate()) {
    SetFailure("Cannot clear completed update state");
  } else {
    CleanupWirelessFiles();
    ApplyManifestSnapshot(
        manifest, main_current, wireless_current, true);
  }
  FinishWorker();
  vTaskDelete(nullptr);
}

/**
 * @brief 在重启后按安全顺序恢复未完成的组合更新任务
 * @param context FreeRTOS 任务参数，本任务未使用
 */
void ResumeTask(void* context) {
  static_cast<void>(context);
  FirmwareReleaseManifest manifest;
  if (!LoadSavedManifest(&manifest)) {
    ClearPendingUpdate();
    CleanupWirelessFiles();
    SetFailure("Saved update information missing");
    FinishWorker();
    vTaskDelete(nullptr);
    return;
  }
  if (std::strcmp(manifest.device_id, kCurrentDeviceId) != 0) {
    ClearPendingUpdate();
    CleanupWirelessFiles();
    SetFailure("Saved update is for another device");
    FinishWorker();
    vTaskDelete(nullptr);
    return;
  }
  ApplyPendingManifestSnapshot(manifest);
  char wireless_current[32] = {};
  if (!WaitForCurrentWirelessVersion(wireless_current,
          sizeof(wireless_current), kWirelessReadyTimeoutMs)) {
    SetFailure("Cannot verify Wireless firmware after restart");
    FinishWorker();
    vTaskDelete(nullptr);
    return;
  }
  char main_current[32] = {};
  if (!ReadCurrentMainVersion(main_current, sizeof(main_current))) {
    SetFailure("Cannot verify Main firmware after restart");
    FinishWorker();
    vTaskDelete(nullptr);
    return;
  }
  bool main_version_valid = false;
  bool wireless_version_valid = false;
  const bool main_update_required = IsVersionUpgrade(main_current,
      manifest.main_version, &main_version_valid);
  const bool wireless_update_required = IsVersionUpgrade(wireless_current,
      manifest.wireless_version, &wireless_version_valid);
  if (!main_version_valid || !wireless_version_valid) {
    SetFailure("Installed firmware version invalid");
    FinishWorker();
    vTaskDelete(nullptr);
    return;
  }
  if (LockManager()) {
    g_manager.snapshot.main_update_available = main_update_required;
    g_manager.snapshot.wireless_update_available =
        wireless_update_required;
    g_manager.snapshot.update_available =
        main_update_required || wireless_update_required;
    UnlockManager();
  }

  // 如果无线固件仍需更新，必须先确认本地镜像可用，再考虑重启进入新的主固件。
  if (wireless_update_required &&
      !InspectWirelessFirmware(kWirelessFirmwarePath, manifest, nullptr)) {
    std::remove(kWirelessFirmwarePath);
    if (!IsNetworkReady()) {
      SetFailure("Wi-Fi is not connected");
      FinishWorker();
      vTaskDelete(nullptr);
      return;
    }
    const FirmwareDownloadResult wireless_result =
        DownloadWirelessFirmware(manifest);
    if (wireless_result != FirmwareDownloadResult::kCompleted) {
      if (wireless_result == FirmwareDownloadResult::kPaused) {
        SetDownloadPaused();
      } else if (wireless_result == FirmwareDownloadResult::kCancelled) {
        CancelPreparedFirmware(true);
      } else {
        FinishWorker();
      }
      vTaskDelete(nullptr);
      return;
    }
  }
  if (HandlePendingDownloadInterruption()) {
    vTaskDelete(nullptr);
    return;
  }
  if (main_update_required) {
    if (!IsNetworkReady()) {
      SetFailure("Wi-Fi is not connected");
      FinishWorker();
      vTaskDelete(nullptr);
      return;
    }
    const MainUpdateResult main_result =
        UpdateMainFirmware(manifest, true, true);
    if (main_result == MainUpdateResult::kPaused) {
      SetDownloadPaused();
    } else if (main_result == MainUpdateResult::kCancelled) {
      CancelPreparedFirmware(true);
    }
    if (main_result != MainUpdateResult::kNotRequired) {
      if (main_result != MainUpdateResult::kPaused &&
          main_result != MainUpdateResult::kCancelled) {
        FinishWorker();
      }
      vTaskDelete(nullptr);
      return;
    }
  }
  // 新主固件只有在无线固件本地镜像可恢复时才取消 bootloader 回滚保护。
  ConfirmRunningMainFirmware();
  if (wireless_update_required) {
    const WirelessUpdateResult wireless_result =
        UpdateWirelessFirmware(manifest, true);
    if (wireless_result != WirelessUpdateResult::kNotRequired) {
      FinishWorker();
      vTaskDelete(nullptr);
      return;
    }
  }
  if (!ClearPendingUpdate()) {
    SetFailure("Cannot clear completed update state");
  } else {
    CleanupWirelessFiles();
    ApplyManifestSnapshot(
        manifest, main_current, wireless_current, true);
  }
  FinishWorker();
  vTaskDelete(nullptr);
}

/**
 * @brief 创建固件更新后台任务并处理创建失败状态
 * @param task 任务入口
 * @param name FreeRTOS 任务名称
 * @return 创建成功返回 true，否则返回 false
 */
bool CreateWorker(TaskFunction_t task, const char* name) {
  const BaseType_t result = xTaskCreate(task, name, kWorkerTaskStackBytes,
      nullptr, kWorkerTaskPriority, nullptr);
  if (result == pdPASS) {
    return true;
  }
  if (LockManager()) {
    g_manager.worker_running = false;
    UnlockManager();
  }
  SetFailure("Cannot create update task");
  return false;
}

}  // namespace

bool InitFirmwareUpdateManager(hal::WifiProvider* wifi) {
  if (g_manager.initialized) {
    if (LockManager()) {
      g_manager.wifi = wifi;
      UnlockManager();
    }
    return true;
  }
  g_manager.mutex = xSemaphoreCreateMutex();
  if (g_manager.mutex == nullptr) {
    return false;
  }
  g_manager.http_client_mutex = xSemaphoreCreateMutex();
  if (g_manager.http_client_mutex == nullptr) {
    vSemaphoreDelete(g_manager.mutex);
    g_manager.mutex = nullptr;
    return false;
  }
  g_manager.wifi = wifi;
  g_manager.initialized = true;
  g_manager.snapshot.device_supported =
      IsFirmwareUpdateDeviceSupported();
  if (!g_manager.snapshot.device_supported) {
    g_manager.snapshot.stage = FirmwareUpdateStage::kFailed;
    CopyText(g_manager.snapshot.message, sizeof(g_manager.snapshot.message),
        "Updates unavailable for this device");
  }
  CopyText(g_manager.snapshot.message, sizeof(g_manager.snapshot.message),
      g_manager.snapshot.device_supported
          ? "Check for firmware updates"
          : "Updates unavailable for this device");
  if (!ReadCurrentMainVersion(g_manager.snapshot.main_current_version,
          sizeof(g_manager.snapshot.main_current_version))) {
    CopyText(g_manager.snapshot.main_current_version,
        sizeof(g_manager.snapshot.main_current_version), "unknown");
  }
  CopyReleaseVersion(g_manager.snapshot.current_release_version,
      sizeof(g_manager.snapshot.current_release_version),
      g_manager.snapshot.main_current_version);
  if (!g_manager.snapshot.device_supported) {
    ConfirmRunningMainFirmware();
    return true;
  }
  if (!PrepareOtaStorageOnStartup()) {
    LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
        "Prepare OTA storage directories failed\n");
  }
  if (!HasPendingUpdate()) {
    ConfirmRunningMainFirmware();
    return true;
  }
  g_manager.worker_running = true;
  g_manager.snapshot.busy = true;
  return CreateWorker(ResumeTask, "ota_resume");
}

bool RequestFirmwareUpdateCheck() {
  if (!LockManager()) {
    return false;
  }
  if (!g_manager.initialized || g_manager.worker_running ||
      !g_manager.snapshot.device_supported ||
      g_manager.snapshot.stage == FirmwareUpdateStage::kPaused ||
      g_manager.snapshot.stage == FirmwareUpdateStage::kReadyToInstall) {
    UnlockManager();
    return false;
  }
  g_manager.worker_running = true;
  g_manager.snapshot.stage = FirmwareUpdateStage::kChecking;
  g_manager.snapshot.busy = true;
  g_manager.snapshot.manifest_available = false;
  g_manager.snapshot.update_available = false;
  g_manager.snapshot.main_update_available = false;
  g_manager.snapshot.wireless_update_available = false;
  g_manager.snapshot.progress_percent = 0;
  g_manager.manifest_valid = false;
  g_manager.pause_requested = false;
  g_manager.cancel_requested = false;
  g_manager.preferred_download_source =
      FirmwareDownloadSource::kPrimary;
  CopyText(g_manager.snapshot.message, sizeof(g_manager.snapshot.message),
      "Checking for updates");
  UnlockManager();
  return CreateWorker(CheckTask, "ota_check");
}

bool StartFirmwareUpdate() {
  if (!LockManager()) {
    return false;
  }
  if (!g_manager.initialized || g_manager.worker_running ||
      !g_manager.snapshot.device_supported ||
      !g_manager.manifest_valid ||
      !g_manager.snapshot.update_available ||
      (g_manager.snapshot.stage != FirmwareUpdateStage::kUpdateAvailable &&
          g_manager.snapshot.stage != FirmwareUpdateStage::kFailed)) {
    UnlockManager();
    return false;
  }
  g_manager.worker_running = true;
  g_manager.snapshot.busy = true;
  g_manager.snapshot.progress_percent = 0;
  g_manager.pause_requested = false;
  g_manager.cancel_requested = false;
  CopyText(g_manager.snapshot.message, sizeof(g_manager.snapshot.message),
      "Preparing firmware download");
  UnlockManager();
  return CreateWorker(UpdateTask, "ota_update");
}

bool PauseFirmwareUpdate() {
  if (!LockManager()) {
    return false;
  }
  const bool can_pause = g_manager.worker_running &&
      (g_manager.snapshot.stage == FirmwareUpdateStage::kDownloadingWireless ||
          g_manager.snapshot.stage == FirmwareUpdateStage::kDownloadingMain);
  if (can_pause) {
    g_manager.pause_requested = true;
    CopyText(g_manager.snapshot.message, sizeof(g_manager.snapshot.message),
        "Pausing download");
  }
  UnlockManager();
  return can_pause;
}

bool ResumeFirmwareUpdate() {
  if (!LockManager()) {
    return false;
  }
  if (!g_manager.initialized || g_manager.worker_running ||
      !g_manager.manifest_valid ||
      g_manager.snapshot.stage != FirmwareUpdateStage::kPaused) {
    UnlockManager();
    return false;
  }
  g_manager.worker_running = true;
  g_manager.snapshot.busy = true;
  g_manager.pause_requested = false;
  g_manager.cancel_requested = false;
  CopyText(g_manager.snapshot.message, sizeof(g_manager.snapshot.message),
      "Resuming firmware download");
  UnlockManager();
  return CreateWorker(UpdateTask, "ota_resume_download");
}

bool CancelFirmwareUpdate() {
  if (!LockManager()) {
    return false;
  }
  const FirmwareUpdateStage stage = g_manager.snapshot.stage;
  const bool downloading = g_manager.worker_running &&
      (stage == FirmwareUpdateStage::kDownloadingWireless ||
          stage == FirmwareUpdateStage::kDownloadingMain);
  const bool prepared = !g_manager.worker_running &&
      (stage == FirmwareUpdateStage::kPaused ||
          stage == FirmwareUpdateStage::kReadyToInstall);
  if (downloading) {
    g_manager.cancel_requested = true;
    g_manager.pause_requested = false;
    CopyText(g_manager.snapshot.message, sizeof(g_manager.snapshot.message),
        "Cancelling update");
  }
  UnlockManager();
  if (downloading) {
    CloseActiveHttpClient();
  }
  if (prepared) {
    return CancelPreparedFirmware();
  }
  return downloading;
}

bool InstallFirmwareUpdateAndRestart() {
  if (!LockManager()) {
    return false;
  }
  if (!g_manager.initialized || g_manager.worker_running ||
      !g_manager.manifest_valid ||
      g_manager.snapshot.stage != FirmwareUpdateStage::kReadyToInstall) {
    UnlockManager();
    return false;
  }
  g_manager.worker_running = true;
  g_manager.snapshot.busy = true;
  g_manager.pause_requested = false;
  g_manager.cancel_requested = false;
  CopyText(g_manager.snapshot.message, sizeof(g_manager.snapshot.message),
      "Preparing installation");
  UnlockManager();
  return CreateWorker(InstallTask, "ota_install");
}

FirmwareUpdateSnapshot GetFirmwareUpdateSnapshot() {
  FirmwareUpdateSnapshot snapshot;
  if (!LockManager()) {
    CopyText(snapshot.message, sizeof(snapshot.message),
        "Firmware update service is unavailable");
    snapshot.stage = FirmwareUpdateStage::kFailed;
    return snapshot;
  }
  snapshot = g_manager.snapshot;
  UnlockManager();
  return snapshot;
}

}  // namespace lilygo_box::app
