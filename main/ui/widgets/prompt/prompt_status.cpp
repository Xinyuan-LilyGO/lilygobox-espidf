/*
 * @Description: Shared in-page status prompt widget
 * @Author: LILYGO_L
 * @Date: 2026-08-22 00:00:00
 * @LastEditTime: 2026-09-02 17:57:17
 * @License: GPL 3.0
 */
#include "ui/widgets/prompt/prompt_status.h"

#include "ui/input/press_cancel.h"

namespace lilygo_box::ui {
namespace {

bool CreatePromptLabel(lv_obj_t* parent, const char* text,
    const lv_font_t* font, uint32_t color, int width, int top) {
  if (text == nullptr || text[0] == '\0') {
    return true;
  }
  lv_obj_t* label = lv_label_create(parent);
  if (label == nullptr) {
    return false;
  }
  lv_label_set_text(label, text);
  lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(label, width);
  lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
  lv_obj_set_style_text_color(label, lv_color_hex(color), LV_PART_MAIN);
  if (font != nullptr) {
    lv_obj_set_style_text_font(label, font, LV_PART_MAIN);
  }
  lv_obj_align(label, LV_ALIGN_TOP_MID, 0, top);
  return true;
}

bool CreatePromptIcon(lv_obj_t* parent, const PromptStatusConfig& config) {
  if (config.icon == nullptr || config.icon[0] == '\0' ||
      config.icon_background_size <= 0) {
    return false;
  }
  lv_obj_t* background = lv_obj_create(parent);
  if (background == nullptr) {
    return false;
  }
  lv_obj_remove_style_all(background);
  lv_obj_remove_flag(background, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(
      background, config.icon_background_size, config.icon_background_size);
  lv_obj_set_style_radius(
      background, config.icon_background_size / 2, LV_PART_MAIN);
  lv_obj_set_style_bg_color(
      background, lv_color_hex(config.icon_background_color), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(background, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_align(background, LV_ALIGN_TOP_MID, 0, config.visual_top);

  lv_obj_t* icon_label = lv_label_create(background);
  if (icon_label == nullptr) {
    return false;
  }
  lv_label_set_text(icon_label, config.icon);
  lv_obj_set_style_text_color(
      icon_label, lv_color_hex(config.icon_color), LV_PART_MAIN);
  if (config.icon_font != nullptr) {
    lv_obj_set_style_text_font(icon_label, config.icon_font, LV_PART_MAIN);
  }
  lv_obj_set_style_text_align(icon_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
  lv_obj_center(icon_label);
  return true;
}

bool CreatePromptSpinner(lv_obj_t* parent, const PromptStatusConfig& config) {
  if (config.spinner_size <= 0 || config.spinner_arc_width <= 0) {
    return false;
  }
  lv_obj_t* spinner = lv_spinner_create(parent);
  if (spinner == nullptr) {
    return false;
  }
  lv_obj_set_size(spinner, config.spinner_size, config.spinner_size);
  lv_spinner_set_anim_params(
      spinner, config.spinner_period_ms, config.spinner_arc_length);
  lv_obj_set_style_arc_color(
      spinner, lv_color_hex(config.spinner_track_color), LV_PART_MAIN);
  lv_obj_set_style_arc_color(
      spinner, lv_color_hex(config.spinner_indicator_color), LV_PART_INDICATOR);
  lv_obj_set_style_arc_width(spinner, config.spinner_arc_width, LV_PART_MAIN);
  lv_obj_set_style_arc_width(
      spinner, config.spinner_arc_width, LV_PART_INDICATOR);
  lv_obj_align(spinner, LV_ALIGN_TOP_MID, 0, config.visual_top);
  return true;
}

bool CreatePromptButton(lv_obj_t* parent, const PromptStatusConfig& config) {
  if (config.button_text == nullptr || config.button_text[0] == '\0') {
    return true;
  }
  if (config.button_width <= 0 || config.button_height <= 0) {
    return false;
  }
  lv_obj_t* button = lv_button_create(parent);
  if (button == nullptr) {
    return false;
  }
  lv_obj_remove_style_all(button);
  if (config.bubble_gestures) {
    lv_obj_add_flag(button, LV_OBJ_FLAG_GESTURE_BUBBLE);
  }
  lv_obj_set_size(button, config.button_width, config.button_height);
  lv_obj_set_style_bg_color(
      button, lv_color_hex(config.button_background_color), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(button, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_bg_color(
      button, lv_color_hex(config.button_pressed_color), LV_STATE_PRESSED);
  lv_obj_set_style_bg_opa(button, LV_OPA_COVER, LV_STATE_PRESSED);
  int button_radius = config.button_radius;
  if (button_radius <= 0) {
    button_radius = config.button_height / 2;
  }
  lv_obj_set_style_radius(button, button_radius, LV_PART_MAIN);
  lv_obj_set_style_radius(button, button_radius, LV_STATE_PRESSED);
  if (!AddPressCancelOnLeave(button)) {
    return false;
  }
  if (config.button_callback != nullptr) {
    lv_obj_add_event_cb(button, config.button_callback, LV_EVENT_CLICKED,
        config.button_user_data);
  }
  lv_obj_align(button, LV_ALIGN_TOP_MID, 0, config.button_top);

  lv_obj_t* label = lv_label_create(button);
  if (label == nullptr) {
    return false;
  }
  lv_label_set_text(label, config.button_text);
  lv_obj_set_style_text_color(
      label, lv_color_hex(config.button_text_color), LV_PART_MAIN);
  if (config.button_font != nullptr) {
    lv_obj_set_style_text_font(label, config.button_font, LV_PART_MAIN);
  }
  lv_obj_center(label);
  return true;
}

}  // namespace

lv_obj_t* CreatePromptStatus(
    lv_obj_t* parent, const PromptStatusConfig& config) {
  const int text_width = config.width - config.horizontal_padding * 2;
  if (parent == nullptr || config.width <= 0 || config.height <= 0 ||
      config.horizontal_padding < 0 || text_width <= 0 ||
      config.title == nullptr || config.title[0] == '\0') {
    return nullptr;
  }

  lv_obj_t* group = lv_obj_create(parent);
  if (group == nullptr) {
    return nullptr;
  }
  lv_obj_remove_style_all(group);
  lv_obj_remove_flag(group, LV_OBJ_FLAG_SCROLLABLE);
  if (config.bubble_gestures) {
    lv_obj_add_flag(group, LV_OBJ_FLAG_GESTURE_BUBBLE);
  }
  lv_obj_set_size(group, config.width, config.height);

  bool visual_created = false;
  if (config.visual == PromptStatusVisual::kSpinner) {
    visual_created = CreatePromptSpinner(group, config);
  } else {
    visual_created = CreatePromptIcon(group, config);
  }
  if (!visual_created ||
      !CreatePromptLabel(group, config.title, config.title_font,
          config.title_color, text_width, config.title_top) ||
      !CreatePromptLabel(group, config.message, config.message_font,
          config.message_color, text_width, config.message_top) ||
      !CreatePromptButton(group, config)) {
    lv_obj_delete(group);
    return nullptr;
  }
  return group;
}

}  // namespace lilygo_box::ui
