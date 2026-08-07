/*
 * @Description: 红外 NEC 协议收发与状态 Provider 接口
 * @Author: LILYGO_L
 * @Date: 2026-07-30 00:00:00
 * @LastEditTime: 2026-07-30 18:00:00
 * @License: GPL 3.0
 */
#pragma once

#include <cstdint>

namespace lilygo_box::hal {

// 红外接收器最近一次 NEC 帧及运行状态。
struct InfraredStatus {
  // RMT 收发通道是否已经完成初始化。
  bool hardware_ready = false;
  // 红外接收通道是否正在连续监听。
  bool receiver_enabled = false;
  // 是否已经成功解码至少一帧 NEC 数据。
  bool frame_received = false;
  // 最近一帧是否为 NEC 重复码。
  bool repeat = false;
  // 最近一帧 NEC 地址。
  uint8_t address = 0;
  // 最近一帧 NEC 命令。
  uint8_t command = 0;
  // 成功接收的 NEC 帧数量。
  uint32_t receive_count = 0;
  // 无法通过 NEC 校验的帧数量。
  uint32_t decode_error_count = 0;
  // 最近一次 ESP-IDF 错误码，0 表示无错误。
  int last_error = 0;
};

class InfraredProvider {
 public:
  virtual ~InfraredProvider() = default;

  /**
   * @brief 启动或停止红外接收通道
   * @param enabled true 连续接收，false 停止接收
   * @return 状态切换成功或目标状态已经满足返回 true
   */
  virtual bool SetInfraredReceiverEnabled(bool enabled) = 0;

  /**
   * @brief 使用 38 kHz 载波发送一帧标准 NEC 指令
   * @param address NEC 八位地址
   * @param command NEC 八位命令
   * @return 完整帧发送成功返回 true，否则返回 false
   */
  virtual bool SendInfraredNec(uint8_t address, uint8_t command) = 0;

  /**
   * @brief 非阻塞读取最近的红外接收状态并继续下一次接收
   * @param status 红外状态输出地址
   * @return 状态读取成功返回 true，否则返回 false
   */
  virtual bool ReadInfraredStatus(InfraredStatus* status) = 0;
};

}  // namespace lilygo_box::hal
