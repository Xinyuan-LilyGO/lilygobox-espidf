/*
 * @Description: LVGL 显示刷新、触摸读取与任务循环实现
 * @Author: LILYGO_L
 * @Date: 2026-05-10 13:27:05
 * @LastEditTime: 2026-07-17 17:57:58
 * @License: GPL 3.0
 */
#include "hal/lvgl_port.h"

#include <algorithm>

#include "base/logger.h"
#include "esp_err.h"
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

  screen_transition_mutex_ =
      xSemaphoreCreateMutexStatic(&screen_transition_mutex_buffer_);
  if (screen_transition_mutex_ == nullptr) {
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

  const ScreenProviderDisplayCallbacks display_callbacks = {
      .flush_ready_callback = FlushReadyCallback,
      .refresh_done_callback = RefreshDoneCallback,
      .callback_context = this,
  };
  if (!screen_->RegisterScreenDisplayCallbacks(display_callbacks)) {
    LogMessage(LogLevel::kError, __FILE__, __LINE__,
        "RegisterScreenDisplayCallbacks failed\n");
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
  esp_err_t result = esp_timer_create(&tick_timer_args, &tick_timer);
  if (result != ESP_OK) {
    LogMessage(LogLevel::kError, __FILE__, __LINE__,
        "esp_timer_create failed: %s (%#X)\n", esp_err_to_name(result),
        static_cast<unsigned>(result));
    return false;
  }

  result = esp_timer_start_periodic(tick_timer, kLvglTickPeriodMs * 1000);
  if (result != ESP_OK) {
    LogMessage(LogLevel::kError, __FILE__, __LINE__,
        "esp_timer_start_periodic failed: %s (%#X)\n",
        esp_err_to_name(result), static_cast<unsigned>(result));
    return false;
  }
  return true;
}

bool LvglPort::Start() {
  const BaseType_t result = xTaskCreate(
      TaskEntry, "lvgl", kLvglTaskStackBytes, this, kLvglTaskPriority,
      &task_handle_);
  return result == pdPASS;
}

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
  portENTER_CRITICAL(&touch_cache_lock_);
  cached_touch_point_count_ = 0;
  portEXIT_CRITICAL(&touch_cache_lock_);
}

bool LvglPort::IsInputBlocked() const {
  return input_blocked_.load(std::memory_order_acquire) ||
         sleep_input_block_count_.load(std::memory_order_acquire) > 0;
}

void LvglPort::SetTouchReadMode(TouchReadMode mode) {
  if (touch_read_mode_.exchange(mode, std::memory_order_acq_rel) == mode) {
    return;
  }

  active_edge_touch_flag_ = false;
  pending_edge_touch_flag_ = false;
  has_last_touch_point_ = false;
  portENTER_CRITICAL(&touch_cache_lock_);
  cached_touch_point_count_ = 0;
  portEXIT_CRITICAL(&touch_cache_lock_);
}

bool LvglPort::ReadTouch(TouchPoint* point, bool* access_available) {
  if (access_available != nullptr) {
    *access_available = false;
  }
  if (point == nullptr || screen_ == nullptr || IsDisplayFlushPaused()) {
    return false;
  }

  if (IsInputBlocked()) {
    size_t point_count = 0;
    return ReadHardwareTouch(TouchReadMode::kSinglePoint, point, 1,
        &point_count, false, access_available);
  }
  if (access_available != nullptr) {
    *access_available = true;
  }
  portENTER_CRITICAL(&touch_cache_lock_);
  const bool touched = cached_touch_point_count_ > 0;
  if (touched) {
    *point = cached_touch_points_[0];
  }
  portEXIT_CRITICAL(&touch_cache_lock_);
  if (touched) {
    point->edge_touch_flag =
        active_edge_touch_flag_.load(std::memory_order_acquire);
  }
  return touched;
}

bool LvglPort::ReadTouchPoints(
    TouchPoint* points, size_t max_points, size_t* point_count) {
  if (point_count != nullptr) {
    *point_count = 0;
  }
  if (points == nullptr || max_points == 0 || point_count == nullptr) {
    return false;
  }

  portENTER_CRITICAL(&touch_cache_lock_);
  const size_t count = std::min(max_points, cached_touch_point_count_);
  for (size_t i = 0; i < count; ++i) {
    points[i] = cached_touch_points_[i];
  }
  portEXIT_CRITICAL(&touch_cache_lock_);
  *point_count = count;
  return count > 0;
}

bool LvglPort::ReadHardwareTouch(TouchReadMode mode, TouchPoint* points,
    size_t max_points, size_t* point_count, bool input_must_be_enabled,
    bool* access_available) {
  if (access_available != nullptr) {
    *access_available = false;
  }
  if (point_count != nullptr) {
    *point_count = 0;
  }
  if (points == nullptr || max_points == 0 || point_count == nullptr ||
      screen_ == nullptr || !TryBeginScreenTransition()) {
    return false;
  }

  const bool can_access =
      !IsDisplayFlushPaused() && (!input_must_be_enabled || !IsInputBlocked());
  if (access_available != nullptr) {
    *access_available = can_access;
  }
  bool touched = false;
  if (can_access) {
    if (mode == TouchReadMode::kMultiPoint) {
      touched = screen_->ReadScreenTouchPoints(points, max_points, point_count);
    } else {
      touched = screen_->ReadScreenTouch(&points[0]);
      *point_count = touched ? 1 : 0;
    }
  }
  EndScreenTransition();
  return touched;
}

void LvglPort::AcquireSleepInputBlock() {
  sleep_input_block_count_.fetch_add(1, std::memory_order_acq_rel);
  active_edge_touch_flag_ = false;
  pending_edge_touch_flag_ = false;
  has_last_touch_point_ = false;
  portENTER_CRITICAL(&touch_cache_lock_);
  cached_touch_point_count_ = 0;
  portEXIT_CRITICAL(&touch_cache_lock_);
}

void LvglPort::ReleaseSleepInputBlock() {
  uint32_t count = sleep_input_block_count_.load(std::memory_order_acquire);
  while (count > 0 &&
      !sleep_input_block_count_.compare_exchange_weak(count, count - 1,
          std::memory_order_acq_rel, std::memory_order_acquire)) {
  }
}

bool LvglPort::BeginScreenTransition() {
  return screen_transition_mutex_ != nullptr &&
         xSemaphoreTake(screen_transition_mutex_, portMAX_DELAY) == pdTRUE;
}

bool LvglPort::TryBeginScreenTransition(TickType_t timeout_ticks) {
  return screen_transition_mutex_ != nullptr &&
         xSemaphoreTake(screen_transition_mutex_, timeout_ticks) == pdTRUE;
}

void LvglPort::EndScreenTransition() {
  if (screen_transition_mutex_ != nullptr) {
    xSemaphoreGive(screen_transition_mutex_);
  }
}

bool LvglPort::PauseDisplayFlush() {
  display_flush_pause_count_.fetch_add(1, std::memory_order_seq_cst);
  const TickType_t poll_ticks =
      std::max<TickType_t>(1, pdMS_TO_TICKS(1));
  const TickType_t timeout_ticks =
      std::max<TickType_t>(1, pdMS_TO_TICKS(kFlushPauseTimeoutMs));
  const TickType_t start_ticks = xTaskGetTickCount();
  while (xTaskGetTickCount() - start_ticks < timeout_ticks) {
    if (!display_flush_in_progress_.load(std::memory_order_seq_cst)) {
      return true;
    }
    vTaskDelay(poll_ticks);
  }
  if (!display_flush_in_progress_.load(std::memory_order_seq_cst)) {
    return true;
  }
  ResumeDisplayFlush();
  LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
      "Pause LVGL display flush timed out\n");
  return false;
}

void LvglPort::ResumeDisplayFlush() {
  ResumeDisplayFlushInternal();
}

bool LvglPort::ResumeDisplayFlushAndWaitForRefresh() {
  const uint32_t generation = ResumeDisplayFlushInternal();
  if (generation == 0) {
    return false;
  }

  const TickType_t poll_ticks =
      std::max<TickType_t>(1, pdMS_TO_TICKS(1));
  const TickType_t timeout_ticks =
      std::max<TickType_t>(1, pdMS_TO_TICKS(kDisplayRefreshTimeoutMs));
  const TickType_t start_ticks = xTaskGetTickCount();
  while (xTaskGetTickCount() - start_ticks < timeout_ticks) {
    if (display_refresh_completed_generation_.load(
            std::memory_order_acquire) == generation) {
      return true;
    }
    vTaskDelay(poll_ticks);
  }
  if (display_refresh_completed_generation_.load(
          std::memory_order_acquire) == generation) {
    return true;
  }
  LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
      "Wait for LVGL display refresh timed out\n");
  return false;
}

uint32_t LvglPort::ResumeDisplayFlushInternal() {
  uint32_t count =
      display_flush_pause_count_.load(std::memory_order_seq_cst);
  while (count > 0) {
    if (display_flush_pause_count_.compare_exchange_weak(count, count - 1,
            std::memory_order_seq_cst, std::memory_order_seq_cst)) {
      if (count == 1) {
        const uint32_t generation =
            display_refresh_request_generation_.fetch_add(
                1, std::memory_order_acq_rel) + 1;
        display_refresh_rendering_generation_.store(
            0, std::memory_order_release);
        display_refresh_scanout_pending_.store(
            false, std::memory_order_release);
        display_refresh_failed_.store(false, std::memory_order_release);
        display_refresh_requested_.store(true, std::memory_order_release);
        if (task_handle_ != nullptr) {
          xTaskNotifyGive(task_handle_);
        }
        return generation;
      }
      return 0;
    }
  }
  return 0;
}

bool LvglPort::IsDisplayFlushPaused() const {
  return display_flush_pause_count_.load(std::memory_order_seq_cst) > 0;
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
  if (self->display_flush_pause_count_.load(
          std::memory_order_seq_cst) > 0) {
    self->display_refresh_failed_.store(true, std::memory_order_release);
    lv_display_flush_ready(lvgl_display);
    return;
  }

  self->display_flush_in_progress_.store(true, std::memory_order_seq_cst);
  if (self->display_flush_pause_count_.load(
          std::memory_order_seq_cst) > 0) {
    self->display_refresh_failed_.store(true, std::memory_order_release);
    self->display_flush_in_progress_.store(
        false, std::memory_order_seq_cst);
    lv_display_flush_ready(lvgl_display);
    return;
  }

  lv_area_t flush_area = *area;
  uint8_t* flush_pixels = pixel_map;
  if (!self->RotateFlushBuffer(
          lvgl_display, area, pixel_map, &flush_area, &flush_pixels)) {
    self->display_refresh_failed_.store(true, std::memory_order_release);
    self->display_flush_in_progress_.store(
        false, std::memory_order_seq_cst);
    lv_display_flush_ready(lvgl_display);
    return;
  }
  if (self->display_flush_pause_count_.load(
          std::memory_order_seq_cst) > 0) {
    self->display_refresh_failed_.store(true, std::memory_order_release);
    self->display_flush_in_progress_.store(
        false, std::memory_order_seq_cst);
    lv_display_flush_ready(lvgl_display);
    return;
  }

  const bool result = self->screen_->WriteScreenPixels(flush_area.x1,
      flush_area.y1, flush_area.x2 + 1, flush_area.y2 + 1, flush_pixels);
  if (!result) {
    self->display_refresh_failed_.store(true, std::memory_order_release);
    self->display_flush_in_progress_.store(
        false, std::memory_order_seq_cst);
    LogMessage(
        LogLevel::kError, __FILE__, __LINE__, "WriteScreenPixels failed\n");
    lv_display_flush_ready(lvgl_display);
  }
}

void LvglPort::FlushReadyCallback(void* context) {
  auto* self = static_cast<LvglPort*>(context);
  if (self != nullptr && self->lvgl_display_ != nullptr) {
    const bool is_last_flush =
        lv_display_flush_is_last(self->lvgl_display_);
    if (is_last_flush &&
        !self->display_refresh_failed_.load(std::memory_order_acquire) &&
        self->display_refresh_rendering_generation_.load(
            std::memory_order_acquire) != 0) {
      self->display_refresh_scanout_pending_.store(
          true, std::memory_order_release);
    }
    self->display_flush_in_progress_.store(
        false, std::memory_order_seq_cst);
    lv_display_flush_ready(self->lvgl_display_);
  }
}

void LvglPort::RefreshDoneCallback(void* context) {
  auto* self = static_cast<LvglPort*>(context);
  if (self == nullptr ||
      self->display_refresh_failed_.load(std::memory_order_acquire)) {
    return;
  }

  if (!self->display_refresh_scanout_pending_.exchange(
          false, std::memory_order_acq_rel)) {
    return;
  }

  const uint32_t generation =
      self->display_refresh_rendering_generation_.exchange(
          0, std::memory_order_acq_rel);
  if (generation != 0) {
    self->display_refresh_completed_generation_.store(
        generation, std::memory_order_release);
  }
}

void LvglPort::TouchReadCallback(lv_indev_t* indev, lv_indev_data_t* data) {
  auto* self = static_cast<LvglPort*>(lv_indev_get_user_data(indev));
  if (self == nullptr || self->screen_ == nullptr) {
    data->state = LV_INDEV_STATE_REL;
    return;
  }

  if (self->input_blocked_.load() ||
      self->sleep_input_block_count_.load(std::memory_order_acquire) > 0) {
    self->active_edge_touch_flag_ = false;
    self->pending_edge_touch_flag_ = false;
    self->has_last_touch_point_ = false;
    portENTER_CRITICAL(&self->touch_cache_lock_);
    self->cached_touch_point_count_ = 0;
    portEXIT_CRITICAL(&self->touch_cache_lock_);
    data->state = LV_INDEV_STATE_REL;
    return;
  }

  TouchPoint point = {};
  std::array<TouchPoint, kMaxTouchPointCount> sampled_points = {};
  size_t sampled_point_count = 0;
  size_t valid_point_count = 0;
  bool access_available = false;
  const TouchReadMode read_mode =
      self->touch_read_mode_.load(std::memory_order_acquire);
  bool result = self->ReadHardwareTouch(read_mode, sampled_points.data(),
      sampled_points.size(), &sampled_point_count, true, &access_available);
  if (access_available) {
    bool edge_touch = false;
    if (result) {
      for (size_t i = 0; i < sampled_point_count; ++i) {
        const TouchPoint& sampled_point = sampled_points[i];
        edge_touch = edge_touch || sampled_point.edge_touch_flag;
        const bool edge_only_point =
            sampled_point.id == 0 && sampled_point.edge_touch_flag;
        if (edge_only_point ||
            !IsValidTouchPoint(sampled_point, self->screen_->ScreenWidth(),
                self->screen_->ScreenHeight())) {
          continue;
        }
        sampled_points[valid_point_count++] = sampled_point;
      }
    }
    if (valid_point_count > 0) {
      point = sampled_points[0];
      point.edge_touch_flag = point.edge_touch_flag || edge_touch;
      result = true;
    } else if (edge_touch) {
      point.id = 0;
      point.x = -1;
      point.y = -1;
      point.pressure = 0;
      point.edge_touch_flag = true;
      result = true;
    } else {
      result = false;
    }
  }
  if (!access_available) {
    if (self->IsInputBlocked() || self->IsDisplayFlushPaused()) {
      self->active_edge_touch_flag_ = false;
      self->pending_edge_touch_flag_ = false;
      self->has_last_touch_point_ = false;
      portENTER_CRITICAL(&self->touch_cache_lock_);
      self->cached_touch_point_count_ = 0;
      portEXIT_CRITICAL(&self->touch_cache_lock_);
      data->state = LV_INDEV_STATE_REL;
      return;
    }
    if (self->has_last_touch_point_.load(std::memory_order_acquire)) {
      data->state = LV_INDEV_STATE_PR;
      data->point = self->last_touch_point_;
    } else {
      data->state = LV_INDEV_STATE_REL;
    }
    return;
  }

  portENTER_CRITICAL(&self->touch_cache_lock_);
  const bool input_available =
      !self->IsInputBlocked() && !self->IsDisplayFlushPaused();
  if (input_available) {
    for (size_t i = 0; i < valid_point_count; ++i) {
      self->cached_touch_points_[i] = sampled_points[i];
    }
    self->cached_touch_point_count_ = valid_point_count;
  } else {
    self->cached_touch_point_count_ = 0;
  }
  portEXIT_CRITICAL(&self->touch_cache_lock_);
  if (!input_available) {
    self->active_edge_touch_flag_ = false;
    self->pending_edge_touch_flag_ = false;
    self->has_last_touch_point_ = false;
    data->state = LV_INDEV_STATE_REL;
    return;
  }

  if (result) {
    const bool valid_point = IsValidTouchPoint(
        point, self->screen_->ScreenWidth(), self->screen_->ScreenHeight());
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
    self->last_touch_point_.x = point.x;
    self->last_touch_point_.y = point.y;
    self->has_last_touch_point_.store(true, std::memory_order_release);

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
    uint32_t refresh_generation = 0;
    Lock();
    if (display_refresh_requested_.exchange(
            false, std::memory_order_acq_rel)) {
      refresh_generation = display_refresh_request_generation_.load(
          std::memory_order_acquire);
      display_refresh_rendering_generation_.store(
          refresh_generation, std::memory_order_release);
      lv_obj_t* active_screen = lv_screen_active();
      if (active_screen != nullptr) {
        lv_obj_invalidate(active_screen);
      }
    }
    uint32_t delay_ms = lv_timer_handler();
    Unlock();

    delay_ms = std::max(delay_ms, kMinimumHandlerDelayMs);
    ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(delay_ms));
  }
}

}  // namespace lilygo_box::hal
