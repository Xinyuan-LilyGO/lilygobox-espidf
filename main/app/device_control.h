/*
 * @Description: 桌面控制命令与设备状态快照，不在网络任务中执行硬件操作。
 * @Author: LILYGO_L
 * @Date: 2026-10-12 00:00:00
 * @LastEditTime: 2026-10-12 00:00:00
 * @License: GPL 3.0
 */
#pragma once

#include <cstdint>

#include "hal/providers/battery_management_provider.h"

namespace lilygo_box::app {

enum class DeviceControlField {
  kScreenEnabled,
  kBrightness,
  kRotation,
  kLockTimeoutSeconds,
  kSoundEnabled,
  kVolume,
  kHapticEnabled,
  kHapticStrength,
  kBatteryCapacityMah,
};

struct DeviceControlCommand {
  uint32_t session_id = 0;
  int request_id = 0;
  DeviceControlField field = DeviceControlField::kBrightness;
  int value = 0;
};

struct DeviceControlState {
  bool ready = false;
  bool screen_supported = false;
  bool audio_supported = false;
  bool haptic_supported = false;
  bool battery_supported = false;
  int battery_capacity_mah = 1000;
  int battery_capacity_minimum_mah = 1;
  int battery_capacity_maximum_mah = 65535;
  bool screen_enabled = true;
  bool sound_enabled = true;
  bool haptic_enabled = true;
  int brightness = 90;
  int rotation = 0;
  int lock_timeout_seconds = 5 * 60;
  int volume = 90;
  int haptic_strength = 90;
  hal::BatteryManagementStatus battery;
};

/**
 * @brief 取出已授权会话的单条命令；屏幕开关只交给锁屏任务执行
 * @param screen_task 是否由锁屏任务调用
 * @param command 命令输出，成功取出后不会重复执行
 * @return 存在匹配且尚未过期的命令时返回 true
 */
bool TakeDeviceControlCommand(bool screen_task, DeviceControlCommand* command);

/**
 * @brief 完成命令并保存结果；断开或会话替换后的旧结果自动丢弃
 * @param command 已执行的命令
 * @param error 空字符串表示成功，否则为稳定的错误代码
 */
void CompleteDeviceControlCommand(const DeviceControlCommand& command,
                                  const char* error);

/**
 * @brief 发布应用任务采集的状态，网络任务仅复制该快照
 * @param state 当前设备状态
 */
void PublishDeviceControlState(const DeviceControlState& state);

}  // namespace lilygo_box::app
