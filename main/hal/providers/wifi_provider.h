/*
 * @Description: WiFi 扫描、连接配置与状态管理接口
 * @Author: LILYGO_L
 * @Date: 2026-05-15 13:20:00
 * @LastEditTime: 2026-05-15 13:20:00
 * @License: GPL 3.0
 */
#pragma once

#include <cstddef>
#include <cstdint>

namespace lilygo_box::hal {

inline constexpr size_t kWifiSsidMaxLength = 32;
inline constexpr size_t kWifiPasswordMaxLength = 64;
inline constexpr size_t kMaxWifiScanNetworkCount = 16;

struct WifiNetworkInfo {
  // 热点 SSID，末尾保留字符串终止符。
  char ssid[kWifiSsidMaxLength + 1] = {};
  // 热点信号强度，单位为 dBm。
  int rssi = 0;
  // 热点所在信道。
  int channel = 0;
  // 热点是否需要密码。
  bool secure = false;
  // 热点是否位于 5 GHz 频段。
  bool is_5g = false;
};

struct WifiScanStatus {
  // WiFi 扫描是否正在进行。
  bool scan_running = false;
  // 最近一次扫描是否失败。
  bool scan_failed = false;
  // 最近一次 WiFi 扫描或连接错误码。
  int last_error = 0;
  // 扫描结果版本号，扫描完成后递增。
  uint32_t generation = 0;
  // 当前有效扫描结果数量。
  size_t network_count = 0;
  // 最近一次扫描得到的热点列表。
  WifiNetworkInfo networks[kMaxWifiScanNetworkCount] = {};
};

struct WifiStatus {
  // WiFi 初始化任务是否正在运行
  bool init_task_running = false;
  // WiFi 驱动是否已经初始化完成
  bool driver_initialized = false;
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
   * @brief 启用或停止 hosted WiFi STA
   * @param enabled true 异步启用，false 立即请求停止
   * @return 状态切换请求成功返回 true，否则返回 false
   */
  virtual bool SetWifiEnabled(bool enabled) = 0;

  /**
   * @brief 启动 hosted WiFi STA 并异步扫描附近热点
   * @return 扫描命令发送成功返回 true，否则返回 false
   */
  virtual bool StartWifiScan() = 0;

  /**
   * @brief 读取最近一次 hosted WiFi 扫描状态与热点列表
   * @param status WiFi 扫描状态输出地址
   * @return 读取成功返回 true，否则返回 false
   */
  virtual bool ReadWifiScanStatus(WifiScanStatus* status) = 0;

  /**
   * @brief 连接指定 hosted WiFi 热点
   * @param ssid 热点 SSID
   * @param password 热点密码，开放网络可传空字符串或 nullptr
   * @return 连接命令发送成功返回 true，否则返回 false
   */
  virtual bool ConnectWifi(const char* ssid, const char* password) = 0;

  /**
   * @brief 取消当前 WiFi 连接等待状态并保持 WiFi 驱动开启
   * @return 取消成功或当前无需取消返回 true，否则返回 false
   */
  virtual bool CancelWifiConnection() = 0;

  /**
   * @brief 临时打开 WiFi 并连接工厂测试热点获取网络时间
   * @return 启动命令发送成功返回 true，否则返回 false
   */
  virtual bool StartWifiTimeTest() = 0;

  /**
   * @brief 停止 WiFi 获取时间测试并恢复进入测试前的 WiFi 状态
   * @return 恢复命令发送成功返回 true，否则返回 false
   */
  virtual bool StopWifiTimeTest() = 0;

  /**
   * @brief 读取 hosted WiFi 连接、DHCP 和时间同步状态
   * @param status WiFi 状态输出地址
   * @return 读取成功返回 true，否则返回 false
   */
  virtual bool ReadWifiStatus(WifiStatus* status) = 0;
};

}  // namespace lilygo_box::hal
