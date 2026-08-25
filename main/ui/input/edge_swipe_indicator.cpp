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
constexpr int kDefaultConfirmDistance = 40;
constexpr int kPassthroughMinEdgeWidth = 36;
constexpr int kPassthroughMaxEdgeWidth = 76;
constexpr int kPassthroughEdgeWidthDivisor = 9;
constexpr int kPassthroughIndicatorStartDistance = 18;
constexpr int kPassthroughConfirmDistance = 90;
constexpr int kPassthroughMaximumVerticalOffset = 140;
constexpr int kIndicatorMinimumDiameter = 96;
constexpr int kIndicatorMaximumDiameter = 132;
constexpr int kIndicatorVisibleDivisor = 3;
constexpr uint32_t kIndicatorFadeDurationMs = 120;
constexpr uint32_t kIndicatorColor = 0x202020;
constexpr uint32_t kIconColor = 0xFFFFFF;
constexpr lv_opa_t kIndicatorMaximumOpacity = 238;

lv_obj_t* g_indicator = nullptr;
lv_obj_t* g_icon = nullptr;
lv_opa_t g_indicator_opacity = LV_OPA_TRANSP;
hal::LvglPort* g_lvgl_port = nullptr;
EdgeSwipeBackCallback g_back_callback;
bool g_passthrough_mode = false;

struct EdgeSwipeIndicatorState {
  lv_point_t start_point = {};
  int inward_distance = 0;
  bool pointer_pressed = false;
  bool tracking = false;
  bool active = false;
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
 * @brief 计算整数绝对值
 * @param value 原始值
 * @return 绝对值
 */
int AbsInt(int value) { return value < 0 ? -value : value; }

/**
 * @brief 计算输入透传模式的边缘起始宽度
 * @param screen_width 当前显示宽度
 * @return 边缘手势起始宽度
 */
int PassthroughEdgeWidth(int screen_width) {
  return ClampInt(screen_width / kPassthroughEdgeWidthDivisor,
      kPassthroughMinEdgeWidth, kPassthroughMaxEdgeWidth);
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
 * @brief 设置返回圆及图标透明度
 * @param object 返回圆对象
 * @param value 透明度数值
 */
void SetIndicatorOpacity(void* object, int32_t value) {
  if (object == nullptr) {
    return;
  }

  g_indicator_opacity = static_cast<lv_opa_t>(
      ClampInt(static_cast<int>(value), 0, LV_OPA_COVER));
  auto* indicator = static_cast<lv_obj_t*>(object);
  lv_obj_set_style_opa(
      indicator, g_indicator_opacity, LV_PART_MAIN);
  lv_obj_set_style_bg_opa(
      indicator, g_indicator_opacity, LV_PART_MAIN);
  if (g_icon != nullptr) {
    lv_obj_set_style_text_opa(
        g_icon, g_indicator_opacity, LV_PART_MAIN);
  }
}

/**
 * @brief 返回圆淡出完成后隐藏对象
 * @param animation LVGL 动画对象
 */
void IndicatorFadeCompletedCallback(lv_anim_t* animation) {
  auto* indicator =
      static_cast<lv_obj_t*>(lv_anim_get_user_data(animation));
  if (indicator != nullptr) {
    lv_obj_add_flag(indicator, LV_OBJ_FLAG_HIDDEN);
  }
}

/**
 * @brief 确保边缘返回圆已经创建
 * @return 返回圆和图标可用时返回 true，否则返回 false
 */
bool EnsureIndicator() {
  if (g_indicator != nullptr && g_icon != nullptr) {
    return true;
  }

  lv_obj_t* layer = lv_layer_top();
  if (layer == nullptr) {
    return false;
  }

  g_indicator = lv_obj_create(layer);
  if (g_indicator == nullptr) {
    return false;
  }
  lv_obj_remove_flag(g_indicator, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(g_indicator, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_flag(g_indicator, LV_OBJ_FLAG_IGNORE_LAYOUT);
  lv_obj_add_flag(g_indicator, LV_OBJ_FLAG_FLOATING);
  lv_obj_add_flag(g_indicator, LV_OBJ_FLAG_HIDDEN);
  lv_obj_set_style_bg_color(
      g_indicator, lv_color_hex(kIndicatorColor), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(g_indicator, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_border_width(g_indicator, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(g_indicator, 0, LV_PART_MAIN);
  lv_obj_set_style_radius(
      g_indicator, LV_RADIUS_CIRCLE, LV_PART_MAIN);
  lv_obj_set_style_opa(g_indicator, LV_OPA_TRANSP, LV_PART_MAIN);

  g_icon = lv_label_create(g_indicator);
  if (g_icon == nullptr) {
    lv_obj_delete(g_indicator);
    g_indicator = nullptr;
    return false;
  }
  lv_obj_remove_flag(g_icon, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(g_icon, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_text_color(
      g_icon, lv_color_hex(kIconColor), LV_PART_MAIN);
  lv_obj_set_style_text_font(
      g_icon, &lvgl_font_material_symbols_fill_32, LV_PART_MAIN);
  lv_obj_set_style_text_align(g_icon, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
  lv_obj_set_style_text_opa(g_icon, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_label_set_text(g_icon, icon::kChevronRight);
  return true;
}

/**
 * @brief 隐藏边缘返回圆
 * @param animated 是否播放淡出动画
 */
void HideIndicator(bool animated) {
  if (g_indicator == nullptr) {
    return;
  }

  lv_anim_delete(g_indicator, SetIndicatorOpacity);
  if (!animated || g_indicator_opacity == LV_OPA_TRANSP ||
      lv_obj_has_flag(g_indicator, LV_OBJ_FLAG_HIDDEN)) {
    SetIndicatorOpacity(g_indicator, LV_OPA_TRANSP);
    lv_obj_add_flag(g_indicator, LV_OBJ_FLAG_HIDDEN);
    return;
  }

  lv_anim_t animation;
  lv_anim_init(&animation);
  lv_anim_set_var(&animation, g_indicator);
  lv_anim_set_user_data(&animation, g_indicator);
  lv_anim_set_values(
      &animation, g_indicator_opacity, LV_OPA_TRANSP);
  lv_anim_set_duration(&animation, kIndicatorFadeDurationMs);
  lv_anim_set_path_cb(&animation, lv_anim_path_ease_out);
  lv_anim_set_exec_cb(&animation, SetIndicatorOpacity);
  lv_anim_set_completed_cb(
      &animation, IndicatorFadeCompletedCallback);
  if (lv_anim_start(&animation) == nullptr) {
    SetIndicatorOpacity(g_indicator, LV_OPA_TRANSP);
    lv_obj_add_flag(g_indicator, LV_OBJ_FLAG_HIDDEN);
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
 * @param confirm_distance 确认返回所需距离
 */
void UpdateBackCommitState(
    EdgeSwipeIndicatorState* state, int inward_distance,
    int confirm_distance) {
  if (state == nullptr) {
    return;
  }

  state->inward_distance = std::max(0, inward_distance);
  const bool was_committed = state->back_committed;
  state->back_committed = state->inward_distance >= confirm_distance;
  if (state->back_committed && !was_committed) {
    PlayUiHapticFeedback();
  }
}

/**
 * @brief 根据当前触摸位置更新边缘滑动指示器
 * @param state 当前边缘滑动状态
 * @param screen_width 当前显示宽度
 * @param point 当前触摸坐标
 * @param reveal_distance 返回圆完全展开所需距离
 */
void UpdateIndicator(const EdgeSwipeIndicatorState& state,
    int screen_width, lv_point_t point, int reveal_distance) {
  if (!EnsureIndicator() || screen_width <= 0 || reveal_distance <= 0) {
    return;
  }

  lv_anim_delete(g_indicator, SetIndicatorOpacity);
  const int distance = ClampInt(
      InwardDistance(state, point), 0, reveal_distance);
  const int progress = distance * 1000 / reveal_distance;
  const int diameter = kIndicatorMinimumDiameter +
      (kIndicatorMaximumDiameter - kIndicatorMinimumDiameter) *
          progress / 1000;
  const int visible_width =
      diameter * progress / 1000 / kIndicatorVisibleDivisor;
  const int opacity =
      static_cast<int>(kIndicatorMaximumOpacity) * progress / 1000;

  int layer_height = lv_obj_get_height(lv_layer_top());
  if (layer_height <= 0) {
    layer_height = lv_obj_get_height(lv_screen_active());
  }
  const int maximum_y = layer_height > diameter
      ? layer_height - diameter
      : 0;
  const int y = ClampInt(
      state.start_point.y - diameter / 2, 0, maximum_y);
  const int x = state.from_left_edge
      ? -(diameter - visible_width)
      : screen_width - visible_width;
  const int icon_offset = state.from_left_edge
      ? (diameter - visible_width) / 2
      : -(diameter - visible_width) / 2;

  lv_obj_remove_flag(g_indicator, LV_OBJ_FLAG_HIDDEN);
  lv_obj_move_to_index(g_indicator, -1);
  lv_obj_set_size(g_indicator, diameter, diameter);
  lv_obj_set_pos(g_indicator, x, y);
  SetIndicatorOpacity(g_indicator, opacity);

  lv_obj_update_layout(g_icon);
  lv_obj_set_style_transform_pivot_x(
      g_icon, lv_obj_get_width(g_icon) / 2, LV_PART_MAIN);
  lv_obj_set_style_transform_pivot_y(
      g_icon, lv_obj_get_height(g_icon) / 2, LV_PART_MAIN);
  lv_obj_set_style_transform_rotation(
      g_icon, state.from_left_edge ? 0 : 1800, LV_PART_MAIN);
  lv_obj_align(g_icon, LV_ALIGN_CENTER, icon_offset, 0);
  lv_obj_invalidate(g_indicator);
}

/**
 * @brief 观察边缘手势并将触摸输入继续交给 LVGL 控件
 * @param input_state LVGL 指针输入状态
 * @param point 已转换到当前显示方向的触摸坐标
 * @param hardware_edge_hint 硬件是否提供了有效的边缘触摸提示
 * @param screen_width 当前显示宽度
 */
void ObservePassthroughPointerInput(lv_indev_state_t input_state,
    lv_point_t point, bool hardware_edge_hint, int screen_width) {
  if (input_state != LV_INDEV_STATE_PRESSED) {
    const bool should_navigate_back =
        g_state.tracking && g_state.active && g_state.back_committed;
    g_state = {};
    HideIndicator(true);
    if (should_navigate_back) {
      lv_async_call(ExecuteBackCallback, nullptr);
    }
    return;
  }

  if (screen_width <= 0) {
    return;
  }

  if (!g_state.pointer_pressed) {
    g_state.pointer_pressed = true;
    g_state.start_point = point;
    const int edge_width = PassthroughEdgeWidth(screen_width);
    g_state.from_left_edge = point.x <= edge_width;
    g_state.from_right_edge = point.x >= screen_width - edge_width;
    if (hardware_edge_hint && !g_state.from_left_edge &&
        !g_state.from_right_edge) {
      g_state.from_left_edge = point.x < screen_width / 2;
      g_state.from_right_edge = !g_state.from_left_edge;
    }
    g_state.tracking = g_state.from_left_edge || g_state.from_right_edge;
    HideIndicator(false);
    return;
  }

  if (!g_state.tracking) {
    return;
  }

  const int inward_distance = InwardDistance(g_state, point);
  const int vertical_distance = point.y - g_state.start_point.y;
  if (!g_state.active) {
    if (AbsInt(vertical_distance) > kPassthroughMaximumVerticalOffset) {
      g_state.tracking = false;
      HideIndicator(true);
      return;
    }
    if (inward_distance <= kPassthroughIndicatorStartDistance ||
        inward_distance < AbsInt(vertical_distance)) {
      HideIndicator(false);
      return;
    }
    g_state.active = true;
  }

  UpdateBackCommitState(
      &g_state, inward_distance, kPassthroughConfirmDistance);
  if (inward_distance >= 0) {
    UpdateIndicator(g_state, screen_width, point,
        kPassthroughConfirmDistance);
  } else {
    HideIndicator(false);
  }
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
  if (g_passthrough_mode && input_state != LV_INDEV_STATE_PRESSED) {
    ObservePassthroughPointerInput(
        input_state, point, hardware_edge_hint, 0);
    return false;
  }

  if (input_state != LV_INDEV_STATE_PRESSED) {
    const bool was_tracking = g_state.tracking;
    const bool should_navigate_back =
        g_state.tracking && g_state.back_committed;
    g_state = {};
    HideIndicator(true);
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

  if (g_passthrough_mode) {
    ObservePassthroughPointerInput(
        input_state, point, hardware_edge_hint, screen_width);
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
    g_state.active = g_state.tracking;
    if (g_state.tracking) {
      if (hardware_edge_hint) {
        g_state.start_point.x =
            g_state.from_left_edge ? 0 : screen_width - 1;
      }
      UpdateBackCommitState(&g_state,
          InwardDistance(g_state, point), kDefaultConfirmDistance);
      UpdateIndicator(
          g_state, screen_width, point, kDefaultConfirmDistance);
    }
    return g_state.tracking;
  }

  if (!g_state.tracking) {
    return false;
  }

  const int inward_distance = InwardDistance(g_state, point);
  UpdateBackCommitState(
      &g_state, inward_distance, kDefaultConfirmDistance);
  if (inward_distance >= 0) {
    UpdateIndicator(
        g_state, screen_width, point, kDefaultConfirmDistance);
  } else {
    HideIndicator(false);
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

  g_passthrough_mode = false;
  g_state = {};
  HideIndicator(false);
  g_lvgl_port = lvgl_port;
  if (g_lvgl_port == nullptr) {
    return;
  }

  EnsureIndicator();
  g_lvgl_port->SetPointerInputInterceptor(InterceptPointerInput);
}

/**
 * @brief 设置是否使用不拦截 LVGL 控件输入的边缘手势模式
 * @param enabled true 透传控件输入，false 拦截边缘手势输入
 */
void SetEdgeSwipePassthroughMode(bool enabled) {
  if (g_passthrough_mode == enabled) {
    return;
  }
  g_passthrough_mode = enabled;
  g_state = {};
  HideIndicator(false);
}

}  // namespace lilygo_box::ui
