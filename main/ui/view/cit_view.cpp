/*
 * @Description: None
 * @Author: LILYGO_L
 * @Date: 2026-05-10 13:27:05
 * @LastEditTime: 2026-05-11 17:38:45
 * @License: GPL 3.0
 */
#include "ui/view/cit_view.h"

#include <array>
#include <cstring>
#include <new>

#include "app/cit_test_catalog.h"
#include "hal/device_diagnostics.h"
#include "hal/screen_device.h"
#include "ui/font/font_assets.h"
#include "ui/font/material_symbols_assets.h"

namespace lilygo_box::ui {
namespace {

constexpr int kStatusBarHeight = 50;
constexpr int kTitleTop = 70;
constexpr int kTitleLeft = 20;
constexpr int kListTop = 136;
constexpr int kListHorizontalPadding = 20;
constexpr int kListTopPadding = 20;
constexpr int kRowHeight = 76;
constexpr int kRowIconWidth = 50;
constexpr int kRowContentOffsetY = -7;
constexpr int kCitRefreshPeriodMs = 200;
constexpr int kDiagnosticsRefreshPeriodMs = 1000;
constexpr uint32_t kCitBackgroundColor = 0xFF7F58;
constexpr uint32_t kListBackgroundColor = 0xF6E4DE;
constexpr uint32_t kRowDividerColor = 0xD8C8C0;
constexpr uint32_t kReadyColor = 0x138A3D;
constexpr uint32_t kWaitingColor = 0x202020;
constexpr uint32_t kPendingColor = 0xF28C00;

struct CitStatusRow {
  const app::CitTestEntry* entry = nullptr;
  lv_obj_t* icon_label = nullptr;
  lv_obj_t* name_label = nullptr;
};

struct CitViewState {
  hal::ScreenDevice* screen = nullptr;
  hal::DeviceDiagnosticsProvider* diagnostics_provider = nullptr;
  hal::DeviceDiagnostics diagnostics;
  int diagnostics_elapsed_ms = kDiagnosticsRefreshPeriodMs;
  bool diagnostics_read = false;
  std::array<CitStatusRow, app::kMaxCitTestEntryCount> rows;
  size_t row_count = 0;
  bool touch_was_seen = false;
  lv_timer_t* refresh_timer = nullptr;
};

void SetTextStyle(lv_obj_t* object, lv_color_t color, const lv_font_t* font) {
  lv_obj_set_style_text_color(object, color, LV_PART_MAIN);
  lv_obj_set_style_text_font(object, font, LV_PART_MAIN);
}

const lv_font_t* Font24() { return &lvgl_font_google_sans_flex_24; }

const lv_font_t* Font32() { return &lvgl_font_google_sans_flex_32; }

const lv_font_t* Font48() { return &lvgl_font_google_sans_flex_48; }

const lv_font_t* MaterialIconFont28() { return &lvgl_font_material_symbols_28; }

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
    case app::CitTestStatus::kWaiting:
      return lv_color_hex(kWaitingColor);
    case app::CitTestStatus::kPending:
      return lv_color_hex(kPendingColor);
  }
  return lv_color_hex(kPendingColor);
}

const char* GetStatusIcon(app::CitTestStatus status) {
  switch (status) {
    case app::CitTestStatus::kReady:
      return icon::kCheckCircle;
    case app::CitTestStatus::kWaiting:
      return "R";
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
  lv_obj_align(name_label, LV_ALIGN_LEFT_MID, kRowIconWidth, kRowContentOffsetY);
}

const lv_font_t* GetStatusIconFont(app::CitTestStatus status) {
  if (status == app::CitTestStatus::kWaiting) {
    return Font32();
  }
  return MaterialIconFont32();
}

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

app::CitTestStatus GetRuntimeStatus(
    const CitViewState& state, const app::CitTestEntry& entry) {
  if (IsEntryId(entry, "version")) {
    return app::CitTestStatus::kReady;
  }

  if (IsEntryId(entry, "screen")) {
    if (state.screen == nullptr) {
      return app::CitTestStatus::kPending;
    }
    if (state.screen->width() <= 0 || state.screen->height() <= 0 ||
        state.screen->bits_per_pixel() <= 0) {
      return app::CitTestStatus::kPending;
    }
    return app::CitTestStatus::kReady;
  }

  if (IsEntryId(entry, "touch")) {
    if (state.touch_was_seen) {
      return app::CitTestStatus::kReady;
    }
    return app::CitTestStatus::kWaiting;
  }

  if (IsEntryId(entry, "power")) {
    if (state.diagnostics_read && state.diagnostics.power.ready) {
      return app::CitTestStatus::kReady;
    }
    return app::CitTestStatus::kPending;
  }

  if (IsEntryId(entry, "imu")) {
    if (state.diagnostics_read && state.diagnostics.motion.ready) {
      return app::CitTestStatus::kReady;
    }
    return app::CitTestStatus::kPending;
  }

  return entry.status;
}

void UpdateStatusRow(lv_obj_t* icon_label, lv_obj_t* name_label,
    app::CitTestStatus status) {
  if (icon_label == nullptr || name_label == nullptr) {
    return;
  }

  const lv_color_t color = GetStatusColor(status);
  lv_label_set_text(icon_label, GetStatusIcon(status));
  lv_obj_set_style_text_color(icon_label, color, LV_PART_MAIN);
  lv_obj_set_style_text_font(icon_label, GetStatusIconFont(status), LV_PART_MAIN);
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
    const app::CitTestStatus status = GetRuntimeStatus(*state, *row.entry);
    UpdateStatusRow(row.icon_label, row.name_label, status);
  }
}

void CitRefreshTimerCallback(lv_timer_t* timer) {
  auto* state = static_cast<CitViewState*>(lv_timer_get_user_data(timer));
  RefreshCitRows(state);
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

lv_obj_t* CreateCitStatusBar(lv_obj_t* parent, int width) {
  lv_obj_t* status_bar = lv_obj_create(parent);
  if (status_bar == nullptr) {
    return nullptr;
  }

  lv_obj_remove_flag(status_bar, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(status_bar, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_size(status_bar, width, kStatusBarHeight);
  lv_obj_align(status_bar, LV_ALIGN_TOP_MID, 0, 0);
  lv_obj_set_style_bg_color(status_bar, lv_color_hex(0x000000), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(status_bar, LV_OPA_20, LV_PART_MAIN);
  lv_obj_set_style_border_width(status_bar, 0, LV_PART_MAIN);
  lv_obj_set_style_radius(status_bar, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_hor(status_bar, 24, LV_PART_MAIN);

  lv_obj_t* time_label =
      CreateLabel(status_bar, "09:15", lv_color_hex(0xFFFFFF), Font24());
  if (time_label == nullptr) {
    lv_obj_delete(status_bar);
    return nullptr;
  }
  lv_obj_align(time_label, LV_ALIGN_LEFT_MID, 0, 0);

  lv_obj_t* battery_label =
      CreateLabel(status_bar, icon::kBatteryAndroid3, lv_color_hex(0xFFFFFF),
          MaterialIconFont28());
  if (battery_label == nullptr) {
    lv_obj_delete(status_bar);
    return nullptr;
  }
  lv_obj_align(battery_label, LV_ALIGN_RIGHT_MID, 0, 0);

  lv_obj_t* wifi_label =
      CreateLabel(status_bar, icon::kWifi, lv_color_hex(0xFFFFFF),
          MaterialIconFont28());
  if (wifi_label == nullptr) {
    lv_obj_delete(status_bar);
    return nullptr;
  }
  lv_obj_align_to(wifi_label, battery_label, LV_ALIGN_OUT_LEFT_MID, -6, 0);
  return status_bar;
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
  lv_obj_set_size(row, LV_PCT(100), kRowHeight);
  lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_set_style_border_width(row, 1, LV_PART_MAIN);
  lv_obj_set_style_border_side(row, LV_BORDER_SIDE_BOTTOM, LV_PART_MAIN);
  lv_obj_set_style_border_color(row, lv_color_hex(kRowDividerColor), LV_PART_MAIN);
  lv_obj_set_style_pad_all(row, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_left(row, 0, LV_PART_MAIN);

  const app::CitTestStatus status = GetRuntimeStatus(*state, entry);
  lv_obj_t* icon_label = CreateLabel(
      row, GetStatusIcon(status), GetStatusColor(status), GetStatusIconFont(status));
  if (icon_label == nullptr) {
    lv_obj_delete(row);
    return nullptr;
  }

  lv_obj_t* name_label =
      CreateLabel(row, entry.name, GetStatusColor(status), Font32());
  if (name_label == nullptr) {
    lv_obj_delete(row);
    return nullptr;
  }
  AlignStatusLabels(icon_label, name_label);

  state->rows[state->row_count] = {
      .entry = &entry,
      .icon_label = icon_label,
      .name_label = name_label,
  };
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
  lv_obj_add_event_cb(container, CitViewDeleteCallback, LV_EVENT_DELETE, state);

  lv_obj_remove_flag(container, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_color(
      container, lv_color_hex(kCitBackgroundColor), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(container, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_radius(container, 0, LV_PART_MAIN);
  lv_obj_set_style_border_width(container, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(container, 0, LV_PART_MAIN);
  lv_obj_set_size(container, config.width, config.height);
  lv_obj_align(container, LV_ALIGN_CENTER, 0, 0);

  if (CreateCitStatusBar(container, config.width) == nullptr) {
    lv_obj_delete(container);
    return nullptr;
  }

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
  lv_obj_set_size(list, config.width, config.height - kListTop);
  lv_obj_align(list, LV_ALIGN_TOP_MID, 0, kListTop);
  lv_obj_set_style_bg_color(list, lv_color_hex(kListBackgroundColor), LV_PART_MAIN);
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
