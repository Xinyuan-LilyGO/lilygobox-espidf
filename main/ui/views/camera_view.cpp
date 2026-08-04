/*
 * @Description: Camera app view
 * @Author: LILYGO_L
 * @Date: 2026-07-02 00:00:00
 * @LastEditTime: 2026-07-02 18:19:32
 * @License: GPL 3.0
 */
#include "ui/views/camera_view.h"

#include <cstdio>
#include <functional>

#include "base/logger.h"
#include "esp_heap_caps.h"

#include "hal/providers/camera_provider.h"
#include "ui/resources/fonts/font_assets.h"

namespace lilygo_box::ui {
namespace {

constexpr uint32_t kCameraViewRefreshPeriodMs = 10;

struct CameraViewState {
  hal::CameraProvider* camera = nullptr;
  std::function<void(std::function<void(bool visible)> callback)>
      set_lock_screen_visibility_callback;
  lv_obj_t* container = nullptr;
  lv_obj_t* image = nullptr;
  lv_obj_t* error_label = nullptr;
  lv_timer_t* refresh_timer = nullptr;
  lv_image_dsc_t image_dsc = {};
  uint8_t* frame_buffer = nullptr;
  size_t frame_buffer_size = 0;
  bool lock_screen_paused = false;
  bool preview_started = false;
  uint32_t last_sequence = 0;
  uint32_t last_width = 0;
  uint32_t last_height = 0;
  uint32_t last_stride = 0;
  uint32_t last_bits_per_pixel = 0;
};

/**
 * @brief 获取 24 号 Google Sans 字体
 * @return 字体指针
 */
const lv_font_t* Font24() { return &lvgl_font_google_sans_flex_24; }

/**
 * @brief 创建文本标签
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
  lv_obj_set_style_text_color(label, color, LV_PART_MAIN);
  lv_obj_set_style_text_font(label, font, LV_PART_MAIN);
  return label;
}

/**
 * @brief 在摄像头页面显示错误码和文字说明
 * @param state 摄像头页面状态
 * @param error 摄像头错误
 */
void ShowCameraError(CameraViewState* state, CameraError error) {
  if (state == nullptr || state->container == nullptr) {
    return;
  }

  const DiagnosticError diagnostic_error = GetCameraDiagnosticError(error);
  char error_message[128] = {};
  std::snprintf(error_message, sizeof(error_message),
      "Camera unavailable\n%s: %s", diagnostic_error.code,
      diagnostic_error.text);
  LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
      "StartCameraPreview failed: %s: %s\n", diagnostic_error.code,
      diagnostic_error.text);

  state->preview_started = false;
  if (state->refresh_timer != nullptr) {
    lv_timer_delete(state->refresh_timer);
    state->refresh_timer = nullptr;
  }
  if (state->image != nullptr) {
    lv_obj_add_flag(state->image, LV_OBJ_FLAG_HIDDEN);
  }
  if (state->error_label == nullptr) {
    state->error_label = CreateLabel(state->container, error_message,
        lv_color_hex(0xFFFFFF), Font24());
  } else {
    lv_label_set_text(state->error_label, error_message);
  }
  if (state->error_label != nullptr) {
    lv_obj_set_width(state->error_label, LV_PCT(90));
    lv_obj_set_style_text_align(
        state->error_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_center(state->error_label);
  }
}

/**
 * @brief 根据像素位宽获取 LVGL 颜色格式
 * @param bits_per_pixel 像素位宽
 * @return LVGL 颜色格式
 */
lv_color_format_t ColorFormatForBitsPerPixel(uint32_t bits_per_pixel) {
  return bits_per_pixel == 24 ? LV_COLOR_FORMAT_RGB888 : LV_COLOR_FORMAT_RGB565;
}

/**
 * @brief 确保 LVGL 预览缓冲区可用
 * @param state 摄像头页面状态
 * @param required_size 需要的缓冲区大小
 * @return 缓冲区可用返回 true，否则返回 false
 */
bool EnsureFrameBuffer(CameraViewState* state, size_t required_size) {
  if (state == nullptr || required_size == 0) {
    return false;
  }
  if (state->frame_buffer != nullptr &&
      state->frame_buffer_size >= required_size) {
    return true;
  }

  if (state->frame_buffer != nullptr) {
    heap_caps_free(state->frame_buffer);
    state->frame_buffer = nullptr;
    state->frame_buffer_size = 0;
  }
  state->frame_buffer = static_cast<uint8_t*>(heap_caps_malloc(
      required_size, MALLOC_CAP_SPIRAM));
  if (state->frame_buffer == nullptr) {
    return false;
  }
  state->frame_buffer_size = required_size;
  return true;
}

/**
 * @brief 根据锁屏状态停止或恢复摄像头预览
 * @param state 摄像头页面状态
 * @param visible true 表示锁屏显示，false 表示锁屏隐藏
 */
void SetCameraLockScreenPaused(CameraViewState* state, bool visible) {
  if (state == nullptr || state->camera == nullptr ||
      state->lock_screen_paused == visible) {
    return;
  }

  state->lock_screen_paused = visible;
  if (!state->preview_started) {
    return;
  }
  if (visible) {
    state->camera->StopCameraPreview();
    return;
  }

  state->last_sequence = 0;
  state->last_width = 0;
  state->last_height = 0;
  state->last_stride = 0;
  state->last_bits_per_pixel = 0;
  state->image_dsc.data = nullptr;
  if (!state->camera->StartCameraPreview()) {
    ShowCameraError(state, state->camera->GetCameraPreviewError());
  }
}

/**
 * @brief 锁屏显示状态变化回调
 * @param state 摄像头页面状态
 * @param visible true 表示锁屏显示，false 表示锁屏隐藏
 */
void CameraLockScreenVisibilityCallback(
    CameraViewState* state, bool visible) {
  SetCameraLockScreenPaused(state, visible);
}

void CameraRefreshTimerCallback(lv_timer_t* timer) {
  if (timer == nullptr) {
    return;
  }

  auto* state = static_cast<CameraViewState*>(lv_timer_get_user_data(timer));
  if (state == nullptr || state->camera == nullptr || state->image == nullptr ||
      state->lock_screen_paused) {
    return;
  }

  hal::CameraPreviewFrameInfo info;
  if (!state->camera->GetCameraPreviewFrameInfo(&info) ||
      info.sequence == state->last_sequence ||
      !EnsureFrameBuffer(state, info.data_size)) {
    return;
  }
  if (!state->camera->CopyCameraPreviewFrame(
          state->frame_buffer, state->frame_buffer_size, &info)) {
    return;
  }

  state->last_sequence = info.sequence;
  const bool source_changed =
      state->image_dsc.data != state->frame_buffer ||
      state->last_width != info.width || state->last_height != info.height ||
      state->last_stride != info.stride ||
      state->last_bits_per_pixel != info.bits_per_pixel;
  state->image_dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
  state->image_dsc.header.cf = ColorFormatForBitsPerPixel(info.bits_per_pixel);
  state->image_dsc.header.flags = 0;
  state->image_dsc.header.w = info.width;
  state->image_dsc.header.h = info.height;
  state->image_dsc.header.stride = info.stride;
  state->image_dsc.data_size = info.data_size;
  state->image_dsc.data = state->frame_buffer;
  if (source_changed) {
    lv_image_set_src(state->image, &state->image_dsc);
    lv_obj_center(state->image);
    state->last_width = info.width;
    state->last_height = info.height;
    state->last_stride = info.stride;
    state->last_bits_per_pixel = info.bits_per_pixel;
  }
  lv_obj_invalidate(state->image);
}

/**
 * @brief 摄像头页面删除时停止预览并释放状态
 * @param event LVGL 事件对象
 */
void CameraViewDeleteEventCallback(lv_event_t* event) {
  if (lv_event_get_code(event) != LV_EVENT_DELETE) {
    return;
  }

  auto* state = static_cast<CameraViewState*>(lv_event_get_user_data(event));
  if (state == nullptr) {
    return;
  }

  if (state->refresh_timer != nullptr) {
    lv_timer_delete(state->refresh_timer);
    state->refresh_timer = nullptr;
  }
  if (state->set_lock_screen_visibility_callback) {
    state->set_lock_screen_visibility_callback(nullptr);
  }
  if (state->camera != nullptr) {
    state->camera->StopCameraPreview();
  }
  if (state->frame_buffer != nullptr) {
    heap_caps_free(state->frame_buffer);
    state->frame_buffer = nullptr;
    state->frame_buffer_size = 0;
  }
  delete state;
}

}  // namespace

lv_obj_t* CreateCameraView(lv_obj_t* parent, const app::AppEntry& app_entry,
    const AppViewConfig& config) {
  (void)app_entry;
  lv_obj_t* container = lv_obj_create(parent);
  if (container == nullptr) {
    return nullptr;
  }

  lv_obj_remove_flag(container, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(container, config.width, config.height);
  lv_obj_align(container, LV_ALIGN_CENTER, 0, 0);
  lv_obj_set_style_bg_color(container, lv_color_hex(0x000000), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(container, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_border_width(container, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(container, 0, LV_PART_MAIN);

  if (config.set_status_bar_visible) {
    config.set_status_bar_visible(true);
  }

  auto* state = new CameraViewState();
  state->camera = config.camera;
  state->container = container;
  state->set_lock_screen_visibility_callback =
      config.set_lock_screen_visibility_callback;
  if (state->set_lock_screen_visibility_callback) {
    state->set_lock_screen_visibility_callback(
        [state](bool visible) {
          CameraLockScreenVisibilityCallback(state, visible);
        });
  }
  lv_obj_add_event_cb(
      container, CameraViewDeleteEventCallback, LV_EVENT_DELETE, state);

  if (config.camera == nullptr || !config.camera->StartCameraPreview()) {
    const CameraError error = config.camera == nullptr
                                  ? CameraError::kProviderUnavailable
                                  : config.camera->GetCameraPreviewError();
    ShowCameraError(state, error);
    return container;
  }
  state->preview_started = true;

  state->image = lv_image_create(container);
  if (state->image == nullptr) {
    lv_obj_delete(container);
    return nullptr;
  }
  lv_obj_remove_flag(state->image, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_center(state->image);

  state->refresh_timer = lv_timer_create(
      CameraRefreshTimerCallback, kCameraViewRefreshPeriodMs, state);
  if (state->refresh_timer == nullptr) {
    lv_obj_delete(container);
    return nullptr;
  }
  CameraRefreshTimerCallback(state->refresh_timer);
  return container;
}

}  // namespace lilygo_box::ui
