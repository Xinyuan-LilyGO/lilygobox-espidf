/*
 * @Description: None
 * @Author: LILYGO_L
 * @Date: 2026-05-10 13:27:05
 * @LastEditTime: 2026-05-11 00:35:05
 * @License: GPL 3.0
 */
#include "ui/ui_manager.h"

#include <algorithm>
#include <cstring>

#include "app/app_catalog.h"
#include "ui/app_view_factory.h"
#include "ui/font/font_assets.h"
#include "ui/font/material_symbols_assets.h"
#include "ui/icon/icon_assets.h"

namespace lilygo_box::ui {
namespace {

constexpr int kStatusBarHeight = 48;
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
constexpr uint32_t kIconReleaseAnimationMs = 120;
constexpr int kIconLabelGap = 8;
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

bool IsId(const char* left, const char* right) {
  if (left == nullptr || right == nullptr) {
    return false;
  }
  return std::strcmp(left, right) == 0;
}

void SetTextStyle(lv_obj_t* object, lv_color_t color, const lv_font_t* font) {
  lv_obj_set_style_text_color(object, color, LV_PART_MAIN);
  lv_obj_set_style_text_font(object, font, LV_PART_MAIN);
}

const lv_font_t* Font22() { return &lvgl_font_google_sans_22; }

const lv_font_t* Font24() { return &lvgl_font_google_sans_24; }

const lv_font_t* MaterialIconFont28() { return &lvgl_font_material_symbols_28; }

const lv_font_t* MaterialIconFont56() { return &lvgl_font_material_symbols_56; }

const lv_font_t* HomeTimeFont() { return &lvgl_font_lineseedkr_rg_120; }

const lv_font_t* HomeDateFont() { return &lvgl_font_lineseedkr_th_60; }

int IconCellWidth() { return kAppIconSize + kIconCellExtraWidth; }

int IconCellHeight() {
  return kIconPressedMargin + kAppIconSize + kIconLabelGap + kIconLabelHeight;
}

int RowCount(size_t item_count, int columns) {
  if (item_count == 0) {
    return 0;
  }

  const int count = static_cast<int>(item_count);
  return (count + columns - 1) / columns;
}

int ScreenEdgeInset(int screen_width, int screen_height) {
  return std::max(8, std::min(screen_width, screen_height) / 25);
}

int ClampInset(
    int screen_width, int requested_inset, int columns, int cell_width) {
  const int minimum_width = columns * cell_width;
  if (screen_width <= minimum_width) {
    return 0;
  }

  return std::min(requested_inset, (screen_width - minimum_width) / 2);
}

int ColumnGap(int screen_width, int inset_x, int columns, int cell_width) {
  if (columns <= 1) {
    return 0;
  }

  const int used_width = 2 * inset_x + columns * cell_width;
  return std::max(0, (screen_width - used_width) / (columns - 1));
}

int HomeGridTop(int screen_height) { return screen_height * 35 / 100; }

constexpr lv_style_selector_t StyleSelector(lv_part_t part, lv_state_t state) {
  return static_cast<lv_style_selector_t>(part) |
         static_cast<lv_style_selector_t>(state);
}

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

void ClearThemePressedGrow(lv_obj_t* object) {
  lv_obj_set_style_transform_width(object, 0, LV_STATE_PRESSED);
  lv_obj_set_style_transform_height(object, 0, LV_STATE_PRESSED);
}

int PressedSize(int normal_size, int shrink_size) {
  return normal_size - shrink_size;
}

int CenterOffset(int normal_size, int pressed_size) {
  return (normal_size - pressed_size) / 2;
}

void SetIconButtonSize(lv_obj_t* object, int normal_size, int size) {
  const int offset = CenterOffset(normal_size, size);
  lv_obj_set_size(object, size, size);
  lv_obj_align(object, LV_ALIGN_TOP_MID, 0, kIconPressedMargin + offset);
}

void SetInnerImageSurfaceSize(lv_obj_t* surface, int size) {
  lv_obj_set_size(surface, size, size);
  lv_obj_center(surface);
}

void AppIconButtonSizeAnimCallback(void* object, int32_t size) {
  SetIconButtonSize(static_cast<lv_obj_t*>(object), kAppIconSize, size);
}

void DockIconButtonSizeAnimCallback(void* object, int32_t size) {
  SetIconButtonSize(static_cast<lv_obj_t*>(object), kDockIconSize, size);
}

void InnerImageSurfaceSizeAnimCallback(void* object, int32_t size) {
  SetInnerImageSurfaceSize(static_cast<lv_obj_t*>(object), size);
}

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

void UpdatePressedFeedback(
    lv_event_t* event, int normal_size, lv_anim_exec_xcb_t icon_callback) {
  const lv_event_code_t code = lv_event_get_code(event);
  const bool pressed = code == LV_EVENT_PRESSED;
  const bool released =
      code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST;
  if (!pressed && !released) {
    return;
  }

  lv_obj_t* object = lv_event_get_target_obj(event);
  if (object == nullptr) {
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

void AppIconPressedEventCallback(lv_event_t* event) {
  UpdatePressedFeedback(event, kAppIconSize, AppIconButtonSizeAnimCallback);
}

void DockIconPressedEventCallback(lv_event_t* event) {
  UpdatePressedFeedback(event, kDockIconSize, DockIconButtonSizeAnimCallback);
}

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

lv_obj_t* CreateLabel(lv_obj_t* parent, const char* text, lv_color_t color) {
  lv_obj_t* label = lv_label_create(parent);
  if (label == nullptr) {
    return nullptr;
  }

  lv_label_set_text(label, text);
  SetTextStyle(label, color, Font24());
  return label;
}

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

lv_obj_t* CreateDecorCircle(lv_obj_t* parent, int size, int x, int y,
    uint32_t color, lv_opa_t opacity) {
  lv_obj_t* circle = lv_obj_create(parent);
  if (circle == nullptr) {
    return nullptr;
  }

  lv_obj_remove_flag(circle, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(circle, size, size);
  lv_obj_set_style_radius(circle, size / 2, LV_PART_MAIN);
  lv_obj_set_style_bg_color(circle, lv_color_hex(color), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(circle, opacity, LV_PART_MAIN);
  lv_obj_set_style_border_width(circle, 0, LV_PART_MAIN);
  lv_obj_align(circle, LV_ALIGN_CENTER, x, y);
  return circle;
}

lv_obj_t* CreatePlantStem(lv_obj_t* parent, int x, int height, int bottom) {
  lv_obj_t* stem = lv_obj_create(parent);
  if (stem == nullptr) {
    return nullptr;
  }

  lv_obj_remove_flag(stem, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(stem, 3, height);
  lv_obj_set_style_radius(stem, 2, LV_PART_MAIN);
  lv_obj_set_style_bg_color(stem, lv_color_hex(0x806097), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(stem, 160, LV_PART_MAIN);
  lv_obj_set_style_border_width(stem, 0, LV_PART_MAIN);
  lv_obj_align(stem, LV_ALIGN_BOTTOM_MID, x, -bottom);
  return stem;
}

void CreateWallpaperObjects(lv_obj_t* parent) {
  CreatePlantStem(parent, -42, 184, 300);
  CreatePlantStem(parent, -12, 236, 300);
  CreatePlantStem(parent, 28, 172, 300);
  CreatePlantStem(parent, 58, 210, 300);

  CreateDecorCircle(parent, 28, -52, -238, 0xA9419D, 165);
  CreateDecorCircle(parent, 22, -26, -292, 0xCB5FB6, 155);
  CreateDecorCircle(parent, 24, 8, -338, 0x7A3A91, 150);
  CreateDecorCircle(parent, 30, 42, -268, 0xBD4DA4, 160);
  CreateDecorCircle(parent, 20, 72, -312, 0x8E3C96, 145);
  CreateDecorCircle(parent, 18, 36, -370, 0xD16FBF, 145);

  lv_obj_t* pot = lv_obj_create(parent);
  if (pot == nullptr) {
    return;
  }
  lv_obj_remove_flag(pot, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(pot, 154, 124);
  lv_obj_set_style_radius(pot, 26, LV_PART_MAIN);
  lv_obj_set_style_bg_color(pot, lv_color_hex(0xF6E4EA), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(pot, 238, LV_PART_MAIN);
  lv_obj_set_style_border_width(pot, 0, LV_PART_MAIN);
  lv_obj_align(pot, LV_ALIGN_BOTTOM_MID, 0, -218);

  lv_obj_t* pot_lip = lv_obj_create(parent);
  if (pot_lip == nullptr) {
    return;
  }
  lv_obj_remove_flag(pot_lip, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(pot_lip, 176, 24);
  lv_obj_set_style_radius(pot_lip, 12, LV_PART_MAIN);
  lv_obj_set_style_bg_color(pot_lip, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(pot_lip, 210, LV_PART_MAIN);
  lv_obj_set_style_border_width(pot_lip, 0, LV_PART_MAIN);
  lv_obj_align_to(pot_lip, pot, LV_ALIGN_OUT_TOP_MID, 0, 10);
}

}  // namespace

bool UiManager::Init(hal::ScreenDevice* screen) {
  if (screen == nullptr) {
    return false;
  }
  screen_ = screen;

  root_screen_ = lv_obj_create(nullptr);
  if (root_screen_ == nullptr) {
    return false;
  }

  lv_obj_remove_flag(root_screen_, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_color(root_screen_, lv_color_hex(0xD6A4D8), LV_PART_MAIN);
  lv_obj_set_style_border_width(root_screen_, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(root_screen_, 0, LV_PART_MAIN);
  lv_obj_add_event_cb(
      root_screen_, GestureEventCallback, LV_EVENT_GESTURE, this);

  CreateWallpaperObjects(root_screen_);
  launcher_container_ = CreateLauncher(root_screen_);
  if (launcher_container_ == nullptr) {
    return false;
  }

  lv_screen_load(root_screen_);
  return true;
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
  if (self == nullptr || self->active_view_container_ == nullptr) {
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

  self->ShowLauncher();
  lv_event_stop_bubbling(event);
}

void UiManager::PageScrollEventCallback(lv_event_t* event) {
  const lv_event_code_t code = lv_event_get_code(event);
  if (code != LV_EVENT_SCROLL && code != LV_EVENT_SCROLL_END) {
    return;
  }

  auto* self = static_cast<UiManager*>(lv_event_get_user_data(event));
  if (self == nullptr || self->page_scroller_ == nullptr ||
      self->screen_ == nullptr) {
    return;
  }

  const int scroll_x =
      static_cast<int>(lv_obj_get_scroll_x(self->page_scroller_));
  const size_t page_index = scroll_x >= self->screen_->width() / 2 ? 1 : 0;
  self->UpdatePageIndicator(page_index);
}

lv_obj_t* UiManager::CreateLauncher(lv_obj_t* parent) {
  lv_obj_t* launcher = lv_obj_create(parent);
  if (launcher == nullptr) {
    return nullptr;
  }

  lv_obj_remove_flag(launcher, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(launcher, LV_OBJ_FLAG_GESTURE_BUBBLE);
  MakeTransparent(launcher);
  lv_obj_set_size(launcher, screen_->width(), screen_->height());
  lv_obj_align(launcher, LV_ALIGN_CENTER, 0, 0);

  page_scroller_ = CreatePageScroller(launcher);
  if (page_scroller_ == nullptr) {
    lv_obj_delete(launcher);
    return nullptr;
  }

  if (CreateStatusBar(launcher) == nullptr || CreateDock(launcher) == nullptr) {
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
  lv_obj_set_size(scroller, screen_->width(), screen_->height());
  lv_obj_align(scroller, LV_ALIGN_CENTER, 0, 0);
  lv_obj_set_scroll_dir(scroller, LV_DIR_HOR);
  lv_obj_set_scroll_snap_x(scroller, LV_SCROLL_SNAP_CENTER);
  lv_obj_set_scrollbar_mode(scroller, LV_SCROLLBAR_MODE_OFF);
  lv_obj_add_flag(scroller, LV_OBJ_FLAG_SCROLL_ONE);
  lv_obj_remove_flag(scroller, LV_OBJ_FLAG_SCROLL_ELASTIC);
  lv_obj_add_event_cb(scroller, PageScrollEventCallback, LV_EVENT_SCROLL, this);
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
  lv_obj_set_size(home_page_, screen_->width(), screen_->height());
  lv_obj_set_pos(home_page_, 0, 0);

  reserved_page_ = lv_obj_create(scroller);
  if (reserved_page_ == nullptr) {
    lv_obj_delete(scroller);
    return nullptr;
  }
  lv_obj_remove_flag(reserved_page_, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(reserved_page_, LV_OBJ_FLAG_SNAPPABLE);
  MakeTransparent(reserved_page_);
  lv_obj_set_size(reserved_page_, screen_->width(), screen_->height());
  lv_obj_set_pos(reserved_page_, screen_->width(), 0);

  if (CreateClockGroup(home_page_) == nullptr ||
      CreateAppGrid(home_page_) == nullptr) {
    lv_obj_delete(scroller);
    return nullptr;
  }

  lv_obj_update_snap(scroller, LV_ANIM_OFF);
  return scroller;
}

lv_obj_t* UiManager::CreateStatusBar(lv_obj_t* parent) {
  lv_obj_t* status_bar = lv_obj_create(parent);
  if (status_bar == nullptr) {
    return nullptr;
  }

  lv_obj_remove_flag(status_bar, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(status_bar, LV_OBJ_FLAG_GESTURE_BUBBLE);
  MakeTransparent(status_bar);
  lv_obj_set_size(status_bar, LV_PCT(100), kStatusBarHeight);
  lv_obj_align(status_bar, LV_ALIGN_TOP_MID, 0, 0);
  lv_obj_set_style_pad_hor(status_bar, 24, LV_PART_MAIN);

  lv_obj_t* time_label =
      CreateLabel(status_bar, "09:15", lv_color_hex(0xFFFFFF));
  if (time_label == nullptr) {
    lv_obj_delete(status_bar);
    return nullptr;
  }
  SetTextStyle(time_label, lv_color_hex(0xFFFFFF), Font24());
  lv_obj_align(time_label, LV_ALIGN_LEFT_MID, 0, 0);

  lv_obj_t* battery_label =
      CreateLabel(status_bar, icon::kBatteryAndroid3, lv_color_hex(0xFFFFFF));
  if (battery_label == nullptr) {
    lv_obj_delete(status_bar);
    return nullptr;
  }
  SetTextStyle(battery_label, lv_color_hex(0xFFFFFF), MaterialIconFont28());
  lv_obj_align(battery_label, LV_ALIGN_RIGHT_MID, 0, 0);

  lv_obj_t* wifi_label =
      CreateLabel(status_bar, icon::kWifi, lv_color_hex(0xFFFFFF));
  if (wifi_label == nullptr) {
    lv_obj_delete(status_bar);
    return nullptr;
  }
  SetTextStyle(wifi_label, lv_color_hex(0xFFFFFF), MaterialIconFont28());
  lv_obj_align_to(wifi_label, battery_label, LV_ALIGN_OUT_LEFT_MID, -6, 0);
  return status_bar;
}

lv_obj_t* UiManager::CreateClockGroup(lv_obj_t* parent) {
  lv_obj_t* group = lv_obj_create(parent);
  if (group == nullptr) {
    return nullptr;
  }

  lv_obj_remove_flag(group, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(group, LV_OBJ_FLAG_GESTURE_BUBBLE);
  MakeTransparent(group);
  lv_obj_set_size(group, screen_->width() - 2 * kHorizontalPadding, 282);
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
  const int inset_x = ClampInset(screen_->width(),
      ScreenEdgeInset(screen_->width(), screen_->height()), kHomeAppColumns,
      cell_width);
  const int column_gap =
      ColumnGap(screen_->width(), inset_x, kHomeAppColumns, cell_width);

  lv_obj_set_size(grid, screen_->width(), grid_height);
  lv_obj_align(grid, LV_ALIGN_TOP_LEFT, 0, HomeGridTop(screen_->height()));

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
  lv_obj_set_size(dock, screen_->width(), kDockHeight);
  lv_obj_align(dock, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_obj_set_style_bg_color(dock, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(dock, 28, LV_PART_MAIN);
  lv_obj_set_style_border_width(dock, 0, LV_PART_MAIN);
  lv_obj_set_style_radius(dock, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(dock, 0, LV_PART_MAIN);

  const int cell_width = IconCellWidth();
  const int inset_x = ClampInset(screen_->width(),
      ScreenEdgeInset(screen_->width(), screen_->height()) + kDockInsetExtra,
      kDockColumns, cell_width);
  const int column_gap =
      ColumnGap(screen_->width(), inset_x, kDockColumns, cell_width);

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

  first_page_dot_ = CreateDecorCircle(indicator, 12, -10, 0, 0xFFFFFF, 240);
  second_page_dot_ = CreateDecorCircle(indicator, 12, 10, 0, 0xFFFFFF, 110);
  if (first_page_dot_ == nullptr || second_page_dot_ == nullptr) {
    lv_obj_delete(indicator);
    first_page_dot_ = nullptr;
    second_page_dot_ = nullptr;
    return nullptr;
  }

  return indicator;
}

bool UiManager::ShowAppView(const app::AppEntry& app_entry) {
  if (root_screen_ == nullptr || launcher_container_ == nullptr) {
    return false;
  }

  lv_obj_add_flag(launcher_container_, LV_OBJ_FLAG_HIDDEN);
  if (active_view_container_ != nullptr) {
    lv_obj_delete(active_view_container_);
    active_view_container_ = nullptr;
  }

  AppViewConfig config;
  config.width = screen_->width();
  config.height = screen_->height();
  config.screen = screen_;
  config.diagnostics = screen_->diagnostics_provider();
  config.back_callback = BackButtonEventCallback;
  config.back_context = this;

  active_view_container_ = CreateAppView(root_screen_, app_entry, config);
  if (active_view_container_ == nullptr) {
    lv_obj_remove_flag(launcher_container_, LV_OBJ_FLAG_HIDDEN);
    return false;
  }
  lv_obj_add_flag(active_view_container_, LV_OBJ_FLAG_GESTURE_BUBBLE);
  lv_obj_add_event_cb(
      active_view_container_, GestureEventCallback, LV_EVENT_GESTURE, this);
  return true;
}

void UiManager::ShowLauncher() {
  if (active_view_container_ != nullptr) {
    lv_obj_delete(active_view_container_);
    active_view_container_ = nullptr;
  }

  if (launcher_container_ != nullptr) {
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
