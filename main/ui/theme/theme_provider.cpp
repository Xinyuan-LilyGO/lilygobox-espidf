/*
 * @Description: UI theme provider
 * @Author: LILYGO_L
 * @Date: 2026-06-25 00:00:00
 * @LastEditTime: 2026-06-25 00:00:00
 * @License: GPL 3.0
 */
#include "ui/theme/theme_provider.h"

namespace lilygo_box::ui::theme {
namespace {

/**
 * @brief 获取指定主题模式对应的颜色配置
 * @param mode 主题模式
 * @return 主题颜色配置
 */
constexpr ThemeColors ResolveThemeColors(ThemeMode mode) {
  switch (mode) {
    case ThemeMode::kLightNeutral:
      return kLightNeutralTheme;
  }
  return kLightNeutralTheme;
}

}  // namespace

ThemeMode ThemeProvider::mode() const { return mode_; }

const ThemeColors& ThemeProvider::colors() const { return colors_; }

void ThemeProvider::SetMode(ThemeMode mode) {
  if (mode_ == mode) {
    return;
  }
  mode_ = mode;
  colors_ = ResolveThemeColors(mode);
}

const ThemeColors& GetThemeColors(const ThemeProvider* provider) {
  if (provider == nullptr) {
    return LightNeutralTheme();
  }
  return provider->colors();
}

}  // namespace lilygo_box::ui::theme
