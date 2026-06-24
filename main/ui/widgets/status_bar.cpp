/*
 * @Description: None
 * @Author: LILYGO_L
 * @Date: 2026-05-12 01:08:42
 * @LastEditTime: 2026-05-12 01:08:42
 * @License: GPL 3.0
 */
#include "ui/widgets/status_bar.h"

#include <algorithm>
#include <cstdint>
#include <cstring>

#include "ui/font/font_assets.h"
#include "ui/font/material_symbols_assets.h"
#include "ui/icon/icon_assets.h"

namespace lilygo_box::ui {
namespace {

constexpr int kStatusBarHeight = 50;
constexpr int kStatusBarPadding = 40;
constexpr int kStatusBarIconGap = -6;
constexpr int kStatusBarBatteryPercentGap = 4;
constexpr uint32_t kStatusBarBackgroundColor = 0x000000;
constexpr uint32_t kStatusBarTextColor = 0xFFFFFF;

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
 * @brief 获取 24 号 Google Sans 字体
 * @return 字体指针
 */
const lv_font_t* Font24() { return &lvgl_font_google_sans_flex_24; }

/**
 * @brief 获取 28 号 Material Symbols 字体
 * @return 字体指针
 */
const lv_font_t* MaterialIconFont28() { return &lvgl_font_material_symbols_28; }

/**
 * @brief 根据电量百分比选择电池图标
 * @param percent 电量百分比
 * @return 电池图标文本
 */
const char* BatteryIconFromPercent(int percent) {
  if (percent >= 95) {
    return icon::kBatteryAndroidFull;
  }
  if (percent >= 80) {
    return icon::kBatteryAndroid6;
  }
  if (percent >= 65) {
    return icon::kBatteryAndroid5;
  }
  if (percent >= 50) {
    return icon::kBatteryAndroid4;
  }
  if (percent >= 35) {
    return icon::kBatteryAndroid3;
  }
  if (percent >= 20) {
    return icon::kBatteryAndroid2;
  }
  if (percent >= 5) {
    return icon::kBatteryAndroid1;
  }
  return icon::kBatteryAndroid0;
}

/**
 * @brief 格式化状态栏电池百分比
 * @param percent 电池百分比，范围 0~100
 * @param buffer 输出缓冲区，至少 5 字节
 */
void FormatBatteryPercent(int percent, char* buffer) {
  if (percent >= 100) {
    buffer[0] = '1';
    buffer[1] = '0';
    buffer[2] = '0';
    buffer[3] = '%';
    buffer[4] = '\0';
    return;
  }

  if (percent >= 10) {
    buffer[0] = static_cast<char>('0' + percent / 10);
    buffer[1] = static_cast<char>('0' + percent % 10);
    buffer[2] = '%';
    buffer[3] = '\0';
    return;
  }

  buffer[0] = static_cast<char>('0' + percent);
  buffer[1] = '%';
  buffer[2] = '\0';
}

/**
 * @brief 创建状态栏文本标签
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
 * @brief 清除对象背景、边框和内边距
 * @param object LVGL 对象
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

  bmu_percent_label_ =
      CreateLabel(object_, "--%", lv_color_hex(kStatusBarTextColor), Font24());
  if (bmu_percent_label_ == nullptr) {
    lv_obj_delete(object_);
    object_ = nullptr;
    return false;
  }
  lv_obj_align(bmu_percent_label_, LV_ALIGN_RIGHT_MID, 0, 0);

  bmu_label_ =
      CreateLabel(object_, icon::kBatteryAndroid3,
          lv_color_hex(kStatusBarTextColor), MaterialIconFont28());
  if (bmu_label_ == nullptr) {
    lv_obj_delete(object_);
    object_ = nullptr;
    return false;
  }
  lv_obj_align_to(bmu_label_, bmu_percent_label_, LV_ALIGN_OUT_LEFT_MID,
      -kStatusBarBatteryPercentGap, 0);

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

void StatusBar::SetTimeText(const char* text) {
  if (time_label_ == nullptr || text == nullptr ||
      std::strncmp(time_text_, text, sizeof(time_text_)) == 0) {
    return;
  }

  std::strncpy(time_text_, text, sizeof(time_text_) - 1);
  time_text_[sizeof(time_text_) - 1] = '\0';
  lv_label_set_text(time_label_, time_text_);
}

void StatusBar::SetBatteryPercent(int percent) {
  if (bmu_label_ == nullptr || bmu_percent_label_ == nullptr) {
    return;
  }

  const int clamped_percent = std::clamp(percent, 0, 100);
  char percent_text[sizeof(bmu_percent_text_)] = {};
  FormatBatteryPercent(clamped_percent, percent_text);
  if (std::strncmp(
          bmu_percent_text_, percent_text, sizeof(bmu_percent_text_)) == 0) {
    return;
  }

  std::strncpy(
      bmu_percent_text_, percent_text, sizeof(bmu_percent_text_) - 1);
  bmu_percent_text_[sizeof(bmu_percent_text_) - 1] = '\0';
  lv_label_set_text(bmu_percent_label_, bmu_percent_text_);
  lv_label_set_text(bmu_label_, BatteryIconFromPercent(clamped_percent));
  lv_obj_align_to(bmu_label_, bmu_percent_label_, LV_ALIGN_OUT_LEFT_MID,
      -kStatusBarBatteryPercentGap, 0);
  lv_obj_align_to(
      wifi_label_, bmu_label_, LV_ALIGN_OUT_LEFT_MID, kStatusBarIconGap, 0);
}

void StatusBar::MoveToTop() {
  if (object_ != nullptr) {
    lv_obj_move_to_index(object_, -1);
  }
}

void StatusBar::SetTextColor(lv_color_t color) {
  if (time_label_ != nullptr) {
    lv_obj_set_style_text_color(time_label_, color, LV_PART_MAIN);
  }
  if (wifi_label_ != nullptr) {
    lv_obj_set_style_text_color(wifi_label_, color, LV_PART_MAIN);
  }
  if (bmu_label_ != nullptr) {
    lv_obj_set_style_text_color(bmu_label_, color, LV_PART_MAIN);
  }
  if (bmu_percent_label_ != nullptr) {
    lv_obj_set_style_text_color(bmu_percent_label_, color, LV_PART_MAIN);
  }
}

void StatusBar::SetVisible(bool visible) {
  if (object_ == nullptr) {
    return;
  }

  if (visible) {
    lv_obj_remove_flag(object_, LV_OBJ_FLAG_HIDDEN);
    MoveToTop();
    return;
  }

  lv_obj_add_flag(object_, LV_OBJ_FLAG_HIDDEN);
}

}  // namespace lilygo_box::ui
