/*
 * @Description: 系统音量快捷浮层实现
 * @Author: LILYGO_L
 * @Date: 2026-08-17 00:00:00
 * @LastEditTime: 2026-08-17 00:00:00
 * @License: GPL 3.0
 */
#include "ui/widgets/volume_overlay.h"

#include <algorithm>
#include <utility>

#include "ui/haptic_feedback.h"
#include "ui/resources/fonts/font_assets.h"
#include "ui/resources/fonts/icon_assets.h"

namespace lilygo_box::ui {
namespace {

constexpr int kPanelMinWidth = 94;
constexpr int kPanelMaxWidth = 112;
constexpr int kPanelMinHeight = 290;
constexpr int kPanelMaxHeight = 380;
constexpr int kPanelBackgroundColor = 0xB7B7B7;
constexpr int kPanelFillColor = 0xF7F7F7;
constexpr int kActiveIconColor = 0xA6A6A6;
constexpr int kMutedIconColor = 0xF1F1F1;
constexpr int kPanelCornerRadius = 28;
constexpr int kSliderCornerRadius = kPanelCornerRadius + 1;
constexpr int kIconLabelHeight = 72;
constexpr int kPanelMinimumTop = 130;
constexpr uint32_t kSlideInDurationMs = 190;
constexpr uint32_t kSlideOutDurationMs = 170;
constexpr uint32_t kAutoHideDelayMs = 1800;
constexpr uint32_t kInputMonitorIntervalMs = 20;

}  // namespace

bool VolumeOverlay::Show(lv_obj_t* parent, int screen_width,
    int screen_height, int volume_percent, VolumeChangeCallback callback) {
  if (parent == nullptr || screen_width <= 0 || screen_height <= 0 ||
      !callback) {
    return false;
  }
  volume_change_callback_ = std::move(callback);
  volume_percent_ = std::clamp(volume_percent, 0, 100);
  if (volume_percent_ > 0) {
    last_nonzero_volume_percent_ = volume_percent_;
  }
  if (panel_ == nullptr && !Create(parent)) {
    return false;
  }

  UpdateLayout(screen_width, screen_height);
  UpdateVisuals();
  StartShowAnimation();
  RestartAutoHideTimer();
  return true;
}

void VolumeOverlay::Reset() {
  visual_state_initialized_ = false;
  pointer_pressed_ = false;
  if (auto_hide_timer_ != nullptr) {
    lv_timer_delete(auto_hide_timer_);
    auto_hide_timer_ = nullptr;
  }
  if (input_monitor_timer_ != nullptr) {
    lv_timer_delete(input_monitor_timer_);
    input_monitor_timer_ = nullptr;
  }
  if (panel_ != nullptr) {
    lv_anim_delete(panel_, SetPanelX);
    lv_obj_t* panel = panel_;
    panel_ = nullptr;
    slider_ = nullptr;
    icon_label_ = nullptr;
    lv_obj_delete(panel);
  }
  volume_change_callback_ = nullptr;
}

bool VolumeOverlay::Create(lv_obj_t* parent) {
  panel_ = lv_obj_create(parent);
  if (panel_ == nullptr) {
    return false;
  }
  lv_obj_remove_style_all(panel_);
  lv_obj_remove_flag(panel_, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(panel_, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_event_cb(
      panel_, PanelDeletedEventCallback, LV_EVENT_DELETE, this);

  slider_ = lv_slider_create(panel_);
  if (slider_ == nullptr) {
    Reset();
    return false;
  }
  lv_slider_set_range(slider_, 100, 0);
  lv_obj_set_style_bg_color(
      slider_, lv_color_hex(kPanelFillColor), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(slider_, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_border_width(slider_, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(slider_, 0, LV_PART_MAIN);
  lv_obj_set_style_radius(slider_, kSliderCornerRadius, LV_PART_MAIN);
  lv_obj_set_style_clip_corner(slider_, true, LV_PART_MAIN);
  lv_obj_set_style_shadow_width(slider_, 0, LV_PART_MAIN);
  lv_obj_set_style_bg_color(
      slider_, lv_color_hex(kPanelBackgroundColor), LV_PART_INDICATOR);
  lv_obj_set_style_bg_opa(slider_, LV_OPA_COVER, LV_PART_INDICATOR);
  lv_obj_set_style_radius(slider_, 0, LV_PART_INDICATOR);
  lv_obj_set_style_bg_opa(slider_, LV_OPA_TRANSP, LV_PART_KNOB);
  lv_obj_add_event_cb(
      slider_, SliderEventCallback, LV_EVENT_PRESSED, this);
  lv_obj_add_event_cb(
      slider_, SliderEventCallback, LV_EVENT_VALUE_CHANGED, this);
  lv_obj_add_event_cb(
      slider_, SliderEventCallback, LV_EVENT_RELEASED, this);
  lv_obj_add_event_cb(
      slider_, SliderEventCallback, LV_EVENT_PRESS_LOST, this);

  icon_label_ = lv_label_create(panel_);
  if (icon_label_ == nullptr) {
    Reset();
    return false;
  }
  lv_obj_add_flag(icon_label_, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_text_font(
      icon_label_, &lvgl_font_material_symbols_fill_56, LV_PART_MAIN);
  lv_obj_set_style_text_align(icon_label_, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
  lv_obj_add_event_cb(
      icon_label_, IconEventCallback, LV_EVENT_CLICKED, this);
  input_monitor_timer_ = lv_timer_create(
      InputMonitorTimerCallback, kInputMonitorIntervalMs, this);
  if (input_monitor_timer_ == nullptr) {
    Reset();
    return false;
  }
  visual_state_initialized_ = false;
  return true;
}

bool VolumeOverlay::ApplyVolume(int volume_percent, bool commit) {
  const int clamped_percent = std::clamp(volume_percent, 0, 100);
  const int previous_percent = volume_percent_;
  if (!volume_change_callback_ ||
      !volume_change_callback_(clamped_percent, commit)) {
    return false;
  }
  volume_percent_ = clamped_percent;
  if (volume_percent_ > 0) {
    last_nonzero_volume_percent_ = volume_percent_;
  }
  if (volume_percent_ != previous_percent &&
      (volume_percent_ == 0 || volume_percent_ == 100)) {
    PlayUiHapticFeedback();
  }
  UpdateVisuals();
  return true;
}

void VolumeOverlay::UpdateLayout(int screen_width, int screen_height) {
  screen_width_ = screen_width;
  screen_height_ = screen_height;
  panel_width_ = std::clamp(screen_width_ / 7,
      kPanelMinWidth, kPanelMaxWidth);
  panel_height_ = std::clamp(screen_height_ * 33 / 100,
      kPanelMinHeight, kPanelMaxHeight);
  const int edge_margin = std::max(26, screen_width_ / 24);
  const int top = std::max(kPanelMinimumTop, screen_height_ / 8);
  visible_x_ = screen_width_ - panel_width_ - edge_margin;
  hidden_x_ = screen_width_;

  lv_obj_set_size(panel_, panel_width_, panel_height_);
  lv_obj_set_y(panel_, top);
  lv_obj_set_size(slider_, panel_width_, panel_height_);
  lv_obj_set_pos(slider_, 0, 0);
  lv_obj_set_size(icon_label_, panel_width_, kIconLabelHeight);
  lv_obj_align(icon_label_, LV_ALIGN_BOTTOM_MID, 0, 0);
}

void VolumeOverlay::UpdateVisuals() {
  if (panel_ == nullptr || slider_ == nullptr || icon_label_ == nullptr) {
    return;
  }
  const int slider_value = 100 - volume_percent_;
  if (lv_slider_get_value(slider_) != slider_value) {
    updating_slider_ = true;
    lv_slider_set_value(slider_, slider_value, LV_ANIM_OFF);
    updating_slider_ = false;
  }

  const bool muted = volume_percent_ == 0;
  if (!visual_state_initialized_ || muted != muted_visual_) {
    lv_label_set_text(icon_label_, muted ? icon::kVolumeOff : icon::kVolumeUp);
    muted_visual_ = muted;
  }
  const int fill_height = panel_height_ * volume_percent_ / 100;
  const bool icon_on_fill = fill_height >= kIconLabelHeight * 3 / 4;
  if (!visual_state_initialized_ || icon_on_fill != icon_on_fill_visual_) {
    lv_obj_set_style_text_color(icon_label_,
        lv_color_hex(icon_on_fill ? kActiveIconColor : kMutedIconColor),
        LV_PART_MAIN);
    icon_on_fill_visual_ = icon_on_fill;
  }
  visual_state_initialized_ = true;
}

void VolumeOverlay::RestartAutoHideTimer() {
  if (auto_hide_timer_ != nullptr) {
    lv_timer_reset(auto_hide_timer_);
    return;
  }
  auto_hide_timer_ =
      lv_timer_create(AutoHideTimerCallback, kAutoHideDelayMs, this);
  if (auto_hide_timer_ != nullptr) {
    lv_timer_set_repeat_count(auto_hide_timer_, 1);
  }
}

void VolumeOverlay::StartShowAnimation() {
  if (panel_ == nullptr) {
    return;
  }
  lv_anim_delete(panel_, SetPanelX);
  const bool was_hidden = lv_obj_has_flag(panel_, LV_OBJ_FLAG_HIDDEN);
  lv_obj_clear_flag(panel_, LV_OBJ_FLAG_HIDDEN);
  lv_obj_move_to_index(panel_, -1);
  if (!was_hidden && lv_obj_get_x(panel_) == visible_x_) {
    return;
  }
  const int start_x = was_hidden ? hidden_x_ : lv_obj_get_x(panel_);
  lv_obj_set_x(panel_, start_x);

  lv_anim_t animation;
  lv_anim_init(&animation);
  lv_anim_set_var(&animation, panel_);
  lv_anim_set_values(&animation, start_x, visible_x_);
  lv_anim_set_duration(&animation, kSlideInDurationMs);
  lv_anim_set_path_cb(&animation, lv_anim_path_ease_out);
  lv_anim_set_exec_cb(&animation, SetPanelX);
  lv_anim_start(&animation);
}

void VolumeOverlay::StartHideAnimation() {
  if (panel_ == nullptr ||
      lv_obj_has_flag(panel_, LV_OBJ_FLAG_HIDDEN)) {
    return;
  }
  if (auto_hide_timer_ != nullptr) {
    lv_timer_delete(auto_hide_timer_);
    auto_hide_timer_ = nullptr;
  }
  lv_anim_delete(panel_, SetPanelX);

  lv_anim_t animation;
  lv_anim_init(&animation);
  lv_anim_set_var(&animation, panel_);
  lv_anim_set_user_data(&animation, this);
  lv_anim_set_values(&animation, lv_obj_get_x(panel_), hidden_x_);
  lv_anim_set_duration(&animation, kSlideOutDurationMs);
  lv_anim_set_path_cb(&animation, lv_anim_path_ease_in);
  lv_anim_set_exec_cb(&animation, SetPanelX);
  lv_anim_set_completed_cb(&animation, HideAnimationCompletedCallback);
  lv_anim_start(&animation);
}

void VolumeOverlay::HandleSliderEvent(lv_event_t* event) {
  if (event == nullptr || slider_ == nullptr || updating_slider_) {
    return;
  }
  const lv_event_code_t code = lv_event_get_code(event);
  if (code == LV_EVENT_PRESSED) {
    if (auto_hide_timer_ != nullptr) {
      lv_timer_delete(auto_hide_timer_);
      auto_hide_timer_ = nullptr;
    }
    return;
  }
  if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
    ApplyVolume(100 - static_cast<int>(lv_slider_get_value(slider_)), true);
    RestartAutoHideTimer();
    return;
  }
  if (code == LV_EVENT_VALUE_CHANGED) {
    ApplyVolume(100 - static_cast<int>(lv_slider_get_value(slider_)), false);
  }
}

void VolumeOverlay::HandlePointerInput() {
  if (panel_ == nullptr || lv_obj_has_flag(panel_, LV_OBJ_FLAG_HIDDEN)) {
    pointer_pressed_ = false;
    return;
  }

  bool pointer_pressed = false;
  bool pointer_outside_panel = false;
  lv_area_t panel_area;
  lv_obj_get_coords(panel_, &panel_area);
  for (lv_indev_t* input = lv_indev_get_next(nullptr); input != nullptr;
       input = lv_indev_get_next(input)) {
    if (lv_indev_get_type(input) != LV_INDEV_TYPE_POINTER ||
        lv_indev_get_state(input) != LV_INDEV_STATE_PRESSED) {
      continue;
    }
    pointer_pressed = true;
    lv_point_t point;
    lv_indev_get_point(input, &point);
    if (point.x < panel_area.x1 || point.x > panel_area.x2 ||
        point.y < panel_area.y1 || point.y > panel_area.y2) {
      pointer_outside_panel = true;
    }
  }

  if (pointer_pressed && !pointer_pressed_ && pointer_outside_panel) {
    StartHideAnimation();
  }
  pointer_pressed_ = pointer_pressed;
}

void VolumeOverlay::HandleIconClick() {
  const int target_percent = volume_percent_ == 0
      ? last_nonzero_volume_percent_ : 0;
  ApplyVolume(target_percent, true);
  RestartAutoHideTimer();
}

void VolumeOverlay::SliderEventCallback(lv_event_t* event) {
  auto* self =
      static_cast<VolumeOverlay*>(lv_event_get_user_data(event));
  if (self != nullptr) {
    self->HandleSliderEvent(event);
  }
}

void VolumeOverlay::IconEventCallback(lv_event_t* event) {
  auto* self =
      static_cast<VolumeOverlay*>(lv_event_get_user_data(event));
  if (self != nullptr && lv_event_get_code(event) == LV_EVENT_CLICKED) {
    self->HandleIconClick();
  }
}

void VolumeOverlay::PanelDeletedEventCallback(lv_event_t* event) {
  auto* self =
      static_cast<VolumeOverlay*>(lv_event_get_user_data(event));
  if (self == nullptr) {
    return;
  }
  if (self->auto_hide_timer_ != nullptr) {
    lv_timer_delete(self->auto_hide_timer_);
    self->auto_hide_timer_ = nullptr;
  }
  if (self->input_monitor_timer_ != nullptr) {
    lv_timer_delete(self->input_monitor_timer_);
    self->input_monitor_timer_ = nullptr;
  }
  self->pointer_pressed_ = false;
  self->panel_ = nullptr;
  self->slider_ = nullptr;
  self->icon_label_ = nullptr;
}

void VolumeOverlay::AutoHideTimerCallback(lv_timer_t* timer) {
  auto* self = static_cast<VolumeOverlay*>(lv_timer_get_user_data(timer));
  if (self != nullptr) {
    self->auto_hide_timer_ = nullptr;
    self->StartHideAnimation();
  }
}

void VolumeOverlay::InputMonitorTimerCallback(lv_timer_t* timer) {
  auto* self = static_cast<VolumeOverlay*>(lv_timer_get_user_data(timer));
  if (self != nullptr) {
    self->HandlePointerInput();
  }
}

void VolumeOverlay::HideAnimationCompletedCallback(lv_anim_t* animation) {
  auto* self =
      static_cast<VolumeOverlay*>(lv_anim_get_user_data(animation));
  if (self != nullptr && self->panel_ != nullptr) {
    lv_obj_add_flag(self->panel_, LV_OBJ_FLAG_HIDDEN);
  }
}

void VolumeOverlay::SetPanelX(void* object, int32_t x) {
  if (object != nullptr) {
    lv_obj_set_x(static_cast<lv_obj_t*>(object), x);
  }
}

}  // namespace lilygo_box::ui
