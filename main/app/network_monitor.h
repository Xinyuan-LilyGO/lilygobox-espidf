/*
 * @Description: 应用层网络可用性监控接口
 * @Author: LILYGO_L
 * @License: GPL 3.0
 */
#pragma once

#include <atomic>
#include <cstdint>

namespace lilygo_box::hal {
class WifiProvider;
}  // namespace lilygo_box::hal

namespace lilygo_box::app {

enum class InternetAccessState : uint8_t {
  // 尚未进行互联网可用性检测
  kUnknown,
  // 正在验证固件服务是否可达
  kChecking,
  // 已取得局域网地址，但固件服务不可达
  kLocalOnly,
  // 已确认固件服务可以访问
  kAvailable,
};

struct NetworkMonitorStatus {
  // 当前互联网可用性验证状态
  InternetAccessState internet_state = InternetAccessState::kUnknown;
  // 最近一次互联网验证结束距当前的秒数
  uint32_t check_age_s = 0;
};

class NetworkMonitor final {
 public:
  /**
   * @brief 获取应用内部唯一的网络监控器
   * @return 网络监控器
   */
  static NetworkMonitor& Instance();

  /**
   * @brief 启动产品网络服务可用性后台监控
   * @param wifi WiFi 状态提供者
   * @return 监控已启动或已经运行返回 true，否则返回 false
   */
  bool Initialize(hal::WifiProvider* wifi);

  /**
   * @brief 读取最近一次互联网可用性验证状态
   * @return 当前网络监控状态
   */
  NetworkMonitorStatus GetStatus() const;

 private:
  NetworkMonitor() = default;

  /**
   * @brief 网络监控任务入口
   * @param argument 网络监控器指针
   */
  static void TaskEntry(void* argument);

  /**
   * @brief 持续检查 WiFi 和产品网络服务可用性
   */
  void RunTask();

  /**
   * @brief 验证固件服务是否可以通过当前网络访问
   * @return 服务可访问返回 true，否则返回 false
   */
  bool CheckInternetAccess() const;

  hal::WifiProvider* wifi_ = nullptr;
  std::atomic<bool> initialized_{false};
  std::atomic<InternetAccessState> internet_state_{
      InternetAccessState::kUnknown};
  std::atomic<int64_t> check_monotonic_ms_{0};
};

}  // namespace lilygo_box::app
