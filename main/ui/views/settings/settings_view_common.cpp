/*
 * @Description: Settings view shared helpers
 * @Author: LILYGO_L
 * @Date: 2026-05-23 00:00:00
 * @LastEditTime: 2026-07-09 16:17:53
 * @License: GPL 3.0
 */
#include "ui/views/settings/settings_view_internal.h"

#include <cstring>

#include "ui/resources/fonts/font_assets.h"
#include "ui/resources/fonts/icon_assets.h"
#include "ui/input/app_view_gesture_flags.h"
#include "ui/input/press_cancel.h"

namespace lilygo_box::ui {

void SetTextStyle(lv_obj_t* object, lv_color_t color, const lv_font_t* font) {
  lv_obj_set_style_text_color(object, color, LV_PART_MAIN);
  lv_obj_set_style_text_font(object, font, LV_PART_MAIN);
}

const lv_font_t* Font22() { return &lvgl_font_google_sans_flex_22; }

const lv_font_t* Font24() { return &lvgl_font_google_sans_flex_24; }

const lv_font_t* Font28() { return &lvgl_font_google_sans_flex_28; }

const lv_font_t* Font32() { return &lvgl_font_google_sans_flex_32; }

const lv_font_t* Font36() { return &lvgl_font_google_sans_flex_36; }

const lv_font_t* Font48() { return &lvgl_font_google_sans_flex_48; }

const lv_font_t* Font64() { return &lvgl_font_google_sans_flex_64; }

const lv_font_t* MaterialIconFont32() {
  return &lvgl_font_material_symbols_fill_32;
}

const lv_font_t* MaterialIconFont44() {
  return &lvgl_font_material_symbols_outline_44;
}

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

void MakeTransparent(lv_obj_t* object) {
  lv_obj_set_style_bg_opa(object, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_set_style_border_width(object, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(object, 0, LV_PART_MAIN);
}

bool IsId(const char* left, const char* right) {
  if (left == nullptr || right == nullptr) {
    return false;
  }
  return std::strcmp(left, right) == 0;
}

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

bool IsObjectOrChildOf(lv_obj_t* object, lv_obj_t* parent) {
  while (object != nullptr) {
    if (object == parent) {
      return true;
    }
    object = lv_obj_get_parent(object);
  }
  return false;
}

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

void RestoreSettingsListGestures(SettingsViewState* state) {
  if (state == nullptr || state->root == nullptr) {
    return;
  }

  lv_obj_remove_flag(state->root, kBlockLauncherGestureFlag);
  lv_obj_add_flag(state->root, LV_OBJ_FLAG_GESTURE_BUBBLE);
}

lv_obj_t* CreateToolbarButton(lv_obj_t* parent, int x, int y,
    lv_event_cb_t callback, SettingsViewState* state) {
  lv_obj_t* button = lv_button_create(parent);
  if (button == nullptr) {
    return nullptr;
  }

  lv_obj_remove_style_all(button);
  lv_obj_remove_flag(button, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(button, LV_OBJ_FLAG_PRESS_LOCK);
  lv_obj_add_flag(button, LV_OBJ_FLAG_GESTURE_BUBBLE);
  lv_obj_set_size(button, kNameEditButtonSize, kNameEditButtonSize);
  lv_obj_set_pos(button, x, y);
  lv_obj_set_style_bg_opa(button, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_set_style_bg_opa(button, LV_OPA_TRANSP, LV_STATE_PRESSED);
  lv_obj_set_style_bg_opa(button, LV_OPA_TRANSP, LV_STATE_FOCUSED);
  lv_obj_set_style_bg_opa(button, LV_OPA_TRANSP, LV_STATE_FOCUS_KEY);
  lv_obj_set_style_border_width(button, 0, LV_PART_MAIN);
  lv_obj_set_style_outline_width(button, 0, LV_PART_MAIN);
  lv_obj_set_style_outline_width(button, 0, LV_STATE_PRESSED);
  lv_obj_set_style_outline_width(button, 0, LV_STATE_FOCUSED);
  lv_obj_set_style_outline_width(button, 0, LV_STATE_FOCUS_KEY);
  lv_obj_set_style_shadow_width(button, 0, LV_PART_MAIN);
  lv_obj_set_style_shadow_width(button, 0, LV_STATE_PRESSED);
  lv_obj_set_style_shadow_width(button, 0, LV_STATE_FOCUSED);
  lv_obj_set_style_shadow_width(button, 0, LV_STATE_FOCUS_KEY);
  lv_obj_set_style_radius(button, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(button, 0, LV_PART_MAIN);
  if (!AddPressCancelOnLeave(button)) {
    return nullptr;
  }
  lv_obj_add_event_cb(button, callback, LV_EVENT_CLICKED, state);
  return button;
}

}  // namespace lilygo_box::ui
