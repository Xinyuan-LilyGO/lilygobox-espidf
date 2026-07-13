/*
 * @Description: Power menu overlay
 * @Author: LILYGO_L
 * @Date: 2026-07-07
 * @License: GPL 3.0
 */
#include "ui/views/power_menu_view.h"

#include <algorithm>

#include "ui/resources/fonts/font_assets.h"
#include "ui/resources/fonts/icon_assets.h"
#include "ui/haptic_feedback.h"
#include "ui/input/edge_back_gesture.h"

namespace lilygo_box::ui {
namespace {

constexpr int kOverlayColor = 0x111111;
constexpr int kButtonColor = 0x343A40;
constexpr int kButtonPressedColor = 0x444B52;
constexpr int kTextColor = 0xFFFFFF;
constexpr int kButtonSize = 118;
constexpr int kButtonGap = 62;
constexpr int kItemWidth = 152;
constexpr int kItemHeight = 172;
constexpr int kButtonLabelHeight = 34;

struct PowerMenuDismissState {
  std::function<void()> callback;
  std::function<void()> restart_callback;
  std::function<void()> power_off_callback;
  bool dismissed = false;
  EdgeBackSwipeState edge_swipe = {};
};

/**
 * @brief 获取 24 号 Google Sans 字体
 * @return 字体指针
 */
const lv_font_t* Font24() { return &lvgl_font_google_sans_flex_24; }

/**
 * @brief 获取 56 号关机菜单图标字体
 * @return 字体指针
 */
const lv_font_t* PowerFillIconFont56() {
  return &lvgl_font_material_symbols_fill_56;
}

/**
 * @brief 清除对象背景、边框和内边距
 * @param object LVGL 对象
 */
void MakeTransparent(lv_obj_t* object) {
  lv_obj_set_style_bg_opa(object, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_set_style_border_width(object, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(object, 0, LV_PART_MAIN);
}

/**
 * @brief 设置文本对象的颜色和字体
 * @param object LVGL 对象
 * @param color 文本颜色
 * @param font 文本字体
 */
void SetTextStyle(lv_obj_t* object, uint32_t color, const lv_font_t* font) {
  lv_obj_set_style_text_color(object, lv_color_hex(color), LV_PART_MAIN);
  lv_obj_set_style_text_font(object, font, LV_PART_MAIN);
}

/**
 * @brief 处理关机菜单重启按钮点击事件
 * @param event LVGL 事件对象
 */
void PowerMenuRestartButtonEventCallback(lv_event_t* event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
    return;
  }

  lv_event_stop_bubbling(event);
  lv_event_stop_processing(event);

  auto* state =
      static_cast<PowerMenuDismissState*>(lv_event_get_user_data(event));
  if (state != nullptr && state->restart_callback) {
    state->restart_callback();
  }
}

/**
 * @brief 处理关机菜单关机按钮点击事件
 * @param event LVGL 事件对象
 */
void PowerMenuPowerOffButtonEventCallback(lv_event_t* event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
    return;
  }

  lv_event_stop_bubbling(event);
  lv_event_stop_processing(event);

  auto* state =
      static_cast<PowerMenuDismissState*>(lv_event_get_user_data(event));
  if (state != nullptr && state->power_off_callback) {
    state->power_off_callback();
  }
}

/**
 * @brief 调用关机菜单退出回调
 * @param state 关机菜单退出状态
 */
void DismissPowerMenu(PowerMenuDismissState* state) {
  if (state != nullptr && !state->dismissed && state->callback) {
    std::function<void()> callback = state->callback;
    state->dismissed = true;
    callback();
  }
}

/**
 * @brief 处理关机菜单遮罩点击和左右滑动退出事件
 * @param event LVGL 事件对象
 */
void PowerMenuOverlayEventCallback(lv_event_t* event) {
  auto* state =
      static_cast<PowerMenuDismissState*>(lv_event_get_user_data(event));
  if (state == nullptr) {
    return;
  }

  const lv_event_code_t code = lv_event_get_code(event);
  if (code == LV_EVENT_CLICKED) {
    lv_obj_t* target = lv_event_get_target_obj(event);
    lv_obj_t* current_target = lv_event_get_current_target_obj(event);
    if (target == current_target) {
      lv_event_stop_bubbling(event);
      lv_event_stop_processing(event);
      DismissPowerMenu(state);
    }
    return;
  }

  if (code != LV_EVENT_PRESSED && code != LV_EVENT_PRESSING &&
      code != LV_EVENT_RELEASED && code != LV_EVENT_PRESS_LOST) {
    return;
  }

  lv_obj_t* current_target = lv_event_get_current_target_obj(event);
  if (HandleEdgeBackSwipeEvent(
          event, lv_obj_get_width(current_target), &state->edge_swipe)) {
    lv_event_stop_bubbling(event);
    lv_event_stop_processing(event);
    DismissPowerMenu(state);
  }
}

/**
 * @brief 给对象添加关机菜单退出事件
 * @param object 接收退出事件的对象
 * @param state 关机菜单退出状态
 */
void AddDismissEvents(lv_obj_t* object, PowerMenuDismissState* state) {
  lv_obj_add_event_cb(
      object, PowerMenuOverlayEventCallback, LV_EVENT_CLICKED, state);
  AddEdgeBackSwipeEvents(object, PowerMenuOverlayEventCallback, state);
}

/**
 * @brief 创建关机菜单文本标签
 * @param parent 父对象
 * @param text 标签文本
 * @param color 文本颜色
 * @param font 文本字体
 * @return 创建成功返回标签对象，失败返回 nullptr
 */
lv_obj_t* CreateLabel(lv_obj_t* parent, const char* text, uint32_t color,
    const lv_font_t* font) {
  lv_obj_t* label = lv_label_create(parent);
  if (label == nullptr) {
    return nullptr;
  }
  lv_label_set_text(label, text);
  SetTextStyle(label, color, font);
  lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
  return label;
}

/**
 * @brief 创建关机菜单圆形操作按钮和文字标签
 * @param parent 父对象
 * @param icon 按钮图标文本
 * @param text 按钮标题文本
 * @param event_callback 按钮点击事件回调
 * @param event_user_data 按钮事件用户数据
 * @return 创建成功返回按钮项对象，失败返回 nullptr
 */
lv_obj_t* CreateActionItem(lv_obj_t* parent, const char* icon,
    const char* text, lv_event_cb_t event_callback, void* event_user_data) {
  lv_obj_t* item = lv_obj_create(parent);
  if (item == nullptr) {
    return nullptr;
  }
  lv_obj_remove_flag(item, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(item, LV_OBJ_FLAG_EVENT_BUBBLE);
  MakeTransparent(item);
  lv_obj_set_size(item, kItemWidth, kItemHeight);

  lv_obj_t* button = lv_button_create(item);
  if (button == nullptr) {
    lv_obj_delete(item);
    return nullptr;
  }
  lv_obj_remove_flag(button, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(button, LV_OBJ_FLAG_EVENT_BUBBLE);
  lv_obj_set_size(button, kButtonSize, kButtonSize);
  lv_obj_set_style_radius(button, LV_RADIUS_CIRCLE, LV_PART_MAIN);
  lv_obj_set_style_bg_color(button, lv_color_hex(kButtonColor), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(button, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_bg_color(
      button, lv_color_hex(kButtonPressedColor), LV_STATE_PRESSED);
  lv_obj_set_style_border_width(button, 0, LV_PART_MAIN);
  lv_obj_set_style_shadow_width(button, 12, LV_PART_MAIN);
  lv_obj_set_style_shadow_opa(button, 36, LV_PART_MAIN);
  lv_obj_set_style_shadow_color(button, lv_color_hex(0x000000), LV_PART_MAIN);
  lv_obj_align(button, LV_ALIGN_TOP_MID, 0, 0);
  lv_obj_add_event_cb(button, event_callback, LV_EVENT_CLICKED,
      event_user_data);

  lv_obj_t* icon_label =
      CreateLabel(button, icon, kTextColor, PowerFillIconFont56());
  if (icon_label == nullptr) {
    lv_obj_delete(item);
    return nullptr;
  }
  lv_obj_add_flag(icon_label, LV_OBJ_FLAG_EVENT_BUBBLE);
  lv_obj_center(icon_label);

  lv_obj_t* label = CreateLabel(item, text, kTextColor, Font24());
  if (label == nullptr) {
    lv_obj_delete(item);
    return nullptr;
  }
  lv_obj_add_flag(label, LV_OBJ_FLAG_EVENT_BUBBLE);
  lv_obj_set_size(label, kItemWidth, kButtonLabelHeight);
  lv_obj_align(label, LV_ALIGN_TOP_MID, 0, kButtonSize + 12);
  return item;
}

}  // namespace

lv_obj_t* CreatePowerMenuView(lv_obj_t* parent,
    const PowerMenuViewOptions& options) {
  if (parent == nullptr) {
    return nullptr;
  }

  const int width = std::max(options.screen_width, 1);
  const int height = std::max(options.screen_height, 1);
  lv_obj_t* overlay = lv_obj_create(parent);
  if (overlay == nullptr) {
    return nullptr;
  }
  lv_obj_remove_flag(overlay, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(overlay, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_size(overlay, width, height);
  lv_obj_set_pos(overlay, 0, 0);
  lv_obj_set_style_bg_color(overlay, lv_color_hex(kOverlayColor), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(overlay, 210, LV_PART_MAIN);
  lv_obj_set_style_border_width(overlay, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(overlay, 0, LV_PART_MAIN);
  auto* dismiss_state = new PowerMenuDismissState{
      .callback = options.dismiss_callback,
      .restart_callback = options.restart_callback,
      .power_off_callback = options.power_off_callback,
  };
  AddDismissEvents(overlay, dismiss_state);
  lv_obj_add_event_cb(
      overlay,
      [](lv_event_t* event) {
        delete static_cast<PowerMenuDismissState*>(
            lv_event_get_user_data(event));
      },
      LV_EVENT_DELETE, dismiss_state);

  lv_obj_t* panel = lv_obj_create(overlay);
  if (panel == nullptr) {
    lv_obj_delete(overlay);
    return nullptr;
  }
  lv_obj_remove_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(panel, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_flag(panel, LV_OBJ_FLAG_EVENT_BUBBLE);
  MakeTransparent(panel);
  lv_obj_set_size(panel, width, height);
  lv_obj_center(panel);
  AddDismissEvents(panel, dismiss_state);

  lv_obj_t* restart = CreateActionItem(panel, icon::kRestartAlt, "Restart",
      PowerMenuRestartButtonEventCallback, dismiss_state);
  lv_obj_t* power_off = CreateActionItem(panel, icon::kPowerSettingsNew,
      "Power off", PowerMenuPowerOffButtonEventCallback, dismiss_state);
  if (restart == nullptr || power_off == nullptr) {
    lv_obj_delete(overlay);
    return nullptr;
  }

  const int offset = (kItemWidth + kButtonGap) / 2;
  const int y_offset = -height / 18;
  lv_obj_align(restart, LV_ALIGN_CENTER, -offset, y_offset);
  lv_obj_align(power_off, LV_ALIGN_CENTER, offset, y_offset);

  lv_obj_move_to_index(overlay, -1);
  return overlay;
}

}  // namespace lilygo_box::ui
