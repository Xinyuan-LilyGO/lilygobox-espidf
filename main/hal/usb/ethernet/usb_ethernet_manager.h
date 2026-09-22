/*
 * @Description: T-Display-P4 V2 USB 以太网异步管理接口
 * @Author: LILYGO_L
 * @License: GPL 3.0
 */
#pragma once

#include <functional>
#include <memory>

#include "hal/providers/ethernet_provider.h"

namespace lilygo_box::hal {
struct UsbEthernetState;

// 板载 RTL8152B 的异步会话；不控制 Type-A 电源开关。
class UsbEthernetManager final {
 public:
  using PowerControl = std::function<bool(bool)>;

  /**
   * @brief 创建板载 USB 以太网管理器
   * @param power_control 板级 Boost 供电申请与释放回调
   */
  explicit UsbEthernetManager(PowerControl power_control);

  /**
   * @brief 停止以太网并等待后台清理任务结束
   */
  ~UsbEthernetManager();
  UsbEthernetManager(const UsbEthernetManager&) = delete;
  UsbEthernetManager& operator=(const UsbEthernetManager&) = delete;

  /**
   * @brief 异步启用或停止板载 USB 以太网
   * @param enabled true 启用网卡，false 请求停止并释放会话
   * @return 请求已接受返回 true，任务创建失败返回 false
   */
  bool SetEnabled(bool enabled);

  /**
   * @brief 读取驱动、链路和 DHCP 状态快照
   * @param status 以太网状态输出地址
   * @return 快照读取成功返回 true，否则返回 false
   */
  bool ReadStatus(EthernetStatus* status) const;

  /**
   * @brief 检查网卡是否仍在初始化、运行或清理
   * @return 后台任务仍持有硬件资源返回 true，否则返回 false
   */
  bool IsActive() const;

 private:
  std::unique_ptr<UsbEthernetState> state_;
};
}  // namespace lilygo_box::hal
