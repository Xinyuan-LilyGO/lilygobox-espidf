/*
 * @Description: LilygoBox 组合固件 OTA 更新管理接口
 * @Author: LILYGO_L
 * @Date: 2026-07-20 00:00:00
 * @LastEditTime: 2026-07-20 00:00:00
 * @License: GPL 3.0
 */
#pragma once

#include <cstddef>
#include <memory>

namespace lilygo_box {
class Application;
}  // namespace lilygo_box

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
  char current_package_size[24] = {};
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

// 管理固件检查、下载、暂存、安装和恢复流程。
class FirmwareUpdateManager final {
 public:
  /**
   * @brief 获取应用内部唯一的固件更新管理器
   * @return 固件更新管理器
   */
  static FirmwareUpdateManager& Instance();

  /**
   * @brief 初始化固件更新管理器并恢复未完成的更新
   * @param wifi 当前设备已经拥有的 WLAN 服务
   * @param application 唯一的应用实例，用于熄屏后重启
   * @return 初始化成功返回 true，否则返回 false
   */
  bool Initialize(hal::WifiProvider* wifi,
      ::lilygo_box::Application& application);

  /**
   * @brief 异步检查最新固件清单
   * @return 检查任务启动成功返回 true，否则返回 false
   */
  bool RequestCheck();

  /**
   * @brief 异步下载并暂存可用的新固件
   * @return 下载任务启动成功返回 true，否则返回 false
   */
  bool StartUpdate();

  /**
   * @brief 暂停当前固件下载任务
   * @return 暂停请求被接受返回 true，否则返回 false
   */
  bool Pause();

  /**
   * @brief 继续已暂停的固件下载任务
   * @return 下载任务重新启动返回 true，否则返回 false
   */
  bool Resume();

  /**
   * @brief 取消下载中或已准备的更新并保留当前固件
   * @return 取消请求被接受返回 true，否则返回 false
   */
  bool Cancel();

  /**
   * @brief 安装已经准备好的固件并重启设备
   * @return 安装任务启动成功返回 true，否则返回 false
   */
  bool InstallAndRestart();

  /**
   * @brief 读取可供界面显示的固件更新状态快照
   * @return 固件更新状态快照
   */
  FirmwareUpdateSnapshot GetSnapshot() const;

 private:
  /**
   * @brief 创建尚未初始化的应用内部固件更新管理器
   */
  FirmwareUpdateManager();

  /**
   * @brief 释放固件更新管理器持有的内部状态
   */
  ~FirmwareUpdateManager();

  FirmwareUpdateManager(const FirmwareUpdateManager&) = delete;
  FirmwareUpdateManager& operator=(const FirmwareUpdateManager&) = delete;

  class Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace lilygo_box::app
