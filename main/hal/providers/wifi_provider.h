/*
 * @Description: None
 * @Author: LILYGO_L
 * @Date: 2026-05-15 13:20:00
 * @LastEditTime: 2026-05-15 13:20:00
 * @License: GPL 3.0
 */
#pragma once

#include <cstdint>

namespace lilygo_box::hal {

struct WifiStatus {
  // WiFi 初始化任务是否正在运行
  bool initializing = false;
  // WiFi 驱动是否已经初始化完成
  bool initialized = false;
  // WiFi 驱动是否已经启动
  bool running = false;
  // STA 是否已经关联热点
  bool connected = false;
  // 是否已经通过 DHCP 获取地址
  bool got_ip = false;
  // 启动或连接是否失败
  bool start_failed = false;
  // 获取时间测试是否正在运行
  bool time_test_running = false;
  // SNTP 时间同步是否已经启动
  bool time_sync_started = false;
  // SNTP 是否已经同步到有效时间
  bool time_synced = false;
  // 当前重试次数
  int retry_count = 0;
  // 最近一次错误码
  int last_error = 0;
  // 最近一次断开原因
  int disconnect_reason = 0;
  // 当前热点信号强度
  int rssi = 0;
  // 当前连接信道
  int channel = 0;
  // STA MAC 地址打包值
  uint64_t mac_address = 0;
  // DHCP IP 地址
  uint32_t ip_address = 0;
  // DHCP 子网掩码
  uint32_t netmask = 0;
  // DHCP 网关
  uint32_t gateway = 0;
  // SNTP 获取到的 UTC Unix 时间戳
  int64_t unix_time = 0;
  // SNTP 最新一次网络同步距当前的秒数
  uint32_t time_sync_age_s = 0;
  // 当前连接或测试使用的 SSID
  char ssid[33] = {};
};

class WifiProvider {
 public:
  virtual ~WifiProvider() = default;

  /**
   * @brief 异步初始化 hosted WiFi 驱动，初始化完成后保持关闭状态
   * @return 启动命令发送成功返回 true，否则返回 false
   * @Date 2026-05-15 13:20:00
   */
  virtual bool StartWifi() = 0;

  /**
   * @brief 临时打开 WiFi 并连接工厂测试热点获取网络时间
   * @return 启动命令发送成功返回 true，否则返回 false
   * @Date 2026-05-15 13:20:00
   */
  virtual bool StartWifiTimeTest() = 0;

  /**
   * @brief 停止 WiFi 获取时间测试并恢复进入测试前的 WiFi 状态
   * @return 恢复命令发送成功返回 true，否则返回 false
   * @Date 2026-05-15 13:20:00
   */
  virtual bool StopWifiTimeTest() = 0;

  /**
   * @brief 读取 hosted WiFi 连接、DHCP 和时间同步状态
   * @param status WiFi 状态输出地址
   * @return 读取成功返回 true，否则返回 false
   * @Date 2026-05-15 13:20:00
   */
  virtual bool ReadWifiStatus(WifiStatus* status) = 0;
};

}  // namespace lilygo_box::hal
