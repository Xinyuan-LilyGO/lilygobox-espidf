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
  char release_version[32] = {};
  char package_size[24] = {};
  char main_current_version[32] = {};
  char main_target_version[32] = {};
  char wireless_current_version[32] = {};
  char wireless_target_version[32] = {};
  char notes[kFirmwareUpdateNoteCapacity][128] = {};
  size_t note_count = 0;
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
 * @brief 异步开始先准备 C6、再更新 P4、最后切换 C6 的组合固件更新
 * @return 更新任务启动成功返回 true，否则返回 false
 */
bool StartFirmwareUpdate();

/**
 * @brief 读取可供界面显示的固件更新状态快照
 * @return 固件更新状态快照
 */
FirmwareUpdateSnapshot GetFirmwareUpdateSnapshot();

}  // namespace lilygo_box::app
