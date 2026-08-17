/*
 * @Description: 蜂窝调制解调器控制、状态与 AT 指令 Provider 接口
 * @Author: LILYGO_L
 * @Date: 2026-07-30 00:00:00
 * @LastEditTime: 2026-07-30 18:00:00
 * @License: GPL 3.0
 */
#pragma once

#include <cstddef>
#include <cstdint>

namespace lilygo_box::hal {

inline constexpr size_t kCellularModelCapacity = 32;
inline constexpr size_t kCellularImeiCapacity = 24;
inline constexpr size_t kCellularFirmwareCapacity = 96;
inline constexpr size_t kCellularOperatorCapacity = 40;
inline constexpr size_t kCellularNetworkTimeCapacity = 40;

// 由 3GPP CPIN 状态归一化后的 SIM 卡状态。
enum class CellularSimState : uint8_t {
  kUnknown,
  kReady,
  kPinRequired,
  kPukRequired,
  kBlocked,
  kFailure,
  kUnavailable,
};

// 由 3GPP CEREG 状态归一化后的网络注册状态。
enum class CellularRegistrationState : uint8_t {
  kUnknown,
  kNotRegistered,
  kRegisteredHome,
  kSearching,
  kDenied,
  kRegisteredRoaming,
};

// nRF9151 蜂窝连接与模块信息快照。
struct CellularStatus {
  // nRF9151 驱动和串口是否可用。
  bool hardware_ready = false;
  // 蜂窝后台管理任务是否已经启动。
  bool enabled = false;
  // 模块电源和 UART 是否已经打开。
  bool powered = false;
  // 当前 SIM 卡访问状态。
  CellularSimState sim_state = CellularSimState::kUnknown;
  // 当前 EPS 网络注册状态。
  CellularRegistrationState registration = CellularRegistrationState::kUnknown;
  // AT+CSQ 原始信号质量，99 表示未知。
  int signal_quality = 99;
  // 由 CSQ 换算的接收信号强度，未知时为 0。
  int rssi_dbm = 0;
  // 模块型号。
  char model[kCellularModelCapacity] = {};
  // 模块 IMEI。
  char imei[kCellularImeiCapacity] = {};
  // modem firmware 版本。
  char firmware[kCellularFirmwareCapacity] = {};
  // 当前运营商名称或数值编码。
  char operator_name[kCellularOperatorCapacity] = {};
  // 网络提供并由 modem 时钟维护的当前时间。
  char network_time[kCellularNetworkTimeCapacity] = {};
  // network_time 是否已经成功读取。
  bool network_time_ready = false;
  // 最近一次 AT 指令或底层错误码，0 表示无错误。
  int last_error = 0;
};

class CellularProvider {
 public:
  virtual ~CellularProvider() = default;

  /**
   * @brief 异步启动或停止 nRF9151 蜂窝网络管理
   * @param enabled true 启动蜂窝模式，false 停止并关闭模块电源
   * @return 请求成功接受或目标状态已经满足返回 true
   */
  virtual bool SetCellularEnabled(bool enabled) = 0;

  /**
   * @brief 非阻塞读取蜂窝网络和模块状态快照
   * @param status 蜂窝状态输出地址
   * @return 状态读取成功返回 true，否则返回 false
   */
  virtual bool ReadCellularStatus(CellularStatus* status) = 0;

  /**
   * @brief 向已经启用的 nRF9151 发送一条 AT 指令
   * @param command 不包含换行符的 AT 指令
   * @param response AT 完整响应输出缓冲区
   * @param response_size 输出缓冲区容量
   * @param timeout_ms 等待最终响应的超时时间
   * @return 收到 OK 最终响应返回 true，否则返回 false
   */
  virtual bool SendCellularCommand(const char* command, char* response,
      size_t response_size, uint32_t timeout_ms) = 0;
};

}  // namespace lilygo_box::hal
