/*
 * @Description: 图标按钮共用按压样式，按下时显示圆形主题背景。
 * @Author: LILYGO_L
 * @Date: 2026-10-12 00:00:00
 * @LastEditTime: 2026-10-12 00:00:00
 * @License: GPL 3.0
 */
#pragma once

#include "lvgl.h"
#include "ui/theme/theme_provider.h"

namespace lilygo_box::ui {

/**
 * @brief 为方形图标按钮添加圆形按压反馈，默认保持透明
 * @param button 已设置尺寸的图标按钮
 */
inline void ApplyCircularIconButtonFeedback(lv_obj_t* button) {
  if (button == nullptr) return;
  lv_obj_set_style_radius(button, LV_RADIUS_CIRCLE, LV_PART_MAIN);
  lv_obj_set_style_radius(button, LV_RADIUS_CIRCLE, LV_STATE_PRESSED);
  lv_obj_set_style_bg_opa(button, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_set_style_bg_color(button,
      lv_color_hex(theme::ActiveThemeColors().state_layer), LV_STATE_PRESSED);
  lv_obj_set_style_bg_opa(button, LV_OPA_COVER, LV_STATE_PRESSED);
  lv_obj_set_style_bg_opa(button, LV_OPA_TRANSP,
      LV_STATE_PRESSED | LV_STATE_DISABLED);
}

}  // namespace lilygo_box::ui
