#pragma once

#include <cstdint>

namespace lilygo_box::hal::p4_v2_fast_charge {

struct Snapshot {
  enum class State : uint8_t {
    kUnavailable,
    kDisconnected,
    kNegotiating,
    kPd,
    kPps,
    kFailed,
  };

  State state = State::kUnavailable;
  uint16_t voltage_mv = 0;
  uint16_t current_ma = 0;
};

/**
 * @brief 启动 P4 V2 的 AXP517 PD/PPS 输入协商服务
 * @return 服务任务创建成功或已经运行时返回 true
 */
bool Start();

/**
 * @brief 停止 PD/PPS 协商并在释放 AXP517 前撤销已建立的电源合同
 * @return 服务停止成功或未运行时返回 true；等待超时返回 false
 */
bool Stop();

/**
 * @brief 读取 PD/PPS 协商状态快照
 * @param snapshot 接收最近一次协商状态
 * @return 服务状态可读取时返回 true
 */
bool ReadSnapshot(Snapshot* snapshot);

}  // namespace lilygo_box::hal::p4_v2_fast_charge
