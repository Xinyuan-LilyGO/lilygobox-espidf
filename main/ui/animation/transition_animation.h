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

// 窗口切换动画模式
enum class WindowTransitionMode {
  kFade,
  kSlideLeft,
  kSlideRight,
};

/**
 * @brief 启动窗口渐变切换动画
 * @param object LVGL 对象
 * @param start_opacity 起始透明度
 * @param end_opacity 结束透明度
 * @param duration_ms 动画时长
 * @param user_data 用户数据
 * @param completed_callback 完成回调
 * @return 启动成功返回 true，否则返回 false
 */
bool StartFadeWindowTransition(lv_obj_t* object, int32_t start_opacity,
    int32_t end_opacity, uint32_t duration_ms, void* user_data,
    lv_anim_completed_cb_t completed_callback);

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
 * @brief 启动背景透明度切换动画
 * @param object LVGL 对象
 * @param start_opacity 起始透明度
 * @param end_opacity 结束透明度
 * @param duration_ms 动画时长
 * @param user_data 用户数据
 * @param completed_callback 完成回调
 * @return 启动成功返回 true，否则返回 false
 */
bool StartBackgroundOpacityTransition(lv_obj_t* object, int32_t start_opacity,
    int32_t end_opacity, uint32_t duration_ms, void* user_data,
    lv_anim_completed_cb_t completed_callback);

/**
 * @brief 删除指定对象上的窗口切换动画
 * @param object LVGL 对象
 * @param mode 窗口切换动画模式
 */
void DeleteWindowTransition(lv_obj_t* object, WindowTransitionMode mode);

/**
 * @brief 删除指定对象上的背景透明度切换动画
 * @param object LVGL 对象
 */
void DeleteBackgroundOpacityTransition(lv_obj_t* object);

}  // namespace lilygo_box::ui
