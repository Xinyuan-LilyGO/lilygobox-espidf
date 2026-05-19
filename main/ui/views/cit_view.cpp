/*
 * @Description: None
 * @Author: LILYGO_L
 * @Date: 2026-05-10 13:27:05
 * @LastEditTime: 2026-05-15 10:16:17
 * @License: GPL 3.0
 */
#include "ui/views/cit_view.h"

#include <algorithm>
#include <array>
#include <cstdarg>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <ctime>
#include <new>

#include "app/cit_catalog.h"
#include "app/device_identity.h"
#include "esp_app_desc.h"
#include "esp_chip_info.h"
#include "esp_err.h"
#include "esp_flash.h"
#include "esp_heap_caps.h"
#include "esp_image_format.h"
#include "esp_mac.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "hal/providers/providers.h"
#include "sdkconfig.h"
#include "ui/input/app_view_gesture_flags.h"
#include "ui/input/edge_back_gesture.h"
#include "ui/font/font_assets.h"
#include "ui/font/material_symbols_assets.h"
#include "ui/input/press_cancel.h"
#include "ui/animation/transition_animation.h"

namespace lilygo_box::ui {
namespace {

constexpr int kTitleTop = 70;
constexpr int kTitleLeft = 20;
constexpr int kListTop = 136;
constexpr int kListHorizontalPadding = 20;
constexpr int kListTopPadding = 20;
constexpr int kRowHeight = 82;
constexpr int kRowIconWidth = 50;
constexpr int kTestButtonBarHeight = 140;
constexpr int kTestButtonWidth = 200;
constexpr int kTestButtonHeight = 60;
constexpr int kTestButtonGap = 60;
constexpr int kTestButtonCenterOffset = (kTestButtonWidth + kTestButtonGap) / 2;
constexpr int kTestStartButtonWidth = 240;
constexpr int kTestStartButtonHeight = 78;
constexpr int kTouchTraceLineWidth = 6;
constexpr int kTouchMarkerSize = 42;
constexpr int kCitRefreshPeriodMs = 200;
constexpr int kDiagnosticsRefreshPeriodMs = 1000;
constexpr size_t kTouchTraceMaxPointCount = 100;
constexpr size_t kTouchDisplayPointCount = 10;
constexpr uint32_t kGestureSuppressTimeoutMs = 500;
constexpr uint32_t kPageSlideAnimationMs = 180;
constexpr uint32_t kMicrophoneNeedleAnimationMs = 180;
constexpr uint32_t kCitBackgroundColor = 0xFF7F58;
constexpr uint32_t kListBackgroundColor = 0xFBF4E4;
constexpr uint32_t kRowPressedColor = 0xEEDBD1;
constexpr lv_opa_t kRowPressedOpacity = 170;
constexpr int kRowPressedHeight = kRowHeight;
constexpr int kRowPressedRadius = 0;
constexpr uint32_t kReadyColor = 0x138A3D;
constexpr uint32_t kFailedColor = 0xEE2C2C;
constexpr uint32_t kPendingColor = 0xF28C00;
constexpr uint32_t kPassButtonColor = 0x3383FF;
constexpr uint32_t kFailButtonColor = 0xF1EADA;
constexpr uint32_t kPassButtonTextColor = 0xFFFFFF;
constexpr uint32_t kFailButtonTextColor = 0x000000;
constexpr uint32_t kStartButtonColor = 0xE9785C;
constexpr std::array<uint32_t, 5> kScreenColorTestColors = {
    0xFF0000,
    0x00FF00,
    0x0000FF,
    0xFFFFFF,
    0x000000,
};

struct CitViewState;

struct CitStatusRow {
  const app::CitTestEntry* entry = nullptr;
  CitViewState* state = nullptr;
  lv_obj_t* row_object = nullptr;
  lv_obj_t* icon_label = nullptr;
  lv_obj_t* name_label = nullptr;
  lv_obj_t* pressed_background = nullptr;
  bool press_cancelled = false;
  size_t index = 0;
};

struct CitViewState {
  lv_obj_t* root = nullptr;
  lv_obj_t* list_page = nullptr;
  lv_obj_t* test_page = nullptr;
  lv_obj_t* test_content = nullptr;
  lv_obj_t* test_data_label = nullptr;
  lv_obj_t* screen_color_overlay = nullptr;
  lv_obj_t* touch_trace_surface = nullptr;
  lv_obj_t* touch_trace_line = nullptr;
  lv_obj_t* microphone_scale = nullptr;
  lv_obj_t* microphone_needle = nullptr;
  lv_obj_t* microphone_adc_to_dac_switch = nullptr;
  std::array<lv_obj_t*, kTouchDisplayPointCount> touch_point_markers = {};
  int width = 0;
  int height = 0;
  hal::ScreenProvider* screen = nullptr;
  hal::DeviceDiagnosticsProvider* diagnostics_provider = nullptr;
  hal::GpsProvider* gps = nullptr;
  hal::AudioProvider* audio = nullptr;
  hal::HapticProvider* haptic = nullptr;
  hal::BmuProvider* bmu = nullptr;
  hal::RtcProvider* rtc = nullptr;
  hal::ImuProvider* imu = nullptr;
  hal::EthernetProvider* ethernet = nullptr;
  hal::WifiProvider* wifi = nullptr;
  hal::DeviceDiagnostics diagnostics;
  int diagnostics_elapsed_ms = kDiagnosticsRefreshPeriodMs;
  bool diagnostics_read = false;
  hal::GpsStatus gps_status;
  bool gps_status_valid = false;
  std::array<CitStatusRow, app::kMaxCitTestEntryCount> rows;
  std::array<app::CitTestStatus, app::kMaxCitTestEntryCount> test_statuses;
  size_t row_count = 0;
  size_t current_test_index = 0;
  size_t pending_test_index = app::kMaxCitTestEntryCount;
  size_t screen_color_index = 0;
  bool touch_was_seen = false;
  int gps_elapsed_ms = 0;
  int gps_read_elapsed_ms = 0;
  uint32_t gps_update_interval_ms = 1000;
  bool gps_positioned = false;
  int microphone_display_level = 0;
  std::array<lv_point_precise_t, kTouchTraceMaxPointCount> touch_trace_points;
  size_t touch_trace_point_count = 0;
  EdgeBackSwipeState test_edge_back_swipe = {};
  bool test_page_closing = false;
  lv_timer_t* refresh_timer = nullptr;
};

void ShowCitList(CitViewState* state);
bool ShowCitTest(CitViewState* state, size_t index);
void RefreshCitRows(CitViewState* state);
void SetCitRowsClickable(CitViewState* state, bool enabled);
void TestPageEdgeBackEventCallback(lv_event_t* event);
void ScreenColorOverlayEventCallback(lv_event_t* event);

/**
 * @brief 设置对象的文本颜色和字体
 * @param object LVGL 对象
 * @param color 文本颜色
 * @param font 文本字体
 */
void SetTextStyle(lv_obj_t* object, lv_color_t color, const lv_font_t* font) {
  lv_obj_set_style_text_color(object, color, LV_PART_MAIN);
  lv_obj_set_style_text_font(object, font, LV_PART_MAIN);
}

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
 * @brief 获取 32 号 Material Symbols 图标字体
 * @return 字体指针
 */
const lv_font_t* MaterialIconFont32() { return &lvgl_font_material_symbols_32; }

/**
 * @brief 创建并初始化普通文本标签
 * @param parent 父对象
 * @param text 显示文本
 * @param color 文本颜色
 * @param font 文本字体
 * @return 创建成功返回对象指针，否则返回 nullptr
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

bool IsEntryId(const app::CitTestEntry& entry, const char* id);

/**
 * @brief 清除临时屏蔽桌面手势的标记
 * @param timer LVGL 定时器
 */
void ClearSuppressLauncherGestureTimerCallback(lv_timer_t* timer) {
  auto* app_view = static_cast<lv_obj_t*>(lv_timer_get_user_data(timer));
  if (app_view != nullptr && lv_obj_is_valid(app_view)) {
    lv_obj_remove_flag(app_view, kSuppressNextLauncherGestureFlag);
  }
}

/**
 * @brief 临时屏蔽下一次桌面返回手势
 * @param app_view 应用根对象
 */
void SuppressNextLauncherGesture(lv_obj_t* app_view) {
  if (app_view == nullptr) {
    return;
  }

  lv_obj_add_flag(app_view, kSuppressNextLauncherGestureFlag);
  lv_timer_t* timer = lv_timer_create(ClearSuppressLauncherGestureTimerCallback,
      kGestureSuppressTimeoutMs, app_view);
  if (timer != nullptr) {
    lv_timer_set_repeat_count(timer, 1);
  }
}

/**
 * @brief 设置麦克风测试指针数值
 * @param context CIT 页面状态
 * @param value 指针数值
 */
void SetMicrophoneNeedleValue(void* context, int32_t value) {
  auto* state = static_cast<CitViewState*>(context);
  if (state == nullptr || state->microphone_scale == nullptr ||
      state->microphone_needle == nullptr) {
    return;
  }

  state->microphone_display_level = value;
  lv_scale_set_line_needle_value(
      state->microphone_scale, state->microphone_needle, 150, value);
}

/**
 * @brief 恢复 CIT 列表页面的手势处理
 * @param state CIT 页面状态
 */
void RestoreCitListGestures(CitViewState* state) {
  if (state == nullptr || state->root == nullptr) {
    return;
  }

  lv_obj_remove_flag(state->root, kBlockLauncherGestureFlag);
  lv_obj_add_flag(state->root, LV_OBJ_FLAG_GESTURE_BUBBLE);
}

/**
 * @brief 停止当前测试页面关联的硬件任务
 * @param state CIT 页面状态
 */
void StopActiveTestHardware(CitViewState* state) {
  if (state == nullptr || state->current_test_index >= state->row_count) {
    return;
  }

  const app::CitTestEntry* entry = state->rows[state->current_test_index].entry;
  if (entry != nullptr && IsEntryId(*entry, "microphone") &&
      state->audio != nullptr) {
    state->audio->StopMicrophone();
  }
  if (entry != nullptr && IsEntryId(*entry, "gps") && state->gps != nullptr) {
    state->gps->StopGps();
  }
  if (entry != nullptr && IsEntryId(*entry, "wifi") && state->wifi != nullptr) {
    state->wifi->StopWifiTimeTest();
  }
}

/**
 * @brief 清空当前测试页面相关状态
 * @param state CIT 页面状态
 */
void ClearTestPageState(CitViewState* state) {
  if (state == nullptr) {
    return;
  }

  state->test_page = nullptr;
  state->test_content = nullptr;
  state->test_data_label = nullptr;
  state->screen_color_overlay = nullptr;
  state->touch_trace_surface = nullptr;
  state->touch_trace_line = nullptr;
  state->microphone_scale = nullptr;
  state->microphone_needle = nullptr;
  state->microphone_adc_to_dac_switch = nullptr;
  state->touch_point_markers.fill(nullptr);
  state->touch_trace_point_count = 0;
  state->gps_elapsed_ms = 0;
  state->gps_read_elapsed_ms = 0;
  state->gps_update_interval_ms = 1000;
  state->gps_status = hal::GpsStatus();
  state->gps_status_valid = false;
  state->gps_positioned = false;
  state->microphone_display_level = 0;
  state->pending_test_index = app::kMaxCitTestEntryCount;
  state->test_edge_back_swipe = EdgeBackSwipeState();
  state->test_page_closing = false;
}

/**
 * @brief 完成测试页面关闭并恢复列表状态
 * @param state CIT 页面状态
 */
void FinishTestPageClose(CitViewState* state) {
  if (state == nullptr) {
    return;
  }

  const size_t next_index = state->pending_test_index;
  state->pending_test_index = app::kMaxCitTestEntryCount;
  if (state->test_page != nullptr) {
    lv_obj_delete(state->test_page);
  }
  ClearTestPageState(state);
  RestoreCitListGestures(state);
  RefreshCitRows(state);

  if (next_index < state->row_count) {
    SetCitRowsClickable(state, false);
    ShowCitTest(state, next_index);
    return;
  }

  SetCitRowsClickable(state, true);
}

/**
 * @brief 处理测试页面关闭动画完成事件
 * @param animation LVGL 动画
 */
void TestPageCloseCompletedCallback(lv_anim_t* animation) {
  auto* state = static_cast<CitViewState*>(lv_anim_get_user_data(animation));
  FinishTestPageClose(state);
}

/**
 * @brief 获取测试项在测试页展示的标题
 * @param entry 测试项
 * @return 字符串指针
 */
const char* TestTitle(const app::CitTestEntry& entry) {
  if (IsEntryId(entry, "version")) {
    return "Version Info Test";
  }
  if (IsEntryId(entry, "touch")) {
    return "Touch Test";
  }
  if (IsEntryId(entry, "screen")) {
    return "Screen Color Test";
  }
  if (IsEntryId(entry, "vibration")) {
    return "Vibration Test";
  }
  if (IsEntryId(entry, "speaker")) {
    return "Speaker Test";
  }
  if (IsEntryId(entry, "microphone")) {
    return "Microphone Test";
  }
  if (IsEntryId(entry, "imu")) {
    return "IMU Test";
  }
  if (IsEntryId(entry, "bmu")) {
    return "BMU Test";
  }
  if (IsEntryId(entry, "gps")) {
    return "GPS Test";
  }
  if (IsEntryId(entry, "ethernet")) {
    return "Ethernet Test";
  }
  if (IsEntryId(entry, "rtc")) {
    return "RTC Test";
  }
  if (IsEntryId(entry, "wifi")) {
    return "WIFI Get Time Test";
  }
  return entry.name;
}

/**
 * @brief 判断测试项 ID 是否匹配
 * @param entry 测试项
 * @param id id 参数
 * @return 成功返回 true，否则返回 false
 */
bool IsEntryId(const app::CitTestEntry& entry, const char* id) {
  if (entry.id == nullptr || id == nullptr) {
    return false;
  }
  return std::strcmp(entry.id, id) == 0;
}

/**
 * @brief 获取测试状态对应的显示颜色
 * @param status 测试状态
 * @return 颜色值
 */
lv_color_t GetStatusColor(app::CitTestStatus status) {
  switch (status) {
    case app::CitTestStatus::kReady:
      return lv_color_hex(kReadyColor);
    case app::CitTestStatus::kFailed:
      return lv_color_hex(kFailedColor);
    case app::CitTestStatus::kPending:
      return lv_color_hex(kPendingColor);
  }
  return lv_color_hex(kPendingColor);
}

/**
 * @brief 获取测试状态对应的图标
 * @param status 测试状态
 * @return 字符串指针
 */
const char* GetStatusIcon(app::CitTestStatus status) {
  switch (status) {
    case app::CitTestStatus::kReady:
      return icon::kCheckCircle;
    case app::CitTestStatus::kFailed:
      return icon::kCancel;
    case app::CitTestStatus::kPending:
      return icon::kWarning;
  }
  return icon::kWarning;
}

/**
 * @brief 对齐列表行里的状态图标和名称
 * @param icon_label 状态图标标签
 * @param name_label 测试名称标签
 */
void AlignStatusLabels(lv_obj_t* icon_label, lv_obj_t* name_label) {
  if (icon_label == nullptr || name_label == nullptr) {
    return;
  }

  lv_obj_set_width(icon_label, kRowIconWidth);
  lv_obj_set_style_text_align(icon_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
  lv_obj_align(icon_label, LV_ALIGN_LEFT_MID, kListHorizontalPadding, 0);
  lv_obj_align(
      name_label, LV_ALIGN_LEFT_MID, kListHorizontalPadding + kRowIconWidth, 0);
}

/**
 * @brief 获取状态图标使用的字体
 * @return 字体指针
 */
const lv_font_t* GetStatusIconFont() { return MaterialIconFont32(); }

/**
 * @brief 刷新列表页触摸测试触发状态
 * @param state CIT 页面状态
 */
void RefreshTouchState(CitViewState* state) {
  if (state == nullptr || state->screen == nullptr || state->touch_was_seen) {
    return;
  }

  hal::TouchPoint point;
  const bool result = state->screen->ReadScreenTouch(&point);
  if (result) {
    state->touch_was_seen = true;
  }
}

/**
 * @brief 向固定缓冲区安全追加格式化文本
 * @param text 显示文本
 * @param text_size 文本缓冲区大小
 * @param used 已使用长度
 * @param format 格式化字符串
 */
void AppendFormatted(
    char* text, size_t text_size, size_t* used, const char* format, ...) {
  if (text == nullptr || text_size == 0 || used == nullptr ||
      *used >= text_size) {
    return;
  }

  va_list args;
  va_start(args, format);
  const int written =
      std::vsnprintf(text + *used, text_size - *used, format, args);
  va_end(args);

  if (written <= 0) {
    return;
  }

  const size_t remaining = text_size - *used;
  *used += std::min(static_cast<size_t>(written), remaining - 1);
}

/**
 * @brief 更新当前多点触摸标记的位置和显示状态
 * @param state CIT 页面状态
 * @param points 触摸点数组
 * @param point_count 触摸点数量
 */
void UpdateTouchPointMarkers(
    CitViewState* state, const hal::TouchPoint* points, size_t point_count) {
  if (state == nullptr || state->touch_trace_surface == nullptr) {
    return;
  }

  const int32_t surface_width = lv_obj_get_width(state->touch_trace_surface);
  const int32_t surface_height = lv_obj_get_height(state->touch_trace_surface);
  if (surface_width <= 0 || surface_height <= 0) {
    return;
  }

  const int32_t max_x = std::max<int32_t>(0, surface_width - kTouchMarkerSize);
  const int32_t max_y = std::max<int32_t>(0, surface_height - kTouchMarkerSize);
  for (size_t i = 0; i < state->touch_point_markers.size(); ++i) {
    lv_obj_t* marker = state->touch_point_markers[i];
    if (marker == nullptr) {
      continue;
    }

    if (points == nullptr || i >= point_count) {
      lv_obj_add_flag(marker, LV_OBJ_FLAG_HIDDEN);
      continue;
    }

    int32_t x = points[i].x - kTouchMarkerSize / 2;
    int32_t y = points[i].y - kTouchMarkerSize / 2;
    x = std::min(std::max<int32_t>(x, 0), max_x);
    y = std::min(std::max<int32_t>(y, 0), max_y);

    lv_obj_remove_flag(marker, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_pos(marker, x, y);
  }
}

/**
 * @brief 刷新触摸测试页面的 10 点数据和压力值
 * @param state CIT 页面状态
 */
void RefreshTouchTestData(CitViewState* state) {
  if (state == nullptr || state->test_data_label == nullptr) {
    return;
  }

  std::array<hal::TouchPoint, kTouchDisplayPointCount> points = {};
  size_t point_count = 0;
  if (state->screen != nullptr && state->screen->ReadScreenTouchPoints(points.data(),
                                      points.size(), &point_count)) {
    state->touch_was_seen = point_count > 0;
  }

  UpdateTouchPointMarkers(state, points.data(), point_count);

  char text[768] = {};
  size_t used = 0;
  AppendFormatted(text, sizeof(text), &used,
      "touch data:\nactive: %u/%u\ntrace: %u\n",
      static_cast<unsigned>(point_count),
      static_cast<unsigned>(kTouchDisplayPointCount),
      static_cast<unsigned>(state->touch_trace_point_count));
  for (size_t i = 0; i < kTouchDisplayPointCount; ++i) {
    if (i < point_count) {
      AppendFormatted(text, sizeof(text), &used,
          "P%02u id:%02u x:%4d y:%4d p:%3u\n", static_cast<unsigned>(i + 1),
          static_cast<unsigned>(points[i].id), static_cast<int>(points[i].x),
          static_cast<int>(points[i].y),
          static_cast<unsigned>(points[i].pressure));
    } else {
      AppendFormatted(text, sizeof(text), &used,
          "P%02u id:-- x:---- y:---- p:---\n", static_cast<unsigned>(i + 1));
    }
  }

  lv_label_set_text(state->test_data_label, text);
}

/**
 * @brief 刷新扬声器测试播放状态
 * @param state CIT 页面状态
 */
void RefreshSpeakerTestData(CitViewState* state) {
  if (state == nullptr || state->test_data_label == nullptr) {
    return;
  }

  hal::SpeakerStatus status;
  if (state->audio == nullptr ||
      !state->audio->ReadSpeakerToneStatus(&status)) {
    lv_label_set_text(
        state->test_data_label, "speaker data:\nstatus: unsupported");
    return;
  }

  const char* state_text = "ready";
  if (status.running) {
    state_text = "playing built-in notification audio";
  } else if (status.completed) {
    state_text =
        status.success ? "playback complete" : "playback failed";
  }

  char text[192] = {};
  std::snprintf(text, sizeof(text),
      "speaker data:\n"
      "status: %s\n"
      "audio: 44.1 kHz / 16-bit / stereo\n"
      "written: %u/%u bytes",
      state_text, static_cast<unsigned int>(status.bytes_written),
      static_cast<unsigned int>(status.total_bytes));
  lv_label_set_text(state->test_data_label, text);
}

/**
 * @brief 刷新麦克风测试数据和指针
 * @param state CIT 页面状态
 */
void RefreshMicrophoneTestData(CitViewState* state) {
  if (state == nullptr || state->test_data_label == nullptr) {
    return;
  }

  hal::MicrophoneStatus status;
  if (state->audio == nullptr || !state->audio->ReadMicrophoneStatus(&status)) {
    lv_label_set_text(
        state->test_data_label, "microphone data:\nstatus: unsupported");
    return;
  }

  if (state->microphone_scale != nullptr &&
      state->microphone_needle != nullptr) {
    lv_anim_delete(state, SetMicrophoneNeedleValue);
    lv_anim_t animation;
    lv_anim_init(&animation);
    lv_anim_set_var(&animation, state);
    lv_anim_set_exec_cb(&animation, SetMicrophoneNeedleValue);
    lv_anim_set_values(
        &animation, state->microphone_display_level, status.level_percent);
    lv_anim_set_time(&animation, kMicrophoneNeedleAnimationMs);
    lv_anim_set_path_cb(&animation, lv_anim_path_ease_out);
    lv_anim_start(&animation);
  }

  char text[192] = {};
  std::snprintf(text, sizeof(text),
      "microphone data:\n"
      "status: %s  level: %d%%\n"
      "peak: %d",
      status.running ? "listening" : "stopped", status.level_percent,
      status.peak_sample);
  lv_label_set_text(state->test_data_label, text);
}

/**
 * @brief 刷新 GPS 测试页面的 GNSS 定位数据
 * @param state CIT 页面状态
 */
void RefreshGpsTestData(CitViewState* state) {
  if (state == nullptr || state->test_data_label == nullptr) {
    return;
  }

  if (state->gps == nullptr) {
    lv_label_set_text(state->test_data_label, "GPS data:\nstatus: unsupported");
    return;
  }

  if (state->gps_status_valid) {
    state->gps_read_elapsed_ms += kCitRefreshPeriodMs;
  }

  const uint32_t update_interval_ms =
      std::max<uint32_t>(state->gps_update_interval_ms, kCitRefreshPeriodMs);
  const bool should_read =
      !state->gps_status_valid ||
      state->gps_read_elapsed_ms >= static_cast<int>(update_interval_ms);
  if (should_read) {
    hal::GpsStatus status;
    if (!state->gps->ReadGpsStatus(&status)) {
      lv_label_set_text(
          state->test_data_label, "GPS data:\nstatus: read failed");
      return;
    }
    state->gps_status = status;
    state->gps_status_valid = true;
    state->gps_read_elapsed_ms = 0;
    if (status.update_interval_ms > 0) {
      state->gps_update_interval_ms = status.update_interval_ms;
    }
  }

  const hal::GpsStatus& status = state->gps_status;
  if (status.running && !state->gps_positioned) {
    state->gps_elapsed_ms += kCitRefreshPeriodMs;
    if (status.positioned) {
      state->gps_positioned = true;
    }
  }

  const char* status_text = "waiting for module data";
  if (!status.running) {
    status_text = "stopped";
  } else if (status.data_ready && status.parse_success) {
    status_text = "GNSS parsed";
  } else if (status.data_ready) {
    status_text = "waiting for valid GNSS data";
  }

  char text[2048] = {};
  size_t used = 0;
  AppendFormatted(text, sizeof(text), &used,
      "GPS data:\nstatus: %s\n%s: %d s\nread bytes: %u\n", status_text,
      state->gps_positioned ? "location found time" : "getting location time",
      (state->gps_elapsed_ms + 999) / 1000,
      static_cast<unsigned int>(status.bytes_read));
  AppendFormatted(text, sizeof(text), &used,
      "update interval: %u ms\n"
      "location status: %s\n"
      "mode: %s  nav: %s\n\n",
      static_cast<unsigned int>(state->gps_update_interval_ms),
      status.location_status[0] == '\0' ? "unknown" : status.location_status,
      status.mode_indicator[0] == '\0' ? "unknown" : status.mode_indicator,
      status.navigational_status[0] == '\0' ? "unknown"
                                             : status.navigational_status);

  char fix_quality_text[16] = "unknown";
  char fix_mode_text[16] = "unknown";
  char satellites_used_text[16] = "unknown";
  char satellites_in_view_text[16] = "unknown";
  char hdop_text[16] = "unknown";
  char pdop_text[16] = "unknown";
  char vdop_text[16] = "unknown";
  if (status.fix_quality_ready) {
    std::snprintf(fix_quality_text, sizeof(fix_quality_text), "%u",
        static_cast<unsigned int>(status.fix_quality));
  }
  if (status.fix_mode_ready) {
    std::snprintf(fix_mode_text, sizeof(fix_mode_text), "%u",
        static_cast<unsigned int>(status.fix_mode));
  }
  if (status.satellites_used_ready) {
    std::snprintf(satellites_used_text, sizeof(satellites_used_text), "%u",
        static_cast<unsigned int>(status.satellites_used));
  }
  if (status.satellites_in_view_ready) {
    std::snprintf(satellites_in_view_text, sizeof(satellites_in_view_text),
        "%u", static_cast<unsigned int>(status.satellites_in_view));
  }
  if (status.hdop_ready) {
    std::snprintf(hdop_text, sizeof(hdop_text), "%.2f", status.hdop);
  }
  if (status.pdop_ready) {
    std::snprintf(pdop_text, sizeof(pdop_text), "%.2f", status.pdop);
  }
  if (status.vdop_ready) {
    std::snprintf(vdop_text, sizeof(vdop_text), "%.2f", status.vdop);
  }

  AppendFormatted(text, sizeof(text), &used,
      "fix quality: %s\nfix mode: %s\n",
      fix_quality_text, fix_mode_text);
  AppendFormatted(text, sizeof(text), &used,
      "satellites used: %s\nsatellites in view: %s\n"
      "satellite records: %u\n",
      satellites_used_text, satellites_in_view_text,
      static_cast<unsigned int>(status.satellite_info_count));
  if (status.strongest_satellite_ready) {
    AppendFormatted(text, sizeof(text), &used,
        "strongest satellite: %u  C/N0: %d\n",
        static_cast<unsigned int>(status.strongest_satellite_id),
        static_cast<int>(status.strongest_satellite_cn0));
  } else {
    AppendFormatted(
        text, sizeof(text), &used, "strongest satellite: unknown\n");
  }
  AppendFormatted(text, sizeof(text), &used,
      "HDOP: %s  PDOP: %s  VDOP: %s\n", hdop_text, pdop_text, vdop_text);
  if (status.altitude_ready) {
    AppendFormatted(text, sizeof(text), &used, "altitude: %.2f %s\n",
        status.altitude,
        status.altitude_unit[0] == '\0' ? "m" : status.altitude_unit);
  } else {
    AppendFormatted(text, sizeof(text), &used, "altitude: unknown\n");
  }
  if (status.speed_ready) {
    AppendFormatted(text, sizeof(text), &used,
        "speed: %.2f km/h  %.2f kn\n", status.speed_kmh,
        status.speed_knots);
  } else {
    AppendFormatted(text, sizeof(text), &used, "speed: unknown\n");
  }
  if (status.course_ready) {
    AppendFormatted(
        text, sizeof(text), &used, "course: %.2f deg\n\n",
        status.course_degree);
  } else {
    AppendFormatted(text, sizeof(text), &used, "course: unknown\n\n");
  }

  if (status.utc.ready) {
    AppendFormatted(text, sizeof(text), &used, "utc: %02u:%02u:%05.2f\n",
        static_cast<unsigned int>(status.utc.hour),
        static_cast<unsigned int>(status.utc.minute), status.utc.second);
  } else {
    AppendFormatted(text, sizeof(text), &used, "utc: unknown\n");
  }

  if (status.date.ready) {
    AppendFormatted(text, sizeof(text), &used, "date: %04u-%02u-%02u\n",
        static_cast<unsigned int>(status.date.year),
        static_cast<unsigned int>(status.date.month),
        static_cast<unsigned int>(status.date.day));
  } else {
    AppendFormatted(text, sizeof(text), &used, "date: unknown\n");
  }

  if (status.latitude.ready) {
    AppendFormatted(text, sizeof(text), &used,
        "\nlat degrees: %u\nlat minutes: %.6f\n"
        "lat degrees_minutes: %.8f\nlat direction: %s\n",
        static_cast<unsigned int>(status.latitude.degrees),
        status.latitude.minutes, status.latitude.degrees_minutes,
        status.latitude.direction[0] == '\0' ? "unknown"
                                             : status.latitude.direction);
  } else {
    AppendFormatted(text, sizeof(text), &used, "\nlat: unknown\n");
  }

  if (status.longitude.ready) {
    AppendFormatted(text, sizeof(text), &used,
        "\nlon degrees: %u\nlon minutes: %.6f\n"
        "lon degrees_minutes: %.8f\nlon direction: %s",
        static_cast<unsigned int>(status.longitude.degrees),
        status.longitude.minutes, status.longitude.degrees_minutes,
        status.longitude.direction[0] == '\0' ? "unknown"
                                              : status.longitude.direction);
  } else {
    AppendFormatted(text, sizeof(text), &used, "\nlon: unknown");
  }

  lv_label_set_text(state->test_data_label, text);
}

/**
 * @brief 将 RTC 星期数字转换为显示文本
 * @param week 星期数字
 * @return 星期文本
 */
const char* RtcWeekName(uint8_t week) {
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
      return "unknown";
  }
}

/**
 * @brief 刷新 PCF8563 RTC 日期时间和完整性状态
 * @param state CIT 页面状态
 */
void RefreshRtcTestData(CitViewState* state) {
  if (state == nullptr || state->test_data_label == nullptr) {
    return;
  }

  hal::RtcStatus status;
  if (state->rtc == nullptr) {
    lv_label_set_text(state->test_data_label, "RTC data:\nstatus: unsupported");
    return;
  }

  if (!state->rtc->ReadRtcStatus(&status)) {
    lv_label_set_text(state->test_data_label, "RTC data:\nstatus: read failed");
    return;
  }

  char text[320] = {};
  std::snprintf(text, sizeof(text),
      "RTC data:\n"
      "status: %s\n"
      "clock integrity: %s\n"
      "\n"
      "date: %04u/%02u/%02u\n"
      "time: %02u:%02u:%02u\n"
      "week: %s",
      status.ready ? "ready" : "not ready",
      status.clock_integrity ? "valid" : "not guaranteed",
      static_cast<unsigned int>(status.year),
      static_cast<unsigned int>(status.month),
      static_cast<unsigned int>(status.day),
      static_cast<unsigned int>(status.hour),
      static_cast<unsigned int>(status.minute),
      static_cast<unsigned int>(status.second), RtcWeekName(status.week));
  lv_label_set_text(state->test_data_label, text);
}

/**
 * @brief 格式化打包后的 MAC 地址
 * @param mac_address 打包后的 MAC 地址
 * @param buffer 输出缓冲区
 * @param size 输出缓冲区大小
 */
void FormatPackedMacAddress(uint64_t mac_address, char* buffer, size_t size) {
  if (buffer == nullptr || size == 0) {
    return;
  }

  if (mac_address == 0) {
    std::snprintf(buffer, size, "waiting");
    return;
  }

  std::snprintf(buffer, size, "%02X:%02X:%02X:%02X:%02X:%02X",
      static_cast<unsigned int>((mac_address >> 40) & 0xFF),
      static_cast<unsigned int>((mac_address >> 32) & 0xFF),
      static_cast<unsigned int>((mac_address >> 24) & 0xFF),
      static_cast<unsigned int>((mac_address >> 16) & 0xFF),
      static_cast<unsigned int>((mac_address >> 8) & 0xFF),
      static_cast<unsigned int>(mac_address & 0xFF));
}

/**
 * @brief 格式化 ESP IPv4 地址
 * @param address IPv4 地址原始值
 * @param buffer 输出缓冲区
 * @param size 输出缓冲区大小
 */
void FormatIpv4Address(uint32_t address, char* buffer, size_t size) {
  if (buffer == nullptr || size == 0) {
    return;
  }

  if (address == 0) {
    std::snprintf(buffer, size, "--");
    return;
  }

  std::snprintf(buffer, size, "%u.%u.%u.%u",
      static_cast<unsigned int>(address & 0xFF),
      static_cast<unsigned int>((address >> 8) & 0xFF),
      static_cast<unsigned int>((address >> 16) & 0xFF),
      static_cast<unsigned int>((address >> 24) & 0xFF));
}

/**
 * @brief 刷新以太网链路和 DHCP 状态
 * @param state CIT 页面状态
 */
void RefreshEthernetTestData(CitViewState* state) {
  if (state == nullptr || state->test_data_label == nullptr) {
    return;
  }

  hal::EthernetStatus status;
  if (state->ethernet == nullptr ||
      !state->ethernet->ReadEthernetStatus(&status)) {
    lv_label_set_text(
        state->test_data_label, "Ethernet data:\nstatus: unsupported");
    return;
  }

  const char* status_text = "idle";
  if (status.init_task_running) {
    status_text = "initializing";
  } else if (status.start_failed) {
    status_text = "start failed";
  } else if (status.running) {
    status_text = "started";
  } else if (status.driver_initialized) {
    status_text = "driver ready";
  }

  const char* dhcp_text = "no link";
  if (status.got_ip) {
    dhcp_text = "got ip";
  } else if (status.link_up) {
    dhcp_text = "waiting";
  }

  char mac_address[24] = {};
  char ip_address[20] = {};
  char netmask[20] = {};
  char gateway[20] = {};
  FormatPackedMacAddress(
      status.mac_address, mac_address, sizeof(mac_address));
  FormatIpv4Address(status.ip_address, ip_address, sizeof(ip_address));
  FormatIpv4Address(status.netmask, netmask, sizeof(netmask));
  FormatIpv4Address(status.gateway, gateway, sizeof(gateway));

  char text[640] = {};
  size_t used = 0;
  AppendFormatted(text, sizeof(text), &used,
      "Ethernet data:\n"
      "status: %s\n"
      "port count: %d\n"
      "cable: %s\n"
      "dhcp: %s\n"
      "\n"
      "mac:\n"
      "     %s\n"
      "ip:\n"
      "     %s\n"
      "mask:\n"
      "     %s\n"
      "gateway:\n"
      "     %s",
      status_text, status.port_count, status.link_up ? "inserted" : "removed",
      dhcp_text, mac_address, ip_address, netmask, gateway);
  if (status.start_failed && status.last_error != ESP_OK) {
    AppendFormatted(text, sizeof(text), &used, "\nerror: %#X",
        static_cast<unsigned int>(status.last_error));
  }

  lv_label_set_text(state->test_data_label, text);
}

/**
 * @brief 格式化 WiFi SNTP 获取到的北京时间
 * @param unix_time UTC Unix 时间戳
 * @param buffer 输出缓冲区
 * @param size 输出缓冲区大小
 */
void FormatWifiChinaTime(int64_t unix_time, char* buffer, size_t size) {
  if (buffer == nullptr || size == 0) {
    return;
  }

  if (unix_time <= 0) {
    std::snprintf(buffer, size, "--");
    return;
  }

  const std::time_t adjusted_time =
      static_cast<std::time_t>(unix_time + 8 * 60 * 60);
  std::tm time_info = {};
  gmtime_r(&adjusted_time, &time_info);
  std::snprintf(buffer, size, "%04d-%02d-%02d %02d:%02d:%02d",
      time_info.tm_year + 1900, time_info.tm_mon + 1, time_info.tm_mday,
      time_info.tm_hour, time_info.tm_min, time_info.tm_sec);
}

/**
 * @brief 刷新 WiFi 获取时间测试的连接、DHCP 和 SNTP 状态
 * @param state CIT 页面状态
 */
void RefreshWifiTestData(CitViewState* state) {
  if (state == nullptr || state->test_data_label == nullptr) {
    return;
  }

  hal::WifiStatus status;
  if (state->wifi == nullptr || !state->wifi->ReadWifiStatus(&status)) {
    lv_label_set_text(
        state->test_data_label, "WIFI time data:\nstatus: unsupported");
    return;
  }

  const char* status_text = "idle";
  if (status.init_task_running) {
    status_text = "initializing";
  } else if (status.start_failed) {
    status_text = "connect failed";
  } else if (status.time_synced) {
    status_text = "time synced";
  } else if (status.time_sync_started) {
    status_text = "syncing time";
  } else if (status.got_ip) {
    status_text = "got ip";
  } else if (status.connected) {
    status_text = "waiting dhcp";
  } else if (status.running) {
    status_text = "connecting";
  } else if (status.driver_initialized) {
    status_text = "driver ready";
  }

  const char* dhcp_text = "waiting";
  if (status.got_ip) {
    dhcp_text = "got ip";
  } else if (!status.connected) {
    dhcp_text = "no link";
  }

  char mac_address[24] = {};
  char ip_address[20] = {};
  char netmask[20] = {};
  char gateway[20] = {};
  char china_time[32] = {};
  char sync_age[16] = {};
  FormatPackedMacAddress(status.mac_address, mac_address, sizeof(mac_address));
  FormatIpv4Address(status.ip_address, ip_address, sizeof(ip_address));
  FormatIpv4Address(status.netmask, netmask, sizeof(netmask));
  FormatIpv4Address(status.gateway, gateway, sizeof(gateway));
  FormatWifiChinaTime(status.unix_time, china_time, sizeof(china_time));
  if (status.time_synced) {
    std::snprintf(sync_age, sizeof(sync_age), "%lu s",
        static_cast<unsigned long>(status.time_sync_age_s));
  } else {
    std::snprintf(sync_age, sizeof(sync_age), "--");
  }

  char text[800] = {};
  size_t used = 0;
  AppendFormatted(text, sizeof(text), &used,
      "WIFI time data:\n"
      "status: %s\n"
      "wifi: %s\n"
      "ssid: %s\n"
      "connect: %s\n"
      "dhcp: %s\n"
      "retry: %d\n"
      "\n"
      "signal:\n"
      "     rssi: %d dBm\n"
      "     channel: %d\n"
      "mac:\n"
      "     %s\n"
      "ip:\n"
      "     %s\n"
      "mask:\n"
      "     %s\n"
      "gateway:\n"
      "     %s\n"
      "\n"
      "time:\n"
      "     network: %s\n"
      "     sync age: %s",
      status_text, status.running ? "on" : "off",
      status.ssid[0] == '\0' ? "--" : status.ssid,
      status.connected ? "connected" : "disconnected", dhcp_text,
      status.retry_count, status.rssi, status.channel, mac_address, ip_address,
      netmask, gateway, china_time, sync_age);

  if (status.start_failed) {
    AppendFormatted(text, sizeof(text), &used,
        "\nerror: %#X\nreason: %d",
        static_cast<unsigned int>(status.last_error),
        status.disconnect_reason);
  }

  lv_label_set_text(state->test_data_label, text);
}

/**
 * @brief 按固定周期刷新诊断数据
 * @param state CIT 页面状态
 */
void RefreshDiagnosticsState(CitViewState* state) {
  if (state == nullptr) {
    return;
  }
  if (state->diagnostics_elapsed_ms < kDiagnosticsRefreshPeriodMs) {
    state->diagnostics_elapsed_ms += kCitRefreshPeriodMs;
    return;
  }

  state->diagnostics = hal::DeviceDiagnostics();
  bool result = false;
  if (state->bmu != nullptr) {
    result |= state->bmu->ReadBmuStatus(&state->diagnostics.bmu);
  }
  if (state->imu != nullptr) {
    result |= state->imu->ReadImuStatus(&state->diagnostics.imu);
  }
  if (!result && state->diagnostics_provider != nullptr) {
    result = state->diagnostics_provider->ReadDeviceDiagnostics(&state->diagnostics);
  }
  state->diagnostics_read = result;
  state->diagnostics_elapsed_ms = 0;
}

/**
 * @brief 获取测试项当前运行时状态
 * @param state CIT 页面状态
 * @param index 测试项索引
 * @return 测试状态
 */
app::CitTestStatus GetRuntimeStatus(const CitViewState& state, size_t index) {
  if (index < state.test_statuses.size()) {
    return state.test_statuses[index];
  }
  return app::CitTestStatus::kPending;
}

/**
 * @brief 更新列表行里的图标、文字和颜色
 * @param icon_label 状态图标标签
 * @param name_label 测试名称标签
 * @param status 测试状态
 */
void UpdateStatusRow(
    lv_obj_t* icon_label, lv_obj_t* name_label, app::CitTestStatus status) {
  if (icon_label == nullptr || name_label == nullptr) {
    return;
  }

  const lv_color_t color = GetStatusColor(status);
  lv_label_set_text(icon_label, GetStatusIcon(status));
  lv_obj_set_style_text_color(icon_label, color, LV_PART_MAIN);
  lv_obj_set_style_text_font(icon_label, GetStatusIconFont(), LV_PART_MAIN);
  lv_obj_set_style_text_color(name_label, color, LV_PART_MAIN);
  AlignStatusLabels(icon_label, name_label);
}

/**
 * @brief 刷新 CIT 列表中所有测试项状态
 * @param state CIT 页面状态
 */
void RefreshCitRows(CitViewState* state) {
  if (state == nullptr) {
    return;
  }

  RefreshTouchState(state);
  RefreshDiagnosticsState(state);
  for (size_t i = 0; i < state->row_count; ++i) {
    const CitStatusRow& row = state->rows[i];
    if (row.entry == nullptr) {
      continue;
    }
    const app::CitTestStatus status = GetRuntimeStatus(*state, row.index);
    UpdateStatusRow(row.icon_label, row.name_label, status);
  }
}

/**
 * @brief 刷新当前测试页里的动态数据
 * @param state CIT 页面状态
 */
void RefreshActiveTestData(CitViewState* state) {
  if (state == nullptr || state->test_data_label == nullptr ||
      state->current_test_index >= state->row_count) {
    return;
  }

  const app::CitTestEntry* entry = state->rows[state->current_test_index].entry;
  if (entry == nullptr) {
    return;
  }

  char text[640] = {};
  if (IsEntryId(*entry, "touch")) {
    RefreshTouchTestData(state);
    return;
  }

  if (IsEntryId(*entry, "speaker")) {
    RefreshSpeakerTestData(state);
    return;
  }

  if (IsEntryId(*entry, "microphone")) {
    RefreshMicrophoneTestData(state);
    return;
  }

  if (IsEntryId(*entry, "gps")) {
    RefreshGpsTestData(state);
    return;
  }

  if (IsEntryId(*entry, "ethernet")) {
    RefreshEthernetTestData(state);
    return;
  }

  if (IsEntryId(*entry, "wifi")) {
    RefreshWifiTestData(state);
    return;
  }

  if (IsEntryId(*entry, "rtc")) {
    RefreshRtcTestData(state);
    return;
  }

  if (IsEntryId(*entry, "imu")) {
    const hal::ImuStatus& imu = state->diagnostics.imu;
    std::snprintf(text, sizeof(text),
        "imu data:\nstatus: %s\nx: %.2f g\ny: %.2f g\nz: %.2f g",
        imu.ready ? "ready" : "not ready", imu.acceleration_x_g,
        imu.acceleration_y_g, imu.acceleration_z_g);
    lv_label_set_text(state->test_data_label, text);
    return;
  }

  if (IsEntryId(*entry, "bmu")) {
    const hal::BmuStatus& bmu = state->diagnostics.bmu;
    std::snprintf(text, sizeof(text),
        "BMU data:\nstatus: %s\npack: %s\ncharging: %s\n"
        "discharging: %s\nfull: %s\nempty: %s\n"
        "\n"
        "voltage: %d mV\ncurrent: %d mA\naverage current: %d mA\n"
        "average BMU: %d mW\n"
        "\n"
        "charge: %d%%\nhealth: %d%%\ncycle count: %d\n"
        "capacity:\n"
        "     remaining: %d mAh\n"
        "     full: %d mAh\n"
        "     design: %d mAh\n"
        "\n"
        "time:\n"
        "     empty: %d min\n"
        "     full: %d min\n"
        "\n"
        "temperature:\n"
        "     pack: %.2f C\n"
        "     gauge: %.2f C",
        bmu.ready ? "ready" : "not ready",
        bmu.pack_present ? "present" : "none",
        bmu.charging ? "yes" : "no", bmu.discharging ? "yes" : "no",
        bmu.full_charged ? "yes" : "no", bmu.full_discharged ? "yes" : "no",
        bmu.voltage_mv, bmu.current_ma, bmu.average_current_ma,
        bmu.average_bmu_mw, bmu.charge_percent, bmu.health_percent,
        bmu.cycle_count, bmu.remaining_capacity_mah,
        bmu.full_charge_capacity_mah, bmu.design_capacity_mah,
        bmu.time_to_empty_min, bmu.time_to_full_min, bmu.pack_temperature_c,
        bmu.gauge_temperature_c);
    lv_label_set_text(state->test_data_label, text);
  }
}

/**
 * @brief 处理 CIT 页面定时刷新事件
 * @param timer LVGL 定时器
 */
void CitRefreshTimerCallback(lv_timer_t* timer) {
  auto* state = static_cast<CitViewState*>(lv_timer_get_user_data(timer));
  RefreshCitRows(state);
  RefreshActiveTestData(state);
}

/**
 * @brief 处理 CIT 根对象删除事件
 * @param event LVGL 事件
 */
void CitViewDeleteCallback(lv_event_t* event) {
  if (lv_event_get_code(event) != LV_EVENT_DELETE) {
    return;
  }

  auto* state = static_cast<CitViewState*>(lv_event_get_user_data(event));
  if (state == nullptr) {
    return;
  }

  if (state->refresh_timer != nullptr) {
    lv_timer_delete(state->refresh_timer);
    state->refresh_timer = nullptr;
  }
  StopActiveTestHardware(state);
  delete state;
}

/**
 * @brief 判断当前触摸点是否仍在对象区域内
 * @param object LVGL 对象
 * @return 在对象区域内返回 true，否则返回 false
 */
bool IsActivePointerInsideObject(lv_obj_t* object) {
  if (object == nullptr) {
    return false;
  }

  lv_indev_t* indev = lv_indev_active();
  if (indev == nullptr) {
    return false;
  }

  lv_point_t point = {};
  lv_indev_get_point(indev, &point);

  lv_area_t coords = {};
  lv_obj_get_coords(object, &coords);
  return point.x >= coords.x1 && point.x <= coords.x2 && point.y >= coords.y1 &&
         point.y <= coords.y2;
}

/**
 * @brief 设置列表行按下背景的显示状态
 * @param row 状态行
 * @param pressed 是否按下
 */
void SetCitRowPressed(CitStatusRow* row, bool pressed) {
  if (row == nullptr || row->pressed_background == nullptr) {
    return;
  }

  if (pressed) {
    lv_obj_remove_flag(row->pressed_background, LV_OBJ_FLAG_HIDDEN);
    return;
  }

  lv_obj_add_flag(row->pressed_background, LV_OBJ_FLAG_HIDDEN);
}

/**
 * @brief 判断 CIT 列表当前是否允许点击
 * @param state CIT 页面状态
 * @return 允许点击返回 true，否则返回 false
 */
bool IsCitListClickable(const CitViewState* state) {
  return state != nullptr && state->test_page == nullptr &&
         !state->test_page_closing &&
         state->pending_test_index >= state->row_count;
}

/**
 * @brief 设置 CIT 列表行是否可以点击
 * @param state CIT 页面状态
 * @param enabled 是否允许点击
 */
void SetCitRowsClickable(CitViewState* state, bool enabled) {
  if (state == nullptr) {
    return;
  }

  for (size_t i = 0; i < state->row_count; ++i) {
    CitStatusRow& row = state->rows[i];
    if (row.row_object == nullptr) {
      continue;
    }

    row.press_cancelled = true;
    SetCitRowPressed(&row, false);
    if (enabled) {
      lv_obj_add_flag(row.row_object, LV_OBJ_FLAG_CLICKABLE);
      continue;
    }
    lv_obj_remove_flag(row.row_object, LV_OBJ_FLAG_CLICKABLE);
  }
}

/**
 * @brief 处理 CIT 列表行事件
 * @param event LVGL 事件
 */
void CitRowEventCallback(lv_event_t* event) {
  auto* row = static_cast<CitStatusRow*>(lv_event_get_user_data(event));
  if (row == nullptr || row->entry == nullptr) {
    return;
  }

  const lv_event_code_t code = lv_event_get_code(event);
  if (!IsCitListClickable(row->state)) {
    row->press_cancelled = true;
    SetCitRowPressed(row, false);
    return;
  }

  if (code == LV_EVENT_PRESSED) {
    row->press_cancelled = false;
    SetCitRowPressed(row, true);
    return;
  }
  if (code == LV_EVENT_PRESSING) {
    auto* target = static_cast<lv_obj_t*>(lv_event_get_current_target(event));
    if (!IsActivePointerInsideObject(target)) {
      row->press_cancelled = true;
      SetCitRowPressed(row, false);
    }
    return;
  }
  if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
    SetCitRowPressed(row, false);
    return;
  }
  if (code == LV_EVENT_CLICKED) {
    auto* target = static_cast<lv_obj_t*>(lv_event_get_current_target(event));
    const bool click_cancelled =
        row->press_cancelled || !IsActivePointerInsideObject(target);
    row->press_cancelled = false;
    SetCitRowPressed(row, false);
    if (click_cancelled) {
      return;
    }
    ShowCitTest(row->state, row->index);
  }
}

/**
 * @brief 处理测试通过按钮点击事件
 * @param event LVGL 事件
 */
void TestPassButtonEventCallback(lv_event_t* event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
    return;
  }

  auto* state = static_cast<CitViewState*>(lv_event_get_user_data(event));
  if (state == nullptr || state->row_count == 0 ||
      state->current_test_index >= state->row_count) {
    return;
  }

  state->test_statuses[state->current_test_index] = app::CitTestStatus::kReady;

  const size_t next_index = state->current_test_index + 1;
  if (next_index < state->row_count) {
    state->pending_test_index = next_index;
    ShowCitList(state);
    return;
  }

  ShowCitList(state);
}

/**
 * @brief 将当前测试标记为失败并返回列表
 * @param state CIT 页面状态
 */
void FailCurrentTestAndShowList(CitViewState* state) {
  if (state == nullptr || state->current_test_index >= state->row_count) {
    return;
  }

  state->test_statuses[state->current_test_index] = app::CitTestStatus::kFailed;
  RefreshCitRows(state);
  ShowCitList(state);
}

/**
 * @brief 处理测试失败按钮点击事件
 * @param event LVGL 事件
 */
void TestFailButtonEventCallback(lv_event_t* event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
    return;
  }

  auto* state = static_cast<CitViewState*>(lv_event_get_user_data(event));
  FailCurrentTestAndShowList(state);
}

/**
 * @brief 处理测试页面的边缘返回事件
 * @param event LVGL 事件
 */
void TestPageEdgeBackEventCallback(lv_event_t* event) {
  auto* state = static_cast<CitViewState*>(lv_event_get_user_data(event));
  if (state == nullptr || state->test_page == nullptr ||
      !HandleEdgeBackSwipeEvent(
          event, state->width, &state->test_edge_back_swipe)) {
    return;
  }

  if (state->root != nullptr) {
    SuppressNextLauncherGesture(state->root);
  }
  lv_event_stop_bubbling(event);
  lv_event_stop_processing(event);
  ShowCitList(state);
}

/**
 * @brief 读取当前触摸点在轨迹绘制区域内的坐标
 * @param state CIT 页面状态
 * @param local_point 局部坐标输出地址
 * @return 成功返回 true，否则返回 false
 */
bool ReadTouchTracePoint(CitViewState* state, lv_point_t* local_point) {
  if (state == nullptr || state->touch_trace_surface == nullptr ||
      local_point == nullptr) {
    return false;
  }

  lv_indev_t* indev = lv_indev_active();
  if (indev == nullptr) {
    return false;
  }

  lv_point_t screen_point = {};
  lv_indev_get_point(indev, &screen_point);

  lv_area_t surface_area = {};
  lv_obj_get_coords(state->touch_trace_surface, &surface_area);
  local_point->x = screen_point.x - surface_area.x1;
  local_point->y = screen_point.y - surface_area.y1;

  const int32_t surface_width = lv_obj_get_width(state->touch_trace_surface);
  const int32_t surface_height = lv_obj_get_height(state->touch_trace_surface);
  if (surface_width <= 0 || surface_height <= 0) {
    return false;
  }

  const int32_t max_x = surface_width - 1;
  const int32_t max_y = surface_height - 1;
  if (local_point->x < 0) {
    local_point->x = 0;
  } else if (local_point->x > max_x) {
    local_point->x = max_x;
  }
  if (local_point->y < 0) {
    local_point->y = 0;
  } else if (local_point->y > max_y) {
    local_point->y = max_y;
  }
  return true;
}

/**
 * @brief 按当前轨迹点刷新红色触摸轨迹线
 * @param state CIT 页面状态
 */
void RefreshTouchTraceLine(CitViewState* state) {
  if (state == nullptr || state->touch_trace_line == nullptr) {
    return;
  }

  if (state->touch_trace_point_count < 2) {
    lv_obj_add_flag(state->touch_trace_line, LV_OBJ_FLAG_HIDDEN);
    return;
  }

  lv_obj_remove_flag(state->touch_trace_line, LV_OBJ_FLAG_HIDDEN);
  lv_line_set_points(state->touch_trace_line, state->touch_trace_points.data(),
      static_cast<uint32_t>(state->touch_trace_point_count));
}

/**
 * @brief 清空触摸轨迹点
 * @param state CIT 页面状态
 */
void ClearTouchTrace(CitViewState* state) {
  if (state == nullptr) {
    return;
  }

  state->touch_trace_point_count = 0;
  RefreshTouchTraceLine(state);
}

/**
 * @brief 向触摸轨迹追加一个坐标点
 * @param state CIT 页面状态
 * @param point 触摸点
 */
void AppendTouchTracePoint(CitViewState* state, const lv_point_t& point) {
  if (state == nullptr) {
    return;
  }

  if (state->touch_trace_point_count > 0) {
    const lv_point_precise_t& last =
        state->touch_trace_points[state->touch_trace_point_count - 1];
    if (static_cast<int32_t>(last.x) == point.x &&
        static_cast<int32_t>(last.y) == point.y) {
      return;
    }
  }

  if (state->touch_trace_point_count >= state->touch_trace_points.size()) {
    std::memmove(state->touch_trace_points.data(),
        state->touch_trace_points.data() + 1,
        (state->touch_trace_points.size() - 1) * sizeof(lv_point_precise_t));
    state->touch_trace_point_count = state->touch_trace_points.size() - 1;
  }

  lv_point_precise_t& next =
      state->touch_trace_points[state->touch_trace_point_count++];
  next.x = point.x;
  next.y = point.y;
  RefreshTouchTraceLine(state);
}

/**
 * @brief 处理触摸轨迹绘制相关事件
 * @param event LVGL 事件
 */
void TouchTraceEventCallback(lv_event_t* event) {
  const lv_event_code_t code = lv_event_get_code(event);
  if (code != LV_EVENT_PRESSED && code != LV_EVENT_PRESSING &&
      code != LV_EVENT_RELEASED && code != LV_EVENT_PRESS_LOST) {
    return;
  }

  auto* state = static_cast<CitViewState*>(lv_event_get_user_data(event));
  if (state == nullptr || state->touch_trace_surface == nullptr) {
    return;
  }

  if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
    RefreshTouchTestData(state);
    return;
  }

  lv_point_t point = {};
  if (!ReadTouchTracePoint(state, &point)) {
    return;
  }

  if (code == LV_EVENT_PRESSED) {
    ClearTouchTrace(state);
  }

  state->touch_was_seen = true;
  AppendTouchTracePoint(state, point);
  RefreshTouchTestData(state);
}

/**
 * @brief 给对象添加触摸轨迹事件回调
 * @param object LVGL 对象
 * @param state CIT 页面状态
 */
void AddTouchTraceEventCallbacks(lv_obj_t* object, CitViewState* state) {
  if (object == nullptr || state == nullptr ||
      state->touch_trace_surface == nullptr) {
    return;
  }

  lv_obj_add_flag(object, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(object, TouchTraceEventCallback, LV_EVENT_PRESSED, state);
  lv_obj_add_event_cb(
      object, TouchTraceEventCallback, LV_EVENT_PRESSING, state);
  lv_obj_add_event_cb(
      object, TouchTraceEventCallback, LV_EVENT_RELEASED, state);
  lv_obj_add_event_cb(
      object, TouchTraceEventCallback, LV_EVENT_PRESS_LOST, state);
}

/**
 * @brief 更新全屏色彩测试浮层颜色
 * @param state CIT 页面状态
 */
void UpdateScreenColorOverlayColor(CitViewState* state) {
  if (state == nullptr || state->screen_color_overlay == nullptr ||
      state->screen_color_index >= kScreenColorTestColors.size()) {
    return;
  }

  lv_obj_set_style_bg_color(state->screen_color_overlay,
      lv_color_hex(kScreenColorTestColors[state->screen_color_index]),
      LV_PART_MAIN);
}

/**
 * @brief 显示全屏色彩测试浮层
 * @param state CIT 页面状态
 * @return 成功返回 true，否则返回 false
 */
bool ShowScreenColorOverlay(CitViewState* state) {
  if (state == nullptr || state->test_page == nullptr) {
    return false;
  }

  if (state->screen_color_overlay == nullptr) {
    lv_obj_t* overlay = lv_obj_create(state->test_page);
    if (overlay == nullptr) {
      return false;
    }

    state->screen_color_overlay = overlay;
    lv_obj_set_size(overlay, LV_PCT(100), LV_PCT(100));
    lv_obj_set_pos(overlay, 0, 0);
    lv_obj_add_flag(overlay, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(overlay, LV_OBJ_FLAG_FLOATING);
    lv_obj_remove_flag(overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(overlay, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(overlay, 0, LV_PART_MAIN);
    lv_obj_set_style_border_width(overlay, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(overlay, 0, LV_PART_MAIN);
    lv_obj_add_event_cb(
        overlay, ScreenColorOverlayEventCallback, LV_EVENT_CLICKED, state);
    AddEdgeBackSwipeEvents(overlay, TestPageEdgeBackEventCallback, state);
  }

  state->screen_color_index = 0;
  UpdateScreenColorOverlayColor(state);
  lv_obj_remove_flag(state->screen_color_overlay, LV_OBJ_FLAG_HIDDEN);
  lv_obj_move_to_index(state->screen_color_overlay, -1);
  return true;
}

/**
 * @brief 处理全屏色彩测试浮层点击事件
 * @param event LVGL 事件
 */
void ScreenColorOverlayEventCallback(lv_event_t* event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
    return;
  }

  auto* state = static_cast<CitViewState*>(lv_event_get_user_data(event));
  if (state == nullptr || state->screen_color_overlay == nullptr) {
    return;
  }

  lv_event_stop_bubbling(event);
  if (state->screen_color_index + 1 >= kScreenColorTestColors.size()) {
    lv_obj_add_flag(state->screen_color_overlay, LV_OBJ_FLAG_HIDDEN);
    return;
  }

  ++state->screen_color_index;
  UpdateScreenColorOverlayColor(state);
}

/**
 * @brief 处理屏幕颜色测试按钮点击事件
 * @param event LVGL 事件
 */
void ScreenColorStartButtonEventCallback(lv_event_t* event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
    return;
  }

  auto* state = static_cast<CitViewState*>(lv_event_get_user_data(event));
  if (state == nullptr || state->test_page == nullptr) {
    return;
  }

  ShowScreenColorOverlay(state);
}

/**
 * @brief 处理普通开始按钮点击事件
 * @param event LVGL 事件
 */
void GenericStartButtonEventCallback(lv_event_t* event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
    return;
  }

  auto* state = static_cast<CitViewState*>(lv_event_get_user_data(event));
  if (state == nullptr || state->test_data_label == nullptr ||
      state->current_test_index >= state->row_count) {
    return;
  }

  const app::CitTestEntry* entry = state->rows[state->current_test_index].entry;
  if (entry == nullptr) {
    return;
  }

  if (IsEntryId(*entry, "vibration")) {
    lv_label_set_text(
        state->test_data_label, "vibration data:\nplaying RAM waveforms...");
    lv_refr_now(nullptr);

    uint8_t waveform_count = 0;
    const bool played = state->haptic != nullptr &&
                        state->haptic->PlayHapticWaveform(&waveform_count);
    char text[160] = {};
    std::snprintf(text, sizeof(text),
        "vibration data:\n"
        "status: %s\n"
        "played waveforms: %u\n"
        "gain: 255\n"
        "loop count: 15",
        played ? "played all RAM waveforms" : "playback failed",
        static_cast<unsigned int>(waveform_count));
    lv_label_set_text(state->test_data_label, text);
    return;
  }
  if (IsEntryId(*entry, "speaker")) {
    if (state->audio != nullptr && state->audio->StartSpeakerTone()) {
      RefreshSpeakerTestData(state);
      return;
    }

    hal::SpeakerStatus status;
    const bool status_read =
        state->audio != nullptr && state->audio->ReadSpeakerToneStatus(&status);
    lv_label_set_text(state->test_data_label,
        status_read && status.running ? "speaker data:\nstatus: already playing"
                                      : "speaker data:\nstatus: start failed");
  }
}

/**
 * @brief 创建测试页面底部的操作按钮
 * @param parent 父对象
 * @param text 显示文本
 * @param color 背景颜色
 * @param text_color 文本颜色
 * @param align 对齐方式
 * @param x X 坐标
 * @param callback 事件回调
 * @param state CIT 页面状态
 * @return 创建成功返回对象指针，否则返回 nullptr
 */
lv_obj_t* CreateTestActionButton(lv_obj_t* parent, const char* text,
    uint32_t color, uint32_t text_color, lv_align_t align, int x,
    lv_event_cb_t callback, CitViewState* state) {
  lv_obj_t* button = lv_button_create(parent);
  if (button == nullptr) {
    return nullptr;
  }

  lv_obj_set_size(button, kTestButtonWidth, kTestButtonHeight);
  lv_obj_align(button, align, x, 0);
  lv_obj_set_style_radius(button, 10, LV_PART_MAIN);
  lv_obj_set_style_bg_color(button, lv_color_hex(color), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(button, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_shadow_width(button, 0, LV_PART_MAIN);
  lv_obj_set_style_border_width(button, 0, LV_PART_MAIN);
  if (!AddPressCancelOnLeave(button)) {
    lv_obj_delete(button);
    return nullptr;
  }
  lv_obj_add_event_cb(button, callback, LV_EVENT_CLICKED, state);
  AddEdgeBackSwipeEvents(button, TestPageEdgeBackEventCallback, state);
  AddTouchTraceEventCallbacks(button, state);

  lv_obj_t* label =
      CreateLabel(button, text, lv_color_hex(text_color), Font28());
  if (label == nullptr) {
    lv_obj_delete(button);
    return nullptr;
  }
  lv_obj_center(label);
  return button;
}

/**
 * @brief 创建测试页面中间的开始按钮
 * @param parent 父对象
 * @param text 显示文本
 * @param callback 事件回调
 * @param state CIT 页面状态
 * @return 创建成功返回对象指针，否则返回 nullptr
 */
lv_obj_t* CreateCenterButton(lv_obj_t* parent, const char* text,
    lv_event_cb_t callback, CitViewState* state) {
  lv_obj_t* button = lv_button_create(parent);
  if (button == nullptr) {
    return nullptr;
  }

  lv_obj_set_size(button, kTestStartButtonWidth, kTestStartButtonHeight);
  lv_obj_center(button);
  lv_obj_set_style_radius(button, 12, LV_PART_MAIN);
  lv_obj_set_style_bg_color(
      button, lv_color_hex(kStartButtonColor), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(button, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_shadow_width(button, 0, LV_PART_MAIN);
  lv_obj_set_style_border_width(button, 0, LV_PART_MAIN);
  if (!AddPressCancelOnLeave(button)) {
    lv_obj_delete(button);
    return nullptr;
  }
  if (callback != nullptr) {
    lv_obj_add_event_cb(button, callback, LV_EVENT_CLICKED, state);
  }
  AddEdgeBackSwipeEvents(button, TestPageEdgeBackEventCallback, state);

  lv_obj_t* label = CreateLabel(button, text, lv_color_hex(0xFFFFFF), Font28());
  if (label == nullptr) {
    lv_obj_delete(button);
    return nullptr;
  }
  lv_obj_center(label);
  return button;
}

/**
 * @brief 创建测试页面底部的通过和失败按钮栏
 * @param parent 父对象
 * @param state CIT 页面状态
 * @return 创建成功返回对象指针，否则返回 nullptr
 */
lv_obj_t* CreateTestButtonBar(lv_obj_t* parent, CitViewState* state) {
  lv_obj_t* button_bar = lv_obj_create(parent);
  if (button_bar == nullptr) {
    return nullptr;
  }

  lv_obj_remove_flag(button_bar, LV_OBJ_FLAG_SCROLLABLE);
  AddEdgeBackSwipeEvents(button_bar, TestPageEdgeBackEventCallback, state);
  AddTouchTraceEventCallbacks(button_bar, state);
  lv_obj_set_size(button_bar, LV_PCT(100), kTestButtonBarHeight);
  lv_obj_align(button_bar, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_obj_set_style_bg_color(
      button_bar, lv_color_hex(kListBackgroundColor), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(button_bar, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_radius(button_bar, 0, LV_PART_MAIN);
  lv_obj_set_style_border_width(button_bar, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(button_bar, 0, LV_PART_MAIN);

  if (CreateTestActionButton(button_bar, "FAIL", kFailButtonColor,
          kFailButtonTextColor, LV_ALIGN_CENTER, -kTestButtonCenterOffset,
          TestFailButtonEventCallback, state) == nullptr ||
      CreateTestActionButton(button_bar, "PASS", kPassButtonColor,
          kPassButtonTextColor, LV_ALIGN_CENTER, kTestButtonCenterOffset,
          TestPassButtonEventCallback, state) == nullptr) {
    lv_obj_delete(button_bar);
    return nullptr;
  }

  return button_bar;
}

/**
 * @brief 获取测试项的默认提示文案
 * @param entry 测试项
 * @return 字符串指针
 */
const char* GetTestHint(const app::CitTestEntry& entry) {
  if (IsEntryId(entry, "version")) {
    return "Check firmware and device version information.";
  }
  if (IsEntryId(entry, "touch")) {
    return "Touch the screen and confirm the touch point is detected.";
  }
  if (IsEntryId(entry, "screen")) {
    return "Check the screen color and visible area.";
  }
  if (IsEntryId(entry, "vibration")) {
    return "Tap START VIB to play all RAM waveforms at max strength.";
  }
  if (IsEntryId(entry, "speaker")) {
    return "Tap START PLAY to play the built-in notification audio.";
  }
  if (IsEntryId(entry, "microphone")) {
    return "Confirm the microphone input.";
  }
  if (IsEntryId(entry, "imu")) {
    return "Move the device and confirm motion data is available.";
  }
  if (IsEntryId(entry, "bmu")) {
    return "Confirm BMU diagnostics.";
  }
  if (IsEntryId(entry, "gps")) {
    return "Confirm GPS test requirements.";
  }
  if (IsEntryId(entry, "ethernet")) {
    return "Plug or unplug the Ethernet cable and wait for DHCP IP.";
  }
  if (IsEntryId(entry, "rtc")) {
    return "Confirm RTC time keeping.";
  }
  if (IsEntryId(entry, "wifi")) {
    return "Confirm WIFI time acquisition.";
  }
  return "Run the hardware test and choose PASS or FAIL.";
}

/**
 * @brief 创建测试页面的数据文本标签
 * @param parent 父对象
 * @param text 显示文本
 * @return 创建成功返回对象指针，否则返回 nullptr
 */
lv_obj_t* CreateDataLabel(lv_obj_t* parent, const char* text) {
  lv_obj_t* label = CreateLabel(parent, text, lv_color_hex(0x202020), Font28());
  if (label == nullptr) {
    return nullptr;
  }

  lv_obj_set_width(label, LV_PCT(100));
  lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
  lv_obj_align(label, LV_ALIGN_TOP_LEFT, 0, 0);
  return label;
}

/**
 * @brief 创建多点触摸位置标记
 * @param state CIT 页面状态
 * @return 成功返回 true，否则返回 false
 */
bool CreateTouchPointMarkers(CitViewState* state) {
  if (state == nullptr || state->touch_trace_surface == nullptr) {
    return false;
  }

  for (size_t i = 0; i < state->touch_point_markers.size(); ++i) {
    lv_obj_t* marker = lv_obj_create(state->touch_trace_surface);
    if (marker == nullptr) {
      return false;
    }

    state->touch_point_markers[i] = marker;
    lv_obj_add_flag(marker, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(marker, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(marker, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(marker, kTouchMarkerSize, kTouchMarkerSize);
    lv_obj_set_style_radius(marker, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_bg_color(
        marker, lv_color_hex(kPassButtonColor), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(marker, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(marker, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_set_style_border_width(marker, 2, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(marker, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(marker, 0, LV_PART_MAIN);

    char marker_text[4] = {};
    std::snprintf(
        marker_text, sizeof(marker_text), "%u", static_cast<unsigned>(i + 1));
    lv_obj_t* label =
        CreateLabel(marker, marker_text, lv_color_hex(0xFFFFFF), Font28());
    if (label == nullptr) {
      return false;
    }
    lv_obj_center(label);
  }
  return true;
}

/**
 * @brief 获取当前配置的芯片型号
 * @return 字符串指针
 */
const char* ConfiguredChipModel() {
#if defined(CONFIG_IDF_TARGET)
  return CONFIG_IDF_TARGET;
#else
  return "unknown";
#endif
}

/**
 * @brief 获取当前配置的目标架构
 * @return 字符串指针
 */
const char* ConfiguredTargetArch() {
#if defined(CONFIG_IDF_TARGET_ARCH)
  return CONFIG_IDF_TARGET_ARCH;
#else
  return "unknown";
#endif
}

/**
 * @brief 获取当前配置的屏幕类型
 * @return 字符串指针
 */
const char* ConfiguredScreenType(hal::ScreenProvider* screen) {
  return screen == nullptr ? "unknown" : screen->ScreenType();
}

/**
 * @brief 获取当前配置的摄像头类型
 * @return 字符串指针
 */
const char* ConfiguredCameraType() {
#if defined(CONFIG_CAMERA_TYPE_SC2336)
  return "sc2336";
#elif defined(CONFIG_CAMERA_TYPE_OV2710)
  return "ov2710";
#elif defined(CONFIG_CAMERA_TYPE_OV5645)
  return "ov5645";
#else
  return "unknown";
#endif
}

/**
 * @brief 获取当前配置的摄像头像素格式
 * @return 字符串指针
 */
const char* ConfiguredCameraPixelFormat() {
#if defined(CONFIG_CAMERA_PIXEL_FORMAT_RGB565)
  return "rgb565";
#elif defined(CONFIG_CAMERA_PIXEL_FORMAT_RGB888)
  return "rgb888";
#else
  return "unknown";
#endif
}

/**
 * @brief 根据像素位数获取屏幕像素格式描述
 * @param bits_per_pixel 像素位数
 * @return 字符串指针
 */
const char* ScreenPixelFormat(int bits_per_pixel) {
#if defined(CONFIG_SCREEN_PIXEL_FORMAT_RGB565)
  return "rgb565";
#elif defined(CONFIG_SCREEN_PIXEL_FORMAT_RGB888)
  return "rgb888";
#else
  switch (bits_per_pixel) {
    case 16:
      return "rgb565";
    case 24:
    case 32:
      return "rgb888";
    default:
      return "unknown";
  }
#endif
}

/**
 * @brief 返回有效字符串
 * @param text 显示文本
 * @return 字符串指针
 */
const char* KnownString(const char* text) {
  return (text == nullptr || text[0] == '\0') ? "unknown" : text;
}

/**
 * @brief 格式化本机 MAC 地址
 * @param buffer 输出缓冲区
 * @param size 输出缓冲区大小
 */
void FormatMacAddress(char* buffer, size_t size) {
  if (buffer == nullptr || size == 0) {
    return;
  }

  uint8_t mac[6] = {};
  if (esp_efuse_mac_get_default(mac) != ESP_OK) {
    std::snprintf(buffer, size, "unknown");
    return;
  }

  std::snprintf(buffer, size, "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1],
      mac[2], mac[3], mac[4], mac[5]);
}

/**
 * @brief 读取当前运行固件镜像大小
 * @param image_size 镜像大小输出地址
 * @return 读取成功返回 true，否则返回 false
 */
bool ReadRunningImageSize(size_t* image_size) {
  if (image_size == nullptr) {
    return false;
  }

  const esp_partition_t* running_partition = esp_ota_get_running_partition();
  if (running_partition == nullptr) {
    return false;
  }

  esp_partition_pos_t partition = {};
  partition.offset = running_partition->address;
  partition.size = running_partition->size;

  esp_image_metadata_t metadata = {};
  if (esp_image_get_metadata(&partition, &metadata) != ESP_OK ||
      metadata.image_len == 0) {
    return false;
  }

  *image_size = metadata.image_len;
  return true;
}

/**
 * @brief 格式化 flash 剩余容量和总容量
 * @param buffer 文本缓冲区
 * @param size 文本缓冲区大小
 */
void FormatFlashFreeTotal(char* buffer, size_t size) {
  if (buffer == nullptr || size == 0) {
    return;
  }

  uint32_t flash_size = 0;
  if (esp_flash_get_size(nullptr, &flash_size) != ESP_OK || flash_size == 0) {
    std::snprintf(buffer, size, "unknown / unknown");
    return;
  }

  size_t image_size = 0;
  if (!ReadRunningImageSize(&image_size)) {
    std::snprintf(buffer, size, "unknown / %lu bytes (%lu MB)",
        static_cast<unsigned long>(flash_size),
        static_cast<unsigned long>(flash_size / 1024 / 1024));
    return;
  }

  const uint64_t free_size =
      flash_size > image_size ? flash_size - image_size : 0;
  std::snprintf(buffer, size, "%llu / %lu bytes (%llu / %lu MB)",
      static_cast<unsigned long long>(free_size),
      static_cast<unsigned long>(flash_size),
      static_cast<unsigned long long>(free_size / 1024 / 1024),
      static_cast<unsigned long>(flash_size / 1024 / 1024));
}

/**
 * @brief 添加版本信息测试内容
 * @param content 内容容器
 * @param state CIT 页面状态
 * @return 成功返回 true，否则返回 false
 */
bool AddVersionContent(lv_obj_t* content, CitViewState* state) {
  esp_chip_info_t chip_info = {};
  esp_chip_info(&chip_info);

  const esp_app_desc_t* app_description = esp_app_get_description();

  char mac_address[18] = {};
  FormatMacAddress(mac_address, sizeof(mac_address));

  char flash_size_text[96] = {};
  FormatFlashFreeTotal(flash_size_text, sizeof(flash_size_text));

  const int screen_width = state->screen->ScreenWidth();
  const int screen_height = state->screen->ScreenHeight();
  const int screen_bpp = state->screen->ScreenBitsPerPixel();
  const char* app_project_name = KnownString(
      app_description == nullptr ? nullptr : app_description->project_name);
  const char* app_version = KnownString(
      app_description == nullptr ? nullptr : app_description->version);
  const char* app_build_date =
      KnownString(app_description == nullptr ? nullptr : app_description->date);
  const char* app_build_time =
      KnownString(app_description == nullptr ? nullptr : app_description->time);

  char text[2048] = {};
  std::snprintf(text, sizeof(text),
      "[Chip]\n"
      "model: %s\n"
      "efuse mac:\n"
      "     %s\n"
      "revision: v%d.%d\n"
      "cores: %d\n"
      "flash size free / total:\n"
      "     %s\n"
      "flash features: %s\n"
      "\n"
      "[Memory]\n"
      "free heap:\n"
      "     %lu bytes\n"
      "internal heap free / total:\n"
      "     %lu / %lu bytes\n"
      "psram free / total:\n"
      "     %lu / %lu bytes\n"
      "\n"
      "[Software]\n"
      "company: lilygo\n"
      "device name: %s\n"
      "software name: %s\n"
      "software version: %s\n"
      "software build date:\n"
      "     %s %s\n"
      "esp-idf version:\n"
      "     %s\n"
      "target arch: %s\n"
      "\n"
      "[Screen]\n"
      "screen type: %s\n"
      "screen size: %d x %d\n"
      "screen pixel format: %s\n"
      "\n"
      "[Camera]\n"
      "camera type: %s\n"
      "camera pixel format: %s\n"
      "\n"
      "[LVGL]\n"
      "lvgl version: %d.%d.%d%s",
      ConfiguredChipModel(), mac_address, chip_info.revision / 100,
      chip_info.revision % 100, chip_info.cores, flash_size_text,
      (chip_info.features & CHIP_FEATURE_EMB_FLASH) ? "embedded" : "external",
      static_cast<unsigned long>(esp_get_free_heap_size()),
      static_cast<unsigned long>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)),
      static_cast<unsigned long>(heap_caps_get_total_size(MALLOC_CAP_INTERNAL)),
      static_cast<unsigned long>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)),
      static_cast<unsigned long>(heap_caps_get_total_size(MALLOC_CAP_SPIRAM)),
      app::ConfiguredDeviceName(), app_project_name, app_version, app_build_date,
      app_build_time, esp_get_idf_version(), ConfiguredTargetArch(),
      ConfiguredScreenType(state->screen), screen_width, screen_height,
      ScreenPixelFormat(screen_bpp), ConfiguredCameraType(),
      ConfiguredCameraPixelFormat(), LVGL_VERSION_MAJOR, LVGL_VERSION_MINOR,
      LVGL_VERSION_PATCH, LVGL_VERSION_INFO);

  lv_obj_add_flag(content, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_scroll_dir(content, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(content, LV_SCROLLBAR_MODE_ACTIVE);
  return CreateDataLabel(content, text) != nullptr;
}

/**
 * @brief 添加触摸测试内容
 * @param content 内容容器
 * @param state CIT 页面状态
 * @return 成功返回 true，否则返回 false
 */
bool AddTouchContent(lv_obj_t* content, CitViewState* state) {
  if (state == nullptr || state->test_page == nullptr) {
    return false;
  }

  state->touch_trace_point_count = 0;
  state->touch_trace_surface = nullptr;
  state->touch_trace_line = nullptr;
  state->touch_point_markers.fill(nullptr);

  lv_obj_t* trace_surface = lv_obj_create(state->test_page);
  if (trace_surface == nullptr) {
    return false;
  }
  state->touch_trace_surface = trace_surface;
  lv_obj_set_size(trace_surface, LV_PCT(100), LV_PCT(100));
  lv_obj_set_pos(trace_surface, 0, 0);
  lv_obj_remove_flag(trace_surface, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_remove_flag(trace_surface, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(trace_surface, LV_OBJ_FLAG_FLOATING);
  lv_obj_set_style_bg_opa(trace_surface, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_set_style_border_width(trace_surface, 0, LV_PART_MAIN);
  lv_obj_set_style_radius(trace_surface, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(trace_surface, 0, LV_PART_MAIN);
  AddTouchTraceEventCallbacks(state->test_page, state);
  AddTouchTraceEventCallbacks(content, state);

  state->touch_trace_line = lv_line_create(trace_surface);
  if (state->touch_trace_line == nullptr) {
    return false;
  }
  lv_obj_add_flag(state->touch_trace_line, LV_OBJ_FLAG_HIDDEN);
  lv_obj_remove_flag(state->touch_trace_line, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_line_color(
      state->touch_trace_line, lv_color_hex(kFailedColor), LV_PART_MAIN);
  lv_obj_set_style_line_width(
      state->touch_trace_line, kTouchTraceLineWidth, LV_PART_MAIN);
  lv_obj_set_style_line_rounded(state->touch_trace_line, true, LV_PART_MAIN);

  if (!CreateTouchPointMarkers(state)) {
    return false;
  }

  state->test_data_label = CreateDataLabel(content, "");
  if (state->test_data_label == nullptr) {
    return false;
  }
  RefreshTouchTestData(state);
  return true;
}

/**
 * @brief 添加屏幕颜色测试内容
 * @param content 内容容器
 * @param state CIT 页面状态
 * @return 成功返回 true，否则返回 false
 */
bool AddScreenColorContent(lv_obj_t* content, CitViewState* state) {
  state->screen_color_index = 0;
  lv_obj_t* hint = CreateDataLabel(content,
      "Tap START COLOR for full-screen red, green, "
      "blue, white, and black test.\nTap screen to switch colors.");
  if (hint == nullptr) {
    return false;
  }
  return CreateCenterButton(content, "START COLOR",
             ScreenColorStartButtonEventCallback, state) != nullptr;
}

/**
 * @brief 添加带中间开始按钮的测试内容
 * @param content 内容容器
 * @param state CIT 页面状态
 * @param data_text 数据文本
 * @param button_text 按钮文本
 * @return 成功返回 true，否则返回 false
 */
bool AddStartButtonContent(lv_obj_t* content, CitViewState* state,
    const char* data_text, const char* button_text) {
  state->test_data_label = CreateDataLabel(content, data_text);
  if (state->test_data_label == nullptr) {
    return false;
  }
  return CreateCenterButton(content, button_text,
             GenericStartButtonEventCallback, state) != nullptr;
}

/**
 * @brief 处理麦克风 ADC 到 DAC 直通开关事件
 * @param event LVGL 事件
 */
void MicrophoneAdcToDacSwitchEventCallback(lv_event_t* event) {
  if (lv_event_get_code(event) != LV_EVENT_VALUE_CHANGED) {
    return;
  }

  auto* state = static_cast<CitViewState*>(lv_event_get_user_data(event));
  lv_obj_t* switch_object = lv_event_get_target_obj(event);
  if (state == nullptr || switch_object == nullptr) {
    return;
  }

  const bool enable = lv_obj_has_state(switch_object, LV_STATE_CHECKED);
  if (state->audio == nullptr || !state->audio->SetAudioAdcToDac(enable)) {
    lv_obj_remove_state(switch_object, LV_STATE_CHECKED);
  }
  RefreshMicrophoneTestData(state);
}

/**
 * @brief 添加麦克风测试内容
 * @param content 内容容器
 * @param state CIT 页面状态
 * @return 成功返回 true，否则返回 false
 */
bool AddMicrophoneContent(lv_obj_t* content, CitViewState* state) {
  if (state == nullptr) {
    return false;
  }

  lv_obj_t* scale = lv_scale_create(content);
  if (scale == nullptr) {
    return false;
  }
  state->microphone_scale = scale;
  lv_obj_set_size(scale, 360, 360);
  lv_obj_align(scale, LV_ALIGN_TOP_MID, 0, 36);
  lv_scale_set_mode(scale, LV_SCALE_MODE_ROUND_INNER);
  lv_obj_set_style_bg_opa(scale, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_bg_color(scale, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
  lv_obj_set_style_radius(scale, LV_RADIUS_CIRCLE, LV_PART_MAIN);
  lv_obj_set_style_clip_corner(scale, true, LV_PART_MAIN);
  lv_scale_set_label_show(scale, true);
  lv_scale_set_total_tick_count(scale, 51);
  lv_scale_set_major_tick_every(scale, 5);
  lv_obj_set_style_length(scale, 5, LV_PART_ITEMS);
  lv_obj_set_style_length(scale, 10, LV_PART_INDICATOR);
  lv_scale_set_range(scale, 0, 100);
  lv_scale_set_angle_range(scale, 270);
  lv_scale_set_rotation(scale, 135);

  lv_obj_t* needle = lv_line_create(scale);
  if (needle == nullptr) {
    return false;
  }
  state->microphone_needle = needle;
  lv_obj_set_style_line_width(needle, 3, LV_PART_MAIN);
  lv_obj_set_style_line_rounded(needle, true, LV_PART_MAIN);
  lv_scale_set_line_needle_value(scale, needle, 150, 0);

  lv_obj_t* label = CreateLabel(content, "microphone data:\nlevel: waiting",
      lv_color_hex(0x202020), Font28());
  if (label == nullptr) {
    return false;
  }
  state->test_data_label = label;
  lv_obj_set_width(label, LV_PCT(100));
  lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
  lv_obj_align(label, LV_ALIGN_TOP_MID, 0, 420);

  lv_obj_t* switch_label =
      CreateLabel(content, "adc -> dac", lv_color_hex(0x202020), Font28());
  if (switch_label == nullptr) {
    return false;
  }
  lv_obj_align(switch_label, LV_ALIGN_TOP_MID, 0, 560);

  lv_obj_t* switch_object = lv_switch_create(content);
  if (switch_object == nullptr) {
    return false;
  }
  state->microphone_adc_to_dac_switch = switch_object;
  lv_obj_set_size(switch_object, 90, 50);
  lv_obj_align(switch_object, LV_ALIGN_TOP_MID, 0, 610);
  lv_obj_add_event_cb(switch_object, MicrophoneAdcToDacSwitchEventCallback,
      LV_EVENT_VALUE_CHANGED, state);

  if (state->audio == nullptr || !state->audio->StartMicrophone()) {
    lv_label_set_text(
        state->test_data_label, "microphone data:\nstatus: start failed");
    return true;
  }
  RefreshMicrophoneTestData(state);
  return true;
}

/**
 * @brief 添加依赖诊断数据的测试内容
 * @param content 内容容器
 * @param state CIT 页面状态
 * @param entry 测试项
 * @return 成功返回 true，否则返回 false
 */
bool AddDiagnosticsContent(
    lv_obj_t* content, CitViewState* state, const app::CitTestEntry& entry) {
  const char* initial_text = "diagnostics data:";
  if (IsEntryId(entry, "imu")) {
    initial_text = "imu data:";
  } else if (IsEntryId(entry, "bmu")) {
    initial_text = "BMU data:";
    lv_obj_add_flag(content, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(content, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(content, LV_SCROLLBAR_MODE_ACTIVE);
  }

  state->test_data_label = CreateDataLabel(content, initial_text);
  if (state->test_data_label == nullptr) {
    return false;
  }
  RefreshActiveTestData(state);
  return true;
}

/**
 * @brief 添加 GPS 测试内容并启动 L76K 读取
 * @param content 内容容器
 * @param state CIT 页面状态
 * @return 成功返回 true，否则返回 false
 */
bool AddGpsContent(lv_obj_t* content, CitViewState* state) {
  if (state == nullptr) {
    return false;
  }

  lv_obj_add_flag(content, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_scroll_dir(content, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(content, LV_SCROLLBAR_MODE_ACTIVE);

  state->gps_elapsed_ms = 0;
  state->gps_read_elapsed_ms = 0;
  state->gps_update_interval_ms = 1000;
  state->gps_status = hal::GpsStatus();
  state->gps_status_valid = false;
  state->gps_positioned = false;
  state->test_data_label = CreateDataLabel(content, "GPS data:\nstatus: start");
  if (state->test_data_label == nullptr) {
    return false;
  }

  if (state->gps == nullptr || !state->gps->StartGps()) {
    lv_label_set_text(
        state->test_data_label, "GPS data:\nstatus: start failed");
    return true;
  }
  RefreshGpsTestData(state);
  return true;
}

/**
 * @brief 添加以太网测试内容并异步启动检测
 * @param content 内容容器
 * @param state CIT 页面状态
 * @return 成功返回 true，否则返回 false
 */
bool AddEthernetContent(lv_obj_t* content, CitViewState* state) {
  if (state == nullptr) {
    return false;
  }

  state->test_data_label =
      CreateDataLabel(content, "Ethernet data:\nstatus: starting");
  if (state->test_data_label == nullptr) {
    return false;
  }

  if (state->ethernet == nullptr || !state->ethernet->StartEthernet()) {
    lv_label_set_text(
        state->test_data_label, "Ethernet data:\nstatus: start failed");
    return true;
  }

  RefreshEthernetTestData(state);
  return true;
}

/**
 * @brief 添加 WiFi 获取时间测试内容并临时连接工厂热点
 * @param content 内容容器
 * @param state CIT 页面状态
 * @return 成功返回 true，否则返回 false
 */
bool AddWifiContent(lv_obj_t* content, CitViewState* state) {
  if (state == nullptr) {
    return false;
  }

  lv_obj_add_flag(content, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_scroll_dir(content, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(content, LV_SCROLLBAR_MODE_ACTIVE);

  state->test_data_label =
      CreateDataLabel(content, "WIFI time data:\nstatus: starting");
  if (state->test_data_label == nullptr) {
    return false;
  }

  if (state->wifi == nullptr || !state->wifi->StartWifiTimeTest()) {
    lv_label_set_text(
        state->test_data_label, "WIFI time data:\nstatus: start failed");
    return true;
  }

  RefreshWifiTestData(state);
  return true;
}

/**
 * @brief 添加 RTC 测试内容并刷新 PCF8563 时间数据
 * @param content 内容容器
 * @param state CIT 页面状态
 * @return 成功返回 true，否则返回 false
 */
bool AddRtcContent(lv_obj_t* content, CitViewState* state) {
  if (state == nullptr) {
    return false;
  }

  state->test_data_label =
      CreateDataLabel(content, "RTC data:\nstatus: starting");
  if (state->test_data_label == nullptr) {
    return false;
  }

  RefreshRtcTestData(state);
  return true;
}

/**
 * @brief 添加普通数据展示类测试内容
 * @param content 内容容器
 * @param entry 测试项
 * @return 成功返回 true，否则返回 false
 */
bool AddPlainDataContent(lv_obj_t* content, const app::CitTestEntry& entry) {
  return CreateDataLabel(content, GetTestHint(entry)) != nullptr;
}

/**
 * @brief 根据测试项类型填充测试页面内容
 * @param content 内容容器
 * @param state CIT 页面状态
 * @param entry 测试项
 * @return 成功返回 true，否则返回 false
 */
bool PopulateTestContent(
    lv_obj_t* content, CitViewState* state, const app::CitTestEntry& entry) {
  if (IsEntryId(entry, "version")) {
    return AddVersionContent(content, state);
  }
  if (IsEntryId(entry, "touch")) {
    return AddTouchContent(content, state);
  }
  if (IsEntryId(entry, "screen")) {
    return AddScreenColorContent(content, state);
  }
  if (IsEntryId(entry, "vibration")) {
    return AddStartButtonContent(
        content, state, "vibration data:", "START VIB");
  }
  if (IsEntryId(entry, "speaker")) {
    return AddStartButtonContent(content, state, "speaker data:", "START PLAY");
  }
  if (IsEntryId(entry, "microphone")) {
    return AddMicrophoneContent(content, state);
  }
  if (IsEntryId(entry, "gps")) {
    return AddGpsContent(content, state);
  }
  if (IsEntryId(entry, "ethernet")) {
    return AddEthernetContent(content, state);
  }
  if (IsEntryId(entry, "wifi")) {
    return AddWifiContent(content, state);
  }
  if (IsEntryId(entry, "rtc")) {
    return AddRtcContent(content, state);
  }
  if (IsEntryId(entry, "imu") || IsEntryId(entry, "bmu")) {
    return AddDiagnosticsContent(content, state, entry);
  }
  return AddPlainDataContent(content, entry);
}

/**
 * @brief 删除当前测试页面对象
 * @param state CIT 页面状态
 */
void DeleteTestPage(CitViewState* state) {
  if (state == nullptr || state->test_page == nullptr) {
    return;
  }

  StopActiveTestHardware(state);
  lv_anim_delete(state, SetMicrophoneNeedleValue);
  DeleteWindowTransition(state->test_page);
  lv_obj_delete(state->test_page);
  ClearTestPageState(state);
}

/**
 * @brief 显示指定索引的测试页面
 * @param state CIT 页面状态
 * @param index 测试项索引
 * @return 成功返回 true，否则返回 false
 */
bool ShowCitTest(CitViewState* state, size_t index) {
  if (state == nullptr || state->root == nullptr || index >= state->row_count) {
    return false;
  }

  SetCitRowsClickable(state, false);
  DeleteTestPage(state);
  state->test_content = nullptr;
  state->test_data_label = nullptr;
  state->current_test_index = index;
  const CitStatusRow& row = state->rows[index];
  if (row.entry == nullptr) {
    return false;
  }

  lv_obj_add_flag(state->root, kBlockLauncherGestureFlag);
  lv_obj_remove_flag(state->root, LV_OBJ_FLAG_GESTURE_BUBBLE);

  lv_obj_t* page = lv_obj_create(state->root);
  if (page == nullptr) {
    return false;
  }
  state->test_page = page;
  state->test_page_closing = false;
  state->test_edge_back_swipe = EdgeBackSwipeState();
  lv_obj_remove_flag(page, LV_OBJ_FLAG_SCROLLABLE);
  AddEdgeBackSwipeEvents(page, TestPageEdgeBackEventCallback, state);
  lv_obj_set_size(page, LV_PCT(100), LV_PCT(100));
  lv_obj_set_pos(page, state->width, 0);
  lv_obj_set_style_bg_color(
      page, lv_color_hex(kCitBackgroundColor), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(page, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_radius(page, 0, LV_PART_MAIN);
  lv_obj_set_style_border_width(page, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(page, 0, LV_PART_MAIN);

  lv_obj_t* title = CreateLabel(
      page, TestTitle(*row.entry), lv_color_hex(0xFFFFFF), Font48());
  if (title == nullptr) {
    DeleteTestPage(state);
    return false;
  }
  lv_obj_set_size(title, state->width - 2 * kTitleLeft, 70);
  lv_obj_align(title, LV_ALIGN_TOP_LEFT, kTitleLeft, kTitleTop);

  lv_obj_t* content = lv_obj_create(page);
  if (content == nullptr) {
    DeleteTestPage(state);
    return false;
  }
  state->test_content = content;
  lv_obj_remove_flag(content, LV_OBJ_FLAG_SCROLLABLE);
  AddEdgeBackSwipeEvents(content, TestPageEdgeBackEventCallback, state);
  lv_obj_set_size(
      content, LV_PCT(100), state->height - kListTop - kTestButtonBarHeight);
  lv_obj_set_style_bg_color(
      content, lv_color_hex(kListBackgroundColor), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(content, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_radius(content, 0, LV_PART_MAIN);
  lv_obj_set_style_border_width(content, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(content, 24, LV_PART_MAIN);
  lv_obj_align(content, LV_ALIGN_TOP_MID, 0, kListTop);

  if (!PopulateTestContent(content, state, *row.entry)) {
    DeleteTestPage(state);
    return false;
  }

  if (CreateTestButtonBar(page, state) == nullptr) {
    DeleteTestPage(state);
    return false;
  }

  if (state->touch_trace_surface != nullptr) {
    lv_obj_move_to_index(state->touch_trace_surface, -1);
  }

  EnableEdgeBackSwipeEventBubble(page);
  if (!StartSlideLeftWindowTransition(
          page, state->width, kPageSlideAnimationMs, state, nullptr)) {
    lv_obj_set_x(page, 0);
  }
  return true;
}

/**
 * @brief 显示 CIT 列表页面
 * @param state CIT 页面状态
 */
void ShowCitList(CitViewState* state) {
  if (state == nullptr) {
    return;
  }

  if (state->list_page != nullptr) {
    lv_obj_remove_flag(state->list_page, LV_OBJ_FLAG_HIDDEN);
  }
  if (state->test_page == nullptr) {
    RestoreCitListGestures(state);
    SetCitRowsClickable(state, true);
    return;
  }
  if (state->test_page_closing) {
    return;
  }

  SetCitRowsClickable(state, false);
  StopActiveTestHardware(state);
  state->test_page_closing = true;
  if (!StartSlideRightWindowTransition(state->test_page, state->width,
          kPageSlideAnimationMs, state, TestPageCloseCompletedCallback)) {
    FinishTestPageClose(state);
  }
}

/**
 * @brief 创建 CIT 列表中的单行测试项
 * @param parent 父对象
 * @param entry 测试项
 * @param state CIT 页面状态
 * @return 创建成功返回对象指针，否则返回 nullptr
 */
lv_obj_t* CreateStatusRow(
    lv_obj_t* parent, const app::CitTestEntry& entry, CitViewState* state) {
  if (state == nullptr || state->row_count >= state->rows.size()) {
    return nullptr;
  }

  lv_obj_t* row = lv_obj_create(parent);
  if (row == nullptr) {
    return nullptr;
  }

  lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_size(row, LV_PCT(100), kRowHeight);
  lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_set_style_border_width(row, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(row, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_left(row, 0, LV_PART_MAIN);

  lv_obj_t* pressed_background = lv_obj_create(row);
  if (pressed_background == nullptr) {
    lv_obj_delete(row);
    return nullptr;
  }
  lv_obj_remove_flag(pressed_background, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(pressed_background, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_flag(pressed_background, LV_OBJ_FLAG_HIDDEN);
  lv_obj_set_size(pressed_background, LV_PCT(100), kRowPressedHeight);
  lv_obj_align(pressed_background, LV_ALIGN_TOP_MID, 0, 0);
  lv_obj_set_style_bg_color(
      pressed_background, lv_color_hex(kRowPressedColor), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(pressed_background, kRowPressedOpacity, LV_PART_MAIN);
  lv_obj_set_style_border_width(pressed_background, 0, LV_PART_MAIN);
  lv_obj_set_style_radius(pressed_background, kRowPressedRadius, LV_PART_MAIN);
  lv_obj_set_style_pad_all(pressed_background, 0, LV_PART_MAIN);

  const size_t row_index = state->row_count;
  const app::CitTestStatus status = GetRuntimeStatus(*state, row_index);
  lv_obj_t* icon_label = CreateLabel(
      row, GetStatusIcon(status), GetStatusColor(status), GetStatusIconFont());
  if (icon_label == nullptr) {
    lv_obj_delete(row);
    return nullptr;
  }

  lv_obj_t* name_label =
      CreateLabel(row, TestTitle(entry), GetStatusColor(status), Font32());
  if (name_label == nullptr) {
    lv_obj_delete(row);
    return nullptr;
  }
  AlignStatusLabels(icon_label, name_label);

  state->rows[state->row_count] = {
      .entry = &entry,
      .state = state,
      .row_object = row,
      .icon_label = icon_label,
      .name_label = name_label,
      .pressed_background = pressed_background,
      .index = row_index,
  };
  if (!AddPressCancelOnLeave(row)) {
    lv_obj_delete(row);
    state->rows[state->row_count] = {};
    return nullptr;
  }
  lv_obj_add_event_cb(
      row, CitRowEventCallback, LV_EVENT_ALL, &state->rows[state->row_count]);
  ++state->row_count;
  return row;
}

/**
 * @brief 向 CIT 列表添加全部测试项行
 * @param parent 父对象
 * @param catalog 测试项目录
 * @param state CIT 页面状态
 * @return 成功返回 true，否则返回 false
 */
bool AddCitRows(
    lv_obj_t* parent, const app::CitTestCatalog& catalog, CitViewState* state) {
  if (state == nullptr || catalog.entries == nullptr ||
      catalog.entry_count > state->rows.size()) {
    return false;
  }

  for (size_t i = 0; i < catalog.entry_count; ++i) {
    const app::CitTestEntry& entry = catalog.entries[i];
    state->test_statuses[i] = app::CitTestStatus::kPending;
    if (CreateStatusRow(parent, entry, state) == nullptr) {
      return false;
    }
  }
  return true;
}

}  // namespace

lv_obj_t* CreateCitView(lv_obj_t* parent, const app::AppEntry& app_entry,
    const AppViewConfig& config) {
  if (parent == nullptr || config.width <= 0 || config.height <= 0) {
    return nullptr;
  }

  lv_obj_t* container = lv_obj_create(parent);
  if (container == nullptr) {
    return nullptr;
  }

  auto* state = new (std::nothrow) CitViewState();
  if (state == nullptr) {
    lv_obj_delete(container);
    return nullptr;
  }
  state->screen = config.screen;
  state->diagnostics_provider = config.diagnostics;
  state->gps = config.gps;
  state->audio = config.audio;
  state->haptic = config.haptic;
  state->bmu = config.bmu;
  state->rtc = config.rtc;
  state->imu = config.imu;
  state->ethernet = config.ethernet;
  state->wifi = config.wifi;
  state->root = container;
  state->width = config.width;
  state->height = config.height;
  state->test_statuses.fill(app::CitTestStatus::kPending);
  lv_obj_add_event_cb(container, CitViewDeleteCallback, LV_EVENT_DELETE, state);
  AddEdgeBackSwipeEvents(container, TestPageEdgeBackEventCallback, state);

  lv_obj_remove_flag(container, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_color(
      container, lv_color_hex(kCitBackgroundColor), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(container, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_radius(container, 0, LV_PART_MAIN);
  lv_obj_set_style_border_width(container, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(container, 0, LV_PART_MAIN);
  lv_obj_set_size(container, config.width, config.height);
  lv_obj_align(container, LV_ALIGN_CENTER, 0, 0);

  lv_obj_t* title_weight =
      CreateLabel(container, app_entry.title, lv_color_hex(0xFFFFFF), Font48());
  if (title_weight == nullptr) {
    lv_obj_delete(container);
    return nullptr;
  }
  lv_obj_set_size(title_weight, config.width - 2 * kTitleLeft, 58);
  lv_obj_align(title_weight, LV_ALIGN_TOP_LEFT, kTitleLeft + 1, kTitleTop);

  lv_obj_t* title =
      CreateLabel(container, app_entry.title, lv_color_hex(0xFFFFFF), Font48());
  if (title == nullptr) {
    lv_obj_delete(container);
    return nullptr;
  }
  lv_obj_set_size(title, config.width - 2 * kTitleLeft, 58);
  lv_obj_align(title, LV_ALIGN_TOP_LEFT, kTitleLeft, kTitleTop);

  const app::CitTestCatalog& catalog = app::GetCitTestCatalog();
  lv_obj_t* list = lv_obj_create(container);
  if (list == nullptr) {
    lv_obj_delete(container);
    return nullptr;
  }
  state->list_page = list;
  lv_obj_set_size(list, config.width, config.height - kListTop);
  lv_obj_align(list, LV_ALIGN_TOP_MID, 0, kListTop);
  lv_obj_set_style_bg_color(
      list, lv_color_hex(kListBackgroundColor), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(list, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_border_width(list, 0, LV_PART_MAIN);
  lv_obj_set_style_radius(list, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_left(list, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_right(list, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_top(list, kListTopPadding, LV_PART_MAIN);
  lv_obj_set_style_pad_bottom(list, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_row(list, 0, LV_PART_MAIN);
  lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_ACTIVE);

  if (!AddCitRows(list, catalog, state)) {
    lv_obj_delete(container);
    return nullptr;
  }

  state->refresh_timer =
      lv_timer_create(CitRefreshTimerCallback, kCitRefreshPeriodMs, state);
  if (state->refresh_timer == nullptr) {
    lv_obj_delete(container);
    return nullptr;
  }
  RefreshCitRows(state);

  return container;
}

}  // namespace lilygo_box::ui
