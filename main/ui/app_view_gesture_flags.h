/*
 * @Description: Shared flags for app view gesture coordination
 * @Author: LILYGO_L
 * @Date: 2026-05-10 13:27:05
 * @LastEditTime: 2026-05-12 01:08:42
 * @License: GPL 3.0
 */
#pragma once

#include "lvgl.h"

namespace lilygo_box::ui {

inline constexpr lv_obj_flag_t kSuppressNextLauncherGestureFlag =
    LV_OBJ_FLAG_USER_1;
inline constexpr lv_obj_flag_t kBlockLauncherGestureFlag =
    LV_OBJ_FLAG_USER_2;

}  // namespace lilygo_box::ui
