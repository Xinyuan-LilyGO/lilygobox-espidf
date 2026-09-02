#include "ui/wallpaper.h"

#include <algorithm>
#include <cstddef>

#include "ui/theme/theme_provider.h"

namespace lilygo_box::ui {
namespace {

/**
 * @brief 创建单个壁纸圆形对象
 * @param parent 父对象
 * @param size 圆形直径
 * @param x X 轴偏移
 * @param y Y 轴偏移
 * @param align 对齐方式
 * @param color 填充颜色
 * @param opacity 填充透明度
 * @return 创建成功返回对象指针，否则返回 nullptr
 */
lv_obj_t* CreateWallpaperCircle(lv_obj_t* parent, int size, int x, int y,
    lv_align_t align, uint32_t color, lv_opa_t opacity) {
  lv_obj_t* circle = lv_obj_create(parent);
  if (circle == nullptr) {
    return nullptr;
  }

  lv_obj_remove_flag(circle, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(circle, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_flag(circle, LV_OBJ_FLAG_GESTURE_BUBBLE);
  lv_obj_set_size(circle, size, size);
  lv_obj_set_style_radius(circle, LV_RADIUS_CIRCLE, LV_PART_MAIN);
  lv_obj_set_style_bg_color(circle, lv_color_hex(color), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(circle, opacity, LV_PART_MAIN);
  lv_obj_set_style_border_width(circle, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(circle, 0, LV_PART_MAIN);
  lv_obj_align(circle, align, x, y);
  return circle;
}

}  // namespace

void CreateWallpaperObjects(
    lv_obj_t* parent, int width, int height, WallpaperObjects* objects) {
  if (parent == nullptr) {
    return;
  }

  WallpaperObjects created_objects;
  created_objects.background = parent;

  constexpr int kBasePortraitWidth = 540;
  constexpr int kBasePortraitHeight = 960;
  const int raw_width = std::max(
      width > 0 ? width : static_cast<int>(lv_obj_get_width(parent)), 1);
  const int raw_height = std::max(
      height > 0 ? height : static_cast<int>(lv_obj_get_height(parent)), 1);
  const int wallpaper_width = std::min(raw_width, raw_height);
  const int wallpaper_height = std::max(raw_width, raw_height);
  const int size_scale = std::min(wallpaper_width * 100 / kBasePortraitWidth,
      wallpaper_height * 100 / kBasePortraitHeight);
  const int y_scale = wallpaper_height * 100 / kBasePortraitHeight;
  const auto scale_size = [size_scale](int value) {
    return std::max(1, value * size_scale / 100);
  };
  const auto scale_y = [y_scale](int value) { return value * y_scale / 100; };
  lv_display_t* display = lv_display_get_default();
  const lv_display_rotation_t rotation = display != nullptr
                                             ? lv_display_get_rotation(display)
                                             : LV_DISPLAY_ROTATION_0;
  const bool is_landscape = raw_width > raw_height ||
                            rotation == LV_DISPLAY_ROTATION_90 ||
                            rotation == LV_DISPLAY_ROTATION_270;
  const theme::ThemeColors& colors = theme::ActiveThemeColors();

  if (is_landscape) {
    const auto landscape_size = [scale_size](int value) {
      return scale_size(value) * 120 / 100;
    };
    created_objects.layers[0] = CreateWallpaperCircle(parent,
        landscape_size(1120), 0, raw_height * 3 / 100, LV_ALIGN_TOP_MID,
        colors.wallpaper_layer_1, LV_OPA_COVER);
    created_objects.layers[1] = CreateWallpaperCircle(parent,
        landscape_size(1000), 0, raw_height * 13 / 100, LV_ALIGN_TOP_MID,
        colors.wallpaper_layer_2, LV_OPA_COVER);
    created_objects.layers[2] = CreateWallpaperCircle(parent,
        landscape_size(940), 0, raw_height * 36 / 100, LV_ALIGN_TOP_MID,
        colors.wallpaper_layer_3, LV_OPA_COVER);
    created_objects.layers[3] = CreateWallpaperCircle(parent,
        landscape_size(1040), 0, raw_height * 70 / 100, LV_ALIGN_TOP_MID,
        colors.wallpaper_layer_4, LV_OPA_COVER);
    if (objects != nullptr) {
      *objects = created_objects;
    }
    ApplyWallpaperTheme(created_objects);
    return;
  }

  created_objects.layers[0] = CreateWallpaperCircle(parent, scale_size(1120), 0,
      scale_y(70), LV_ALIGN_TOP_MID, colors.wallpaper_layer_1, LV_OPA_COVER);
  created_objects.layers[1] = CreateWallpaperCircle(parent, scale_size(1000), 0,
      scale_y(140), LV_ALIGN_TOP_MID, colors.wallpaper_layer_2, LV_OPA_COVER);
  created_objects.layers[2] = CreateWallpaperCircle(parent, scale_size(940), 0,
      scale_y(300), LV_ALIGN_TOP_MID, colors.wallpaper_layer_3, LV_OPA_COVER);
  created_objects.layers[3] =
      CreateWallpaperCircle(parent, scale_size(1040), 0, scale_y(640),
          LV_ALIGN_BOTTOM_MID, colors.wallpaper_layer_4, LV_OPA_COVER);
  if (objects != nullptr) {
    *objects = created_objects;
  }
  ApplyWallpaperTheme(created_objects);
}

void ApplyWallpaperTheme(const WallpaperObjects& objects) {
  const theme::ThemeColors& colors = theme::ActiveThemeColors();
  if (objects.background != nullptr) {
    lv_obj_set_style_bg_color(objects.background,
        lv_color_hex(colors.wallpaper_background), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(objects.background, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_invalidate(objects.background);
  }
  const uint32_t layer_colors[kWallpaperLayerCount] = {
      colors.wallpaper_layer_1,
      colors.wallpaper_layer_2,
      colors.wallpaper_layer_3,
      colors.wallpaper_layer_4,
  };
  for (size_t index = 0; index < kWallpaperLayerCount; ++index) {
    if (objects.layers[index] != nullptr) {
      lv_obj_set_style_bg_color(objects.layers[index],
          lv_color_hex(layer_colors[index]), LV_PART_MAIN);
      lv_obj_invalidate(objects.layers[index]);
    }
  }
}

}  // namespace lilygo_box::ui
