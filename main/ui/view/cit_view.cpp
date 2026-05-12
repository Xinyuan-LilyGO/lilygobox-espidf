/*
 * @Description: None
 * @Author: LILYGO_L
 * @Date: 2026-05-10 13:27:05
 * @LastEditTime: 2026-05-12 22:44:23
 * @License: GPL 3.0
 */
#include "ui/view/cit_view.h"

#include <array>
#include <cstdio>
#include <cstring>
#include <new>

#include "app/cit_test_catalog.h"
#include "esp_app_desc.h"
#include "esp_chip_info.h"
#include "esp_err.h"
#include "esp_flash.h"
#include "esp_heap_caps.h"
#include "esp_mac.h"
#include "esp_system.h"
#include "hal/device_diagnostics.h"
#include "hal/screen_device.h"
#include "sdkconfig.h"
#include "ui/app_view_gesture_flags.h"
#include "ui/edge_back_gesture.h"
#include "ui/font/font_assets.h"
#include "ui/font/material_symbols_assets.h"

namespace lilygo_box::ui {
namespace {

constexpr int kTitleTop = 70;
constexpr int kTitleLeft = 20;
constexpr int kListTop = 136;
constexpr int kListHorizontalPadding = 20;
constexpr int kListTopPadding = 20;
constexpr int kRowHeight = 76;
constexpr int kRowIconWidth = 50;
constexpr int kRowContentOffsetY = -7;
constexpr int kTestButtonBarHeight = 140;
constexpr int kTestButtonWidth = 200;
constexpr int kTestButtonHeight = 60;
constexpr int kTestButtonGap = 60;
constexpr int kTestButtonCenterOffset = (kTestButtonWidth + kTestButtonGap) / 2;
constexpr int kTestStartButtonWidth = 240;
constexpr int kTestStartButtonHeight = 78;
constexpr int kCitRefreshPeriodMs = 200;
constexpr int kDiagnosticsRefreshPeriodMs = 1000;
constexpr uint32_t kGestureSuppressTimeoutMs = 500;
constexpr uint32_t kPageSlideAnimationMs = 180;
constexpr lv_opa_t kBottomPageDimOpacity = 84;
constexpr uint32_t kCitBackgroundColor = 0xFF7F58;
constexpr uint32_t kListBackgroundColor = 0xFBF4E4;
constexpr uint32_t kRowDividerColor = 0xD8C8C0;
constexpr uint32_t kRowPressedColor = 0xEEDBD1;
constexpr lv_opa_t kRowPressedOpacity = 170;
constexpr int kRowPressedHeight = kRowHeight - 4;
constexpr int kRowPressedRadius = 12;
constexpr uint32_t kReadyColor = 0x138A3D;
constexpr uint32_t kFailedColor = 0xEE2C2C;
constexpr uint32_t kPendingColor = 0xF28C00;
constexpr uint32_t kPassButtonColor = 0x2F80ED;
constexpr uint32_t kFailButtonColor = 0x8A8A8A;
constexpr uint32_t kStartButtonColor = 0xE9785C;

struct CitViewState;

struct CitStatusRow {
  const app::CitTestEntry* entry = nullptr;
  CitViewState* state = nullptr;
  lv_obj_t* icon_label = nullptr;
  lv_obj_t* name_label = nullptr;
  lv_obj_t* pressed_background = nullptr;
  size_t index = 0;
};

struct CitViewState {
  lv_obj_t* root = nullptr;
  lv_obj_t* list_page = nullptr;
  lv_obj_t* list_dim_overlay = nullptr;
  lv_obj_t* test_page = nullptr;
  lv_obj_t* test_content = nullptr;
  lv_obj_t* test_data_label = nullptr;
  int width = 0;
  int height = 0;
  hal::ScreenDevice* screen = nullptr;
  hal::DeviceDiagnosticsProvider* diagnostics_provider = nullptr;
  hal::DeviceDiagnostics diagnostics;
  int diagnostics_elapsed_ms = kDiagnosticsRefreshPeriodMs;
  bool diagnostics_read = false;
  std::array<CitStatusRow, app::kMaxCitTestEntryCount> rows;
  std::array<app::CitTestStatus, app::kMaxCitTestEntryCount> test_statuses;
  size_t row_count = 0;
  size_t current_test_index = 0;
  int screen_color_index = 0;
  bool touch_was_seen = false;
  bool test_page_closing = false;
  lv_timer_t* refresh_timer = nullptr;
};

void ShowCitList(CitViewState* state);
bool ShowCitTest(CitViewState* state, size_t index);
void TestPageGestureEventCallback(lv_event_t* event);

void SetTextStyle(lv_obj_t* object, lv_color_t color, const lv_font_t* font) {
  lv_obj_set_style_text_color(object, color, LV_PART_MAIN);
  lv_obj_set_style_text_font(object, font, LV_PART_MAIN);
}

const lv_font_t* Font28() { return &lvgl_font_google_sans_flex_28; }

const lv_font_t* Font32() { return &lvgl_font_google_sans_flex_32; }

const lv_font_t* Font48() { return &lvgl_font_google_sans_flex_48; }

const lv_font_t* MaterialIconFont32() { return &lvgl_font_material_symbols_32; }

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

void ClearSuppressLauncherGestureTimerCallback(lv_timer_t* timer) {
  auto* app_view = static_cast<lv_obj_t*>(lv_timer_get_user_data(timer));
  if (app_view != nullptr && lv_obj_is_valid(app_view)) {
    lv_obj_remove_flag(app_view, kSuppressNextLauncherGestureFlag);
  }
}

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

void SetPageX(void* object, int32_t x) {
  lv_obj_set_x(static_cast<lv_obj_t*>(object), x);
}

void SetDimOverlayOpacity(void* object, int32_t opacity) {
  lv_obj_set_style_bg_opa(
      static_cast<lv_obj_t*>(object), opacity, LV_PART_MAIN);
}

void RestoreCitListGestures(CitViewState* state) {
  if (state == nullptr || state->root == nullptr) {
    return;
  }

  lv_obj_remove_flag(state->root, kBlockLauncherGestureFlag);
  lv_obj_add_flag(state->root, LV_OBJ_FLAG_GESTURE_BUBBLE);
}

void ClearTestPageState(CitViewState* state) {
  if (state == nullptr) {
    return;
  }

  state->test_page = nullptr;
  state->test_content = nullptr;
  state->test_data_label = nullptr;
  state->test_page_closing = false;
}

void FinishTestPageClose(CitViewState* state) {
  if (state == nullptr) {
    return;
  }

  if (state->test_page != nullptr) {
    lv_obj_delete(state->test_page);
  }
  ClearTestPageState(state);
  RestoreCitListGestures(state);
}

void TestPageCloseCompletedCallback(lv_anim_t* animation) {
  auto* state = static_cast<CitViewState*>(lv_anim_get_user_data(animation));
  FinishTestPageClose(state);
}

bool StartTestPageSlideAnimation(lv_obj_t* page, int32_t start_x, int32_t end_x,
    CitViewState* state, lv_anim_completed_cb_t completed_callback) {
  if (page == nullptr) {
    return false;
  }

  lv_anim_delete(page, SetPageX);
  lv_obj_set_x(page, start_x);

  lv_anim_t animation;
  lv_anim_init(&animation);
  lv_anim_set_var(&animation, page);
  lv_anim_set_values(&animation, start_x, end_x);
  lv_anim_set_duration(&animation, kPageSlideAnimationMs);
  lv_anim_set_path_cb(&animation, lv_anim_path_ease_out);
  lv_anim_set_exec_cb(&animation, SetPageX);
  lv_anim_set_user_data(&animation, state);
  if (completed_callback != nullptr) {
    lv_anim_set_completed_cb(&animation, completed_callback);
  }
  return lv_anim_start(&animation) != nullptr;
}

void DeleteListDimOverlay(CitViewState* state) {
  if (state == nullptr || state->list_dim_overlay == nullptr) {
    return;
  }

  lv_anim_delete(state->list_dim_overlay, SetDimOverlayOpacity);
  lv_obj_delete(state->list_dim_overlay);
  state->list_dim_overlay = nullptr;
}

void DimOverlayFadeOutCompletedCallback(lv_anim_t* animation) {
  auto* state = static_cast<CitViewState*>(lv_anim_get_user_data(animation));
  DeleteListDimOverlay(state);
}

lv_obj_t* EnsureListDimOverlay(CitViewState* state, bool* created) {
  if (created != nullptr) {
    *created = false;
  }
  if (state == nullptr || state->root == nullptr) {
    return nullptr;
  }
  if (state->list_dim_overlay != nullptr &&
      lv_obj_is_valid(state->list_dim_overlay)) {
    return state->list_dim_overlay;
  }

  lv_obj_t* overlay = lv_obj_create(state->root);
  if (overlay == nullptr) {
    state->list_dim_overlay = nullptr;
    return nullptr;
  }
  state->list_dim_overlay = overlay;
  if (created != nullptr) {
    *created = true;
  }

  lv_obj_remove_flag(overlay, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(overlay, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_size(overlay, state->width, state->height);
  lv_obj_set_pos(overlay, 0, 0);
  lv_obj_set_style_bg_color(overlay, lv_color_hex(0x000000), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(overlay, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_set_style_border_width(overlay, 0, LV_PART_MAIN);
  lv_obj_set_style_radius(overlay, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(overlay, 0, LV_PART_MAIN);
  return overlay;
}

bool StartDimOverlayAnimation(lv_obj_t* overlay, int32_t start_opacity,
    int32_t end_opacity, CitViewState* state,
    lv_anim_completed_cb_t completed_callback) {
  if (overlay == nullptr) {
    return false;
  }

  lv_anim_delete(overlay, SetDimOverlayOpacity);
  SetDimOverlayOpacity(overlay, start_opacity);

  lv_anim_t animation;
  lv_anim_init(&animation);
  lv_anim_set_var(&animation, overlay);
  lv_anim_set_values(&animation, start_opacity, end_opacity);
  lv_anim_set_duration(&animation, kPageSlideAnimationMs);
  lv_anim_set_path_cb(&animation, lv_anim_path_ease_out);
  lv_anim_set_exec_cb(&animation, SetDimOverlayOpacity);
  lv_anim_set_user_data(&animation, state);
  if (completed_callback != nullptr) {
    lv_anim_set_completed_cb(&animation, completed_callback);
  }
  return lv_anim_start(&animation) != nullptr;
}

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
  if (IsEntryId(entry, "battery")) {
    return "Battery Health Test";
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

bool IsEntryId(const app::CitTestEntry& entry, const char* id) {
  if (entry.id == nullptr || id == nullptr) {
    return false;
  }
  return std::strcmp(entry.id, id) == 0;
}

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

void AlignStatusLabels(lv_obj_t* icon_label, lv_obj_t* name_label) {
  if (icon_label == nullptr || name_label == nullptr) {
    return;
  }

  lv_obj_set_width(icon_label, kRowIconWidth);
  lv_obj_set_style_text_align(icon_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
  lv_obj_align(icon_label, LV_ALIGN_LEFT_MID, 0, kRowContentOffsetY);
  lv_obj_align(
      name_label, LV_ALIGN_LEFT_MID, kRowIconWidth, kRowContentOffsetY);
}

const lv_font_t* GetStatusIconFont() { return MaterialIconFont32(); }

void RefreshTouchState(CitViewState* state) {
  if (state == nullptr || state->screen == nullptr || state->touch_was_seen) {
    return;
  }

  hal::TouchPoint point;
  const bool result = state->screen->ReadTouch(&point);
  if (result) {
    state->touch_was_seen = true;
  }
}

void RefreshDiagnosticsState(CitViewState* state) {
  if (state == nullptr || state->diagnostics_provider == nullptr) {
    return;
  }
  if (state->diagnostics_elapsed_ms < kDiagnosticsRefreshPeriodMs) {
    state->diagnostics_elapsed_ms += kCitRefreshPeriodMs;
    return;
  }

  const bool result =
      state->diagnostics_provider->ReadDiagnostics(&state->diagnostics);
  state->diagnostics_read = result;
  state->diagnostics_elapsed_ms = 0;
}

app::CitTestStatus GetRuntimeStatus(const CitViewState& state, size_t index) {
  if (index < state.test_statuses.size()) {
    return state.test_statuses[index];
  }
  return app::CitTestStatus::kPending;
}

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

void RefreshActiveTestData(CitViewState* state) {
  if (state == nullptr || state->test_data_label == nullptr ||
      state->current_test_index >= state->row_count) {
    return;
  }

  const app::CitTestEntry* entry = state->rows[state->current_test_index].entry;
  if (entry == nullptr) {
    return;
  }

  char text[256] = {};
  if (IsEntryId(*entry, "touch")) {
    hal::TouchPoint point;
    if (state->screen != nullptr && state->screen->ReadTouch(&point)) {
      std::snprintf(text, sizeof(text),
          "touch data:\nstate: pressed\nx: %d\ny: %d", point.x, point.y);
    } else {
      std::snprintf(text, sizeof(text), "touch data:\nstate: released");
    }
    lv_label_set_text(state->test_data_label, text);
    return;
  }

  if (IsEntryId(*entry, "imu")) {
    const hal::MotionDiagnostics& motion = state->diagnostics.motion;
    std::snprintf(text, sizeof(text),
        "imu data:\nready: %s\nx: %.2f g\ny: %.2f g\nz: %.2f g",
        motion.ready ? "yes" : "no", motion.acceleration_x_g,
        motion.acceleration_y_g, motion.acceleration_z_g);
    lv_label_set_text(state->test_data_label, text);
    return;
  }

  if (IsEntryId(*entry, "battery")) {
    const hal::PowerDiagnostics& power = state->diagnostics.power;
    std::snprintf(text, sizeof(text),
        "battery health data:\nready: %s\nbattery: %s\ncharging: %s\n"
        "voltage: %d mV\ncurrent: %d mA\ncharge: %d%%",
        power.ready ? "yes" : "no", power.battery_present ? "present" : "none",
        power.charging ? "yes" : "no", power.voltage_mv, power.current_ma,
        power.charge_percent);
    lv_label_set_text(state->test_data_label, text);
  }
}

void CitRefreshTimerCallback(lv_timer_t* timer) {
  auto* state = static_cast<CitViewState*>(lv_timer_get_user_data(timer));
  RefreshCitRows(state);
  RefreshActiveTestData(state);
}

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
  delete state;
}

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

void CitRowEventCallback(lv_event_t* event) {
  auto* row = static_cast<CitStatusRow*>(lv_event_get_user_data(event));
  if (row == nullptr || row->entry == nullptr) {
    return;
  }

  const lv_event_code_t code = lv_event_get_code(event);
  if (code == LV_EVENT_PRESSED) {
    SetCitRowPressed(row, true);
    return;
  }
  if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
    SetCitRowPressed(row, false);
    return;
  }
  if (code == LV_EVENT_CLICKED) {
    SetCitRowPressed(row, false);
    ShowCitTest(row->state, row->index);
  }
}

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
  RefreshCitRows(state);

  const size_t next_index = state->current_test_index + 1;
  if (next_index < state->row_count) {
    ShowCitTest(state, next_index);
    return;
  }

  ShowCitList(state);
}

void FailCurrentTestAndShowList(CitViewState* state) {
  if (state == nullptr || state->current_test_index >= state->row_count) {
    return;
  }

  state->test_statuses[state->current_test_index] = app::CitTestStatus::kFailed;
  RefreshCitRows(state);
  ShowCitList(state);
}

void TestFailButtonEventCallback(lv_event_t* event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
    return;
  }

  auto* state = static_cast<CitViewState*>(lv_event_get_user_data(event));
  FailCurrentTestAndShowList(state);
}

void TestPageGestureEventCallback(lv_event_t* event) {
  if (lv_event_get_code(event) != LV_EVENT_GESTURE) {
    return;
  }

  auto* state = static_cast<CitViewState*>(lv_event_get_user_data(event));
  if (state == nullptr || state->test_page == nullptr) {
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
      !IsBackGestureFromEdge(gesture, state->width)) {
    return;
  }

  if (state->root != nullptr) {
    SuppressNextLauncherGesture(state->root);
  }
  lv_event_stop_bubbling(event);
  lv_event_stop_processing(event);
  ShowCitList(state);
}

void ScreenColorStartButtonEventCallback(lv_event_t* event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
    return;
  }

  auto* state = static_cast<CitViewState*>(lv_event_get_user_data(event));
  if (state == nullptr || state->test_content == nullptr) {
    return;
  }

  constexpr uint32_t kColorList[] = {
      0xFF0000,
      0x00FF00,
      0x0000FF,
      0xFFFFFF,
      kListBackgroundColor,
  };
  state->screen_color_index = (state->screen_color_index + 1) %
                              (sizeof(kColorList) / sizeof(kColorList[0]));
  lv_obj_set_style_bg_color(state->test_content,
      lv_color_hex(kColorList[state->screen_color_index]), LV_PART_MAIN);
}

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
    lv_label_set_text(state->test_data_label,
        "vibration data:\nSTART F0 requested\nconfirm motor response");
    return;
  }
  if (IsEntryId(*entry, "speaker")) {
    lv_label_set_text(state->test_data_label,
        "speaker data:\nSTART PLAY requested\nconfirm audio output");
  }
}

lv_obj_t* CreateTestActionButton(lv_obj_t* parent, const char* text,
    uint32_t color, lv_align_t align, int x, lv_event_cb_t callback,
    CitViewState* state) {
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
  lv_obj_add_event_cb(button, callback, LV_EVENT_CLICKED, state);
  lv_obj_add_event_cb(
      button, TestPageGestureEventCallback, LV_EVENT_GESTURE, state);

  lv_obj_t* label = CreateLabel(button, text, lv_color_hex(0xFFFFFF), Font28());
  if (label == nullptr) {
    lv_obj_delete(button);
    return nullptr;
  }
  lv_obj_center(label);
  return button;
}

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
  if (callback != nullptr) {
    lv_obj_add_event_cb(button, callback, LV_EVENT_CLICKED, state);
  }
  lv_obj_add_event_cb(
      button, TestPageGestureEventCallback, LV_EVENT_GESTURE, state);

  lv_obj_t* label = CreateLabel(button, text, lv_color_hex(0xFFFFFF), Font28());
  if (label == nullptr) {
    lv_obj_delete(button);
    return nullptr;
  }
  lv_obj_center(label);
  return button;
}

lv_obj_t* CreateTestButtonBar(lv_obj_t* parent, CitViewState* state) {
  lv_obj_t* button_bar = lv_obj_create(parent);
  if (button_bar == nullptr) {
    return nullptr;
  }

  lv_obj_remove_flag(button_bar, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_event_cb(
      button_bar, TestPageGestureEventCallback, LV_EVENT_GESTURE, state);
  lv_obj_set_size(button_bar, LV_PCT(100), kTestButtonBarHeight);
  lv_obj_align(button_bar, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_obj_set_style_bg_color(
      button_bar, lv_color_hex(kListBackgroundColor), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(button_bar, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_radius(button_bar, 0, LV_PART_MAIN);
  lv_obj_set_style_border_width(button_bar, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(button_bar, 0, LV_PART_MAIN);

  if (CreateTestActionButton(button_bar, "FAIL", kFailButtonColor,
          LV_ALIGN_CENTER, -kTestButtonCenterOffset,
          TestFailButtonEventCallback, state) == nullptr ||
      CreateTestActionButton(button_bar, "PASS", kPassButtonColor,
          LV_ALIGN_CENTER, kTestButtonCenterOffset, TestPassButtonEventCallback,
          state) == nullptr) {
    lv_obj_delete(button_bar);
    return nullptr;
  }

  return button_bar;
}

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
    return "Confirm the vibration motor response.";
  }
  if (IsEntryId(entry, "speaker")) {
    return "Confirm the speaker output.";
  }
  if (IsEntryId(entry, "microphone")) {
    return "Confirm the microphone input.";
  }
  if (IsEntryId(entry, "imu")) {
    return "Move the device and confirm motion data is available.";
  }
  if (IsEntryId(entry, "battery")) {
    return "Confirm battery and power diagnostics.";
  }
  if (IsEntryId(entry, "gps")) {
    return "Confirm GPS test requirements.";
  }
  if (IsEntryId(entry, "ethernet")) {
    return "Confirm Ethernet connectivity test requirements.";
  }
  if (IsEntryId(entry, "rtc")) {
    return "Confirm RTC time keeping.";
  }
  if (IsEntryId(entry, "wifi")) {
    return "Confirm WIFI time acquisition.";
  }
  return "Run the hardware test and choose PASS or FAIL.";
}

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

const char* ConfiguredChipModel() {
#if defined(CONFIG_IDF_TARGET)
  return CONFIG_IDF_TARGET;
#else
  return "unknown";
#endif
}

const char* ConfiguredTargetArch() {
#if defined(CONFIG_IDF_TARGET_ARCH)
  return CONFIG_IDF_TARGET_ARCH;
#else
  return "unknown";
#endif
}

const char* ConfiguredDeviceName() {
#if defined(CONFIG_BOARD_TYPE_T_DISPLAY_P4_KEYBOARD)
  return "t-display-p4-keyboard";
#elif defined(CONFIG_BOARD_TYPE_T_DISPLAY_P4)
#if defined(CONFIG_BOARD_VERSION_T_DISPLAY_P4_V1_0)
  return "t-display-p4_v1.0";
#elif defined(CONFIG_BOARD_VERSION_T_DISPLAY_P4_V2_0)
  return "t-display-p4_v2.0";
#else
  return "t-display-p4";
#endif
#elif defined(CONFIG_LILYGO_BOX_DEVICE_T_DISPLAY_P4)
  return "t-display-p4";
#else
  return "unknown";
#endif
}

const char* ConfiguredScreenType() {
#if defined(CONFIG_SCREEN_TYPE_HI8561)
  return "hi8561";
#elif defined(CONFIG_SCREEN_TYPE_RM69A10)
  return "rm69a10";
#else
  return "unknown";
#endif
}

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

const char* ConfiguredCameraPixelFormat() {
#if defined(CONFIG_CAMERA_PIXEL_FORMAT_RGB565)
  return "rgb565";
#elif defined(CONFIG_CAMERA_PIXEL_FORMAT_RGB888)
  return "rgb888";
#else
  return "unknown";
#endif
}

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

const char* KnownString(const char* text) {
  return (text == nullptr || text[0] == '\0') ? "unknown" : text;
}

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

bool AddVersionContent(lv_obj_t* content, CitViewState* state) {
  esp_chip_info_t chip_info = {};
  esp_chip_info(&chip_info);

  const esp_app_desc_t* app_description = esp_app_get_description();

  char mac_address[18] = {};
  FormatMacAddress(mac_address, sizeof(mac_address));

  uint32_t flash_size = 0;
  const bool flash_size_read =
      esp_flash_get_size(nullptr, &flash_size) == ESP_OK;

  const int screen_width = state->screen->width();
  const int screen_height = state->screen->height();
  const int screen_bpp = state->screen->bits_per_pixel();
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
      "flash size:\n"
      "     %lu bytes (%lu MB)\n"
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
      "screen color depth: %d bpp\n"
      "\n"
      "[Camera]\n"
      "camera type: %s\n"
      "camera pixel format: %s\n"
      "\n"
      "[LVGL]\n"
      "lvgl version: %d.%d.%d%s",
      ConfiguredChipModel(), mac_address, chip_info.revision / 100,
      chip_info.revision % 100, chip_info.cores,
      flash_size_read ? static_cast<unsigned long>(flash_size) : 0UL,
      flash_size_read ? static_cast<unsigned long>(flash_size / 1024 / 1024)
                      : 0UL,
      (chip_info.features & CHIP_FEATURE_EMB_FLASH) ? "embedded" : "external",
      static_cast<unsigned long>(esp_get_free_heap_size()),
      static_cast<unsigned long>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)),
      static_cast<unsigned long>(heap_caps_get_total_size(MALLOC_CAP_INTERNAL)),
      static_cast<unsigned long>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)),
      static_cast<unsigned long>(heap_caps_get_total_size(MALLOC_CAP_SPIRAM)),
      ConfiguredDeviceName(), app_project_name, app_version, app_build_date,
      app_build_time, esp_get_idf_version(), ConfiguredTargetArch(),
      ConfiguredScreenType(), screen_width, screen_height,
      ScreenPixelFormat(screen_bpp), screen_bpp, ConfiguredCameraType(),
      ConfiguredCameraPixelFormat(), LVGL_VERSION_MAJOR, LVGL_VERSION_MINOR,
      LVGL_VERSION_PATCH, LVGL_VERSION_INFO);

  lv_obj_add_flag(content, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_scroll_dir(content, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(content, LV_SCROLLBAR_MODE_ACTIVE);
  return CreateDataLabel(content, text) != nullptr;
}

bool AddTouchContent(lv_obj_t* content, CitViewState* state) {
  state->test_data_label = CreateDataLabel(content, "touch data:\nstate: idle");
  if (state->test_data_label == nullptr) {
    return false;
  }

  lv_obj_t* touch_area = lv_obj_create(content);
  if (touch_area == nullptr) {
    return false;
  }
  lv_obj_set_size(touch_area, LV_PCT(100), 260);
  lv_obj_align(touch_area, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_obj_set_style_bg_color(touch_area, lv_color_hex(0xD9D9D9), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(touch_area, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_border_width(touch_area, 0, LV_PART_MAIN);
  lv_obj_set_style_radius(touch_area, 0, LV_PART_MAIN);

  lv_obj_t* label =
      CreateLabel(touch_area, "Touch Area", lv_color_hex(0x606060), Font28());
  if (label == nullptr) {
    return false;
  }
  lv_obj_center(label);
  return true;
}

bool AddScreenColorContent(lv_obj_t* content, CitViewState* state) {
  state->screen_color_index = 4;
  lv_obj_t* hint = CreateDataLabel(content,
      "Tap START COLOR to cycle red, green, blue, "
      "white, and normal background.");
  if (hint == nullptr) {
    return false;
  }
  return CreateCenterButton(content, "START COLOR",
             ScreenColorStartButtonEventCallback, state) != nullptr;
}

bool AddStartButtonContent(lv_obj_t* content, CitViewState* state,
    const char* data_text, const char* button_text) {
  state->test_data_label = CreateDataLabel(content, data_text);
  if (state->test_data_label == nullptr) {
    return false;
  }
  return CreateCenterButton(content, button_text,
             GenericStartButtonEventCallback, state) != nullptr;
}

bool AddMicrophoneContent(lv_obj_t* content) {
  lv_obj_t* scale = lv_scale_create(content);
  if (scale == nullptr) {
    return false;
  }
  lv_obj_set_size(scale, 360, 360);
  lv_obj_align(scale, LV_ALIGN_TOP_MID, 0, 36);
  lv_scale_set_mode(scale, LV_SCALE_MODE_ROUND_INNER);
  lv_scale_set_label_show(scale, true);
  lv_scale_set_total_tick_count(scale, 51);
  lv_scale_set_major_tick_every(scale, 5);
  lv_scale_set_range(scale, 0, 100);
  lv_scale_set_angle_range(scale, 270);
  lv_scale_set_rotation(scale, 135);

  lv_obj_t* label = CreateLabel(content, "microphone data:\nlevel: waiting",
      lv_color_hex(0x202020), Font28());
  if (label == nullptr) {
    return false;
  }
  lv_obj_align(label, LV_ALIGN_TOP_MID, 0, 420);

  lv_obj_t* switch_label =
      CreateLabel(content, "adc -> dac", lv_color_hex(0x202020), Font28());
  if (switch_label == nullptr) {
    return false;
  }
  lv_obj_align(switch_label, LV_ALIGN_TOP_MID, 0, 500);

  lv_obj_t* switch_object = lv_switch_create(content);
  if (switch_object == nullptr) {
    return false;
  }
  lv_obj_set_size(switch_object, 90, 50);
  lv_obj_align(switch_object, LV_ALIGN_TOP_MID, 0, 550);
  return true;
}

bool AddDiagnosticsContent(
    lv_obj_t* content, CitViewState* state, const app::CitTestEntry& entry) {
  const char* initial_text = "diagnostics data:";
  if (IsEntryId(entry, "imu")) {
    initial_text = "imu data:";
  } else if (IsEntryId(entry, "battery")) {
    initial_text = "battery health data:";
  }

  state->test_data_label = CreateDataLabel(content, initial_text);
  if (state->test_data_label == nullptr) {
    return false;
  }
  RefreshActiveTestData(state);
  return true;
}

bool AddPlainDataContent(lv_obj_t* content, const app::CitTestEntry& entry) {
  if (IsEntryId(entry, "gps")) {
    return CreateDataLabel(content, "GPS data:\nwaiting for module data") !=
           nullptr;
  }
  if (IsEntryId(entry, "ethernet")) {
    return CreateDataLabel(content, "Ethernet data:\nwaiting for link data") !=
           nullptr;
  }
  if (IsEntryId(entry, "rtc")) {
    return CreateDataLabel(content, "RTC data:\nwaiting for time data") !=
           nullptr;
  }
  if (IsEntryId(entry, "wifi")) {
    return CreateDataLabel(
               content, "WIFI time data:\nwaiting for time data") != nullptr;
  }
  return CreateDataLabel(content, GetTestHint(entry)) != nullptr;
}

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
    return AddStartButtonContent(content, state, "vibration data:", "START F0");
  }
  if (IsEntryId(entry, "speaker")) {
    return AddStartButtonContent(content, state, "speaker data:", "START PLAY");
  }
  if (IsEntryId(entry, "microphone")) {
    return AddMicrophoneContent(content);
  }
  if (IsEntryId(entry, "imu") || IsEntryId(entry, "battery")) {
    return AddDiagnosticsContent(content, state, entry);
  }
  return AddPlainDataContent(content, entry);
}

void DeleteTestPage(CitViewState* state) {
  if (state == nullptr || state->test_page == nullptr) {
    return;
  }

  lv_anim_delete(state->test_page, SetPageX);
  lv_obj_delete(state->test_page);
  ClearTestPageState(state);
}

void DeleteTestPageAndDimOverlay(CitViewState* state) {
  DeleteTestPage(state);
  DeleteListDimOverlay(state);
}

bool ShowCitTest(CitViewState* state, size_t index) {
  if (state == nullptr || state->root == nullptr || index >= state->row_count) {
    return false;
  }

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
  lv_obj_remove_flag(page, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_event_cb(
      page, TestPageGestureEventCallback, LV_EVENT_GESTURE, state);
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
    DeleteTestPageAndDimOverlay(state);
    return false;
  }
  lv_obj_set_size(title, state->width - 2 * kTitleLeft, 70);
  lv_obj_align(title, LV_ALIGN_TOP_LEFT, kTitleLeft, kTitleTop);

  lv_obj_t* content = lv_obj_create(page);
  if (content == nullptr) {
    DeleteTestPageAndDimOverlay(state);
    return false;
  }
  state->test_content = content;
  lv_obj_remove_flag(content, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_event_cb(
      content, TestPageGestureEventCallback, LV_EVENT_GESTURE, state);
  lv_obj_set_size(
      content, LV_PCT(100), state->height - kListTop - kTestButtonBarHeight);
  lv_obj_set_style_bg_color(
      content, lv_color_hex(kListBackgroundColor), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(content, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_radius(content, 0, LV_PART_MAIN);
  lv_obj_set_style_border_width(content, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(content, 24, LV_PART_MAIN);
  lv_obj_align(content, LV_ALIGN_TOP_MID, 0, kListTop);

  if (!PopulateTestContent(content, state, *row.entry) ||
      CreateTestButtonBar(page, state) == nullptr) {
    DeleteTestPageAndDimOverlay(state);
    return false;
  }

  bool dim_overlay_created = false;
  lv_obj_t* dim_overlay = EnsureListDimOverlay(state, &dim_overlay_created);
  if (dim_overlay != nullptr) {
    lv_obj_move_to_index(page, -1);
    lv_opa_t start_opacity = LV_OPA_TRANSP;
    if (!dim_overlay_created) {
      start_opacity = lv_obj_get_style_bg_opa(dim_overlay, LV_PART_MAIN);
    }
    if (!StartDimOverlayAnimation(dim_overlay, start_opacity,
            kBottomPageDimOpacity, state, nullptr)) {
      SetDimOverlayOpacity(dim_overlay, kBottomPageDimOpacity);
    }
  }

  if (!StartTestPageSlideAnimation(page, state->width, 0, state, nullptr)) {
    lv_obj_set_x(page, 0);
  }
  return true;
}

void ShowCitList(CitViewState* state) {
  if (state == nullptr) {
    return;
  }

  if (state->list_page != nullptr) {
    lv_obj_remove_flag(state->list_page, LV_OBJ_FLAG_HIDDEN);
  }
  if (state->test_page == nullptr) {
    DeleteListDimOverlay(state);
    RestoreCitListGestures(state);
    return;
  }
  if (state->test_page_closing) {
    return;
  }

  state->test_page_closing = true;
  if (state->list_dim_overlay != nullptr &&
      lv_obj_is_valid(state->list_dim_overlay)) {
    const int32_t start_opacity =
        lv_obj_get_style_bg_opa(state->list_dim_overlay, LV_PART_MAIN);
    if (!StartDimOverlayAnimation(state->list_dim_overlay, start_opacity,
            LV_OPA_TRANSP, state, DimOverlayFadeOutCompletedCallback)) {
      DeleteListDimOverlay(state);
    }
  }
  const int32_t start_x = lv_obj_get_x(state->test_page);
  if (!StartTestPageSlideAnimation(state->test_page, start_x, state->width,
          state, TestPageCloseCompletedCallback)) {
    FinishTestPageClose(state);
  }
}

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
  lv_obj_set_style_border_width(row, 1, LV_PART_MAIN);
  lv_obj_set_style_border_side(row, LV_BORDER_SIDE_BOTTOM, LV_PART_MAIN);
  lv_obj_set_style_border_color(
      row, lv_color_hex(kRowDividerColor), LV_PART_MAIN);
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
  lv_obj_align(
      pressed_background, LV_ALIGN_CENTER, 0, kRowContentOffsetY);
  lv_obj_set_style_bg_color(
      pressed_background, lv_color_hex(kRowPressedColor), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(
      pressed_background, kRowPressedOpacity, LV_PART_MAIN);
  lv_obj_set_style_border_width(pressed_background, 0, LV_PART_MAIN);
  lv_obj_set_style_radius(
      pressed_background, kRowPressedRadius, LV_PART_MAIN);
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
      .icon_label = icon_label,
      .name_label = name_label,
      .pressed_background = pressed_background,
      .index = row_index,
  };
  lv_obj_add_event_cb(row, CitRowEventCallback, LV_EVENT_ALL,
      &state->rows[state->row_count]);
  ++state->row_count;
  return row;
}

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
  state->root = container;
  state->width = config.width;
  state->height = config.height;
  state->test_statuses.fill(app::CitTestStatus::kPending);
  lv_obj_add_event_cb(container, CitViewDeleteCallback, LV_EVENT_DELETE, state);
  lv_obj_add_event_cb(
      container, TestPageGestureEventCallback, LV_EVENT_GESTURE, state);

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
  lv_obj_set_style_pad_left(list, kListHorizontalPadding, LV_PART_MAIN);
  lv_obj_set_style_pad_right(list, kListHorizontalPadding, LV_PART_MAIN);
  lv_obj_set_style_pad_top(list, kListTopPadding, LV_PART_MAIN);
  lv_obj_set_style_pad_bottom(list, 0, LV_PART_MAIN);
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
