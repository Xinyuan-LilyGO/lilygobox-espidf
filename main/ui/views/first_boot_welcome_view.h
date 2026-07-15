/*
 * @Description: Material 风格首次开机欢迎页接口
 * @Author: LILYGO_L
 * @Date: 2026-07-15 00:00:00
 * @LastEditTime: 2026-07-15 00:00:00
 * @License: GPL 3.0
 */
#pragma once

#include <functional>

#include "lvgl.h"
#include "ui/theme/theme_provider.h"

namespace lilygo_box::ui {

struct FirstBootWelcomeViewOptions {
  int screen_width = 0;
  int screen_height = 0;
  const theme::ThemeColors* colors = nullptr;
  std::function<bool()> completion_callback;
};

/**
 * @brief 创建全屏首次开机欢迎页
 * @param parent LVGL 父对象
 * @param options 页面尺寸、主题和完成回调配置
 * @return 创建成功返回页面对象，失败返回 nullptr
 */
lv_obj_t* CreateFirstBootWelcomeView(
    lv_obj_t* parent, const FirstBootWelcomeViewOptions& options);

}  // namespace lilygo_box::ui
