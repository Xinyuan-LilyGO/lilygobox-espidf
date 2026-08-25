/*
 * @Description: 全局边缘滑动指示器接口
 * @Author: LILYGO_L
 * @Date: 2026-08-24 00:00:00
 * @LastEditTime: 2026-08-24 00:00:00
 * @License: GPL 3.0
 */
#pragma once

#include <functional>

namespace lilygo_box::hal {
class LvglPort;
}  // namespace lilygo_box::hal

namespace lilygo_box::ui {

using EdgeSwipeBackCallback = std::function<void()>;

/**
 * @brief 初始化全局边缘滑动指示器
 * @param lvgl_port LVGL 输入端口
 * @param back_callback 手势完成后的统一返回回调
 */
void InitializeEdgeSwipeIndicator(hal::LvglPort* lvgl_port,
    EdgeSwipeBackCallback back_callback);

}  // namespace lilygo_box::ui
