/*
 * @Description: Storage provider
 * @Author: LILYGO_L
 * @Date: 2026-07-09 00:00:00
 * @LastEditTime: 2026-09-02 17:52:40
 * @License: GPL 3.0
 */
#pragma once

#include <cstddef>
#include <cstdint>

namespace lilygo_box::hal {

inline constexpr size_t kMaxUsbStorageDeviceCount = 9;
inline constexpr size_t kUsbStorageNameSize = 32;
inline constexpr size_t kUsbStorageBasePathSize = 16;

struct UsbStorageDeviceInfo {
  uint32_t id = 0;
  uint8_t usb_address = 0;
  char name[kUsbStorageNameSize] = {};
  char base_path[kUsbStorageBasePathSize] = {};
};

struct UsbStorageSnapshot {
  uint32_t generation = 0;
  size_t device_count = 0;
  bool monitor_running = false;
  bool start_failed = false;
  UsbStorageDeviceInfo devices[kMaxUsbStorageDeviceCount] = {};
};

class StorageProvider {
 public:
  virtual ~StorageProvider() = default;

  /**
   * @brief 确保 SD 卡已经挂载到文件系统
   * @return 挂载成功或已经挂载返回 true，否则返回 false
   */
  virtual bool EnsureSdCardMounted() = 0;

  /**
   * @brief 卸载 SD 卡文件系统并释放总线资源
   * @return 卸载成功或当前未挂载返回 true，否则返回 false
   */
  virtual bool UnmountSdCard() = 0;

  /**
   * @brief 判断 SD 卡文件系统是否已经挂载
   * @return 已挂载返回 true，否则返回 false
   */
  virtual bool IsSdCardMounted() const = 0;

  /**
   * @brief 获取 SD 卡挂载路径
   * @return SD 卡挂载路径字符串
   */
  virtual const char* SdCardBasePath() const = 0;

  /**
   * @brief 启动 USB Host MSC 监控，自动挂载后续接入的 U 盘
   * @return 监控已经运行或启动任务创建成功返回 true，否则返回 false
   */
  virtual bool StartUsbStorage() = 0;

  /**
   * @brief 停止 USB Host MSC 监控并卸载全部 U 盘
   * @return USB 资源全部释放返回 true，否则返回 false
   */
  virtual bool StopUsbStorage() = 0;

  /**
   * @brief 读取当前已经挂载的 USB 存储设备快照
   * @param snapshot 快照输出地址
   * @return 读取成功返回 true，否则返回 false
   */
  virtual bool ReadUsbStorageSnapshot(UsbStorageSnapshot* snapshot) const = 0;
};

}  // namespace lilygo_box::hal
