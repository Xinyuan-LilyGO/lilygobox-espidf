/*
 * @Description: 启动器布局、应用切换与系统覆盖层管理实现
 * @Author: LILYGO_L
 * @Date: 2026-05-10 13:27:05
 * @LastEditTime: 2026-07-30 18:00:00
 * @License: GPL 3.0
 */
#include "ui/ui_manager.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <utility>

#include "app/app_catalog.h"
#include "app/network_monitor.h"
#include "app/storage/input_method_storage.h"
#include "ui/app_view_factory.h"
#include "ui/haptic_feedback.h"
#include "ui/input/app_view_gesture_flags.h"
#include "ui/input/edge_back_gesture.h"
#include "ui/input/press_cancel.h"
#include "ui/resources/fonts/font_assets.h"
#include "ui/resources/fonts/icon_assets.h"
#include "ui/resources/images/image_assets.h"
#include "ui/views/first_boot_welcome_view.h"
#include "ui/views/lock_screen_view.h"
#include "ui/views/power_menu_view.h"
#include "ui/views/settings_view.h"
#include "ui/wallpaper.h"
#include "ui/widgets/brand_icon.h"
#include "ui/widgets/shared_keyboard.h"

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
constexpr int kMaxColumnGap = 40;
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
constexpr int kLandscapeClockWidth = 360;
constexpr int kPageIndicatorBottom = kDockHeight + 8;
constexpr uint32_t kStartupProgressFullMs = 1000;
constexpr uint32_t kStartupProgressMinStepMs = 200;
constexpr uint32_t kStartupFadeOutMs = 220;
constexpr uint32_t kFirstBootWelcomeFadeOutMs = 180;
constexpr uint32_t kSystemStatusRefreshPeriodMs = 1000;
constexpr uint32_t kStartupBackgroundColor = 0xFFFFFF;
constexpr uint32_t kStartupTextColor = 0x111111;
constexpr uint32_t kStartupProgressTrackColor = 0xE8E8E8;
constexpr uint32_t kStartupProgressFillColor = 0x1C1C1C;
constexpr uint32_t kStartupBlackBackgroundColor = 0x000000;
constexpr uint32_t kLowBatteryStartupTextColor = 0xFFFFFF;
constexpr uint32_t kPowerOffChargingColor = 0x27C769;
constexpr uint32_t kPowerOffChargingCriticalColor = 0xFF3B30;
constexpr int kPowerOffChargingBoltOffsetX = -2;
constexpr uint32_t kStatusBarLightTextColor = 0xFFFFFF;
constexpr int kStartupProgressMaxWidth = 360;
constexpr int kStartupProgressWidthPercent = 54;
constexpr int kStartupProgressMinHeight = 6;
constexpr int kStartupProgressHeightDivisor = 150;
constexpr int kStartupProgressOffsetY = -30;
constexpr int kStartupTitleGap = 20;
constexpr int kStartupBrandIconSize = 56;
constexpr int kStartupBrandIconGap = 14;
constexpr int kLowBatteryStartupIconOffsetY = -36;
constexpr int kLowBatteryStartupPercentGap = 18;
constexpr int kStartupBatteryFillMaxWidth = 40;
constexpr int kStartupBatteryFillMinVisibleWidth = 4;
constexpr int kStartupBatteryFillHeight = 20;
constexpr int kStartupBatteryFillOffsetX = 6;
constexpr int kStartupBatteryFillOffsetY = 0;
constexpr int kStartupBatteryFillRadius = 2;
constexpr int kKeyboardExpansionPromptSideMargin = 34;
constexpr int kKeyboardExpansionPromptHeight = 312;
constexpr int kKeyboardExpansionPromptRadius = 48;
constexpr int kKeyboardExpansionPromptInnerPadding = 32;
constexpr int kKeyboardExpansionPromptButtonHeight = 74;
constexpr int kKeyboardExpansionPromptButtonRadius = 24;
constexpr int kKeyboardExpansionPromptBottomMargin = 32;

struct IconStyle {
  const char* symbol;
  const lv_image_dsc_t* image;
  uint32_t shell_color;
  uint32_t surface_color;
  uint32_t pressed_shell_color;
  int image_offset_x;
  int image_offset_y;
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
 * @brief 获取 56 号 Material Symbols 字体
 * @return 字体指针
 */
const lv_font_t* MaterialOutlineIconFont56() {
  return &lvgl_font_material_symbols_outline_56;
}

/**
 * @brief 获取 32 号 Material Symbols 填充图标字体
 * @return 字体指针
 */
const lv_font_t* MaterialFillIconFont32() {
  return &lvgl_font_material_symbols_fill_32;
}

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
  if (screen_width > screen_height) {
    return screen_width / 5;
  }
  return std::max(8, std::min(screen_width, screen_height) / 25);
}

/**
 * @brief 根据屏幕横竖屏状态计算桌面应用图标列数
 * @param screen_width 屏幕宽度
 * @param screen_height 屏幕高度
 * @return 列数
 */
int AppColumnCount(int screen_width, int screen_height) {
  return screen_width > screen_height ? 5 : kHomeAppColumns;
}

/**
 * @brief 根据屏幕横竖屏状态计算 dock 图标列数
 * @param screen_width 屏幕宽度
 * @param screen_height 屏幕高度
 * @return 列数
 */
int DockColumnCount(int screen_width, int screen_height) {
  return screen_width > screen_height ? 4 : kDockColumns;
}

/**
 * @brief 根据屏幕横竖屏状态计算 dock 高度
 * @param screen_width 屏幕宽度
 * @param screen_height 屏幕高度
 * @return dock 高度
 */
int DockHeight(int screen_width, int screen_height) {
  return screen_width > screen_height
             ? IconCellHeight() + kDockTopPadding
             : kDockHeight;
}

/**
 * @brief 根据屏幕横竖屏状态计算页面指示器距底部偏移
 * @param screen_width 屏幕宽度
 * @param screen_height 屏幕高度
 * @return 距底部偏移量
 */
int PageIndicatorBottom(int screen_width, int screen_height) {
  return DockHeight(screen_width, screen_height) + 8;
}

/**
 * @brief 根据屏幕横竖屏状态计算时钟区域顶部偏移
 * @param screen_width 屏幕宽度
 * @param screen_height 屏幕高度
 * @return 顶部 Y 坐标
 */
int ClockTop(int screen_width, int screen_height) {
  return screen_width > screen_height ? 56 : kClockTop;
}

/**
 * @brief 根据屏幕横竖屏状态计算时钟区域高度
 * @param screen_width 屏幕宽度
 * @param screen_height 屏幕高度
 * @return 时钟区域高度
 */
int ClockGroupHeight(int screen_width, int screen_height) {
  return screen_width > screen_height ? std::max(230, screen_height * 38 / 100) : 282;
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
  return std::clamp(
      (screen_width - used_width) / (columns - 1), 0, kMaxColumnGap);
}

/**
 * @brief 计算桌面应用网格顶部位置
 * @param screen_height 屏幕高度
 * @return 顶部 Y 坐标
 */
/**
 * @brief 计算桌面应用网格顶部位置
 * @param screen_width 屏幕宽度
 * @param screen_height 屏幕高度
 * @return 顶部 Y 坐标
 */
int HomeGridTop(int screen_width, int screen_height) {
  if (screen_width > screen_height) {
    return ClockTop(screen_width, screen_height) +
           ClockGroupHeight(screen_width, screen_height) + 8;
  }
  return screen_height * 35 / 100;
}

/**
 * @brief 获取月份显示名称
 * @param month 月份，范围 1~12
 * @return 月份文本
 */
const char* MonthName(uint8_t month) {
  switch (month) {
    case 1:
      return "January";
    case 2:
      return "February";
    case 3:
      return "March";
    case 4:
      return "April";
    case 5:
      return "May";
    case 6:
      return "June";
    case 7:
      return "July";
    case 8:
      return "August";
    case 9:
      return "September";
    case 10:
      return "October";
    case 11:
      return "November";
    case 12:
      return "December";
    default:
      return "Unknown";
  }
}

/**
 * @brief 获取日期序数后缀
 * @param day 日期，范围 1~31
 * @return 日期后缀
 */
const char* DaySuffix(uint8_t day) {
  if (day >= 11 && day <= 13) {
    return "th";
  }

  switch (day % 10) {
    case 1:
      return "st";
    case 2:
      return "nd";
    case 3:
      return "rd";
    default:
      return "th";
  }
}

/**
 * @brief 获取星期显示名称
 * @param week 星期，范围 0~6
 * @return 星期文本
 */
const char* WeekName(uint8_t week) {
  switch (week) {
    case 0:
      return "Sun";
    case 1:
      return "Mon";
    case 2:
      return "Tue";
    case 3:
      return "Wed";
    case 4:
      return "Thu";
    case 5:
      return "Fri";
    case 6:
      return "Sat";
    default:
      return "Unknown";
  }
}

/**
 * @brief 格式化状态栏和主界面时间
 * @param status RTC 状态
 * @param buffer 输出缓冲区，至少 6 字节
 */
void FormatClockTime(const hal::RtcStatus& status, char* buffer) {
  const uint8_t hour = status.hour % 24;
  const uint8_t minute = status.minute % 60;
  buffer[0] = static_cast<char>('0' + hour / 10);
  buffer[1] = static_cast<char>('0' + hour % 10);
  buffer[2] = ':';
  buffer[3] = static_cast<char>('0' + minute / 10);
  buffer[4] = static_cast<char>('0' + minute % 10);
  buffer[5] = '\0';
}

/**
 * @brief 格式化主界面日期
 * @param status RTC 状态
 * @param buffer 输出缓冲区
 * @param size 输出缓冲区大小
 */
void FormatHomeDate(const hal::RtcStatus& status, char* buffer, size_t size) {
  if (buffer == nullptr || size == 0) {
    return;
  }

  std::snprintf(buffer, size, "%s %u%s", MonthName(status.month),
      static_cast<unsigned int>(status.day), DaySuffix(status.day));
}

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
  lv_anim_delete(object, callback);

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

  if (IsId(app_entry.id, "radio")) {
    return {
        .symbol = nullptr,
        .image = &radio_inner_icon_68x68,
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

  if (IsId(app_entry.id, "files")) {
    return {
        .symbol = nullptr,
        .image = &files_inner_icon_68x68,
        .shell_color = 0xD98F3B,
        .surface_color = 0xFFE9A8,
        .pressed_shell_color = 0xB8742E,
        .image_offset_x = 0,
        .image_offset_y = 0,
    };
  }

  if (IsId(app_entry.id, "camera")) {
    return {
        .symbol = nullptr,
        .image = &camera_inner_icon_68x68,
        .shell_color = 0xF2C051,
        .surface_color = 0xFBE995,
        .pressed_shell_color = 0xD69B36,
        .image_offset_x = 0,
        .image_offset_y = 0,
    };
  }

  if (IsId(app_entry.id, "settings")) {
    return {
        .symbol = nullptr,
        .image = &settings_inner_icon_68x68,
        .shell_color = 0x7D7D7D,
        .surface_color = 0xD1D1D1,
        .pressed_shell_color = 0x666666,
        .image_offset_x = 0,
        .image_offset_y = 0,
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

}  // namespace

bool UiManager::Init(hal::ScreenProvider* screen,
    hal::LvglPort* lvgl_port,
    const hal::DeviceCapabilities& device_capabilities,
    hal::DeviceDiagnosticsProvider* diagnostics,
    hal::DeviceInfoProvider* device_info,
    hal::GpsProvider* gps,
    hal::AudioProvider* audio,
    hal::HapticProvider* haptic,
    hal::BatteryManagementProvider* battery_management,
    hal::CameraProvider* camera,
    hal::RtcProvider* rtc,
    hal::RadioProvider* radio,
    hal::KeyboardExpansionProvider* keyboard_expansion,
    hal::ImuProvider* imu,
    hal::EthernetProvider* ethernet,
    hal::WifiProvider* wifi,
    hal::StorageProvider* storage, hal::OtgProvider* otg,
    hal::NfcProvider* nfc,
    hal::InfraredProvider* infrared, hal::CellularProvider* cellular) {
  if (screen == nullptr || lvgl_port == nullptr) {
    return false;
  }
  screen_ = screen;
  lvgl_port_ = lvgl_port;
  device_capabilities_ = device_capabilities;
  diagnostics_provider_ = diagnostics;
  device_info_provider_ = device_info;
  gps_provider_ = gps;
  audio_provider_ = audio;
  haptic_provider_ = haptic;
  RegisterUiHapticProvider(haptic_provider_);
  battery_management_provider_ = battery_management;
  camera_provider_ = camera;
  rtc_provider_ = rtc;
  radio_provider_ = radio;
  keyboard_expansion_provider_ = keyboard_expansion;
  RegisterSharedKeyboardPhysicalKeyboardProvider(
      keyboard_expansion_provider_);
  imu_provider_ = imu;
  ethernet_provider_ = ethernet;
  wifi_provider_ = wifi;
  storage_provider_ = storage;
  otg_provider_ = otg;
  nfc_provider_ = nfc;
  infrared_provider_ = infrared;
  cellular_provider_ = cellular;
  system_status_cache_.Init(rtc_provider_, battery_management_provider_, wifi_provider_);

  root_screen_ = lv_obj_create(nullptr);
  if (root_screen_ == nullptr) {
    return false;
  }

  lv_obj_remove_flag(root_screen_, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(root_screen_, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
  lv_obj_set_style_bg_color(root_screen_, lv_color_hex(0xE2E2E2), LV_PART_MAIN);
  lv_obj_set_style_border_width(root_screen_, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(root_screen_, 0, LV_PART_MAIN);
  AddEdgeBackSwipeEvents(root_screen_, AppBackSwipeEventCallback, this);
  lv_obj_add_event_cb(root_screen_, RootLayoutRefreshEventCallback,
      LV_EVENT_SIZE_CHANGED, this);
  lv_obj_add_event_cb(root_screen_, RootLayoutRefreshEventCallback,
      LV_EVENT_REFRESH, this);

  layout_width_ = LayoutWidth();
  layout_height_ = LayoutHeight();

  launcher_container_ = CreateLauncher(root_screen_);
  if (launcher_container_ == nullptr) {
    return false;
  }

  if (!status_bar_.Init(root_screen_, LayoutWidth())) {
    return false;
  }
  status_bar_.MoveToTop();

  system_status_refresh_timer_ = lv_timer_create(
      SystemStatusRefreshTimerCallback, kSystemStatusRefreshPeriodMs, this);
  if (system_status_refresh_timer_ == nullptr) {
    return false;
  }
  RefreshSystemStatus();

  startup_background_ = CreateStartupBackground(root_screen_);
  if (startup_background_ == nullptr) {
    return false;
  }

  lv_screen_load(root_screen_);
  return true;
}

bool UiManager::StartStartupScreenAnimation() {
  if (root_screen_ == nullptr) {
    return false;
  }
  if (startup_screen_ == nullptr) {
    startup_screen_ = CreateStartupScreen(root_screen_);
  }
  if (startup_screen_ == nullptr || startup_progress_fill_ == nullptr) {
    return false;
  }

  if (startup_background_ != nullptr) {
    lv_obj_delete(startup_background_);
    startup_background_ = nullptr;
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

bool UiManager::ShowBatteryStartupWarning(const char* icon_text,
    uint32_t icon_color, const char* message, int battery_percent) {
  if (root_screen_ == nullptr || icon_text == nullptr || message == nullptr) {
    return false;
  }

  lv_obj_t* warning = lv_obj_create(root_screen_);
  if (warning == nullptr) {
    return false;
  }
  lv_obj_remove_flag(warning, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(warning, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_size(warning, LayoutWidth(), LayoutHeight());
  lv_obj_set_pos(warning, 0, 0);
  lv_obj_set_style_bg_color(
      warning, lv_color_hex(kStartupBlackBackgroundColor), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(warning, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_border_width(warning, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(warning, 0, LV_PART_MAIN);
  lv_obj_set_style_radius(warning, 0, LV_PART_MAIN);

  lv_obj_t* icon = lv_label_create(warning);
  if (icon == nullptr) {
    lv_obj_delete(warning);
    return false;
  }
  lv_label_set_text(icon, icon_text);
  const uint32_t shell_color =
      battery_percent >= 0 ? kLowBatteryStartupTextColor : icon_color;
  SetTextStyle(icon, lv_color_hex(shell_color), MaterialOutlineIconFont56());
  lv_obj_align(icon, LV_ALIGN_CENTER, 0, kLowBatteryStartupIconOffsetY);

  if (battery_percent >= 0) {
    const int clamped_percent = std::clamp(battery_percent, 0, 100);
    lv_obj_t* fill = lv_obj_create(warning);
    if (fill == nullptr) {
      lv_obj_delete(warning);
      return false;
    }
    lv_obj_remove_flag(fill, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(fill, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(
        fill, lv_color_hex(icon_color), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(fill, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(fill, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(fill, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(fill, kStartupBatteryFillRadius, LV_PART_MAIN);
    if (clamped_percent <= 0) {
      lv_obj_set_size(fill, 1, kStartupBatteryFillHeight);
      lv_obj_add_flag(fill, LV_OBJ_FLAG_HIDDEN);
    } else {
      const int fill_width = std::max(
          kStartupBatteryFillMinVisibleWidth,
          kStartupBatteryFillMaxWidth * clamped_percent / 100);
      lv_obj_set_size(fill, fill_width, kStartupBatteryFillHeight);
    }
    lv_obj_align_to(fill, icon, LV_ALIGN_LEFT_MID,
        kStartupBatteryFillOffsetX, kStartupBatteryFillOffsetY);
    lv_obj_move_to_index(fill, lv_obj_get_index(icon));
    lv_obj_move_to_index(icon, -1);
  }

  lv_obj_t* label = lv_label_create(warning);
  if (label == nullptr) {
    lv_obj_delete(warning);
    return false;
  }
  lv_label_set_text(label, message);
  SetTextStyle(label, lv_color_hex(kLowBatteryStartupTextColor), Font32());
  lv_obj_align_to(label, icon, LV_ALIGN_OUT_BOTTOM_MID, 0,
      kLowBatteryStartupPercentGap);

  if (startup_background_ != nullptr) {
    lv_obj_delete(startup_background_);
    startup_background_ = nullptr;
  }
  lv_obj_move_to_index(warning, -1);
  lv_obj_invalidate(warning);
  return true;
}

bool UiManager::ShowKeyboardExpansionUnavailablePrompt() {
  if (root_screen_ == nullptr) {
    return false;
  }

  RefreshActiveSettingsKeyboardExpansion();

  PromptDialogConfig config;
  config.screen_width = LayoutWidth();
  config.screen_height = LayoutHeight();
  config.dialog_width =
      config.screen_width - 2 * kKeyboardExpansionPromptSideMargin;
  config.dialog_height = kKeyboardExpansionPromptHeight;
  config.dialog_radius = kKeyboardExpansionPromptRadius;
  config.inner_padding = kKeyboardExpansionPromptInnerPadding;
  config.header_height = 78;
  config.title_y = 34;
  config.title_subtitle_gap = 8;
  config.subtitle_body_gap = 16;
  config.action_height = 106;
  config.action_button_height = kKeyboardExpansionPromptButtonHeight;
  config.action_button_radius = kKeyboardExpansionPromptButtonRadius;
  config.action_button_gap = 20;
  config.action_bottom_padding = kKeyboardExpansionPromptInnerPadding;
  config.bottom_margin = kKeyboardExpansionPromptBottomMargin;
  config.animation_ms = 180;
  config.slide_from_bottom = true;
  config.title = "Keyboard expansion unavailable";
  config.subtitle =
      "Reconnect the keyboard expansion to use it again. Auto-connect "
      "remains on.";
  config.title_font = Font32();
  config.subtitle_font = Font24();
  config.action_font = Font28();
  config.title_text_align = LV_TEXT_ALIGN_CENTER;
  config.subtitle_text_align = LV_TEXT_ALIGN_CENTER;
  config.cancel_text = "OK";
  config.cancel_background_color = theme::LightNeutralTheme().action;
  config.cancel_pressed_color = theme::LightNeutralTheme().action_pressed;
  config.cancel_text_color = theme::LightNeutralTheme().on_action;
  config.confirm_text = nullptr;
  config.cancel_callback =
      KeyboardExpansionUnavailablePromptDismissedCallback;
  config.callback_context = this;
  if (ShowPromptDialog(root_screen_,
          &keyboard_expansion_unavailable_prompt_, config) == nullptr) {
    return false;
  }

  if (!app::GetInputMethodPreferences().use_on_screen_keyboard) {
    DefocusSharedKeyboardTextAreas();
  }
  return true;
}

void UiManager::CloseKeyboardExpansionUnavailablePrompt() {
  ClosePromptDialog(&keyboard_expansion_unavailable_prompt_);
}

void UiManager::KeyboardExpansionUnavailablePromptDismissedCallback(
    void* context) {
  auto* manager = static_cast<UiManager*>(context);
  if (manager != nullptr) {
    manager->RefreshActiveSettingsKeyboardExpansion();
    RefreshSharedKeyboardVisibility();
  }
}

bool UiManager::ShowPowerOffChargingScreen(
    int battery_percent, bool critical, bool full_charged) {
  const int clamped_percent = std::clamp(battery_percent, 0, 100);
  char message[32] = {};
  if (full_charged) {
    std::snprintf(message, sizeof(message), "%d%%  Fully charged",
        clamped_percent);
  } else {
    std::snprintf(
        message, sizeof(message), "%d%%  Charging", clamped_percent);
  }

  if (!ShowBatteryStartupWarning(icon::kBatteryAndroid0,
          critical ? kPowerOffChargingCriticalColor : kPowerOffChargingColor,
          message, clamped_percent)) {
    return false;
  }

  lv_obj_t* bolt = lv_label_create(root_screen_);
  if (bolt == nullptr) {
    return false;
  }
  lv_label_set_text(bolt, icon::kBolt);
  SetTextStyle(
      bolt, lv_color_hex(kLowBatteryStartupTextColor), MaterialFillIconFont32());
  lv_obj_align(bolt, LV_ALIGN_CENTER, kPowerOffChargingBoltOffsetX,
      kLowBatteryStartupIconOffsetY);
  lv_obj_move_to_index(bolt, -1);
  lv_obj_invalidate(bolt);
  return true;
}

void UiManager::SetStatusBarTextColor(uint32_t color) {
  status_bar_.SetTextColor(lv_color_hex(color));
}

void UiManager::SetStatusBarVisible(bool visible) {
  status_bar_.SetVisible(visible);
}

bool UiManager::ShowLockScreen() {
  if (root_screen_ == nullptr || screen_ == nullptr) {
    return false;
  }

  RefreshSystemStatusNow();
  if (lock_screen_ != nullptr) {
    UpdateLockScreenViewClock(
        lock_screen_, clock_time_text_, home_date_text_, home_week_text_);
    lv_obj_clear_flag(lock_screen_, LV_OBJ_FLAG_HIDDEN);
    ::lilygo_box::ui::SetLockScreenDragOffset(lock_screen_, 0);
    lv_obj_move_to_index(lock_screen_, -1);
    status_bar_.MoveToTop();
    NotifyLockScreenVisibilityChanged(true);
    return true;
  }

  LockScreenViewOptions options = {
      .screen_width = LayoutWidth(),
      .screen_height = LayoutHeight(),
      .time_text = clock_time_text_,
      .date_text = home_date_text_,
      .week_text = home_week_text_,
  };
  lock_screen_ = CreateLockScreenView(root_screen_, options);
  if (lock_screen_ == nullptr) {
    return false;
  }

  ::lilygo_box::ui::SetLockScreenDragOffset(lock_screen_, 0);
  status_bar_.MoveToTop();
  NotifyLockScreenVisibilityChanged(true);
  return true;
}

void UiManager::HideLockScreen() {
  if (lock_screen_ == nullptr) {
    return;
  }

  lv_obj_delete(lock_screen_);
  lock_screen_ = nullptr;
  NotifyLockScreenVisibilityChanged(false);
}

void UiManager::SetLockScreenDragOffset(int offset) {
  ::lilygo_box::ui::SetLockScreenDragOffset(lock_screen_, offset);
}

void UiManager::ResetLockScreenDrag() {
  ::lilygo_box::ui::StartLockScreenResetAnimation(lock_screen_);
}

void UiManager::PlayLockScreenUnlockAnimation() {
  ::lilygo_box::ui::StartLockScreenUnlockAnimation(lock_screen_);
}

bool UiManager::ShowPowerMenu(std::function<void()> restart_callback,
    std::function<void()> power_off_callback,
    std::function<void()> dismiss_callback) {
  if (root_screen_ == nullptr) {
    return false;
  }

  if (power_menu_ != nullptr) {
    lv_obj_move_to_index(power_menu_, -1);
    return true;
  }

  PowerMenuViewOptions options = {
      .screen_width = LayoutWidth(),
      .screen_height = LayoutHeight(),
      .dismiss_callback = [this, dismiss_callback = std::move(
                              dismiss_callback)]() {
        HidePowerMenu();
        if (dismiss_callback) {
          dismiss_callback();
        }
      },
      .restart_callback = std::move(restart_callback),
      .power_off_callback = std::move(power_off_callback),
  };
  power_menu_ = CreatePowerMenuView(root_screen_, options);
  if (power_menu_ == nullptr) {
    return false;
  }
  lv_obj_move_to_index(power_menu_, -1);
  return true;
}

void UiManager::SetSystemPowerCallbacks(
    std::function<void()> restart_callback,
    std::function<void()> power_off_callback) {
  restart_device_callback_ = std::move(restart_callback);
  power_off_device_callback_ = std::move(power_off_callback);
}

void UiManager::SetScreenLockCallback(std::function<void()> callback) {
  screen_lock_callback_ = std::move(callback);
}

void UiManager::SetScreenBrightnessCallback(
    std::function<bool(int)> callback) {
  screen_brightness_callback_ = std::move(callback);
}

bool UiManager::ShowVolumeOverlay(int volume_percent,
    VolumeOverlay::VolumeChangeCallback callback) {
  if (root_screen_ == nullptr) {
    return false;
  }
  auto synchronized_callback =
      [this, callback = std::move(callback)](int percent, bool commit) {
        if (!callback || !callback(percent, commit)) {
          return false;
        }
        UpdateActiveSettingsVolume(percent);
        return true;
      };
  const bool shown = volume_overlay_.Show(root_screen_, LayoutWidth(),
      LayoutHeight(), volume_percent, std::move(synchronized_callback));
  if (shown) {
    UpdateActiveSettingsVolume(volume_percent);
  }
  return shown;
}

void UiManager::UpdateActiveSettingsVolume(int volume_percent) {
  if (active_app_entry_ == nullptr || active_app_entry_->id == nullptr ||
      std::strcmp(active_app_entry_->id, "settings") != 0 ||
      active_view_container_ == nullptr) {
    return;
  }
  UpdateSettingsViewVolume(active_view_container_, volume_percent);
}

void UiManager::RefreshActiveSettingsKeyboardExpansion() {
  if (active_app_entry_ == nullptr || active_app_entry_->id == nullptr ||
      std::strcmp(active_app_entry_->id, "settings") != 0 ||
      active_view_container_ == nullptr) {
    return;
  }
  RefreshSettingsViewKeyboardExpansion(active_view_container_);
}

void UiManager::HidePowerMenu() {
  if (power_menu_ == nullptr) {
    return;
  }
  lv_obj_delete(power_menu_);
  power_menu_ = nullptr;
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

bool UiManager::IsStartupScreenActive() const {
  return startup_screen_ != nullptr;
}

bool UiManager::ShowFirstBootWelcome(
    std::function<bool()> completion_callback) {
  if (root_screen_ == nullptr || !completion_callback) {
    return false;
  }

  first_boot_welcome_completion_callback_ = std::move(completion_callback);
  first_boot_welcome_pending_ = true;
  if (first_boot_welcome_screen_ != nullptr) {
    return true;
  }
  if (CreateFirstBootWelcomeScreen()) {
    // 欢迎页预先放在启动页下方，避免启动页消失时短暂露出主界面。
    if (startup_screen_ != nullptr) {
      lv_obj_move_to_index(startup_screen_, -1);
    }
    return true;
  }
  first_boot_welcome_pending_ = false;
  first_boot_welcome_completion_callback_ = nullptr;
  return false;
}

bool UiManager::IsFirstBootWelcomeActive() const {
  return first_boot_welcome_pending_ ||
         first_boot_welcome_screen_ != nullptr;
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

  ResetAppIconPressedFeedback(context);

  lv_timer_t* timer = lv_timer_create(
      AppButtonOpenDelayCallback, kIconReleaseAnimationMs, context);
  if (timer == nullptr) {
    context->manager->ShowAppView(*context->app_entry);
    return;
  }

  lv_timer_set_repeat_count(timer, 1);
}

void UiManager::ResetAppIconPressedFeedback(AppButtonContext* context) {
  if (context == nullptr || context->icon_button == nullptr ||
      context->normal_icon_size <= 0) {
    return;
  }

  lv_anim_delete(context->icon_button, AppIconButtonSizeAnimCallback);
  lv_anim_delete(context->icon_button, DockIconButtonSizeAnimCallback);
  SetIconButtonSize(context->icon_button, context->normal_icon_size,
      context->normal_icon_size);
  lv_obj_remove_state(context->icon_button, LV_STATE_PRESSED);

  if (context->icon_surface != nullptr) {
    lv_anim_delete(context->icon_surface, InnerImageSurfaceSizeAnimCallback);
    SetInnerImageSurfaceSize(context->icon_surface, kInnerIconSurfaceSize);
  }
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

void UiManager::AppBackSwipeEventCallback(lv_event_t* event) {
  auto* self = static_cast<UiManager*>(lv_event_get_user_data(event));
  if (self == nullptr || self->active_view_container_ == nullptr ||
      self->screen_ == nullptr) {
    return;
  }

  if (!HandleEdgeBackSwipeEvent(
          event, self->LayoutWidth(), &self->app_back_swipe_)) {
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
  lv_event_stop_processing(event);
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
  const size_t page_index = scroll_x >= self->LayoutWidth() / 2 ? 1 : 0;
  self->UpdatePageIndicator(page_index);
}

void UiManager::SystemStatusRefreshTimerCallback(lv_timer_t* timer) {
  auto* self = static_cast<UiManager*>(lv_timer_get_user_data(timer));
  if (self != nullptr) {
    self->RefreshSystemStatus();
  }
}

void UiManager::RootLayoutRefreshEventCallback(lv_event_t* event) {
  const lv_event_code_t code = lv_event_get_code(event);
  if (code != LV_EVENT_SIZE_CHANGED && code != LV_EVENT_REFRESH) {
    return;
  }

  auto* self = static_cast<UiManager*>(lv_event_get_user_data(event));
  if (self != nullptr) {
    self->RelayoutForScreenSize();
  }
}

int UiManager::LayoutWidth() const {
  lv_display_t* display = lv_display_get_default();
  if (display != nullptr) {
    const int width = static_cast<int>(
        lv_display_get_horizontal_resolution(display));
    if (width > 0) {
      return width;
    }
  }
  if (root_screen_ != nullptr) {
    const int width = static_cast<int>(lv_obj_get_width(root_screen_));
    if (width > 0) {
      return width;
    }
  }
  return screen_ != nullptr ? screen_->ScreenWidth() : 0;
}

int UiManager::LayoutHeight() const {
  lv_display_t* display = lv_display_get_default();
  if (display != nullptr) {
    const int height = static_cast<int>(
        lv_display_get_vertical_resolution(display));
    if (height > 0) {
      return height;
    }
  }
  if (root_screen_ != nullptr) {
    const int height = static_cast<int>(lv_obj_get_height(root_screen_));
    if (height > 0) {
      return height;
    }
  }
  return screen_ != nullptr ? screen_->ScreenHeight() : 0;
}

void UiManager::RelayoutForScreenSize() {
  if (relayouting_ || root_screen_ == nullptr) {
    return;
  }

  const int new_width = LayoutWidth();
  const int new_height = LayoutHeight();
  if (new_width <= 0 || new_height <= 0 ||
      (new_width == layout_width_ && new_height == layout_height_)) {
    return;
  }

  relayouting_ = true;
  layout_width_ = new_width;
  layout_height_ = new_height;
  const app::AppEntry* reopen_app = active_app_entry_;
  active_app_entry_ = nullptr;
  const bool startup_background_active = startup_background_ != nullptr;
  const bool startup_active = startup_screen_ != nullptr;
  const bool first_boot_welcome_visible =
      first_boot_welcome_screen_ != nullptr;
  const bool first_boot_welcome_closing = first_boot_welcome_closing_;
  const int startup_percent = std::max(startup_progress_percent_,
      std::max(startup_progress_target_percent_, startup_progress_pending_percent_));
  lv_anim_delete(this, SetStartupProgressWidth);
  lv_anim_delete(this, SetFirstBootWelcomeOpacity);
  startup_progress_animating_ = false;
  startup_progress_pending_percent_ = 0;
  volume_overlay_.Reset();

  if (lock_screen_ != nullptr) {
    lv_obj_delete(lock_screen_);
    lock_screen_ = nullptr;
    NotifyLockScreenVisibilityChanged(false);
  }
  if (power_menu_ != nullptr) {
    lv_obj_delete(power_menu_);
    power_menu_ = nullptr;
  }
  if (startup_screen_ != nullptr) {
    lv_obj_delete(startup_screen_);
    startup_screen_ = nullptr;
    startup_progress_fill_ = nullptr;
  }
  if (startup_background_ != nullptr) {
    lv_obj_delete(startup_background_);
    startup_background_ = nullptr;
  }
  if (launcher_container_ != nullptr) {
    lv_obj_delete(launcher_container_);
    launcher_container_ = nullptr;
    page_scroller_ = nullptr;
    home_page_ = nullptr;
    reserved_page_ = nullptr;
    home_time_label_ = nullptr;
    home_date_label_ = nullptr;
    home_week_label_ = nullptr;
    page_indicator_ = nullptr;
    first_page_dot_ = nullptr;
    second_page_dot_ = nullptr;
  }
  if (active_view_container_ != nullptr) {
    active_view_lock_screen_callback_ = nullptr;
    lv_obj_delete(active_view_container_);
    active_view_container_ = nullptr;
  }

  if (status_bar_.object() != nullptr) {
    lv_obj_set_width(status_bar_.object(), LayoutWidth());
    lv_obj_align(status_bar_.object(), LV_ALIGN_TOP_MID, 0, 0);
    status_bar_.MoveToTop();
  }

  launcher_container_ = CreateLauncher(root_screen_);
  if (launcher_container_ != nullptr) {
    if (system_status_cache_.rtc_status_valid()) {
      UpdateClockLabels(system_status_cache_.rtc_status());
    }
    if (system_status_cache_.battery_management_status_valid()) {
      UpdateBatteryStatus(system_status_cache_.battery_management_status());
    }
    if (system_status_cache_.wifi_status_valid()) {
      UpdateWifiStatus(system_status_cache_.wifi_status());
    }
    UpdatePageIndicator(page_index_);
  }
  if (first_boot_welcome_screen_ != nullptr) {
    lv_obj_delete(first_boot_welcome_screen_);
    first_boot_welcome_screen_ = nullptr;
  }
  first_boot_welcome_closing_ = false;
  status_bar_.MoveToTop();

  if (reopen_app != nullptr) {
    ShowAppView(*reopen_app);
  } else if (startup_active) {
    startup_screen_ = CreateStartupScreen(root_screen_);
    if (startup_screen_ != nullptr) {
      lv_obj_clear_flag(startup_screen_, LV_OBJ_FLAG_HIDDEN);
      lv_obj_move_to_index(startup_screen_, -1);
      if (startup_progress_fill_ != nullptr) {
        lv_obj_t* track = lv_obj_get_parent(startup_progress_fill_);
        if (track != nullptr) {
          lv_obj_set_width(startup_progress_fill_,
              lv_obj_get_width(track) *
                  std::clamp(startup_percent, 0, 100) / 100);
        }
      }
    }
  } else if (startup_background_active) {
    startup_background_ = CreateStartupBackground(root_screen_);
    if (startup_background_ != nullptr) {
      lv_obj_move_to_index(startup_background_, -1);
    }
  }

  if (first_boot_welcome_visible && first_boot_welcome_pending_) {
    if (!CreateFirstBootWelcomeScreen()) {
      first_boot_welcome_pending_ = false;
      first_boot_welcome_completion_callback_ = nullptr;
      SetStatusBarVisible(true);
    } else if (startup_screen_ != nullptr) {
      // 旋转重建后仍保持启动页在欢迎页上方。
      lv_obj_move_to_index(startup_screen_, -1);
    }
  } else if (first_boot_welcome_closing) {
    SetStatusBarTextColor(kStatusBarLightTextColor);
    SetStatusBarVisible(true);
    status_bar_.MoveToTop();
  }

  lv_obj_invalidate(root_screen_);
  relayouting_ = false;
}

lv_obj_t* UiManager::CreateLauncher(lv_obj_t* parent) {
  lv_obj_t* launcher = lv_obj_create(parent);
  if (launcher == nullptr) {
    return nullptr;
  }

  lv_obj_remove_flag(launcher, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(launcher, LV_OBJ_FLAG_GESTURE_BUBBLE);
  lv_obj_add_flag(launcher, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
  MakeTransparent(launcher);
  lv_obj_set_size(launcher, LayoutWidth(), LayoutHeight());
  lv_obj_align(launcher, LV_ALIGN_CENTER, 0, 0);

  CreateWallpaperObjects(launcher, LayoutWidth(), LayoutHeight());

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
  lv_obj_set_size(scroller, LayoutWidth(), LayoutHeight());
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
  lv_obj_set_size(home_page_, LayoutWidth(), LayoutHeight());
  lv_obj_set_pos(home_page_, 0, 0);

  reserved_page_ = lv_obj_create(scroller);
  if (reserved_page_ == nullptr) {
    lv_obj_delete(scroller);
    return nullptr;
  }
  lv_obj_remove_flag(reserved_page_, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(reserved_page_, LV_OBJ_FLAG_SNAPPABLE);
  MakeTransparent(reserved_page_);
  lv_obj_set_size(reserved_page_, LayoutWidth(), LayoutHeight());
  lv_obj_set_pos(reserved_page_, LayoutWidth(), 0);

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
  const int screen_width = LayoutWidth();
  const int screen_height = LayoutHeight();
  const int clock_width = screen_width > screen_height
                              ? kLandscapeClockWidth
                              : screen_width - 2 * kHorizontalPadding;
  lv_obj_set_size(
      group, clock_width, ClockGroupHeight(screen_width, screen_height));
  lv_obj_align(group, LV_ALIGN_TOP_LEFT, kHorizontalPadding,
      ClockTop(screen_width, screen_height));

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
  home_time_label_ = time_label;
  home_date_label_ = date_label;
  home_week_label_ = week_label;
  return group;
}

void UiManager::RefreshSystemStatus() {
  system_status_cache_.RefreshSystemStatus();
  if (system_status_cache_.rtc_status_valid()) {
    UpdateClockLabels(system_status_cache_.rtc_status());
  }
  if (system_status_cache_.battery_management_status_valid()) {
    UpdateBatteryStatus(system_status_cache_.battery_management_status());
  }
  if (system_status_cache_.wifi_status_valid()) {
    UpdateWifiStatus(system_status_cache_.wifi_status());
  }
}

void UiManager::RefreshSystemStatusNow() {
  system_status_cache_.RefreshClock();
  system_status_cache_.RefreshBattery();
  if (system_status_cache_.rtc_status_valid()) {
    UpdateClockLabels(system_status_cache_.rtc_status());
  }
  if (system_status_cache_.battery_management_status_valid()) {
    UpdateBatteryStatus(system_status_cache_.battery_management_status());
  }
  system_status_cache_.RefreshWifi();
  if (system_status_cache_.wifi_status_valid()) {
    UpdateWifiStatus(system_status_cache_.wifi_status());
  }
}

void UiManager::UpdateClockLabels(const hal::RtcStatus& status) {
  char time_text[sizeof(clock_time_text_)] = {};
  FormatClockTime(status, time_text);
  if (std::strncmp(
          clock_time_text_, time_text, sizeof(clock_time_text_)) != 0) {
    std::memcpy(clock_time_text_, time_text, sizeof(clock_time_text_));
    status_bar_.SetTimeText(clock_time_text_);
    if (home_time_label_ != nullptr) {
      lv_label_set_text(home_time_label_, clock_time_text_);
    }
    if (lock_screen_ != nullptr) {
      UpdateLockScreenViewClock(
          lock_screen_, clock_time_text_, home_date_text_, home_week_text_);
    }
  }

  char date_text[sizeof(home_date_text_)] = {};
  FormatHomeDate(status, date_text, sizeof(date_text));
  if (std::strncmp(home_date_text_, date_text, sizeof(home_date_text_)) != 0) {
    std::strncpy(home_date_text_, date_text, sizeof(home_date_text_) - 1);
    home_date_text_[sizeof(home_date_text_) - 1] = '\0';
    if (home_date_label_ != nullptr) {
      lv_label_set_text(home_date_label_, home_date_text_);
    }
    if (lock_screen_ != nullptr) {
      UpdateLockScreenViewClock(
          lock_screen_, clock_time_text_, home_date_text_, home_week_text_);
    }
  }

  const char* week_text = WeekName(status.week);
  if (std::strncmp(home_week_text_, week_text, sizeof(home_week_text_)) != 0) {
    std::strncpy(home_week_text_, week_text, sizeof(home_week_text_) - 1);
    home_week_text_[sizeof(home_week_text_) - 1] = '\0';
    if (home_week_label_ != nullptr) {
      lv_label_set_text(home_week_label_, home_week_text_);
    }
    if (lock_screen_ != nullptr) {
      UpdateLockScreenViewClock(
          lock_screen_, clock_time_text_, home_date_text_, home_week_text_);
    }
  }
}

void UiManager::UpdateBatteryStatus(const hal::BatteryManagementStatus& status) {
  status_bar_.SetBatteryStatus(status.charge_percent, status.charging);
}

void UiManager::UpdateWifiStatus(const hal::WifiStatus& status) {
  const app::InternetAccessState internet_state =
      app::NetworkMonitor::Instance().GetStatus().internet_state;
  const bool internet_unavailable = status.time_test_running
      ? !status.time_synced
      : internet_state != app::InternetAccessState::kAvailable;
  status_bar_.SetWifiStatus(status.connected, status.rssi,
      internet_unavailable);
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

  const app::AppCatalog& app_catalog = app::GetHomeAppCatalog();
  button_context_count_ = app_catalog.entry_count;
  if (button_context_count_ > button_contexts_.size()) {
    button_context_count_ = button_contexts_.size();
  }

  const int cell_width = IconCellWidth();
  const int cell_height = IconCellHeight();
  const int screen_width = LayoutWidth();
  const int screen_height = LayoutHeight();
  const int columns = AppColumnCount(screen_width, screen_height);
  const int rows = RowCount(button_context_count_, columns);
  const int row_gaps = std::max(0, rows - 1) * kAppRowGap;
  const int grid_height = rows * cell_height + row_gaps;
  int inset_x = ClampInset(screen_width,
      ScreenEdgeInset(screen_width, screen_height), columns, cell_width);
  int column_gap = ColumnGap(screen_width, inset_x, columns, cell_width);

  if (screen_width > screen_height) {
    const int clock_right = kHorizontalPadding + kLandscapeClockWidth;
    const int grid_x = clock_right + kHorizontalPadding;
    const int grid_width = screen_width - grid_x;
    inset_x = ClampInset(grid_width, std::max(8, screen_height / 25),
        columns, cell_width);
    column_gap = ColumnGap(grid_width, inset_x, columns, cell_width);
    lv_obj_set_size(grid, grid_width, grid_height);
    lv_obj_align(grid, LV_ALIGN_TOP_LEFT, grid_x,
        ClockTop(screen_width, screen_height) + 20);
  } else {
    lv_obj_set_size(grid, screen_width, grid_height);
    lv_obj_align(grid, LV_ALIGN_TOP_LEFT, 0,
        HomeGridTop(screen_width, screen_height));
  }

  for (size_t i = 0; i < button_context_count_; ++i) {
    button_contexts_[i].manager = this;
    button_contexts_[i].app_entry = &app_catalog.entries[i];
    button_contexts_[i].icon_button = nullptr;
    button_contexts_[i].icon_surface = nullptr;
    button_contexts_[i].normal_icon_size = 0;

    lv_obj_t* cell = CreateAppIcon(grid, &button_contexts_[i], cell_width);
    if (cell == nullptr) {
      lv_obj_delete(grid);
      return nullptr;
    }

    const int column = static_cast<int>(i % columns);
    const int row = static_cast<int>(i / columns);
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
  context->icon_button = button;
  context->icon_surface = icon_parent == button ? nullptr : icon_parent;
  context->normal_icon_size = kAppIconSize;
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
      SetTextStyle(
          icon, lv_color_hex(0xFFFFFF), MaterialOutlineIconFont56());
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
  const int screen_width = LayoutWidth();
  const int screen_height = LayoutHeight();
  const int dock_columns = DockColumnCount(screen_width, screen_height);
  lv_obj_set_size(dock, screen_width, DockHeight(screen_width, screen_height));
  lv_obj_align(dock, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_obj_set_style_bg_color(dock, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(dock, 28, LV_PART_MAIN);
  lv_obj_set_style_border_width(dock, 0, LV_PART_MAIN);
  lv_obj_set_style_radius(dock, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(dock, 0, LV_PART_MAIN);

  const int cell_width = IconCellWidth();
  const int dock_inset = screen_width > screen_height
                             ? std::max(8, screen_height / 8)
                             : ScreenEdgeInset(screen_width, screen_height) +
                                   kDockInsetExtra;
  const int inset_x =
      ClampInset(screen_width, dock_inset, dock_columns, cell_width);
  const int column_gap =
      ColumnGap(screen_width, inset_x, dock_columns, cell_width);

  const app::AppCatalog& dock_catalog = app::GetDockAppCatalog();
  dock_button_context_count_ = dock_catalog.entry_count;
  if (dock_button_context_count_ > dock_button_contexts_.size()) {
    dock_button_context_count_ = dock_button_contexts_.size();
  }

  for (size_t i = 0; i < dock_button_context_count_; ++i) {
    dock_button_contexts_[i].manager = this;
    dock_button_contexts_[i].app_entry = &dock_catalog.entries[i];
    dock_button_contexts_[i].icon_button = nullptr;
    dock_button_contexts_[i].icon_surface = nullptr;
    dock_button_contexts_[i].normal_icon_size = 0;

    lv_obj_t* cell =
        CreateDockIcon(dock, &dock_button_contexts_[i], cell_width);
    if (cell == nullptr) {
      lv_obj_delete(dock);
      return nullptr;
    }

    const int column = static_cast<int>(i % dock_columns);
    const int x = inset_x + column * (cell_width + column_gap);
    lv_obj_align(cell, LV_ALIGN_TOP_LEFT, x, kDockTopPadding);
  }

  return dock;
}

lv_obj_t* UiManager::CreateDockIcon(
    lv_obj_t* parent, AppButtonContext* context, int cell_width) {
  if (context == nullptr || context->app_entry == nullptr) {
    return nullptr;
  }

  const app::AppEntry& entry = *context->app_entry;
  const IconStyle style = GetIconStyle(entry);
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
  lv_obj_add_event_cb(
      icon_box, AppButtonEventCallback, LV_EVENT_CLICKED, context);

  lv_obj_t* icon_parent = icon_box;
  if (style.image != nullptr) {
    icon_parent = CreateInnerImageSurface(icon_box, style.surface_color);
    if (icon_parent == nullptr) {
      lv_obj_delete(cell);
      return nullptr;
    }
  }
  context->icon_button = icon_box;
  context->icon_surface = icon_parent == icon_box ? nullptr : icon_parent;
  context->normal_icon_size = kDockIconSize;
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
      SetTextStyle(
          icon, lv_color_hex(0xFFFFFF), MaterialOutlineIconFont56());
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
  lv_obj_align(indicator, LV_ALIGN_BOTTOM_MID, 0,
      -PageIndicatorBottom(LayoutWidth(), LayoutHeight()));

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

lv_obj_t* UiManager::CreateStartupBackground(lv_obj_t* parent) {
  if (parent == nullptr) {
    return nullptr;
  }

  lv_obj_t* background = lv_obj_create(parent);
  if (background == nullptr) {
    return nullptr;
  }
  lv_obj_remove_flag(background, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(background, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_size(background, LayoutWidth(), LayoutHeight());
  lv_obj_set_pos(background, 0, 0);
  lv_obj_set_style_bg_color(background,
      lv_color_hex(kStartupBlackBackgroundColor), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(background, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_border_width(background, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(background, 0, LV_PART_MAIN);
  lv_obj_set_style_radius(background, 0, LV_PART_MAIN);
  lv_obj_move_to_index(background, -1);
  return background;
}

lv_obj_t* UiManager::CreateStartupScreen(lv_obj_t* parent) {
  if (parent == nullptr) {
    return nullptr;
  }

  lv_obj_t* startup = lv_obj_create(parent);
  if (startup == nullptr) {
    return nullptr;
  }

  lv_obj_remove_flag(startup, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(startup, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_size(startup, LayoutWidth(), LayoutHeight());
  lv_obj_set_pos(startup, 0, 0);
  lv_obj_set_style_bg_color(
      startup, lv_color_hex(kStartupBackgroundColor), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(startup, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_border_width(startup, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(startup, 0, LV_PART_MAIN);
  lv_obj_set_style_radius(startup, 0, LV_PART_MAIN);
  lv_obj_set_style_opa(startup, LV_OPA_COVER, LV_PART_MAIN);

  lv_obj_t* brand = lv_obj_create(startup);
  if (brand == nullptr) {
    lv_obj_delete(startup);
    return nullptr;
  }
  lv_obj_remove_flag(brand, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(brand, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_flag(brand, LV_OBJ_FLAG_GESTURE_BUBBLE);
  MakeTransparent(brand);

  lv_obj_t* brand_icon =
      CreateLilygoBoxBrandIcon(brand, kStartupBrandIconSize);
  if (brand_icon == nullptr) {
    lv_obj_delete(startup);
    return nullptr;
  }

  lv_obj_t* title = lv_label_create(brand);
  if (title == nullptr) {
    lv_obj_delete(startup);
    return nullptr;
  }
  lv_label_set_text(title, "LilygoBox");
  SetTextStyle(title, lv_color_hex(kStartupTextColor), Font32());
  lv_obj_update_layout(title);
  const int brand_width = kStartupBrandIconSize + kStartupBrandIconGap +
                          lv_obj_get_width(title);
  lv_obj_set_size(brand, brand_width, kStartupBrandIconSize);
  lv_obj_align(brand_icon, LV_ALIGN_LEFT_MID, 0, 0);
  lv_obj_align_to(title, brand_icon, LV_ALIGN_OUT_RIGHT_MID,
      kStartupBrandIconGap, 0);

  const int progress_width = std::min(kStartupProgressMaxWidth,
      LayoutWidth() * kStartupProgressWidthPercent / 100);
  const int progress_height = std::max(
      kStartupProgressMinHeight,
      LayoutHeight() / kStartupProgressHeightDivisor);
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
  lv_obj_align_to(brand, track, LV_ALIGN_OUT_TOP_MID, 0, -kStartupTitleGap);

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

void UiManager::SetFirstBootWelcomeOpacity(
    void* user_data, int32_t opacity) {
  auto* self = static_cast<UiManager*>(user_data);
  if (self == nullptr || self->first_boot_welcome_screen_ == nullptr) {
    return;
  }

  lv_obj_set_style_opa(
      self->first_boot_welcome_screen_, opacity, LV_PART_MAIN);
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

void UiManager::FirstBootWelcomeFadeCompletedCallback(
    lv_anim_t* animation) {
  auto* self = static_cast<UiManager*>(lv_anim_get_user_data(animation));
  if (self == nullptr) {
    return;
  }

  self->DestroyFirstBootWelcomeScreen();
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
  // 兜底时也先创建欢迎页，再删除启动页，避免中间帧显示主界面。
  if (first_boot_welcome_pending_ &&
      first_boot_welcome_screen_ == nullptr &&
      !CreateFirstBootWelcomeScreen()) {
    first_boot_welcome_pending_ = false;
    first_boot_welcome_completion_callback_ = nullptr;
    SetStatusBarVisible(true);
  }
  if (startup_screen_ != nullptr) {
    lv_obj_delete(startup_screen_);
    startup_screen_ = nullptr;
  }
  if (startup_background_ != nullptr) {
    lv_obj_delete(startup_background_);
    startup_background_ = nullptr;
  }
  startup_progress_fill_ = nullptr;
  startup_progress_percent_ = 0;
  startup_progress_target_percent_ = 0;
  startup_progress_pending_percent_ = 0;
  startup_progress_animating_ = false;
}

bool UiManager::CreateFirstBootWelcomeScreen() {
  if (first_boot_welcome_screen_ != nullptr) {
    return true;
  }
  if (root_screen_ == nullptr || !first_boot_welcome_pending_ ||
      !first_boot_welcome_completion_callback_) {
    return false;
  }

  FirstBootWelcomeViewOptions options;
  options.screen_width = LayoutWidth();
  options.screen_height = LayoutHeight();
  options.colors = &theme_provider_.colors();
  options.completion_callback = [this]() {
    return CompleteFirstBootWelcome();
  };
  first_boot_welcome_screen_ =
      CreateFirstBootWelcomeView(root_screen_, options);
  if (first_boot_welcome_screen_ == nullptr) {
    return false;
  }

  SetStatusBarVisible(false);
  lv_obj_move_to_index(first_boot_welcome_screen_, -1);
  return true;
}

bool UiManager::StartFirstBootWelcomeFadeOut() {
  if (first_boot_welcome_screen_ == nullptr) {
    return false;
  }

  lv_anim_delete(this, SetFirstBootWelcomeOpacity);
  const int current_opacity = lv_obj_get_style_opa(
      first_boot_welcome_screen_, LV_PART_MAIN);
  lv_anim_t animation;
  lv_anim_init(&animation);
  lv_anim_set_var(&animation, this);
  lv_anim_set_user_data(&animation, this);
  lv_anim_set_values(
      &animation, current_opacity, LV_OPA_TRANSP);
  lv_anim_set_duration(&animation, kFirstBootWelcomeFadeOutMs);
  lv_anim_set_path_cb(&animation, lv_anim_path_linear);
  lv_anim_set_exec_cb(&animation, SetFirstBootWelcomeOpacity);
  lv_anim_set_completed_cb(
      &animation, FirstBootWelcomeFadeCompletedCallback);
  return lv_anim_start(&animation) != nullptr;
}

void UiManager::DestroyFirstBootWelcomeScreen() {
  if (first_boot_welcome_screen_ != nullptr) {
    lv_obj_t* screen = first_boot_welcome_screen_;
    first_boot_welcome_screen_ = nullptr;
    lv_obj_delete(screen);
  }
  first_boot_welcome_closing_ = false;
  SetStatusBarTextColor(kStatusBarLightTextColor);
  SetStatusBarVisible(true);
  status_bar_.MoveToTop();
}

bool UiManager::CompleteFirstBootWelcome() {
  if (first_boot_welcome_closing_) {
    return true;
  }
  if (!first_boot_welcome_pending_ ||
      first_boot_welcome_screen_ == nullptr) {
    return false;
  }
  if (first_boot_welcome_completion_callback_ &&
      !first_boot_welcome_completion_callback_()) {
    return false;
  }

  first_boot_welcome_pending_ = false;
  first_boot_welcome_completion_callback_ = nullptr;
  first_boot_welcome_closing_ = true;
  lv_obj_remove_flag(first_boot_welcome_screen_, LV_OBJ_FLAG_CLICKABLE);
  // 先恢复下层主界面，再把欢迎页保持在最上方执行淡出。
  SetStatusBarTextColor(kStatusBarLightTextColor);
  SetStatusBarVisible(true);
  lv_obj_move_to_index(first_boot_welcome_screen_, -1);
  if (!StartFirstBootWelcomeFadeOut()) {
    DestroyFirstBootWelcomeScreen();
  }
  return true;
}

bool UiManager::CreateActiveAppView(const app::AppEntry& app_entry) {
  AppViewConfig config;
  config.width = LayoutWidth();
  config.height = LayoutHeight();
  config.device_capabilities = device_capabilities_;
  config.screen = screen_;
  config.lvgl_port = lvgl_port_;
  config.diagnostics = diagnostics_provider_;
  config.device_info = device_info_provider_;
  config.gps = gps_provider_;
  config.audio = audio_provider_;
  config.haptic = haptic_provider_;
  config.battery_management = battery_management_provider_;
  config.camera = camera_provider_;
  config.rtc = rtc_provider_;
  config.radio = radio_provider_;
  config.keyboard_expansion = keyboard_expansion_provider_;
  config.imu = imu_provider_;
  config.ethernet = ethernet_provider_;
  config.wifi = wifi_provider_;
  config.storage = storage_provider_;
  config.otg = otg_provider_;
  config.nfc = nfc_provider_;
  config.infrared = infrared_provider_;
  config.cellular = cellular_provider_;
  config.system_status = &system_status_cache_;
  config.theme_provider = &theme_provider_;
  config.back_callback = BackButtonEventCallback;
  config.back_context = this;
  config.set_status_bar_text_color = [this](uint32_t color) {
    SetStatusBarTextColor(color);
  };
  config.set_status_bar_visible = [this](bool visible) {
    SetStatusBarVisible(visible);
  };
  config.set_lock_screen_visibility_callback = [this](
      std::function<void(bool visible)> callback) {
    active_view_lock_screen_callback_ = std::move(callback);
  };
  config.request_screen_lock = screen_lock_callback_;
  config.set_screen_brightness = screen_brightness_callback_;
  config.show_power_options = [this]() {
    return ShowPowerMenu(restart_device_callback_,
        power_off_device_callback_, std::function<void()>());
  };

  SetStatusBarVisible(true);
  SetStatusBarTextColor(kStatusBarLightTextColor);
  active_view_container_ = CreateAppView(root_screen_, app_entry, config);
  if (active_view_container_ == nullptr) {
    return false;
  }

  lv_obj_set_pos(active_view_container_, 0, 0);
  lv_obj_set_size(active_view_container_, LayoutWidth(), LayoutHeight());
  app_back_swipe_ = EdgeBackSwipeState();
  EnableEdgeBackSwipeEventBubble(active_view_container_);
  AddEdgeBackSwipeEvents(
      active_view_container_, AppBackSwipeEventCallback, this);
  status_bar_.MoveToTop();
  return true;
}

bool UiManager::ShowAppView(const app::AppEntry& app_entry) {
  if (root_screen_ == nullptr || launcher_container_ == nullptr) {
    return false;
  }
  active_app_entry_ = &app_entry;

  lv_obj_remove_flag(launcher_container_, LV_OBJ_FLAG_HIDDEN);
  lv_obj_set_style_opa(launcher_container_, LV_OPA_COVER, LV_PART_MAIN);
  if (active_view_container_ != nullptr) {
    active_view_lock_screen_callback_ = nullptr;
    lv_obj_delete(active_view_container_);
    active_view_container_ = nullptr;
  }

  if (!CreateActiveAppView(app_entry)) {
    active_app_entry_ = nullptr;
    active_view_lock_screen_callback_ = nullptr;
    return false;
  }
  app_back_swipe_ = EdgeBackSwipeState();
  lv_obj_add_flag(launcher_container_, LV_OBJ_FLAG_HIDDEN);
  return true;
}

void UiManager::ShowLauncher() {
  active_app_entry_ = nullptr;
  app_back_swipe_ = EdgeBackSwipeState();
  if (active_view_container_ == nullptr || root_screen_ == nullptr ||
      launcher_container_ == nullptr) {
    if (launcher_container_ != nullptr) {
      lv_obj_remove_flag(launcher_container_, LV_OBJ_FLAG_HIDDEN);
      lv_obj_set_style_opa(launcher_container_, LV_OPA_COVER, LV_PART_MAIN);
      SetStatusBarTextColor(kStatusBarLightTextColor);
      SetStatusBarVisible(true);
      status_bar_.MoveToTop();
    }
    return;
  }

  active_view_lock_screen_callback_ = nullptr;
  lv_obj_delete(active_view_container_);
  active_view_container_ = nullptr;
  lv_obj_remove_flag(launcher_container_, LV_OBJ_FLAG_HIDDEN);
  lv_obj_set_style_opa(launcher_container_, LV_OPA_COVER, LV_PART_MAIN);
  SetStatusBarTextColor(kStatusBarLightTextColor);
  SetStatusBarVisible(true);
  status_bar_.MoveToTop();
}

void UiManager::NotifyLockScreenVisibilityChanged(bool visible) {
  if (active_view_lock_screen_callback_) {
    active_view_lock_screen_callback_(visible);
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
