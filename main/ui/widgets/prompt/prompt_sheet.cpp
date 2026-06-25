/*
 * @Description: Bottom prompt sheet widget
 * @Author: LILYGO_L
 * @Date: 2026-06-23 00:00:00
 * @LastEditTime: 2026-06-23 00:00:00
 * @License: GPL 3.0
 */
#include "ui/widgets/prompt/prompt_sheet.h"

#include "ui/input/press_cancel.h"

namespace lilygo_box::ui {

namespace {

/**
 * @brief 合并 LVGL 部件和状态选择器
 * @param part LVGL 部件
 * @param state LVGL 状态
 * @return 合并后的样式选择器
 */
lv_style_selector_t StyleSelector(lv_part_t part, lv_state_t state) {
  return static_cast<lv_style_selector_t>(
      static_cast<uint32_t>(part) | static_cast<uint32_t>(state));
}

void SetObjectY(void* object, int32_t value) {
  if (object == nullptr) {
    return;
  }
  lv_obj_set_y(static_cast<lv_obj_t*>(object), value);
}

/**
 * @brief 处理提示栏退出动画完成后的遮罩删除
 * @param animation LVGL 动画对象
 */
void PromptSheetOutCompletedCallback(lv_anim_t* animation) {
  auto* overlay = static_cast<lv_obj_t*>(lv_anim_get_user_data(animation));
  if (overlay != nullptr) {
    lv_obj_delete(overlay);
  }
}

}  // namespace

lv_obj_t* CreatePromptSheetOverlay(
    lv_obj_t* parent, const PromptSheetConfig& config) {
  if (parent == nullptr || config.screen_width <= 0 ||
      config.screen_height <= 0) {
    return nullptr;
  }

  lv_obj_t* overlay = lv_obj_create(parent);
  if (overlay == nullptr) {
    return nullptr;
  }
  lv_obj_remove_flag(overlay, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(overlay, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_size(overlay, config.screen_width, config.screen_height);
  lv_obj_set_pos(overlay, 0, 0);
  lv_obj_set_style_bg_color(overlay, lv_color_hex(0x000000), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(overlay, config.overlay_opacity, LV_PART_MAIN);
  lv_obj_set_style_border_width(overlay, 0, LV_PART_MAIN);
  lv_obj_set_style_radius(overlay, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(overlay, 0, LV_PART_MAIN);
  return overlay;
}

lv_obj_t* CreatePromptSheet(
    lv_obj_t* overlay, const PromptSheetConfig& config) {
  if (overlay == nullptr || config.sheet_width <= 0 ||
      config.sheet_height <= 0) {
    return nullptr;
  }

  lv_obj_t* sheet = lv_obj_create(overlay);
  if (sheet == nullptr) {
    return nullptr;
  }
  lv_obj_remove_flag(sheet, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(sheet, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_flag(sheet, LV_OBJ_FLAG_GESTURE_BUBBLE);
  lv_obj_set_size(sheet, config.sheet_width, config.sheet_height);
  lv_obj_set_pos(sheet, config.side_margin, config.screen_height);
  lv_obj_set_style_bg_color(
      sheet, lv_color_hex(config.sheet_color), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(sheet, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_border_width(sheet, 0, LV_PART_MAIN);
  lv_obj_set_style_radius(sheet, config.sheet_radius, LV_PART_MAIN);
  lv_obj_set_style_pad_all(sheet, 0, LV_PART_MAIN);
  return sheet;
}

/**
 * @brief 创建底部提示栏文本标签
 * @param parent 父对象
 * @param text 标签文本
 * @param color 文本颜色
 * @param font 文本字体
 * @return 创建成功返回标签对象，否则返回 nullptr
 */
lv_obj_t* CreatePromptSheetLabel(lv_obj_t* parent, const char* text,
    uint32_t color, const lv_font_t* font) {
  if (parent == nullptr || text == nullptr) {
    return nullptr;
  }

  lv_obj_t* label = lv_label_create(parent);
  if (label == nullptr) {
    return nullptr;
  }
  lv_label_set_text(label, text);
  lv_obj_set_style_text_color(label, lv_color_hex(color), LV_PART_MAIN);
  if (font != nullptr) {
    lv_obj_set_style_text_font(label, font, LV_PART_MAIN);
  }
  return label;
}

lv_obj_t* CreatePromptSheetButton(
    lv_obj_t* parent, const PromptSheetButtonConfig& config) {
  if (parent == nullptr || config.text == nullptr || config.width <= 0 ||
      config.height <= 0) {
    return nullptr;
  }

  lv_obj_t* button = lv_obj_create(parent);
  if (button == nullptr) {
    return nullptr;
  }
  lv_obj_remove_flag(button, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(button, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
  lv_obj_add_flag(button, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_flag(button, LV_OBJ_FLAG_GESTURE_BUBBLE);
  lv_obj_set_size(button, config.width, config.height);
  lv_obj_set_pos(button, config.x, config.y);
  lv_obj_set_style_bg_color(
      button, lv_color_hex(config.background_color), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(button, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_bg_color(button, lv_color_hex(config.background_color),
      StyleSelector(LV_PART_MAIN, LV_STATE_FOCUSED));
  lv_obj_set_style_bg_opa(
      button, LV_OPA_COVER, StyleSelector(LV_PART_MAIN, LV_STATE_FOCUSED));
  lv_obj_set_style_bg_color(button, lv_color_hex(config.background_color),
      StyleSelector(LV_PART_MAIN, LV_STATE_FOCUS_KEY));
  lv_obj_set_style_bg_opa(
      button, LV_OPA_COVER, StyleSelector(LV_PART_MAIN, LV_STATE_FOCUS_KEY));
  lv_obj_set_style_bg_color(button, lv_color_hex(
      config.disabled_background_color),
      StyleSelector(LV_PART_MAIN, LV_STATE_DISABLED));
  lv_obj_set_style_bg_opa(button, LV_OPA_COVER,
      StyleSelector(LV_PART_MAIN, LV_STATE_DISABLED));
  lv_obj_set_style_bg_color(button,
      lv_color_hex(config.pressed_background_color),
      StyleSelector(LV_PART_MAIN, LV_STATE_PRESSED));
  lv_obj_set_style_bg_opa(button, config.pressed_opacity,
      StyleSelector(LV_PART_MAIN, LV_STATE_PRESSED));
  lv_obj_set_style_border_width(button, 0, LV_PART_MAIN);
  lv_obj_set_style_outline_width(button, 0, LV_PART_MAIN);
  lv_obj_set_style_outline_width(
      button, 0, StyleSelector(LV_PART_MAIN, LV_STATE_FOCUSED));
  lv_obj_set_style_outline_width(
      button, 0, StyleSelector(LV_PART_MAIN, LV_STATE_FOCUS_KEY));
  lv_obj_set_style_shadow_width(button, 0, LV_PART_MAIN);
  lv_obj_set_style_shadow_width(
      button, 0, StyleSelector(LV_PART_MAIN, LV_STATE_FOCUSED));
  lv_obj_set_style_shadow_width(
      button, 0, StyleSelector(LV_PART_MAIN, LV_STATE_FOCUS_KEY));
  lv_obj_set_style_radius(button, config.radius, LV_PART_MAIN);
  lv_obj_set_style_pad_all(button, 0, LV_PART_MAIN);
  if (!config.enabled) {
    lv_obj_add_state(button, LV_STATE_DISABLED);
  }
  if (!AddPressCancelOnLeave(button)) {
    lv_obj_delete(button);
    return nullptr;
  }
  if (config.callback != nullptr) {
    lv_obj_add_event_cb(
        button, config.callback, LV_EVENT_CLICKED, config.user_data);
  }

  lv_obj_t* label = lv_label_create(button);
  if (label == nullptr) {
    lv_obj_delete(button);
    return nullptr;
  }
  lv_label_set_text(label, config.text);
  lv_obj_set_style_text_color(
      label, lv_color_hex(config.text_color), LV_PART_MAIN);
  if (config.font != nullptr) {
    lv_obj_set_style_text_font(label, config.font, LV_PART_MAIN);
  }
  lv_obj_center(label);
  return button;
}

void AnimatePromptSheetIn(
    lv_obj_t* sheet, const PromptSheetConfig& config, uint32_t duration_ms) {
  if (sheet == nullptr) {
    return;
  }

  lv_anim_t animation;
  lv_anim_init(&animation);
  lv_anim_set_var(&animation, sheet);
  lv_anim_set_values(&animation, config.screen_height,
      config.screen_height - config.sheet_height - config.bottom_margin);
  lv_anim_set_duration(&animation, duration_ms);
  lv_anim_set_path_cb(&animation, lv_anim_path_ease_out);
  lv_anim_set_exec_cb(&animation, SetObjectY);
  lv_anim_start(&animation);
}

/**
 * @brief 播放底部提示栏退出动画并在结束后删除遮罩层
 * @param overlay 遮罩对象
 * @param sheet 面板对象
 * @param duration_ms 动画时长
 * @return 动画启动成功返回 true，否则返回 false
 */
bool AnimatePromptSheetOut(
    lv_obj_t* overlay, lv_obj_t* sheet, uint32_t duration_ms) {
  if (overlay == nullptr || sheet == nullptr || duration_ms == 0) {
    return false;
  }

  lv_obj_remove_flag(overlay, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_remove_flag(sheet, LV_OBJ_FLAG_CLICKABLE);
  lv_anim_delete(sheet, SetObjectY);

  lv_anim_t animation;
  lv_anim_init(&animation);
  lv_anim_set_var(&animation, sheet);
  lv_anim_set_values(
      &animation, lv_obj_get_y(sheet), lv_obj_get_height(overlay));
  lv_anim_set_duration(&animation, duration_ms);
  lv_anim_set_path_cb(&animation, lv_anim_path_ease_in);
  lv_anim_set_exec_cb(&animation, SetObjectY);
  lv_anim_set_completed_cb(&animation, PromptSheetOutCompletedCallback);
  lv_anim_set_user_data(&animation, overlay);
  lv_anim_start(&animation);
  return true;
}

}  // namespace lilygo_box::ui
