/*
 * @Description: None
 * @Author: LILYGO_L
 * @Date: 2026-05-15 10:18:00
 * @LastEditTime: 2026-05-15 10:18:00
 * @License: GPL 3.0
 */
#pragma once

#include "lvgl.h"

namespace lilygo_box::ui {

/**
 * @brief 判断当前触摸点是否在对象区域内
 * @param object LVGL 对象
 * @return 在对象区域内返回 true，否则返回 false
 * @Date 2026-05-15 10:18:00
 */
bool IsPointerInsideObject(lv_obj_t* object);

/**
 * @brief 添加按下后移出区域取消点击的行为
 * @param object LVGL 对象
 * @return 添加成功返回 true，否则返回 false
 * @Date 2026-05-15 10:18:00
 */
bool AddPressCancelOnLeave(lv_obj_t* object);

}  // namespace lilygo_box::ui
