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
#include "draw/lv_draw_buf.h"
#include "draw/sw/lv_draw_sw_utils.h"

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

ppa_srm_rotation_angle_t ToPpaRotation(lv_display_rotation_t rotation) {
  switch (rotation) {
    case LV_DISPLAY_ROTATION_90:
      return PPA_SRM_ROTATION_ANGLE_90;
    case LV_DISPLAY_ROTATION_180:
      return PPA_SRM_ROTATION_ANGLE_180;
    case LV_DISPLAY_ROTATION_270:
      return PPA_SRM_ROTATION_ANGLE_270;
    default:
      return PPA_SRM_ROTATION_ANGLE_0;
  }
}

}  // namespace

LvglPort::~LvglPort() {
  if (rotation_buffer_ != nullptr) {
    heap_caps_free(rotation_buffer_);
    rotation_buffer_ = nullptr;
    rotation_buffer_size_ = 0;
  }
}

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
  lv_display_set_physical_resolution(
      lvgl_display_, screen_->ScreenWidth(), screen_->ScreenHeight());

  void* buffer = heap_caps_malloc(DrawBufferSize(), MALLOC_CAP_SPIRAM);
  if (buffer == nullptr) {
    LogMessage(LogLevel::kError, __FILE__, __LINE__,
        "LVGL draw buffer allocation failed\n");
    return false;
  }

  lv_display_set_buffers(lvgl_display_, buffer, nullptr, DrawBufferSize(),
      LV_DISPLAY_RENDER_MODE_PARTIAL);
  lv_display_set_flush_cb(lvgl_display_, FlushCallback);
  ppa_rotation_available_ = ppa_rotation_.Init();
  if (!ppa_rotation_available_) {
    LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
        "PPA rotation init failed, using LVGL software rotation\n");
  }

  input_device_ = lv_indev_create();
  if (input_device_ == nullptr) {
    return false;
  }
  lv_indev_set_type(input_device_, LV_INDEV_TYPE_POINTER);
  lv_indev_set_user_data(input_device_, this);
  lv_indev_set_read_cb(input_device_, TouchReadCallback);
  lv_indev_set_display(input_device_, lvgl_display_);

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

void LvglPort::SetDisplayRotation(int angle) {
  if (lvgl_display_ == nullptr) return;

  lv_display_rotation_t rotation = LV_DISPLAY_ROTATION_0;
  switch (angle) {
    case 90:  rotation = LV_DISPLAY_ROTATION_90;  break;
    case 180: rotation = LV_DISPLAY_ROTATION_180; break;
    case 270: rotation = LV_DISPLAY_ROTATION_270; break;
    default:  rotation = LV_DISPLAY_ROTATION_0;   break;
  }
  // 回调在 LVGL 线程，无需额外加锁
  lv_display_set_rotation(lvgl_display_, rotation);
  lv_obj_t* active_screen = lv_screen_active();
  if (active_screen != nullptr) {
    lv_obj_send_event(active_screen, LV_EVENT_REFRESH, nullptr);
  }
}

void LvglPort::FlushCallback(
    lv_display_t* lvgl_display, const lv_area_t* area, uint8_t* pixel_map) {
  auto* self = static_cast<LvglPort*>(lv_display_get_user_data(lvgl_display));
  if (self == nullptr || self->screen_ == nullptr) {
    lv_display_flush_ready(lvgl_display);
    return;
  }

  lv_area_t flush_area = *area;
  uint8_t* flush_pixels = pixel_map;
  if (!self->RotateFlushBuffer(
          lvgl_display, area, pixel_map, &flush_area, &flush_pixels)) {
    lv_display_flush_ready(lvgl_display);
    return;
  }

  const bool result = self->screen_->WriteScreenPixels(flush_area.x1,
      flush_area.y1, flush_area.x2 + 1, flush_area.y2 + 1, flush_pixels);
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

ppa_srm_color_mode_t LvglPort::PpaColorMode() const {
  switch (screen_->ScreenBitsPerPixel()) {
    case 24:
      return PPA_SRM_COLOR_MODE_RGB888;
    case 16:
    default:
      return PPA_SRM_COLOR_MODE_RGB565;
  }
}

size_t LvglPort::DrawBufferSize() const {
  const size_t bytes_per_pixel = screen_->ScreenBitsPerPixel() / 8;
  return static_cast<size_t>(screen_->ScreenWidth()) * screen_->ScreenHeight() *
         bytes_per_pixel;
}

void* LvglPort::EnsureRotationBuffer(size_t size) {
  if (rotation_buffer_ != nullptr && rotation_buffer_size_ >= size) {
    return rotation_buffer_;
  }

  if (rotation_buffer_ != nullptr) {
    heap_caps_free(rotation_buffer_);
    rotation_buffer_ = nullptr;
    rotation_buffer_size_ = 0;
  }

  const size_t cache_line_size = ppa_rotation_.CacheLineSize();
  const size_t alignment = cache_line_size > 0
      ? std::max<size_t>(cache_line_size, sizeof(void*))
      : sizeof(void*);
  const size_t aligned_size = AlignUp(size, alignment);
  rotation_buffer_ = heap_caps_aligned_alloc(
      alignment, aligned_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (rotation_buffer_ == nullptr) {
    rotation_buffer_ = heap_caps_aligned_alloc(
        alignment, aligned_size, MALLOC_CAP_8BIT);
  }
  if (rotation_buffer_ == nullptr) {
    LogMessage(LogLevel::kError, __FILE__, __LINE__,
        "LVGL rotation buffer allocation failed (%u bytes)\n",
        static_cast<unsigned>(aligned_size));
    return nullptr;
  }

  rotation_buffer_size_ = aligned_size;
  return rotation_buffer_;
}

bool LvglPort::RotateFlushBuffer(lv_display_t* lvgl_display,
    const lv_area_t* area, uint8_t* pixel_map, lv_area_t* rotated_area,
    uint8_t** rotated_pixel_map) {
  const lv_display_rotation_t rotation = lv_display_get_rotation(lvgl_display);
  if (rotation == LV_DISPLAY_ROTATION_0) {
    *rotated_area = *area;
    *rotated_pixel_map = pixel_map;
    return true;
  }

  *rotated_area = *area;
  lv_display_rotate_area(lvgl_display, rotated_area);

  const int32_t src_width = lv_area_get_width(area);
  const int32_t src_height = lv_area_get_height(area);
  const uint32_t src_stride =
      lv_draw_buf_width_to_stride(src_width, ColorFormat());
  const uint32_t dest_stride = lv_draw_buf_width_to_stride(
      lv_area_get_width(rotated_area), ColorFormat());
  const size_t dest_size =
      static_cast<size_t>(dest_stride) * lv_area_get_height(rotated_area);
  void* dest = EnsureRotationBuffer(dest_size);
  if (dest == nullptr) {
    return false;
  }

  bool ppa_done = false;
  if (ppa_rotation_available_) {
    PpaSrmImageConfig input;
    input.buffer = pixel_map;
    input.buffer_size = static_cast<size_t>(src_stride) * src_height;
    input.pic_width = static_cast<uint32_t>(src_width);
    input.pic_height = static_cast<uint32_t>(src_height);
    input.block_width = static_cast<uint32_t>(src_width);
    input.block_height = static_cast<uint32_t>(src_height);
    input.color_mode = PpaColorMode();

    PpaSrmImageConfig output;
    output.buffer = dest;
    output.buffer_size = rotation_buffer_size_;
    output.pic_width = static_cast<uint32_t>(lv_area_get_width(rotated_area));
    output.pic_height = static_cast<uint32_t>(lv_area_get_height(rotated_area));
    output.block_width = output.pic_width;
    output.block_height = output.pic_height;
    output.color_mode = PpaColorMode();

    PpaSrmTransformConfig transform;
    transform.rotation_angle = ToPpaRotation(rotation);
    ppa_done = ppa_rotation_.Transform(input, output, transform);
  }

  if (!ppa_done) {
    lv_draw_sw_rotate(pixel_map, dest, src_width, src_height, src_stride,
        dest_stride, rotation, ColorFormat());
  }

  *rotated_pixel_map = static_cast<uint8_t*>(dest);
  return true;
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
