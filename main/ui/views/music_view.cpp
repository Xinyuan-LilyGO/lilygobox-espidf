/*
 * @Description: 音乐应用视图
 * @Author: LILYGO_L
 * @Date: 2026-07-08 00:00:00
 * @LastEditTime: 2026-07-22 20:04:14
 * @License: GPL 3.0
 */
#include "ui/views/music_view.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <memory>
#include <new>
#include <string>
#include <utility>
#include <vector>

#include "app/music_library.h"
#include "app/storage/music_storage.h"
#include "base/logger.h"
#include "esp_random.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "hal/providers/audio_provider.h"
#include "hal/providers/storage_provider.h"
#include "ui/animation/transition_animation.h"
#include "ui/resources/fonts/font_assets.h"
#include "ui/resources/fonts/icon_assets.h"
#include "ui/input/edge_back_gesture.h"
#include "ui/input/press_cancel.h"
#include "ui/theme/theme_provider.h"
#include "ui/views/files_view.h"
#include "ui/widgets/navigation_drawer.h"
#include "ui/widgets/prompt/prompt_dialog.h"
#include "ui/widgets/prompt/prompt_status.h"

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
constexpr int kPlayerLandscapeSideMargin = 40;
constexpr int kPlayerLandscapeColumnGap = 24;
constexpr int kPlayerLandscapeBottomMargin = 36;
constexpr int kPlayerLandscapeArtworkWidthPercent = 43;
constexpr int kPlayerLandscapeRegularContentTop = 146;
constexpr int kPlayerLandscapeCompactContentTop = 128;
constexpr int kPlayerLandscapeDetailsTopOffset = 40;
constexpr int kPlayerLandscapeProgressDownOffset = 18;
constexpr int kPlayerLandscapeControlRowGap = 120;
constexpr int kPlayerLandscapeRegularControlGap = 20;
constexpr int kPlayerLandscapeCompactControlGap = 12;
constexpr int kSettingsAnimationMs = 240;
constexpr int kNavigationTitleTop = 78;
constexpr int kMusicSourcesHeaderTop = 112;
constexpr int kMusicSourcesAddTop = 98;
constexpr int kMusicSourcesListTop = 174;
constexpr int kMusicLibraryTop = 226;
constexpr int kMusicTrackRowHeight = 104;
constexpr uint32_t kPlaybackStatusIntervalMs = 250;
constexpr uint32_t kMusicScanStartDelayMs = 850;
constexpr uint32_t kMusicScanTaskStackBytes = 8 * 1024;
constexpr UBaseType_t kMusicScanTaskPriority = 2;
constexpr int kMusicEmptyGroupOffsetY = -100;
constexpr int kMusicScanningGroupOffsetY = -48;
constexpr int kMusicStatusGroupTopGap = 24;

constexpr int kMusicFolderOptionCount = 8;

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

struct MusicTrackAction;
struct MusicViewState;

struct MusicScanJob {
  std::vector<std::string> source_paths;
  std::vector<app::MusicTrack> tracks;
  std::atomic<bool> completed{false};
  bool success = false;
};

/**
 * @brief 独立于音乐界面生命周期的播放会话
 */
struct MusicPlaybackSession {
  hal::AudioProvider* audio = nullptr;
  hal::StorageProvider* storage = nullptr;
  MusicViewState* view = nullptr;
  std::vector<app::MusicTrack> tracks;
  std::shared_ptr<MusicScanJob> scan_job;
  lv_timer_t* status_timer = nullptr;
  int current_track = -1;
  bool playing = false;
  bool completion_handled = false;
  bool storage_was_mounted = false;
  bool library_initialized = false;
  MusicPlaybackMode playback_mode = MusicPlaybackMode::kRepeatAll;
};

struct MusicViewState {
  AppViewConfig config;
  MusicPlaybackSession* session = nullptr;
  lv_obj_t* root = nullptr;
  lv_obj_t* player_page = nullptr;
  lv_obj_t* play_button = nullptr;
  lv_obj_t* mini_player = nullptr;
  lv_obj_t* mini_play_button = nullptr;
  lv_obj_t* mini_title_label = nullptr;
  lv_obj_t* mini_artist_label = nullptr;
  lv_obj_t* player_title_label = nullptr;
  lv_obj_t* player_artist_label = nullptr;
  lv_obj_t* playback_mode_label = nullptr;
  lv_obj_t* progress_slider = nullptr;
  lv_obj_t* current_time_label = nullptr;
  lv_obj_t* total_time_label = nullptr;
  lv_obj_t* library_content = nullptr;
  lv_obj_t* library_status_anchor = nullptr;
  lv_obj_t* settings_page = nullptr;
  NavigationDrawerState drawer;
  PromptDialogState sources_dialog;
  EdgeBackSwipeState player_edge_swipe;
  EdgeBackSwipeState settings_edge_swipe;
  bool source_enabled[kMusicFolderOptionCount] = {};
  bool draft_source_enabled[kMusicFolderOptionCount] = {};
  std::array<std::string, kMusicFolderOptionCount> source_paths;
  std::array<std::string, kMusicFolderOptionCount> draft_source_paths;
  std::vector<std::unique_ptr<MusicTrackAction>> track_actions;
  lv_timer_t* scan_start_timer = nullptr;
  bool seeking = false;
  bool settings_closing = false;
};

/**
 * @brief 获取应用生命周期内唯一的音乐播放会话
 * @return 音乐播放会话地址
 */
MusicPlaybackSession* GetMusicPlaybackSession() {
  static auto* session = new MusicPlaybackSession();
  return session;
}

struct MusicTrackAction {
  MusicViewState* state = nullptr;
  size_t track_index = 0;
};

struct MusicSourceAction {
  MusicViewState* state = nullptr;
  lv_obj_t* row = nullptr;
  int source = 0;
};

/**
 * @brief 重新构建音乐主页面的曲目列表
 * @param state 音乐视图状态
 * @return 构建成功返回 true，否则返回 false
 */
bool RenderMusicLibrary(MusicViewState* state);

/**
 * @brief 渲染音乐文件扫描中的加载状态
 * @param state 音乐视图状态
 * @return 构建成功返回 true，否则返回 false
 */
bool RenderMusicScanningContent(MusicViewState* state);

/**
 * @brief 从已配置音乐源重新扫描 MP3 曲库
 * @param state 音乐视图状态
 * @return 扫描和界面刷新完成返回 true，否则返回 false
 */
bool RefreshMusicLibrary(MusicViewState* state);

/**
 * @brief 在扫描提示显示后执行存储挂载和曲库扫描
 * @param state 音乐视图状态
 * @return 扫描任务启动成功返回 true，否则返回 false
 */
bool StartMusicLibraryScan(MusicViewState* state);

/**
 * @brief 停止等待执行的音乐存储扫描计时器
 * @param state 音乐视图状态
 */
void StopMusicScanStartTimer(MusicViewState* state);

/**
 * @brief 接收后台曲库扫描结果并刷新音乐列表
 * @param session 音乐播放会话
 */
void FinishMusicLibraryScan(MusicPlaybackSession* session);

/**
 * @brief 更新播放按钮图标和形状
 * @param state 音乐视图状态
 * @param animated 是否播放圆角切换动画
 */
void UpdatePlayButton(MusicViewState* state, bool animated);

/**
 * @brief 处理刷新曲库按钮点击事件
 * @param event LVGL 事件对象
 */
void RefreshMusicClickedEventCallback(lv_event_t* event);

/**
 * @brief 开始播放曲库中的指定曲目
 * @param session 音乐播放会话
 * @param track_index 曲目索引
 * @return 播放任务启动成功返回 true，否则返回 false
 */
bool StartMusicTrack(MusicPlaybackSession* session, size_t track_index);

/**
 * @brief 按当前播放模式选择上一首或下一首曲目
 * @param session 音乐播放会话
 * @param direction 切换方向，负数为上一首，正数为下一首
 * @return 播放任务启动成功返回 true，否则返回 false
 */
bool PlayAdjacentMusicTrack(MusicPlaybackSession* session, int direction);

/**
 * @brief 获取当前选中的曲目
 * @param session 音乐播放会话
 * @return 当前曲目地址，没有选中曲目时返回 nullptr
 */
const app::MusicTrack* CurrentMusicTrack(
    const MusicPlaybackSession* session);

/**
 * @brief 更新播放状态以及两个播放按钮的图标
 * @param session 音乐播放会话
 * @param playing 是否正在播放
 * @param animated 是否播放详情页按钮形状动画
 */
void SetMusicPlaying(
    MusicPlaybackSession* session, bool playing, bool animated);

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
 * @brief 获取 56 号轮廓 Material Symbols 字体
 * @return 字体指针
 */
const lv_font_t* MaterialOutlineIconFont56() {
  return &lvgl_font_material_symbols_outline_56;
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
  const app::MusicTrack* track = CurrentMusicTrack(state->session);
  const int duration_seconds = track == nullptr
                                   ? kDefaultTrackDurationSeconds
                                   : static_cast<int>(
                                         track->duration_ms / 1000U);
  const int current_seconds = duration_seconds * value / 100;
  SetMusicTimeLabel(state->current_time_label, current_seconds);
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
    if (state != nullptr) {
      const app::MusicTrack* track = CurrentMusicTrack(state->session);
      if (state->session != nullptr && state->session->audio != nullptr &&
          track != nullptr &&
          track->duration_ms > 0) {
        const uint32_t position_ms = static_cast<uint32_t>(
            static_cast<uint64_t>(track->duration_ms) *
            lv_slider_get_value(slider) / 100U);
        state->session->audio->SeekAudioFile(position_ms);
      }
      state->seeking = false;
    }
    return;
  }
  if (code == LV_EVENT_VALUE_CHANGED) {
    UpdateMusicCurrentTime(state, lv_slider_get_value(slider));
    return;
  }
  if (code != LV_EVENT_PRESSED && code != LV_EVENT_PRESSING) {
    return;
  }
  if (state != nullptr) {
    state->seeking = true;
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
  state->player_title_label = nullptr;
  state->player_artist_label = nullptr;
  state->progress_slider = nullptr;
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

void UpdatePlayButton(MusicViewState* state, bool animated) {
  if (state == nullptr || state->session == nullptr ||
      state->play_button == nullptr) {
    return;
  }

  lv_obj_invalidate(state->play_button);
  const int32_t target_radius = state->session->playing ? 34 : 58;
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
  if (state == nullptr || state->session == nullptr) {
    return;
  }
  lv_obj_t* target = lv_event_get_target_obj(event);
  DrawMusicControlIcon(target, lv_event_get_layer(event),
      state->session->playing ? MusicControlIcon::kPause
                              : MusicControlIcon::kPlay,
      lv_obj_get_style_text_color(target, LV_PART_MAIN));
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
  if (state == nullptr || state->session == nullptr ||
      state->session->audio == nullptr) {
    return;
  }
  MusicPlaybackSession* session = state->session;
  if (session->playing) {
    if (session->audio->PauseAudioFile()) {
      SetMusicPlaying(session, false, true);
    }
  } else if (session->current_track < 0) {
    StartMusicTrack(session, 0);
  } else if (session->audio->ResumeAudioFile()) {
    SetMusicPlaying(session, true, true);
  } else {
    StartMusicTrack(session, static_cast<size_t>(session->current_track));
  }
  lv_event_stop_bubbling(event);
}

/**
 * @brief 处理上一首按钮点击事件
 * @param event LVGL 事件对象
 */
void PreviousTrackClickedEventCallback(lv_event_t* event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
    return;
  }
  auto* state = static_cast<MusicViewState*>(lv_event_get_user_data(event));
  PlayAdjacentMusicTrack(state == nullptr ? nullptr : state->session, -1);
  lv_event_stop_bubbling(event);
}

/**
 * @brief 处理下一首按钮点击事件
 * @param event LVGL 事件对象
 */
void NextTrackClickedEventCallback(lv_event_t* event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
    return;
  }
  auto* state = static_cast<MusicViewState*>(lv_event_get_user_data(event));
  PlayAdjacentMusicTrack(state == nullptr ? nullptr : state->session, 1);
  lv_event_stop_bubbling(event);
}

const app::MusicTrack* CurrentMusicTrack(
    const MusicPlaybackSession* session) {
  if (session == nullptr || session->current_track < 0 ||
      session->current_track >= static_cast<int>(session->tracks.size())) {
    return nullptr;
  }
  return &session->tracks[session->current_track];
}

/**
 * @brief 更新迷你播放器和播放详情页中的曲目信息
 * @param state 音乐视图状态
 */
void UpdateCurrentTrackLabels(MusicViewState* state) {
  const app::MusicTrack* track =
      CurrentMusicTrack(state == nullptr ? nullptr : state->session);
  if (state == nullptr || track == nullptr) {
    return;
  }
  if (state->mini_title_label != nullptr) {
    lv_label_set_text(state->mini_title_label, track->title.c_str());
  }
  if (state->mini_artist_label != nullptr) {
    lv_label_set_text(state->mini_artist_label, track->artist.c_str());
  }
  if (state->player_title_label != nullptr) {
    lv_label_set_text(state->player_title_label, track->title.c_str());
  }
  if (state->player_artist_label != nullptr) {
    lv_label_set_text(state->player_artist_label, track->artist.c_str());
  }
  SetMusicTimeLabel(state->total_time_label,
      static_cast<int>(track->duration_ms / 1000U));
}

/**
 * @brief 设置迷你播放器可见性并同步曲目列表高度
 * @param state 音乐视图状态
 * @param visible 是否显示迷你播放器
 */
void SetMiniPlayerVisible(MusicViewState* state, bool visible) {
  if (state == nullptr) {
    return;
  }
  if (state->mini_player != nullptr) {
    if (visible) {
      lv_obj_remove_flag(state->mini_player, LV_OBJ_FLAG_HIDDEN);
    } else {
      lv_obj_add_flag(state->mini_player, LV_OBJ_FLAG_HIDDEN);
    }
  }
  if (state->library_content != nullptr) {
    lv_obj_set_height(state->library_content,
        state->config.height - kMusicLibraryTop -
            (visible ? kMiniPlayerHeight : 0));
  }
}

void SetMusicPlaying(
    MusicPlaybackSession* session, bool playing, bool animated) {
  if (session == nullptr) {
    return;
  }
  session->playing = playing;
  MusicViewState* state = session->view;
  if (state != nullptr) {
    UpdatePlayButton(state, animated);
    if (state->mini_play_button != nullptr) {
      lv_obj_invalidate(state->mini_play_button);
    }
  }
}

bool StartMusicTrack(MusicPlaybackSession* session, size_t track_index) {
  if (session == nullptr || session->audio == nullptr ||
      track_index >= session->tracks.size()) {
    return false;
  }
  const app::MusicTrack& track = session->tracks[track_index];
  if (!session->audio->StartAudioFile(track.path.c_str(), track.duration_ms)) {
    SetMusicPlaying(session, false, true);
    return false;
  }
  session->current_track = static_cast<int>(track_index);
  session->completion_handled = false;
  MusicViewState* state = session->view;
  if (state != nullptr) {
    SetMiniPlayerVisible(state, true);
    UpdateCurrentTrackLabels(state);
    SetMusicTimeLabel(state->current_time_label, 0);
    if (state->progress_slider != nullptr) {
      lv_slider_set_value(state->progress_slider, 0, LV_ANIM_OFF);
    }
  }
  SetMusicPlaying(session, true, true);
  return true;
}

bool PlayAdjacentMusicTrack(MusicPlaybackSession* session, int direction) {
  if (session == nullptr || session->tracks.empty()) {
    return false;
  }
  size_t track_index = 0;
  if (session->playback_mode == MusicPlaybackMode::kShuffle &&
      session->tracks.size() > 1) {
    do {
      track_index = esp_random() % session->tracks.size();
    } while (track_index == static_cast<size_t>(session->current_track));
  } else if (session->current_track >= 0) {
    const int track_count = static_cast<int>(session->tracks.size());
    track_index = static_cast<size_t>(
        (session->current_track + direction + track_count) % track_count);
  }
  return StartMusicTrack(session, track_index);
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
  if (state == nullptr || state->session == nullptr ||
      state->playback_mode_label == nullptr) {
    return;
  }
  lv_label_set_text(state->playback_mode_label,
      PlaybackModeIcon(state->session->playback_mode));
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
  if (state == nullptr || state->session == nullptr) {
    return;
  }
  switch (state->session->playback_mode) {
    case MusicPlaybackMode::kRepeatAll:
      state->session->playback_mode = MusicPlaybackMode::kRepeatOne;
      break;
    case MusicPlaybackMode::kRepeatOne:
      state->session->playback_mode = MusicPlaybackMode::kShuffle;
      break;
    case MusicPlaybackMode::kShuffle:
      state->session->playback_mode = MusicPlaybackMode::kRepeatAll;
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
          MaterialFillIconFont56());
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
  lv_obj_align(close_button, LV_ALIGN_TOP_LEFT, 14, 56);
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

  const bool landscape_layout = state->config.width > state->config.height;
  const bool compact_portrait =
      !landscape_layout && state->config.height < 700;
  const bool compact_landscape =
      landscape_layout && state->config.height < 600;
  const lv_font_t* title_font = compact_portrait ? Font28() : Font36();
  const lv_font_t* artist_font = compact_portrait ? Font24() : Font28();
  const int title_height =
      static_cast<int>(lv_font_get_line_height(title_font));
  const int artist_height =
      static_cast<int>(lv_font_get_line_height(artist_font));

  int artwork_size = 0;
  int artwork_x = 0;
  int artwork_y = 0;
  int text_x = 32;
  int text_width = state->config.width - 64;
  int title_y = 0;
  int artist_y = 0;
  int track_y = 0;
  int time_y = 0;
  int play_button_size = 116;
  int side_button_size = 88;
  int mode_button_size = 72;
  int mode_button_x = 0;
  int previous_button_x = 0;
  int play_button_x = 0;
  int next_button_x = 0;
  int control_center_y = 0;
  int control_gap = 0;
  int control_bottom = compact_portrait ? -62 : -94;
  int side_control_bottom = compact_portrait ? -76 : -108;
  int side_control_offset = compact_portrait ? 96 : 120;

  // 横屏使用独立双栏，避免继续沿用竖屏的纵向坐标而挤出屏幕。
  if (landscape_layout) {
    const int content_top = compact_landscape
                                ? kPlayerLandscapeCompactContentTop
                                : kPlayerLandscapeRegularContentTop;
    const int available_width = state->config.width -
        2 * kPlayerLandscapeSideMargin - kPlayerLandscapeColumnGap;
    const int artwork_column_width = available_width *
        kPlayerLandscapeArtworkWidthPercent / 100;
    artwork_size = std::min(artwork_column_width,
        state->config.height - content_top -
            kPlayerLandscapeBottomMargin);
    artwork_size = std::max(1, artwork_size);
    artwork_x = kPlayerLandscapeSideMargin +
        (artwork_column_width - artwork_size) / 2;
    artwork_y = content_top +
        (state->config.height - content_top -
            kPlayerLandscapeBottomMargin - artwork_size) / 2;
    text_x = artwork_x + artwork_size + kPlayerLandscapeColumnGap;
    text_width = state->config.width - kPlayerLandscapeSideMargin - text_x;
    title_y = content_top +
        (compact_landscape ? 8 : kPlayerLandscapeDetailsTopOffset);
    artist_y = title_y + title_height + 10;
    track_y = artist_y + artist_height +
        (compact_landscape ? 20 : 36) +
        kPlayerLandscapeProgressDownOffset;
    time_y = track_y + 60;

    control_gap = compact_landscape
                      ? kPlayerLandscapeCompactControlGap
                      : kPlayerLandscapeRegularControlGap;
    const int desired_control_center_y =
        time_y + kPlayerLandscapeControlRowGap;
    const int maximum_control_center_y = state->config.height -
        kPlayerLandscapeBottomMargin - play_button_size / 2;
    control_center_y =
        std::min(desired_control_center_y, maximum_control_center_y);
    play_button_x = text_x + (text_width - play_button_size) / 2;
    previous_button_x =
        play_button_x - control_gap - side_button_size;
    next_button_x = play_button_x + play_button_size + control_gap;
    mode_button_x =
        previous_button_x - control_gap - mode_button_size;
  } else {
    artwork_size = compact_portrait
        ? std::min(state->config.width / 3, state->config.height / 3)
        : std::min(state->config.width - 64, state->config.height / 2);
    artwork_y = compact_portrait ? 124 : 164;
    title_y = artwork_y + artwork_size +
        (compact_portrait ? 12 : 34);
    artist_y = title_y + title_height +
        (compact_portrait ? 8 : 12);
    track_y = title_y + (compact_portrait ? 74 : 142);
    time_y = track_y + 60;
  }

  lv_obj_t* artwork = CreateArtwork(page, artwork_size, 26);
  if (artwork != nullptr) {
    if (landscape_layout) {
      lv_obj_align(
          artwork, LV_ALIGN_TOP_LEFT, artwork_x, artwork_y);
    } else {
      lv_obj_align(artwork, LV_ALIGN_TOP_MID, 0, artwork_y);
    }
  }

  state->player_title_label =
      CreateLabel(
          page, "Unknown Track", lv_color_hex(kMainTextColor), title_font);
  if (state->player_title_label != nullptr) {
    lv_obj_set_size(state->player_title_label,
        text_width, title_height);
    lv_label_set_long_mode(
        state->player_title_label, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_align(
        state->player_title_label, LV_ALIGN_TOP_LEFT, text_x, title_y);
  }
  state->player_artist_label =
      CreateLabel(page, "Unknown Artist", lv_color_hex(kSecondaryTextColor),
          artist_font);
  if (state->player_artist_label != nullptr &&
      state->player_title_label != nullptr) {
    lv_obj_set_size(state->player_artist_label,
        text_width, artist_height);
    lv_label_set_long_mode(
        state->player_artist_label, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_align(
        state->player_artist_label, LV_ALIGN_TOP_LEFT, text_x, artist_y);
  }

  state->progress_slider = lv_slider_create(page);
  if (state->progress_slider != nullptr) {
    lv_obj_t* slider = state->progress_slider;
    lv_obj_set_size(slider, text_width, kProgressSliderHeight);
    lv_obj_align(slider, LV_ALIGN_TOP_LEFT, text_x, track_y);
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
    lv_obj_align(state->current_time_label, LV_ALIGN_TOP_LEFT, text_x,
        time_y);
    UpdateMusicCurrentTime(state, 0);
  }
  state->total_time_label =
      CreateLabel(page, "0:00", lv_color_hex(kSecondaryTextColor), Font24());
  if (state->total_time_label != nullptr) {
    const int right_margin =
        state->config.width - text_x - text_width;
    lv_obj_align(state->total_time_label, LV_ALIGN_TOP_RIGHT, -right_margin,
        time_y);
    const app::MusicTrack* track = CurrentMusicTrack(state->session);
    SetMusicTimeLabel(state->total_time_label,
        track == nullptr
            ? kDefaultTrackDurationSeconds
            : static_cast<int>(track->duration_ms / 1000U));
  }

  lv_obj_t* previous =
      CreatePlayerControlButton(page, MusicControlIcon::kSkipPrevious,
          side_button_size, kSecondaryContainerColor, kPrimaryColor);
  if (previous != nullptr) {
    if (landscape_layout) {
      lv_obj_set_pos(previous, previous_button_x,
          control_center_y - side_button_size / 2);
    } else {
      lv_obj_align(previous, LV_ALIGN_BOTTOM_MID, -side_control_offset,
          side_control_bottom);
    }
    lv_obj_add_event_cb(previous, PreviousTrackClickedEventCallback,
        LV_EVENT_CLICKED, state);
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
    lv_obj_set_size(
        playback_mode_button, mode_button_size, mode_button_size);
    if (landscape_layout) {
      lv_obj_set_pos(playback_mode_button, mode_button_x,
          control_center_y - mode_button_size / 2);
    } else {
      lv_obj_align_to(playback_mode_button, previous, LV_ALIGN_OUT_LEFT_MID,
          -20, 0);
    }
    lv_obj_set_style_radius(
        playback_mode_button, mode_button_size / 2, LV_PART_MAIN);
    lv_obj_set_style_bg_color(playback_mode_button,
        lv_color_hex(kSecondaryContainerColor), LV_PART_MAIN);
    lv_obj_set_style_outline_width(playback_mode_button, 0, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(playback_mode_button, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_color(playback_mode_button,
        lv_color_hex(kSecondaryContainerColor), pressed_selector);
    lv_obj_set_style_bg_opa(playback_mode_button, LV_OPA_COVER,
        pressed_selector);
    lv_obj_set_style_radius(
        playback_mode_button, mode_button_size / 2, pressed_selector);
    state->playback_mode_label =
        CreateLabel(playback_mode_button,
            PlaybackModeIcon(state->session->playback_mode),
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
          page, MusicControlIcon::kPlay, play_button_size,
          kPrimaryColor, 0xFFFFFF);
  if (state->play_button != nullptr) {
    if (landscape_layout) {
      lv_obj_set_pos(state->play_button, play_button_x,
          control_center_y - play_button_size / 2);
    } else {
      lv_obj_align(
          state->play_button, LV_ALIGN_BOTTOM_MID, 0, control_bottom);
    }
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
          side_button_size, kSecondaryContainerColor, kPrimaryColor);
  if (next != nullptr) {
    if (landscape_layout) {
      lv_obj_set_pos(next, next_button_x,
          control_center_y - side_button_size / 2);
    } else {
      lv_obj_align(next, LV_ALIGN_BOTTOM_MID, side_control_offset,
          side_control_bottom);
    }
    lv_obj_add_event_cb(next, NextTrackClickedEventCallback,
        LV_EVENT_CLICKED, state);
  }

  UpdateCurrentTrackLabels(state);

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
  StopMusicScanStartTimer(state);
  if (state != nullptr && state->session != nullptr &&
      state->session->view == state) {
    state->session->view = nullptr;
  }
  delete state;
}

/**
 * @brief 根据屏幕方向定位音乐状态提示
 * @param group 状态提示容器
 * @param state 音乐页面状态
 * @param portrait_offset_y 竖屏时相对内容中心的纵向偏移
 */
void PositionMusicStatusGroup(lv_obj_t* group, MusicViewState* state,
    int portrait_offset_y) {
  if (group == nullptr || state == nullptr ||
      state->library_content == nullptr) {
    return;
  }

  if (state->config.height > state->config.width) {
    lv_obj_align(group, LV_ALIGN_CENTER, 0, portrait_offset_y);
    return;
  }

  int group_top = kMusicStatusGroupTopGap;
  if (state->root != nullptr && state->library_status_anchor != nullptr) {
    lv_obj_update_layout(state->root);
    lv_area_t anchor_area = {};
    lv_area_t content_area = {};
    lv_obj_get_coords(state->library_status_anchor, &anchor_area);
    lv_obj_get_coords(state->library_content, &content_area);
    group_top = static_cast<int>(anchor_area.y2) -
                static_cast<int>(content_area.y1) + 1 +
                kMusicStatusGroupTopGap;
  }
  lv_obj_set_pos(group, 0, std::max(0, group_top));
}

/**
 * @brief 创建主界面的空音乐提示
 * @param parent 父对象
 * @param state 音乐视图状态
 * @return 创建成功返回 true，否则返回 false
 */
bool CreateEmptyMusicContent(lv_obj_t* parent, MusicViewState* state) {
  if (state == nullptr) {
    return false;
  }
  const bool storage_available =
      state->session != nullptr && state->session->storage_was_mounted;
  PromptStatusConfig config;
  config.width = state->config.width;
  config.height = 280;
  config.icon = storage_available ? icon::kMusic : icon::kSdStorage;
  config.icon_font = MaterialFillIconFont56();
  config.icon_background_color = kSecondaryContainerColor;
  config.icon_color = kPrimaryColor;
  config.title =
      storage_available ? "No music found" : "Storage device not found";
  config.title_font = Font28();
  config.title_color = kMainTextColor;
  config.message = storage_available
                       ? "Add a music source or scan again."
                       : "Insert a storage device and scan again.";
  config.message_font = Font22();
  config.message_color = kSecondaryTextColor;
  config.button_text = storage_available ? "Scan Music" : "Scan again";
  config.button_font = Font24();
  config.button_width = 230;
  config.button_height = 62;
  config.button_background_color = kPrimaryColor;
  config.button_pressed_color = kPrimaryColor;
  config.button_text_color = 0xFFFFFF;
  config.button_callback = RefreshMusicClickedEventCallback;
  config.button_user_data = state;
  lv_obj_t* group = CreatePromptStatus(parent, config);
  if (group == nullptr) {
    return false;
  }
  PositionMusicStatusGroup(group, state, kMusicEmptyGroupOffsetY);
  EnableEdgeBackSwipeEventBubble(parent);
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
  state->mini_player = card;
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
  state->mini_title_label =
      CreateLabel(card, "Unknown Track", lv_color_hex(kMainTextColor), Font24());
  if (state->mini_title_label != nullptr) {
    lv_obj_set_size(state->mini_title_label,
        state->config.width - 288,
        static_cast<int>(lv_font_get_line_height(Font24())));
    lv_label_set_long_mode(
        state->mini_title_label, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_align(state->mini_title_label, LV_ALIGN_LEFT_MID, 90, -15);
  }
  state->mini_artist_label =
      CreateLabel(card, "Unknown Artist", lv_color_hex(kSecondaryTextColor), Font22());
  if (state->mini_artist_label != nullptr) {
    lv_obj_set_size(state->mini_artist_label,
        state->config.width - 288,
        static_cast<int>(lv_font_get_line_height(Font22())));
    lv_label_set_long_mode(
        state->mini_artist_label, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_align(state->mini_artist_label, LV_ALIGN_LEFT_MID, 90, 19);
  }

  state->mini_play_button = lv_button_create(card);
  if (state->mini_play_button != nullptr) {
    lv_obj_t* play = state->mini_play_button;
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
    lv_obj_add_event_cb(play, PlayButtonDrawEventCallback,
        LV_EVENT_DRAW_MAIN, state);
    lv_obj_add_event_cb(play, PlayButtonClickedEventCallback,
        LV_EVENT_CLICKED, state);
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
    lv_obj_add_event_cb(next, NextTrackClickedEventCallback,
        LV_EVENT_CLICKED, state);
    lv_obj_add_event_cb(
        next, StopClickBubblingEventCallback, LV_EVENT_ALL, nullptr);
  }
  if (CurrentMusicTrack(state->session) == nullptr) {
    lv_obj_add_flag(card, LV_OBJ_FLAG_HIDDEN);
  }
  return true;
}

bool RenderMusicScanningContent(MusicViewState* state) {
  if (state == nullptr || state->library_content == nullptr) {
    return false;
  }
  lv_obj_clean(state->library_content);
  lv_obj_scroll_to_y(state->library_content, 0, LV_ANIM_OFF);
  state->track_actions.clear();

  PromptStatusConfig config;
  config.width = state->config.width;
  config.height = 250;
  config.visual = PromptStatusVisual::kSpinner;
  config.spinner_track_color = kSecondaryContainerColor;
  config.spinner_indicator_color = kPrimaryColor;
  config.title = "Scanning music files...";
  config.title_font = Font28();
  config.title_color = kMainTextColor;
  config.title_top = 96;
  config.message = "Reading MP3 files from selected folders";
  config.message_font = Font22();
  config.message_color = kSecondaryTextColor;
  config.message_top = 138;
  lv_obj_t* group = CreatePromptStatus(state->library_content, config);
  if (group == nullptr) {
    return false;
  }
  PositionMusicStatusGroup(group, state, kMusicScanningGroupOffsetY);
  EnableEdgeBackSwipeEventBubble(state->library_content);
  return true;
}

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
  state->settings_closing = true;
  if (!StartSlideRightWindowTransition(state->settings_page,
      state->config.width, kSettingsAnimationMs, state,
      SettingsCloseCompletedCallback)) {
    lv_obj_t* page = state->settings_page;
    state->settings_page = nullptr;
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
  lv_obj_align(row, LV_ALIGN_TOP_MID, 0, 210);
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
      page, title, lv_color_hex(kMainTextColor), Font32());
  if (title_label == nullptr) {
    return false;
  }
  lv_obj_set_width(title_label, state->config.width);
  lv_obj_set_style_text_align(
      title_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
  lv_obj_align(title_label, LV_ALIGN_TOP_MID, 0, kNavigationTitleTop);
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
 * @brief 从长期 RAM 缓存加载音乐源文件夹到视图状态
 * @param state 音乐视图状态
 */
void LoadStoredMusicSources(MusicViewState* state) {
  if (state == nullptr) {
    return;
  }
  auto preferences = std::unique_ptr<app::MusicSourcePreferences>(
      new (std::nothrow) app::MusicSourcePreferences());
  if (preferences == nullptr ||
      !app::GetMusicSourcePreferences(preferences.get())) {
    return;
  }
  const size_t count = std::min<size_t>(
      kMusicFolderOptionCount, app::kMusicSourceCapacity);
  for (size_t index = 0; index < count; ++index) {
    state->source_paths[index] = preferences->paths[index];
    state->source_enabled[index] = !state->source_paths[index].empty();
  }
}

/**
 * @brief 将视图状态中的音乐源文件夹保存到 NVS
 * @param state 音乐视图状态
 * @return 无变化或 NVS 提交成功返回 true，否则返回 false
 */
bool UpdateStoredMusicSources(const MusicViewState* state) {
  if (state == nullptr) {
    return false;
  }
  auto preferences = std::unique_ptr<app::MusicSourcePreferences>(
      new (std::nothrow) app::MusicSourcePreferences());
  if (preferences == nullptr) {
    return false;
  }
  const size_t count = std::min<size_t>(
      kMusicFolderOptionCount, app::kMusicSourceCapacity);
  for (size_t index = 0; index < count; ++index) {
    std::snprintf(preferences->paths[index],
        app::kMusicSourcePathCapacity, "%s",
        state->source_paths[index].c_str());
  }
  return app::UpdateMusicSourcePreferences(*preferences);
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
  UpdateStoredMusicSources(state);
  RefreshMusicLibrary(state);
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
      lv_color_hex(kSecondaryTextColor), MaterialFillIconFont44());
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
 * @brief 处理曲目列表行点击事件
 * @param event LVGL 事件对象
 */
void MusicTrackClickedEventCallback(lv_event_t* event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
    return;
  }
  auto* action =
      static_cast<MusicTrackAction*>(lv_event_get_user_data(event));
  if (action == nullptr || action->state == nullptr) {
    return;
  }
  StartMusicTrack(action->state->session, action->track_index);
  lv_event_stop_bubbling(event);
}

/**
 * @brief 创建曲目列表中的一行
 * @param state 音乐视图状态
 * @param track_index 曲目索引
 * @param y 行顶部坐标
 * @return 创建成功返回 true，否则返回 false
 */
bool CreateMusicTrackRow(
    MusicViewState* state, size_t track_index, int y) {
  if (state == nullptr || state->session == nullptr ||
      state->library_content == nullptr ||
      track_index >= state->session->tracks.size()) {
    return false;
  }
  const app::MusicTrack& track = state->session->tracks[track_index];
  lv_obj_t* row = lv_button_create(state->library_content);
  if (row == nullptr) {
    return false;
  }
  lv_obj_remove_style_all(row);
  lv_obj_add_flag(row, LV_OBJ_FLAG_GESTURE_BUBBLE);
  lv_obj_set_size(row, state->config.width, kMusicTrackRowHeight);
  lv_obj_set_pos(row, 0, y);
  lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_set_style_bg_color(row, lv_color_hex(kPressedColor),
      static_cast<lv_style_selector_t>(LV_PART_MAIN) |
          static_cast<lv_style_selector_t>(LV_STATE_PRESSED));
  lv_obj_set_style_bg_opa(row, LV_OPA_COVER,
      static_cast<lv_style_selector_t>(LV_PART_MAIN) |
          static_cast<lv_style_selector_t>(LV_STATE_PRESSED));
  if (!AddPressCancelOnLeave(row)) {
    lv_obj_delete(row);
    return false;
  }

  lv_obj_t* artwork = CreateArtwork(row, 72, 14);
  if (artwork != nullptr) {
    lv_obj_align(artwork, LV_ALIGN_LEFT_MID, 24, 0);
  }
  lv_obj_t* title = CreateLabel(
      row, track.title.c_str(), lv_color_hex(kMainTextColor), Font28());
  if (title != nullptr) {
    lv_obj_set_size(title, state->config.width - 140, 34);
    lv_label_set_long_mode(title, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 116, 18);
  }
  lv_obj_t* artist = CreateLabel(
      row, track.artist.c_str(), lv_color_hex(kSecondaryTextColor), Font22());
  if (artist != nullptr) {
    lv_obj_set_size(artist, state->config.width - 140, 30);
    lv_label_set_long_mode(artist, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_align(artist, LV_ALIGN_TOP_LEFT, 116, 57);
  }
  lv_obj_t* divider = lv_obj_create(row);
  if (divider != nullptr) {
    lv_obj_remove_flag(divider, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(divider, state->config.width - 116, 1);
    lv_obj_align(divider, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
    lv_obj_set_style_bg_color(
        divider, lv_color_hex(kDividerColor), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(divider, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(divider, 0, LV_PART_MAIN);
  }

  auto action = std::make_unique<MusicTrackAction>();
  action->state = state;
  action->track_index = track_index;
  MusicTrackAction* action_pointer = action.get();
  state->track_actions.push_back(std::move(action));
  lv_obj_add_event_cb(row, MusicTrackClickedEventCallback,
      LV_EVENT_CLICKED, action_pointer);
  return true;
}

bool RenderMusicLibrary(MusicViewState* state) {
  if (state == nullptr || state->session == nullptr ||
      state->library_content == nullptr) {
    return false;
  }
  lv_obj_clean(state->library_content);
  lv_obj_scroll_to_y(state->library_content, 0, LV_ANIM_OFF);
  state->track_actions.clear();
  if (state->session->tracks.empty()) {
    return CreateEmptyMusicContent(state->library_content, state);
  }

  int y = 0;
  for (size_t index = 0; index < state->session->tracks.size(); ++index) {
    if (!CreateMusicTrackRow(state, index, y)) {
      return false;
    }
    y += kMusicTrackRowHeight;
  }
  lv_obj_set_scrollbar_mode(
      state->library_content, LV_SCROLLBAR_MODE_AUTO);
  EnableEdgeBackSwipeEventBubble(state->library_content);
  return true;
}

/**
 * @brief 后台扫描音乐源中的 MP3 文件
 * @param context 音乐扫描任务共享状态
 */
void MusicLibraryScanTaskEntry(void* context) {
  auto* shared_job =
      static_cast<std::shared_ptr<MusicScanJob>*>(context);
  if (shared_job == nullptr) {
    vTaskDelete(nullptr);
    return;
  }
  std::shared_ptr<MusicScanJob> job = *shared_job;
  delete shared_job;
  job->success =
      app::ScanMusicLibrary(job->source_paths, &job->tracks);
  job->completed.store(true, std::memory_order_release);
  vTaskDelete(nullptr);
}

void FinishMusicLibraryScan(MusicPlaybackSession* session) {
  if (session == nullptr || session->scan_job == nullptr ||
      !session->scan_job->completed.load(std::memory_order_acquire)) {
    return;
  }
  std::shared_ptr<MusicScanJob> job = session->scan_job;
  session->scan_job.reset();

  std::string current_path;
  const app::MusicTrack* current_track = CurrentMusicTrack(session);
  if (current_track != nullptr) {
    current_path = current_track->path;
  }
  session->tracks = std::move(job->tracks);
  session->current_track = -1;
  if (!current_path.empty()) {
    for (size_t index = 0; index < session->tracks.size(); ++index) {
      if (session->tracks[index].path == current_path) {
        session->current_track = static_cast<int>(index);
        break;
      }
    }
  }
  if (!current_path.empty() && session->current_track < 0) {
    if (session->audio != nullptr) {
      session->audio->StopAudioFile();
    }
    SetMusicPlaying(session, false, false);
  }
  session->library_initialized = true;
  MusicViewState* state = session->view;
  if (state != nullptr) {
    if (session->current_track < 0) {
      SetMiniPlayerVisible(state, false);
    } else {
      SetMiniPlayerVisible(state, true);
      UpdateCurrentTrackLabels(state);
    }
  }
  if (!job->success) {
    LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
        "Some music source folders could not be scanned\n");
  }
  if (state != nullptr) {
    RenderMusicLibrary(state);
  }
}

/**
 * @brief 挂载音乐存储并启动曲库扫描任务
 * @param state 音乐视图状态
 * @return 成功启动或完成扫描返回 true，否则返回 false
 */
bool StartMusicLibraryScan(MusicViewState* state) {
  if (state == nullptr || state->session == nullptr) {
    return false;
  }
  MusicPlaybackSession* session = state->session;
  if (session->storage == nullptr ||
      !session->storage->EnsureSdCardMounted()) {
    if (session->audio != nullptr) {
      session->audio->StopAudioFile();
    }
    session->tracks.clear();
    session->current_track = -1;
    session->scan_job.reset();
    session->storage_was_mounted = false;
    session->library_initialized = true;
    SetMusicPlaying(session, false, false);
    SetMiniPlayerVisible(state, false);
    return RenderMusicLibrary(state);
  }
  session->storage_was_mounted = true;

  std::vector<std::string> source_paths;
  for (const std::string& path : state->source_paths) {
    if (!path.empty()) {
      source_paths.push_back(path);
    }
  }
  if (session->scan_job != nullptr &&
      !session->scan_job->completed.load(std::memory_order_acquire) &&
      session->scan_job->source_paths == source_paths) {
    return true;
  }
  auto job = std::make_shared<MusicScanJob>();
  job->source_paths = std::move(source_paths);
  session->scan_job = job;

  auto* task_context =
      new (std::nothrow) std::shared_ptr<MusicScanJob>(job);
  if (task_context != nullptr &&
      xTaskCreate(MusicLibraryScanTaskEntry, "music_scan",
          kMusicScanTaskStackBytes, task_context,
          kMusicScanTaskPriority, nullptr) == pdPASS) {
    return true;
  }
  delete task_context;
  job->success = app::ScanMusicLibrary(job->source_paths, &job->tracks);
  job->completed.store(true, std::memory_order_release);
  FinishMusicLibraryScan(session);
  return true;
}

/**
 * @brief 停止等待执行的音乐存储扫描计时器
 * @param state 音乐视图状态
 */
void StopMusicScanStartTimer(MusicViewState* state) {
  if (state == nullptr || state->scan_start_timer == nullptr) {
    return;
  }
  lv_timer_t* timer = state->scan_start_timer;
  state->scan_start_timer = nullptr;
  lv_timer_delete(timer);
}

/**
 * @brief 扫描提示显示后开始挂载存储并扫描音乐
 * @param timer 音乐存储扫描计时器
 */
void MusicScanStartTimerCallback(lv_timer_t* timer) {
  auto* state = static_cast<MusicViewState*>(lv_timer_get_user_data(timer));
  if (state == nullptr || state->scan_start_timer != timer) {
    return;
  }
  state->scan_start_timer = nullptr;
  lv_timer_delete(timer);
  StartMusicLibraryScan(state);
}

bool RefreshMusicLibrary(MusicViewState* state) {
  if (state == nullptr) {
    return false;
  }
  if (state->scan_start_timer != nullptr ||
      (state->session != nullptr && state->session->scan_job != nullptr)) {
    return true;
  }
  if (!RenderMusicScanningContent(state)) {
    return false;
  }
  state->scan_start_timer = lv_timer_create(
      MusicScanStartTimerCallback, kMusicScanStartDelayMs, state);
  if (state->scan_start_timer != nullptr) {
    return true;
  }
  return StartMusicLibraryScan(state);
}

void RefreshMusicClickedEventCallback(lv_event_t* event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
    return;
  }
  auto* state = static_cast<MusicViewState*>(lv_event_get_user_data(event));
  RefreshMusicLibrary(state);
  lv_event_stop_bubbling(event);
}

/**
 * @brief 定时同步硬件播放状态、播放进度和自动切歌
 * @param timer LVGL 定时器
 */
void MusicPlaybackStatusTimerCallback(lv_timer_t* timer) {
  auto* session =
      static_cast<MusicPlaybackSession*>(lv_timer_get_user_data(timer));
  if (session == nullptr) {
    return;
  }
  MusicViewState* state = session->view;
  const bool storage_mounted = session->storage != nullptr &&
      session->storage->IsSdCardMounted();
  if (session->storage_was_mounted && !storage_mounted) {
    session->storage_was_mounted = false;
    if (state != nullptr) {
      StopMusicScanStartTimer(state);
    }
    session->scan_job.reset();
    if (session->audio != nullptr) {
      session->audio->StopAudioFile();
    }
    session->tracks.clear();
    session->current_track = -1;
    session->library_initialized = true;
    SetMusicPlaying(session, false, false);
    if (state != nullptr) {
      SetMiniPlayerVisible(state, false);
      RenderMusicLibrary(state);
    }
    return;
  }
  session->storage_was_mounted = storage_mounted;
  FinishMusicLibraryScan(session);
  const app::MusicTrack* track = CurrentMusicTrack(session);
  if (session->audio == nullptr || track == nullptr) {
    return;
  }

  hal::AudioFilePlaybackStatus status;
  if (!session->audio->ReadAudioFileStatus(&status)) {
    return;
  }
  const uint32_t duration_ms = status.duration_ms == 0
                                   ? track->duration_ms
                                   : status.duration_ms;
  if (state != nullptr && !state->seeking) {
    const int progress = duration_ms == 0
                             ? 0
                             : static_cast<int>(std::min<uint64_t>(
                                   100, static_cast<uint64_t>(
                                            status.elapsed_ms) * 100 /
                                            duration_ms));
    if (state->progress_slider != nullptr) {
      lv_slider_set_value(state->progress_slider, progress, LV_ANIM_OFF);
      lv_obj_invalidate(state->progress_slider);
    }
    SetMusicTimeLabel(state->current_time_label,
        static_cast<int>(status.elapsed_ms / 1000U));
  }
  if (state != nullptr) {
    SetMusicTimeLabel(
        state->total_time_label, static_cast<int>(duration_ms / 1000U));
  }

  if (status.state == hal::AudioFilePlaybackState::kPlaying) {
    if (!session->playing) {
      SetMusicPlaying(session, true, true);
    }
    session->completion_handled = false;
    return;
  }
  if (status.state == hal::AudioFilePlaybackState::kPaused) {
    if (session->playing) {
      SetMusicPlaying(session, false, true);
    }
    return;
  }
  if (status.state == hal::AudioFilePlaybackState::kCompleted &&
      !session->completion_handled) {
    session->completion_handled = true;
    if (session->playback_mode == MusicPlaybackMode::kRepeatOne) {
      StartMusicTrack(session, static_cast<size_t>(session->current_track));
    } else {
      PlayAdjacentMusicTrack(session, 1);
    }
    return;
  }
  if (status.state == hal::AudioFilePlaybackState::kStopped ||
      status.state == hal::AudioFilePlaybackState::kError) {
    if (session->playing) {
      SetMusicPlaying(session, false, true);
    }
  }
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
    lv_obj_align(section, LV_ALIGN_TOP_LEFT, 28, 164);
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
    RefreshMusicLibrary(state);
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
  state->session = GetMusicPlaybackSession();
  state->root = container;
  state->session->audio = config.audio;
  state->session->storage = config.storage;
  state->session->view = state;
  LoadStoredMusicSources(state);
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
  state->library_status_anchor = underline != nullptr ? underline : tab;

  state->library_content = lv_obj_create(container);
  if (state->library_content == nullptr) {
    lv_obj_delete(container);
    return nullptr;
  }
  MakeTransparent(state->library_content);
  lv_obj_set_pos(state->library_content, 0, kMusicLibraryTop);
  lv_obj_set_size(state->library_content, config.width,
      config.height - kMusicLibraryTop);
  lv_obj_set_style_pad_all(state->library_content, 0, LV_PART_MAIN);
  lv_obj_set_scroll_dir(state->library_content, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(
      state->library_content, LV_SCROLLBAR_MODE_AUTO);

  bool content_created = false;
  if (state->session->scan_job != nullptr) {
    content_created = RenderMusicScanningContent(state);
  } else if (state->session->library_initialized) {
    content_created = RenderMusicLibrary(state);
  } else {
    content_created = RefreshMusicLibrary(state);
  }
  if (!CreateMiniPlayer(container, state) || !content_created) {
    LogMessage(LogLevel::kError, __FILE__, __LINE__,
        "CreateMusicView content failed\n");
    lv_obj_delete(container);
    return nullptr;
  }

  if (!CreateMusicHeader(container, state)) {
    lv_obj_delete(container);
    return nullptr;
  }
  if (CurrentMusicTrack(state->session) != nullptr) {
    UpdateCurrentTrackLabels(state);
    SetMiniPlayerVisible(state, true);
  }

  if (state->session->status_timer == nullptr) {
    state->session->status_timer = lv_timer_create(
        MusicPlaybackStatusTimerCallback, kPlaybackStatusIntervalMs,
        state->session);
    if (state->session->status_timer == nullptr) {
      lv_obj_delete(container);
      return nullptr;
    }
  }

  return container;
}

}  // namespace lilygo_box::ui
