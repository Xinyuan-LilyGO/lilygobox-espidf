/*
 * @Description: None
 * @Author: LILYGO_L
 * @Date: 2026-05-10 13:27:05
 * @LastEditTime: 2026-05-10 23:41:00
 * @License: GPL 3.0
 */
#include "hal/lvgl_port.h"

#include <algorithm>

#include "base/logger.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace lilygo_box::hal {
namespace {

/**
 * @brief 判断触摸点坐标是否落在当前屏幕有效范围内。
 * @param point 触摸点信息。
 * @param screen_width 屏幕宽度。
 * @param screen_height 屏幕高度。
 * @return 触摸点坐标有效返回 true，否则返回 false。
 */
bool IsValidTouchPoint(const TouchPoint& point, int screen_width,
    int screen_height) {
  return point.x >= 0 && point.x < screen_width && point.y >= 0 &&
         point.y < screen_height;
}

}  // namespace

bool LvglPort::Init(ScreenProvider* screen) {
  if (screen == nullptr) {
    return false;
  }

  screen_ = screen;
  lv_init();

  lvgl_display_ =
      lv_display_create(screen_->ScreenWidth(), screen_->ScreenHeight());
  if (lvgl_display_ == nullptr) {
    return false;
  }

  lv_display_set_user_data(lvgl_display_, this);
  lv_display_set_color_format(lvgl_display_, ColorFormat());

  void* buffer = heap_caps_malloc(DrawBufferSize(), MALLOC_CAP_SPIRAM);
  if (buffer == nullptr) {
    LogMessage(LogLevel::kError, __FILE__, __LINE__,
        "LVGL draw buffer allocation failed\n");
    return false;
  }

  lv_display_set_buffers(lvgl_display_, buffer, nullptr, DrawBufferSize(),
      LV_DISPLAY_RENDER_MODE_PARTIAL);
  lv_display_set_flush_cb(lvgl_display_, FlushCallback);

  input_device_ = lv_indev_create();
  if (input_device_ == nullptr) {
    return false;
  }
  lv_indev_set_type(input_device_, LV_INDEV_TYPE_POINTER);
  lv_indev_set_user_data(input_device_, this);
  lv_indev_set_read_cb(input_device_, TouchReadCallback);

  if (!screen_->RegisterScreenFlushReadyCallback(FlushReadyCallback, this)) {
    LogMessage(LogLevel::kError, __FILE__, __LINE__,
        "RegisterScreenFlushReadyCallback failed\n");
    return false;
  }

  const esp_timer_create_args_t tick_timer_args = {
      .callback = TickCallback,
      .arg = this,
      .dispatch_method = ESP_TIMER_TASK,
      .name = "lvgl_tick",
      .skip_unhandled_events = false,
  };
  esp_timer_handle_t tick_timer = nullptr;
  int result = esp_timer_create(&tick_timer_args, &tick_timer);
  if (result != 0) {
    LogMessage(LogLevel::kError, __FILE__, __LINE__,
        "esp_timer_create failed (error code: %#X)\n", result);
    return false;
  }

  result = esp_timer_start_periodic(tick_timer, kLvglTickPeriodMs * 1000);
  if (result != 0) {
    LogMessage(LogLevel::kError, __FILE__, __LINE__,
        "esp_timer_start_periodic failed (error code: %#X)\n", result);
    return false;
  }
  return true;
}

bool LvglPort::Start() {
  const BaseType_t result = xTaskCreate(
      TaskEntry, "lvgl", kLvglTaskStackBytes, this, kLvglTaskPriority, nullptr);
  return result == pdPASS;
}

/**
 * @brief 判断当前 LVGL 输入是否带有硬件边缘触摸标志。
 * @return 当前输入来自硬件边缘触摸检测返回 true，否则返回 false。
 */
bool LvglPort::ActiveInputEdgeTouch() {
  lv_indev_t* indev = lv_indev_active();
  if (indev == nullptr) {
    return false;
  }

  auto* self = static_cast<LvglPort*>(lv_indev_get_user_data(indev));
  return self != nullptr && self->active_edge_touch_flag_;
}

void LvglPort::SetInputBlocked(bool blocked) {
  input_blocked_.store(blocked);
  active_edge_touch_flag_ = false;
  pending_edge_touch_flag_ = false;
  has_last_touch_point_ = false;
}

void LvglPort::Lock() { _lock_acquire(&lock_); }

void LvglPort::Unlock() { _lock_release(&lock_); }

void LvglPort::FlushCallback(
    lv_display_t* lvgl_display, const lv_area_t* area, uint8_t* pixel_map) {
  auto* self = static_cast<LvglPort*>(lv_display_get_user_data(lvgl_display));
  if (self == nullptr || self->screen_ == nullptr) {
    lv_display_flush_ready(lvgl_display);
    return;
  }

  const bool result = self->screen_->WriteScreenPixels(
      area->x1, area->y1, area->x2 + 1, area->y2 + 1, pixel_map);
  if (!result) {
    LogMessage(
        LogLevel::kError, __FILE__, __LINE__, "WriteScreenPixels failed\n");
    lv_display_flush_ready(lvgl_display);
  }
}

void LvglPort::FlushReadyCallback(void* context) {
  auto* self = static_cast<LvglPort*>(context);
  if (self != nullptr && self->lvgl_display_ != nullptr) {
    lv_display_flush_ready(self->lvgl_display_);
  }
}

void LvglPort::TouchReadCallback(lv_indev_t* indev, lv_indev_data_t* data) {
  auto* self = static_cast<LvglPort*>(lv_indev_get_user_data(indev));
  if (self == nullptr || self->screen_ == nullptr) {
    data->state = LV_INDEV_STATE_REL;
    return;
  }

  if (self->input_blocked_.load()) {
    self->active_edge_touch_flag_ = false;
    self->pending_edge_touch_flag_ = false;
    self->has_last_touch_point_ = false;
    data->state = LV_INDEV_STATE_REL;
    return;
  }

  TouchPoint point;
  const bool result = self->screen_->ReadScreenTouch(&point);
  if (result) {
    const bool valid_point =
        IsValidTouchPoint(point, self->screen_->ScreenWidth(),
            self->screen_->ScreenHeight());
    if (!valid_point) {
      if (point.edge_touch_flag) {
        self->pending_edge_touch_flag_ = true;
      }
      if (!self->has_last_touch_point_) {
        data->state = LV_INDEV_STATE_REL;
        return;
      }

      self->active_edge_touch_flag_ = self->active_edge_touch_flag_ ||
                                      self->pending_edge_touch_flag_ ||
                                      point.edge_touch_flag;
      data->state = LV_INDEV_STATE_PR;
      data->point = self->last_touch_point_;
      return;
    }

    self->active_edge_touch_flag_ =
        point.edge_touch_flag || self->pending_edge_touch_flag_;
    self->pending_edge_touch_flag_ = false;
    self->has_last_touch_point_ = true;
    self->last_touch_point_.x = point.x;
    self->last_touch_point_.y = point.y;

    data->state = LV_INDEV_STATE_PR;
    data->point.x = point.x;
    data->point.y = point.y;
    return;
  }

  self->active_edge_touch_flag_ = false;
  self->pending_edge_touch_flag_ = false;
  self->has_last_touch_point_ = false;
  data->state = LV_INDEV_STATE_REL;
}

void LvglPort::TickCallback(void*) { lv_tick_inc(kLvglTickPeriodMs); }

void LvglPort::TaskEntry(void* arg) {
  auto* self = static_cast<LvglPort*>(arg);
  self->TaskLoop();
}

lv_color_format_t LvglPort::ColorFormat() const {
  switch (screen_->ScreenBitsPerPixel()) {
    case 16:
      return LV_COLOR_FORMAT_RGB565;
    case 24:
      return LV_COLOR_FORMAT_RGB888;
    default:
      LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
          "Unsupported color depth %d, falling back to RGB565\n",
          screen_->ScreenBitsPerPixel());
      return LV_COLOR_FORMAT_RGB565;
  }
}

size_t LvglPort::DrawBufferSize() const {
  const size_t bytes_per_pixel = screen_->ScreenBitsPerPixel() / 8;
  return static_cast<size_t>(screen_->ScreenWidth()) * screen_->ScreenHeight() *
         bytes_per_pixel;
}

void LvglPort::TaskLoop() {
  while (true) {
    Lock();
    uint32_t delay_ms = lv_timer_handler();
    Unlock();

    delay_ms = std::max(delay_ms, kMinimumHandlerDelayMs);
    vTaskDelay(pdMS_TO_TICKS(delay_ms));
  }
}

}  // namespace lilygo_box::hal
