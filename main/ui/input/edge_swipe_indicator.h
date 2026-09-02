/*
 * @Description: 全局边缘滑动指示器接口
 * @Author: LILYGO_L
 * @Date: 2026-08-24 00:00:00
 * @LastEditTime: 2026-09-02 17:54:23
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
void InitializeEdgeSwipeIndicator(
    hal::LvglPort* lvgl_port, EdgeSwipeBackCallback back_callback);

/**
 * @brief 设置是否使用不拦截 LVGL 控件输入的边缘手势模式
 * @param enabled true 透传控件输入，false 拦截边缘手势输入
 */
void SetEdgeSwipePassthroughMode(bool enabled);

}  // namespace lilygo_box::ui
