/*
 * @Description: LilygoBox 组合固件 OTA 更新管理接口
 * @Author: LILYGO_L
 * @Date: 2026-07-20 00:00:00
 * @LastEditTime: 2026-07-20 00:00:00
 * @License: GPL 3.0
 */
#pragma once

#include <cstddef>

namespace lilygo_box::hal {
class WifiProvider;
}  // namespace lilygo_box::hal

namespace lilygo_box::app {

inline constexpr size_t kFirmwareUpdateNoteCapacity = 3;

enum class FirmwareUpdateStage {
  kIdle,
  kWaitingForNetwork,
  kChecking,
  kUpdateAvailable,
  kUpToDate,
  kDownloadingWireless,
  kInstallingWireless,
  kDownloadingMain,
  kPaused,
  kReadyToInstall,
  kRestarting,
  kFailed,
};

struct FirmwareUpdateSnapshot {
  FirmwareUpdateStage stage = FirmwareUpdateStage::kIdle;
  int progress_percent = 0;
  bool device_supported = false;
  bool busy = false;
  bool manifest_available = false;
  bool update_available = false;
  bool manual_update_required = false;
  bool main_update_available = false;
  bool wireless_update_available = false;
  char release_version[32] = {};
  char release_channel[16] = {};
  char release_time[32] = {};
  char current_release_version[32] = {};
  char current_release_channel[16] = {};
  char current_release_time[32] = {};
  char package_size[24] = {};
  char main_size[24] = {};
  char wireless_size[24] = {};
  char current_main_size[24] = {};
  char current_wireless_size[24] = {};
  char main_current_version[32] = {};
  char main_target_version[32] = {};
  char wireless_current_version[32] = {};
  char wireless_target_version[32] = {};
  char notes[kFirmwareUpdateNoteCapacity][128] = {};
  size_t note_count = 0;
  char current_notes[kFirmwareUpdateNoteCapacity][128] = {};
  size_t current_note_count = 0;
  char message[128] = {};
};

/**
 * @brief 初始化组合固件更新管理器并恢复未完成的更新
 * @param wifi 当前设备已经拥有的 WLAN 服务
 * @return 初始化成功返回 true，否则返回 false
 */
bool InitFirmwareUpdateManager(hal::WifiProvider* wifi);

/**
 * @brief 异步检查 GitHub Releases 中的最新固件清单
 * @return 检查任务启动成功返回 true，否则返回 false
 */
bool RequestFirmwareUpdateCheck();

/**
 * @brief 异步开始先准备无线固件、再更新主固件、最后切换无线固件的组合更新
 * @return 更新任务启动成功返回 true，否则返回 false
 */
bool StartFirmwareUpdate();

/**
 * @brief 暂停当前固件下载任务
 * @return 暂停请求被接受返回 true，否则返回 false
 */
bool PauseFirmwareUpdate();

/**
 * @brief 从当前固件组件继续已暂停的下载任务
 * @return 下载任务重新启动返回 true，否则返回 false
 */
bool ResumeFirmwareUpdate();

/**
 * @brief 取消下载中或已准备的更新并保留当前固件
 * @return 取消请求被接受返回 true，否则返回 false
 */
bool CancelFirmwareUpdate();

/**
 * @brief 安装已经准备好的固件并重启设备
 * @return 安装任务启动成功返回 true，否则返回 false
 */
bool InstallFirmwareUpdateAndRestart();

/**
 * @brief 读取可供界面显示的固件更新状态快照
 * @return 固件更新状态快照
 */
FirmwareUpdateSnapshot GetFirmwareUpdateSnapshot();

}  // namespace lilygo_box::app
