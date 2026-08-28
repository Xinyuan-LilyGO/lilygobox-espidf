/*
 * @Description: T-Display-P4 键盘扩展硬件实现
 * @Author: LILYGO_L
 * @Date: 2026-08-28 00:00:00
 * @LastEditTime: 2026-08-28 00:00:00
 * @License: GPL 3.0
 */
#include "hal/device/t_display_p4/device.h"

#include <atomic>
#include <cstdint>

#include "base/logger.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace lilygo_box::hal {
namespace keyboard_device =
    lilygo_device_driver::t_display_p4::keyboard_expansion::device;
namespace keyboard_gpio =
    lilygo_device_driver::t_display_p4::keyboard_expansion::gpio;
namespace {

using DriverKeyboardExpansionLed =
    lilygo_device_driver::TDisplayP4Driver::KeyboardExpansionLed;

constexpr int kKeyboardBacklightBrightnessMinPercent = 0;
constexpr int kKeyboardBacklightBrightnessMaxPercent = 100;
// 键盘背光 PWM 只使用 0~500/1000，避免负载过大影响系统稳定性。
constexpr uint32_t kKeyboardBacklightDutyMax = 500;
constexpr uint32_t kSy7200aDutyScale = 1000;

/**
 * @brief 将键盘背光百分比转换为 SY7200A PWM 占空比
 * @param percent 键盘背光亮度百分比
 * @return SY7200A PWM 占空比
 */
cpp_bus_driver::Pwm::DutyCycle
KeyboardBacklightBrightnessPercentToSy7200aDutyCycle(int percent) {
  return {
      .value = static_cast<uint32_t>(percent) * kKeyboardBacklightDutyMax /
               kKeyboardBacklightBrightnessMaxPercent,
      .scale = kSy7200aDutyScale,
  };
}

/**
 * @brief 将键盘扩展硬件键值转换为通用键盘键值
 * @param key_code 键盘扩展硬件键值
 * @param shift_pressed Shift 当前是否按下
 * @return 通用键盘键值
 */
KeyboardKey ToKeyboardKey(
    keyboard_device::tca8418::KeyCode key_code, bool shift_pressed) {
  using KeyCode = keyboard_device::tca8418::KeyCode;
  switch (key_code) {
    case KeyCode::kCharacter:
      return KeyboardKey::kCharacter;
    case KeyCode::kEscape:
      return KeyboardKey::kEscape;
    case KeyCode::kBackspace:
      return KeyboardKey::kBackspace;
    case KeyCode::kEnter:
      return shift_pressed ? KeyboardKey::kLineBreak : KeyboardKey::kEnter;
    case KeyCode::kTab:
      return shift_pressed ? KeyboardKey::kPrevious : KeyboardKey::kNext;
    case KeyCode::kUp:
      return KeyboardKey::kUp;
    case KeyCode::kDown:
      return KeyboardKey::kDown;
    case KeyCode::kLeft:
      return KeyboardKey::kLeft;
    case KeyCode::kRight:
      return KeyboardKey::kRight;
    case KeyCode::kCapsLock:
      return KeyboardKey::kCapsLock;
    case KeyCode::kShift:
      return KeyboardKey::kShift;
    case KeyCode::kControl:
      return KeyboardKey::kControl;
    case KeyCode::kAlt:
      return KeyboardKey::kAlt;
    case KeyCode::kMeta:
      return KeyboardKey::kMeta;
    case KeyCode::kFunction:
      return KeyboardKey::kFunction;
    case KeyCode::kRecord:
      return KeyboardKey::kRecord;
    case KeyCode::kF1:
      return KeyboardKey::kF1;
    case KeyCode::kF2:
      return KeyboardKey::kF2;
    case KeyCode::kF3:
      return KeyboardKey::kF3;
    case KeyCode::kF4:
      return KeyboardKey::kF4;
    case KeyCode::kF5:
      return KeyboardKey::kF5;
    case KeyCode::kF6:
      return KeyboardKey::kF6;
    case KeyCode::kF7:
      return KeyboardKey::kF7;
    case KeyCode::kF8:
      return KeyboardKey::kF8;
    case KeyCode::kF9:
      return KeyboardKey::kF9;
    case KeyCode::kF10:
      return KeyboardKey::kF10;
    case KeyCode::kF11:
      return KeyboardKey::kF11;
    case KeyCode::kUnknown:
    default:
      return KeyboardKey::kUnknown;
  }
}

/**
 * @brief 根据 Fn、Shift 和 Caps Lock 状态解析实体键盘字符
 * @param mapping 实体键盘硬件映射
 * @param function_pressed Fn 当前是否按下
 * @param shift_pressed Shift 当前是否按下
 * @param caps_lock_enabled Caps Lock 当前是否启用
 * @return ASCII 字符值，无有效字符返回 0
 */
uint32_t ResolveKeyboardCharacter(
    const keyboard_device::tca8418::KeyMapping& mapping,
    bool function_pressed, bool shift_pressed, bool caps_lock_enabled) {
  const bool use_function_character =
      function_pressed && mapping.function_character != '\0';
  char character = use_function_character
      ? mapping.function_character
      : mapping.character;
  if (!use_function_character && shift_pressed != caps_lock_enabled &&
      character >= 'a' && character <= 'z') {
    character = static_cast<char>(character - 'a' + 'A');
  }
  return static_cast<uint8_t>(character);
}

}  // namespace

bool TDisplayP4Device::InitializeKeyboardExpansionConnectionInterrupt(
    bool detect_current_level) {
  if (tool_ == nullptr) {
    return false;
  }

  bool expected = false;
  if (!keyboard_expansion_.interrupt_initialized
           .compare_exchange_strong(expected, true)) {
    return true;
  }

  keyboard_expansion_.connection_interrupt_pending.store(
      false, std::memory_order_relaxed);
  if (!tool_->SetGpioMode(keyboard_gpio::tca8418::kInt,
          cpp_bus_driver::Tool::GpioMode::kInput,
          cpp_bus_driver::Tool::GpioStatus::kPulldown)) {
    keyboard_expansion_.interrupt_initialized.store(false);
    return false;
  }
  const bool connected_before_interrupt =
      tool_->GpioRead(keyboard_gpio::tca8418::kInt);
  if (!tool_->InitGpioInterrupt(keyboard_gpio::tca8418::kInt,
          cpp_bus_driver::Tool::InterruptMode::kRising,
          KeyboardExpansionConnectionInterruptHandler, this,
          cpp_bus_driver::Tool::GpioStatus::kPulldown)) {
    keyboard_expansion_.interrupt_initialized.store(false);
    return false;
  }

  // 初始化失败且连接线始终为高时等待下一次实际插拔，避免循环扫描；
  // 监听注册期间出现的低到高变化仍需立即补记。
  if ((detect_current_level || !connected_before_interrupt) &&
      tool_->GpioRead(keyboard_gpio::tca8418::kInt)) {
    keyboard_expansion_.connection_interrupt_tick.store(
        xTaskGetTickCount(), std::memory_order_relaxed);
    keyboard_expansion_.connection_interrupt_pending.store(
        true, std::memory_order_release);
  }
  return true;
}

bool TDisplayP4Device::InitializeKeyboardInputInterrupt() {
  if (tool_ == nullptr) {
    return false;
  }

  bool expected = false;
  if (!keyboard_expansion_.interrupt_initialized.compare_exchange_strong(
          expected, true)) {
    return true;
  }

  keyboard_expansion_.input_interrupt_pending.store(
      false, std::memory_order_relaxed);
  keyboard_expansion_.disconnection_check_pending.store(
      false, std::memory_order_relaxed);
  if (!tool_->InitGpioInterrupt(keyboard_gpio::tca8418::kInt,
          cpp_bus_driver::Tool::InterruptMode::kFalling,
          KeyboardInputInterruptHandler, this,
          cpp_bus_driver::Tool::GpioStatus::kPulldown)) {
    keyboard_expansion_.interrupt_initialized.store(false);
    return false;
  }

  // 注册中断前 INT 可能已经拉低，需要补记已存在的按键事件。
  if (!tool_->GpioRead(keyboard_gpio::tca8418::kInt)) {
    keyboard_expansion_.input_interrupt_pending.store(
        true, std::memory_order_release);
    keyboard_expansion_.disconnection_check_pending.store(
        true, std::memory_order_release);
  }
  return true;
}

bool TDisplayP4Device::DeinitializeKeyboardExpansionInterrupt() {
  if (!keyboard_expansion_.interrupt_initialized.exchange(false)) {
    keyboard_expansion_.connection_interrupt_pending.store(
        false, std::memory_order_relaxed);
    keyboard_expansion_.input_interrupt_pending.store(
        false, std::memory_order_relaxed);
    keyboard_expansion_.disconnection_check_pending.store(
        false, std::memory_order_relaxed);
    return true;
  }

  const bool result = tool_ != nullptr &&
      tool_->DeinitGpioInterrupt(keyboard_gpio::tca8418::kInt);
  keyboard_expansion_.connection_interrupt_pending.store(
      false, std::memory_order_relaxed);
  keyboard_expansion_.input_interrupt_pending.store(
      false, std::memory_order_relaxed);
  keyboard_expansion_.disconnection_check_pending.store(
      false, std::memory_order_relaxed);
  return result;
}

void TDisplayP4Device::KeyboardExpansionConnectionInterruptHandler(
    void* context) {
  if (context == nullptr) {
    return;
  }
  auto* device = static_cast<TDisplayP4Device*>(context);
  device->keyboard_expansion_.connection_interrupt_tick.store(
      xTaskGetTickCountFromISR(), std::memory_order_relaxed);
  device->keyboard_expansion_.connection_interrupt_pending.store(
      true, std::memory_order_release);
}

void TDisplayP4Device::KeyboardInputInterruptHandler(void* context) {
  if (context == nullptr) {
    return;
  }
  auto* device = static_cast<TDisplayP4Device*>(context);
  device->keyboard_expansion_.input_interrupt_pending.store(
      true, std::memory_order_release);
  device->keyboard_expansion_.disconnection_check_pending.store(
      true, std::memory_order_release);
}

void TDisplayP4Device::RecordKeyboardInputReadFailure() {
  const uint8_t failure_count =
      keyboard_expansion_.consecutive_read_failures.fetch_add(1) + 1;
  if (failure_count < kKeyboardExpansionDisconnectFailureThreshold) {
    if (keyboard_expansion_.interrupt_initialized.load(
            std::memory_order_acquire)) {
      keyboard_expansion_.input_interrupt_pending.store(
          true, std::memory_order_release);
    }
    return;
  }

  KeyboardExpansionState expected = KeyboardExpansionState::kReady;
  if (keyboard_expansion_.state.compare_exchange_strong(
          expected, KeyboardExpansionState::kDisconnected)) {
    keyboard_expansion_.tca8418.store(
        KeyboardExpansionComponentState::kFailed);
    keyboard_expansion_.shift_pressed.store(false);
    keyboard_expansion_.function_pressed.store(false);
    keyboard_expansion_.caps_lock_enabled.store(false);
    keyboard_expansion_.scan_generation.fetch_add(1);
    LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
        "Keyboard expansion disconnected\n");
  }
}

bool TDisplayP4Device::StartKeyboardExpansionScan() {
  if (keyboard_expansion_.state.load() == KeyboardExpansionState::kReady) {
    return true;
  }

  if (!DeinitializeKeyboardExpansionInterrupt()) {
    return false;
  }

  bool expected = false;
  if (!keyboard_expansion_.task_running.compare_exchange_strong(
          expected, true)) {
    return keyboard_expansion_.state.load() ==
           KeyboardExpansionState::kScanning;
  }

  keyboard_expansion_.xl9555.store(
      KeyboardExpansionComponentState::kNotChecked);
  keyboard_expansion_.tca8418.store(
      KeyboardExpansionComponentState::kNotChecked);
  keyboard_expansion_.sy7200a.store(
      KeyboardExpansionComponentState::kNotChecked);
  keyboard_expansion_.cc1101.store(
      KeyboardExpansionComponentState::kNotChecked);
  keyboard_expansion_.nrf24l01.store(
      KeyboardExpansionComponentState::kNotChecked);
  keyboard_expansion_.st25r3916.store(
      KeyboardExpansionComponentState::kNotChecked);
  keyboard_expansion_.shift_pressed.store(false);
  keyboard_expansion_.function_pressed.store(false);
  keyboard_expansion_.caps_lock_enabled.store(false);
  keyboard_expansion_.consecutive_read_failures.store(0);
  keyboard_expansion_.state.store(KeyboardExpansionState::kScanning);

  if (xTaskCreate(KeyboardExpansionScanTaskEntry,
          "KeyboardExpScan", kKeyboardExpansionTaskStackBytes, this,
          kKeyboardExpansionTaskPriority, nullptr) != pdPASS) {
    keyboard_expansion_.task_running.store(false);
    keyboard_expansion_.state.store(
        KeyboardExpansionState::kComponentFailure);
    keyboard_expansion_.scan_generation.fetch_add(1);
    if (!InitializeKeyboardExpansionConnectionInterrupt(false)) {
      LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
          "Initialize keyboard expansion connection interrupt failed\n");
    }
    return false;
  }
  return true;
}

bool TDisplayP4Device::DeinitializeKeyboardExpansionHardware(
    KeyboardExpansionState final_state) {
  const KeyboardExpansionState previous_state =
      keyboard_expansion_.state.load();
  RadioState* extension_states[] = {&cc1101_radio_, &nrf24l01_radio_};
  for (RadioState* state : extension_states) {
    if (state->mutex == nullptr ||
        xSemaphoreTake(state->mutex, pdMS_TO_TICKS(2000)) != pdTRUE) {
      LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
          "Wait for keyboard expansion Radio session failed\n");
      return false;
    }
    const bool active = state->active;
    const uint32_t client_token = state->active_client_token;
    const radio::ChipType chip = state->chip;
    const radio::ProtocolType protocol = state->protocol;
    xSemaphoreGive(state->mutex);
    if (!active) {
      continue;
    }
    if (!DeactivateRadio(client_token)) {
      LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
          "Stop keyboard expansion Radio session failed\n");
      return false;
    }
    if (xSemaphoreTake(state->mutex, pdMS_TO_TICKS(2000)) != pdTRUE) {
      return false;
    }
    // 保留逻辑激活项，让扩展板重新连接后由 Radio 页面自动恢复会话。
    state->active_client_token = client_token;
    state->chip = chip;
    state->protocol = protocol;
    state->chip_error = true;
    xSemaphoreGive(state->mutex);
  }
  // 先停止发布键盘和扩展射频能力，避免清理过程中重新排队硬件操作。
  keyboard_expansion_.state.store(final_state);
  if (!SetNfcPollingEnabled(false)) {
    LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
        "Stop keyboard expansion NFC polling failed\n");
    keyboard_expansion_.state.store(
        KeyboardExpansionState::kComponentFailure);
    return false;
  }
  if (!WaitForKeyboardExpansionTask()) {
    keyboard_expansion_.state.store(
        KeyboardExpansionState::kComponentFailure);
    return false;
  }

  const bool interrupt_deinitialized =
      DeinitializeKeyboardExpansionInterrupt();
  const auto deinit_mode =
      previous_state == KeyboardExpansionState::kDisconnected
          ? lilygo_device_driver::TDisplayP4Driver::
                KeyboardExpansionDeinitMode::kForced
          : lilygo_device_driver::TDisplayP4Driver::
                KeyboardExpansionDeinitMode::kNormal;
  const bool result =
      driver_.DeinitKeyboardExpansion(deinit_mode) && interrupt_deinitialized;
  keyboard_expansion_.xl9555.store(
      KeyboardExpansionComponentState::kNotChecked);
  keyboard_expansion_.tca8418.store(
      KeyboardExpansionComponentState::kNotChecked);
  keyboard_expansion_.sy7200a.store(
      KeyboardExpansionComponentState::kNotChecked);
  keyboard_expansion_.cc1101.store(
      KeyboardExpansionComponentState::kNotChecked);
  keyboard_expansion_.nrf24l01.store(
      KeyboardExpansionComponentState::kNotChecked);
  keyboard_expansion_.st25r3916.store(
      KeyboardExpansionComponentState::kNotChecked);
  keyboard_expansion_.shift_pressed.store(false);
  keyboard_expansion_.function_pressed.store(false);
  keyboard_expansion_.caps_lock_enabled.store(false);
  keyboard_expansion_.consecutive_read_failures.store(0);
  keyboard_expansion_.state.store(result
          ? final_state
          : KeyboardExpansionState::kComponentFailure);
  keyboard_expansion_.scan_generation.fetch_add(1);
  return result;
}

bool TDisplayP4Device::DisableKeyboardExpansion() {
  return DeinitializeKeyboardExpansionHardware(
      KeyboardExpansionState::kDisabled);
}

bool TDisplayP4Device::SuspendKeyboardExpansionForScreenLock() {
  keyboard_expansion_.screen_lock_suspended.store(true);
  if (keyboard_expansion_.task_running.load() &&
      !WaitForKeyboardExpansionTask()) {
    return false;
  }
  if (keyboard_expansion_.state.load() != KeyboardExpansionState::kReady) {
    return true;
  }

  return ApplyKeyboardExpansionScreenLockSleep();
}

bool TDisplayP4Device::ApplyKeyboardExpansionScreenLockSleep() {
  bool result = driver_.IsTca8418Ready() &&
      driver_.chip().tca8418 != nullptr;
  if (result) {
    auto* keyboard = driver_.chip().tca8418.get();
    result &= keyboard->SetInterruptEnable(0);
    result &= keyboard->SetKeypadPins(0);
    result &= keyboard->ClearEventFifo();
  }
  keyboard_expansion_.input_interrupt_pending.store(
      false, std::memory_order_relaxed);
  keyboard_expansion_.disconnection_check_pending.store(
      false, std::memory_order_relaxed);
  keyboard_expansion_.shift_pressed.store(false);
  keyboard_expansion_.function_pressed.store(false);
  keyboard_expansion_.consecutive_read_failures.store(0);
  result &= driver_.SetKeyboardExpansionOperatingMode(
      lilygo_device_driver::TDisplayP4Driver::
          KeyboardExpansionOperatingMode::kSleep);
  return result;
}

bool TDisplayP4Device::ResumeKeyboardExpansionAfterScreenUnlock() {
  if (!keyboard_expansion_.screen_lock_suspended.load()) {
    return true;
  }
  if (keyboard_expansion_.task_running.load() &&
      !WaitForKeyboardExpansionTask()) {
    return false;
  }
  if (keyboard_expansion_.state.load() != KeyboardExpansionState::kReady) {
    keyboard_expansion_.screen_lock_suspended.store(false);
    return true;
  }

  bool input_restored = driver_.IsTca8418Ready() &&
      driver_.chip().tca8418 != nullptr;
  if (input_restored) {
    auto* keyboard = driver_.chip().tca8418.get();
    input_restored &= keyboard->ClearEventFifo();
    input_restored &= keyboard->SetKeypadScanWindow(0, 0,
        keyboard_device::tca8418::kKeypadScanWidth,
        keyboard_device::tca8418::kKeypadScanHeight);
    input_restored &= keyboard->ClearEventFifo();
    input_restored &= keyboard->SetIrqGpioMode(
        cpp_bus_driver::Tca8418::IrqMask::kKeyEvents);
  }
  keyboard_expansion_.input_interrupt_pending.store(
      false, std::memory_order_relaxed);
  keyboard_expansion_.disconnection_check_pending.store(
      false, std::memory_order_relaxed);
  keyboard_expansion_.shift_pressed.store(false);
  keyboard_expansion_.function_pressed.store(false);
  keyboard_expansion_.consecutive_read_failures.store(0);
  if (!input_restored) {
    return false;
  }
  keyboard_expansion_.screen_lock_suspended.store(false);
  return RestoreKeyboardExpansionOperatingState();
}

bool TDisplayP4Device::HasKeyboardExpansionDisconnectionCheckPending() const {
  return keyboard_expansion_.disconnection_check_pending.load(
      std::memory_order_acquire);
}

bool TDisplayP4Device::UpdateKeyboardExpansionDisconnectionState() {
  if (keyboard_expansion_.state.load() != KeyboardExpansionState::kReady ||
      !keyboard_expansion_.interrupt_initialized.load(
          std::memory_order_acquire) ||
      !keyboard_expansion_.disconnection_check_pending.exchange(
          false, std::memory_order_acq_rel)) {
    return true;
  }
  if (tool_ == nullptr || !driver_.IsTca8418Ready() ||
      driver_.chip().tca8418 == nullptr) {
    return false;
  }

  // 扩展板上的 INT 空闲时由外部上拉保持高电平，拔出后由主板内部
  // 下拉保持低电平。下降沿也可能来自正常按键，因此需要通过一次
  // TCA8418 通信确认，不能仅根据 GPIO 电平判定扩展已断开。
  if (tool_->GpioRead(keyboard_gpio::tca8418::kInt)) {
    keyboard_expansion_.consecutive_read_failures.store(0);
    return true;
  }
  if (driver_.chip().tca8418->GetFingerCount() != UINT8_MAX) {
    keyboard_expansion_.consecutive_read_failures.store(0);
    // 锁屏时按键事件可能暂时不被 LVGL 消费，INT 会持续为低。保留低频
    // 复查，确保此后直接拔出扩展板时仍能发现通信已经中断。
    if (!tool_->GpioRead(keyboard_gpio::tca8418::kInt)) {
      keyboard_expansion_.disconnection_check_pending.store(
          true, std::memory_order_release);
    }
    return true;
  }

  RecordKeyboardInputReadFailure();
  if (keyboard_expansion_.state.load() == KeyboardExpansionState::kReady) {
    keyboard_expansion_.disconnection_check_pending.store(
        true, std::memory_order_release);
  }
  return true;
}

bool TDisplayP4Device::UpdateKeyboardExpansionConnection(
    bool* scan_started) {
  if (scan_started != nullptr) {
    *scan_started = false;
  }

  KeyboardExpansionState state = keyboard_expansion_.state.load();
  if (state == KeyboardExpansionState::kReady ||
      state == KeyboardExpansionState::kScanning) {
    return true;
  }
  if (state == KeyboardExpansionState::kDisabled) {
    return true;
  }

  if (state == KeyboardExpansionState::kDisconnected) {
    if (!DeinitializeKeyboardExpansionHardware(
            KeyboardExpansionState::kNotFound)) {
      return false;
    }
    if (!InitializeKeyboardExpansionConnectionInterrupt(true)) {
      return false;
    }
  } else if (!InitializeKeyboardExpansionConnectionInterrupt(false)) {
    return false;
  }

  if (!keyboard_expansion_.connection_interrupt_pending.load(
          std::memory_order_acquire)) {
    return true;
  }
  const TickType_t interrupt_tick =
      keyboard_expansion_.connection_interrupt_tick.load(
          std::memory_order_relaxed);
  if (xTaskGetTickCount() - interrupt_tick <
      pdMS_TO_TICKS(kKeyboardExpansionConnectionDebounceMs)) {
    return true;
  }
  if (!keyboard_expansion_.connection_interrupt_pending.exchange(
          false, std::memory_order_acq_rel)) {
    return true;
  }
  if (tool_ == nullptr ||
      !tool_->GpioRead(keyboard_gpio::tca8418::kInt)) {
    return true;
  }

  const bool started = StartKeyboardExpansionScan();
  if (scan_started != nullptr) {
    *scan_started = started;
  }
  return started;
}

bool TDisplayP4Device::HasKeyboardExpansionConnectionChangePending() const {
  return keyboard_expansion_.connection_interrupt_pending.load(
      std::memory_order_acquire);
}

bool TDisplayP4Device::SetKeyboardBacklightBrightnessPercent(int percent) {
  if (percent < kKeyboardBacklightBrightnessMinPercent ||
      percent > kKeyboardBacklightBrightnessMaxPercent) {
    return false;
  }

  if (driver_.IsSy7200aReady()) {
    bool applied = false;
    if (percent == kKeyboardBacklightBrightnessMinPercent) {
      applied = driver_.chip().sy7200a->DisableOutput(
          cpp_bus_driver::Pwm::IdleLevel::kLow);
    } else {
      applied = driver_.chip().sy7200a->SetDuty(
          KeyboardBacklightBrightnessPercentToSy7200aDutyCycle(percent));
    }
    if (!applied) {
      return false;
    }
  } else if (keyboard_expansion_.state.load() ==
             KeyboardExpansionState::kReady) {
    return false;
  }

  keyboard_expansion_.backlight_brightness_percent.store(percent);
  return true;
}

bool TDisplayP4Device::SetKeyboardExpansionLed(
    KeyboardExpansionLed led, bool enabled) {
  if (keyboard_expansion_.state.load() != KeyboardExpansionState::kReady ||
      !driver_.IsXl9555Ready()) {
    return false;
  }

  DriverKeyboardExpansionLed driver_led;
  switch (led) {
    case KeyboardExpansionLed::kLed1:
      driver_led = DriverKeyboardExpansionLed::kLed1;
      break;
    case KeyboardExpansionLed::kLed2:
      driver_led = DriverKeyboardExpansionLed::kLed2;
      break;
    case KeyboardExpansionLed::kLed3:
      driver_led = DriverKeyboardExpansionLed::kLed3;
      break;
    default:
      return false;
  }
  return driver_.SetKeyboardExpansionLed(driver_led, enabled);
}

bool TDisplayP4Device::ReadKeyboardInputEvent(KeyboardInputEvent* event) {
  if (event == nullptr || tool_ == nullptr ||
      keyboard_expansion_.screen_lock_suspended.load() ||
      keyboard_expansion_.state.load() != KeyboardExpansionState::kReady ||
      !driver_.IsTca8418Ready() || driver_.chip().tca8418 == nullptr) {
    return false;
  }

  const bool interrupt_initialized =
      keyboard_expansion_.interrupt_initialized.load(
          std::memory_order_acquire);
  if (interrupt_initialized &&
      !keyboard_expansion_.input_interrupt_pending.exchange(
          false, std::memory_order_acq_rel)) {
    return false;
  }
  // 中断初始化失败时保留 GPIO 轮询回退，避免键盘完全失去输入。
  if (tool_->GpioRead(keyboard_gpio::tca8418::kInt)) {
    return false;
  }

  const uint8_t event_count = driver_.chip().tca8418->GetFingerCount();
  if (event_count == UINT8_MAX) {
    RecordKeyboardInputReadFailure();
    return false;
  }
  if (event_count == 0 || event_count > 10) {
    if (event_count == 0) {
      if (!driver_.chip().tca8418->ClearIrqFlag(
              cpp_bus_driver::Tca8418::IrqFlag::kKeyEvents)) {
        RecordKeyboardInputReadFailure();
      } else {
        keyboard_expansion_.consecutive_read_failures.store(0);
      }
    } else {
      keyboard_expansion_.consecutive_read_failures.store(0);
    }
    return false;
  }

  cpp_bus_driver::Tca8418::TouchInfo input;
  if (!driver_.chip().tca8418->ReadKeyEvent(&input)) {
    RecordKeyboardInputReadFailure();
    return false;
  }
  if (event_count > 1 && interrupt_initialized) {
    keyboard_expansion_.input_interrupt_pending.store(
        true, std::memory_order_release);
  }
  if (event_count == 1) {
    if (!driver_.chip().tca8418->ClearIrqFlag(
            cpp_bus_driver::Tca8418::IrqFlag::kKeyEvents)) {
      RecordKeyboardInputReadFailure();
    } else {
      keyboard_expansion_.consecutive_read_failures.store(0);
    }
  } else {
    keyboard_expansion_.consecutive_read_failures.store(0);
  }
  if (input.num == 0 ||
      input.num > keyboard_device::tca8418::kMap.size()) {
    return false;
  }

  const keyboard_device::tca8418::KeyMapping& mapping =
      keyboard_device::tca8418::kMap[input.num - 1];
  const bool shift_pressed = keyboard_expansion_.shift_pressed.load();
  const bool function_pressed = keyboard_expansion_.function_pressed.load();
  if (mapping.key == keyboard_device::tca8418::KeyCode::kShift) {
    keyboard_expansion_.shift_pressed.store(input.press_flag);
  } else if (mapping.key ==
             keyboard_device::tca8418::KeyCode::kFunction) {
    keyboard_expansion_.function_pressed.store(input.press_flag);
  } else if (mapping.key ==
                 keyboard_device::tca8418::KeyCode::kCapsLock &&
             input.press_flag) {
    const bool caps_lock_enabled =
        !keyboard_expansion_.caps_lock_enabled.load();
    keyboard_expansion_.caps_lock_enabled.store(caps_lock_enabled);
    if (!SetKeyboardExpansionLed(
            KeyboardExpansionLed::kLed1, caps_lock_enabled)) {
      LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
          "Set keyboard Caps Lock indicator failed\n");
    }
  }

  event->key = ToKeyboardKey(mapping.key, shift_pressed);
  event->character = mapping.key ==
          keyboard_device::tca8418::KeyCode::kCharacter
      ? ResolveKeyboardCharacter(mapping, function_pressed, shift_pressed,
            keyboard_expansion_.caps_lock_enabled.load())
      : 0;
  event->key_id = input.num;
  event->pressed = input.press_flag;
  return event->key != KeyboardKey::kUnknown;
}

bool TDisplayP4Device::ReadKeyboardExpansionStatus(
    KeyboardExpansionStatus* status) const {
  if (status == nullptr) {
    return false;
  }
  status->state = keyboard_expansion_.state.load();
  status->xl9555 = keyboard_expansion_.xl9555.load();
  status->tca8418 = keyboard_expansion_.tca8418.load();
  status->sy7200a = keyboard_expansion_.sy7200a.load();
  status->cc1101 = keyboard_expansion_.cc1101.load();
  status->nrf24l01 = keyboard_expansion_.nrf24l01.load();
  status->st25r3916 = keyboard_expansion_.st25r3916.load();
  status->backlight_brightness_percent =
      keyboard_expansion_.backlight_brightness_percent.load();
  status->scan_generation = keyboard_expansion_.scan_generation.load();
  return true;
}

void TDisplayP4Device::KeyboardExpansionScanTaskEntry(void* context) {
  auto* device = static_cast<TDisplayP4Device*>(context);
  if (device != nullptr) {
    device->RunKeyboardExpansionScanTask();
  }
  vTaskDelete(nullptr);
}

void TDisplayP4Device::RunKeyboardExpansionScanTask() {
  const bool initialized = driver_.InitKeyboardExpansion();
  const bool keep_screen_lock_suspended =
      keyboard_expansion_.screen_lock_suspended.load();
  const int backlight_brightness_percent =
      keyboard_expansion_.backlight_brightness_percent.load();
  const bool backlight_applied = !initialized || keep_screen_lock_suspended ||
      SetKeyboardBacklightBrightnessPercent(backlight_brightness_percent);

  // 将各扩展芯片的初始化结果统一转换为对外组件状态。
  const auto component_state = [](bool ready) {
    return ready ? KeyboardExpansionComponentState::kReady
                 : KeyboardExpansionComponentState::kFailed;
  };
  const bool xl9555_ready = driver_.IsXl9555Ready();
  keyboard_expansion_.xl9555.store(component_state(xl9555_ready));
  keyboard_expansion_.tca8418.store(
      component_state(driver_.IsTca8418Ready()));
  keyboard_expansion_.sy7200a.store(
      component_state(driver_.IsSy7200aReady() && backlight_applied));
  keyboard_expansion_.cc1101.store(
      component_state(driver_.IsCc1101Ready()));
  keyboard_expansion_.nrf24l01.store(
      component_state(driver_.IsNrf24l01Ready()));
  keyboard_expansion_.st25r3916.store(
      component_state(driver_.IsSt25r3916Ready()));

  KeyboardExpansionState state;
  if (initialized && backlight_applied) {
    state = KeyboardExpansionState::kReady;
  } else if (!xl9555_ready) {
    state = KeyboardExpansionState::kNotFound;
  } else {
    state = KeyboardExpansionState::kComponentFailure;
  }

  if (state != KeyboardExpansionState::kReady &&
      !driver_.DeinitKeyboardExpansion()) {
    LogMessage(LogLevel::kError, __FILE__, __LINE__,
        "Keyboard expansion cleanup failed\n");
  }
  if (state == KeyboardExpansionState::kReady &&
      !InitializeKeyboardInputInterrupt()) {
    LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
        "Initialize keyboard input interrupt failed; using polling fallback\n");
  }
  if (state == KeyboardExpansionState::kReady &&
      keep_screen_lock_suspended &&
      !ApplyKeyboardExpansionScreenLockSleep()) {
    LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
        "Keep keyboard expansion asleep after locked scan failed\n");
  }
  keyboard_expansion_.state.store(state);
  keyboard_expansion_.scan_generation.fetch_add(1);
  if (state != KeyboardExpansionState::kReady &&
      !InitializeKeyboardExpansionConnectionInterrupt(false)) {
    LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
        "Initialize keyboard expansion connection interrupt failed\n");
  }
  keyboard_expansion_.task_running.store(false);
}

bool TDisplayP4Device::WaitForKeyboardExpansionTask() {
  for (int elapsed_ms = 0; elapsed_ms < kPowerOffTaskTimeoutMs;
      elapsed_ms += kPowerOffTaskPollMs) {
    if (!keyboard_expansion_.task_running.load()) {
      return true;
    }
    vTaskDelay(pdMS_TO_TICKS(kPowerOffTaskPollMs));
  }
  return !keyboard_expansion_.task_running.load();
}

}  // namespace lilygo_box::hal
