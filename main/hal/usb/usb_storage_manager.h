/*
 * @Description: USB Host MSC 存储管理器
 * @Author: LILYGO_L
 * @License: GPL 3.0
 */
#pragma once

#include <functional>
#include <memory>

#include "hal/providers/storage_provider.h"

namespace lilygo_box::hal {

struct UsbStorageManagerState;

class UsbStorageManager {
 public:
  // USB Host 完全停止后通知设备层释放板级供电。
  using HostStoppedCallback = std::function<void()>;

  /**
   * @brief 创建 USB Host MSC 存储管理器
   * @param host_stopped_callback USB Host 完全停止后的通知回调
   */
  explicit UsbStorageManager(
      HostStoppedCallback host_stopped_callback = {});
  ~UsbStorageManager();

  UsbStorageManager(const UsbStorageManager&) = delete;
  UsbStorageManager& operator=(const UsbStorageManager&) = delete;

  /**
   * @brief 异步启动 USB Host MSC 监控
   * @return 监控已经运行或启动任务创建成功返回 true，否则返回 false
   */
  bool Start();

  /**
   * @brief 停止 USB Host MSC 监控并卸载全部 U 盘
   * @return USB Host 在超时前完全停止返回 true，否则返回 false
   */
  bool Stop();

  /**
   * @brief 读取当前已经挂载的 USB 存储设备快照
   * @param snapshot 快照输出地址
   * @return 读取成功返回 true，否则返回 false
   */
  bool ReadSnapshot(UsbStorageSnapshot* snapshot) const;

 private:
  std::unique_ptr<UsbStorageManagerState> state_;
};

}  // namespace lilygo_box::hal
