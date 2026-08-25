#pragma once

#include <cstddef>

#include "lvgl.h"

namespace lilygo_box::ui {

inline constexpr size_t kWallpaperLayerCount = 4;

struct WallpaperObjects {
  lv_obj_t* background = nullptr;
  lv_obj_t* layers[kWallpaperLayerCount] = {};
};

/**
 * @brief 创建通用壁纸圆形对象
 * @param parent 父对象
 * @param width 壁纸宽度，<= 0 时使用 parent 当前宽度
 * @param height 壁纸高度，<= 0 时使用 parent 当前高度
 * @param objects 可选返回四层壁纸对象
 */
void CreateWallpaperObjects(lv_obj_t* parent, int width = 0, int height = 0,
    WallpaperObjects* objects = nullptr);

/**
 * @brief 将已有壁纸对象刷新为当前主题颜色
 * @param objects 壁纸对象集合
 */
void ApplyWallpaperTheme(const WallpaperObjects& objects);

}  // namespace lilygo_box::ui
