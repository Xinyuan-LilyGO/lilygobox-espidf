/*
 * @Description: None
 * @Author: LILYGO_L
 * @Date: 2026-05-10 13:27:05
 * @LastEditTime: 2026-05-11 00:05:30
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

namespace lilygo_box::ui {
namespace {

constexpr int kViewRadius = 0;
constexpr int kButtonRadius = 12;
constexpr int kViewPadding = 22;
constexpr int kBackButtonWidth = 190;
constexpr int kBackButtonHeight = 70;
constexpr int kStatusRowHeight = 54;
constexpr int kStatusRowGap = 8;
constexpr int kCitRefreshPeriodMs = 200;
constexpr int kDiagnosticsRefreshPeriodMs = 1000;

struct CitStatusRow {
  const app::CitTestEntry* entry = nullptr;
  lv_obj_t* status_label = nullptr;
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

const lv_font_t* Font24() { return &lvgl_font_google_sans_24; }

const lv_font_t* Font48() { return &lvgl_font_google_sans_48; }

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

lv_obj_t* CreateBackButton(lv_obj_t* parent, const AppViewConfig& config) {
  lv_obj_t* button = lv_button_create(parent);
  if (button == nullptr) {
    return nullptr;
  }

  lv_obj_set_size(button, kBackButtonWidth, kBackButtonHeight);
  lv_obj_set_style_radius(button, kButtonRadius, LV_PART_MAIN);
  lv_obj_set_style_bg_color(button, lv_color_hex(0x2D3C48), LV_PART_MAIN);
  lv_obj_align(button, LV_ALIGN_BOTTOM_LEFT, 0, 0);

  if (config.back_callback != nullptr) {
    lv_obj_add_event_cb(
        button, config.back_callback, LV_EVENT_CLICKED, config.back_context);
  }

  lv_obj_t* label =
      CreateLabel(button, "Back", lv_color_hex(0xF5F7FA), Font24());
  if (label == nullptr) {
    lv_obj_delete(button);
    return nullptr;
  }

  lv_obj_center(label);
  return button;
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
      return lv_color_hex(0x51D88A);
    case app::CitTestStatus::kWaiting:
      return lv_color_hex(0x5FB3FF);
    case app::CitTestStatus::kPending:
      return lv_color_hex(0xF4C95D);
  }
  return lv_color_hex(0xF4C95D);
}

int CalculateRowsHeight(size_t entry_count) {
  if (entry_count == 0) {
    return 0;
  }
  const size_t row_height = kStatusRowHeight * entry_count;
  const size_t gap_height = kStatusRowGap * (entry_count - 1);
  return static_cast<int>(row_height + gap_height);
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

void UpdateStatusLabel(lv_obj_t* label, app::CitTestStatus status) {
  if (label == nullptr) {
    return;
  }
  lv_label_set_text(label, app::GetCitTestStatusText(status));
  lv_obj_set_style_text_color(label, GetStatusColor(status), LV_PART_MAIN);
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
    UpdateStatusLabel(row.status_label, status);
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
  lv_obj_set_size(row, LV_PCT(100), kStatusRowHeight);
  lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_set_style_border_width(row, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(row, 0, LV_PART_MAIN);
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER,
      LV_FLEX_ALIGN_CENTER);

  lv_obj_t* name_label =
      CreateLabel(row, entry.name, lv_color_hex(0xF5F7FA), Font24());
  const app::CitTestStatus status = GetRuntimeStatus(*state, entry);
  lv_obj_t* status_label = CreateLabel(
      row, app::GetCitTestStatusText(status), GetStatusColor(status), Font24());
  if (name_label == nullptr || status_label == nullptr) {
    lv_obj_delete(row);
    return nullptr;
  }

  state->rows[state->row_count] = {
      .entry = &entry,
      .status_label = status_label,
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
  lv_obj_set_style_bg_color(container, lv_color_hex(0x121820), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(container, 245, LV_PART_MAIN);
  lv_obj_set_style_radius(container, kViewRadius, LV_PART_MAIN);
  lv_obj_set_style_border_width(container, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(container, kViewPadding, LV_PART_MAIN);
  lv_obj_set_size(container, config.width, config.height);
  lv_obj_align(container, LV_ALIGN_CENTER, 0, 0);

  lv_obj_t* title =
      CreateLabel(container, app_entry.title, lv_color_hex(0xF5F7FA), Font48());
  if (title == nullptr) {
    lv_obj_delete(container);
    return nullptr;
  }
  lv_obj_align(title, LV_ALIGN_TOP_LEFT, 0, 0);

  lv_obj_t* subtitle = CreateLabel(
      container, app_entry.subtitle, lv_color_hex(0xAAB2BD), Font24());
  if (subtitle == nullptr) {
    lv_obj_delete(container);
    return nullptr;
  }
  lv_obj_align_to(subtitle, title, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 12);

  const app::CitTestCatalog& catalog = app::GetCitTestCatalog();
  lv_obj_t* row_group = lv_obj_create(container);
  if (row_group == nullptr) {
    lv_obj_delete(container);
    return nullptr;
  }
  lv_obj_remove_flag(row_group, LV_OBJ_FLAG_SCROLLABLE);
  const int rows_height = CalculateRowsHeight(catalog.entry_count);
  lv_obj_set_size(row_group, LV_PCT(100), rows_height);
  lv_obj_set_style_bg_opa(row_group, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_set_style_border_width(row_group, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(row_group, 0, LV_PART_MAIN);
  lv_obj_set_flex_flow(row_group, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(row_group, kStatusRowGap, LV_PART_MAIN);
  lv_obj_align_to(row_group, subtitle, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 18);

  if (!AddCitRows(row_group, catalog, state)) {
    lv_obj_delete(container);
    return nullptr;
  }

  if (CreateBackButton(container, config) == nullptr) {
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
