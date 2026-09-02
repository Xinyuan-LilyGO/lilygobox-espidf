/*
 * @Description: UI haptic feedback helpers
 * @Author: LILYGO_L
 * @Date: 2026-06-30 00:00:00
 * @LastEditTime: 2026-09-02 17:54:10
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
int g_haptic_strength_percent = 90;

/**
 * @brief 确保已从长期 RAM 缓存加载 UI 交互振动反馈偏好
 */
void EnsureHapticPreferencesLoaded() {
  if (g_haptic_preferences_loaded) {
    return;
  }

  app::HapticPreferences preferences = app::GetHapticPreferences();
  g_haptic_enabled = preferences.enabled;
  g_haptic_strength_percent = preferences.strength_percent;
  g_haptic_preferences_loaded = true;
}

}  // namespace

void RegisterUiHapticProvider(hal::HapticProvider* haptic) {
  g_haptic_provider = haptic;
}

void SetUiHapticPreferences(bool enabled, int strength_percent) {
  g_haptic_enabled = enabled;
  g_haptic_strength_percent = std::clamp(strength_percent, 0, 100);
  g_haptic_preferences_loaded = true;
}

void PlayUiHapticFeedback() {
  if (g_haptic_provider == nullptr) {
    return;
  }

  EnsureHapticPreferencesLoaded();
  if (!g_haptic_enabled) {
    return;
  }

  const uint8_t gain =
      static_cast<uint8_t>(g_haptic_strength_percent * UINT8_MAX / 100);
  g_haptic_provider->PlayHapticWaveform(1, 1, gain, true);
}

}  // namespace lilygo_box::ui
