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
  // 已取得局域网地址，正在等待首次或按需 SNTP 复检结果
  kChecking,
  // 已取得局域网地址，但 SNTP 未能在限定时间内返回网络时间
  kLocalOnly,
  // SNTP 已经成功获取有效网络时间
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
   * @brief 启动首次 SNTP 与业务触发复检相结合的网络监控
   * @param wifi WiFi 状态提供者
   * @return 监控已启动或已经运行返回 true，否则返回 false
   */
  bool Initialize(hal::WifiProvider* wifi);

  /**
   * @brief 读取最近一次互联网可用性验证状态
   * @return 当前网络监控状态
   */
  NetworkMonitorStatus GetStatus() const;

  /**
   * @brief 在互联网业务开始前等待当前连接完成一次按需入网验证
   * @param timeout_ms 最长等待时间，单位为毫秒
   * @return 已确认可以入网返回 true，否则返回 false
   *
   * 该接口会阻塞调用任务，不允许从 LVGL 或网络监控任务中调用。
   */
  bool EnsureInternetAccess(uint32_t timeout_ms);

  /**
   * @brief 请求使用 SNTP 对当前 WiFi 执行一次异步入网复检
   */
  void RequestInternetAccessRecheck();

 private:
  NetworkMonitor() = default;

  /**
   * @brief 网络监控任务入口
   * @param argument 网络监控器指针
   */
  static void TaskEntry(void* argument);

  /**
   * @brief 处理 WiFi 状态和按需 SNTP 复检
   */
  void RunTask();

  hal::WifiProvider* wifi_ = nullptr;
  std::atomic<bool> initialized_{false};
  std::atomic<InternetAccessState> internet_state_{
      InternetAccessState::kUnknown};
  std::atomic<int64_t> check_monotonic_ms_{0};
  std::atomic<uint32_t> check_generation_{0};
  std::atomic<bool> recheck_requested_{false};
};

}  // namespace lilygo_box::app
