/*
 * @Description: 以太网连接状态与控制接口
 * @Author: LILYGO_L
 * @Date: 2026-05-14 00:20:00
 * @LastEditTime: 2026-05-14 00:20:00
 * @License: GPL 3.0
 */
#pragma once

#include <cstdint>

namespace lilygo_box::hal {

// 以太网驱动、链路和 DHCP 状态快照。
struct EthernetStatus {
  // 异步初始化任务是否正在运行。
  bool init_task_running = false;
  // ESP-IDF 以太网驱动是否已经初始化。
  bool driver_initialized = false;
  // 以太网驱动是否已经启动。
  bool running = false;
  // 网线链路是否已经建立。
  bool link_up = false;
  // 是否已经通过 DHCP 获得地址。
  bool got_ip = false;
  // 最近一次启动或连接流程是否失败。
  bool start_failed = false;
  // 当前可用的以太网端口数量。
  int port_count = 0;
  // 最近一次底层错误码。
  int last_error = 0;
  // 打包后的 MAC 地址。
  uint64_t mac_address = 0;
  // DHCP 分配的 IP 地址。
  uint32_t ip_address = 0;
  // DHCP 分配的子网掩码。
  uint32_t netmask = 0;
  // DHCP 分配的网关。
  uint32_t gateway = 0;
};

class EthernetProvider {
 public:
  virtual ~EthernetProvider() = default;

  /**
   * @brief 启用或停止以太网驱动
   * @param enabled true 异步启用，false 立即请求停止
   * @return 状态切换请求成功返回 true，否则返回 false
   */
  virtual bool SetEthernetEnabled(bool enabled) = 0;

  /**
   * @brief 读取以太网链路和 DHCP 状态
   * @param status 以太网状态输出地址
   * @return 读取成功返回 true，否则返回 false
   */
  virtual bool ReadEthernetStatus(EthernetStatus* status) = 0;
};

}  // namespace lilygo_box::hal
