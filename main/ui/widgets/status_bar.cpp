/*
 * @Description: None
 * @Author: LILYGO_L
 * @Date: 2026-05-12 01:08:42
 * @LastEditTime: 2026-05-12 01:08:42
 * @License: GPL 3.0
 */
#include "ui/widgets/status_bar.h"

#include <cstdint>

#include "ui/font/font_assets.h"
#include "ui/font/material_symbols_assets.h"
#include "ui/icon/icon_assets.h"

namespace lilygo_box::ui {
namespace {

constexpr int kStatusBarHeight = 50;
constexpr int kStatusBarPadding = 24;
constexpr int kStatusBarIconGap = -6;
constexpr uint32_t kStatusBarBackgroundColor = 0x000000;
constexpr uint32_t kStatusBarTextColor = 0xFFFFFF;

/**
 * @brief 设置文本对象的颜色和字体
 * @param object LVGL 对象
 * @param color 文本颜色
 * @param font 文本字体
 * @return
 * @Date 2026-05-13 09:55:00
 */
void SetTextStyle(lv_obj_t* object, lv_color_t color, const lv_font_t* font) {
  lv_obj_set_style_text_color(object, color, LV_PART_MAIN);
  lv_obj_set_style_text_font(object, font, LV_PART_MAIN);
}

/**
 * @brief 获取 24 号 Google Sans 字体
 * @return 字体指针
 * @Date 2026-05-13 09:55:00
 */
const lv_font_t* Font24() { return &lvgl_font_google_sans_flex_24; }

/**
 * @brief 获取 28 号 Material Symbols 字体
 * @return 字体指针
 * @Date 2026-05-13 09:55:00
 */
const lv_font_t* MaterialIconFont28() { return &lvgl_font_material_symbols_28; }

/**
 * @brief 创建状态栏文本标签
 * @param parent 父对象
 * @param text 显示文本
 * @param color 文本颜色
 * @param font 文本字体
 * @return 创建成功返回对象指针，否则返回 nullptr
 * @Date 2026-05-13 09:55:00
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
 * @brief 清除对象背景、边框和内边距
 * @param object LVGL 对象
 * @return
 * @Date 2026-05-13 09:55:00
 */
void MakeTransparent(lv_obj_t* object) {
  lv_obj_set_style_bg_opa(object, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_set_style_border_width(object, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(object, 0, LV_PART_MAIN);
}

}  // namespace

bool StatusBar::Init(lv_obj_t* parent, int width) {
  if (parent == nullptr || width <= 0) {
    return false;
  }

  object_ = lv_obj_create(parent);
  if (object_ == nullptr) {
    return false;
  }

  lv_obj_remove_flag(object_, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(object_, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_remove_flag(object_, LV_OBJ_FLAG_GESTURE_BUBBLE);
  MakeTransparent(object_);
  lv_obj_set_size(object_, width, kStatusBarHeight);
  lv_obj_align(object_, LV_ALIGN_TOP_MID, 0, 0);
  lv_obj_set_style_bg_color(
      object_, lv_color_hex(kStatusBarBackgroundColor), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(object_, LV_OPA_10, LV_PART_MAIN);
  lv_obj_set_style_radius(object_, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_hor(object_, kStatusBarPadding, LV_PART_MAIN);

  time_label_ =
      CreateLabel(object_, "09:15", lv_color_hex(kStatusBarTextColor),
          Font24());
  if (time_label_ == nullptr) {
    lv_obj_delete(object_);
    object_ = nullptr;
    return false;
  }
  lv_obj_align(time_label_, LV_ALIGN_LEFT_MID, 0, 0);

  bmu_label_ =
      CreateLabel(object_, icon::kBatteryAndroid3,
          lv_color_hex(kStatusBarTextColor), MaterialIconFont28());
  if (bmu_label_ == nullptr) {
    lv_obj_delete(object_);
    object_ = nullptr;
    return false;
  }
  lv_obj_align(bmu_label_, LV_ALIGN_RIGHT_MID, 0, 0);

  wifi_label_ = CreateLabel(object_, icon::kWifi,
      lv_color_hex(kStatusBarTextColor), MaterialIconFont28());
  if (wifi_label_ == nullptr) {
    lv_obj_delete(object_);
    object_ = nullptr;
    return false;
  }
  lv_obj_align_to(
      wifi_label_, bmu_label_, LV_ALIGN_OUT_LEFT_MID, kStatusBarIconGap, 0);
  return true;
}

void StatusBar::MoveToTop() {
  if (object_ != nullptr) {
    lv_obj_move_to_index(object_, -1);
  }
}

}  // namespace lilygo_box::ui
