/*
 * @Description: Settings basic page shared helpers
 * @Author: LILYGO_L
 * @Date: 2026-05-23 00:00:00
 * @LastEditTime: 2026-07-13 21:51:14
 * @License: GPL 3.0
 */
#include "ui/views/settings/settings_basic_view_common.h"

#include <algorithm>

#include "app/device_identity.h"
#include "hal/providers/screen_provider.h"
#include "ui/animation/transition_animation.h"
#include "ui/resources/fonts/icon_assets.h"
#include "ui/input/app_view_gesture_flags.h"
#include "ui/input/edge_back_gesture.h"
#include "ui/input/press_cancel.h"

namespace lilygo_box::ui {
namespace {

constexpr int kTextAreaRadius = 22;
constexpr int kTextAreaHorizontalPadding = 20;

/**
 * @brief 关闭普通设置详情页
 * @param state 设置页状态
 * @param animated 是否播放关闭动画
 */
void CloseExtraPage(SettingsViewState* state, bool animated);

/**
 * @brief 关闭普通设置二级页
 * @param state 设置页状态
 * @param animated 是否播放关闭动画
 */
void CloseNestedPage(SettingsViewState* state, bool animated);

/**
 * @brief 普通设置页关闭动画结束后清理页面对象
 * @param animation LVGL 动画对象
 */
void ExtraCloseCompletedCallback(lv_anim_t* animation) {
  SetSettingsRestoreSubPage(nullptr);
  auto* state =
      static_cast<SettingsViewState*>(lv_anim_get_user_data(animation));
  if (state == nullptr || state->settings_extra_page == nullptr) {
    return;
  }

  lv_obj_t* page = state->settings_extra_page;
  state->settings_extra_page = nullptr;
  state->settings_extra_closing = false;
  state->settings_extra_swipe = EdgeBackSwipeState();
  lv_obj_delete(page);
  RestoreSettingsListGestures(state);
}

/**
 * @brief 普通设置二级页关闭动画结束后清理页面对象
 * @param animation LVGL 动画对象
 */
void NestedCloseCompletedCallback(lv_anim_t* animation) {
  SetSettingsRestoreSubPage(nullptr);
  auto* state =
      static_cast<SettingsViewState*>(lv_anim_get_user_data(animation));
  if (state == nullptr || state->settings_nested_page == nullptr) {
    return;
  }

  lv_obj_t* page = state->settings_nested_page;
  state->settings_nested_page = nullptr;
  state->settings_nested_closing = false;
  state->settings_nested_swipe = EdgeBackSwipeState();
  lv_obj_delete(page);
}

/**
 * @brief 关闭普通设置详情页
 * @param state 设置页状态
 * @param animated 是否播放关闭动画
 */
void CloseExtraPage(SettingsViewState* state, bool animated) {
  if (state == nullptr || state->settings_extra_page == nullptr ||
      state->settings_extra_closing) {
    return;
  }

  if (state->settings_nested_page != nullptr) {
    CloseNestedPage(state, false);
  }

  if (animated &&
      StartSlideRightWindowTransition(state->settings_extra_page,
          state->config.width, kDetailSlideAnimationMs, state,
          ExtraCloseCompletedCallback)) {
    state->settings_extra_closing = true;
    return;
  }

  lv_obj_t* page = state->settings_extra_page;
  state->settings_extra_page = nullptr;
  state->settings_extra_closing = false;
  state->settings_extra_swipe = EdgeBackSwipeState();
  lv_obj_delete(page);
  RestoreSettingsListGestures(state);
}

/**
 * @brief 关闭普通设置二级页
 * @param state 设置页状态
 * @param animated 是否播放关闭动画
 */
void CloseNestedPage(SettingsViewState* state, bool animated) {
  if (state == nullptr || state->settings_nested_page == nullptr ||
      state->settings_nested_closing) {
    return;
  }

  if (animated &&
      StartSlideRightWindowTransition(state->settings_nested_page,
          state->config.width, kDetailSlideAnimationMs, state,
          NestedCloseCompletedCallback)) {
    state->settings_nested_closing = true;
    return;
  }

  lv_obj_t* page = state->settings_nested_page;
  state->settings_nested_page = nullptr;
  state->settings_nested_closing = false;
  state->settings_nested_swipe = EdgeBackSwipeState();
  lv_obj_delete(page);
}

/**
 * @brief 处理普通设置页顶部返回按钮
 * @param event LVGL 事件对象
 */
void ExtraBackClickedEventCallback(lv_event_t* event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
    return;
  }
  CloseExtraPage(
      static_cast<SettingsViewState*>(lv_event_get_user_data(event)), true);
}

/**
 * @brief 处理普通设置二级页顶部返回按钮
 * @param event LVGL 事件对象
 */
void NestedBackClickedEventCallback(lv_event_t* event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
    return;
  }
  CloseNestedPage(
      static_cast<SettingsViewState*>(lv_event_get_user_data(event)), true);
}

/**
 * @brief 处理普通设置页边缘返回手势
 * @param event LVGL 事件对象
 */
void ExtraEdgeBackEventCallback(lv_event_t* event) {
  auto* state = static_cast<SettingsViewState*>(lv_event_get_user_data(event));
  if (state == nullptr || state->settings_extra_page == nullptr ||
      state->settings_extra_closing || state->config.screen == nullptr ||
      !HandleEdgeBackSwipeEvent(event, state->config.width,
          &state->settings_extra_swipe)) {
    return;
  }

  CloseExtraPage(state, true);
  lv_event_stop_bubbling(event);
  lv_event_stop_processing(event);
}

/**
 * @brief 处理普通设置二级页边缘返回手势
 * @param event LVGL 事件对象
 */
void NestedEdgeBackEventCallback(lv_event_t* event) {
  auto* state = static_cast<SettingsViewState*>(lv_event_get_user_data(event));
  if (state == nullptr || state->settings_nested_page == nullptr ||
      state->settings_nested_closing || state->config.screen == nullptr ||
      !HandleEdgeBackSwipeEvent(event, state->config.width,
          &state->settings_nested_swipe)) {
    return;
  }

  CloseNestedPage(state, true);
  lv_event_stop_bubbling(event);
  lv_event_stop_processing(event);
}

/**
 * @brief 创建普通设置页顶栏
 * @param page 页面对象
 * @param state 设置页状态
 * @param title 页面标题
 * @param nested 是否为二级页
 * @return 创建成功返回 true，否则返回 false
 */
bool CreateBasicHeader(
    lv_obj_t* page, SettingsViewState* state, const char* title, bool nested) {
  lv_event_cb_t callback = nested ? NestedBackClickedEventCallback
                                  : ExtraBackClickedEventCallback;
  lv_obj_t* back_button = CreateToolbarButton(page, kDetailBackButtonLeft,
      kDetailBackButtonTop, callback, state);
  if (back_button == nullptr) {
    return false;
  }
  lv_obj_t* back_icon = CreateLabel(back_button, icon::kArrowBack,
      lv_color_hex(kDetailBackColor), MaterialIconFont44());
  if (back_icon == nullptr) {
    return false;
  }
  lv_obj_align(back_icon, LV_ALIGN_CENTER, kDetailBackIconOffsetX, 0);

  lv_obj_t* title_label =
      CreateLabel(page, title, lv_color_hex(kTitleColor), Font48());
  if (title_label == nullptr) {
    return false;
  }
  lv_obj_align(title_label, LV_ALIGN_TOP_LEFT, kBasicSidePadding,
      kBasicTitleTop);
  return true;
}

/**
 * @brief 创建普通设置页滚动内容容器
 * @param page 页面对象
 * @param state 设置页状态
 * @param nested 是否为二级页
 * @return 创建成功返回内容容器，否则返回 nullptr
 */
lv_obj_t* CreateBasicBody(
    lv_obj_t* page, SettingsViewState* state, bool nested) {
  lv_obj_t* body = lv_obj_create(page);
  if (body == nullptr) {
    return nullptr;
  }
  MakeTransparent(body);
  lv_obj_set_size(body, state->config.width,
      state->config.height - kBasicBodyTop);
  lv_obj_align(body, LV_ALIGN_TOP_LEFT, 0, kBasicBodyTop);
  lv_obj_set_scroll_dir(body, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(body, LV_SCROLLBAR_MODE_OFF);
  lv_obj_add_flag(body, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(body, LV_OBJ_FLAG_GESTURE_BUBBLE);
  lv_obj_remove_flag(body, LV_OBJ_FLAG_SCROLL_ELASTIC);
  AddEdgeBackSwipeEvents(body,
      nested ? NestedEdgeBackEventCallback : ExtraEdgeBackEventCallback,
      state);
  return body;
}

}  // namespace

void ApplySettingsTextAreaStyle(
    lv_obj_t* text_area, const lv_font_t* font, int height) {
  if (text_area == nullptr || font == nullptr || height <= 0) {
    return;
  }

  lv_obj_set_scrollbar_mode(text_area, LV_SCROLLBAR_MODE_OFF);
  lv_obj_set_style_text_font(text_area, font, LV_PART_MAIN);
  lv_obj_set_style_text_color(
      text_area, lv_color_hex(kPrimaryTextColor), LV_PART_MAIN);
  lv_obj_set_style_bg_color(
      text_area, lv_color_hex(kBasicTextAreaColor), LV_PART_MAIN);
  lv_obj_set_style_bg_color(
      text_area, lv_color_hex(kBasicTextAreaColor), LV_STATE_FOCUSED);
  lv_obj_set_style_bg_opa(text_area, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_bg_opa(text_area, LV_OPA_COVER, LV_STATE_FOCUSED);
  lv_obj_set_style_border_width(text_area, 0, LV_PART_MAIN);
  lv_obj_set_style_border_width(text_area, 0, LV_STATE_FOCUSED);
  lv_obj_set_style_outline_width(text_area, 0, LV_PART_MAIN);
  lv_obj_set_style_outline_width(text_area, 0, LV_STATE_FOCUSED);
  lv_obj_set_style_shadow_width(text_area, 0, LV_PART_MAIN);
  lv_obj_set_style_radius(text_area, kTextAreaRadius, LV_PART_MAIN);
  lv_obj_set_style_pad_left(
      text_area, kTextAreaHorizontalPadding, LV_PART_MAIN);
  lv_obj_set_style_pad_right(
      text_area, kTextAreaHorizontalPadding, LV_PART_MAIN);
  const int line_height =
      static_cast<int>(lv_font_get_line_height(font));
  const int vertical_padding = std::max(0, (height - line_height) / 2);
  lv_obj_set_style_pad_top(text_area, vertical_padding, LV_PART_MAIN);
  lv_obj_set_style_pad_bottom(text_area, vertical_padding, LV_PART_MAIN);

  lv_obj_t* content_label = lv_textarea_get_label(text_area);
  if (content_label != nullptr) {
    lv_obj_align(content_label, LV_ALIGN_LEFT_MID, 0, 0);
  }
}

/**
 * @brief 读取当前设备名，未设置时使用默认名称
 * @return 设备名文本
 */
const char* ReadBasicDeviceName() {
  const char* name = app::ConfiguredDeviceName();
  return (name == nullptr || name[0] == '\0') ? "LilygoBox" : name;
}

/**
 * @brief 打开普通设置详情页
 * @param state 设置页状态
 * @param title 页面标题
 * @param builder 内容构建函数
 * @return 打开成功返回 true，否则返回 false
 */
bool ShowBasicPage(SettingsViewState* state, const char* title,
    SettingsContentBuilder builder) {
  if (state == nullptr || state->root == nullptr || title == nullptr ||
      builder == nullptr) {
    return false;
  }
  if (state->settings_extra_closing) {
    return true;
  }
  if (state->settings_extra_page != nullptr) {
    lv_obj_move_to_index(state->settings_extra_page, -1);
    return true;
  }

  lv_obj_t* page = lv_obj_create(state->root);
  if (page == nullptr) {
    return false;
  }
  state->settings_extra_page = page;
  state->settings_extra_swipe = EdgeBackSwipeState();
  state->settings_extra_closing = false;
  lv_obj_add_flag(state->root, kBlockLauncherGestureFlag);
  lv_obj_remove_flag(state->root, LV_OBJ_FLAG_GESTURE_BUBBLE);

  lv_obj_remove_flag(page, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(page, LV_OBJ_FLAG_GESTURE_BUBBLE);
  AddEdgeBackSwipeEvents(page, ExtraEdgeBackEventCallback, state);
  lv_obj_set_size(page, state->config.width, state->config.height);
  lv_obj_set_pos(page, 0, 0);
  lv_obj_set_style_bg_color(page, lv_color_hex(kBackgroundColor),
      LV_PART_MAIN);
  lv_obj_set_style_bg_opa(page, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_border_width(page, 0, LV_PART_MAIN);
  lv_obj_set_style_radius(page, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(page, 0, LV_PART_MAIN);

  if (!CreateBasicHeader(page, state, title, false)) {
    CloseExtraPage(state, false);
    return false;
  }
  lv_obj_t* body = CreateBasicBody(page, state, false);
  if (body == nullptr || !builder(body, state)) {
    CloseExtraPage(state, false);
    return false;
  }

  EnableEdgeBackSwipeEventBubble(page);
  if (ConsumeSkipPageAnimation()) {
    // 旋转恢复时跳过滑入动画，直接把页面放到最终位置
    lv_obj_set_x(page, 0);
  } else if (!StartSlideLeftWindowTransition(page, state->config.width,
          kDetailSlideAnimationMs, state, nullptr)) {
    CloseExtraPage(state, false);
    return false;
  }
  return true;
}

/**
 * @brief 创建普通设置二级页面
 * @param state 设置页状态
 * @param title 页面标题
 * @param builder 内容构建函数
 * @return 页面创建成功返回 true，否则返回 false
 */
bool ShowNestedPage(SettingsViewState* state, const char* title,
    SettingsContentBuilder builder) {
  if (state == nullptr || state->settings_extra_page == nullptr ||
      title == nullptr || builder == nullptr) {
    return false;
  }
  if (state->settings_nested_page != nullptr) {
    lv_obj_delete(state->settings_nested_page);
    state->settings_nested_page = nullptr;
  }

  lv_obj_t* page = lv_obj_create(state->root);
  if (page == nullptr) {
    return false;
  }
  state->settings_nested_page = page;
  state->settings_nested_closing = false;
  state->settings_nested_swipe = EdgeBackSwipeState();
  lv_obj_remove_flag(page, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(page, LV_OBJ_FLAG_GESTURE_BUBBLE);
  AddEdgeBackSwipeEvents(page, NestedEdgeBackEventCallback, state);
  lv_obj_set_size(page, state->config.width, state->config.height);
  lv_obj_set_pos(page, 0, 0);
  lv_obj_set_style_bg_color(page, lv_color_hex(kBackgroundColor),
      LV_PART_MAIN);
  lv_obj_set_style_bg_opa(page, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_border_width(page, 0, LV_PART_MAIN);
  lv_obj_set_style_radius(page, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(page, 0, LV_PART_MAIN);

  if (!CreateBasicHeader(page, state, title, true)) {
    CloseNestedPage(state, false);
    return false;
  }
  lv_obj_t* body = CreateBasicBody(page, state, true);
  if (body == nullptr || !builder(body, state)) {
    CloseNestedPage(state, false);
    return false;
  }
  EnableEdgeBackSwipeEventBubble(page);
  if (!StartSlideLeftWindowTransition(page, state->config.width,
          kDetailSlideAnimationMs, state, nullptr)) {
    CloseNestedPage(state, false);
    return false;
  }
  return true;
}

/**
 * @brief 创建分组标题
 * @param parent 父对象
 * @param text 标题文本
 * @param y 顶部坐标
 * @param width 页面宽度
 * @return 创建成功返回 true，否则返回 false
 */
bool CreateSectionLabel(lv_obj_t* parent, const char* text, int y, int width) {
  lv_obj_t* label =
      CreateLabel(parent, text, lv_color_hex(kBasicMutedColor), Font24());
  if (label == nullptr) {
    return false;
  }
  lv_obj_set_width(label, width - 2 * kBasicSidePadding);
  lv_obj_align(label, LV_ALIGN_TOP_LEFT, kBasicSidePadding, y + 12);
  return true;
}

/**
 * @brief 在普通设置页创建分割线
 * @param parent 父对象
 * @param y 顶部坐标
 * @param width 页面宽度
 * @return 创建成功返回 true，否则返回 false
 */
bool CreateBasicDivider(lv_obj_t* parent, int y, int width) {
  lv_obj_t* divider = CreateDivider(parent, width - 2 * kBasicSidePadding);
  if (divider == nullptr) {
    return false;
  }
  lv_obj_set_pos(divider, kBasicSidePadding, y);
  return true;
}

/**
 * @brief 创建普通点击行
 * @param parent 父对象
 * @param title 标题文本
 * @param value 右侧文本
 * @param y 顶部坐标
 * @param width 页面宽度
 * @param callback 点击回调
 * @param state 设置页状态
 * @return 创建成功返回 true，否则返回 false
 */
bool CreateArrowRow(lv_obj_t* parent, const char* title, const char* value,
    int y, int width, lv_event_cb_t callback, SettingsViewState* state) {
  lv_obj_t* row = lv_obj_create(parent);
  if (row == nullptr) {
    return false;
  }
  lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_flag(row, LV_OBJ_FLAG_GESTURE_BUBBLE);
  lv_obj_set_size(row, width, kBasicRowHeight);
  lv_obj_set_pos(row, 0, y);
  lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_set_style_bg_color(row, lv_color_hex(kPressedColor),
      LV_STATE_PRESSED);
  lv_obj_set_style_bg_opa(row, kPressedOpacity, LV_STATE_PRESSED);
  lv_obj_set_style_border_width(row, 0, LV_PART_MAIN);
  lv_obj_set_style_radius(row, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(row, 0, LV_PART_MAIN);
  if (!AddPressCancelOnLeave(row)) {
    return false;
  }
  AddEdgeBackSwipeEvents(row,
      state != nullptr && state->settings_nested_page != nullptr
          ? NestedEdgeBackEventCallback
          : ExtraEdgeBackEventCallback,
      state);
  if (callback != nullptr) {
    lv_obj_add_event_cb(row, callback, LV_EVENT_CLICKED, state);
  }

  lv_obj_t* title_label =
      CreateLabel(row, title, lv_color_hex(kPrimaryTextColor), Font28());
  if (title_label == nullptr) {
    return false;
  }
  lv_obj_set_width(title_label, width - 2 * kBasicSidePadding - 170);
  lv_label_set_long_mode(title_label, LV_LABEL_LONG_DOT);
  lv_obj_align(title_label, LV_ALIGN_LEFT_MID, kBasicSidePadding, 0);

  if (value != nullptr && value[0] != '\0') {
    lv_obj_t* value_label =
        CreateLabel(row, value, lv_color_hex(kSecondaryTextColor), Font24());
    if (value_label == nullptr) {
      return false;
    }
    lv_obj_set_width(value_label, 180);
    lv_obj_set_style_text_align(value_label, LV_TEXT_ALIGN_RIGHT,
        LV_PART_MAIN);
    lv_obj_align(value_label, LV_ALIGN_RIGHT_MID,
        -(kBasicSidePadding + 40), 0);
  }

  lv_obj_t* arrow = CreateLabel(row, icon::kChevronRight,
      lv_color_hex(kSecondaryTextColor), MaterialIconFont32());
  if (arrow == nullptr) {
    return false;
  }
  lv_obj_align(arrow, LV_ALIGN_RIGHT_MID, -kBasicSidePadding, 0);
  return true;
}

/**
 * @brief 创建开关行
 * @param parent 父对象
 * @param title 标题文本
 * @param y 顶部坐标
 * @param width 页面宽度
 * @param checked 当前是否打开
 * @param callback 开关变化回调
 * @param state 设置页状态
 * @return 创建成功返回 true，否则返回 false
 */
bool CreateSwitchRow(lv_obj_t* parent, const char* title, int y, int width,
    bool checked, lv_event_cb_t callback, SettingsViewState* state) {
  lv_obj_t* row = lv_obj_create(parent);
  if (row == nullptr) {
    return false;
  }
  lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(row, LV_OBJ_FLAG_GESTURE_BUBBLE);
  lv_obj_set_size(row, width, kBasicRowHeight);
  lv_obj_set_pos(row, 0, y);
  lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_set_style_border_width(row, 0, LV_PART_MAIN);
  lv_obj_set_style_radius(row, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(row, 0, LV_PART_MAIN);
  AddEdgeBackSwipeEvents(row, ExtraEdgeBackEventCallback, state);

  lv_obj_t* label =
      CreateLabel(row, title, lv_color_hex(kPrimaryTextColor), Font28());
  if (label == nullptr) {
    return false;
  }
  lv_obj_align(label, LV_ALIGN_LEFT_MID, kBasicSidePadding, 0);

  lv_obj_t* switch_object = lv_switch_create(row);
  if (switch_object == nullptr) {
    return false;
  }
  lv_obj_add_flag(switch_object, LV_OBJ_FLAG_GESTURE_BUBBLE);
  lv_obj_set_size(switch_object, kBasicSwitchWidth, kBasicSwitchHeight);
  lv_obj_align(switch_object, LV_ALIGN_RIGHT_MID, -kBasicSidePadding, 0);
  lv_obj_set_style_anim_duration(
      switch_object, kWifiSwitchAnimationMs, LV_PART_MAIN);
  lv_obj_set_style_bg_color(switch_object, lv_color_hex(kBasicBlueColor),
      kWifiSwitchCheckedIndicatorSelector);
  lv_obj_set_style_bg_opa(switch_object, LV_OPA_COVER,
      kWifiSwitchCheckedIndicatorSelector);
  if (checked) {
    lv_obj_add_state(switch_object, LV_STATE_CHECKED);
  }
  if (callback != nullptr) {
    lv_obj_add_event_cb(switch_object, callback, LV_EVENT_VALUE_CHANGED,
        state);
  }
  return true;
}

/**
 * @brief 创建带图标的滑动条行
 * @param parent 父对象
 * @param icon_text Material Symbols 图标文本
 * @param title 标题文本
 * @param value 当前百分比
 * @param y 顶部坐标
 * @param width 页面宽度
 * @param callback 滑动条变化回调
 * @param state 设置页状态
 * @return 创建成功返回 true，否则返回 false
 */
bool CreateSliderRow(lv_obj_t* parent, const char* icon_text,
    const char* title, int value, int y, int width, lv_event_cb_t callback,
    SettingsViewState* state) {
  lv_obj_t* label =
      CreateLabel(parent, title, lv_color_hex(kPrimaryTextColor), Font28());
  if (label == nullptr) {
    return false;
  }
  constexpr int kSliderSidePadding = 50;
  constexpr int kSliderIconSize = 42;
  constexpr int kSliderTitleGap = 12;
  constexpr int kSliderIconTopOffset = -4;
  constexpr int kSliderBarTopOffset = 48;
  constexpr int kSliderBarHeight = 38;
  constexpr uint32_t kSliderIconColor =
      theme::LightNeutralTheme().outline;
  constexpr uint32_t kSliderTrackColor =
      theme::LightNeutralTheme().surface_container_high;
  lv_obj_align(label, LV_ALIGN_TOP_LEFT,
      kSliderSidePadding + kSliderIconSize + kSliderTitleGap, y);

  lv_obj_t* icon_label = CreateLabel(parent, icon_text,
      lv_color_hex(kSliderIconColor), MaterialIconFont32());
  if (icon_label == nullptr) {
    return false;
  }
  lv_obj_set_size(icon_label, kSliderIconSize, kSliderIconSize);
  lv_obj_set_style_text_align(icon_label, LV_TEXT_ALIGN_CENTER,
      LV_PART_MAIN);
  lv_obj_align(icon_label, LV_ALIGN_TOP_LEFT, kSliderSidePadding,
      y + kSliderIconTopOffset);

  lv_obj_t* slider = lv_slider_create(parent);
  if (slider == nullptr) {
    return false;
  }
  lv_obj_add_flag(slider, LV_OBJ_FLAG_GESTURE_BUBBLE);
  lv_obj_set_size(slider, width - 2 * kSliderSidePadding,
      kSliderBarHeight);
  lv_obj_align(slider, LV_ALIGN_TOP_LEFT, kSliderSidePadding,
      y + kSliderBarTopOffset);
  lv_slider_set_range(slider, 0, 100);
  lv_slider_set_value(slider, value, LV_ANIM_OFF);
  lv_obj_set_style_bg_color(slider, lv_color_hex(kSliderTrackColor),
      LV_PART_MAIN);
  lv_obj_set_style_bg_opa(slider, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_radius(slider, kSliderBarHeight / 2, LV_PART_MAIN);
  lv_obj_set_style_bg_color(slider, lv_color_hex(kBasicBlueColor),
      LV_PART_INDICATOR);
  lv_obj_set_style_bg_opa(slider, LV_OPA_COVER, LV_PART_INDICATOR);
  lv_obj_set_style_radius(slider, 0, LV_PART_INDICATOR);
  lv_obj_set_style_bg_opa(slider, LV_OPA_TRANSP, LV_PART_KNOB);
  lv_obj_set_style_pad_all(slider, 0, LV_PART_KNOB);
  if (callback != nullptr) {
    lv_obj_add_event_cb(slider, callback, LV_EVENT_VALUE_CHANGED, state);
  }
  return true;
}

/**
 * @brief 从滑动条读取百分比
 * @param event LVGL 事件对象
 * @return 当前滑动条百分比
 */
int SliderPercentFromEvent(lv_event_t* event) {
  lv_obj_t* target = lv_event_get_target_obj(event);
  if (target == nullptr) {
    return 0;
  }
  return static_cast<int>(lv_slider_get_value(target));
}

}  // namespace lilygo_box::ui
