/*
 * @Description: UI theme provider
 * @Author: LILYGO_L
 * @Date: 2026-06-25 00:00:00
 * @LastEditTime: 2026-08-25 00:00:00
 * @License: GPL 3.0
 */
#pragma once

#include <cstdint>

namespace lilygo_box::ui::theme {

enum class ThemeMode : uint8_t {
  kLight,
  kDark,
};

// 全局界面只能使用这些语义色；品牌色和媒体内容色可以由业务保留。
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
  uint32_t disabled_content = 0x8F8F8F;
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
  uint32_t error = 0xBA1A1A;
  uint32_t on_error = 0xFFFFFF;
  uint32_t error_container = 0xFFDAD6;
  uint32_t on_error_container = 0x410002;
  uint32_t warning = 0x8A4F00;
  uint32_t warning_pressed = 0x704000;
  uint32_t on_warning = 0xFFFFFF;
  uint32_t success = 0x138A3D;
  uint32_t on_success = 0xFFFFFF;
  uint32_t keyboard_background = 0xE7E7E7;
  uint32_t keyboard_key = 0xFFFFFF;
  uint32_t keyboard_key_pressed = 0xD4D4D4;
  uint32_t keyboard_special_key = 0xC9CDD4;
  uint32_t on_keyboard_key = 0x202020;
  uint32_t wallpaper_background = 0xE6E6E6;
  uint32_t wallpaper_layer_1 = 0xDCDCDC;
  uint32_t wallpaper_layer_2 = 0xC8C8C8;
  uint32_t wallpaper_layer_3 = 0xB7B7B7;
  uint32_t wallpaper_layer_4 = 0x9F9F9F;
  uint32_t scrim = 0x000000;
};

inline constexpr ThemeColors kLightTheme = {};

inline constexpr ThemeColors kDarkTheme = {
    .surface = 0x121212,
    .surface_dim = 0x0F0F0F,
    .surface_container_lowest = 0x101010,
    .surface_container_low = 0x181818,
    .surface_container = 0x1E1E1E,
    .surface_container_high = 0x272727,
    .surface_container_highest = 0x303030,
    .on_surface = 0xF1F1F1,
    .on_surface_variant = 0xB8B8B8,
    .outline = 0x8C8C8C,
    .outline_variant = 0x3F3F3F,
    .primary = 0xF1F1F1,
    .on_primary = 0x151515,
    .primary_container = 0x353535,
    .on_primary_container = 0xF1F1F1,
    .secondary_container = 0x292929,
    .on_secondary_container = 0xF1F1F1,
    .disabled_container = 0x3A3A3A,
    .disabled_content = 0x777777,
    .state_layer = 0x292929,
    .state_layer_strong = 0x363636,
    .action = 0x8AB4F8,
    .action_pressed = 0xA8C7FA,
    .action_disabled = 0x455A73,
    .action_container = 0x203A5C,
    .action_container_pressed = 0x2A4A73,
    .on_action = 0x10233D,
    .button_secondary = 0x303030,
    .button_secondary_pressed = 0x3C3C3C,
    .on_button_secondary = 0xF1F1F1,
    .error = 0xFFB4AB,
    .on_error = 0x690005,
    .error_container = 0x93000A,
    .on_error_container = 0xFFDAD6,
    .warning = 0xFFB95C,
    .warning_pressed = 0xFFCA7A,
    .on_warning = 0x452B00,
    .success = 0x6DD58C,
    .on_success = 0x003917,
    .keyboard_background = 0x181818,
    .keyboard_key = 0x303030,
    .keyboard_key_pressed = 0x484848,
    .keyboard_special_key = 0x3A3F46,
    .on_keyboard_key = 0xF1F1F1,
    .wallpaper_background = 0x383838,
    .wallpaper_layer_1 = 0x303030,
    .wallpaper_layer_2 = 0x292929,
    .wallpaper_layer_3 = 0x232323,
    .wallpaper_layer_4 = 0x1C1C1C,
    .scrim = 0x000000,
};

/**
 * @brief 获取亮色主题颜色
 * @return 亮色主题颜色配置
 */
constexpr const ThemeColors& LightTheme() { return kLightTheme; }

/**
 * @brief 获取暗色主题颜色
 * @return 暗色主题颜色配置
 */
constexpr const ThemeColors& DarkTheme() { return kDarkTheme; }

class ThemeProvider final {
 public:
  ThemeProvider() = default;

  /**
   * @brief 获取当前主题模式
   * @return 主题模式
   */
  ThemeMode mode() const;

  /**
   * @brief 判断当前是否为暗色主题
   * @return 暗色主题返回 true
   */
  bool is_dark() const;

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
  ThemeMode mode_ = ThemeMode::kLight;
  const ThemeColors* colors_ = &kLightTheme;
};

/**
 * @brief 获取主题提供器中的颜色配置
 * @param provider 主题提供器指针，为空时使用当前全局主题
 * @return 主题颜色配置
 */
const ThemeColors& GetThemeColors(const ThemeProvider* provider);

/**
 * @brief 获取当前全局主题颜色
 * @return 当前全局主题颜色配置
 */
const ThemeColors& ActiveThemeColors();

}  // namespace lilygo_box::ui::theme
