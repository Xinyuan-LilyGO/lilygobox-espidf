/*
 * @Description: 全局边缘滑动指示器实现
 * @Author: LILYGO_L
 * @Date: 2026-05-12 22:15:00
 * @LastEditTime: 2026-08-25 11:26:42
 * @License: GPL 3.0
 */
#include "ui/input/edge_swipe_indicator.h"

#include <algorithm>
#include <utility>

#include "hal/lvgl_port.h"
#include "ui/haptic_feedback.h"
#include "ui/input/back_navigation_controller.h"
#include "ui/resources/fonts/font_assets.h"
#include "ui/resources/fonts/icon_assets.h"

namespace lilygo_box::ui {
namespace {

constexpr int kActivationEdgeWidth = 1;
// GT9895 首个有效边缘坐标的实测偏移约为 8 px。仅保留一倍余量，
// 避免扩大到屏幕键盘和页面两侧的正常控件区域。
constexpr int kHardwareHintActivationWidth = 16;
constexpr int kMinimumVisibleWidth = kActivationEdgeWidth;
constexpr int kIndicatorDiameter = 104;
constexpr int kMaximumVisibleWidth = kIndicatorDiameter * 2 / 5;
constexpr int kIconOuterEdgeOffset = 4;
constexpr int kFullRevealDistance =
    kMaximumVisibleWidth - kMinimumVisibleWidth;
constexpr uint32_t kIndicatorColor = 0x202020;
constexpr uint32_t kIconColor = 0xFFFFFF;
constexpr lv_opa_t kMinimumOpacity = 150;
constexpr lv_opa_t kMaximumOpacity = 230;

lv_obj_t* g_clip = nullptr;
lv_obj_t* g_circle = nullptr;
lv_obj_t* g_icon = nullptr;
hal::LvglPort* g_lvgl_port = nullptr;
EdgeSwipeBackCallback g_back_callback;

struct EdgeSwipeIndicatorState {
  lv_point_t start_point = {};
  int inward_distance = 0;
  bool pointer_pressed = false;
  bool tracking = false;
  bool back_committed = false;
  bool from_left_edge = false;
  bool from_right_edge = false;
};

EdgeSwipeIndicatorState g_state;

/**
 * @brief 将整数限制在指定闭区间内
 * @param value 待限制的数值
 * @param minimum 最小值
 * @param maximum 最大值
 * @return 限制后的数值
 */
int ClampInt(int value, int minimum, int maximum) {
  return std::clamp(value, minimum, maximum);
}

/**
 * @brief 计算触摸点从起始屏幕边缘向内移动的距离
 * @param state 当前边缘滑动状态
 * @param point 当前触摸坐标
 * @return 向屏幕内部移动的像素距离
 */
int InwardDistance(const EdgeSwipeIndicatorState& state, lv_point_t point) {
  if (state.from_left_edge) {
    return point.x - state.start_point.x;
  }
  if (state.from_right_edge) {
    return state.start_point.x - point.x;
  }
  return 0;
}

/**
 * @brief 确保边缘滑动指示器及其子对象已经创建
 * @return 所有指示器对象可用时返回 true，否则返回 false
 */
bool EnsureIndicator() {
  if (g_clip != nullptr && g_circle != nullptr && g_icon != nullptr) {
    return true;
  }

  lv_obj_t* layer = lv_layer_top();
  if (layer == nullptr) {
    return false;
  }

  g_clip = lv_obj_create(layer);
  if (g_clip == nullptr) {
    return false;
  }
  lv_obj_remove_flag(g_clip, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(g_clip, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_flag(g_clip, LV_OBJ_FLAG_IGNORE_LAYOUT);
  lv_obj_add_flag(g_clip, LV_OBJ_FLAG_FLOATING);
  lv_obj_add_flag(g_clip, LV_OBJ_FLAG_HIDDEN);
  lv_obj_set_style_bg_opa(g_clip, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_set_style_border_width(g_clip, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(g_clip, 0, LV_PART_MAIN);
  lv_obj_set_style_radius(g_clip, 0, LV_PART_MAIN);

  g_circle = lv_obj_create(g_clip);
  if (g_circle == nullptr) {
    lv_obj_delete(g_clip);
    g_clip = nullptr;
    return false;
  }
  lv_obj_remove_flag(g_circle, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(g_circle, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_flag(g_circle, LV_OBJ_FLAG_IGNORE_LAYOUT);
  lv_obj_set_style_bg_color(
      g_circle, lv_color_hex(kIndicatorColor), LV_PART_MAIN);
  lv_obj_set_style_border_width(g_circle, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(g_circle, 0, LV_PART_MAIN);
  lv_obj_set_style_radius(g_circle, LV_RADIUS_CIRCLE, LV_PART_MAIN);
  lv_obj_set_style_clip_corner(g_circle, true, LV_PART_MAIN);

  g_icon = lv_label_create(g_circle);
  if (g_icon == nullptr) {
    lv_obj_delete(g_clip);
    g_clip = nullptr;
    g_circle = nullptr;
    return false;
  }
  lv_obj_remove_flag(g_icon, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(g_icon, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_text_color(
      g_icon, lv_color_hex(kIconColor), LV_PART_MAIN);
  lv_obj_set_style_text_font(
      g_icon, &lvgl_font_material_symbols_fill_32, LV_PART_MAIN);
  lv_obj_set_style_text_align(g_icon, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
  lv_label_set_text(g_icon, icon::kChevronRight);
  return true;
}

/**
 * @brief 隐藏边缘滑动指示器
 */
void HideIndicator() {
  if (g_clip != nullptr) {
    lv_obj_add_flag(g_clip, LV_OBJ_FLAG_HIDDEN);
  }
}

/**
 * @brief 异步执行当前页面的分层返回操作
 * @param user_data LVGL 异步回调保留参数
 */
void ExecuteBackCallback(void* /*user_data*/) {
  if (!RequestBackNavigation() && g_back_callback) {
    g_back_callback();
  }
}

/**
 * @brief 根据向内移动距离更新返回提交状态并触发触觉反馈
 * @param state 当前边缘滑动状态
 * @param inward_distance 向屏幕内部移动的像素距离
 */
void UpdateBackCommitState(
    EdgeSwipeIndicatorState* state, int inward_distance) {
  if (state == nullptr) {
    return;
  }

  state->inward_distance = std::max(0, inward_distance);
  const bool was_committed = state->back_committed;
  state->back_committed =
      state->inward_distance >= kFullRevealDistance;
  if (state->back_committed && !was_committed) {
    PlayUiHapticFeedback();
  }
}

/**
 * @brief 根据当前触摸位置更新边缘滑动指示器
 * @param state 当前边缘滑动状态
 * @param screen_width 当前显示宽度
 * @param point 当前触摸坐标
 */
void UpdateIndicator(const EdgeSwipeIndicatorState& state,
    int screen_width, lv_point_t point) {
  if (!EnsureIndicator() || screen_width <= 0) {
    return;
  }

  const int distance = std::max(0, InwardDistance(state, point));
  const int visible_width = ClampInt(
      kMinimumVisibleWidth + distance,
      kMinimumVisibleWidth, kMaximumVisibleWidth);
  const int opacity = ClampInt(
      kMinimumOpacity + distance * 2, kMinimumOpacity, kMaximumOpacity);

  int layer_height = lv_obj_get_height(lv_layer_top());
  if (layer_height <= 0) {
    layer_height = lv_obj_get_height(lv_screen_active());
  }
  const int center_y = ClampInt(
      state.start_point.y, kIndicatorDiameter / 2,
      std::max(kIndicatorDiameter / 2,
          layer_height - kIndicatorDiameter / 2));
  const int clip_x = state.from_left_edge ? 0 : screen_width - visible_width;
  const int circle_x = state.from_left_edge
      ? visible_width - kIndicatorDiameter
      : 0;

  lv_obj_set_size(g_clip, visible_width, kIndicatorDiameter);
  lv_obj_set_pos(
      g_clip, clip_x, center_y - kIndicatorDiameter / 2);
  lv_obj_set_size(g_circle, kIndicatorDiameter, kIndicatorDiameter);
  lv_obj_set_pos(g_circle, circle_x, 0);
  lv_obj_set_style_bg_opa(
      g_circle, static_cast<lv_opa_t>(opacity), LV_PART_MAIN);
  lv_obj_set_style_text_opa(
      g_icon, static_cast<lv_opa_t>(opacity), LV_PART_MAIN);

  lv_obj_update_layout(g_icon);
  lv_obj_set_style_transform_pivot_x(
      g_icon, lv_obj_get_width(g_icon) / 2, LV_PART_MAIN);
  lv_obj_set_style_transform_pivot_y(
      g_icon, lv_obj_get_height(g_icon) / 2, LV_PART_MAIN);
  lv_obj_set_style_transform_rotation(
      g_icon, state.from_left_edge ? 0 : 1800, LV_PART_MAIN);
  const int icon_offset =
      state.from_left_edge ? -kIconOuterEdgeOffset : kIconOuterEdgeOffset;
  const int icon_center_x = visible_width / 2 - circle_x + icon_offset;
  lv_obj_set_pos(g_icon,
      icon_center_x - lv_obj_get_width(g_icon) / 2,
      kIndicatorDiameter / 2 - lv_obj_get_height(g_icon) / 2);

  lv_obj_remove_flag(g_clip, LV_OBJ_FLAG_HIDDEN);
  lv_obj_move_to_index(g_clip, -1);
  lv_obj_invalidate(g_clip);
}

/**
 * @brief 拦截并处理用于全局返回的边缘指针输入
 * @param input_state LVGL 指针输入状态
 * @param point 已转换到当前显示方向的触摸坐标
 * @param hardware_edge_hint 硬件是否提供了有效的边缘触摸提示
 * @return 本次输入被边缘返回手势占用时返回 true，否则返回 false
 */
bool InterceptPointerInput(lv_indev_state_t input_state, lv_point_t point,
    bool hardware_edge_hint) {
  if (input_state != LV_INDEV_STATE_PRESSED) {
    const bool was_tracking = g_state.tracking;
    const bool should_navigate_back =
        g_state.tracking && g_state.back_committed;
    g_state = {};
    HideIndicator();
    if (should_navigate_back) {
      lv_async_call(ExecuteBackCallback, nullptr);
    }
    return was_tracking;
  }

  lv_display_t* display =
      g_lvgl_port == nullptr ? nullptr : g_lvgl_port->lvgl_display();
  if (display == nullptr) {
    return false;
  }
  const int screen_width = lv_display_get_horizontal_resolution(display);
  if (screen_width <= 0) {
    return false;
  }

  if (!g_state.pointer_pressed) {
    g_state.pointer_pressed = true;
    g_state.start_point = point;
    const int activation_width = hardware_edge_hint
        ? kHardwareHintActivationWidth
        : kActivationEdgeWidth;
    g_state.from_left_edge = point.x < activation_width;
    g_state.from_right_edge = point.x >= screen_width - activation_width;
    g_state.tracking = g_state.from_left_edge || g_state.from_right_edge;
    if (g_state.tracking) {
      // 部分触摸固件会在物理边缘先给出硬件提示，却把首个有效坐标
      // 推迟到边缘内侧。硬件提示只扩大候选区并将软件起点补回物理
      // 边缘；动画距离、完整展开和松手返回仍全部由有效坐标决定。
      if (hardware_edge_hint) {
        g_state.start_point.x =
            g_state.from_left_edge ? 0 : screen_width - 1;
      }
      UpdateBackCommitState(
          &g_state, InwardDistance(g_state, point));
      UpdateIndicator(g_state, screen_width, point);
    }
    return g_state.tracking;
  }

  if (!g_state.tracking) {
    return false;
  }

  const int inward_distance = InwardDistance(g_state, point);
  UpdateBackCommitState(&g_state, inward_distance);
  if (inward_distance >= 0) {
    UpdateIndicator(g_state, screen_width, point);
  } else {
    HideIndicator();
  }
  return true;
}

}  // namespace

/**
 * @brief 初始化全局边缘滑动指示器
 * @param lvgl_port LVGL 输入端口
 * @param back_callback 没有分层返回目标时执行的后备回调
 */
void InitializeEdgeSwipeIndicator(hal::LvglPort* lvgl_port,
    EdgeSwipeBackCallback back_callback) {
  g_back_callback = std::move(back_callback);
  if (g_lvgl_port == lvgl_port) {
    return;
  }
  if (g_lvgl_port != nullptr) {
    g_lvgl_port->SetPointerInputInterceptor({});
  }

  g_state = {};
  HideIndicator();
  g_lvgl_port = lvgl_port;
  if (g_lvgl_port == nullptr) {
    return;
  }

  EnsureIndicator();
  g_lvgl_port->SetPointerInputInterceptor(InterceptPointerInput);
}

}  // namespace lilygo_box::ui
