/*
 * @Description: NFC 读卡器发现控制与卡片状态 Provider 接口
 * @Author: LILYGO_L
 * @Date: 2026-07-30 00:00:00
 * @LastEditTime: 2026-07-30 18:00:00
 * @License: GPL 3.0
 */
#pragma once

#include <cstddef>
#include <cstdint>

namespace lilygo_box::hal {

inline constexpr size_t kNfcIdentifierCapacity = 16;

// 应用层可识别的 NFC 轮询技术。
enum class NfcTechnology : uint8_t {
  kUnknown,
  kTypeA,
  kTypeB,
  kTypeF,
  kTypeV,
  kSt25Tb,
};

// NFC 发现任务和最近一张卡片的快照。
struct NfcStatus {
  // ST25R3916 及 RFAL 协议栈是否已经完成初始化。
  bool hardware_ready = false;
  // 后台轮询任务是否正在运行。
  bool polling = false;
  // 天线区域内是否存在已经激活的卡片。
  bool card_present = false;
  // 最近一次识别到的 NFC 技术。
  NfcTechnology technology = NfcTechnology::kUnknown;
  // 最近一次识别到的 NFC 标识符。
  uint8_t identifier[kNfcIdentifierCapacity] = {};
  // identifier 中的有效字节数。
  size_t identifier_length = 0;
  // 本次启动轮询后检测到的新卡片次数。
  uint32_t detection_count = 0;
  // 最近一次 RFAL 或平台错误码，0 表示无错误。
  int last_error = 0;
};

class NfcProvider {
 public:
  virtual ~NfcProvider() = default;

  /**
   * @brief 启动或停止 NFC-A、B、F、V 与 ST25TB 后台发现
   * @param enabled true 启动轮询，false 停止轮询并关闭射频场
   * @return 请求成功接受或目标状态已经满足返回 true
   */
  virtual bool SetNfcPollingEnabled(bool enabled) = 0;

  /**
   * @brief 非阻塞读取 NFC 轮询和最近卡片状态
   * @param status NFC 状态输出地址
   * @return 状态读取成功返回 true，否则返回 false
   */
  virtual bool ReadNfcStatus(NfcStatus* status) = 0;
};

}  // namespace lilygo_box::hal
