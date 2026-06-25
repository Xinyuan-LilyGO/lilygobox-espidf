/*
 * @Description: Bottom prompt sheet widget
 * @Author: LILYGO_L
 * @Date: 2026-06-23 00:00:00
 * @LastEditTime: 2026-06-23 00:00:00
 * @License: GPL 3.0
 */
#pragma once

#include "lvgl.h"

namespace lilygo_box::ui {

// 底部提示栏的面板和遮罩配置。
struct PromptSheetConfig {
  int screen_width = 0;
  int screen_height = 0;
  int sheet_width = 0;
  int sheet_height = 0;
  int side_margin = 0;
  int bottom_margin = 0;
  int sheet_radius = 0;
  uint32_t sheet_color = 0xFFFFFF;
  lv_opa_t overlay_opacity = 115;
};

// 底部提示栏按钮配置。
struct PromptSheetButtonConfig {
  const char* text = nullptr;
  int x = 0;
  int y = 0;
  int width = 0;
  int height = 0;
  int radius = 0;
  uint32_t background_color = 0xFFFFFF;
  uint32_t disabled_background_color = 0xFFFFFF;
  uint32_t pressed_background_color = 0xE5E5E5;
  uint32_t text_color = 0x000000;
  lv_opa_t pressed_opacity = LV_OPA_COVER;
  const lv_font_t* font = nullptr;
  lv_event_cb_t callback = nullptr;
  void* user_data = nullptr;
  bool enabled = true;
};

/**
 * @brief 创建底部提示栏遮罩层
 * @param parent 父对象
 * @param config 提示栏配置
 * @return 创建成功返回遮罩对象，否则返回 nullptr
 */
lv_obj_t* CreatePromptSheetOverlay(
    lv_obj_t* parent, const PromptSheetConfig& config);

/**
 * @brief 创建底部提示栏面板
 * @param overlay 遮罩对象
 * @param config 提示栏配置
 * @return 创建成功返回面板对象，否则返回 nullptr
 */
lv_obj_t* CreatePromptSheet(
    lv_obj_t* overlay, const PromptSheetConfig& config);

/**
 * @brief 创建底部提示栏文本标签
 * @param parent 父对象
 * @param text 标签文本
 * @param color 文本颜色
 * @param font 文本字体
 * @return 创建成功返回标签对象，否则返回 nullptr
 */
lv_obj_t* CreatePromptSheetLabel(lv_obj_t* parent, const char* text,
    uint32_t color, const lv_font_t* font);

/**
 * @brief 创建底部提示栏按钮
 * @param parent 父对象
 * @param config 按钮配置
 * @return 创建成功返回按钮对象，否则返回 nullptr
 */
lv_obj_t* CreatePromptSheetButton(
    lv_obj_t* parent, const PromptSheetButtonConfig& config);

/**
 * @brief 播放底部提示栏进入动画
 * @param sheet 面板对象
 * @param config 提示栏配置
 * @param duration_ms 动画时长
 */
void AnimatePromptSheetIn(
    lv_obj_t* sheet, const PromptSheetConfig& config, uint32_t duration_ms);

/**
 * @brief 播放底部提示栏退出动画并在结束后删除遮罩层
 * @param overlay 遮罩对象
 * @param sheet 面板对象
 * @param duration_ms 动画时长
 * @return 动画启动成功返回 true，否则返回 false
 */
bool AnimatePromptSheetOut(
    lv_obj_t* overlay, lv_obj_t* sheet, uint32_t duration_ms);

}  // namespace lilygo_box::ui
