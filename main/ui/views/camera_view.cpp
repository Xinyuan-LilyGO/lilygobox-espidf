/*
 * @Description: Camera app view
 * @Author: LILYGO_L
 * @Date: 2026-07-02 00:00:00
 * @LastEditTime: 2026-07-02 18:19:32
 * @License: GPL 3.0
 */
#include "ui/views/camera_view.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <new>

#include "base/logger.h"
#include "esp_heap_caps.h"

#include "hal/providers/camera_provider.h"
#include "ui/input/press_cancel.h"
#include "ui/resources/fonts/font_assets.h"
#include "ui/resources/fonts/icon_assets.h"
#include "ui/theme/theme_provider.h"

namespace lilygo_box::ui {
namespace {

constexpr uint32_t kCameraViewRefreshPeriodMs = 10;
constexpr uint32_t kBackgroundColor = 0x000000;
constexpr uint32_t kPrimaryTextColor = 0xFFFFFF;
constexpr uint32_t kSecondaryTextColor = 0xBDBDBD;
constexpr uint32_t kActionColor = theme::LightNeutralTheme().action;
constexpr uint32_t kActionTextColor = theme::LightNeutralTheme().on_action;
constexpr int kScanningGroupOffsetY = 0;
constexpr int kErrorGroupOffsetY = 0;

enum class CameraContentState : uint8_t {
  kScanning,
  kPreview,
  kError,
};

struct CameraViewState {
  hal::CameraProvider* camera = nullptr;
  std::function<void(std::function<void(bool visible)> callback)>
      set_lock_screen_visibility_callback;
  std::function<void(uint32_t color)> set_status_bar_text_color;
  lv_obj_t* container = nullptr;
  lv_obj_t* status_layer = nullptr;
  lv_obj_t* image = nullptr;
  lv_timer_t* refresh_timer = nullptr;
  lv_image_dsc_t image_dsc = {};
  uint8_t* frame_buffer = nullptr;
  size_t frame_buffer_size = 0;
  CameraContentState content_state = CameraContentState::kScanning;
  CameraError displayed_error = CameraError::kNone;
  bool lock_screen_paused = false;
  bool preview_started = false;
  int width = 0;
  int height = 0;
  uint32_t last_sequence = 0;
  uint32_t last_width = 0;
  uint32_t last_height = 0;
  uint32_t last_stride = 0;
  uint32_t last_bits_per_pixel = 0;
};

/**
 * @brief 获取页面使用的 Google Sans 字体
 */
const lv_font_t* Font22() { return &lvgl_font_google_sans_flex_22; }
const lv_font_t* Font24() { return &lvgl_font_google_sans_flex_24; }
const lv_font_t* Font28() { return &lvgl_font_google_sans_flex_28; }

/**
 * @brief 获取 56 号填充 Material Symbols 字体
 * @return 字体指针
 */
const lv_font_t* MaterialFillIconFont56() {
  return &lvgl_font_material_symbols_fill_56;
}

/**
 * @brief 将对象设置为透明无边框容器
 * @param object LVGL 对象
 */
void MakeTransparent(lv_obj_t* object) {
  if (object == nullptr) {
    return;
  }
  lv_obj_set_style_bg_opa(object, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_set_style_border_width(object, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(object, 0, LV_PART_MAIN);
}

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
  lv_label_set_text(label, text == nullptr ? "" : text);
  lv_obj_set_style_text_color(label, color, LV_PART_MAIN);
  lv_obj_set_style_text_font(label, font, LV_PART_MAIN);
  return label;
}

/**
 * @brief 创建 Material Symbols 图标标签
 * @param parent 父对象
 * @param symbol 图标字符
 * @param color 图标颜色
 * @return 创建成功返回对象指针，否则返回 nullptr
 */
lv_obj_t* CreateMaterialIcon(
    lv_obj_t* parent, const char* symbol, lv_color_t color) {
  lv_obj_t* icon_label =
      CreateLabel(parent, symbol, color, MaterialFillIconFont56());
  if (icon_label != nullptr) {
    lv_obj_set_style_text_align(
        icon_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
  }
  return icon_label;
}

void RetryCameraClickedEventCallback(lv_event_t* event);

/**
 * @brief 创建主要操作按钮
 * @param parent 父对象
 * @param text 按钮文本
 * @param callback 点击回调
 * @param user_data 回调上下文
 * @return 创建成功返回对象指针，否则返回 nullptr
 */
lv_obj_t* CreatePrimaryActionButton(lv_obj_t* parent, const char* text,
    lv_event_cb_t callback, void* user_data) {
  lv_obj_t* button = lv_button_create(parent);
  if (button == nullptr) {
    return nullptr;
  }
  lv_obj_remove_style_all(button);
  lv_obj_add_flag(button, LV_OBJ_FLAG_GESTURE_BUBBLE);
  lv_obj_set_size(button, 196, 64);
  lv_obj_set_style_bg_color(
      button, lv_color_hex(kActionColor), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(button, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_bg_color(button,
      lv_color_hex(theme::LightNeutralTheme().action_pressed),
      LV_STATE_PRESSED);
  lv_obj_set_style_bg_opa(button, LV_OPA_COVER, LV_STATE_PRESSED);
  lv_obj_set_style_radius(button, 32, LV_PART_MAIN);
  lv_obj_set_style_radius(button, 32, LV_STATE_PRESSED);
  if (!AddPressCancelOnLeave(button)) {
    lv_obj_delete(button);
    return nullptr;
  }
  lv_obj_add_event_cb(button, callback, LV_EVENT_CLICKED, user_data);

  lv_obj_t* label = CreateLabel(
      button, text, lv_color_hex(kActionTextColor), Font24());
  if (label != nullptr) {
    lv_obj_center(label);
  }
  return button;
}

/**
 * @brief 创建居中的状态内容容器
 * @param state 摄像头页面状态
 * @param height 容器高度
 * @param offset_y 垂直偏移
 * @return 创建成功返回对象指针，否则返回 nullptr
 */
lv_obj_t* CreateStatusGroup(
    CameraViewState* state, int height, int offset_y) {
  if (state == nullptr || state->status_layer == nullptr) {
    return nullptr;
  }
  lv_obj_t* group = lv_obj_create(state->status_layer);
  if (group == nullptr) {
    return nullptr;
  }
  MakeTransparent(group);
  lv_obj_remove_flag(group, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(group, LV_OBJ_FLAG_GESTURE_BUBBLE);
  lv_obj_set_size(group, state->width, height);
  const int centered_top = (state->height - height) / 2 + offset_y;
  const int maximum_top = std::max(0, state->height - height);
  const int group_top = std::min(std::max(centered_top, 0), maximum_top);
  lv_obj_align(group, LV_ALIGN_TOP_MID, 0, group_top);
  return group;
}

/**
 * @brief 设置摄像头页面的黑色背景和浅色状态栏
 * @param state 摄像头页面状态
 */
void ApplyCameraPageColors(CameraViewState* state) {
  if (state == nullptr || state->container == nullptr) {
    return;
  }
  lv_obj_set_style_bg_color(
      state->container, lv_color_hex(kBackgroundColor), LV_PART_MAIN);
  if (state->set_status_bar_text_color) {
    state->set_status_bar_text_color(kPrimaryTextColor);
  }
}

/**
 * @brief 渲染摄像头扫描中的加载状态
 * @param state 摄像头页面状态
 */
void RenderCameraScanning(CameraViewState* state) {
  if (state == nullptr || state->status_layer == nullptr) {
    return;
  }
  state->content_state = CameraContentState::kScanning;
  state->displayed_error = CameraError::kNone;
  ApplyCameraPageColors(state);
  if (state->image != nullptr) {
    lv_obj_add_flag(state->image, LV_OBJ_FLAG_HIDDEN);
  }
  lv_obj_remove_flag(state->status_layer, LV_OBJ_FLAG_HIDDEN);
  lv_obj_clean(state->status_layer);

  lv_obj_t* group =
      CreateStatusGroup(state, 180, kScanningGroupOffsetY);
  if (group == nullptr) {
    return;
  }
  lv_obj_t* spinner = lv_spinner_create(group);
  if (spinner != nullptr) {
    lv_obj_set_size(spinner, 68, 68);
    lv_spinner_set_anim_params(spinner, 850, 250);
    lv_obj_set_style_arc_color(spinner,
        lv_color_hex(theme::LightNeutralTheme().surface_container_high),
        LV_PART_MAIN);
    lv_obj_set_style_arc_color(
        spinner, lv_color_hex(kActionColor), LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(spinner, 7, LV_PART_MAIN);
    lv_obj_set_style_arc_width(spinner, 7, LV_PART_INDICATOR);
    lv_obj_align(spinner, LV_ALIGN_TOP_MID, 0, 0);
  }

  lv_obj_t* message = CreateLabel(group, "Looking for a camera...",
      lv_color_hex(kPrimaryTextColor), Font28());
  if (message != nullptr) {
    lv_obj_align(message, LV_ALIGN_TOP_MID, 0, 96);
  }
  lv_obj_t* hint = CreateLabel(group,
      "Keep the camera connected while scanning",
      lv_color_hex(kSecondaryTextColor), Font22());
  if (hint != nullptr) {
    lv_obj_set_width(hint, state->width - 80);
    lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_label_set_long_mode(hint, LV_LABEL_LONG_WRAP);
    lv_obj_align(hint, LV_ALIGN_TOP_MID, 0, 138);
  }
}

/**
 * @brief 渲染摄像头扫描或初始化失败状态
 * @param state 摄像头页面状态
 * @param error 摄像头错误
 */
void RenderCameraError(CameraViewState* state, CameraError error) {
  if (state == nullptr || state->status_layer == nullptr) {
    return;
  }
  if (state->content_state == CameraContentState::kError &&
      state->displayed_error == error) {
    return;
  }

  const DiagnosticError diagnostic_error = GetCameraDiagnosticError(error);
  LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
      "StartCameraPreview failed: %s: %s\n", diagnostic_error.code,
      diagnostic_error.text);
  state->content_state = CameraContentState::kError;
  state->displayed_error = error;
  state->preview_started = false;
  ApplyCameraPageColors(state);
  if (state->image != nullptr) {
    lv_obj_add_flag(state->image, LV_OBJ_FLAG_HIDDEN);
  }
  lv_obj_remove_flag(state->status_layer, LV_OBJ_FLAG_HIDDEN);
  lv_obj_clean(state->status_layer);

  lv_obj_t* group = CreateStatusGroup(state, 300, kErrorGroupOffsetY);
  if (group == nullptr) {
    return;
  }
  lv_obj_t* icon_label =
      CreateMaterialIcon(group, icon::kCamera, lv_color_hex(kActionColor));
  if (icon_label != nullptr) {
    lv_obj_align(icon_label, LV_ALIGN_TOP_MID, 0, 20);
  }

  lv_obj_t* message = CreateLabel(group, "Camera unavailable",
      lv_color_hex(kPrimaryTextColor), Font28());
  if (message != nullptr) {
    lv_obj_align(message, LV_ALIGN_TOP_MID, 0, 100);
  }
  char hint_text[128] = {};
  std::snprintf(hint_text, sizeof(hint_text), "%s: %s",
      diagnostic_error.code, diagnostic_error.text);
  lv_obj_t* hint = CreateLabel(group, hint_text,
      lv_color_hex(kSecondaryTextColor), Font22());
  if (hint != nullptr) {
    lv_obj_set_width(hint, state->width - 80);
    lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_label_set_long_mode(hint, LV_LABEL_LONG_WRAP);
    lv_obj_align(hint, LV_ALIGN_TOP_MID, 0, 140);
  }
  lv_obj_t* retry = CreatePrimaryActionButton(
      group, "Retry", RetryCameraClickedEventCallback, state);
  if (retry != nullptr) {
    lv_obj_align(retry, LV_ALIGN_TOP_MID, 0, 200);
  }
}

/**
 * @brief 切换为摄像头预览状态
 * @param state 摄像头页面状态
 */
void RenderCameraPreview(CameraViewState* state) {
  if (state == nullptr) {
    return;
  }
  state->content_state = CameraContentState::kPreview;
  state->displayed_error = CameraError::kNone;
  ApplyCameraPageColors(state);
  if (state->status_layer != nullptr) {
    lv_obj_add_flag(state->status_layer, LV_OBJ_FLAG_HIDDEN);
  }
  if (state->image != nullptr) {
    lv_obj_remove_flag(state->image, LV_OBJ_FLAG_HIDDEN);
  }
}

/**
 * @brief 清除上一次预览的帧元数据
 * @param state 摄像头页面状态
 */
void ResetCameraFrameState(CameraViewState* state) {
  if (state == nullptr) {
    return;
  }
  state->last_sequence = 0;
  state->last_width = 0;
  state->last_height = 0;
  state->last_stride = 0;
  state->last_bits_per_pixel = 0;
  state->image_dsc = {};
}

/**
 * @brief 提交后台摄像头扫描请求
 * @param state 摄像头页面状态
 * @return 请求成功提交返回 true，否则返回 false
 */
bool StartCameraScan(CameraViewState* state) {
  if (state == nullptr) {
    return false;
  }
  if (state->camera == nullptr) {
    RenderCameraError(state, CameraError::kProviderUnavailable);
    return false;
  }
  if (!state->camera->StartCameraPreview()) {
    CameraError error = state->camera->GetCameraPreviewError();
    if (error == CameraError::kNone) {
      error = CameraError::kVideoInitFailed;
    }
    RenderCameraError(state, error);
    return false;
  }
  state->preview_started = true;
  return true;
}

/**
 * @brief 处理重新扫描按钮点击事件
 * @param event LVGL 事件对象
 */
void RetryCameraClickedEventCallback(lv_event_t* event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
    return;
  }
  auto* state =
      static_cast<CameraViewState*>(lv_event_get_user_data(event));
  if (state == nullptr) {
    return;
  }

  // 错误发布与后台任务清理之间可能存在极短窗口，先等待旧任务退出再重启。
  if (state->camera != nullptr) {
    state->camera->StopCameraPreview();
  }
  ResetCameraFrameState(state);
  RenderCameraScanning(state);
  StartCameraScan(state);
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
  state->frame_buffer = static_cast<uint8_t*>(
      heap_caps_malloc(required_size, MALLOC_CAP_SPIRAM));
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

  ResetCameraFrameState(state);
  RenderCameraScanning(state);
  StartCameraScan(state);
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

/**
 * @brief 刷新摄像头页面状态和预览帧
 * @param timer LVGL 定时器
 */
void CameraRefreshTimerCallback(lv_timer_t* timer) {
  if (timer == nullptr) {
    return;
  }

  auto* state = static_cast<CameraViewState*>(lv_timer_get_user_data(timer));
  if (state == nullptr || state->camera == nullptr || state->image == nullptr ||
      state->lock_screen_paused ||
      state->content_state == CameraContentState::kError) {
    return;
  }

  hal::CameraPreviewFrameInfo info;
  if (!state->camera->GetCameraPreviewFrameInfo(&info)) {
    const CameraError error = state->camera->GetCameraPreviewError();
    if (error != CameraError::kNone) {
      RenderCameraError(state, error);
    }
    return;
  }
  if (info.sequence == state->last_sequence) {
    return;
  }
  if (!EnsureFrameBuffer(state, info.data_size)) {
    state->camera->StopCameraPreview();
    RenderCameraError(state, CameraError::kOutputBufferAllocationFailed);
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
  if (state->content_state != CameraContentState::kPreview) {
    RenderCameraPreview(state);
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

  auto* state =
      static_cast<CameraViewState*>(lv_event_get_user_data(event));
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
  if (parent == nullptr || config.width <= 0 || config.height <= 0) {
    LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
        "CreateCameraView received invalid input (parent: %p, width: %d, "
        "height: %d)\n",
        parent, config.width, config.height);
    return nullptr;
  }
  lv_obj_t* container = lv_obj_create(parent);
  if (container == nullptr) {
    return nullptr;
  }

  lv_obj_remove_flag(container, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(container, config.width, config.height);
  lv_obj_align(container, LV_ALIGN_CENTER, 0, 0);
  lv_obj_set_style_bg_color(
      container, lv_color_hex(kBackgroundColor), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(container, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_border_width(container, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(container, 0, LV_PART_MAIN);

  if (config.set_status_bar_visible) {
    config.set_status_bar_visible(true);
  }

  auto* state = new (std::nothrow) CameraViewState();
  if (state == nullptr) {
    lv_obj_delete(container);
    return nullptr;
  }
  state->camera = config.camera;
  state->container = container;
  state->width = config.width;
  state->height = config.height;
  state->set_status_bar_text_color = config.set_status_bar_text_color;
  state->set_lock_screen_visibility_callback =
      config.set_lock_screen_visibility_callback;
  lv_obj_add_event_cb(
      container, CameraViewDeleteEventCallback, LV_EVENT_DELETE, state);

  state->image = lv_image_create(container);
  if (state->image == nullptr) {
    lv_obj_delete(container);
    return nullptr;
  }
  lv_obj_remove_flag(state->image, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_flag(state->image, LV_OBJ_FLAG_HIDDEN);
  lv_obj_center(state->image);

  state->status_layer = lv_obj_create(container);
  if (state->status_layer == nullptr) {
    lv_obj_delete(container);
    return nullptr;
  }
  MakeTransparent(state->status_layer);
  lv_obj_remove_flag(state->status_layer, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(state->status_layer, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_flag(state->status_layer, LV_OBJ_FLAG_GESTURE_BUBBLE);
  lv_obj_set_size(state->status_layer, config.width, config.height);
  lv_obj_align(state->status_layer, LV_ALIGN_CENTER, 0, 0);

  if (state->set_lock_screen_visibility_callback) {
    state->set_lock_screen_visibility_callback([state](bool visible) {
      CameraLockScreenVisibilityCallback(state, visible);
    });
  }

  state->refresh_timer = lv_timer_create(
      CameraRefreshTimerCallback, kCameraViewRefreshPeriodMs, state);
  if (state->refresh_timer == nullptr) {
    lv_obj_delete(container);
    return nullptr;
  }

  RenderCameraScanning(state);
  StartCameraScan(state);
  CameraRefreshTimerCallback(state->refresh_timer);
  return container;
}

}  // namespace lilygo_box::ui
