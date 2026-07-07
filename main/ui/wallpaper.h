#pragma once

#include "lvgl.h"

namespace lilygo_box::ui {

/**
 * @brief 创建通用壁纸圆形对象
 * @param parent 父对象
 * @param width 壁纸宽度，<= 0 时使用 parent 当前宽度
 * @param height 壁纸高度，<= 0 时使用 parent 当前高度
 */
void CreateWallpaperObjects(lv_obj_t* parent, int width = 0, int height = 0);

}  // namespace lilygo_box::ui
