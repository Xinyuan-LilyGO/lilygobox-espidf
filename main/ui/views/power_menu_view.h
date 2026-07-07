/*
 * @Description: Power menu overlay
 * @Author: LILYGO_L
 * @Date: 2026-07-07
 * @License: GPL 3.0
 */
#pragma once

#include <functional>

#include "lvgl.h"

namespace lilygo_box::ui {

struct PowerMenuViewOptions {
  int screen_width = 0;
  int screen_height = 0;
  std::function<void()> dismiss_callback;
};

/**
 * @brief 创建长按锁屏键后显示的关机菜单覆盖层
 * @param parent 父对象
 * @param options 关机菜单创建参数
 * @return 创建成功返回关机菜单对象，失败返回 nullptr
 */
lv_obj_t* CreatePowerMenuView(lv_obj_t* parent,
    const PowerMenuViewOptions& options);

}  // namespace lilygo_box::ui
