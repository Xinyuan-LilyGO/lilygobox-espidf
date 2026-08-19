/*
 * @Description: Keyboard expansion lifecycle provider
 * @Author: LILYGO_L
 * @Date: 2026-08-19 00:00:00
 * @LastEditTime: 2026-08-19 00:00:00
 * @License: GPL 3.0
 */
#pragma once

#include <cstdint>

namespace lilygo_box::hal {

enum class KeyboardExpansionState : uint8_t {
  kDisabled,
  kScanning,
  kReady,
  kNotFound,
  kComponentFailure,
};

enum class KeyboardExpansionComponentState : uint8_t {
  kNotChecked,
  kReady,
  kFailed,
};

struct KeyboardExpansionStatus {
  KeyboardExpansionState state = KeyboardExpansionState::kDisabled;
  KeyboardExpansionComponentState xl9555 =
      KeyboardExpansionComponentState::kNotChecked;
  KeyboardExpansionComponentState tca8418 =
      KeyboardExpansionComponentState::kNotChecked;
  KeyboardExpansionComponentState sy7200a =
      KeyboardExpansionComponentState::kNotChecked;
  KeyboardExpansionComponentState cc1101 =
      KeyboardExpansionComponentState::kNotChecked;
  KeyboardExpansionComponentState nrf24l01 =
      KeyboardExpansionComponentState::kNotChecked;
  KeyboardExpansionComponentState st25r3916 =
      KeyboardExpansionComponentState::kNotChecked;
  uint32_t scan_generation = 0;
};

class KeyboardExpansionProvider {
 public:
  virtual ~KeyboardExpansionProvider() = default;

  /**
   * @brief 启动一次键盘扩展硬件扫描和初始化任务
   * @return 任务已启动或扩展已经就绪时返回 true，否则返回 false
   */
  virtual bool StartKeyboardExpansionScan() = 0;

  /**
   * @brief 关闭键盘扩展并释放所有硬件资源
   * @return 扫描任务停止且硬件完成清理时返回 true，否则返回 false
   */
  virtual bool DisableKeyboardExpansion() = 0;

  /**
   * @brief 读取键盘扩展当前生命周期和逐器件状态
   * @param status 状态输出地址
   * @return 读取成功返回 true，否则返回 false
   */
  virtual bool ReadKeyboardExpansionStatus(
      KeyboardExpansionStatus* status) const = 0;
};

}  // namespace lilygo_box::hal
