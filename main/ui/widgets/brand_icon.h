#pragma once

#include "lvgl.h"

namespace lilygo_box::ui {

/**
 * @brief 创建无背景的 LilygoBox 品牌图标，前景色跟随当前主题
 * @param parent 父对象
 * @param size 图标布局尺寸
 * @return 创建成功返回图标对象，否则返回 nullptr
 */
lv_obj_t* CreateLilygoBoxBrandIcon(lv_obj_t* parent, int size);

}  // namespace lilygo_box::ui
