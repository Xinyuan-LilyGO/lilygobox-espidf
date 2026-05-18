/*
 * @Description: None
 * @Author: LILYGO_L
 * @Date: 2026-05-16 18:20:00
 * @LastEditTime: 2026-05-16 18:35:00
 * @License: GPL 3.0
 */
#pragma once

#include <cstdint>

#include "lvgl.h"

namespace lilygo_box::ui {

/**
 * @brief 启动窗口向左切换动画
 * @param object LVGL 对象
 * @param distance 切换距离
 * @param duration_ms 动画时长
 * @param user_data 用户数据
 * @param completed_callback 完成回调
 * @return 启动成功返回 true，否则返回 false
 */
bool StartSlideLeftWindowTransition(lv_obj_t* object, int32_t distance,
    uint32_t duration_ms, void* user_data,
    lv_anim_completed_cb_t completed_callback);

/**
 * @brief 启动窗口向右切换动画
 * @param object LVGL 对象
 * @param distance 切换距离
 * @param duration_ms 动画时长
 * @param user_data 用户数据
 * @param completed_callback 完成回调
 * @return 启动成功返回 true，否则返回 false
 */
bool StartSlideRightWindowTransition(lv_obj_t* object, int32_t distance,
    uint32_t duration_ms, void* user_data,
    lv_anim_completed_cb_t completed_callback);

/**
 * @brief 删除指定对象上的窗口切换动画
 * @param object LVGL 对象
 */
void DeleteWindowTransition(lv_obj_t* object);

}  // namespace lilygo_box::ui
