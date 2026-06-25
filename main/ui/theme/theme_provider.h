/*
 * @Description: UI theme provider
 * @Author: LILYGO_L
 * @Date: 2026-06-25 00:00:00
 * @LastEditTime: 2026-06-25 00:00:00
 * @License: GPL 3.0
 */
#pragma once

#include <cstdint>

namespace lilygo_box::ui::theme {

enum class ThemeMode {
  kLightNeutral,
};

struct ThemeColors {
  uint32_t surface = 0xFFFFFF;
  uint32_t surface_dim = 0xE6E6E6;
  uint32_t surface_container_lowest = 0xFFFFFF;
  uint32_t surface_container_low = 0xF7F7F7;
  uint32_t surface_container = 0xF2F2F2;
  uint32_t surface_container_high = 0xE8E8E8;
  uint32_t surface_container_highest = 0xDDDDDD;
  uint32_t on_surface = 0x101010;
  uint32_t on_surface_variant = 0x5F5F5F;
  uint32_t outline = 0x8F8F8F;
  uint32_t outline_variant = 0xE1E1E1;
  uint32_t primary = 0x202020;
  uint32_t on_primary = 0xFFFFFF;
  uint32_t primary_container = 0xE6E6E6;
  uint32_t on_primary_container = 0x101010;
  uint32_t secondary_container = 0xF2F2F2;
  uint32_t on_secondary_container = 0x101010;
  uint32_t disabled_container = 0xD7D7D7;
  uint32_t state_layer = 0xEBEBEB;
  uint32_t state_layer_strong = 0xE0E0E0;
  uint32_t action = 0x3F82F6;
  uint32_t action_pressed = 0x2F73E8;
  uint32_t action_disabled = 0xBFD7FB;
  uint32_t action_container = 0xE8F3FF;
  uint32_t action_container_pressed = 0xD8E9FF;
  uint32_t on_action = 0xFFFFFF;
  uint32_t button_secondary = 0xE8E8E8;
  uint32_t button_secondary_pressed = 0xDCDCDC;
  uint32_t on_button_secondary = 0x101010;
};

inline constexpr ThemeColors kLightNeutralTheme = {
    .surface = 0xFFFFFF,
    .surface_dim = 0xE6E6E6,
    .surface_container_lowest = 0xFFFFFF,
    .surface_container_low = 0xF7F7F7,
    .surface_container = 0xF2F2F2,
    .surface_container_high = 0xE8E8E8,
    .surface_container_highest = 0xDDDDDD,
    .on_surface = 0x101010,
    .on_surface_variant = 0x5F5F5F,
    .outline = 0x8F8F8F,
    .outline_variant = 0xE1E1E1,
    .primary = 0x202020,
    .on_primary = 0xFFFFFF,
    .primary_container = 0xE6E6E6,
    .on_primary_container = 0x101010,
    .secondary_container = 0xF2F2F2,
    .on_secondary_container = 0x101010,
    .disabled_container = 0xD7D7D7,
    .state_layer = 0xEBEBEB,
    .state_layer_strong = 0xE0E0E0,
    .action = 0x3F82F6,
    .action_pressed = 0x2F73E8,
    .action_disabled = 0xBFD7FB,
    .action_container = 0xE8F3FF,
    .action_container_pressed = 0xD8E9FF,
    .on_action = 0xFFFFFF,
    .button_secondary = 0xE8E8E8,
    .button_secondary_pressed = 0xDCDCDC,
    .on_button_secondary = 0x101010,
};

/**
 * @brief 获取默认亮色灰阶主题颜色
 * @return 主题颜色配置
 */
constexpr const ThemeColors& LightNeutralTheme() { return kLightNeutralTheme; }

class ThemeProvider final {
 public:
  ThemeProvider() = default;

  /**
   * @brief 获取当前主题模式
   * @return 主题模式
   */
  ThemeMode mode() const;

  /**
   * @brief 获取当前主题颜色
   * @return 主题颜色配置
   */
  const ThemeColors& colors() const;

  /**
   * @brief 设置当前主题模式
   * @param mode 主题模式
   */
  void SetMode(ThemeMode mode);

 private:
  ThemeMode mode_ = ThemeMode::kLightNeutral;
  ThemeColors colors_ = kLightNeutralTheme;
};

/**
 * @brief 获取主题提供器中的颜色配置
 * @param provider 主题提供器指针，为空时使用默认亮色主题
 * @return 主题颜色配置
 */
const ThemeColors& GetThemeColors(const ThemeProvider* provider);

}  // namespace lilygo_box::ui::theme
