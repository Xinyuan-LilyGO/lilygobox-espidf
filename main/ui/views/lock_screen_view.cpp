/*
 * @Description: None
 * @Author: LILYGO_L
 * @Date: 2026-05-10 13:27:05
 * @LastEditTime: 2026-06-25 10:18:00
 * @License: GPL 3.0
 */
#include "ui/views/lock_screen_view.h"

#include <algorithm>

#include "ui/font/font_assets.h"
#include "ui/font/material_symbols_assets.h"

namespace lilygo_box::ui {
namespace {

constexpr int kLockClockTop = 90;
constexpr int kLockHorizontalPadding = 10;
constexpr int kUnlockHintBottom = 46;
constexpr uint32_t kLockResetAnimationMs = 180;
constexpr uint32_t kLockUnlockAnimationMs = 220;
constexpr uint32_t kLockTextColor = 0xFFFFFF;

/**
 * @brief 获取锁屏时间字体
 * @return 字体指针
 */
const lv_font_t* HomeTimeFont() { return &lvgl_font_lineseedkr_rg_120; }

/**
 * @brief 获取锁屏日期字体
 * @return 字体指针
 */
const lv_font_t* HomeDateFont() { return &lvgl_font_lineseedkr_th_60; }

/**
 * @brief 获取 56 号 Material Symbols 字体
 * @return 字体指针
 */
const lv_font_t* MaterialIconFont56() { return &lvgl_font_material_symbols_56; }

/**
 * @brief 设置文本对象的颜色和字体
 * @param object LVGL 对象
 * @param color 文本颜色
 * @param font 文本字体
 */
void SetTextStyle(lv_obj_t* object, lv_color_t color, const lv_font_t* font) {
  lv_obj_set_style_text_color(object, color, LV_PART_MAIN);
  lv_obj_set_style_text_font(object, font, LV_PART_MAIN);
}

/**
 * @brief 清除对象背景、边框和内边距
 * @param object LVGL 对象
 */
void MakeTransparent(lv_obj_t* object) {
  lv_obj_set_style_bg_opa(object, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_set_style_border_width(object, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(object, 0, LV_PART_MAIN);
}

/**
 * @brief 创建指定字体文本标签
 * @param parent 父对象
 * @param text 显示文本
 * @param color 文本颜色
 * @param font 文本字体
 * @return 创建成功返回对象指针，否则返回 nullptr
 */
lv_obj_t* CreateLabel(lv_obj_t* parent, const char* text, lv_color_t color,
    const lv_font_t* font) {
  lv_obj_t* label = lv_label_create(parent);
  if (label == nullptr) {
    return nullptr;
  }
  lv_label_set_text(label, text);
  SetTextStyle(label, color, font);
  return label;
}

/**
 * @brief 创建壁纸圆形对象
 * @param parent 父对象
 * @param size 圆形尺寸
 * @param x X 偏移
 * @param y Y 偏移
 * @param align 对齐方式
 * @param color 填充颜色
 * @param opacity 透明度
 * @return 创建成功返回对象指针，否则返回 nullptr
 */
lv_obj_t* CreateCircle(lv_obj_t* parent, int size, int x, int y,
    lv_align_t align, uint32_t color, lv_opa_t opacity) {
  lv_obj_t* circle = lv_obj_create(parent);
  if (circle == nullptr) {
    return nullptr;
  }
  lv_obj_remove_flag(circle, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(circle, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_size(circle, size, size);
  lv_obj_set_style_radius(circle, LV_RADIUS_CIRCLE, LV_PART_MAIN);
  lv_obj_set_style_bg_color(circle, lv_color_hex(color), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(circle, opacity, LV_PART_MAIN);
  lv_obj_set_style_border_width(circle, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(circle, 0, LV_PART_MAIN);
  lv_obj_align(circle, align, x, y);
  return circle;
}

/**
 * @brief 设置对象 Y 坐标动画回调
 * @param object LVGL 对象
 * @param value Y 坐标
 */
void SetObjectY(void* object, int32_t value) {
  lv_obj_set_y(static_cast<lv_obj_t*>(object), value);
}

/**
 * @brief 创建锁屏壁纸对象
 * @param parent 父对象
 */
void CreateWallpaperObjects(lv_obj_t* parent) {
  CreateCircle(parent, 1120, 0, 70, LV_ALIGN_TOP_MID, 0xDCDCDC, LV_OPA_COVER);
  CreateCircle(parent, 1000, 0, 140, LV_ALIGN_TOP_MID, 0xC8C8C8, LV_OPA_COVER);
  CreateCircle(parent, 940, 0, 300, LV_ALIGN_TOP_MID, 0xB7B7B7, LV_OPA_COVER);
  CreateCircle(parent, 1040, 0, 640, LV_ALIGN_BOTTOM_MID, 0x9F9F9F,
      LV_OPA_COVER);
}

/**
 * @brief 创建锁屏时间日期区域
 * @param parent 父对象
 * @param options 锁屏视图配置
 * @return 创建成功返回对象指针，否则返回 nullptr
 */
lv_obj_t* CreateClockGroup(
    lv_obj_t* parent, const LockScreenViewOptions& options) {
  lv_obj_t* group = lv_obj_create(parent);
  if (group == nullptr) {
    return nullptr;
  }
  lv_obj_remove_flag(group, LV_OBJ_FLAG_SCROLLABLE);
  MakeTransparent(group);
  lv_obj_set_size(group, options.screen_width - 2 * kLockHorizontalPadding, 282);
  lv_obj_align(group, LV_ALIGN_TOP_LEFT, kLockHorizontalPadding, kLockClockTop);

  lv_obj_t* time_label =
      CreateLabel(group, options.time_text, lv_color_hex(kLockTextColor),
          HomeTimeFont());
  if (time_label == nullptr) {
    lv_obj_delete(group);
    return nullptr;
  }
  lv_obj_set_size(time_label, 400, 110);
  lv_obj_set_style_text_opa(time_label, 245, LV_PART_MAIN);
  lv_obj_align(time_label, LV_ALIGN_TOP_LEFT, 0, 0);

  lv_obj_t* date_label =
      CreateLabel(group, options.date_text, lv_color_hex(kLockTextColor),
          HomeDateFont());
  if (date_label == nullptr) {
    lv_obj_delete(group);
    return nullptr;
  }
  lv_obj_set_size(date_label, 400, 70);
  lv_obj_set_style_text_opa(date_label, 220, LV_PART_MAIN);
  lv_obj_align(date_label, LV_ALIGN_TOP_LEFT, 10, 110);

  lv_obj_t* week_label =
      CreateLabel(group, options.week_text, lv_color_hex(kLockTextColor),
          HomeDateFont());
  if (week_label == nullptr) {
    lv_obj_delete(group);
    return nullptr;
  }
  lv_obj_set_size(week_label, 400, 50);
  lv_obj_set_style_text_opa(week_label, 220, LV_PART_MAIN);
  lv_obj_align(week_label, LV_ALIGN_TOP_LEFT, 10, 172);
  return group;
}

/**
 * @brief 获取锁屏时间日期区域
 * @param lock_screen 锁屏页面对象
 * @return 找到返回对象指针，否则返回 nullptr
 */
lv_obj_t* LockClockGroup(lv_obj_t* lock_screen) {
  if (lock_screen == nullptr || lv_obj_get_child_count(lock_screen) < 2) {
    return nullptr;
  }
  return lv_obj_get_child(lock_screen, 1);
}

}  // namespace

/**
 * @brief 创建锁屏覆盖页面
 * @param parent 父对象
 * @param options 锁屏视图配置
 * @return 创建成功返回对象指针，否则返回 nullptr
 */
lv_obj_t* CreateLockScreenView(lv_obj_t* parent,
    const LockScreenViewOptions& options) {
  if (parent == nullptr) {
    return nullptr;
  }

  lv_obj_t* lock_screen = lv_obj_create(parent);
  if (lock_screen == nullptr) {
    return nullptr;
  }
  lv_obj_remove_flag(lock_screen, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(lock_screen, options.screen_width, options.screen_height);
  lv_obj_align(lock_screen, LV_ALIGN_CENTER, 0, 0);
  lv_obj_set_style_bg_color(lock_screen, lv_color_hex(0xE2E2E2), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(lock_screen, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_border_width(lock_screen, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(lock_screen, 0, LV_PART_MAIN);

  lv_obj_t* wallpaper = lv_obj_create(lock_screen);
  if (wallpaper == nullptr) {
    lv_obj_delete(lock_screen);
    return nullptr;
  }
  lv_obj_remove_flag(wallpaper, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(wallpaper, LV_OBJ_FLAG_CLICKABLE);
  MakeTransparent(wallpaper);
  lv_obj_set_size(wallpaper, options.screen_width, options.screen_height);
  lv_obj_center(wallpaper);
  CreateWallpaperObjects(wallpaper);

  if (CreateClockGroup(lock_screen, options) == nullptr) {
    lv_obj_delete(lock_screen);
    return nullptr;
  }

  lv_obj_t* arrow = CreateLabel(lock_screen, icon::kKeyboardArrowUp,
      lv_color_hex(kLockTextColor), MaterialIconFont56());
  if (arrow == nullptr) {
    lv_obj_delete(lock_screen);
    return nullptr;
  }
  lv_obj_set_style_text_opa(arrow, 230, LV_PART_MAIN);
  lv_obj_align(arrow, LV_ALIGN_BOTTOM_MID, 0, -kUnlockHintBottom);

  lv_obj_move_to_index(lock_screen, -1);
  return lock_screen;
}

/**
 * @brief 更新锁屏页面时间日期文本
 * @param lock_screen 锁屏页面对象
 * @param time_text 时间文本
 * @param date_text 日期文本
 * @param week_text 星期文本
 */
void UpdateLockScreenViewClock(lv_obj_t* lock_screen, const char* time_text,
    const char* date_text, const char* week_text) {
  lv_obj_t* group = LockClockGroup(lock_screen);
  if (group == nullptr || lv_obj_get_child_count(group) < 3) {
    return;
  }

  lv_label_set_text(lv_obj_get_child(group, 0), time_text);
  lv_label_set_text(lv_obj_get_child(group, 1), date_text);
  lv_label_set_text(lv_obj_get_child(group, 2), week_text);
}

/**
 * @brief 根据视觉上滑拖拽距离更新锁屏页面位置
 * @param lock_screen 锁屏页面对象
 * @param offset Y 轴偏移，负数表示向上移动
 */
void SetLockScreenDragOffset(lv_obj_t* lock_screen, int offset) {
  if (lock_screen == nullptr) {
    return;
  }

  const int clamped_offset = std::min(offset, 0);
  lv_anim_delete(lock_screen, SetObjectY);
  lv_obj_set_y(lock_screen, clamped_offset);
}

/**
 * @brief 播放锁屏页面回弹动画
 * @param lock_screen 锁屏页面对象
 */
void StartLockScreenResetAnimation(lv_obj_t* lock_screen) {
  if (lock_screen == nullptr) {
    return;
  }

  lv_anim_t position_animation;
  lv_anim_init(&position_animation);
  lv_anim_set_var(&position_animation, lock_screen);
  lv_anim_set_values(&position_animation, lv_obj_get_y(lock_screen), 0);
  lv_anim_set_duration(&position_animation, kLockResetAnimationMs);
  lv_anim_set_path_cb(&position_animation, lv_anim_path_ease_out);
  lv_anim_set_exec_cb(&position_animation, SetObjectY);
  lv_anim_start(&position_animation);
}

/**
 * @brief 播放锁屏页面解锁退出动画
 * @param lock_screen 锁屏页面对象
 */
void StartLockScreenUnlockAnimation(lv_obj_t* lock_screen) {
  if (lock_screen == nullptr) {
    return;
  }

  lv_anim_t position_animation;
  lv_anim_init(&position_animation);
  lv_anim_set_var(&position_animation, lock_screen);
  lv_anim_set_values(&position_animation, lv_obj_get_y(lock_screen),
      -lv_obj_get_height(lock_screen));
  lv_anim_set_duration(&position_animation, kLockUnlockAnimationMs);
  lv_anim_set_path_cb(&position_animation, lv_anim_path_ease_out);
  lv_anim_set_exec_cb(&position_animation, SetObjectY);
  lv_anim_start(&position_animation);
}

}  // namespace lilygo_box::ui
