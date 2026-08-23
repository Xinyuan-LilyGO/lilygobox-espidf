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
  kDisconnected,
  kComponentFailure,
};

enum class KeyboardExpansionComponentState : uint8_t {
  kNotChecked,
  kReady,
  kFailed,
};

enum class KeyboardKey : uint8_t {
  kUnknown,
  kCharacter,
  kEscape,
  kBackspace,
  kEnter,
  kLineBreak,
  kNext,
  kPrevious,
  kUp,
  kDown,
  kLeft,
  kRight,
  kCapsLock,
  kShift,
  kControl,
  kAlt,
  kMeta,
  kFunction,
  kRecord,
  kF1,
  kF2,
  kF3,
  kF4,
  kF5,
  kF6,
  kF7,
  kF8,
  kF9,
  kF10,
  kF11,
};

enum class KeyboardExpansionLed : uint8_t {
  kLed1,
  kLed2,
  kLed3,
};

struct KeyboardInputEvent {
  KeyboardKey key = KeyboardKey::kUnknown;
  uint32_t character = 0;
  uint8_t key_id = 0;
  bool pressed = false;
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
  int backlight_brightness_percent = -1;
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
   * @brief 让键盘扩展进入锁屏休眠并保留物理插拔检测
   * @return 键盘扫描、背光和扩展器件完成休眠时返回 true
   */
  virtual bool SuspendKeyboardExpansionForScreenLock() = 0;

  /**
   * @brief 在屏幕真正解锁后恢复键盘扩展
   * @return 按键扫描、背光和活动扩展器件恢复成功时返回 true
   */
  virtual bool ResumeKeyboardExpansionAfterScreenUnlock() = 0;

  /**
   * @brief 根据待处理的键盘中断更新扩展断开状态
   * @return 检查无需执行或状态更新完成时返回 true，否则返回 false
   */
  virtual bool UpdateKeyboardExpansionDisconnectionState() = 0;

  /**
   * @brief 判断是否存在尚未执行的键盘扩展断开确认
   * @return 存在待确认的键盘中断时返回 true
   */
  virtual bool HasKeyboardExpansionDisconnectionCheckPending() const = 0;

  /**
   * @brief 处理键盘扩展连接变化并在重新连接后启动扫描
   * @param scan_started 自动重连扫描已启动时写入 true
   * @return 连接监听和所需状态转换成功返回 true，否则返回 false
   */
  virtual bool UpdateKeyboardExpansionConnection(bool* scan_started) = 0;

  /**
   * @brief 判断是否收到尚未处理的键盘扩展连接信号
   * @return 存在待处理连接变化时返回 true
   */
  virtual bool HasKeyboardExpansionConnectionChangePending() const = 0;

  /**
   * @brief 设置键盘背光期望亮度
   * @param percent 亮度百分比，范围 0~100
   * @return 参数已保存且在扩展就绪时成功应用返回 true，否则返回 false
   */
  virtual bool SetKeyboardBacklightBrightnessPercent(int percent) = 0;

  /**
   * @brief 设置键盘扩展指示灯状态
   * @param led 键盘扩展指示灯
   * @param enabled true 点亮，false 熄灭
   * @return 指示灯状态设置成功返回 true，否则返回 false
   */
  virtual bool SetKeyboardExpansionLed(
      KeyboardExpansionLed led, bool enabled) = 0;

  /**
   * @brief 非阻塞读取一个实体键盘按键事件
   * @param event 按键事件输出地址
   * @return 读取到有效按下或释放事件返回 true，否则返回 false
   */
  virtual bool ReadKeyboardInputEvent(KeyboardInputEvent* event) = 0;

  /**
   * @brief 读取键盘扩展当前生命周期和逐器件状态
   * @param status 状态输出地址
   * @return 读取成功返回 true，否则返回 false
   */
  virtual bool ReadKeyboardExpansionStatus(
      KeyboardExpansionStatus* status) const = 0;
};

}  // namespace lilygo_box::hal
