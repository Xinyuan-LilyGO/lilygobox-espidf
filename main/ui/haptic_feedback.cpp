/*
 * @Description: UI haptic feedback helpers
 * @Author: LILYGO_L
 * @Date: 2026-06-30 00:00:00
 * @LastEditTime: 2026-06-30 00:00:00
 * @License: GPL 3.0
 */
#include "ui/haptic_feedback.h"

#include <algorithm>
#include <cstdint>

#include "app/storage/haptic_storage.h"
#include "hal/providers/haptic_provider.h"

namespace lilygo_box::ui {
namespace {

hal::HapticProvider* g_haptic_provider = nullptr;
bool g_haptic_preferences_loaded = false;
bool g_haptic_enabled = true;
int g_haptic_strength_percent = 45;

/**
 * @brief 确保已从 NVS 加载 UI 交互振动反馈偏好
 */
void EnsureHapticPreferencesLoaded() {
  if (g_haptic_preferences_loaded) {
    return;
  }

  app::HapticPreferences preferences;
  if (app::LoadHapticPreferencesFromNvs(&preferences)) {
    g_haptic_enabled = preferences.enabled;
    g_haptic_strength_percent = preferences.strength_percent;
  }
  g_haptic_preferences_loaded = true;
}

}  // namespace

/**
 * @brief 注册 UI 交互振动反馈提供者
 * @param haptic 振动反馈提供者指针
 */
void RegisterUiHapticProvider(hal::HapticProvider* haptic) {
  g_haptic_provider = haptic;
}

/**
 * @brief 设置 UI 交互振动反馈偏好
 * @param enabled true 表示启用振动反馈，false 表示关闭振动反馈
 * @param strength_percent 振动强度百分比，范围 0~100
 */
void SetUiHapticPreferences(bool enabled, int strength_percent) {
  g_haptic_enabled = enabled;
  g_haptic_strength_percent = std::clamp(strength_percent, 0, 100);
  g_haptic_preferences_loaded = true;
}

/**
 * @brief 播放一次 UI 交互振动反馈
 */
void PlayUiHapticFeedback() {
  if (g_haptic_provider == nullptr) {
    return;
  }

  EnsureHapticPreferencesLoaded();
  if (!g_haptic_enabled) {
    return;
  }

  const uint8_t gain = static_cast<uint8_t>(
      g_haptic_strength_percent * UINT8_MAX / 100);
  g_haptic_provider->PlayHapticWaveform(1, 1, gain, true);
}

}  // namespace lilygo_box::ui
