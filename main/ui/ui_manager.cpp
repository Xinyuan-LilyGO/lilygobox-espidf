/*
 * @Description: None
 * @Author: LILYGO_L
 * @Date: 2026-05-10 13:27:05
 * @LastEditTime: 2026-05-16 17:14:31
 * @License: GPL 3.0
 */
#include "ui/ui_manager.h"

#include <algorithm>
#include <cstdint>
#include <cstring>

#include "app/app_catalog.h"
#include "ui/app_view_factory.h"
#include "ui/font/font_assets.h"
#include "ui/font/material_symbols_assets.h"
#include "ui/icon/icon_assets.h"
#include "ui/input/app_view_gesture_flags.h"
#include "ui/input/edge_back_gesture.h"
#include "ui/input/press_cancel.h"
#include "ui/animation/transition_animation.h"

namespace lilygo_box::ui {
namespace {

constexpr int kHorizontalPadding = 10;
constexpr int kClockTop = 90;
constexpr int kAppIconSize = 98;
constexpr int kIconCellExtraWidth = 12;
constexpr int kIconPressedMargin = 5;
constexpr int kIconPressedShrink = 8;
constexpr int kIconRadius = 24;
constexpr int kInnerIconSurfaceSize = 82;
constexpr int kInnerIconSurfacePressedShrink = 6;
constexpr int kInnerIconSurfaceInset =
    (kAppIconSize - kInnerIconSurfaceSize) / 2;
constexpr int kInnerIconSurfaceRadius = kIconRadius - kInnerIconSurfaceInset;
constexpr int kInnerImageOffsetX = -1;
constexpr int kInnerImageOffsetY = -3;
constexpr uint32_t kIconPressAnimationMs = 90;
constexpr uint32_t kIconReleaseAnimationMs = 100;
constexpr int kIconLabelGap = 6;
constexpr int kIconLabelHeight = 34;
constexpr int kHomeAppColumns = 4;
constexpr int kDockColumns = 3;
constexpr int kAppRowGap = 30;
constexpr int kDockHeight = 160;
constexpr int kDockIconSize = 98;
constexpr uint32_t kIconGlowColor = 0x242424;
constexpr int kIconGlowWidth = 15;
constexpr int kIconPressedGlowWidth = 17;
constexpr int kIconGlowSpread = 0;
constexpr int kIconPressedGlowSpread = 0;
constexpr lv_opa_t kAppIconGlowOpacity = 116;
constexpr lv_opa_t kDockIconGlowOpacity = 108;
constexpr lv_opa_t kIconPressedGlowOpacity = 136;
constexpr int kDockTopPadding = 10;
constexpr int kDockInsetExtra = 40;
constexpr int kPageIndicatorBottom = kDockHeight + 8;
constexpr uint32_t kAppOpenFadeInMs = 45;
constexpr uint32_t kAppOpenFadeOutMs = 50;
constexpr uint32_t kAppOpenFadeCoverColor = 0xE2E2E2;
constexpr uint32_t kStartupProgressFullMs = 1000;
constexpr uint32_t kStartupProgressMinStepMs = 200;
constexpr uint32_t kStartupFadeOutMs = 220;
constexpr uint32_t kStartupBackgroundColor = 0xFFFFFF;
constexpr uint32_t kStartupTextColor = 0x111111;
constexpr uint32_t kStartupProgressTrackColor = 0xE8E8E8;
constexpr uint32_t kStartupProgressFillColor = 0x1C1C1C;
constexpr int kStartupProgressMaxWidth = 360;
constexpr int kStartupProgressWidthPercent = 54;
constexpr int kStartupProgressMinHeight = 6;
constexpr int kStartupProgressHeightDivisor = 150;
constexpr int kStartupProgressOffsetY = -30;
constexpr int kStartupTitleGap = 20;

struct IconStyle {
  const char* symbol;
  const lv_image_dsc_t* image;
  uint32_t shell_color;
  uint32_t surface_color;
  uint32_t pressed_shell_color;
  int image_offset_x;
  int image_offset_y;
};

struct DockIconEntry {
  const char* title;
  IconStyle style;
};

constexpr DockIconEntry kDockIconEntries[] = {
    {.title = "Camera",
        .style =
            {
                .symbol = nullptr,
                .image = &camera_inner_icon_68x68,
                .shell_color = 0xF2C051,
                .surface_color = 0xFBE995,
                .pressed_shell_color = 0xD69B36,
                .image_offset_x = 0,
                .image_offset_y = 0,
            }},
    {.title = "Settings",
        .style =
            {
                .symbol = nullptr,
                .image = &settings_inner_icon_68x68,
                .shell_color = 0x7D7D7D,
                .surface_color = 0xD1D1D1,
                .pressed_shell_color = 0x666666,
                .image_offset_x = 0,
                .image_offset_y = 0,
            }},
};

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
 * @brief 获取 32 号 Google Sans 字体
 * @return 字体指针
 */
const lv_font_t* Font32() { return &lvgl_font_google_sans_flex_32; }

/**
 * @brief 获取 56 号 Material Symbols 字体
 * @return 字体指针
 */
const lv_font_t* MaterialIconFont56() { return &lvgl_font_material_symbols_56; }

/**
 * @brief 获取桌面时间字体
 * @return 字体指针
 */
const lv_font_t* HomeTimeFont() { return &lvgl_font_lineseedkr_rg_120; }

/**
 * @brief 获取桌面日期字体
 * @return 字体指针
 */
const lv_font_t* HomeDateFont() { return &lvgl_font_lineseedkr_th_60; }

/**
 * @brief 计算应用图标单元格宽度
 * @return 单元格宽度
 */
int IconCellWidth() { return kAppIconSize + kIconCellExtraWidth; }

/**
 * @brief 计算应用图标单元格高度
 * @return 单元格高度
 */
int IconCellHeight() {
  return kIconPressedMargin + kAppIconSize + kIconLabelGap + kIconLabelHeight;
}

/**
 * @brief 根据项目数量和列数计算行数
 * @param item_count 项目数量
 * @param columns 列数
 * @return 行数
 */
int RowCount(size_t item_count, int columns) {
  if (item_count == 0) {
    return 0;
  }

  const int count = static_cast<int>(item_count);
  return (count + columns - 1) / columns;
}

/**
 * @brief 根据屏幕尺寸计算边缘缩进
 * @param screen_width 屏幕宽度
 * @param screen_height 屏幕高度
 * @return 边缘缩进
 */
int ScreenEdgeInset(int screen_width, int screen_height) {
  return std::max(8, std::min(screen_width, screen_height) / 25);
}

/**
 * @brief 限制图标网格水平缩进不超过可用空间
 * @param screen_width 屏幕宽度
 * @param requested_inset 请求缩进
 * @param columns 列数
 * @param cell_width 单元格宽度
 * @return 限制后的缩进
 */
int ClampInset(
    int screen_width, int requested_inset, int columns, int cell_width) {
  const int minimum_width = columns * cell_width;
  if (screen_width <= minimum_width) {
    return 0;
  }

  return std::min(requested_inset, (screen_width - minimum_width) / 2);
}

/**
 * @brief 计算图标网格列间距
 * @param screen_width 屏幕宽度
 * @param inset_x 水平缩进
 * @param columns 列数
 * @param cell_width 单元格宽度
 * @return 列间距
 */
int ColumnGap(int screen_width, int inset_x, int columns, int cell_width) {
  if (columns <= 1) {
    return 0;
  }

  const int used_width = 2 * inset_x + columns * cell_width;
  return std::max(0, (screen_width - used_width) / (columns - 1));
}

/**
 * @brief 计算桌面应用网格顶部位置
 * @param screen_height 屏幕高度
 * @return 顶部 Y 坐标
 */
int HomeGridTop(int screen_height) { return screen_height * 35 / 100; }

/**
 * @brief 组合 LVGL 样式选择器
 * @param part LVGL 部件
 * @param state LVGL 状态
 * @return 样式选择器
 */
constexpr lv_style_selector_t StyleSelector(lv_part_t part, lv_state_t state) {
  return static_cast<lv_style_selector_t>(part) |
         static_cast<lv_style_selector_t>(state);
}

/**
 * @brief 清除对象背景、边框和内边距
 * @param object LVGL 对象
 */
void MakeTransparent(lv_obj_t* object) {
  const lv_style_selector_t pressed_selector =
      StyleSelector(LV_PART_MAIN, LV_STATE_PRESSED);

  lv_obj_set_style_bg_opa(object, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_set_style_bg_opa(object, LV_OPA_TRANSP, pressed_selector);
  lv_obj_set_style_border_width(object, 0, LV_PART_MAIN);
  lv_obj_set_style_border_width(object, 0, pressed_selector);
  lv_obj_set_style_pad_all(object, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(object, 0, pressed_selector);
}

/**
 * @brief 设置图标外壳阴影样式
 * @param object LVGL 对象
 * @param opacity 阴影透明度
 */
void SetIconGlowStyle(lv_obj_t* object, lv_opa_t opacity) {
  lv_obj_set_style_shadow_width(object, kIconGlowWidth, LV_PART_MAIN);
  lv_obj_set_style_shadow_width(
      object, kIconPressedGlowWidth, LV_STATE_PRESSED);
  lv_obj_set_style_shadow_spread(object, kIconGlowSpread, LV_PART_MAIN);
  lv_obj_set_style_shadow_spread(
      object, kIconPressedGlowSpread, LV_STATE_PRESSED);
  lv_obj_set_style_shadow_offset_x(object, 0, LV_PART_MAIN);
  lv_obj_set_style_shadow_offset_x(object, 0, LV_STATE_PRESSED);
  lv_obj_set_style_shadow_offset_y(object, 0, LV_PART_MAIN);
  lv_obj_set_style_shadow_offset_y(object, 0, LV_STATE_PRESSED);
  lv_obj_set_style_shadow_color(
      object, lv_color_hex(kIconGlowColor), LV_PART_MAIN);
  lv_obj_set_style_shadow_color(
      object, lv_color_hex(kIconGlowColor), LV_STATE_PRESSED);
  lv_obj_set_style_shadow_opa(object, opacity, LV_PART_MAIN);
  lv_obj_set_style_shadow_opa(
      object, kIconPressedGlowOpacity, LV_STATE_PRESSED);
}

/**
 * @brief 清除主题默认的按下变形效果
 * @param object LVGL 对象
 */
void ClearThemePressedGrow(lv_obj_t* object) {
  lv_obj_set_style_transform_width(object, 0, LV_STATE_PRESSED);
  lv_obj_set_style_transform_height(object, 0, LV_STATE_PRESSED);
}

/**
 * @brief 根据收缩值计算按下状态尺寸
 * @param normal_size 正常尺寸
 * @param shrink_size 收缩尺寸
 * @return 按下状态尺寸
 */
int PressedSize(int normal_size, int shrink_size) {
  return normal_size - shrink_size;
}

/**
 * @brief 计算尺寸变化后的中心偏移
 * @param normal_size 正常尺寸
 * @param pressed_size 按下状态尺寸
 * @return 中心偏移
 */
int CenterOffset(int normal_size, int pressed_size) {
  return (normal_size - pressed_size) / 2;
}

/**
 * @brief 设置图标按钮尺寸并保持视觉居中
 * @param object LVGL 对象
 * @param normal_size 正常尺寸
 * @param size 目标尺寸
 */
void SetIconButtonSize(lv_obj_t* object, int normal_size, int size) {
  const int offset = CenterOffset(normal_size, size);
  lv_obj_set_size(object, size, size);
  lv_obj_align(object, LV_ALIGN_TOP_MID, 0, kIconPressedMargin + offset);
}

/**
 * @brief 设置内部图像背景面的尺寸
 * @param surface 内部图像背景面
 * @param size 目标尺寸
 */
void SetInnerImageSurfaceSize(lv_obj_t* surface, int size) {
  lv_obj_set_size(surface, size, size);
  lv_obj_center(surface);
}

/**
 * @brief 应用图标按钮尺寸动画回调
 * @param object LVGL 对象
 * @param size 目标尺寸
 */
void AppIconButtonSizeAnimCallback(void* object, int32_t size) {
  SetIconButtonSize(static_cast<lv_obj_t*>(object), kAppIconSize, size);
}

/**
 * @brief Dock 图标按钮尺寸动画回调
 * @param object LVGL 对象
 * @param size 目标尺寸
 */
void DockIconButtonSizeAnimCallback(void* object, int32_t size) {
  SetIconButtonSize(static_cast<lv_obj_t*>(object), kDockIconSize, size);
}

/**
 * @brief 内部图像背景面尺寸动画回调
 * @param object LVGL 对象
 * @param size 目标尺寸
 */
void InnerImageSurfaceSizeAnimCallback(void* object, int32_t size) {
  SetInnerImageSurfaceSize(static_cast<lv_obj_t*>(object), size);
}

/**
 * @brief 启动尺寸动画
 * @param object LVGL 对象
 * @param target_size 目标尺寸
 * @param callback 动画执行回调
 * @param pressed 是否为按下状态
 */
void StartSizeAnimation(lv_obj_t* object, int target_size,
    lv_anim_exec_xcb_t callback, bool pressed) {
  const int current_size = lv_obj_get_width(object);
  if (current_size == target_size) {
    callback(object, target_size);
    return;
  }

  lv_anim_t animation;
  lv_anim_init(&animation);
  lv_anim_set_var(&animation, object);
  lv_anim_set_values(&animation, current_size, target_size);
  lv_anim_set_duration(
      &animation, pressed ? kIconPressAnimationMs : kIconReleaseAnimationMs);
  lv_anim_set_path_cb(&animation, lv_anim_path_ease_out);
  lv_anim_set_exec_cb(&animation, callback);
  lv_anim_start(&animation);
}

/**
 * @brief 根据按压事件刷新图标按下反馈
 * @param event LVGL 事件
 * @param normal_size 正常尺寸
 * @param icon_callback 图标尺寸动画回调
 */
void UpdatePressedFeedback(
    lv_event_t* event, int normal_size, lv_anim_exec_xcb_t icon_callback) {
  const lv_event_code_t code = lv_event_get_code(event);
  lv_obj_t* object = lv_event_get_target_obj(event);
  if (object == nullptr) {
    return;
  }

  const bool pressed = code == LV_EVENT_PRESSED;
  const bool press_cancelled =
      code == LV_EVENT_PRESSING && !IsPointerInsideObject(object);
  const bool released =
      code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST ||
      press_cancelled;
  if (!pressed && !released) {
    return;
  }

  const int icon_target =
      pressed ? PressedSize(normal_size, kIconPressedShrink) : normal_size;
  StartSizeAnimation(object, icon_target, icon_callback, pressed);

  lv_obj_t* surface = static_cast<lv_obj_t*>(lv_event_get_user_data(event));
  if (surface != nullptr) {
    const int surface_target = pressed ? PressedSize(kInnerIconSurfaceSize,
                                             kInnerIconSurfacePressedShrink)
                                       : kInnerIconSurfaceSize;
    StartSizeAnimation(
        surface, surface_target, InnerImageSurfaceSizeAnimCallback, pressed);
  }
}

/**
 * @brief 应用图标按压事件回调
 * @param event LVGL 事件
 */
void AppIconPressedEventCallback(lv_event_t* event) {
  UpdatePressedFeedback(event, kAppIconSize, AppIconButtonSizeAnimCallback);
}

/**
 * @brief Dock 图标按压事件回调
 * @param event LVGL 事件
 */
void DockIconPressedEventCallback(lv_event_t* event) {
  UpdatePressedFeedback(event, kDockIconSize, DockIconButtonSizeAnimCallback);
}

/**
 * @brief 设置内部图像图标外壳样式
 * @param object LVGL 对象
 * @param style 图标样式
 */
void SetInnerImageShellStyle(lv_obj_t* object, const IconStyle& style) {
  lv_obj_set_style_radius(object, kIconRadius, LV_PART_MAIN);
  lv_obj_set_style_radius(object, kIconRadius, LV_STATE_PRESSED);
  lv_obj_set_style_bg_opa(object, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_bg_color(
      object, lv_color_hex(style.shell_color), LV_PART_MAIN);
  lv_obj_set_style_bg_grad_dir(object, LV_GRAD_DIR_NONE, LV_PART_MAIN);
  lv_obj_set_style_bg_color(
      object, lv_color_hex(style.pressed_shell_color), LV_STATE_PRESSED);
  lv_obj_set_style_bg_grad_dir(object, LV_GRAD_DIR_NONE, LV_STATE_PRESSED);
  lv_obj_set_style_border_width(object, 0, LV_PART_MAIN);
  lv_obj_set_style_border_width(object, 0, LV_STATE_PRESSED);
  lv_obj_set_style_pad_all(object, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(object, 0, LV_STATE_PRESSED);
}

/**
 * @brief 创建内部图像背景面
 * @param parent 父对象
 * @param color 背景颜色
 * @return 创建成功返回对象指针，否则返回 nullptr
 */
lv_obj_t* CreateInnerImageSurface(lv_obj_t* parent, uint32_t color) {
  lv_obj_t* surface = lv_obj_create(parent);
  if (surface == nullptr) {
    return nullptr;
  }

  lv_obj_remove_flag(surface, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(surface, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_flag(surface, LV_OBJ_FLAG_GESTURE_BUBBLE);
  lv_obj_set_size(surface, kInnerIconSurfaceSize, kInnerIconSurfaceSize);
  lv_obj_center(surface);
  lv_obj_set_style_radius(surface, kInnerIconSurfaceRadius, LV_PART_MAIN);
  lv_obj_set_style_radius(surface, kInnerIconSurfaceRadius, LV_STATE_PRESSED);
  lv_obj_set_style_bg_opa(surface, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_bg_color(surface, lv_color_hex(color), LV_PART_MAIN);
  lv_obj_set_style_bg_color(surface, lv_color_hex(color), LV_STATE_PRESSED);
  lv_obj_set_style_border_width(surface, 0, LV_PART_MAIN);
  lv_obj_set_style_border_width(surface, 0, LV_STATE_PRESSED);
  lv_obj_set_style_pad_all(surface, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(surface, 0, LV_STATE_PRESSED);
  return surface;
}

/**
 * @brief 创建默认字体文本标签
 * @param parent 父对象
 * @param text 显示文本
 * @param color 文本颜色
 * @return 创建成功返回对象指针，否则返回 nullptr
 */
lv_obj_t* CreateLabel(lv_obj_t* parent, const char* text, lv_color_t color) {
  lv_obj_t* label = lv_label_create(parent);
  if (label == nullptr) {
    return nullptr;
  }

  lv_label_set_text(label, text);
  SetTextStyle(label, color, Font24());
  return label;
}

/**
 * @brief 根据应用条目获取图标样式
 * @param app_entry 应用条目
 * @return 图标样式
 */
IconStyle GetIconStyle(const app::AppEntry& app_entry) {
  if (IsId(app_entry.id, "cit")) {
    return {
        .symbol = nullptr,
        .image = &cit_inner_icon_56x68,
        .shell_color = 0x3F3F3F,
        .surface_color = 0x939391,
        .pressed_shell_color = 0x303030,
        .image_offset_x = kInnerImageOffsetX,
        .image_offset_y = kInnerImageOffsetY,
    };
  }

  if (IsId(app_entry.id, "rf")) {
    return {
        .symbol = nullptr,
        .image = &rf_inner_icon_68x68,
        .shell_color = 0x554890,
        .surface_color = 0xA69CDB,
        .pressed_shell_color = 0x443971,
        .image_offset_x = 0,
        .image_offset_y = 0,
    };
  }

  if (IsId(app_entry.id, "music")) {
    return {
        .symbol = nullptr,
        .image = &music_inner_icon_68x68,
        .shell_color = 0xC45252,
        .surface_color = 0xEC8F88,
        .pressed_shell_color = 0xA94343,
        .image_offset_x = 0,
        .image_offset_y = -4,
    };
  }

  return {
      .symbol = icon::kHome,
      .image = nullptr,
      .shell_color = 0x4CAF50,
      .surface_color = 0x8BC34A,
      .pressed_shell_color = 0x2E7D32,
      .image_offset_x = 0,
      .image_offset_y = 0,
  };
}

/**
 * @brief 创建圆形装饰对象
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
  lv_obj_add_flag(circle, LV_OBJ_FLAG_GESTURE_BUBBLE);
  lv_obj_set_size(circle, size, size);
  lv_obj_set_style_radius(circle, size / 2, LV_PART_MAIN);
  lv_obj_set_style_bg_color(circle, lv_color_hex(color), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(circle, opacity, LV_PART_MAIN);
  lv_obj_set_style_border_width(circle, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(circle, 0, LV_PART_MAIN);
  lv_obj_align(circle, align, x, y);
  return circle;
}

/**
 * @brief 创建壁纸层级圆形对象
 * @param parent 父对象
 * @param size 圆形尺寸
 * @param x X 偏移
 * @param y Y 偏移
 * @param align 对齐方式
 * @param color 填充颜色
 * @param opacity 透明度
 * @return 创建成功返回对象指针，否则返回 nullptr
 */
lv_obj_t* CreateToneCircle(lv_obj_t* parent, int size, int x, int y,
    lv_align_t align, uint32_t color, lv_opa_t opacity) {
  return CreateCircle(parent, size, x, y, align, color, opacity);
}

/**
 * @brief 创建桌面壁纸对象
 * @param parent 父对象
 */
void CreateWallpaperObjects(lv_obj_t* parent) {
  CreateToneCircle(
      parent, 1120, 0, 70, LV_ALIGN_TOP_MID, 0xDCDCDC, LV_OPA_COVER);
  CreateToneCircle(
      parent, 1000, 0, 140, LV_ALIGN_TOP_MID, 0xC8C8C8, LV_OPA_COVER);

  CreateToneCircle(
      parent, 940, 0, 300, LV_ALIGN_TOP_MID, 0xB7B7B7, LV_OPA_COVER);

  CreateToneCircle(
      parent, 1040, 0, 640, LV_ALIGN_BOTTOM_MID, 0x9F9F9F, LV_OPA_COVER);
}

}  // namespace

struct UiManager::AppOpenTransitionState {
  UiManager* manager = nullptr;
  const app::AppEntry* app_entry = nullptr;
  lv_obj_t* cover = nullptr;
};

bool UiManager::Init(hal::ScreenProvider* screen,
    hal::DeviceDiagnosticsProvider* diagnostics,
    hal::GpsProvider* gps,
    hal::AudioProvider* audio,
    hal::HapticProvider* haptic,
    hal::BmuProvider* bmu,
    hal::RtcProvider* rtc,
    hal::ImuProvider* imu,
    hal::EthernetProvider* ethernet,
    hal::WifiProvider* wifi) {
  if (screen == nullptr) {
    return false;
  }
  screen_ = screen;
  diagnostics_provider_ = diagnostics;
  gps_provider_ = gps;
  audio_provider_ = audio;
  haptic_provider_ = haptic;
  bmu_provider_ = bmu;
  rtc_provider_ = rtc;
  imu_provider_ = imu;
  ethernet_provider_ = ethernet;
  wifi_provider_ = wifi;

  root_screen_ = lv_obj_create(nullptr);
  if (root_screen_ == nullptr) {
    return false;
  }

  lv_obj_remove_flag(root_screen_, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(root_screen_, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
  lv_obj_set_style_bg_color(root_screen_, lv_color_hex(0xE2E2E2), LV_PART_MAIN);
  lv_obj_set_style_border_width(root_screen_, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(root_screen_, 0, LV_PART_MAIN);
  lv_obj_add_event_cb(
      root_screen_, GestureEventCallback, LV_EVENT_GESTURE, this);

  CreateWallpaperObjects(root_screen_);
  launcher_container_ = CreateLauncher(root_screen_);
  if (launcher_container_ == nullptr) {
    return false;
  }

  if (!status_bar_.Init(root_screen_, screen_->ScreenWidth())) {
    return false;
  }
  status_bar_.MoveToTop();

  startup_screen_ = CreateStartupScreen(root_screen_);
  if (startup_screen_ == nullptr) {
    return false;
  }

  lv_screen_load(root_screen_);
  return true;
}

bool UiManager::StartStartupScreenAnimation() {
  if (startup_screen_ == nullptr || startup_progress_fill_ == nullptr) {
    return false;
  }

  lv_obj_clear_flag(startup_screen_, LV_OBJ_FLAG_HIDDEN);
  lv_obj_move_to_index(startup_screen_, -1);
  lv_obj_set_style_opa(startup_screen_, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_width(startup_progress_fill_, 1);
  lv_anim_delete(this, SetStartupProgressWidth);
  startup_progress_percent_ = 0;
  startup_progress_target_percent_ = 0;
  startup_progress_pending_percent_ = 0;
  startup_progress_animating_ = false;
  lv_obj_invalidate(startup_screen_);
  return true;
}

bool UiManager::SetStartupScreenProgress(int percent) {
  if (startup_screen_ == nullptr || startup_progress_fill_ == nullptr) {
    return false;
  }

  const int clamped_percent = std::clamp(percent, 0, 100);
  lv_obj_t* track = lv_obj_get_parent(startup_progress_fill_);
  if (track == nullptr) {
    return false;
  }

  if (clamped_percent <= startup_progress_percent_ &&
      !startup_progress_animating_) {
    return true;
  }

  if (startup_progress_animating_) {
    startup_progress_pending_percent_ =
        std::max(startup_progress_pending_percent_, clamped_percent);
    return true;
  }

  return StartStartupProgressAnimation(clamped_percent);
}

void UiManager::AppButtonEventCallback(lv_event_t* event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
    return;
  }

  auto* context = static_cast<AppButtonContext*>(lv_event_get_user_data(event));
  if (context == nullptr || context->manager == nullptr ||
      context->app_entry == nullptr) {
    return;
  }

  lv_timer_t* timer = lv_timer_create(
      AppButtonOpenDelayCallback, kIconReleaseAnimationMs, context);
  if (timer == nullptr) {
    context->manager->ShowAppView(*context->app_entry);
    return;
  }

  lv_timer_set_repeat_count(timer, 1);
}

void UiManager::AppButtonOpenDelayCallback(lv_timer_t* timer) {
  auto* context =
      static_cast<AppButtonContext*>(lv_timer_get_user_data(timer));
  if (context == nullptr || context->manager == nullptr ||
      context->app_entry == nullptr) {
    return;
  }

  context->manager->ShowAppView(*context->app_entry);
}

void UiManager::BackButtonEventCallback(lv_event_t* event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
    return;
  }

  auto* self = static_cast<UiManager*>(lv_event_get_user_data(event));
  if (self != nullptr) {
    self->ShowLauncher();
  }
}

void UiManager::GestureEventCallback(lv_event_t* event) {
  if (lv_event_get_code(event) != LV_EVENT_GESTURE) {
    return;
  }

  auto* self = static_cast<UiManager*>(lv_event_get_user_data(event));
  if (self == nullptr || self->active_view_container_ == nullptr ||
      self->screen_ == nullptr) {
    return;
  }

  lv_indev_t* indev = lv_indev_active();
  if (indev == nullptr) {
    return;
  }

  const lv_dir_t direction = lv_indev_get_gesture_dir(indev);
  if (direction != LV_DIR_LEFT && direction != LV_DIR_RIGHT) {
    return;
  }

  BackGestureInfo gesture;
  if (!ReadBackGestureInfo(indev, &gesture) ||
      !IsBackGestureFromEdge(gesture, self->screen_->ScreenWidth())) {
    return;
  }

  if (lv_obj_has_flag(
          self->active_view_container_, kBlockLauncherGestureFlag)) {
    lv_event_stop_bubbling(event);
    lv_event_stop_processing(event);
    return;
  }

  if (lv_obj_has_flag(
          self->active_view_container_, kSuppressNextLauncherGestureFlag)) {
    lv_obj_remove_flag(
        self->active_view_container_, kSuppressNextLauncherGestureFlag);
    lv_event_stop_bubbling(event);
    lv_event_stop_processing(event);
    return;
  }

  self->ShowLauncher();
  lv_event_stop_bubbling(event);
}

void UiManager::PageScrollEventCallback(lv_event_t* event) {
  const lv_event_code_t code = lv_event_get_code(event);
  if (code != LV_EVENT_SCROLL_END) {
    return;
  }

  auto* self = static_cast<UiManager*>(lv_event_get_user_data(event));
  if (self == nullptr || self->page_scroller_ == nullptr ||
      self->screen_ == nullptr) {
    return;
  }

  const int scroll_x =
      static_cast<int>(lv_obj_get_scroll_x(self->page_scroller_));
  const size_t page_index = scroll_x >= self->screen_->ScreenWidth() / 2 ? 1 : 0;
  self->UpdatePageIndicator(page_index);
}

void UiManager::AppOpenFadeInCompletedCallback(lv_anim_t* animation) {
  auto* state = static_cast<AppOpenTransitionState*>(
      lv_anim_get_user_data(animation));
  if (state == nullptr || state->manager == nullptr) {
    return;
  }

  UiManager* self = state->manager;
  if (self->app_open_transition_state_ != state) {
    return;
  }

  if (!self->CreateActiveAppView(*state->app_entry)) {
    self->ShowLauncher();
    return;
  }

  if (self->launcher_container_ != nullptr) {
    lv_obj_add_flag(self->launcher_container_, LV_OBJ_FLAG_HIDDEN);
  }

  if (state->cover != nullptr) {
    lv_obj_move_to_index(state->cover, -1);
    self->status_bar_.MoveToTop();
  }

  if (!self->StartAppOpenCoverFade(state, LV_OPA_COVER, LV_OPA_TRANSP,
          kAppOpenFadeOutMs, AppOpenFadeOutCompletedCallback)) {
    self->FinishAppOpenTransition(state);
  }
}

void UiManager::AppOpenFadeOutCompletedCallback(lv_anim_t* animation) {
  auto* state = static_cast<AppOpenTransitionState*>(
      lv_anim_get_user_data(animation));
  if (state == nullptr || state->manager == nullptr) {
    return;
  }

  state->manager->FinishAppOpenTransition(state);
}

void UiManager::AppCloseFadeInCompletedCallback(lv_anim_t* animation) {
  auto* state = static_cast<AppOpenTransitionState*>(
      lv_anim_get_user_data(animation));
  if (state == nullptr || state->manager == nullptr) {
    return;
  }

  UiManager* self = state->manager;
  if (self->app_open_transition_state_ != state) {
    return;
  }

  if (self->active_view_container_ != nullptr) {
    lv_obj_delete(self->active_view_container_);
    self->active_view_container_ = nullptr;
  }

  if (self->launcher_container_ != nullptr) {
    lv_obj_remove_flag(self->launcher_container_, LV_OBJ_FLAG_HIDDEN);
  }

  if (state->cover != nullptr) {
    lv_obj_move_to_index(state->cover, -1);
    self->status_bar_.MoveToTop();
  }

  if (!self->StartAppOpenCoverFade(state, LV_OPA_COVER, LV_OPA_TRANSP,
          kAppOpenFadeOutMs, AppCloseFadeOutCompletedCallback)) {
    self->CancelAppOpenTransition();
  }
}

void UiManager::AppCloseFadeOutCompletedCallback(lv_anim_t* animation) {
  auto* state = static_cast<AppOpenTransitionState*>(
      lv_anim_get_user_data(animation));
  if (state == nullptr || state->manager == nullptr) {
    return;
  }

  state->manager->CancelAppOpenTransition();
}

void UiManager::FinishAppOpenTransition(AppOpenTransitionState* state) {
  if (state == nullptr || app_open_transition_state_ != state) {
    return;
  }

  if (root_screen_ != nullptr && active_view_container_ != nullptr) {
    lv_obj_set_pos(active_view_container_, 0, 0);
    lv_obj_set_size(
        active_view_container_, screen_->ScreenWidth(), screen_->ScreenHeight());
    lv_obj_move_to_index(active_view_container_, -1);
    status_bar_.MoveToTop();
  }

  if (launcher_container_ != nullptr && active_view_container_ != nullptr) {
    lv_obj_add_flag(launcher_container_, LV_OBJ_FLAG_HIDDEN);
  }

  CancelAppOpenTransition();
}

lv_obj_t* UiManager::CreateLauncher(lv_obj_t* parent) {
  lv_obj_t* launcher = lv_obj_create(parent);
  if (launcher == nullptr) {
    return nullptr;
  }

  lv_obj_remove_flag(launcher, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(launcher, LV_OBJ_FLAG_GESTURE_BUBBLE);
  MakeTransparent(launcher);
  lv_obj_set_size(launcher, screen_->ScreenWidth(), screen_->ScreenHeight());
  lv_obj_align(launcher, LV_ALIGN_CENTER, 0, 0);

  page_scroller_ = CreatePageScroller(launcher);
  if (page_scroller_ == nullptr) {
    lv_obj_delete(launcher);
    return nullptr;
  }

  if (CreateDock(launcher) == nullptr) {
    lv_obj_delete(launcher);
    return nullptr;
  }

  page_indicator_ = CreatePageIndicator(launcher);
  if (page_indicator_ == nullptr) {
    lv_obj_delete(launcher);
    return nullptr;
  }

  UpdatePageIndicator(0);
  return launcher;
}

lv_obj_t* UiManager::CreatePageScroller(lv_obj_t* parent) {
  lv_obj_t* scroller = lv_obj_create(parent);
  if (scroller == nullptr) {
    return nullptr;
  }

  MakeTransparent(scroller);
  lv_obj_set_size(scroller, screen_->ScreenWidth(), screen_->ScreenHeight());
  lv_obj_align(scroller, LV_ALIGN_CENTER, 0, 0);
  lv_obj_set_scroll_dir(scroller, LV_DIR_HOR);
  lv_obj_set_scroll_snap_x(scroller, LV_SCROLL_SNAP_CENTER);
  lv_obj_set_scrollbar_mode(scroller, LV_SCROLLBAR_MODE_OFF);
  lv_obj_add_flag(scroller, LV_OBJ_FLAG_SCROLL_ONE);
  lv_obj_remove_flag(scroller, LV_OBJ_FLAG_SCROLL_ELASTIC);
  lv_obj_remove_flag(scroller, LV_OBJ_FLAG_SCROLL_MOMENTUM);
  lv_obj_set_style_anim_duration(scroller, 120, LV_PART_MAIN);
  lv_obj_add_event_cb(
      scroller, PageScrollEventCallback, LV_EVENT_SCROLL_END, this);

  home_page_ = lv_obj_create(scroller);
  if (home_page_ == nullptr) {
    lv_obj_delete(scroller);
    return nullptr;
  }
  lv_obj_remove_flag(home_page_, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(home_page_, LV_OBJ_FLAG_SNAPPABLE);
  MakeTransparent(home_page_);
  lv_obj_set_size(home_page_, screen_->ScreenWidth(), screen_->ScreenHeight());
  lv_obj_set_pos(home_page_, 0, 0);

  reserved_page_ = lv_obj_create(scroller);
  if (reserved_page_ == nullptr) {
    lv_obj_delete(scroller);
    return nullptr;
  }
  lv_obj_remove_flag(reserved_page_, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(reserved_page_, LV_OBJ_FLAG_SNAPPABLE);
  MakeTransparent(reserved_page_);
  lv_obj_set_size(reserved_page_, screen_->ScreenWidth(), screen_->ScreenHeight());
  lv_obj_set_pos(reserved_page_, screen_->ScreenWidth(), 0);

  if (CreateClockGroup(home_page_) == nullptr ||
      CreateAppGrid(home_page_) == nullptr) {
    lv_obj_delete(scroller);
    return nullptr;
  }

  lv_obj_update_snap(scroller, LV_ANIM_OFF);
  return scroller;
}

lv_obj_t* UiManager::CreateClockGroup(lv_obj_t* parent) {
  lv_obj_t* group = lv_obj_create(parent);
  if (group == nullptr) {
    return nullptr;
  }

  lv_obj_remove_flag(group, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(group, LV_OBJ_FLAG_GESTURE_BUBBLE);
  MakeTransparent(group);
  lv_obj_set_size(group, screen_->ScreenWidth() - 2 * kHorizontalPadding, 282);
  lv_obj_align(group, LV_ALIGN_TOP_LEFT, kHorizontalPadding, kClockTop);

  lv_obj_t* time_label = CreateLabel(group, "09:15", lv_color_hex(0xFFFFFF));
  if (time_label == nullptr) {
    lv_obj_delete(group);
    return nullptr;
  }
  SetTextStyle(time_label, lv_color_hex(0xFFFFFF), HomeTimeFont());
  lv_obj_set_size(time_label, 400, 110);
  lv_obj_set_style_text_opa(time_label, 245, LV_PART_MAIN);
  lv_obj_align(time_label, LV_ALIGN_TOP_LEFT, 0, 0);

  lv_obj_t* date_label =
      CreateLabel(group, "June 21th", lv_color_hex(0xFFFFFF));
  if (date_label == nullptr) {
    lv_obj_delete(group);
    return nullptr;
  }
  SetTextStyle(date_label, lv_color_hex(0xFFFFFF), HomeDateFont());
  lv_obj_set_size(date_label, 400, 70);
  lv_obj_set_style_text_opa(date_label, 220, LV_PART_MAIN);
  lv_obj_align(date_label, LV_ALIGN_TOP_LEFT, 10, 110);

  lv_obj_t* week_label = CreateLabel(group, "Sat", lv_color_hex(0xFFFFFF));
  if (week_label == nullptr) {
    lv_obj_delete(group);
    return nullptr;
  }
  SetTextStyle(week_label, lv_color_hex(0xFFFFFF), HomeDateFont());
  lv_obj_set_size(week_label, 400, 50);
  lv_obj_set_style_text_opa(week_label, 220, LV_PART_MAIN);
  lv_obj_align(week_label, LV_ALIGN_TOP_LEFT, 10, 172);
  return group;
}

lv_obj_t* UiManager::CreateAppGrid(lv_obj_t* parent) {
  lv_obj_t* grid = lv_obj_create(parent);
  if (grid == nullptr) {
    return nullptr;
  }

  lv_obj_remove_flag(grid, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(grid, LV_OBJ_FLAG_GESTURE_BUBBLE);
  lv_obj_add_flag(grid, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
  MakeTransparent(grid);

  const app::AppCatalog& app_catalog = app::GetAppCatalog();
  button_context_count_ = app_catalog.entry_count;
  if (button_context_count_ > button_contexts_.size()) {
    button_context_count_ = button_contexts_.size();
  }

  const int cell_width = IconCellWidth();
  const int cell_height = IconCellHeight();
  const int rows = RowCount(button_context_count_, kHomeAppColumns);
  const int row_gaps = std::max(0, rows - 1) * kAppRowGap;
  const int grid_height = rows * cell_height + row_gaps;
  const int inset_x = ClampInset(screen_->ScreenWidth(),
      ScreenEdgeInset(screen_->ScreenWidth(), screen_->ScreenHeight()), kHomeAppColumns,
      cell_width);
  const int column_gap =
      ColumnGap(screen_->ScreenWidth(), inset_x, kHomeAppColumns, cell_width);

  lv_obj_set_size(grid, screen_->ScreenWidth(), grid_height);
  lv_obj_align(grid, LV_ALIGN_TOP_LEFT, 0, HomeGridTop(screen_->ScreenHeight()));

  for (size_t i = 0; i < button_context_count_; ++i) {
    button_contexts_[i].manager = this;
    button_contexts_[i].app_entry = &app_catalog.entries[i];

    lv_obj_t* cell = CreateAppIcon(grid, &button_contexts_[i], cell_width);
    if (cell == nullptr) {
      lv_obj_delete(grid);
      return nullptr;
    }

    const int column = static_cast<int>(i % kHomeAppColumns);
    const int row = static_cast<int>(i / kHomeAppColumns);
    const int x = inset_x + column * (cell_width + column_gap);
    const int y = row * (cell_height + kAppRowGap);
    lv_obj_align(cell, LV_ALIGN_TOP_LEFT, x, y);
  }

  return grid;
}

lv_obj_t* UiManager::CreateAppIcon(
    lv_obj_t* parent, AppButtonContext* context, int cell_width) {
  if (context == nullptr || context->app_entry == nullptr) {
    return nullptr;
  }

  const IconStyle style = GetIconStyle(*context->app_entry);
  lv_obj_t* cell = lv_obj_create(parent);
  if (cell == nullptr) {
    return nullptr;
  }

  lv_obj_remove_flag(cell, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(cell, LV_OBJ_FLAG_GESTURE_BUBBLE);
  lv_obj_add_flag(cell, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
  MakeTransparent(cell);
  lv_obj_set_size(cell, cell_width, IconCellHeight());

  lv_obj_t* button = lv_button_create(cell);
  if (button == nullptr) {
    lv_obj_delete(cell);
    return nullptr;
  }
  lv_obj_add_flag(button, LV_OBJ_FLAG_GESTURE_BUBBLE);
  lv_obj_remove_flag(button, LV_OBJ_FLAG_PRESS_LOCK);
  ClearThemePressedGrow(button);
  lv_obj_set_size(button, kAppIconSize, kAppIconSize);
  SetInnerImageShellStyle(button, style);
  SetIconGlowStyle(button, kAppIconGlowOpacity);
  lv_obj_align(button, LV_ALIGN_TOP_MID, 0, kIconPressedMargin);
  if (!AddPressCancelOnLeave(button)) {
    lv_obj_delete(cell);
    return nullptr;
  }
  lv_obj_add_event_cb(
      button, AppButtonEventCallback, LV_EVENT_CLICKED, context);

  lv_obj_t* icon_parent = button;
  if (style.image != nullptr) {
    icon_parent = CreateInnerImageSurface(button, style.surface_color);
    if (icon_parent == nullptr) {
      lv_obj_delete(cell);
      return nullptr;
    }
  }
  lv_obj_add_event_cb(button, AppIconPressedEventCallback, LV_EVENT_ALL,
      icon_parent == button ? nullptr : icon_parent);

  lv_obj_t* icon = nullptr;
  if (style.image != nullptr) {
    icon = lv_image_create(icon_parent);
    if (icon != nullptr) {
      lv_image_set_src(icon, style.image);
    }
  } else if (style.symbol != nullptr) {
    icon = CreateLabel(button, style.symbol, lv_color_hex(0xFFFFFF));
    if (icon != nullptr) {
      SetTextStyle(icon, lv_color_hex(0xFFFFFF), MaterialIconFont56());
    }
  }

  if (icon == nullptr) {
    lv_obj_delete(cell);
    return nullptr;
  }
  if (style.image != nullptr) {
    lv_obj_align(
        icon, LV_ALIGN_CENTER, style.image_offset_x, style.image_offset_y);
  } else {
    lv_obj_center(icon);
  }

  lv_obj_t* title =
      CreateLabel(cell, context->app_entry->title, lv_color_hex(0xFFFFFF));
  if (title == nullptr) {
    lv_obj_delete(cell);
    return nullptr;
  }
  lv_obj_set_width(title, cell_width);
  SetTextStyle(title, lv_color_hex(0xFFFFFF), Font22());
  lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
  lv_obj_set_style_text_opa(title, 235, LV_PART_MAIN);
  lv_obj_align(title, LV_ALIGN_TOP_MID, 0,
      kIconPressedMargin + kAppIconSize + kIconLabelGap);
  return cell;
}

lv_obj_t* UiManager::CreateDock(lv_obj_t* parent) {
  lv_obj_t* dock = lv_obj_create(parent);
  if (dock == nullptr) {
    return nullptr;
  }

  lv_obj_remove_flag(dock, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(dock, LV_OBJ_FLAG_GESTURE_BUBBLE);
  lv_obj_add_flag(dock, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
  lv_obj_set_size(dock, screen_->ScreenWidth(), kDockHeight);
  lv_obj_align(dock, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_obj_set_style_bg_color(dock, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(dock, 28, LV_PART_MAIN);
  lv_obj_set_style_border_width(dock, 0, LV_PART_MAIN);
  lv_obj_set_style_radius(dock, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(dock, 0, LV_PART_MAIN);

  const int cell_width = IconCellWidth();
  const int inset_x = ClampInset(screen_->ScreenWidth(),
      ScreenEdgeInset(screen_->ScreenWidth(), screen_->ScreenHeight()) + kDockInsetExtra,
      kDockColumns, cell_width);
  const int column_gap =
      ColumnGap(screen_->ScreenWidth(), inset_x, kDockColumns, cell_width);

  for (size_t i = 0; i < sizeof(kDockIconEntries) / sizeof(kDockIconEntries[0]);
      ++i) {
    lv_obj_t* cell = CreateDockIcon(dock, i, cell_width);
    if (cell == nullptr) {
      lv_obj_delete(dock);
      return nullptr;
    }

    const int column = static_cast<int>(i % kDockColumns);
    const int x = inset_x + column * (cell_width + column_gap);
    lv_obj_align(cell, LV_ALIGN_TOP_LEFT, x, kDockTopPadding);
  }

  return dock;
}

lv_obj_t* UiManager::CreateDockIcon(
    lv_obj_t* parent, size_t entry_index, int cell_width) {
  if (entry_index >= sizeof(kDockIconEntries) / sizeof(kDockIconEntries[0])) {
    return nullptr;
  }

  const DockIconEntry& entry = kDockIconEntries[entry_index];
  const IconStyle& style = entry.style;
  lv_obj_t* cell = lv_obj_create(parent);
  if (cell == nullptr) {
    return nullptr;
  }

  lv_obj_remove_flag(cell, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(cell, LV_OBJ_FLAG_GESTURE_BUBBLE);
  lv_obj_add_flag(cell, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
  MakeTransparent(cell);
  lv_obj_set_size(cell, cell_width, IconCellHeight());

  lv_obj_t* icon_box = lv_button_create(cell);
  if (icon_box == nullptr) {
    lv_obj_delete(cell);
    return nullptr;
  }
  lv_obj_remove_flag(icon_box, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(icon_box, LV_OBJ_FLAG_GESTURE_BUBBLE);
  lv_obj_remove_flag(icon_box, LV_OBJ_FLAG_PRESS_LOCK);
  ClearThemePressedGrow(icon_box);
  lv_obj_set_size(icon_box, kDockIconSize, kDockIconSize);
  SetInnerImageShellStyle(icon_box, style);
  SetIconGlowStyle(icon_box, kDockIconGlowOpacity);
  lv_obj_align(icon_box, LV_ALIGN_TOP_MID, 0, kIconPressedMargin);
  if (!AddPressCancelOnLeave(icon_box)) {
    lv_obj_delete(cell);
    return nullptr;
  }

  lv_obj_t* icon_parent = icon_box;
  if (style.image != nullptr) {
    icon_parent = CreateInnerImageSurface(icon_box, style.surface_color);
    if (icon_parent == nullptr) {
      lv_obj_delete(cell);
      return nullptr;
    }
  }
  lv_obj_add_event_cb(icon_box, DockIconPressedEventCallback, LV_EVENT_ALL,
      icon_parent == icon_box ? nullptr : icon_parent);

  lv_obj_t* icon = nullptr;
  if (style.image != nullptr) {
    icon = lv_image_create(icon_parent);
    if (icon != nullptr) {
      lv_image_set_src(icon, style.image);
    }
  } else if (style.symbol != nullptr) {
    icon = CreateLabel(icon_box, style.symbol, lv_color_hex(0xFFFFFF));
    if (icon != nullptr) {
      SetTextStyle(icon, lv_color_hex(0xFFFFFF), MaterialIconFont56());
    }
  }

  if (icon == nullptr) {
    lv_obj_delete(cell);
    return nullptr;
  }
  if (style.image != nullptr) {
    lv_obj_align(
        icon, LV_ALIGN_CENTER, style.image_offset_x, style.image_offset_y);
  } else {
    lv_obj_center(icon);
  }

  lv_obj_t* title_label =
      CreateLabel(cell, entry.title, lv_color_hex(0xFFFFFF));
  if (title_label == nullptr) {
    lv_obj_delete(cell);
    return nullptr;
  }
  lv_obj_set_width(title_label, cell_width);
  SetTextStyle(title_label, lv_color_hex(0xFFFFFF), Font22());
  lv_obj_set_style_text_align(title_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
  lv_obj_set_style_text_opa(title_label, 235, LV_PART_MAIN);
  lv_obj_align(title_label, LV_ALIGN_TOP_MID, 0,
      kIconPressedMargin + kDockIconSize + kIconLabelGap);
  return cell;
}

lv_obj_t* UiManager::CreatePageIndicator(lv_obj_t* parent) {
  lv_obj_t* indicator = lv_obj_create(parent);
  if (indicator == nullptr) {
    return nullptr;
  }

  lv_obj_remove_flag(indicator, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(indicator, LV_OBJ_FLAG_GESTURE_BUBBLE);
  MakeTransparent(indicator);
  lv_obj_set_size(indicator, 48, 18);
  lv_obj_align(indicator, LV_ALIGN_BOTTOM_MID, 0, -kPageIndicatorBottom);

  first_page_dot_ =
      CreateCircle(indicator, 12, -10, 0, LV_ALIGN_CENTER, 0xFFFFFF, 240);
  second_page_dot_ =
      CreateCircle(indicator, 12, 10, 0, LV_ALIGN_CENTER, 0xFFFFFF, 110);
  if (first_page_dot_ == nullptr || second_page_dot_ == nullptr) {
    lv_obj_delete(indicator);
    first_page_dot_ = nullptr;
    second_page_dot_ = nullptr;
    return nullptr;
  }

  return indicator;
}

void UiManager::CancelAppOpenTransition() {
  if (app_open_transition_state_ == nullptr) {
    return;
  }

  if (app_open_transition_state_->cover != nullptr) {
    DeleteWindowTransition(
        app_open_transition_state_->cover, WindowTransitionMode::kFade);
    lv_obj_delete(app_open_transition_state_->cover);
    app_open_transition_state_->cover = nullptr;
  }

  delete app_open_transition_state_;
  app_open_transition_state_ = nullptr;
}

bool UiManager::StartAppOpenCoverFade(AppOpenTransitionState* state,
    int start_opacity, int end_opacity, uint32_t duration_ms,
    lv_anim_completed_cb_t completed_callback) {
  if (state == nullptr || state->cover == nullptr) {
    return false;
  }

  return StartFadeWindowTransition(state->cover, start_opacity, end_opacity,
      duration_ms, state, completed_callback);
}

lv_obj_t* UiManager::CreateAppTransitionCover() {
  if (root_screen_ == nullptr || screen_ == nullptr) {
    return nullptr;
  }

  lv_obj_t* cover = lv_obj_create(root_screen_);
  if (cover == nullptr) {
    return nullptr;
  }

  lv_obj_remove_flag(cover, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(cover, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_size(cover, screen_->ScreenWidth(), screen_->ScreenHeight());
  lv_obj_set_pos(cover, 0, 0);
  lv_obj_set_style_bg_color(
      cover, lv_color_hex(kAppOpenFadeCoverColor), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(cover, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_border_width(cover, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(cover, 0, LV_PART_MAIN);
  lv_obj_set_style_radius(cover, 0, LV_PART_MAIN);
  lv_obj_set_style_opa(cover, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_move_to_index(cover, -1);
  status_bar_.MoveToTop();
  return cover;
}

lv_obj_t* UiManager::CreateStartupScreen(lv_obj_t* parent) {
  if (parent == nullptr || screen_ == nullptr) {
    return nullptr;
  }

  lv_obj_t* startup = lv_obj_create(parent);
  if (startup == nullptr) {
    return nullptr;
  }

  lv_obj_remove_flag(startup, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(startup, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_size(startup, screen_->ScreenWidth(), screen_->ScreenHeight());
  lv_obj_set_pos(startup, 0, 0);
  lv_obj_set_style_bg_color(
      startup, lv_color_hex(kStartupBackgroundColor), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(startup, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_border_width(startup, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(startup, 0, LV_PART_MAIN);
  lv_obj_set_style_radius(startup, 0, LV_PART_MAIN);
  lv_obj_set_style_opa(startup, LV_OPA_COVER, LV_PART_MAIN);

  lv_obj_t* title = lv_label_create(startup);
  if (title == nullptr) {
    lv_obj_delete(startup);
    return nullptr;
  }
  lv_label_set_text(title, "LilygoBox");
  SetTextStyle(title, lv_color_hex(kStartupTextColor), Font32());

  const int progress_width = std::min(kStartupProgressMaxWidth,
      screen_->ScreenWidth() * kStartupProgressWidthPercent / 100);
  const int progress_height = std::max(
      kStartupProgressMinHeight,
      screen_->ScreenHeight() / kStartupProgressHeightDivisor);
  lv_obj_t* track = lv_obj_create(startup);
  if (track == nullptr) {
    lv_obj_delete(startup);
    return nullptr;
  }
  lv_obj_remove_flag(track, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(track, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_size(track, progress_width, progress_height);
  lv_obj_align(track, LV_ALIGN_CENTER, 0, kStartupProgressOffsetY);
  lv_obj_set_style_bg_color(
      track, lv_color_hex(kStartupProgressTrackColor), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(track, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_border_width(track, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(track, 0, LV_PART_MAIN);
  lv_obj_set_style_radius(track, progress_height / 2, LV_PART_MAIN);
  lv_obj_align_to(title, track, LV_ALIGN_OUT_TOP_MID, 0, -kStartupTitleGap);

  startup_progress_fill_ = lv_obj_create(track);
  if (startup_progress_fill_ == nullptr) {
    lv_obj_delete(startup);
    return nullptr;
  }
  lv_obj_remove_flag(startup_progress_fill_, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(startup_progress_fill_, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_size(startup_progress_fill_, 1, progress_height);
  lv_obj_set_pos(startup_progress_fill_, 0, 0);
  lv_obj_set_style_bg_color(startup_progress_fill_,
      lv_color_hex(kStartupProgressFillColor), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(startup_progress_fill_, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_border_width(startup_progress_fill_, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(startup_progress_fill_, 0, LV_PART_MAIN);
  lv_obj_set_style_radius(
      startup_progress_fill_, progress_height / 2, LV_PART_MAIN);

  lv_obj_move_to_index(startup, -1);
  return startup;
}

void UiManager::SetStartupProgressWidth(void* user_data, int32_t width) {
  auto* self = static_cast<UiManager*>(user_data);
  if (self == nullptr || self->startup_progress_fill_ == nullptr) {
    return;
  }

  lv_obj_set_width(self->startup_progress_fill_, width);
}

void UiManager::SetStartupScreenOpacity(void* user_data, int32_t opacity) {
  auto* self = static_cast<UiManager*>(user_data);
  if (self == nullptr || self->startup_screen_ == nullptr) {
    return;
  }

  lv_obj_set_style_opa(self->startup_screen_, opacity, LV_PART_MAIN);
}

void UiManager::StartupProgressCompletedCallback(lv_anim_t* animation) {
  auto* self = static_cast<UiManager*>(lv_anim_get_user_data(animation));
  if (self == nullptr || self->startup_screen_ == nullptr) {
    return;
  }

  self->startup_progress_percent_ = self->startup_progress_target_percent_;
  self->startup_progress_animating_ = false;

  if (self->startup_progress_pending_percent_ >
      self->startup_progress_percent_) {
    const int pending_percent = self->startup_progress_pending_percent_;
    self->startup_progress_pending_percent_ = 0;
    if (self->StartStartupProgressAnimation(pending_percent)) {
      return;
    }
  }

  if (self->startup_progress_percent_ >= 100 &&
      !self->StartStartupFadeOut()) {
    self->DestroyStartupScreen();
  }
}

void UiManager::StartupFadeCompletedCallback(lv_anim_t* animation) {
  auto* self = static_cast<UiManager*>(lv_anim_get_user_data(animation));
  if (self == nullptr) {
    return;
  }

  self->DestroyStartupScreen();
}

bool UiManager::StartStartupProgressAnimation(int target_percent) {
  if (startup_screen_ == nullptr || startup_progress_fill_ == nullptr) {
    return false;
  }

  lv_obj_t* track = lv_obj_get_parent(startup_progress_fill_);
  if (track == nullptr) {
    return false;
  }

  const int track_width = lv_obj_get_width(track);
  if (track_width <= 0) {
    return false;
  }

  const int clamped_percent = std::clamp(target_percent, 0, 100);
  if (clamped_percent <= startup_progress_percent_) {
    return true;
  }

  const int start_width = std::max(
      1, track_width * startup_progress_percent_ / 100);
  const int end_width = std::max(1, track_width * clamped_percent / 100);
  const int progress_delta = clamped_percent - startup_progress_percent_;
  const uint32_t duration_ms = std::max(kStartupProgressMinStepMs,
      kStartupProgressFullMs * static_cast<uint32_t>(progress_delta) / 100U);

  startup_progress_target_percent_ = clamped_percent;
  startup_progress_animating_ = true;
  lv_anim_delete(this, SetStartupProgressWidth);

  lv_anim_t animation;
  lv_anim_init(&animation);
  lv_anim_set_var(&animation, this);
  lv_anim_set_user_data(&animation, this);
  lv_anim_set_values(&animation, start_width, end_width);
  lv_anim_set_duration(&animation, duration_ms);
  lv_anim_set_path_cb(&animation, lv_anim_path_ease_out);
  lv_anim_set_exec_cb(&animation, SetStartupProgressWidth);
  lv_anim_set_completed_cb(&animation, StartupProgressCompletedCallback);
  if (lv_anim_start(&animation) == nullptr) {
    startup_progress_animating_ = false;
    return false;
  }
  return true;
}

bool UiManager::StartStartupFadeOut() {
  if (startup_screen_ == nullptr) {
    return false;
  }

  lv_anim_t animation;
  lv_anim_init(&animation);
  lv_anim_set_var(&animation, this);
  lv_anim_set_user_data(&animation, this);
  lv_anim_set_values(&animation, LV_OPA_COVER, LV_OPA_TRANSP);
  lv_anim_set_duration(&animation, kStartupFadeOutMs);
  lv_anim_set_exec_cb(&animation, SetStartupScreenOpacity);
  lv_anim_set_completed_cb(&animation, StartupFadeCompletedCallback);
  return lv_anim_start(&animation) != nullptr;
}

void UiManager::DestroyStartupScreen() {
  lv_anim_delete(this, SetStartupProgressWidth);
  if (startup_screen_ != nullptr) {
    lv_obj_delete(startup_screen_);
    startup_screen_ = nullptr;
  }
  startup_progress_fill_ = nullptr;
  startup_progress_percent_ = 0;
  startup_progress_target_percent_ = 0;
  startup_progress_pending_percent_ = 0;
  startup_progress_animating_ = false;
}

bool UiManager::CreateActiveAppView(const app::AppEntry& app_entry) {
  AppViewConfig config;
  config.width = screen_->ScreenWidth();
  config.height = screen_->ScreenHeight();
  config.screen = screen_;
  config.diagnostics = diagnostics_provider_;
  config.gps = gps_provider_;
  config.audio = audio_provider_;
  config.haptic = haptic_provider_;
  config.bmu = bmu_provider_;
  config.rtc = rtc_provider_;
  config.imu = imu_provider_;
  config.ethernet = ethernet_provider_;
  config.wifi = wifi_provider_;
  config.back_callback = BackButtonEventCallback;
  config.back_context = this;

  active_view_container_ = CreateAppView(root_screen_, app_entry, config);
  if (active_view_container_ == nullptr) {
    return false;
  }

  lv_obj_set_pos(active_view_container_, 0, 0);
  lv_obj_set_size(active_view_container_, screen_->ScreenWidth(), screen_->ScreenHeight());
  lv_obj_add_flag(active_view_container_, LV_OBJ_FLAG_GESTURE_BUBBLE);
  lv_obj_add_event_cb(
      active_view_container_, GestureEventCallback, LV_EVENT_GESTURE, this);
  status_bar_.MoveToTop();
  return true;
}

bool UiManager::ShowAppView(const app::AppEntry& app_entry) {
  if (root_screen_ == nullptr || launcher_container_ == nullptr) {
    return false;
  }

  CancelAppOpenTransition();
  lv_obj_remove_flag(launcher_container_, LV_OBJ_FLAG_HIDDEN);
  lv_obj_set_style_opa(launcher_container_, LV_OPA_COVER, LV_PART_MAIN);
  if (active_view_container_ != nullptr) {
    lv_obj_delete(active_view_container_);
    active_view_container_ = nullptr;
  }

  auto* state = new AppOpenTransitionState;
  state->manager = this;
  state->app_entry = &app_entry;
  state->cover = CreateAppTransitionCover();
  if (state->cover == nullptr) {
    delete state;
    return false;
  }

  app_open_transition_state_ = state;

  if (!StartAppOpenCoverFade(state, LV_OPA_TRANSP, LV_OPA_COVER,
          kAppOpenFadeInMs, AppOpenFadeInCompletedCallback)) {
    if (!CreateActiveAppView(app_entry)) {
      CancelAppOpenTransition();
      return false;
    }
    if (launcher_container_ != nullptr) {
      lv_obj_add_flag(launcher_container_, LV_OBJ_FLAG_HIDDEN);
    }
    FinishAppOpenTransition(state);
  }

  return true;
}

void UiManager::ShowLauncher() {
  CancelAppOpenTransition();

  if (active_view_container_ == nullptr || root_screen_ == nullptr ||
      launcher_container_ == nullptr) {
    if (launcher_container_ != nullptr) {
      lv_obj_remove_flag(launcher_container_, LV_OBJ_FLAG_HIDDEN);
    }
    return;
  }

  auto* state = new AppOpenTransitionState;
  state->manager = this;
  state->cover = CreateAppTransitionCover();
  if (state->cover == nullptr) {
    delete state;
    lv_obj_delete(active_view_container_);
    active_view_container_ = nullptr;
    lv_obj_remove_flag(launcher_container_, LV_OBJ_FLAG_HIDDEN);
    return;
  }

  app_open_transition_state_ = state;
  if (!StartAppOpenCoverFade(state, LV_OPA_TRANSP, LV_OPA_COVER,
          kAppOpenFadeInMs, AppCloseFadeInCompletedCallback)) {
    CancelAppOpenTransition();
    lv_obj_delete(active_view_container_);
    active_view_container_ = nullptr;
    lv_obj_remove_flag(launcher_container_, LV_OBJ_FLAG_HIDDEN);
  }
}

void UiManager::UpdatePageIndicator(size_t page_index) {
  page_index_ = page_index > 0 ? 1 : 0;

  if (first_page_dot_ != nullptr && second_page_dot_ != nullptr) {
    const lv_opa_t first_opa = page_index_ == 0 ? 240 : 110;
    const lv_opa_t second_opa = page_index_ == 0 ? 110 : 240;
    lv_obj_set_style_bg_opa(first_page_dot_, first_opa, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(second_page_dot_, second_opa, LV_PART_MAIN);
  }
}

}  // namespace lilygo_box::ui
