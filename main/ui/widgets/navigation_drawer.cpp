/*
 * @Description: 公共导航抽屉控件
 * @Author: LILYGO_L
 * @Date: 2026-07-11 00:00:00
 * @LastEditTime: 2026-07-11 00:00:00
 * @License: GPL 3.0
 */
#include "ui/widgets/navigation_drawer.h"

#include "ui/input/edge_back_gesture.h"

namespace lilygo_box::ui {
namespace {

/**
 * @brief 设置对象的 X 坐标
 * @param object LVGL 对象
 * @param x X 坐标
 */
void SetObjectX(void* object, int32_t x) {
  if (object != nullptr) {
    lv_obj_set_x(static_cast<lv_obj_t*>(object), x);
  }
}

/**
 * @brief 创建导航抽屉文本标签
 * @param parent 父对象
 * @param text 标签文本
 * @param color 文本颜色
 * @param font 文本字体
 * @return 创建成功返回标签，否则返回 nullptr
 */
lv_obj_t* CreateLabel(lv_obj_t* parent, const char* text, uint32_t color,
                      const lv_font_t* font) {
  lv_obj_t* label = lv_label_create(parent);
  if (label == nullptr) {
    return nullptr;
  }
  lv_label_set_text(label, text == nullptr ? "" : text);
  lv_obj_set_style_text_color(label, lv_color_hex(color), LV_PART_MAIN);
  if (font != nullptr) {
    lv_obj_set_style_text_font(label, font, LV_PART_MAIN);
  }
  return label;
}

/**
 * @brief 处理导航抽屉退出动画完成事件
 * @param animation LVGL 动画对象
 */
void CloseCompletedCallback(lv_anim_t* animation) {
  auto* state = static_cast<NavigationDrawerState*>(
      lv_anim_get_user_data(animation));
  if (state != nullptr && state->overlay != nullptr) {
    lv_obj_t* overlay = state->overlay;
    state->overlay = nullptr;
    state->panel = nullptr;
    state->panel_width = 0;
    state->edge_swipe = EdgeBackSwipeState();
    lv_obj_delete(overlay);
  }
}

/**
 * @brief 处理导航抽屉遮罩点击事件
 * @param event LVGL 事件对象
 */
void OverlayClickedEventCallback(lv_event_t* event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED ||
      lv_event_get_target_obj(event) !=
          lv_event_get_current_target_obj(event)) {
    return;
  }
  CloseNavigationDrawer(static_cast<NavigationDrawerState*>(
      lv_event_get_user_data(event)));
}

/**
 * @brief 处理导航抽屉边缘返回手势
 * @param event LVGL 事件对象
 */
void EdgeBackEventCallback(lv_event_t* event) {
  auto* state = static_cast<NavigationDrawerState*>(
      lv_event_get_user_data(event));
  if (state == nullptr) {
    return;
  }
  lv_event_stop_bubbling(event);
  if (HandleEdgeBackSwipeEvent(event, state->config.screen_width,
                               &state->edge_swipe)) {
    CloseNavigationDrawer(state);
    lv_event_stop_processing(event);
  }
}

}  // namespace

lv_obj_t* OpenNavigationDrawer(lv_obj_t* parent,
    NavigationDrawerState* state, const NavigationDrawerConfig& config) {
  if (parent == nullptr || state == nullptr || state->overlay != nullptr ||
      config.screen_width <= 0 || config.screen_height <= 0) {
    return nullptr;
  }

  lv_obj_t* overlay = lv_obj_create(parent);
  if (overlay == nullptr) {
    return nullptr;
  }
  state->config = config;
  state->overlay = overlay;
  state->edge_swipe = EdgeBackSwipeState();
  lv_obj_set_size(overlay, config.screen_width, config.screen_height);
  lv_obj_set_style_bg_color(overlay, lv_color_hex(config.scrim_color),
                            LV_PART_MAIN);
  lv_obj_set_style_bg_opa(overlay, LV_OPA_50, LV_PART_MAIN);
  lv_obj_set_style_border_width(overlay, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(overlay, 0, LV_PART_MAIN);
  lv_obj_remove_flag(overlay, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(overlay, LV_OBJ_FLAG_EVENT_BUBBLE);
  lv_obj_add_event_cb(overlay, OverlayClickedEventCallback,
                      LV_EVENT_CLICKED, state);
  AddEdgeBackSwipeEvents(overlay, EdgeBackEventCallback, state);

  const int drawer_width =
      config.screen_width * config.width_percent / 100;
  lv_obj_t* panel = lv_obj_create(overlay);
  if (panel == nullptr) {
    state->overlay = nullptr;
    lv_obj_delete(overlay);
    return nullptr;
  }
  state->panel = panel;
  state->panel_width = drawer_width;
  lv_obj_set_size(panel, drawer_width, config.screen_height);
  lv_obj_set_pos(panel, -drawer_width, 0);
  lv_obj_set_style_bg_color(panel, lv_color_hex(config.background_color),
                            LV_PART_MAIN);
  lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_border_width(panel, 0, LV_PART_MAIN);
  lv_obj_set_style_radius(panel, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(panel, 0, LV_PART_MAIN);
  lv_obj_set_scroll_dir(panel, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(panel, LV_SCROLLBAR_MODE_ACTIVE);
  lv_obj_remove_flag(panel, LV_OBJ_FLAG_EVENT_BUBBLE);
  AddEdgeBackSwipeEvents(panel, EdgeBackEventCallback, state);

  lv_obj_t* title = CreateLabel(panel, config.title,
      config.primary_text_color, config.title_font);
  if (title != nullptr) {
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 34,
        kNavigationDrawerTitleTop);
  }

  return panel;
}

bool PresentNavigationDrawer(NavigationDrawerState* state) {
  if (state == nullptr || state->overlay == nullptr ||
      state->panel == nullptr || state->panel_width <= 0) {
    return false;
  }

  lv_obj_update_layout(state->panel);
  lv_obj_move_to_index(state->overlay, -1);
  lv_obj_invalidate(state->panel);
  lv_anim_delete(state->panel, SetObjectX);
  lv_obj_set_x(state->panel, -state->panel_width);
  lv_anim_t animation;
  lv_anim_init(&animation);
  lv_anim_set_var(&animation, state->panel);
  lv_anim_set_values(&animation, -state->panel_width, 0);
  lv_anim_set_duration(&animation, state->config.animation_ms);
  lv_anim_set_path_cb(&animation, lv_anim_path_ease_out);
  lv_anim_set_exec_cb(&animation, SetObjectX);
  lv_anim_start(&animation);
  return true;
}

void CloseNavigationDrawer(NavigationDrawerState* state) {
  if (state == nullptr || state->overlay == nullptr) {
    return;
  }
  if (state->panel == nullptr) {
    lv_obj_t* overlay = state->overlay;
    state->overlay = nullptr;
    state->panel_width = 0;
    state->edge_swipe = EdgeBackSwipeState();
    lv_obj_delete(overlay);
    return;
  }

  const int drawer_width = state->panel_width;
  lv_anim_delete(state->panel, SetObjectX);
  lv_anim_t animation;
  lv_anim_init(&animation);
  lv_anim_set_var(&animation, state->panel);
  lv_anim_set_values(&animation, lv_obj_get_x(state->panel), -drawer_width);
  lv_anim_set_duration(&animation, state->config.animation_ms);
  lv_anim_set_path_cb(&animation, lv_anim_path_ease_in);
  lv_anim_set_exec_cb(&animation, SetObjectX);
  lv_anim_set_user_data(&animation, state);
  lv_anim_set_completed_cb(&animation, CloseCompletedCallback);
  lv_anim_start(&animation);
}

bool IsNavigationDrawerOpen(const NavigationDrawerState* state) {
  return state != nullptr && state->overlay != nullptr;
}

int NavigationDrawerWidth(const NavigationDrawerState* state) {
  return state == nullptr ? 0 : state->panel_width;
}

lv_obj_t* CreateNavigationDrawerItem(NavigationDrawerState* state,
    const char* symbol, const char* text, int y, lv_event_cb_t callback,
    void* callback_context) {
  if (state == nullptr || state->panel == nullptr) {
    return nullptr;
  }
  const int drawer_width = NavigationDrawerWidth(state);
  lv_obj_t* row = lv_button_create(state->panel);
  if (row == nullptr) {
    return nullptr;
  }
  lv_obj_remove_style_all(row);
  lv_obj_add_flag(row, LV_OBJ_FLAG_GESTURE_BUBBLE);
  lv_obj_add_flag(row, LV_OBJ_FLAG_EVENT_BUBBLE);
  lv_obj_set_size(row, drawer_width, kNavigationDrawerItemHeight);
  lv_obj_set_pos(row, 0, y);
  lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, LV_PART_MAIN);
  if (callback != nullptr) {
    lv_obj_set_style_bg_color(row,
        lv_color_hex(state->config.pressed_color), LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(row, LV_OPA_COVER, LV_STATE_PRESSED);
    lv_obj_add_event_cb(row, callback, LV_EVENT_CLICKED, callback_context);
  } else {
    lv_obj_remove_flag(row, LV_OBJ_FLAG_CLICKABLE);
  }

  lv_obj_t* icon = CreateLabel(row, symbol, state->config.icon_color,
                               state->config.icon_font);
  if (icon != nullptr) {
    lv_obj_align(icon, LV_ALIGN_LEFT_MID, 34, 0);
  }
  lv_obj_t* label = CreateLabel(row, text,
      state->config.primary_text_color, state->config.item_font);
  if (label != nullptr) {
    lv_obj_set_width(label, drawer_width - 130);
    lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
    lv_obj_align(label, LV_ALIGN_LEFT_MID, 94, 0);
  }
  return row;
}

lv_obj_t* CreateNavigationDrawerDivider(
    NavigationDrawerState* state, int y) {
  if (state == nullptr || state->panel == nullptr) {
    return nullptr;
  }
  lv_obj_t* divider = lv_obj_create(state->panel);
  if (divider == nullptr) {
    return nullptr;
  }
  lv_obj_set_size(divider, NavigationDrawerWidth(state), 2);
  lv_obj_set_pos(divider, 0, y);
  lv_obj_set_style_bg_color(divider,
      lv_color_hex(state->config.divider_color), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(divider, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_border_width(divider, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(divider, 0, LV_PART_MAIN);
  lv_obj_remove_flag(divider, LV_OBJ_FLAG_SCROLLABLE);
  return divider;
}

}  // namespace lilygo_box::ui
