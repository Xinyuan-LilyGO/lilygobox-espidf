/*
 * @Description: Music app view
 * @Author: LILYGO_L
 * @Date: 2026-07-08 00:00:00
 * @LastEditTime: 2026-07-09 13:34:34
 * @License: GPL 3.0
 */
#include "ui/views/music_view.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <functional>

#include "base/logger.h"
#include "ui/font/font_assets.h"
#include "ui/font/material_symbols_assets.h"
#include "ui/input/edge_back_gesture.h"

namespace lilygo_box::ui {
namespace {

constexpr uint32_t kMainBackgroundColor = 0xFEF9F6;
constexpr uint32_t kSurfaceContainerColor = 0xFCF1EF;
constexpr uint32_t kSecondaryContainerColor = 0xF8DBD5;
constexpr uint32_t kPrimaryColor = 0x874E43;
constexpr uint32_t kMainTextColor = 0x24201F;
constexpr uint32_t kSecondaryTextColor = 0x716864;

constexpr int kMainPadding = 32;
constexpr int kHeaderTop = 70;
constexpr int kTabTop = 164;
constexpr int kMiniPlayerHeight = 104;
constexpr int kMiniPlayerRadius = 22;
constexpr int kPlayerAnimationMs = 260;
constexpr int kPlayToggleAnimationMs = 160;
constexpr int kProgressBarHeight = 22;
constexpr int kProgressSliderHeight = 50;
constexpr int kProgressThumbNormalWidth = 10;
constexpr int kProgressThumbPressedWidth = 13;
constexpr int kProgressThumbPressedHeight = 60;
constexpr int kDefaultTrackDurationSeconds = 90;

enum class MusicControlIcon {
  kPlay,
  kPause,
  kSkipPrevious,
  kSkipNext,
};

struct MusicViewState {
  AppViewConfig config;
  lv_obj_t* root = nullptr;
  lv_obj_t* player_page = nullptr;
  lv_obj_t* play_button = nullptr;
  lv_obj_t* current_time_label = nullptr;
  lv_obj_t* total_time_label = nullptr;
  EdgeBackSwipeState player_edge_swipe;
  bool playing = false;
};

/**
 * @brief 设置对象背景、边框和内边距为透明
 * @param object LVGL 对象
 */
void MakeTransparent(lv_obj_t* object) {
  lv_obj_set_style_bg_opa(object, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_set_style_border_width(object, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(object, 0, LV_PART_MAIN);
}

/**
 * @brief 设置文本对象颜色和字体
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
 * @brief 获取 48 号 Google Sans 字体
 * @return 字体指针
 */
const lv_font_t* Font48() { return &lvgl_font_google_sans_flex_48; }

/**
 * @brief 获取 56 号 Material Symbols 字体
 * @return 字体指针
 */
const lv_font_t* MaterialIconFont56() { return &lvgl_font_material_symbols_56; }

/**
 * @brief 创建文本标签
 * @param parent 父对象
 * @param text 显示文本
 * @param color 文本颜色
 * @param font 文本字体
 * @return 创建成功返回标签对象，否则返回 nullptr
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
 * @brief 设置音乐时间标签文本
 * @param label 时间标签对象
 * @param seconds 时间，单位秒
 */
void SetMusicTimeLabel(lv_obj_t* label, int seconds) {
  if (label == nullptr) {
    return;
  }
  seconds = std::max(0, seconds);
  char text[16];
  std::snprintf(text, sizeof(text), "%d:%02d", seconds / 60, seconds % 60);
  lv_label_set_text(label, text);
}

/**
 * @brief 根据进度条百分比更新当前播放时间
 * @param state 音乐视图状态
 * @param value 进度条百分比
 */
void UpdateMusicCurrentTime(MusicViewState* state, int32_t value) {
  if (state == nullptr) {
    return;
  }
  value = std::clamp<int32_t>(value, 0, 100);
  const int current_seconds = kDefaultTrackDurationSeconds * value / 100;
  SetMusicTimeLabel(state->current_time_label, current_seconds);
}

/**
 * @brief 将音乐进度条和当前播放时间重置为起始状态
 * @param slider 进度条对象
 * @param state 音乐视图状态
 */
void ResetMusicProgress(lv_obj_t* slider, MusicViewState* state) {
  if (slider != nullptr) {
    lv_slider_set_value(slider, 0, LV_ANIM_OFF);
    lv_obj_invalidate(slider);
  }
  UpdateMusicCurrentTime(state, 0);
}

/**
 * @brief 创建可点击的无边框按钮
 * @param parent 父对象
 * @return 创建成功返回按钮对象，否则返回 nullptr
 */
lv_obj_t* CreateFlatButton(lv_obj_t* parent) {
  lv_obj_t* button = lv_button_create(parent);
  if (button == nullptr) {
    return nullptr;
  }
  MakeTransparent(button);
  lv_obj_set_style_shadow_width(button, 0, LV_PART_MAIN);
  lv_obj_set_style_bg_opa(button, LV_OPA_TRANSP, LV_PART_MAIN);
  return button;
}

/**
 * @brief 设置对象 Y 坐标
 * @param object LVGL 对象
 * @param y Y 坐标
 */
void SetObjectY(void* object, int32_t y) {
  lv_obj_set_y(static_cast<lv_obj_t*>(object), y);
}

/**
 * @brief 设置对象圆角
 * @param object LVGL 对象
 * @param radius 圆角半径
 */
void SetObjectRadius(void* object, int32_t radius) {
  lv_obj_set_style_radius(static_cast<lv_obj_t*>(object), radius, LV_PART_MAIN);
}

/**
 * @brief 绘制填充矩形
 * @param layer LVGL 绘制层
 * @param x1 左上 X 坐标
 * @param y1 左上 Y 坐标
 * @param x2 右下 X 坐标
 * @param y2 右下 Y 坐标
 * @param radius 圆角半径
 * @param color 颜色
 */
void DrawFilledRect(lv_layer_t* layer, int32_t x1, int32_t y1, int32_t x2,
    int32_t y2, int32_t radius, lv_color_t color) {
  lv_area_t area;
  area.x1 = x1;
  area.y1 = y1;
  area.x2 = x2;
  area.y2 = y2;
  lv_draw_fill_dsc_t descriptor;
  lv_draw_fill_dsc_init(&descriptor);
  descriptor.color = color;
  descriptor.opa = LV_OPA_COVER;
  descriptor.radius = radius;
  lv_draw_fill(layer, &descriptor, &area);
}

/**
 * @brief 绘制播放三角形
 * @param layer LVGL 绘制层
 * @param area 图标区域
 * @param color 颜色
 * @param mirrored 是否水平镜像
 */
void DrawPlayTriangle(lv_layer_t* layer, const lv_area_t& area,
    lv_color_t color, bool mirrored) {
  const int32_t width = lv_area_get_width(&area);
  const int32_t height = lv_area_get_height(&area);
  const int32_t optical_offset = mirrored ? -(width * 5 / 100) : width * 5 / 100;
  const int32_t center_x = area.x1 + width / 2 + optical_offset;
  const int32_t center_y = area.y1 + height / 2;
  const int32_t triangle_width = width * 32 / 100;
  const int32_t triangle_height = height * 40 / 100;
  const int32_t left = center_x - triangle_width / 2;
  const int32_t right = center_x + triangle_width / 2;
  const int32_t top = center_y - triangle_height / 2;
  const int32_t bottom = center_y + triangle_height / 2;

  lv_draw_triangle_dsc_t descriptor;
  lv_draw_triangle_dsc_init(&descriptor);
  descriptor.color = color;
  descriptor.opa = LV_OPA_COVER;
  if (mirrored) {
    descriptor.p[0].x = right;
    descriptor.p[0].y = top;
    descriptor.p[1].x = right;
    descriptor.p[1].y = bottom;
    descriptor.p[2].x = left;
    descriptor.p[2].y = center_y;
  } else {
    descriptor.p[0].x = left;
    descriptor.p[0].y = top;
    descriptor.p[1].x = left;
    descriptor.p[1].y = bottom;
    descriptor.p[2].x = right;
    descriptor.p[2].y = center_y;
  }
  lv_draw_triangle(layer, &descriptor);
}

/**
 * @brief 绘制跳转播放图标模板
 * @param layer LVGL 绘制层
 * @param area 图标区域
 * @param color 颜色
 * @param mirrored 是否水平镜像为上一首
 */
void DrawSkipControlIcon(lv_layer_t* layer, const lv_area_t& area,
    lv_color_t color, bool mirrored) {
  const int32_t width = lv_area_get_width(&area);
  const int32_t height = lv_area_get_height(&area);
  const int32_t center_x = area.x1 + width / 2;
  const int32_t center_y = area.y1 + height / 2;
  const int32_t bar_width = std::max<int32_t>(3, width * 7 / 100);
  const int32_t bar_height = height * 34 / 100;

  lv_area_t triangle_area = area;
  const int32_t inset_x = width * 8 / 100;
  const int32_t inset_y = height * 8 / 100;
  const int32_t triangle_offset = -(width * 5 / 100);
  triangle_area.x1 += inset_x + triangle_offset;
  triangle_area.x2 -= inset_x - triangle_offset;
  triangle_area.y1 += inset_y;
  triangle_area.y2 -= inset_y;

  int32_t bar_x = center_x + width * 14 / 100;
  if (mirrored) {
    const int32_t mirrored_x1 = 2 * center_x - triangle_area.x2;
    const int32_t mirrored_x2 = 2 * center_x - triangle_area.x1;
    triangle_area.x1 = mirrored_x1;
    triangle_area.x2 = mirrored_x2;
    bar_x = center_x - width * 14 / 100 - bar_width;
  }

  DrawPlayTriangle(layer, triangle_area, color, mirrored);
  DrawFilledRect(layer, bar_x, center_y - bar_height / 2,
      bar_x + bar_width, center_y + bar_height / 2, LV_RADIUS_CIRCLE, color);
}

/**
 * @brief 绘制音乐控制图标
 * @param object 图标容器
 * @param layer LVGL 绘制层
 * @param icon 控制图标类型
 * @param color 颜色
 */
void DrawMusicControlIcon(lv_obj_t* object, lv_layer_t* layer,
    MusicControlIcon icon, lv_color_t color) {
  lv_area_t area;
  lv_obj_get_coords(object, &area);
  const int32_t width = lv_area_get_width(&area);
  const int32_t height = lv_area_get_height(&area);
  const int32_t center_x = area.x1 + width / 2;
  const int32_t center_y = area.y1 + height / 2;
  const int32_t gap = std::max<int32_t>(4, width * 6 / 100);

  if (icon == MusicControlIcon::kPlay) {
    DrawPlayTriangle(layer, area, color, false);
    return;
  }
  if (icon == MusicControlIcon::kPause) {
    const int32_t pause_width = width * 11 / 100;
    const int32_t pause_height = height * 42 / 100;
    const int32_t left_x = center_x - pause_width - gap / 2;
    const int32_t right_x = center_x + gap / 2;
    const int32_t top = center_y - pause_height / 2;
    const int32_t bottom = center_y + pause_height / 2;
    DrawFilledRect(layer, left_x, top, left_x + pause_width, bottom,
        LV_RADIUS_CIRCLE, color);
    DrawFilledRect(layer, right_x, top, right_x + pause_width, bottom,
        LV_RADIUS_CIRCLE, color);
    return;
  }

  DrawSkipControlIcon(
      layer, area, color, icon == MusicControlIcon::kSkipPrevious);
}

/**
 * @brief 控制图标绘制事件
 * @param event LVGL 事件对象
 */
void StaticControlIconDrawEventCallback(lv_event_t* event) {
  if (lv_event_get_code(event) != LV_EVENT_DRAW_MAIN) {
    return;
  }
  lv_obj_t* target = lv_event_get_target_obj(event);
  auto icon = static_cast<MusicControlIcon>(
      reinterpret_cast<intptr_t>(lv_event_get_user_data(event)));
  DrawMusicControlIcon(target, lv_event_get_layer(event), icon,
      lv_obj_get_style_text_color(target, LV_PART_MAIN));
}

/**
 * @brief 绘制音乐播放进度条
 * @param event LVGL 事件对象
 */
void MusicProgressDrawEventCallback(lv_event_t* event) {
  if (lv_event_get_code(event) != LV_EVENT_DRAW_MAIN) {
    return;
  }
  lv_obj_t* slider = lv_event_get_target_obj(event);
  lv_area_t area;
  lv_obj_get_coords(slider, &area);

  const int32_t width = lv_area_get_width(&area);
  const int32_t center_y = area.y1 + lv_area_get_height(&area) / 2;
  const int32_t track_top = center_y - kProgressBarHeight / 2;
  const int32_t track_bottom = track_top + kProgressBarHeight - 1;
  const int32_t mask_padding = std::max<int32_t>(2, kProgressBarHeight / 6);
  const int32_t mask_side_padding =
      std::max<int32_t>(3, kProgressThumbNormalWidth / 2);
  const int32_t progress_overlap = 2;
  const int32_t value =
      std::clamp<int32_t>(lv_slider_get_value(slider), 0, 100);
  const bool pressed = lv_obj_has_state(slider, LV_STATE_PRESSED);
  const int32_t thumb_width =
      pressed ? kProgressThumbPressedWidth : kProgressThumbNormalWidth;
  const int32_t thumb_height =
      pressed ? kProgressThumbPressedHeight : kProgressSliderHeight;
  const int32_t thumb_x = area.x1 +
      std::clamp<int32_t>(
          value * (width - thumb_width) / 100, 0, width - thumb_width);
  const int32_t thumb_top = center_y - thumb_height / 2;
  const int32_t thumb_bottom = thumb_top + thumb_height - 1;
  const int32_t mask_left =
      std::clamp<int32_t>(thumb_x - mask_side_padding, area.x1, area.x2);
  const int32_t mask_right =
      std::clamp<int32_t>(
          thumb_x + thumb_width - 1 + mask_side_padding, area.x1, area.x2);
  const int32_t progress_right =
      std::clamp<int32_t>(mask_left + progress_overlap, area.x1, thumb_x);
  lv_layer_t* layer = lv_event_get_layer(event);
  const lv_color_t progress_color = lv_color_hex(kPrimaryColor);
  const lv_color_t track_color = lv_color_hex(kSecondaryContainerColor);
  const lv_color_t mask_color = lv_color_hex(kSurfaceContainerColor);

  DrawFilledRect(layer, area.x1, track_top, area.x2, track_bottom,
      kProgressBarHeight / 2, track_color);
  if (progress_right > area.x1) {
    DrawFilledRect(layer, area.x1, track_top, progress_right, track_bottom,
        kProgressBarHeight / 2, progress_color);
  }
  if (mask_right >= mask_left) {
    DrawFilledRect(layer, mask_left, track_top - mask_padding, mask_right,
        track_bottom + mask_padding, 0, mask_color);
  }
  DrawFilledRect(layer, thumb_x, thumb_top, thumb_x + thumb_width - 1,
      thumb_bottom, LV_RADIUS_CIRCLE, progress_color);
}

/**
 * @brief 处理音乐播放进度条触摸输入
 * @param event LVGL 事件对象
 */
void MusicProgressInputEventCallback(lv_event_t* event) {
  const lv_event_code_t code = lv_event_get_code(event);
  auto* state = static_cast<MusicViewState*>(lv_event_get_user_data(event));
  lv_obj_t* slider = lv_event_get_target_obj(event);
  if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
    ResetMusicProgress(slider, state);
    return;
  }
  if (code == LV_EVENT_VALUE_CHANGED) {
    UpdateMusicCurrentTime(state, lv_slider_get_value(slider));
    return;
  }
  if (code != LV_EVENT_PRESSED && code != LV_EVENT_PRESSING) {
    return;
  }

  lv_indev_t* indev = lv_indev_active();
  if (indev == nullptr) {
    return;
  }

  lv_area_t area;
  lv_point_t point;
  lv_obj_get_coords(slider, &area);
  lv_indev_get_point(indev, &point);

  const int32_t width = std::max<int32_t>(1, lv_area_get_width(&area));
  const int32_t clamped_x =
      std::clamp<int32_t>(point.x - area.x1, 0, width);
  const int32_t value = clamped_x * 100 / width;
  lv_slider_set_value(slider, value, LV_ANIM_OFF);
  UpdateMusicCurrentTime(state, value);
  lv_obj_invalidate(slider);
}

/**
 * @brief 播放页关闭动画完成后删除页面
 * @param animation LVGL 动画对象
 */
void PlayerCloseCompletedCallback(lv_anim_t* animation) {
  auto* state = static_cast<MusicViewState*>(lv_anim_get_user_data(animation));
  if (state == nullptr || state->player_page == nullptr) {
    return;
  }
  lv_obj_t* page = state->player_page;
  state->player_page = nullptr;
  state->play_button = nullptr;
  state->current_time_label = nullptr;
  state->total_time_label = nullptr;
  lv_obj_delete(page);
  if (state->config.set_status_bar_visible) {
    state->config.set_status_bar_visible(true);
  }
  if (state->config.set_status_bar_text_color) {
    state->config.set_status_bar_text_color(kMainTextColor);
  }
}

/**
 * @brief 启动页面纵向滑动动画
 * @param object 动画对象
 * @param start_y 起始 Y 坐标
 * @param end_y 结束 Y 坐标
 * @param state 音乐视图状态
 * @param completed_callback 完成回调
 */
void StartVerticalSlide(lv_obj_t* object, int32_t start_y, int32_t end_y,
    MusicViewState* state, lv_anim_completed_cb_t completed_callback) {
  if (object == nullptr) {
    return;
  }
  lv_anim_delete(object, SetObjectY);
  lv_obj_set_y(object, start_y);
  lv_anim_t animation;
  lv_anim_init(&animation);
  lv_anim_set_var(&animation, object);
  lv_anim_set_values(&animation, start_y, end_y);
  lv_anim_set_duration(&animation, kPlayerAnimationMs);
  lv_anim_set_path_cb(&animation, lv_anim_path_ease_out);
  lv_anim_set_exec_cb(&animation, SetObjectY);
  lv_anim_set_user_data(&animation, state);
  if (completed_callback != nullptr) {
    lv_anim_set_completed_cb(&animation, completed_callback);
  }
  lv_anim_start(&animation);
}

/**
 * @brief 更新播放按钮图标和形状
 * @param state 音乐视图状态
 * @param animated 是否播放圆角切换动画
 */
void UpdatePlayButton(MusicViewState* state, bool animated) {
  if (state == nullptr || state->play_button == nullptr) {
    return;
  }

  lv_obj_invalidate(state->play_button);
  const int32_t target_radius = state->playing ? 34 : 58;
  if (!animated) {
    SetObjectRadius(state->play_button, target_radius);
    return;
  }

  lv_anim_delete(state->play_button, SetObjectRadius);
  lv_anim_t animation;
  lv_anim_init(&animation);
  lv_anim_set_var(&animation, state->play_button);
  lv_anim_set_values(&animation,
      lv_obj_get_style_radius(state->play_button, LV_PART_MAIN),
      target_radius);
  lv_anim_set_duration(&animation, kPlayToggleAnimationMs);
  lv_anim_set_path_cb(&animation, lv_anim_path_ease_out);
  lv_anim_set_exec_cb(&animation, SetObjectRadius);
  lv_anim_start(&animation);
}

/**
 * @brief 播放按钮图标绘制事件
 * @param event LVGL 事件对象
 */
void PlayButtonDrawEventCallback(lv_event_t* event) {
  if (lv_event_get_code(event) != LV_EVENT_DRAW_MAIN) {
    return;
  }
  auto* state = static_cast<MusicViewState*>(lv_event_get_user_data(event));
  if (state == nullptr) {
    return;
  }
  DrawMusicControlIcon(lv_event_get_target_obj(event), lv_event_get_layer(event),
      state->playing ? MusicControlIcon::kPause : MusicControlIcon::kPlay,
      lv_color_hex(0xFFFFFF));
}

/**
 * @brief 播放按钮点击事件
 * @param event LVGL 事件对象
 */
void PlayButtonClickedEventCallback(lv_event_t* event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
    return;
  }
  auto* state = static_cast<MusicViewState*>(lv_event_get_user_data(event));
  if (state == nullptr) {
    return;
  }
  state->playing = !state->playing;
  UpdatePlayButton(state, true);
  lv_event_stop_bubbling(event);
}

/**
 * @brief 阻止按钮点击事件继续冒泡
 * @param event LVGL 事件对象
 */
void StopClickBubblingEventCallback(lv_event_t* event) {
  const lv_event_code_t code = lv_event_get_code(event);
  if (code == LV_EVENT_PRESSED || code == LV_EVENT_PRESSING ||
      code == LV_EVENT_RELEASED || code == LV_EVENT_CLICKED ||
      code == LV_EVENT_PRESS_LOST) {
    lv_event_stop_bubbling(event);
  }
}

/**
 * @brief 请求关闭播放页
 * @param state 音乐视图状态
 */
void ClosePlayerPage(MusicViewState* state) {
  if (state == nullptr || state->player_page == nullptr) {
    return;
  }
  StartVerticalSlide(state->player_page, lv_obj_get_y(state->player_page),
      state->config.height, state, PlayerCloseCompletedCallback);
}

/**
 * @brief 关闭播放页点击事件
 * @param event LVGL 事件对象
 */
void PlayerBackClickedEventCallback(lv_event_t* event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
    return;
  }
  auto* state = static_cast<MusicViewState*>(lv_event_get_user_data(event));
  ClosePlayerPage(state);
}

/**
 * @brief 处理播放页边缘返回滑动事件
 * @param event LVGL 事件对象
 */
void PlayerEdgeBackEventCallback(lv_event_t* event) {
  auto* state = static_cast<MusicViewState*>(lv_event_get_user_data(event));
  if (state == nullptr || state->player_page == nullptr ||
      !HandleEdgeBackSwipeEvent(
          event, state->config.width, &state->player_edge_swipe)) {
    return;
  }

  ClosePlayerPage(state);
  lv_event_stop_bubbling(event);
  lv_event_stop_processing(event);
}

/**
 * @brief 创建播放器圆形控制按钮
 * @param parent 父对象
 * @param text 按钮文本
 * @param size 按钮大小
 * @param color 背景颜色
 * @return 创建成功返回按钮对象，否则返回 nullptr
 */
lv_obj_t* CreatePlayerControlButton(
    lv_obj_t* parent, MusicControlIcon icon, int size, uint32_t background_color,
    uint32_t icon_color) {
  lv_obj_t* button = lv_button_create(parent);
  if (button == nullptr) {
    return nullptr;
  }
  lv_obj_set_size(button, size, size);
  lv_obj_set_style_radius(button, size / 2, LV_PART_MAIN);
  lv_obj_set_style_bg_color(button, lv_color_hex(background_color),
      LV_PART_MAIN);
  lv_obj_set_style_bg_opa(button, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_border_width(button, 0, LV_PART_MAIN);
  lv_obj_set_style_shadow_width(button, 0, LV_PART_MAIN);
  lv_obj_set_style_text_color(button, lv_color_hex(icon_color), LV_PART_MAIN);
  lv_obj_add_event_cb(button, StaticControlIconDrawEventCallback,
      LV_EVENT_DRAW_MAIN, reinterpret_cast<void*>(static_cast<intptr_t>(icon)));
  return button;
}

/**
 * @brief 创建音乐封面占位
 * @param parent 父对象
 * @param size 封面尺寸
 * @param radius 圆角半径
 * @return 创建成功返回封面对象，否则返回 nullptr
 */
lv_obj_t* CreateArtwork(lv_obj_t* parent, int size, int radius) {
  lv_obj_t* artwork = lv_obj_create(parent);
  if (artwork == nullptr) {
    return nullptr;
  }
  lv_obj_remove_flag(artwork, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(artwork, size, size);
  lv_obj_set_style_radius(artwork, radius, LV_PART_MAIN);
  lv_obj_set_style_bg_color(
      artwork, lv_color_hex(kSecondaryContainerColor), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(artwork, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_border_width(artwork, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(artwork, 0, LV_PART_MAIN);

  lv_obj_t* icon_label =
      CreateLabel(artwork, icon::kMusic, lv_color_hex(kPrimaryColor),
          MaterialIconFont56());
  if (icon_label != nullptr) {
    lv_obj_center(icon_label);
  }
  return artwork;
}

/**
 * @brief 创建播放详情页
 * @param state 音乐视图状态
 * @return 创建成功返回 true，否则返回 false
 */
bool CreatePlayerPage(MusicViewState* state) {
  if (state == nullptr || state->root == nullptr || state->player_page != nullptr) {
    return false;
  }

  lv_obj_t* page = lv_obj_create(state->root);
  if (page == nullptr) {
    return false;
  }
  state->player_page = page;
  lv_obj_add_flag(page, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_remove_flag(page, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(page, state->config.width, state->config.height);
  lv_obj_set_style_bg_color(page, lv_color_hex(kSurfaceContainerColor),
      LV_PART_MAIN);
  lv_obj_set_style_bg_opa(page, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_border_width(page, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(page, 0, LV_PART_MAIN);
  AddEdgeBackSwipeEvents(page, PlayerEdgeBackEventCallback, state);

  if (state->config.set_status_bar_visible) {
    state->config.set_status_bar_visible(true);
  }
  if (state->config.set_status_bar_text_color) {
    state->config.set_status_bar_text_color(kMainTextColor);
  }

  lv_obj_t* close_button = CreateFlatButton(page);
  if (close_button == nullptr) {
    return false;
  }
  lv_obj_set_size(close_button, 76, 76);
  lv_obj_align(close_button, LV_ALIGN_TOP_LEFT, 14, 48);
  lv_obj_add_event_cb(
      close_button, PlayerBackClickedEventCallback, LV_EVENT_CLICKED, state);
  lv_obj_t* close_label =
      CreateLabel(close_button, icon::kKeyboardArrowDown,
          lv_color_hex(kMainTextColor), MaterialIconFont56());
  if (close_label != nullptr) {
    lv_obj_center(close_label);
  }

  lv_obj_t* heading =
      CreateLabel(page, "Now Playing", lv_color_hex(kMainTextColor), Font32());
  if (heading != nullptr) {
    lv_obj_align(heading, LV_ALIGN_TOP_MID, 0, 60);
  }
  lv_obj_t* subheading =
      CreateLabel(page, "All Songs", lv_color_hex(kSecondaryTextColor),
          Font24());
  if (subheading != nullptr && heading != nullptr) {
    lv_obj_align_to(subheading, heading, LV_ALIGN_OUT_BOTTOM_MID, 0, 2);
  }

  const bool compact_layout = state->config.height < 700;
  const int artwork_size = compact_layout
      ? std::min(state->config.width / 3, state->config.height / 3)
      : std::min(state->config.width - 64, state->config.height / 2);
  const int artwork_y = compact_layout ? 132 : 176;
  const int title_y = artwork_y + artwork_size + (compact_layout ? 18 : 46);
  const int track_y = title_y + (compact_layout ? 82 : 154);
  const int control_bottom = compact_layout ? -46 : -78;
  const int side_control_bottom = compact_layout ? -60 : -92;
  const int side_control_offset = compact_layout ? 96 : 120;
  lv_obj_t* artwork = CreateArtwork(page, artwork_size, 26);
  if (artwork != nullptr) {
    lv_obj_align(artwork, LV_ALIGN_TOP_MID, 0, artwork_y);
  }

  lv_obj_t* title =
      CreateLabel(page, "Unknown Track", lv_color_hex(kMainTextColor), Font48());
  if (title != nullptr) {
    lv_obj_set_width(title, state->config.width - 64);
    lv_label_set_long_mode(title, LV_LABEL_LONG_DOT);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 32, title_y);
  }
  lv_obj_t* artist =
      CreateLabel(page, "Unknown Artist", lv_color_hex(kSecondaryTextColor),
          Font28());
  if (artist != nullptr && title != nullptr) {
    lv_obj_set_width(artist, state->config.width - 64);
    lv_label_set_long_mode(artist, LV_LABEL_LONG_DOT);
    lv_obj_align_to(artist, title, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 8);
  }

  lv_obj_t* slider = lv_slider_create(page);
  if (slider != nullptr) {
    lv_obj_set_size(slider, state->config.width - 64, kProgressSliderHeight);
    lv_obj_align(slider, LV_ALIGN_TOP_LEFT, 32, track_y);
    lv_slider_set_range(slider, 0, 100);
    lv_slider_set_value(slider, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_opa(slider, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(slider, 0, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(slider, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(slider, LV_OPA_TRANSP, LV_PART_INDICATOR);
    lv_obj_set_style_shadow_width(slider, 0, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(slider, LV_OPA_TRANSP, LV_PART_KNOB);
    lv_obj_set_style_shadow_width(slider, 0, LV_PART_KNOB);
    lv_obj_set_style_pad_left(slider, 0, LV_PART_KNOB);
    lv_obj_set_style_pad_right(slider, 0, LV_PART_KNOB);
    lv_obj_set_ext_click_area(slider, 12);
    lv_obj_add_event_cb(
        slider, MusicProgressDrawEventCallback, LV_EVENT_DRAW_MAIN, nullptr);
    lv_obj_add_event_cb(
        slider, MusicProgressInputEventCallback, LV_EVENT_PRESSED, state);
    lv_obj_add_event_cb(
        slider, MusicProgressInputEventCallback, LV_EVENT_PRESSING, state);
    lv_obj_add_event_cb(
        slider, MusicProgressInputEventCallback, LV_EVENT_VALUE_CHANGED, state);
    lv_obj_add_event_cb(
        slider, MusicProgressInputEventCallback, LV_EVENT_RELEASED, state);
    lv_obj_add_event_cb(
        slider, MusicProgressInputEventCallback, LV_EVENT_PRESS_LOST, state);
  }
  state->current_time_label =
      CreateLabel(page, "0:00", lv_color_hex(kSecondaryTextColor), Font24());
  if (state->current_time_label != nullptr) {
    lv_obj_align(state->current_time_label, LV_ALIGN_TOP_LEFT, 32,
        track_y + 60);
    UpdateMusicCurrentTime(state, 0);
  }
  state->total_time_label =
      CreateLabel(page, "0:00", lv_color_hex(kSecondaryTextColor), Font24());
  if (state->total_time_label != nullptr) {
    lv_obj_align(state->total_time_label, LV_ALIGN_TOP_RIGHT, -32,
        track_y + 60);
    SetMusicTimeLabel(state->total_time_label, kDefaultTrackDurationSeconds);
  }

  lv_obj_t* previous =
      CreatePlayerControlButton(page, MusicControlIcon::kSkipPrevious,
          88, kSecondaryContainerColor, kPrimaryColor);
  if (previous != nullptr) {
    lv_obj_align(previous, LV_ALIGN_BOTTOM_MID, -side_control_offset,
        side_control_bottom);
  }
  state->play_button =
      CreatePlayerControlButton(
          page, MusicControlIcon::kPlay, 116, kPrimaryColor, 0xFFFFFF);
  if (state->play_button != nullptr) {
    lv_obj_align(state->play_button, LV_ALIGN_BOTTOM_MID, 0, control_bottom);
    lv_obj_remove_event_cb(
        state->play_button, StaticControlIconDrawEventCallback);
    lv_obj_add_event_cb(
        state->play_button, PlayButtonDrawEventCallback, LV_EVENT_DRAW_MAIN,
        state);
    lv_obj_add_event_cb(
        state->play_button, PlayButtonClickedEventCallback, LV_EVENT_CLICKED,
        state);
    UpdatePlayButton(state, false);
  }
  lv_obj_t* next =
      CreatePlayerControlButton(page, MusicControlIcon::kSkipNext,
          88, kSecondaryContainerColor, kPrimaryColor);
  if (next != nullptr) {
    lv_obj_align(next, LV_ALIGN_BOTTOM_MID, side_control_offset,
        side_control_bottom);
  }

  EnableEdgeBackSwipeEventBubble(page);
  StartVerticalSlide(page, state->config.height, 0, state, nullptr);
  return true;
}

/**
 * @brief 底部迷你播放器点击事件
 * @param event LVGL 事件对象
 */
void MiniPlayerClickedEventCallback(lv_event_t* event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
    return;
  }
  auto* state = static_cast<MusicViewState*>(lv_event_get_user_data(event));
  if (state == nullptr) {
    return;
  }
  CreatePlayerPage(state);
}

/**
 * @brief 音乐视图删除时释放状态
 * @param event LVGL 事件对象
 */
void MusicViewDeleteEventCallback(lv_event_t* event) {
  if (lv_event_get_code(event) != LV_EVENT_DELETE) {
    return;
  }
  auto* state = static_cast<MusicViewState*>(lv_event_get_user_data(event));
  delete state;
}

/**
 * @brief 创建主界面的空音乐提示
 * @param parent 父对象
 * @param config 应用视图配置
 * @return 创建成功返回 true，否则返回 false
 */
bool CreateEmptyMusicContent(lv_obj_t* parent, const AppViewConfig& config) {
  lv_obj_t* group = lv_obj_create(parent);
  if (group == nullptr) {
    return false;
  }
  MakeTransparent(group);
  lv_obj_remove_flag(group, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(group, config.width, 150);
  lv_obj_align(group, LV_ALIGN_CENTER, 0, 60);

  lv_obj_t* message =
      CreateLabel(group, "No music found", lv_color_hex(kMainTextColor), Font28());
  if (message != nullptr) {
    lv_obj_align(message, LV_ALIGN_TOP_MID, 0, 0);
  }
  lv_obj_t* scan_button = lv_button_create(group);
  if (scan_button == nullptr) {
    return false;
  }
  lv_obj_set_size(scan_button, 180, 62);
  lv_obj_set_style_radius(scan_button, 31, LV_PART_MAIN);
  lv_obj_set_style_bg_color(scan_button, lv_color_hex(kPrimaryColor),
      LV_PART_MAIN);
  lv_obj_set_style_bg_opa(scan_button, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_border_width(scan_button, 0, LV_PART_MAIN);
  lv_obj_set_style_shadow_width(scan_button, 0, LV_PART_MAIN);
  lv_obj_align_to(scan_button, message, LV_ALIGN_OUT_BOTTOM_MID, 0, 26);
  lv_obj_t* scan_label =
      CreateLabel(scan_button, "Scan Music", lv_color_hex(0xFFFFFF), Font24());
  if (scan_label != nullptr) {
    lv_obj_center(scan_label);
  }
  return true;
}

/**
 * @brief 创建主界面底部迷你播放器
 * @param parent 父对象
 * @param state 音乐视图状态
 * @return 创建成功返回 true，否则返回 false
 */
bool CreateMiniPlayer(lv_obj_t* parent, MusicViewState* state) {
  lv_obj_t* card = lv_button_create(parent);
  if (card == nullptr) {
    return false;
  }
  lv_obj_set_size(card, state->config.width, kMiniPlayerHeight);
  lv_obj_align(card, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_obj_set_style_radius(card, kMiniPlayerRadius, LV_PART_MAIN);
  lv_obj_set_style_bg_color(card, lv_color_hex(kSurfaceContainerColor),
      LV_PART_MAIN);
  lv_obj_set_style_bg_opa(card, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_border_width(card, 0, LV_PART_MAIN);
  lv_obj_set_style_shadow_width(card, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(card, 16, LV_PART_MAIN);
  lv_obj_add_event_cb(
      card, MiniPlayerClickedEventCallback, LV_EVENT_CLICKED, state);

  lv_obj_t* artwork = CreateArtwork(card, 72, 14);
  if (artwork != nullptr) {
    lv_obj_align(artwork, LV_ALIGN_LEFT_MID, 0, 0);
  }
  lv_obj_t* title =
      CreateLabel(card, "Unknown Track", lv_color_hex(kMainTextColor), Font24());
  if (title != nullptr) {
    lv_obj_set_width(title, state->config.width - 270);
    lv_label_set_long_mode(title, LV_LABEL_LONG_DOT);
    lv_obj_align(title, LV_ALIGN_LEFT_MID, 90, -15);
  }
  lv_obj_t* artist =
      CreateLabel(card, "Unknown Artist", lv_color_hex(kSecondaryTextColor), Font22());
  if (artist != nullptr) {
    lv_obj_set_width(artist, state->config.width - 270);
    lv_label_set_long_mode(artist, LV_LABEL_LONG_DOT);
    lv_obj_align(artist, LV_ALIGN_LEFT_MID, 90, 19);
  }

  lv_obj_t* play = lv_button_create(card);
  if (play != nullptr) {
    lv_obj_set_size(play, 62, 62);
    lv_obj_align(play, LV_ALIGN_RIGHT_MID, -88, 0);
    lv_obj_set_style_radius(play, 31, LV_PART_MAIN);
    lv_obj_set_style_bg_color(play, lv_color_hex(kSecondaryContainerColor),
        LV_PART_MAIN);
    lv_obj_set_style_bg_opa(play, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(play, 0, LV_PART_MAIN);
    lv_obj_set_style_border_width(play, 0, LV_PART_MAIN);
    lv_obj_set_style_text_color(play, lv_color_hex(kPrimaryColor),
        LV_PART_MAIN);
    lv_obj_add_event_cb(play, StaticControlIconDrawEventCallback,
        LV_EVENT_DRAW_MAIN,
        reinterpret_cast<void*>(
            static_cast<intptr_t>(MusicControlIcon::kPlay)));
    lv_obj_add_event_cb(
        play, StopClickBubblingEventCallback, LV_EVENT_ALL, nullptr);
  }

  lv_obj_t* next = lv_button_create(card);
  if (next != nullptr) {
    lv_obj_set_size(next, 62, 62);
    lv_obj_align(next, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_set_style_radius(next, 31, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(next, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_color(next, lv_color_hex(kPrimaryColor),
        LV_PART_MAIN);
    lv_obj_set_style_border_width(next, 2, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(next, 0, LV_PART_MAIN);
    lv_obj_set_style_text_color(next, lv_color_hex(kPrimaryColor),
        LV_PART_MAIN);
    lv_obj_add_event_cb(next, StaticControlIconDrawEventCallback,
        LV_EVENT_DRAW_MAIN,
        reinterpret_cast<void*>(
            static_cast<intptr_t>(MusicControlIcon::kSkipNext)));
    lv_obj_add_event_cb(
        next, StopClickBubblingEventCallback, LV_EVENT_ALL, nullptr);
  }
  return true;
}

}  // namespace

lv_obj_t* CreateMusicView(lv_obj_t* parent, const app::AppEntry&,
    const AppViewConfig& config) {
  lv_obj_t* container = lv_obj_create(parent);
  if (container == nullptr) {
    return nullptr;
  }

  lv_obj_remove_flag(container, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(container, config.width, config.height);
  lv_obj_align(container, LV_ALIGN_CENTER, 0, 0);
  lv_obj_set_style_bg_color(container, lv_color_hex(kMainBackgroundColor),
      LV_PART_MAIN);
  lv_obj_set_style_bg_opa(container, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_border_width(container, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(container, 0, LV_PART_MAIN);

  if (config.set_status_bar_visible) {
    config.set_status_bar_visible(true);
  }
  if (config.set_status_bar_text_color) {
    config.set_status_bar_text_color(kMainTextColor);
  }

  auto* state = new MusicViewState();
  state->config = config;
  state->root = container;
  lv_obj_add_event_cb(
      container, MusicViewDeleteEventCallback, LV_EVENT_DELETE, state);

  lv_obj_t* title =
      CreateLabel(container, "Music", lv_color_hex(kMainTextColor), Font48());
  if (title == nullptr) {
    lv_obj_delete(container);
    return nullptr;
  }
  lv_obj_align(title, LV_ALIGN_TOP_LEFT, kMainPadding, kHeaderTop);

  lv_obj_t* tab =
      CreateLabel(container, "Songs", lv_color_hex(kPrimaryColor), Font28());
  if (tab == nullptr) {
    lv_obj_delete(container);
    return nullptr;
  }
  lv_obj_align(tab, LV_ALIGN_TOP_LEFT, kMainPadding + 8, kTabTop);
  lv_obj_t* underline = lv_obj_create(container);
  if (underline != nullptr) {
    lv_obj_remove_flag(underline, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(underline, 58, 6);
    lv_obj_set_style_radius(underline, 3, LV_PART_MAIN);
    lv_obj_set_style_bg_color(underline, lv_color_hex(kPrimaryColor),
        LV_PART_MAIN);
    lv_obj_set_style_bg_opa(underline, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(underline, 0, LV_PART_MAIN);
    lv_obj_align_to(underline, tab, LV_ALIGN_OUT_BOTTOM_MID, 0, 18);
  }

  if (!CreateEmptyMusicContent(container, config) ||
      !CreateMiniPlayer(container, state)) {
    LogMessage(LogLevel::kError, __FILE__, __LINE__,
        "CreateMusicView content failed\n");
    lv_obj_delete(container);
    return nullptr;
  }

  return container;
}

}  // namespace lilygo_box::ui
