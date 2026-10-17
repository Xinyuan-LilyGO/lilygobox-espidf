/*
 * @Description: LilygoBox 组合固件 OTA 更新管理接口
 * @Author: LILYGO_L
 * @Date: 2026-07-20 00:00:00
 * @LastEditTime: 2026-09-02 17:51:05
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

/**
 * @brief 当前设备用于固件目标匹配的芯片信息，无线修订号来自板级配置
 */
struct FirmwareHardwareInfo {
  char main_model[16] = {};
  char main_revision[24] = {};
  char wireless_model[16] = {};
  char wireless_revision[24] = {};
};

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

/**
 * @brief 局域网固件传输结束后正在执行的处理操作
 */
enum class FirmwareUpdateOperation {
  kNone,
  kVerifyingWireless,
  kVerifyingMain,
  kPreparingInstall,
};

struct FirmwareUpdateSnapshot {
  FirmwareUpdateStage stage = FirmwareUpdateStage::kIdle;
  FirmwareUpdateOperation operation = FirmwareUpdateOperation::kNone;
  int progress_percent = 0;
  size_t received_bytes = 0;
  size_t total_bytes = 0;
  bool local_update = false;
  bool cancelled = false;
  bool device_supported = false;
  bool busy = false;
  bool manifest_available = false;
  bool current_release_notes_available = false;
  bool update_available = false;
  bool manual_update_required = false;
  bool main_update_available = false;
  bool wireless_update_available = false;
  char release_version[32] = {};
  char release_channel[16] = {};
  char publish_time[32] = {};
  char current_release_version[32] = {};
  char current_release_channel[16] = {};
  char current_publish_time[32] = {};
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
   * @brief 读取主芯片实际修订号及无线芯片板级兼容信息
   * @return 与设备更新管理器使用相同来源的芯片匹配信息
   */
  static FirmwareHardwareInfo ReadHardwareInfo();

  /**
   * @brief 获取当前设备使用的固件清单地址
   * @return 固件清单 URL，不支持当前设备时返回空字符串
   */
  static const char* ManifestUrl();

  /**
   * @brief 读取无线协处理器当前固件版本
   * @param version 输出版本字符串
   * @param version_size 输出缓冲区大小
   * @return 读取成功返回 true
   */
  static bool ReadWirelessVersion(char* version, size_t version_size);

  /**
   * @brief 初始化固件更新管理器并恢复未完成的更新
   * @param wifi 当前设备已经拥有的 WLAN 服务
   * @param application 唯一的应用实例，用于熄屏后重启
   * @return 初始化成功返回 true，否则返回 false
   */
  bool Initialize(
      hal::WifiProvider* wifi, ::lilygo_box::Application& application);

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
   * @brief 使用应用提供的局域网固件地址准备主固件更新
   * @param url 应用临时 HTTP 服务中的固件地址
   * @param version 目标主固件版本
   * @param size 固件字节数
   * @param sha256 固件 SHA-256 十六进制字符串
   * @return 更新任务启动成功返回 true
   */
  bool StartLocalMainUpdate(const char* url, const char* version,
                            size_t size, const char* sha256);

  /**
   * @brief 使用应用提供的主固件和无线固件地址启动组合更新
   * @param main_url 主固件局域网下载地址，无需更新时可为空字符串
   * @param wireless_url 无线固件局域网下载地址，无需更新时可为空字符串
   * @param main_version 主固件目标版本
   * @param wireless_version 无线固件目标版本
   * @param main_size 主固件字节数
   * @param wireless_size 无线固件字节数
   * @param main_sha256 主固件 SHA-256
   * @param wireless_sha256 无线固件 SHA-256
   * @return 更新任务启动成功返回 true
   */
  bool StartLocalUpdate(const char* main_url, const char* wireless_url,
                        const char* main_version, const char* wireless_version,
                        size_t main_size, size_t wireless_size,
                        const char* main_sha256, const char* wireless_sha256);

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
   * @brief 仅取消应用发起且尚未开始安装的局域网更新
   * @return 取消请求被接受返回 true，互联网更新或安装中返回 false
   */
  bool CancelLocalUpdate();

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

  /**
   * @brief 判断本地更新是否正在传输、等待安装或重启
   * @return 本地更新仍在进行时返回 true
   */
  bool IsLocalUpdateActive() const;

 private:
  /**
   * @brief 在同一状态锁内检查更新来源并提交取消请求
   * @param local_only 是否仅允许取消局域网更新
   * @return 取消请求被接受返回 true
   */
  bool CancelUpdate(bool local_only);

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
