/*
 * @Description: 音乐应用视图
 * @Author: LILYGO_L
 * @Date: 2026-07-08 00:00:00
 * @LastEditTime: 2026-07-14 22:36:01
 * @License: GPL 3.0
 */
#include "ui/views/music_view.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <functional>
#include <string>

#include "base/logger.h"
#include "ui/animation/transition_animation.h"
#include "ui/resources/fonts/font_assets.h"
#include "ui/resources/fonts/icon_assets.h"
#include "ui/input/edge_back_gesture.h"
#include "ui/theme/theme_provider.h"
#include "ui/views/files_view.h"
#include "ui/widgets/navigation_drawer.h"
#include "ui/widgets/prompt/prompt_dialog.h"
#include "ui/widgets/prompt/prompt_select_sheet.h"

namespace lilygo_box::ui {
namespace {

constexpr uint32_t kMainBackgroundColor = 0xFEF9F6;
constexpr uint32_t kSurfaceContainerColor = 0xFCF1EF;
constexpr uint32_t kSecondaryContainerColor = 0xF8DBD5;
constexpr uint32_t kPrimaryColor = 0x874E43;
constexpr uint32_t kMainTextColor = 0x24201F;
constexpr uint32_t kSecondaryTextColor = 0x716864;
constexpr uint32_t kDividerColor = 0xDED8D5;
constexpr uint32_t kPressedColor = 0xF0E5E2;

constexpr int kMainPadding = 32;
constexpr int kHeaderTop = 68;
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
constexpr int kSettingsAnimationMs = 240;
constexpr int kMusicSourcesHeaderTop = 112;
constexpr int kMusicSourcesAddTop = 98;
constexpr int kMusicSourcesListTop = 174;

constexpr int kMusicFolderOptionCount = 8;
constexpr PromptSelectSheetOption kMusicFolderOptions[] = {
    {0, "/sdcard/Music"},
    {1, "/sdcard/Download"},
    {2, "/sdcard"},
    {3, "/sdcard/Podcasts"},
    {4, "/sdcard/Recordings"},
    {5, "/sdcard/Audiobooks"},
    {6, "/sdcard/Notifications"},
    {7, "/sdcard/Ringtones"},
};

enum class MusicControlIcon {
  kPlay,
  kPause,
  kSkipPrevious,
  kSkipNext,
};

enum class MusicPlaybackMode {
  kRepeatAll,
  kRepeatOne,
  kShuffle,
};

struct MusicViewState {
  AppViewConfig config;
  lv_obj_t* root = nullptr;
  lv_obj_t* player_page = nullptr;
  lv_obj_t* play_button = nullptr;
  lv_obj_t* playback_mode_label = nullptr;
  lv_obj_t* current_time_label = nullptr;
  lv_obj_t* total_time_label = nullptr;
  lv_obj_t* settings_page = nullptr;
  lv_obj_t* sources_page = nullptr;
  lv_obj_t* sources_body = nullptr;
  lv_obj_t* sources_summary_label = nullptr;
  NavigationDrawerState drawer;
  PromptSelectSheetState folder_select_sheet;
  PromptDialogState sources_dialog;
  PromptDialogState folder_dialog;
  EdgeBackSwipeState player_edge_swipe;
  EdgeBackSwipeState settings_edge_swipe;
  EdgeBackSwipeState sources_edge_swipe;
  int selected_folder = 0;
  bool source_enabled[kMusicFolderOptionCount] = {};
  bool draft_source_enabled[kMusicFolderOptionCount] = {};
  bool picker_source_enabled[kMusicFolderOptionCount] = {};
  std::array<std::string, kMusicFolderOptionCount> source_paths;
  std::array<std::string, kMusicFolderOptionCount> draft_source_paths;
  bool playing = false;
  MusicPlaybackMode playback_mode = MusicPlaybackMode::kRepeatAll;
  bool settings_closing = false;
  bool sources_closing = false;
};

struct MusicSourceAction {
  MusicViewState* state = nullptr;
  lv_obj_t* row = nullptr;
  lv_obj_t* check = nullptr;
  int source = 0;
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
 * @brief 获取 36 号 Google Sans 字体
 * @return 字体指针
 */
const lv_font_t* Font36() { return &lvgl_font_google_sans_flex_36; }

/**
 * @brief 获取 48 号 Google Sans 字体
 * @return 字体指针
 */
const lv_font_t* Font48() { return &lvgl_font_google_sans_flex_48; }

/**
 * @brief 获取 56 号轮廓 Material Symbols 字体
 * @return 字体指针
 */
const lv_font_t* MaterialOutlineIconFont56() {
  return &lvgl_font_material_symbols_outline_56;
}

/**
 * @brief 获取 32 号填充 Material Symbols 字体
 * @return 字体指针
 */
const lv_font_t* MaterialFillIconFont32() {
  return &lvgl_font_material_symbols_fill_32;
}

/**
 * @brief 获取 44 号轮廓 Material Symbols 字体
 * @return 字体指针
 */
const lv_font_t* MaterialOutlineIconFont44() {
  return &lvgl_font_material_symbols_outline_44;
}

/**
 * @brief 获取 44 号填充 Material Symbols 字体
 * @return 字体指针
 */
const lv_font_t* MaterialFillIconFont44() {
  return &lvgl_font_material_symbols_fill_44;
}

/**
 * @brief 获取 56 号填充 Material Symbols 字体
 * @return 字体指针
 */
const lv_font_t* MaterialFillIconFont56() {
  return &lvgl_font_material_symbols_fill_56;
}

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
  state->playback_mode_label = nullptr;
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
 * @brief 获取播放模式对应的 Material Symbols 图标
 * @param mode 播放模式
 * @return 播放模式对应的 UTF-8 图标文本
 */
const char* PlaybackModeIcon(MusicPlaybackMode mode) {
  switch (mode) {
    case MusicPlaybackMode::kRepeatAll:
      return icon::kRepeat;
    case MusicPlaybackMode::kRepeatOne:
      return icon::kRepeatOne;
    case MusicPlaybackMode::kShuffle:
      return icon::kShuffle;
  }
  return icon::kRepeat;
}

/**
 * @brief 刷新播放模式按钮图标
 * @param state 音乐视图状态
 */
void UpdatePlaybackModeButton(MusicViewState* state) {
  if (state == nullptr || state->playback_mode_label == nullptr) {
    return;
  }
  lv_label_set_text(
      state->playback_mode_label, PlaybackModeIcon(state->playback_mode));
  lv_obj_center(state->playback_mode_label);
}

/**
 * @brief 切换列表循环、单曲循环和随机播放模式
 * @param event LVGL 事件对象
 */
void PlaybackModeClickedEventCallback(lv_event_t* event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
    return;
  }
  auto* state = static_cast<MusicViewState*>(lv_event_get_user_data(event));
  if (state == nullptr) {
    return;
  }
  switch (state->playback_mode) {
    case MusicPlaybackMode::kRepeatAll:
      state->playback_mode = MusicPlaybackMode::kRepeatOne;
      break;
    case MusicPlaybackMode::kRepeatOne:
      state->playback_mode = MusicPlaybackMode::kShuffle;
      break;
    case MusicPlaybackMode::kShuffle:
      state->playback_mode = MusicPlaybackMode::kRepeatAll;
      break;
  }
  UpdatePlaybackModeButton(state);
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
          MaterialOutlineIconFont56());
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
          lv_color_hex(kMainTextColor), MaterialOutlineIconFont56());
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

  lv_obj_t* playback_mode_button =
      previous != nullptr ? lv_button_create(page) : nullptr;
  if (playback_mode_button != nullptr) {
    const lv_style_selector_t pressed_selector =
        static_cast<lv_style_selector_t>(LV_PART_MAIN) |
        static_cast<lv_style_selector_t>(LV_STATE_PRESSED);
    MakeTransparent(playback_mode_button);
    lv_obj_remove_flag(playback_mode_button, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(playback_mode_button, LV_OBJ_FLAG_CLICK_FOCUSABLE);
    lv_obj_set_size(playback_mode_button, 72, 72);
    lv_obj_align_to(playback_mode_button, previous, LV_ALIGN_OUT_LEFT_MID,
        -20, 0);
    lv_obj_set_style_radius(playback_mode_button, 36, LV_PART_MAIN);
    lv_obj_set_style_bg_color(playback_mode_button,
        lv_color_hex(kSecondaryContainerColor), LV_PART_MAIN);
    lv_obj_set_style_outline_width(playback_mode_button, 0, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(playback_mode_button, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_color(playback_mode_button,
        lv_color_hex(kSecondaryContainerColor), pressed_selector);
    lv_obj_set_style_bg_opa(playback_mode_button, LV_OPA_COVER,
        pressed_selector);
    lv_obj_set_style_radius(playback_mode_button, 36, pressed_selector);
    state->playback_mode_label =
        CreateLabel(playback_mode_button,
            PlaybackModeIcon(state->playback_mode),
            lv_color_hex(kPrimaryColor), MaterialOutlineIconFont56());
    if (state->playback_mode_label != nullptr) {
      lv_obj_center(state->playback_mode_label);
    }
    lv_obj_add_event_cb(playback_mode_button,
        PlaybackModeClickedEventCallback, LV_EVENT_CLICKED, state);
    lv_obj_add_event_cb(playback_mode_button,
        StopClickBubblingEventCallback, LV_EVENT_ALL, nullptr);
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
  lv_obj_set_size(scan_button, 230, 62);
  lv_obj_set_style_radius(scan_button, 31, LV_PART_MAIN);
  lv_obj_set_style_bg_color(scan_button, lv_color_hex(kPrimaryColor),
      LV_PART_MAIN);
  lv_obj_set_style_bg_opa(scan_button, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_border_width(scan_button, 0, LV_PART_MAIN);
  lv_obj_set_style_shadow_width(scan_button, 0, LV_PART_MAIN);
  lv_obj_align_to(scan_button, message, LV_ALIGN_OUT_BOTTOM_MID, 0, 26);
  lv_obj_t* scan_label =
      CreateLabel(scan_button, "Refresh Music", lv_color_hex(0xFFFFFF),
          Font24());
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

/**
 * @brief 根据文件夹选项值获取对应路径
 * @param value 文件夹选项值
 * @return 对应的文件夹路径
 */
const char* MusicFolderPath(int value) {
  for (const auto& option : kMusicFolderOptions) {
    if (option.value == value) {
      return option.text;
    }
  }
  return kMusicFolderOptions[0].text;
}

/**
 * @brief 重新构建旧版音乐源页面内容
 * @param state 音乐视图状态
 * @return 构建成功返回 true，否则返回 false
 */
bool RenderMusicSourcesContent(MusicViewState* state);

/**
 * @brief 统计当前启用的音乐源数量
 * @param state 音乐视图状态
 * @return 已启用的音乐源数量
 */
int MusicSourceCount(const MusicViewState* state) {
  if (state == nullptr) {
    return 0;
  }
  int count = 0;
  for (bool enabled : state->source_enabled) {
    if (enabled) {
      ++count;
    }
  }
  return count;
}

/**
 * @brief 更新音乐设置页中的音乐源数量摘要
 * @param state 音乐视图状态
 */
void UpdateMusicSourcesSummary(MusicViewState* state) {
  if (state == nullptr || state->sources_summary_label == nullptr) {
    return;
  }
  const int count = MusicSourceCount(state);
  char text[24] = {};
  if (count == 0) {
    std::snprintf(text, sizeof(text), "No folders");
  } else {
    std::snprintf(text, sizeof(text), "%d %s", count,
                  count == 1 ? "folder" : "folders");
  }
  lv_label_set_text(state->sources_summary_label, text);
}

/**
 * @brief 处理旧版文件夹选择弹窗的确认结果
 * @param context 音乐视图状态
 * @param value 选中的文件夹选项值
 */
void MusicFolderSelectedCallback(void* context, int value) {
  auto* state = static_cast<MusicViewState*>(context);
  if (state == nullptr || value < 0 ||
      value >= kMusicFolderOptionCount) {
    return;
  }
  state->selected_folder = value;
  state->source_enabled[value] = true;
  UpdateMusicSourcesSummary(state);
  RenderMusicSourcesContent(state);
}

/**
 * @brief 显示旧版音乐文件夹选择底部弹窗
 * @param state 音乐视图状态
 * @return 显示成功返回 true，否则返回 false
 */
bool ShowMusicFolderSheet(MusicViewState* state) {
  if (state == nullptr || state->root == nullptr) {
    return false;
  }

  PromptSelectSheetConfig config;
  config.screen_width = state->config.width;
  config.screen_height = state->config.height;
  config.sheet_width = state->config.width - 32;
  config.sheet_height = std::min(state->config.height - 32, 510);
  config.side_margin = 16;
  config.bottom_margin = 16;
  config.sheet_radius = 28;
  config.inner_padding = 24;
  config.option_top = 142;
  config.option_height = 68;
  config.button_height = 60;
  config.button_radius = 30;
  config.sheet_color = kMainBackgroundColor;
  config.selected_color = kSecondaryContainerColor;
  config.primary_text_color = kMainTextColor;
  config.secondary_text_color = kSecondaryTextColor;
  config.selected_text_color = kPrimaryColor;
  config.cancel_background_color = kSurfaceContainerColor;
  config.pressed_color = kPressedColor;
  config.title = "Add music source";
  config.message = "Choose a folder to include in music searches.";
  config.check_icon = icon::kCheck;
  config.options = kMusicFolderOptions;
  config.option_count =
      sizeof(kMusicFolderOptions) / sizeof(kMusicFolderOptions[0]);
  config.selected_value = state->selected_folder;
  config.title_font = Font32();
  config.message_font = Font24();
  config.option_font = Font24();
  config.cancel_font = Font24();
  config.icon_font = MaterialFillIconFont32();
  config.state = &state->folder_select_sheet;
  config.callback = MusicFolderSelectedCallback;
  config.callback_context = state;
  return ShowPromptSelectSheet(state->root, config);
}

/**
 * @brief 显示旧版音乐源管理页面
 * @param state 音乐视图状态
 * @return 显示成功返回 true，否则返回 false
 */
bool ShowMusicSourcesPage(MusicViewState* state);

/**
 * @brief 关闭旧版音乐源管理页面
 * @param state 音乐视图状态
 */
void CloseMusicSourcesPage(MusicViewState* state);

/**
 * @brief 显示音乐源管理弹窗
 * @param state 音乐视图状态
 * @return 显示成功返回 true，否则返回 false
 */
bool ShowMusicSourcesPrompt(MusicViewState* state);

/**
 * @brief 处理音乐设置页音乐源行点击事件
 * @param event LVGL 事件对象
 */
void MusicSourcesRowClickedEventCallback(lv_event_t* event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
    return;
  }
  ShowMusicSourcesPrompt(
      static_cast<MusicViewState*>(lv_event_get_user_data(event)));
  lv_event_stop_bubbling(event);
  lv_event_stop_processing(event);
}

/**
 * @brief 处理音乐设置页退出动画完成事件
 * @param animation LVGL 动画对象
 */
void SettingsCloseCompletedCallback(lv_anim_t* animation) {
  auto* state = static_cast<MusicViewState*>(
      lv_anim_get_user_data(animation));
  if (state == nullptr || state->settings_page == nullptr) {
    return;
  }
  lv_obj_t* page = state->settings_page;
  state->settings_page = nullptr;
  state->sources_summary_label = nullptr;
  state->settings_closing = false;
  state->settings_edge_swipe = EdgeBackSwipeState();
  lv_obj_delete(page);
}

/**
 * @brief 使用退出动画关闭音乐设置页面
 * @param state 音乐视图状态
 */
void CloseMusicSettingsPage(MusicViewState* state) {
  if (state == nullptr || state->settings_page == nullptr ||
      state->settings_closing) {
    return;
  }
  if (state->sources_page != nullptr) {
    CloseMusicSourcesPage(state);
    return;
  }
  state->settings_closing = true;
  if (!StartSlideRightWindowTransition(state->settings_page,
      state->config.width, kSettingsAnimationMs, state,
      SettingsCloseCompletedCallback)) {
    lv_obj_t* page = state->settings_page;
    state->settings_page = nullptr;
    state->sources_summary_label = nullptr;
    state->settings_closing = false;
    state->settings_edge_swipe = EdgeBackSwipeState();
    lv_obj_delete(page);
  }
}

/**
 * @brief 处理音乐设置页返回按钮点击事件
 * @param event LVGL 事件对象
 */
void SettingsBackClickedEventCallback(lv_event_t* event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
    return;
  }
  CloseMusicSettingsPage(
      static_cast<MusicViewState*>(lv_event_get_user_data(event)));
}

/**
 * @brief 处理音乐设置页边缘返回手势
 * @param event LVGL 事件对象
 */
void SettingsEdgeBackEventCallback(lv_event_t* event) {
  auto* state = static_cast<MusicViewState*>(lv_event_get_user_data(event));
  if (state == nullptr || state->settings_page == nullptr ||
      !HandleEdgeBackSwipeEvent(event, state->config.width,
          &state->settings_edge_swipe)) {
    return;
  }
  CloseMusicSettingsPage(state);
  lv_event_stop_bubbling(event);
  lv_event_stop_processing(event);
}

/**
 * @brief 创建音乐设置页音乐源入口行
 * @param page 设置页面对象
 * @param state 音乐视图状态
 * @return 创建成功返回 true，否则返回 false
 */
bool CreateMusicSourcesSettingRow(
    lv_obj_t* page, MusicViewState* state) {
  lv_obj_t* row = lv_button_create(page);
  if (row == nullptr) {
    return false;
  }
  lv_obj_remove_style_all(row);
  lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(row, state->config.width, 120);
  lv_obj_align(row, LV_ALIGN_TOP_MID, 0, 300);
  lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_set_style_bg_color(row, lv_color_hex(kSurfaceContainerColor),
                            LV_STATE_PRESSED);
  lv_obj_set_style_bg_opa(row, LV_OPA_COVER, LV_STATE_PRESSED);
  lv_obj_set_style_radius(row, 0, LV_PART_MAIN);
  lv_obj_set_style_border_width(row, 0, LV_PART_MAIN);
  lv_obj_set_style_shadow_width(row, 0, LV_PART_MAIN);
  lv_obj_add_event_cb(row, MusicSourcesRowClickedEventCallback,
                      LV_EVENT_CLICKED, state);

  lv_obj_t* title = CreateLabel(
      row, "Music sources", lv_color_hex(kMainTextColor), Font28());
  if (title != nullptr) {
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 34, 23);
  }
  lv_obj_t* subtitle = CreateLabel(row, "Manage music loading locations",
      lv_color_hex(kSecondaryTextColor), Font24());
  if (subtitle != nullptr) {
    lv_obj_set_width(subtitle, state->config.width - 68);
    lv_label_set_long_mode(subtitle, LV_LABEL_LONG_DOT);
    lv_obj_align(subtitle, LV_ALIGN_TOP_LEFT, 34, 65);
  }
  return true;
}

/**
 * @brief 创建与系统设置页一致的标题和返回按钮
 * @param page 页面对象
 * @param title 页面标题
 * @param callback 返回按钮点击回调
 * @param state 音乐视图状态
 * @return 创建成功返回 true，否则返回 false
 */
bool CreateSettingsStyleHeader(lv_obj_t* page, const char* title,
    lv_event_cb_t callback, MusicViewState* state) {
  lv_obj_t* back = lv_button_create(page);
  if (back == nullptr) {
    return false;
  }
  lv_obj_remove_style_all(back);
  lv_obj_remove_flag(back, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(back, LV_OBJ_FLAG_PRESS_LOCK);
  lv_obj_add_flag(back, LV_OBJ_FLAG_GESTURE_BUBBLE);
  lv_obj_set_size(back, 62, 62);
  lv_obj_set_pos(back, 18, 66);
  lv_obj_set_style_bg_opa(back, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_set_style_bg_opa(back, LV_OPA_TRANSP, LV_STATE_PRESSED);
  lv_obj_add_event_cb(back, callback, LV_EVENT_CLICKED, state);

  lv_obj_t* back_icon = CreateLabel(back, icon::kArrowBack,
      lv_color_hex(kMainTextColor), MaterialOutlineIconFont44());
  if (back_icon == nullptr) {
    return false;
  }
  lv_obj_align(back_icon, LV_ALIGN_CENTER, -4, 0);

  lv_obj_t* title_label = CreateLabel(
      page, title, lv_color_hex(kMainTextColor), Font48());
  if (title_label == nullptr) {
    return false;
  }
  lv_obj_align(title_label, LV_ALIGN_TOP_LEFT, 34, 154);
  return true;
}

/**
 * @brief 释放音乐源操作事件上下文
 * @param event LVGL 事件对象
 */
void MusicSourceActionDeleteEventCallback(lv_event_t* event) {
  if (lv_event_get_code(event) == LV_EVENT_DELETE) {
    delete static_cast<MusicSourceAction*>(lv_event_get_user_data(event));
  }
}

/**
 * @brief 处理旧版音乐源删除按钮点击事件
 * @param event LVGL 事件对象
 */
void MusicSourceRemoveClickedEventCallback(lv_event_t* event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
    return;
  }
  auto* action = static_cast<MusicSourceAction*>(
      lv_event_get_user_data(event));
  if (action == nullptr || action->state == nullptr || action->source < 0 ||
      action->source >= kMusicFolderOptionCount) {
    return;
  }
  action->state->source_enabled[action->source] = false;
  if (action->row != nullptr) {
    lv_obj_add_flag(action->row, LV_OBJ_FLAG_HIDDEN);
  }
  UpdateMusicSourcesSummary(action->state);
  lv_event_stop_bubbling(event);
  lv_event_stop_processing(event);
}

/**
 * @brief 处理旧版添加音乐源按钮点击事件
 * @param event LVGL 事件对象
 */
void AddMusicSourceClickedEventCallback(lv_event_t* event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
    return;
  }
  ShowMusicFolderSheet(
      static_cast<MusicViewState*>(lv_event_get_user_data(event)));
  lv_event_stop_bubbling(event);
  lv_event_stop_processing(event);
}

/**
 * @brief 创建旧版音乐源页面中的文件夹行
 * @param state 音乐视图状态
 * @param source 音乐源索引
 * @param y 行顶部坐标
 * @return 创建成功返回 true，否则返回 false
 */
bool CreateMusicSourceRow(
    MusicViewState* state, int source, int y) {
  lv_obj_t* row = lv_obj_create(state->sources_body);
  if (row == nullptr) {
    return false;
  }
  lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(row, LV_OBJ_FLAG_GESTURE_BUBBLE);
  lv_obj_set_size(row, state->config.width - 32, 100);
  lv_obj_set_pos(row, 16, y);
  lv_obj_set_style_bg_color(row, lv_color_hex(kSurfaceContainerColor),
                            LV_PART_MAIN);
  lv_obj_set_style_bg_opa(row, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_border_width(row, 0, LV_PART_MAIN);
  lv_obj_set_style_radius(row, 20, LV_PART_MAIN);
  lv_obj_set_style_pad_all(row, 0, LV_PART_MAIN);

  lv_obj_t* folder_icon = CreateLabel(row, icon::kFolder,
      lv_color_hex(kPrimaryColor), MaterialFillIconFont44());
  if (folder_icon != nullptr) {
    lv_obj_align(folder_icon, LV_ALIGN_LEFT_MID, 18, 0);
  }
  lv_obj_t* path = CreateLabel(row, MusicFolderPath(source),
      lv_color_hex(kMainTextColor), Font24());
  if (path != nullptr) {
    lv_obj_set_width(path, state->config.width - 190);
    lv_label_set_long_mode(path, LV_LABEL_LONG_DOT);
    lv_obj_align(path, LV_ALIGN_TOP_LEFT, 74, 18);
  }
  lv_obj_t* detail = CreateLabel(row, "Included in library search",
      lv_color_hex(kSecondaryTextColor), Font22());
  if (detail != nullptr) {
    lv_obj_align(detail, LV_ALIGN_TOP_LEFT, 74, 56);
  }

  lv_obj_t* remove = lv_button_create(row);
  if (remove == nullptr) {
    return false;
  }
  lv_obj_remove_style_all(remove);
  lv_obj_add_flag(remove, LV_OBJ_FLAG_GESTURE_BUBBLE);
  lv_obj_set_size(remove, 54, 54);
  lv_obj_align(remove, LV_ALIGN_RIGHT_MID, -10, 0);
  auto* action = new MusicSourceAction{
      .state = state,
      .row = row,
      .source = source,
  };
  lv_obj_add_event_cb(remove, MusicSourceRemoveClickedEventCallback,
                      LV_EVENT_CLICKED, action);
  lv_obj_add_event_cb(remove, MusicSourceActionDeleteEventCallback,
                      LV_EVENT_DELETE, action);
  lv_obj_t* close_icon = CreateLabel(remove, icon::kClose,
      lv_color_hex(kSecondaryTextColor), MaterialOutlineIconFont44());
  if (close_icon != nullptr) {
    lv_obj_center(close_icon);
  }
  return true;
}

bool RenderMusicSourcesContent(MusicViewState* state) {
  if (state == nullptr || state->sources_body == nullptr) {
    return false;
  }
  lv_obj_clean(state->sources_body);

  lv_obj_t* section = CreateLabel(state->sources_body, "SEARCH FOLDERS",
      lv_color_hex(kPrimaryColor), Font22());
  if (section != nullptr) {
    lv_obj_set_pos(section, 28, 12);
  }

  int y = 52;
  for (int source = 0; source < kMusicFolderOptionCount; ++source) {
    if (!state->source_enabled[source]) {
      continue;
    }
    if (!CreateMusicSourceRow(state, source, y)) {
      return false;
    }
    y += 112;
  }
  if (MusicSourceCount(state) == 0) {
    lv_obj_t* empty = CreateLabel(state->sources_body,
        "No music source folders", lv_color_hex(kSecondaryTextColor),
        Font24());
    if (empty != nullptr) {
      lv_obj_set_pos(empty, 28, y + 18);
    }
    y += 76;
  }

  lv_obj_t* add = lv_button_create(state->sources_body);
  if (add == nullptr) {
    return false;
  }
  lv_obj_set_size(add, 190, 62);
  lv_obj_set_pos(add, 28, y + 16);
  lv_obj_set_style_bg_color(add, lv_color_hex(kPrimaryColor), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(add, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_border_width(add, 0, LV_PART_MAIN);
  lv_obj_set_style_shadow_width(add, 0, LV_PART_MAIN);
  lv_obj_set_style_radius(add, 31, LV_PART_MAIN);
  lv_obj_add_event_cb(add, AddMusicSourceClickedEventCallback,
                      LV_EVENT_CLICKED, state);
  lv_obj_t* add_label = CreateLabel(
      add, "Add folder", lv_color_hex(0xFFFFFF), Font24());
  if (add_label != nullptr) {
    lv_obj_center(add_label);
  }
  return true;
}

/**
 * @brief 处理旧版音乐源页面退出动画完成事件
 * @param animation LVGL 动画对象
 */
void SourcesCloseCompletedCallback(lv_anim_t* animation) {
  auto* state = static_cast<MusicViewState*>(
      lv_anim_get_user_data(animation));
  if (state == nullptr || state->sources_page == nullptr) {
    return;
  }
  lv_obj_t* page = state->sources_page;
  state->sources_page = nullptr;
  state->sources_body = nullptr;
  state->sources_closing = false;
  state->sources_edge_swipe = EdgeBackSwipeState();
  lv_obj_delete(page);
}

void CloseMusicSourcesPage(MusicViewState* state) {
  if (state == nullptr || state->sources_page == nullptr ||
      state->sources_closing) {
    return;
  }
  state->sources_closing = true;
  if (!StartSlideRightWindowTransition(state->sources_page,
      state->config.width, kSettingsAnimationMs, state,
      SourcesCloseCompletedCallback)) {
    lv_obj_t* page = state->sources_page;
    state->sources_page = nullptr;
    state->sources_body = nullptr;
    state->sources_closing = false;
    state->sources_edge_swipe = EdgeBackSwipeState();
    lv_obj_delete(page);
  }
}

/**
 * @brief 处理旧版音乐源页面返回按钮点击事件
 * @param event LVGL 事件对象
 */
void SourcesBackClickedEventCallback(lv_event_t* event) {
  if (lv_event_get_code(event) == LV_EVENT_CLICKED) {
    CloseMusicSourcesPage(
        static_cast<MusicViewState*>(lv_event_get_user_data(event)));
  }
}

/**
 * @brief 处理旧版音乐源页面边缘返回手势
 * @param event LVGL 事件对象
 */
void SourcesEdgeBackEventCallback(lv_event_t* event) {
  auto* state = static_cast<MusicViewState*>(lv_event_get_user_data(event));
  if (state == nullptr || state->sources_page == nullptr ||
      !HandleEdgeBackSwipeEvent(event, state->config.width,
          &state->sources_edge_swipe)) {
    return;
  }
  CloseMusicSourcesPage(state);
  lv_event_stop_bubbling(event);
  lv_event_stop_processing(event);
}

[[maybe_unused]] bool ShowMusicSourcesPage(MusicViewState* state) {
  if (state == nullptr || state->root == nullptr ||
      state->settings_page == nullptr) {
    return false;
  }
  if (state->sources_page != nullptr) {
    lv_obj_move_to_index(state->sources_page, -1);
    return true;
  }

  lv_obj_t* page = lv_obj_create(state->root);
  if (page == nullptr) {
    return false;
  }
  state->sources_page = page;
  state->sources_closing = false;
  state->sources_edge_swipe = EdgeBackSwipeState();
  lv_obj_remove_flag(page, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(page, LV_OBJ_FLAG_GESTURE_BUBBLE);
  lv_obj_set_size(page, state->config.width, state->config.height);
  lv_obj_set_pos(page, 0, 0);
  lv_obj_set_style_bg_color(page, lv_color_hex(kMainBackgroundColor),
                            LV_PART_MAIN);
  lv_obj_set_style_bg_opa(page, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_border_width(page, 0, LV_PART_MAIN);
  lv_obj_set_style_radius(page, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(page, 0, LV_PART_MAIN);
  AddEdgeBackSwipeEvents(page, SourcesEdgeBackEventCallback, state);

  if (!CreateSettingsStyleHeader(
      page, "Music sources", SourcesBackClickedEventCallback, state)) {
    lv_obj_delete(page);
    state->sources_page = nullptr;
    return false;
  }
  state->sources_body = lv_obj_create(page);
  if (state->sources_body == nullptr) {
    lv_obj_delete(page);
    state->sources_page = nullptr;
    return false;
  }
  MakeTransparent(state->sources_body);
  lv_obj_set_size(state->sources_body, state->config.width,
                  state->config.height - 238);
  lv_obj_set_pos(state->sources_body, 0, 238);
  lv_obj_set_scroll_dir(state->sources_body, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(state->sources_body, LV_SCROLLBAR_MODE_OFF);
  lv_obj_add_flag(state->sources_body, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(state->sources_body, LV_OBJ_FLAG_GESTURE_BUBBLE);
  AddEdgeBackSwipeEvents(
      state->sources_body, SourcesEdgeBackEventCallback, state);
  if (!RenderMusicSourcesContent(state)) {
    lv_obj_delete(page);
    state->sources_page = nullptr;
    state->sources_body = nullptr;
    return false;
  }

  EnableEdgeBackSwipeEventBubble(page);
  if (!StartSlideLeftWindowTransition(page, state->config.width,
      kSettingsAnimationMs, state, nullptr)) {
    lv_obj_delete(page);
    state->sources_page = nullptr;
    state->sources_body = nullptr;
    return false;
  }
  return true;
}

/**
 * @brief 复制音乐源文件夹选择状态
 * @param destination 目标状态数组
 * @param source 来源状态数组
 */
void CopyMusicSourceFlags(bool* destination, const bool* source) {
  if (destination == nullptr || source == nullptr) {
    return;
  }
  for (int i = 0; i < kMusicFolderOptionCount; ++i) {
    destination[i] = source[i];
  }
}

/**
 * @brief 创建音乐页面公共提示框配置
 * @param state 音乐视图状态
 * @param title 提示框标题
 * @return 提示框配置
 */
PromptDialogConfig CreateMusicPromptConfig(
    MusicViewState* state, const char* title) {
  PromptDialogConfig config;
  config.screen_width = state->config.width;
  config.screen_height = state->config.height;
  config.dialog_width = state->config.width - 68;
  config.dialog_height = std::min(state->config.height - 64, 650);
  config.dialog_radius = 48;
  config.inner_padding = 32;
  config.header_height = 120;
  config.title_y = 42;
  config.action_height = 106;
  config.action_button_height = 74;
  config.action_button_gap = 20;
  config.action_bottom_padding = 32;
  config.dialog_color = kSurfaceContainerColor;
  config.primary_text_color = kMainTextColor;
  config.secondary_text_color = kSecondaryTextColor;
  config.divider_color = kDividerColor;
  config.pressed_color = kPressedColor;
  config.confirm_background_color = kPrimaryColor;
  config.confirm_pressed_color = kPrimaryColor;
  config.confirm_text_color = 0xFFFFFF;
  config.overlay_opacity = 115;
  config.animation_ms = 180;
  config.title = title;
  config.title_font = Font36();
  config.action_font = Font28();
  config.callback_context = state;
  return config;
}

/**
 * @brief 重新构建音乐源管理弹窗的可滚动列表
 * @param state 音乐视图状态
 * @return 构建成功返回 true，否则返回 false
 */
bool RenderMusicSourcesPromptContent(MusicViewState* state);

/**
 * @brief 在当前点击事件结束后重新排列音乐源列表
 * @param context 音乐视图状态
 */
void RebuildMusicSourcesPromptAsync(void* context) {
  RenderMusicSourcesPromptContent(
      static_cast<MusicViewState*>(context));
}

/**
 * @brief 保存音乐源提示框中的文件夹配置
 * @param context 音乐视图状态
 */
void MusicSourcesPromptSavedCallback(void* context) {
  auto* state = static_cast<MusicViewState*>(context);
  if (state == nullptr) {
    return;
  }
  CopyMusicSourceFlags(
      state->source_enabled, state->draft_source_enabled);
  state->source_paths = state->draft_source_paths;
  for (int source = 0; source < kMusicFolderOptionCount; ++source) {
    state->source_enabled[source] =
        !state->source_paths[source].empty();
  }
  UpdateMusicSourcesSummary(state);
}

/**
 * @brief 处理音乐源提示框文件夹移除事件
 * @param event LVGL 事件对象
 */
void MusicSourcesPromptRemoveClickedEventCallback(lv_event_t* event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
    return;
  }
  auto* action = static_cast<MusicSourceAction*>(
      lv_event_get_user_data(event));
  if (action == nullptr || action->state == nullptr || action->source < 0 ||
      action->source >= kMusicFolderOptionCount) {
    return;
  }
  action->state->draft_source_enabled[action->source] = false;
  action->state->draft_source_paths[action->source].clear();
  lv_async_call(RebuildMusicSourcesPromptAsync, action->state);
  lv_event_stop_bubbling(event);
  lv_event_stop_processing(event);
}

/**
 * @brief 确认文件夹选择并更新音乐源草稿
 * @param context 音乐视图状态
 */
void MusicFolderPickerConfirmedCallback(void* context) {
  auto* state = static_cast<MusicViewState*>(context);
  if (state == nullptr) {
    return;
  }
  CopyMusicSourceFlags(
      state->draft_source_enabled, state->picker_source_enabled);
  RenderMusicSourcesPromptContent(state);
}

/**
 * @brief 处理文件夹选择行点击事件
 * @param event LVGL 事件对象
 */
void MusicFolderPickerRowClickedEventCallback(lv_event_t* event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
    return;
  }
  auto* action = static_cast<MusicSourceAction*>(
      lv_event_get_user_data(event));
  if (action == nullptr || action->state == nullptr || action->source < 0 ||
      action->source >= kMusicFolderOptionCount) {
    return;
  }
  bool& selected = action->state->picker_source_enabled[action->source];
  selected = !selected;
  if (action->check != nullptr) {
    if (selected) {
      lv_obj_remove_flag(action->check, LV_OBJ_FLAG_HIDDEN);
    } else {
      lv_obj_add_flag(action->check, LV_OBJ_FLAG_HIDDEN);
    }
  }
  lv_event_stop_bubbling(event);
  lv_event_stop_processing(event);
}

/**
 * @brief 创建文件夹选择列表行
 * @param state 音乐视图状态
 * @param body 提示框内容区域
 * @param source 文件夹索引
 * @param y 顶部坐标
 * @return 创建成功返回 true，否则返回 false
 */
bool CreateMusicFolderPickerRow(
    MusicViewState* state, lv_obj_t* body, int source, int y) {
  lv_obj_t* row = lv_button_create(body);
  if (row == nullptr) {
    return false;
  }
  lv_obj_remove_style_all(row);
  lv_obj_add_flag(row, LV_OBJ_FLAG_GESTURE_BUBBLE);
  lv_obj_set_size(row, state->config.width - 68, 86);
  lv_obj_set_pos(row, 0, y);
  lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_set_style_bg_color(row, lv_color_hex(kPressedColor),
                            LV_STATE_PRESSED);
  lv_obj_set_style_bg_opa(row, LV_OPA_COVER, LV_STATE_PRESSED);

  lv_obj_t* folder = CreateLabel(row, icon::kFolder,
      lv_color_hex(kPrimaryColor), MaterialFillIconFont44());
  if (folder != nullptr) {
    lv_obj_align(folder, LV_ALIGN_LEFT_MID, 16, 0);
  }
  lv_obj_t* path = CreateLabel(row, MusicFolderPath(source),
      lv_color_hex(kMainTextColor), Font24());
  if (path != nullptr) {
    lv_obj_set_width(path, state->config.width - 220);
    lv_label_set_long_mode(path, LV_LABEL_LONG_DOT);
    lv_obj_align(path, LV_ALIGN_TOP_LEFT, 76, 13);
  }
  lv_obj_t* storage = CreateLabel(
      row, "SD card", lv_color_hex(kSecondaryTextColor), Font22());
  if (storage != nullptr) {
    lv_obj_align(storage, LV_ALIGN_TOP_LEFT, 76, 49);
  }
  lv_obj_t* check_box = lv_obj_create(row);
  if (check_box == nullptr) {
    return false;
  }
  lv_obj_remove_flag(check_box, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(check_box, 48, 48);
  lv_obj_align(check_box, LV_ALIGN_RIGHT_MID, -12, 0);
  lv_obj_set_style_bg_color(check_box, lv_color_hex(kSecondaryContainerColor),
                            LV_PART_MAIN);
  lv_obj_set_style_bg_opa(check_box, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_border_width(check_box, 0, LV_PART_MAIN);
  lv_obj_set_style_radius(check_box, 12, LV_PART_MAIN);
  lv_obj_set_style_pad_all(check_box, 0, LV_PART_MAIN);
  lv_obj_t* check = CreateLabel(check_box, icon::kCheck,
      lv_color_hex(kPrimaryColor), MaterialFillIconFont32());
  if (check != nullptr) {
    lv_obj_center(check);
    if (!state->picker_source_enabled[source]) {
      lv_obj_add_flag(check, LV_OBJ_FLAG_HIDDEN);
    }
  }

  auto* action = new MusicSourceAction{
      .state = state,
      .row = row,
      .check = check,
      .source = source,
  };
  lv_obj_add_event_cb(row, MusicFolderPickerRowClickedEventCallback,
                      LV_EVENT_CLICKED, action);
  lv_obj_add_event_cb(row, MusicSourceActionDeleteEventCallback,
                      LV_EVENT_DELETE, action);
  return true;
}

/**
 * @brief 显示可滚动的音乐文件夹选择提示框
 * @param state 音乐视图状态
 * @return 显示成功返回 true，否则返回 false
 */
[[maybe_unused]] bool ShowMusicFolderPickerPrompt(MusicViewState* state) {
  if (state == nullptr || state->root == nullptr) {
    return false;
  }
  CopyMusicSourceFlags(
      state->picker_source_enabled, state->draft_source_enabled);
  PromptDialogConfig config =
      CreateMusicPromptConfig(state, "Choose folders");
  config.dialog_height = std::min(state->config.height - 64, 700);
  config.confirm_text = "Confirm";
  config.confirm_callback = MusicFolderPickerConfirmedCallback;
  lv_obj_t* body = ShowPromptDialog(
      state->root, &state->folder_dialog, config);
  if (body == nullptr) {
    return false;
  }
  lv_obj_set_scrollbar_mode(body, LV_SCROLLBAR_MODE_AUTO);
  for (int source = 0; source < kMusicFolderOptionCount; ++source) {
    if (!CreateMusicFolderPickerRow(state, body, source, source * 86)) {
      ClosePromptDialog(&state->folder_dialog);
      return false;
    }
  }
  return true;
}

/**
 * @brief 处理添加音乐源按钮点击事件
 * @param event LVGL 事件对象
 */
void AddMusicSourcePromptClickedEventCallback(lv_event_t* event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
    return;
  }
  auto* state = static_cast<MusicViewState*>(lv_event_get_user_data(event));
  if (state == nullptr || state->root == nullptr) {
    return;
  }
  FolderPickerViewConfig picker_config;
  picker_config.view_config = state->config;
  picker_config.title = "Select folder";
  picker_config.action_text = "Use this folder";
  picker_config.action_color = theme::LightNeutralTheme().action;
  picker_config.action_text_color = theme::LightNeutralTheme().on_action;
  picker_config.selected_callback = [state](const char* path) {
    if (path == nullptr || path[0] == '\0') {
      return;
    }
    for (const std::string& current : state->draft_source_paths) {
      if (current == path) {
        return;
      }
    }
    for (int source = 0; source < kMusicFolderOptionCount; ++source) {
      if (!state->draft_source_paths[source].empty()) {
        continue;
      }
      state->draft_source_paths[source] = path;
      state->draft_source_enabled[source] = true;
      break;
    }
  };
  picker_config.closed_callback = [state]() {
    if (RenderMusicSourcesPromptContent(state) &&
        state->sources_dialog.body != nullptr) {
      lv_obj_update_layout(state->sources_dialog.body);
      lv_obj_scroll_to_y(
          state->sources_dialog.body, LV_COORD_MAX, LV_ANIM_OFF);
      lv_obj_invalidate(state->sources_dialog.body);
    }
  };
  CreateFolderPickerView(state->root, picker_config);
  lv_event_stop_bubbling(event);
  lv_event_stop_processing(event);
}

/**
 * @brief 创建音乐源提示框中的文件夹行
 * @param state 音乐视图状态
 * @param body 提示框内容区域
 * @param source 文件夹索引
 * @param y 顶部坐标
 * @return 创建成功返回 true，否则返回 false
 */
bool CreateMusicSourcesPromptRow(
    MusicViewState* state, lv_obj_t* body, int source, int y) {
  lv_obj_t* row = lv_obj_create(body);
  if (row == nullptr) {
    return false;
  }
  lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(row, state->config.width - 68, 82);
  lv_obj_set_pos(row, 0, y);
  lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_set_style_border_width(row, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(row, 0, LV_PART_MAIN);

  lv_obj_t* path = CreateLabel(
      row, state->draft_source_paths[source].c_str(),
      lv_color_hex(kMainTextColor), Font24());
  if (path != nullptr) {
    lv_obj_set_size(path, state->config.width - 188, 32);
    lv_label_set_long_mode(path, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_align(path, LV_ALIGN_TOP_LEFT, 32, 12);
  }
  lv_obj_t* detail = CreateLabel(
      row, "Music search folder", lv_color_hex(kSecondaryTextColor),
      Font22());
  if (detail != nullptr) {
    lv_obj_align(detail, LV_ALIGN_TOP_LEFT, 32, 47);
  }
  lv_obj_t* remove = lv_button_create(row);
  if (remove == nullptr) {
    return false;
  }
  lv_obj_remove_style_all(remove);
  lv_obj_set_size(remove, 54, 54);
  lv_obj_align(remove, LV_ALIGN_RIGHT_MID, -20, 0);
  auto* action = new MusicSourceAction{
      .state = state,
      .row = row,
      .source = source,
  };
  lv_obj_add_event_cb(remove, MusicSourcesPromptRemoveClickedEventCallback,
                      LV_EVENT_CLICKED, action);
  lv_obj_add_event_cb(remove, MusicSourceActionDeleteEventCallback,
                      LV_EVENT_DELETE, action);
  lv_obj_t* delete_icon = CreateLabel(remove, icon::kDelete,
      lv_color_hex(kSecondaryTextColor), MaterialOutlineIconFont44());
  if (delete_icon != nullptr) {
    lv_obj_center(delete_icon);
  }
  return true;
}

bool RenderMusicSourcesPromptContent(MusicViewState* state) {
  if (state == nullptr || state->sources_dialog.body == nullptr) {
    return false;
  }
  lv_obj_t* body = state->sources_dialog.body;
  lv_obj_clean(body);

  int y = 0;
  int folder_count = 0;
  for (int source = 0; source < kMusicFolderOptionCount; ++source) {
    if (state->draft_source_paths[source].empty()) {
      continue;
    }
    if (!CreateMusicSourcesPromptRow(state, body, source, y)) {
      return false;
    }
    y += 82;
    ++folder_count;
  }
  if (folder_count == 0) {
    lv_obj_t* empty = CreateLabel(body, "No folders",
        lv_color_hex(kSecondaryTextColor), Font24());
    if (empty != nullptr) {
      lv_obj_set_pos(empty, 32, y + 12);
    }
  }
  return true;
}

/**
 * @brief 创建音乐源弹窗中固定的文件夹标题和添加按钮
 * @param state 音乐视图状态
 * @return 创建成功返回 true，否则返回 false
 */
bool CreateMusicSourcesPromptHeader(MusicViewState* state) {
  if (state == nullptr || state->sources_dialog.panel == nullptr) {
    return false;
  }
  lv_obj_t* panel = state->sources_dialog.panel;
  lv_obj_t* section = CreateLabel(panel, "Folders to load",
      lv_color_hex(kSecondaryTextColor), Font24());
  if (section != nullptr) {
    lv_obj_set_pos(section, 32, kMusicSourcesHeaderTop);
  }
  lv_obj_t* add = lv_button_create(panel);
  if (add == nullptr) {
    return false;
  }
  lv_obj_remove_style_all(add);
  lv_obj_set_size(add, 62, 62);
  lv_obj_align(add, LV_ALIGN_TOP_RIGHT, -16, kMusicSourcesAddTop);
  lv_obj_set_style_bg_opa(add, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_set_style_bg_color(add, lv_color_hex(kPressedColor),
                            LV_STATE_PRESSED);
  lv_obj_set_style_bg_opa(add, LV_OPA_COVER, LV_STATE_PRESSED);
  lv_obj_set_style_radius(add, 31, LV_PART_MAIN);
  lv_obj_add_event_cb(add, AddMusicSourcePromptClickedEventCallback,
                      LV_EVENT_CLICKED, state);
  lv_obj_t* add_icon = CreateLabel(add, icon::kAdd,
      lv_color_hex(kMainTextColor), MaterialOutlineIconFont44());
  if (add_icon != nullptr) {
    lv_obj_center(add_icon);
  }
  return true;
}

bool ShowMusicSourcesPrompt(MusicViewState* state) {
  if (state == nullptr || state->root == nullptr) {
    return false;
  }
  CopyMusicSourceFlags(
      state->draft_source_enabled, state->source_enabled);
  state->draft_source_paths = state->source_paths;
  PromptDialogConfig config =
      CreateMusicPromptConfig(state, "Music sources");
  config.confirm_callback = MusicSourcesPromptSavedCallback;
  lv_obj_t* body = ShowPromptDialog(
      state->root, &state->sources_dialog, config);
  if (body == nullptr) {
    return false;
  }
  const int list_height = config.dialog_height - config.action_height -
                          kMusicSourcesListTop;
  lv_obj_set_pos(body, 0, kMusicSourcesListTop);
  lv_obj_set_size(body, config.dialog_width, list_height);
  if (!CreateMusicSourcesPromptHeader(state)) {
    ClosePromptDialog(&state->sources_dialog);
    return false;
  }
  return RenderMusicSourcesPromptContent(state);
}

/**
 * @brief 显示音乐设置页面并播放进入动画
 * @param state 音乐视图状态
 * @return 显示成功返回 true，否则返回 false
 */
bool ShowMusicSettingsPage(MusicViewState* state) {
  if (state == nullptr || state->root == nullptr) {
    return false;
  }
  if (state->settings_page != nullptr) {
    lv_obj_move_to_index(state->settings_page, -1);
    return true;
  }

  lv_obj_t* page = lv_obj_create(state->root);
  if (page == nullptr) {
    return false;
  }
  state->settings_page = page;
  state->settings_closing = false;
  state->settings_edge_swipe = EdgeBackSwipeState();
  lv_obj_remove_flag(page, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(page, LV_OBJ_FLAG_GESTURE_BUBBLE);
  lv_obj_set_size(page, state->config.width, state->config.height);
  lv_obj_set_pos(page, 0, 0);
  lv_obj_set_style_bg_color(page, lv_color_hex(kMainBackgroundColor),
                            LV_PART_MAIN);
  lv_obj_set_style_bg_opa(page, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_border_width(page, 0, LV_PART_MAIN);
  lv_obj_set_style_radius(page, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(page, 0, LV_PART_MAIN);
  AddEdgeBackSwipeEvents(page, SettingsEdgeBackEventCallback, state);

  if (!CreateSettingsStyleHeader(
      page, "Music settings", SettingsBackClickedEventCallback, state)) {
    lv_obj_delete(page);
    state->settings_page = nullptr;
    return false;
  }
  lv_obj_t* section = CreateLabel(
      page, "LIBRARY", lv_color_hex(kPrimaryColor), Font22());
  if (section != nullptr) {
    lv_obj_align(section, LV_ALIGN_TOP_LEFT, 28, 254);
  }
  if (!CreateMusicSourcesSettingRow(page, state)) {
    lv_obj_delete(page);
    state->settings_page = nullptr;
    return false;
  }
  EnableEdgeBackSwipeEventBubble(page);
  if (!StartSlideLeftWindowTransition(page, state->config.width,
      kSettingsAnimationMs, state, nullptr)) {
    lv_obj_delete(page);
    state->settings_page = nullptr;
    state->sources_summary_label = nullptr;
    return false;
  }
  return true;
}

/**
 * @brief 处理音乐侧边栏刷新按钮点击事件
 * @param event LVGL 事件对象
 */
void DrawerRefreshClickedEventCallback(lv_event_t* event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
    return;
  }
  auto* state = static_cast<MusicViewState*>(lv_event_get_user_data(event));
  if (state != nullptr) {
    CloseNavigationDrawer(&state->drawer);
  }
}

/**
 * @brief 处理音乐侧边栏设置按钮点击事件
 * @param event LVGL 事件对象
 */
void DrawerSettingsClickedEventCallback(lv_event_t* event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
    return;
  }
  auto* state = static_cast<MusicViewState*>(lv_event_get_user_data(event));
  if (state == nullptr) {
    return;
  }
  CloseNavigationDrawer(&state->drawer);
  ShowMusicSettingsPage(state);
}

/**
 * @brief 显示音乐应用导航侧边栏
 * @param state 音乐视图状态
 */
void ShowMusicDrawer(MusicViewState* state) {
  if (state == nullptr || state->root == nullptr ||
      IsNavigationDrawerOpen(&state->drawer)) {
    return;
  }

  NavigationDrawerConfig config;
  config.screen_width = state->config.width;
  config.screen_height = state->config.height;
  config.background_color = kMainBackgroundColor;
  config.primary_text_color = kMainTextColor;
  config.icon_color = kSecondaryTextColor;
  config.pressed_color = kPressedColor;
  config.divider_color = kDividerColor;
  config.title = "Music";
  config.title_font = Font36();
  config.item_font = Font28();
  config.icon_font = MaterialFillIconFont44();
  if (OpenNavigationDrawer(
      state->root, &state->drawer, config) == nullptr) {
    return;
  }

  int y = kNavigationDrawerContentTop;
  CreateNavigationDrawerItem(&state->drawer, icon::kRefresh,
      "Refresh music files", y, DrawerRefreshClickedEventCallback, state);
  y += kNavigationDrawerItemHeight + 12;
  CreateNavigationDrawerDivider(&state->drawer, y);
  y += 18;
  CreateNavigationDrawerItem(&state->drawer, icon::kSettings, "Settings",
      y, DrawerSettingsClickedEventCallback, state);
  PresentNavigationDrawer(&state->drawer);
}

/**
 * @brief 处理音乐主页面菜单按钮点击事件
 * @param event LVGL 事件对象
 */
void MenuButtonClickedEventCallback(lv_event_t* event) {
  if (lv_event_get_code(event) == LV_EVENT_CLICKED) {
    ShowMusicDrawer(
        static_cast<MusicViewState*>(lv_event_get_user_data(event)));
  }
}

/**
 * @brief 创建音乐主页面的菜单按钮和标题
 * @param parent 父对象
 * @param state 音乐视图状态
 * @return 创建成功返回 true，否则返回 false
 */
bool CreateMusicHeader(lv_obj_t* parent, MusicViewState* state) {
  lv_obj_t* menu = CreateFlatButton(parent);
  if (menu == nullptr) {
    return false;
  }
  lv_obj_remove_style_all(menu);
  lv_obj_add_flag(menu, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_size(menu, 72, 72);
  lv_obj_align(menu, LV_ALIGN_TOP_LEFT, 20, kHeaderTop - 2);
  lv_obj_add_event_cb(menu, MenuButtonClickedEventCallback,
                      LV_EVENT_CLICKED, state);
  lv_obj_t* menu_icon = CreateLabel(menu, icon::kMenu,
      lv_color_hex(kMainTextColor), MaterialFillIconFont56());
  if (menu_icon != nullptr) {
    lv_obj_center(menu_icon);
  }

  lv_obj_t* title = CreateLabel(
      parent, "Music", lv_color_hex(kMainTextColor), Font36());
  if (title == nullptr) {
    return false;
  }
  lv_obj_align_to(title, menu, LV_ALIGN_OUT_RIGHT_MID, 12, 0);
  return true;
}

}  // namespace

lv_obj_t* CreateMusicView(lv_obj_t* parent, const app::AppEntry& app_entry,
    const AppViewConfig& config) {
  static_cast<void>(app_entry);
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

  if (!CreateMusicHeader(container, state)) {
    lv_obj_delete(container);
    return nullptr;
  }

  return container;
}

}  // namespace lilygo_box::ui
