/*
 * @Description: UI 分层返回目标管理接口
 * @Author: LILYGO_L
 * @Date: 2026-08-24 00:00:00
 * @LastEditTime: 2026-08-24 00:00:00
 * @License: GPL 3.0
 */
#pragma once

#include <functional>

#include "lvgl.h"

namespace lilygo_box::ui {

using BackNavigationCallback = std::function<void()>;
using ConditionalBackNavigationCallback = std::function<bool()>;

/**
 * @brief 注册与页面对象生命周期绑定的返回处理器
 * @param owner 返回目标所属的页面对象
 * @param callback 返回操作
 * @return 注册成功返回 true，否则返回 false
 */
bool RegisterBackNavigationHandler(
    lv_obj_t* owner, BackNavigationCallback callback);

/**
 * @brief 注册可决定是否处理本次返回请求的页面处理器
 * @param owner 返回目标所属的页面对象
 * @param callback 返回操作；处理成功时返回 true
 * @return 注册成功返回 true，否则返回 false
 */
bool RegisterConditionalBackNavigationHandler(
    lv_obj_t* owner, ConditionalBackNavigationCallback callback);

/**
 * @brief 执行当前最上层可见页面的返回操作
 * @return 找到并执行返回目标时返回 true，否则返回 false
 */
bool RequestBackNavigation();

}  // namespace lilygo_box::ui
