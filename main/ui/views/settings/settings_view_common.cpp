/*
 * @Description: Settings view shared helpers
 * @Author: LILYGO_L
 * @Date: 2026-05-23 00:00:00
 * @LastEditTime: 2026-05-23 00:00:00
 * @License: GPL 3.0
 */
#include "ui/views/settings/settings_view_internal.h"

#include <cstring>

#include "ui/font/font_assets.h"
#include "ui/font/material_symbols_assets.h"
#include "ui/input/app_view_gesture_flags.h"
#include "ui/input/press_cancel.h"

namespace lilygo_box::ui {

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
 * @brief 获取 22 号 Google Sans 字体
 * @return 字体指针
 */
const lv_font_t* Font22() { return &lvgl_font_google_sans_flex_22; }

/**
 * @brief 获取 24 号 Google Sans 字体
 * @return 字体指针
 */
const lv_font_t* Font24() { return &lvgl_font_google_sans_flex_24; }

/**
 * @brief 获取 28 号 Google Sans 字体
 * @return 字体指针
 */
const lv_font_t* Font28() { return &lvgl_font_google_sans_flex_28; }

/**
 * @brief 获取 32 号 Google Sans 字体
 * @return 字体指针
 */
const lv_font_t* Font32() { return &lvgl_font_google_sans_flex_32; }

/**
 * @brief 获取 36 号 Google Sans 字体
 * @return 字体指针
 */
const lv_font_t* Font36() { return &lvgl_font_google_sans_flex_36; }

/**
 * @brief 获取 48 号 Google Sans 字体
 * @return 字体指针
 */
const lv_font_t* Font48() { return &lvgl_font_google_sans_flex_48; }

/**
 * @brief 获取 64 号 Google Sans 字体
 * @return 字体指针
 */
const lv_font_t* Font64() { return &lvgl_font_google_sans_flex_64; }

/**
 * @brief 获取 32 号 Material Symbols 字体
 * @return 字体指针
 */
const lv_font_t* MaterialIconFont32() {
  return &lvgl_font_material_symbols_32;
}

/**
 * @brief 创建文本标签
 * @param parent 父对象
 * @param text 文本内容
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
  lv_obj_add_flag(label, LV_OBJ_FLAG_GESTURE_BUBBLE);
  SetTextStyle(label, color, font);
  return label;
}

/**
 * @brief 设置对象为透明背景
 * @param object LVGL 对象
 */
void MakeTransparent(lv_obj_t* object) {
  lv_obj_set_style_bg_opa(object, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_set_style_border_width(object, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(object, 0, LV_PART_MAIN);
}

/**
 * @brief 判断两个 ID 字符串是否相同
 * @param left 左侧 ID
 * @param right 右侧 ID
 * @return 相同返回 true，否则返回 false
 */
bool IsId(const char* left, const char* right) {
  if (left == nullptr || right == nullptr) {
    return false;
  }
  return std::strcmp(left, right) == 0;
}

/**
 * @brief 创建基础容器对象
 * @param parent 父对象
 * @param width 对象宽度
 * @param height 对象高度
 * @param color 背景颜色
 * @param opacity 背景透明度
 * @param radius 圆角半径
 * @return 创建成功返回对象指针，否则返回 nullptr
 */
lv_obj_t* CreateBox(lv_obj_t* parent, int width, int height, uint32_t color,
    lv_opa_t opacity, int radius) {
  lv_obj_t* object = lv_obj_create(parent);
  if (object == nullptr) {
    return nullptr;
  }

  lv_obj_remove_flag(object, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(object, LV_OBJ_FLAG_GESTURE_BUBBLE);
  lv_obj_set_size(object, width, height);
  lv_obj_set_style_bg_color(object, lv_color_hex(color), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(object, opacity, LV_PART_MAIN);
  lv_obj_set_style_border_width(object, 0, LV_PART_MAIN);
  lv_obj_set_style_radius(object, radius, LV_PART_MAIN);
  lv_obj_set_style_pad_all(object, 0, LV_PART_MAIN);
  return object;
}

/**
 * @brief 判断对象是否为指定父对象或其子对象
 * @param object 待判断对象
 * @param parent 目标父对象
 * @return 是目标对象或子对象返回 true，否则返回 false
 */
bool IsObjectOrChildOf(lv_obj_t* object, lv_obj_t* parent) {
  while (object != nullptr) {
    if (object == parent) {
      return true;
    }
    object = lv_obj_get_parent(object);
  }
  return false;
}

/**
 * @brief 创建分组分割线
 * @param parent 父对象
 * @param width 分割线宽度
 * @return 创建成功返回对象指针，否则返回 nullptr
 */
lv_obj_t* CreateDivider(lv_obj_t* parent, int width) {
  lv_obj_t* divider = lv_obj_create(parent);
  if (divider == nullptr) {
    return nullptr;
  }

  lv_obj_remove_flag(divider, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(divider, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_size(divider, width, kDividerHeight);
  lv_obj_set_style_bg_color(
      divider, lv_color_hex(kDividerColor), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(divider, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_border_width(divider, 0, LV_PART_MAIN);
  lv_obj_set_style_radius(divider, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(divider, 0, LV_PART_MAIN);
  return divider;
}

/**
 * @brief 恢复设置列表页面的 launcher 手势
 * @param state 设置页面状态
 */
void RestoreSettingsListGestures(SettingsViewState* state) {
  if (state == nullptr || state->root == nullptr) {
    return;
  }

  lv_obj_remove_flag(state->root, kBlockLauncherGestureFlag);
  lv_obj_add_flag(state->root, LV_OBJ_FLAG_GESTURE_BUBBLE);
}

/**
 * @brief 创建透明工具按钮
 * @param parent 父对象
 * @param x X 坐标
 * @param y Y 坐标
 * @param callback 点击回调
 * @param state 设置页面状态
 * @return 创建成功返回按钮对象，否则返回 nullptr
 */
lv_obj_t* CreateToolbarButton(lv_obj_t* parent, int x, int y,
    lv_event_cb_t callback, SettingsViewState* state) {
  lv_obj_t* button = lv_button_create(parent);
  if (button == nullptr) {
    return nullptr;
  }

  lv_obj_remove_flag(button, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(button, LV_OBJ_FLAG_PRESS_LOCK);
  lv_obj_add_flag(button, LV_OBJ_FLAG_GESTURE_BUBBLE);
  lv_obj_set_size(button, kNameEditButtonSize, kNameEditButtonSize);
  lv_obj_set_pos(button, x, y);
  lv_obj_set_style_bg_opa(button, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_set_style_bg_color(
      button, lv_color_hex(kPressedColor), LV_STATE_PRESSED);
  lv_obj_set_style_bg_opa(button, kPressedOpacity, LV_STATE_PRESSED);
  lv_obj_set_style_border_width(button, 0, LV_PART_MAIN);
  lv_obj_set_style_radius(button, kNameEditButtonSize / 2, LV_PART_MAIN);
  lv_obj_set_style_pad_all(button, 0, LV_PART_MAIN);
  if (!AddPressCancelOnLeave(button)) {
    return nullptr;
  }
  lv_obj_add_event_cb(button, callback, LV_EVENT_CLICKED, state);
  return button;
}

}  // namespace lilygo_box::ui
