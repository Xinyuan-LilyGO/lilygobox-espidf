/*
 * @Description: None
 * @Author: LILYGO_L
 * @Date: 2026-05-14 00:20:00
 * @LastEditTime: 2026-05-14 00:20:00
 * @License: GPL 3.0
 */
#pragma once

#include <cstdint>

namespace lilygo_box::hal {

struct EthernetStatus {
  bool init_task_running = false;
  bool driver_initialized = false;
  bool running = false;
  bool link_up = false;
  bool got_ip = false;
  bool start_failed = false;
  int port_count = 0;
  int last_error = 0;
  uint64_t mac_address = 0;
  uint32_t ip_address = 0;
  uint32_t netmask = 0;
  uint32_t gateway = 0;
};

class EthernetProvider {
 public:
  virtual ~EthernetProvider() = default;

  /**
   * @brief 异步启动以太网检测
   * @return 启动命令发送成功返回 true，否则返回 false
   * @Date 2026-05-14 00:20:00
   */
  virtual bool StartEthernet() = 0;

  /**
   * @brief 读取以太网链路和 DHCP 状态
   * @param status 以太网状态输出地址
   * @return 读取成功返回 true，否则返回 false
   * @Date 2026-05-14 00:20:00
   */
  virtual bool ReadEthernetStatus(EthernetStatus* status) = 0;
};

}  // namespace lilygo_box::hal
