/*
 * @Description: RF control app view
 * @Author: LILYGO_L
 * @Date: 2026-07-12 00:00:00
 * @LastEditTime: 2026-07-12 00:00:00
 * @License: GPL 3.0
 */
#include "ui/views/rf_view.h"

#include <cstddef>
#include <cstdlib>
#include <cstdio>

#include "ui/animation/transition_animation.h"
#include "ui/resources/fonts/font_assets.h"
#include "ui/resources/fonts/icon_assets.h"
#include "ui/input/edge_back_gesture.h"
#include "ui/input/press_cancel.h"
#include "ui/widgets/navigation_drawer.h"
#include "ui/widgets/shared_keyboard.h"

namespace lilygo_box::ui {
namespace {

constexpr uint32_t kMainBackgroundColor = 0xFFFBFE;
constexpr uint32_t kSurfaceContainerLowColor = 0xEEE8F4;
constexpr uint32_t kSurfaceContainerColor = 0xE7DFF0;
constexpr uint32_t kSurfaceContainerHighColor = 0xDDD2E8;
constexpr uint32_t kNoticeContainerColor = 0xF0EFF2;
constexpr uint32_t kPrimaryColor = 0x6750A4;
constexpr uint32_t kPrimaryPressedColor = 0x4F378B;
constexpr uint32_t kOnPrimaryColor = 0xFFFFFF;
constexpr uint32_t kMainTextColor = 0x1D1B20;
constexpr uint32_t kSecondaryTextColor = 0x49454F;
constexpr uint32_t kOutlineVariantColor = 0xCAC4D0;
constexpr uint32_t kPressedColor = kSurfaceContainerLowColor;
constexpr uint32_t kDisabledContainerColor = 0xE4E1E6;
constexpr uint32_t kDisabledTextColor = 0xA7A2AA;
constexpr uint32_t kUnreadColor = 0xBA1A1A;
constexpr uint32_t kSendSuccessColor = 0x2E7D32;
constexpr uint32_t kSendFailureColor = 0xBA1A1A;
constexpr uint32_t kInputErrorColor = 0xBA1A1A;
constexpr int kHeaderTop = 68;
constexpr int kListTop = 154;
constexpr int kRowHeight = 108;
constexpr int kAnimationMs = 240;
constexpr int kAddPageHeaderHeight = 232;
constexpr int kAddPageActionHeight = 124;
constexpr int kAddKeyboardHeightPercent = 35;
constexpr int kAddKeyboardTopGap = 12;
constexpr int kAddInputHeight = 70;
constexpr int kAddNameInputY = 42;
constexpr int kAddFrequencyInputY = 486;
constexpr char kFrequencyAcceptedChars[] = "0123456789";

struct RfModuleItem {
  const char* short_name;
  const char* name;
  const char* latest_message;
  const char* time;
  uint32_t color;
};

constexpr RfModuleItem kModuleItems[] = {
    {"SX1", "Gateway Node #1", "Sensor Data: T=25.3 C H=68%",
     "2 min", 0x006B5F},
    {"LR2", "Sensor Node Alpha", "Battery: 78%  Temp: 22.1 C",
     "45 min", 0x6750A4},
    {"nRF", "RC Controller", "Joystick: X=127 Y=89 BTN=0x03",
     "Now", 0x7D5700},
    {"CC1", "CC1101 Dev Kit", "ACK  Packet #1247",
     "15 min", 0xB5005A},
};

constexpr RfModuleItem kNewModuleItems[] = {
    {"SX1", "New SX1262 Module", nullptr, "Now",
     0x006B5F},
    {"LR2", "New LR2021 Module", nullptr, "Now",
     0x6750A4},
    {"CC1", "New CC1101 Module", nullptr, "Now",
     0xB5005A},
    {"nRF", "New nRF24 Module", nullptr, "Now",
     0x7D5700},
    {"RF", "New Custom RF Module", nullptr, "Now",
     0x006684},
};

constexpr size_t kRfModuleCapacity = 10;
constexpr size_t kInitialModuleCount =
    sizeof(kModuleItems) / sizeof(kModuleItems[0]);

struct RfViewState {
  AppViewConfig config;
  lv_obj_t* root = nullptr;
  lv_obj_t* detail_page = nullptr;
  lv_obj_t* detail_input = nullptr;
  lv_obj_t* detail_keyboard = nullptr;
  lv_obj_t* detail_composer_background = nullptr;
  lv_obj_t* detail_divider = nullptr;
  lv_obj_t* detail_send_button = nullptr;
  lv_obj_t* add_page = nullptr;
  lv_obj_t* add_body = nullptr;
  lv_obj_t* add_name_input = nullptr;
  lv_obj_t* add_frequency_input = nullptr;
  lv_obj_t* add_keyboard = nullptr;
  lv_obj_t* add_submit_button = nullptr;
  lv_obj_t* add_submit_label = nullptr;
  lv_obj_t* module_list = nullptr;
  lv_obj_t* summary_label = nullptr;
  lv_obj_t* add_chip_buttons[5] = {};
  lv_obj_t* add_protocol_buttons[2] = {};
  lv_obj_t* add_sf_buttons[7] = {};
  NavigationDrawerState drawer;
  EdgeBackSwipeState detail_edge_swipe = {};
  EdgeBackSwipeState add_edge_swipe = {};
  RfModuleItem modules[kRfModuleCapacity] = {};
  char module_names[kRfModuleCapacity][48] = {};
  size_t module_count = 0;
  int selected_add_chip = 0;
  int selected_add_protocol = 0;
  int selected_add_sf = 1;
  bool detail_closing = false;
  bool add_closing = false;
};

struct RfModuleAction {
  RfViewState* state = nullptr;
  size_t index = 0;
};

enum class RfAddOptionGroup {
  kChip,
  kProtocol,
  kSpreadingFactor,
};

struct RfAddOptionAction {
  RfViewState* state = nullptr;
  RfAddOptionGroup group = RfAddOptionGroup::kChip;
  int index = 0;
};

/**
 * @brief 获取 22 号 Google Sans 字体
 * @return 字体指针
 */
const lv_font_t* Font22() { return &lvgl_font_google_sans_flex_22; }

/**
 * @brief 获取 24 号 Google Sans 字体
 * @return 字体指针
 */
const lv_font_t* Font24() { return &lvgl_font_google_sans_flex_24; }

/**
 * @brief 获取 28 号 Google Sans 字体
 * @return 字体指针
 */
const lv_font_t* Font28() { return &lvgl_font_google_sans_flex_28; }

/**
 * @brief 获取 36 号 Google Sans 字体
 * @return 字体指针
 */
const lv_font_t* Font36() { return &lvgl_font_google_sans_flex_36; }

/**
 * @brief 获取 48 号 Google Sans 字体
 * @return 字体指针
 */
const lv_font_t* Font48() { return &lvgl_font_google_sans_flex_48; }

/**
 * @brief 获取 44 号轮廓图标字体
 * @return 字体指针
 */
const lv_font_t* OutlineIconFont44() {
  return &lvgl_font_material_symbols_outline_44;
}

/**
 * @brief 获取 32 号填充图标字体
 * @return 字体指针
 */
const lv_font_t* FillIconFont32() {
  return &lvgl_font_material_symbols_fill_32;
}

/**
 * @brief 获取 44 号填充图标字体
 * @return 字体指针
 */
const lv_font_t* FillIconFont44() {
  return &lvgl_font_material_symbols_fill_44;
}

/**
 * @brief 获取 56 号填充图标字体
 * @return 字体指针
 */
const lv_font_t* FillIconFont56() {
  return &lvgl_font_material_symbols_fill_56;
}

/**
 * @brief 创建射频页面文本标签
 * @param parent 父对象
 * @param text 标签文字
 * @param color 文字颜色
 * @param font 文字字体
 * @return 创建成功返回标签对象，否则返回 nullptr
 */
lv_obj_t* CreateLabel(lv_obj_t* parent, const char* text, uint32_t color,
    const lv_font_t* font) {
  lv_obj_t* label = lv_label_create(parent);
  if (label == nullptr) {
    return nullptr;
  }
  lv_label_set_text(label, text == nullptr ? "" : text);
  lv_obj_set_style_text_color(label, lv_color_hex(color), LV_PART_MAIN);
  lv_obj_set_style_text_font(label, font, LV_PART_MAIN);
  return label;
}

/**
 * @brief 释放射频页面状态
 * @param event LVGL 事件对象
 */
void RfViewDeleteEventCallback(lv_event_t* event) {
  if (lv_event_get_code(event) != LV_EVENT_DELETE) {
    return;
  }
  delete static_cast<RfViewState*>(lv_event_get_user_data(event));
}

/**
 * @brief 释放模块列表点击参数
 * @param event LVGL 事件对象
 */
void ModuleActionDeleteEventCallback(lv_event_t* event) {
  if (lv_event_get_code(event) == LV_EVENT_DELETE) {
    delete static_cast<RfModuleAction*>(lv_event_get_user_data(event));
  }
}

/**
 * @brief 处理详情页退出动画完成事件
 * @param animation LVGL 动画对象
 */
void DetailCloseCompletedCallback(lv_anim_t* animation) {
  auto* state = static_cast<RfViewState*>(
      lv_anim_get_user_data(animation));
  if (state != nullptr && state->detail_page != nullptr) {
    lv_obj_t* page = state->detail_page;
    state->detail_page = nullptr;
    state->detail_input = nullptr;
    state->detail_keyboard = nullptr;
    state->detail_composer_background = nullptr;
    state->detail_divider = nullptr;
    state->detail_send_button = nullptr;
    state->detail_edge_swipe = EdgeBackSwipeState();
    state->detail_closing = false;
    lv_obj_delete(page);
  }
}

/**
 * @brief 关闭射频模块信息详情页
 * @param state 射频页面状态
 */
void CloseModuleDetail(RfViewState* state) {
  if (state == nullptr || state->detail_page == nullptr ||
      state->detail_closing) {
    return;
  }
  HideSharedKeyboard(state->detail_keyboard);
  state->detail_closing = true;
  if (!StartSlideRightWindowTransition(state->detail_page,
      state->config.width, kAnimationMs, state,
      DetailCloseCompletedCallback)) {
    lv_obj_t* page = state->detail_page;
    state->detail_page = nullptr;
    state->detail_input = nullptr;
    state->detail_keyboard = nullptr;
    state->detail_composer_background = nullptr;
    state->detail_divider = nullptr;
    state->detail_send_button = nullptr;
    state->detail_edge_swipe = EdgeBackSwipeState();
    state->detail_closing = false;
    lv_obj_delete(page);
  }
}

/**
 * @brief 处理详情页返回按钮点击事件
 * @param event LVGL 事件对象
 */
void DetailBackClickedEventCallback(lv_event_t* event) {
  if (lv_event_get_code(event) == LV_EVENT_CLICKED) {
    CloseModuleDetail(
        static_cast<RfViewState*>(lv_event_get_user_data(event)));
  }
}

/**
 * @brief 处理射频模块详情页边缘返回手势
 * @param event LVGL 事件对象
 */
void DetailEdgeBackEventCallback(lv_event_t* event) {
  auto* state = static_cast<RfViewState*>(lv_event_get_user_data(event));
  if (state == nullptr || state->detail_page == nullptr ||
      !HandleEdgeBackSwipeEvent(event, state->config.width,
          &state->detail_edge_swipe)) {
    return;
  }
  CloseModuleDetail(state);
  lv_event_stop_bubbling(event);
  lv_event_stop_processing(event);
}

/**
 * @brief 创建带独立小圆角尾部的聊天气泡
 * @param parent 父对象
 * @param text 气泡主文本
 * @param y 顶部坐标
 * @param max_width 气泡最大宽度
 * @param outgoing 是否为右侧发送气泡
 * @param rendered_height 返回气泡实际高度
 * @return 创建成功返回气泡对象，否则返回 nullptr
 */
lv_obj_t* CreateChatBubble(lv_obj_t* parent, const char* text,
    int y, int max_width, bool outgoing, int* rendered_height) {
  if (parent == nullptr || text == nullptr || max_width <= 0 ||
      rendered_height == nullptr) {
    return nullptr;
  }
  const uint32_t background_color =
      outgoing ? kPrimaryColor : kSurfaceContainerColor;
  const uint32_t text_color = outgoing ? kOnPrimaryColor : kMainTextColor;
  lv_obj_t* bubble = lv_obj_create(parent);
  if (bubble == nullptr) {
    return nullptr;
  }
  lv_obj_remove_flag(bubble, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(bubble, LV_OBJ_FLAG_GESTURE_BUBBLE);
  lv_obj_set_style_bg_color(
      bubble, lv_color_hex(background_color), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(bubble, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_border_width(bubble, 0, LV_PART_MAIN);
  lv_obj_set_style_radius(bubble, 20, LV_PART_MAIN);
  lv_obj_set_style_pad_all(bubble, 0, LV_PART_MAIN);

  lv_obj_t* title = CreateLabel(bubble, text, text_color, Font24());
  if (title == nullptr) {
    lv_obj_delete(bubble);
    return nullptr;
  }
  lv_obj_update_layout(title);
  int bubble_width = lv_obj_get_width(title) + 36;
  if (bubble_width < 90) {
    bubble_width = 90;
  }
  if (bubble_width > max_width) {
    bubble_width = max_width;
  }
  lv_obj_set_width(title, bubble_width - 36);
  lv_label_set_long_mode(title, LV_LABEL_LONG_WRAP);
  lv_obj_update_layout(title);
  int bubble_height = lv_obj_get_height(title) + 28;
  if (bubble_height < 64) {
    bubble_height = 64;
  }
  const int x = outgoing
                    ? lv_obj_get_width(parent) - bubble_width - 28
                    : 28;
  lv_obj_set_size(bubble, bubble_width, bubble_height);
  lv_obj_set_pos(bubble, x, y);
  lv_obj_align(title, LV_ALIGN_LEFT_MID, 18, 0);

  lv_obj_t* corner = lv_obj_create(parent);
  if (corner == nullptr) {
    lv_obj_delete(bubble);
    return nullptr;
  }
  lv_obj_remove_style_all(corner);
  lv_obj_remove_flag(corner, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(corner, LV_OBJ_FLAG_GESTURE_BUBBLE);
  lv_obj_set_size(corner, 20, 20);
  lv_obj_set_pos(corner, x + (outgoing ? bubble_width - 20 : 0),
      y + bubble_height - 20);
  lv_obj_set_style_bg_color(
      corner, lv_color_hex(background_color), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(corner, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_border_width(corner, 0, LV_PART_MAIN);
  lv_obj_set_style_outline_width(corner, 0, LV_PART_MAIN);
  lv_obj_set_style_shadow_width(corner, 0, LV_PART_MAIN);
  lv_obj_set_style_radius(corner, 6, LV_PART_MAIN);
  lv_obj_set_style_pad_all(corner, 0, LV_PART_MAIN);
  *rendered_height = bubble_height;
  return bubble;
}

/**
 * @brief 创建接收消息下方的射频参数和时间
 * @param parent 父对象
 * @param rssi RSSI 参数文本
 * @param snr SNR 参数文本
 * @param time 时间文本
 * @param y 参数文本顶部坐标
 * @return 创建成功返回 true，否则返回 false
 */
bool CreateReceiveTelemetry(lv_obj_t* parent, const char* rssi,
    const char* snr, const char* time, int y) {
  if (parent == nullptr || rssi == nullptr || snr == nullptr ||
      time == nullptr) {
    return false;
  }
  lv_obj_t* parameters = CreateLabel(parent, rssi,
      kSecondaryTextColor, Font22());
  if (parameters == nullptr) {
    return false;
  }
  lv_obj_set_pos(parameters, 28, y);
  lv_obj_t* snr_label = CreateLabel(parent, snr,
      kSecondaryTextColor, Font22());
  if (snr_label == nullptr) {
    return false;
  }
  lv_obj_set_pos(snr_label, 174, y);
  lv_obj_t* time_label = CreateLabel(parent, time,
      kSecondaryTextColor, Font22());
  if (time_label == nullptr) {
    return false;
  }
  lv_obj_set_pos(time_label, 28, y + 28);
  return true;
}

/**
 * @brief 创建发送消息下方的时间和发送结果图标
 * @param parent 父对象
 * @param time 时间文本
 * @param success 是否发送成功
 * @param y 顶部坐标
 * @return 创建成功返回 true，否则返回 false
 */
bool CreateSendStatus(
    lv_obj_t* parent, const char* time, bool success, int y) {
  if (parent == nullptr || time == nullptr) {
    return false;
  }
  lv_obj_t* icon_label = CreateLabel(parent,
      success ? icon::kCheck : icon::kClose,
      success ? kSendSuccessColor : kSendFailureColor, FillIconFont32());
  if (icon_label == nullptr) {
    return false;
  }
  lv_obj_align(icon_label, LV_ALIGN_TOP_RIGHT, -28, y - 5);
  lv_obj_t* time_label = CreateLabel(
      parent, time, kSecondaryTextColor, Font22());
  if (time_label == nullptr) {
    return false;
  }
  lv_obj_align(time_label, LV_ALIGN_TOP_RIGHT, -66, y);
  return true;
}

/**
 * @brief 创建发送按钮中的 Near Me 图标
 * @param parent 图标父对象
 * @return 创建成功返回 true，否则返回 false
 */
bool CreateNearMeIcon(lv_obj_t* parent) {
  if (parent == nullptr) {
    return false;
  }
  lv_obj_t* icon_label = CreateLabel(
      parent, icon::kNearMe, kOnPrimaryColor, OutlineIconFont44());
  if (icon_label == nullptr) {
    return false;
  }
  lv_obj_align(icon_label, LV_ALIGN_CENTER, -1, 0);
  return true;
}

/**
 * @brief 调整消息输入区位置并控制共享键盘显示状态
 * @param state 射频页面状态
 * @param visible 是否显示键盘
 */
void SetDetailKeyboardVisible(RfViewState* state, bool visible) {
  if (state == nullptr || state->detail_input == nullptr ||
      state->detail_composer_background == nullptr ||
      state->detail_divider == nullptr ||
      state->detail_send_button == nullptr) {
    return;
  }
  const int keyboard_height = state->config.height *
      kAddKeyboardHeightPercent / 100;
  const int offset = visible ? keyboard_height : 0;
  lv_obj_set_y(state->detail_composer_background,
      state->config.height - 108 - offset);
  lv_obj_set_y(state->detail_divider,
      state->config.height - 108 - offset);
  lv_obj_set_y(state->detail_input,
      state->config.height - 89 - offset);
  lv_obj_set_y(state->detail_send_button,
      state->config.height - 87 - offset);
  if (!visible) {
    HideSharedKeyboard(state->detail_keyboard);
  }
}

/**
 * @brief 处理消息输入框的键盘显示和隐藏事件
 * @param event LVGL 事件对象
 */
void DetailInputEventCallback(lv_event_t* event) {
  auto* state = static_cast<RfViewState*>(lv_event_get_user_data(event));
  const lv_event_code_t code = lv_event_get_code(event);
  if (code == LV_EVENT_FOCUSED || code == LV_EVENT_CLICKED) {
    SetDetailKeyboardVisible(state, true);
  } else if (code == LV_EVENT_READY || code == LV_EVENT_CANCEL ||
             code == LV_EVENT_DEFOCUSED) {
    SetDetailKeyboardVisible(state, false);
  }
}

/**
 * @brief 创建聊天页面底部发送输入区域
 * @param page 详情页面对象
 * @param state 射频页面状态
 * @return 创建成功返回 true，否则返回 false
 */
bool CreateChatComposer(lv_obj_t* page, RfViewState* state) {
  if (page == nullptr || state == nullptr) {
    return false;
  }
  const int divider_y = state->config.height - 108;
  lv_obj_t* background = lv_obj_create(page);
  if (background == nullptr) {
    return false;
  }
  lv_obj_remove_flag(background, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(background, state->config.width, 108);
  lv_obj_set_pos(background, 0, divider_y);
  lv_obj_set_style_bg_color(background,
      lv_color_hex(kMainBackgroundColor), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(background, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_border_width(background, 0, LV_PART_MAIN);
  lv_obj_set_style_radius(background, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(background, 0, LV_PART_MAIN);
  state->detail_composer_background = background;

  lv_obj_t* divider = lv_obj_create(page);
  if (divider == nullptr) {
    return false;
  }
  lv_obj_remove_flag(divider, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(divider, state->config.width, 1);
  lv_obj_set_pos(divider, 0, divider_y);
  lv_obj_set_style_bg_color(
      divider, lv_color_hex(kOutlineVariantColor), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(divider, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_border_width(divider, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(divider, 0, LV_PART_MAIN);
  state->detail_divider = divider;

  lv_obj_t* input = lv_textarea_create(page);
  if (input == nullptr) {
    return false;
  }
  lv_obj_add_flag(input, LV_OBJ_FLAG_GESTURE_BUBBLE);
  lv_textarea_set_one_line(input, true);
  lv_obj_set_size(input, state->config.width - 142, kAddInputHeight);
  lv_obj_set_pos(input, 20, state->config.height - 89);
  lv_textarea_set_placeholder_text(input, "Enter data or HEX command...");
  lv_obj_set_style_text_font(input, Font22(), LV_PART_MAIN);
  lv_obj_set_style_text_color(
      input, lv_color_hex(kMainTextColor), LV_PART_MAIN);
  lv_obj_set_style_bg_color(
      input, lv_color_hex(kSurfaceContainerLowColor), LV_PART_MAIN);
  lv_obj_set_style_bg_color(input,
      lv_color_hex(kSurfaceContainerLowColor), LV_STATE_FOCUSED);
  lv_obj_set_style_bg_opa(input, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_bg_opa(input, LV_OPA_COVER, LV_STATE_FOCUSED);
  lv_obj_set_style_border_width(input, 0, LV_PART_MAIN);
  lv_obj_set_style_radius(input, 22, LV_PART_MAIN);
  lv_obj_set_style_pad_left(input, 20, LV_PART_MAIN);
  lv_obj_set_style_pad_right(input, 20, LV_PART_MAIN);
  const int vertical_padding = (kAddInputHeight -
      lv_font_get_line_height(Font22())) / 2;
  lv_obj_set_style_pad_top(input, vertical_padding, LV_PART_MAIN);
  lv_obj_set_style_pad_bottom(input, vertical_padding, LV_PART_MAIN);
  lv_obj_t* input_label = lv_textarea_get_label(input);
  if (input_label != nullptr) {
    lv_obj_align(input_label, LV_ALIGN_LEFT_MID, 0, 0);
  }
  state->detail_input = input;
  lv_obj_add_event_cb(
      input, DetailInputEventCallback, LV_EVENT_ALL, state);

  lv_obj_t* send = lv_button_create(page);
  if (send == nullptr) {
    return false;
  }
  lv_obj_add_flag(send, LV_OBJ_FLAG_GESTURE_BUBBLE);
  lv_obj_set_size(send, 66, 66);
  lv_obj_set_pos(send, state->config.width - 98,
      state->config.height - 87);
  lv_obj_set_style_radius(send, 33, LV_PART_MAIN);
  lv_obj_set_style_bg_color(send, lv_color_hex(kPrimaryColor), LV_PART_MAIN);
  lv_obj_set_style_bg_color(send,
      lv_color_hex(kPrimaryPressedColor), LV_STATE_PRESSED);
  lv_obj_set_style_bg_opa(send, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_bg_opa(send, LV_OPA_COVER, LV_STATE_PRESSED);
  lv_obj_set_style_border_width(send, 0, LV_PART_MAIN);
  lv_obj_set_style_shadow_width(send, 0, LV_PART_MAIN);
  if (!CreateNearMeIcon(send)) {
    return false;
  }
  state->detail_send_button = send;

  SharedKeyboardConfig keyboard_config;
  keyboard_config.width = state->config.width;
  keyboard_config.height = state->config.height *
      kAddKeyboardHeightPercent / 100;
  state->detail_keyboard = CreateSharedKeyboard(page, keyboard_config);
  if (state->detail_keyboard == nullptr ||
      !AttachSharedKeyboardToTextArea(
          state->detail_keyboard, input, nullptr)) {
    return false;
  }
  lv_obj_add_flag(state->detail_keyboard, LV_OBJ_FLAG_GESTURE_BUBBLE);
  AddEdgeBackSwipeEvents(state->detail_keyboard,
      DetailEdgeBackEventCallback, state);
  return true;
}

/**
 * @brief 创建射频模块信息详情页
 * @param state 射频页面状态
 * @param index 模块索引
 * @return 创建成功返回 true，否则返回 false
 */
bool ShowModuleDetail(RfViewState* state, size_t index) {
  if (state == nullptr || state->root == nullptr ||
      index >= state->module_count) {
    return false;
  }
  const RfModuleItem& item = state->modules[index];
  lv_obj_t* page = lv_obj_create(state->root);
  if (page == nullptr) {
    return false;
  }
  state->detail_page = page;
  state->detail_input = nullptr;
  state->detail_keyboard = nullptr;
  state->detail_composer_background = nullptr;
  state->detail_divider = nullptr;
  state->detail_send_button = nullptr;
  state->detail_edge_swipe = EdgeBackSwipeState();
  lv_obj_remove_flag(page, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(page, LV_OBJ_FLAG_GESTURE_BUBBLE);
  lv_obj_set_size(page, state->config.width, state->config.height);
  lv_obj_set_style_bg_color(
      page, lv_color_hex(kMainBackgroundColor), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(page, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_border_width(page, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(page, 0, LV_PART_MAIN);
  AddEdgeBackSwipeEvents(page, DetailEdgeBackEventCallback, state);
  lv_obj_t* back = lv_button_create(page);
  if (back == nullptr) {
    lv_obj_delete(page);
    state->detail_page = nullptr;
    return false;
  }
  lv_obj_remove_style_all(back);
  lv_obj_add_flag(back, LV_OBJ_FLAG_GESTURE_BUBBLE);
  lv_obj_set_size(back, 62, 62);
  lv_obj_set_pos(back, 18, 66);
  lv_obj_add_event_cb(
      back, DetailBackClickedEventCallback, LV_EVENT_CLICKED, state);
  lv_obj_t* back_icon = CreateLabel(
      back, icon::kArrowBack, kMainTextColor, OutlineIconFont44());
  if (back_icon != nullptr) {
    lv_obj_align(back_icon, LV_ALIGN_CENTER, -4, 0);
  }
  lv_obj_t* avatar = lv_obj_create(page);
  if (avatar != nullptr) {
    lv_obj_remove_flag(avatar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(avatar, 60, 60);
    lv_obj_set_pos(avatar, 92, 67);
    lv_obj_set_style_radius(avatar, 30, LV_PART_MAIN);
    lv_obj_set_style_bg_color(
        avatar, lv_color_hex(item.color), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(avatar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(avatar, 0, LV_PART_MAIN);
    lv_obj_t* chip = CreateLabel(
        avatar, item.short_name, 0xFFFFFF, Font22());
    if (chip != nullptr) {
      lv_obj_center(chip);
    }
  }
  lv_obj_t* title = CreateLabel(
      page, item.name, kMainTextColor, Font28());
  if (title != nullptr) {
    lv_obj_set_width(title, state->config.width - 190);
    lv_label_set_long_mode(title, LV_LABEL_LONG_DOT);
    lv_obj_set_pos(title, 170, 72);
  }
  lv_obj_t* status = CreateLabel(
      page, "Connected", item.color, Font22());
  if (status != nullptr) {
    lv_obj_set_pos(status, 170, 108);
  }

  const char* connection_text = "Connected | LoRa | 915 | SF7";
  lv_obj_t* connection_notice = lv_obj_create(page);
  if (connection_notice != nullptr) {
    lv_obj_remove_flag(connection_notice, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(
        connection_notice, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_bg_color(connection_notice,
        lv_color_hex(kNoticeContainerColor), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(connection_notice, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(connection_notice, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(connection_notice, 0, LV_PART_MAIN);
    lv_obj_t* notice_text = CreateLabel(connection_notice, connection_text,
        kSecondaryTextColor, Font22());
    if (notice_text != nullptr) {
      lv_obj_update_layout(notice_text);
      int notice_width = lv_obj_get_width(notice_text) + 36;
      const int maximum_width = state->config.width - 64;
      if (notice_width < 280) {
        notice_width = 280;
      }
      if (notice_width > maximum_width) {
        notice_width = maximum_width;
      }
      lv_obj_set_size(connection_notice, notice_width, 42);
      lv_obj_align(connection_notice, LV_ALIGN_TOP_MID, 0, 164);
      lv_obj_set_size(notice_text, notice_width - 28, 28);
      lv_label_set_long_mode(notice_text, LV_LABEL_LONG_DOT);
      lv_obj_set_style_text_align(
          notice_text, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
      lv_obj_center(notice_text);
    }
  }

  const int composer_top = state->config.height - 108;
  const int chat_top = 146;
  const int bubble_metadata_gap = 8;
  lv_obj_t* chat_body = lv_obj_create(page);
  if (chat_body == nullptr) {
    lv_obj_delete(page);
    state->detail_page = nullptr;
    return false;
  }
  lv_obj_set_pos(chat_body, 0, chat_top);
  lv_obj_set_size(
      chat_body, state->config.width, composer_top - chat_top);
  lv_obj_set_style_bg_opa(chat_body, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_set_style_border_width(chat_body, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(chat_body, 0, LV_PART_MAIN);
  lv_obj_set_scroll_dir(chat_body, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(chat_body, LV_SCROLLBAR_MODE_AUTO);
  lv_obj_add_flag(chat_body, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(chat_body, LV_OBJ_FLAG_GESTURE_BUBBLE);

  if (connection_notice != nullptr) {
    lv_obj_set_parent(connection_notice, chat_body);
    lv_obj_align(connection_notice, LV_ALIGN_TOP_MID, 0, 0);
  }

  int chat_y = 58;
  int bubble_height = 0;
  lv_obj_t* bubble = CreateChatBubble(
      chat_body, "PING", chat_y, 190, true, &bubble_height);
  if (bubble == nullptr) {
    lv_obj_delete(page);
    state->detail_page = nullptr;
    return false;
  }
  chat_y += bubble_height + bubble_metadata_gap;
  if (!CreateSendStatus(chat_body, "09:19:34", true, chat_y)) {
    lv_obj_delete(page);
    state->detail_page = nullptr;
    return false;
  }
  chat_y += 50;

  bubble = CreateChatBubble(
      chat_body, "PONG", chat_y, 190, false, &bubble_height);
  if (bubble == nullptr) {
    lv_obj_delete(page);
    state->detail_page = nullptr;
    return false;
  }
  chat_y += bubble_height + bubble_metadata_gap;
  if (!CreateReceiveTelemetry(chat_body, "RSSI  -72 dBm", "SNR  +9.5",
          "09:20:34", chat_y)) {
    lv_obj_delete(page);
    state->detail_page = nullptr;
    return false;
  }
  chat_y += 78;

  bubble = CreateChatBubble(
      chat_body, "SET FREQ 915", chat_y, 230, true,
      &bubble_height);
  if (bubble == nullptr) {
    lv_obj_delete(page);
    state->detail_page = nullptr;
    return false;
  }
  chat_y += bubble_height + bubble_metadata_gap;
  if (!CreateSendStatus(chat_body, "09:21:08", false, chat_y)) {
    lv_obj_delete(page);
    state->detail_page = nullptr;
    return false;
  }
  chat_y += 50;

  if (item.latest_message != nullptr && item.latest_message[0] != '\0') {
    bubble = CreateChatBubble(chat_body, item.latest_message, chat_y,
        state->config.width - 100, false, &bubble_height);
    if (bubble == nullptr) {
      lv_obj_delete(page);
      state->detail_page = nullptr;
      return false;
    }
    chat_y += bubble_height + bubble_metadata_gap;
    if (!CreateReceiveTelemetry(
            chat_body, "RSSI  -74 dBm", "SNR  +8.2",
            "09:23:34", chat_y)) {
      lv_obj_delete(page);
      state->detail_page = nullptr;
      return false;
    }
    chat_y += 78;
  }

  bubble = CreateChatBubble(
      chat_body, "GET STATUS", chat_y, 220, true,
      &bubble_height);
  if (bubble == nullptr) {
    lv_obj_delete(page);
    state->detail_page = nullptr;
    return false;
  }
  chat_y += bubble_height + bubble_metadata_gap;
  if (!CreateSendStatus(chat_body, "09:24:02", true, chat_y)) {
    lv_obj_delete(page);
    state->detail_page = nullptr;
    return false;
  }

  if (!CreateChatComposer(page, state)) {
    lv_obj_delete(page);
    state->detail_page = nullptr;
    state->detail_input = nullptr;
    state->detail_keyboard = nullptr;
    state->detail_composer_background = nullptr;
    state->detail_divider = nullptr;
    state->detail_send_button = nullptr;
    return false;
  }
  EnableEdgeBackSwipeEventBubble(page);
  StartSlideLeftWindowTransition(
      page, state->config.width, kAnimationMs, nullptr, nullptr);
  return true;
}

/**
 * @brief 处理模块列表行点击事件
 * @param event LVGL 事件对象
 */
void ModuleRowClickedEventCallback(lv_event_t* event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
    return;
  }
  auto* action = static_cast<RfModuleAction*>(
      lv_event_get_user_data(event));
  if (action != nullptr) {
    ShowModuleDetail(action->state, action->index);
  }
}

/**
 * @brief 关闭射频导航侧边栏
 * @param state 射频页面状态
 */
void CloseRfDrawer(RfViewState* state) {
  if (state != nullptr) {
    CloseNavigationDrawer(&state->drawer);
  }
}

/**
 * @brief 处理射频侧边栏刷新项点击事件
 * @param event LVGL 事件对象
 */
void DrawerRefreshClickedEventCallback(lv_event_t* event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
    return;
  }
  CloseRfDrawer(
      static_cast<RfViewState*>(lv_event_get_user_data(event)));
}

/**
 * @brief 显示射频页面导航侧边栏
 * @param state 射频页面状态
 */
void ShowRfDrawer(RfViewState* state) {
  if (state == nullptr || state->root == nullptr ||
      IsNavigationDrawerOpen(&state->drawer)) {
    return;
  }
  NavigationDrawerConfig config;
  config.screen_width = state->config.width;
  config.screen_height = state->config.height;
  config.background_color = kMainBackgroundColor;
  config.primary_text_color = kMainTextColor;
  config.icon_color = kSecondaryTextColor;
  config.pressed_color = kPressedColor;
  config.divider_color = kOutlineVariantColor;
  config.title = "RF";
  config.title_font = Font36();
  config.item_font = Font28();
  config.icon_font = FillIconFont44();
  if (OpenNavigationDrawer(
      state->root, &state->drawer, config) == nullptr) {
    return;
  }
  int y = kNavigationDrawerContentTop;
  CreateNavigationDrawerItem(&state->drawer, icon::kRefresh,
      "Refresh modules", y, DrawerRefreshClickedEventCallback, state);
  y += kNavigationDrawerItemHeight + 12;
  CreateNavigationDrawerDivider(&state->drawer, y);
  y += 18;
  CreateNavigationDrawerItem(&state->drawer, icon::kSettings,
      "Settings", y, nullptr, state);
  PresentNavigationDrawer(&state->drawer);
}

/**
 * @brief 处理射频主页面菜单按钮点击事件
 * @param event LVGL 事件对象
 */
void MenuClickedEventCallback(lv_event_t* event) {
  if (lv_event_get_code(event) == LV_EVENT_CLICKED) {
    ShowRfDrawer(
        static_cast<RfViewState*>(lv_event_get_user_data(event)));
  }
}

/**
 * @brief 创建射频模块头像和未读消息提示点
 * @param row 模块列表行
 * @param item 模块数据
 * @return 创建成功返回 true，否则返回 false
 */
bool CreateModuleAvatar(lv_obj_t* row, const RfModuleItem& item) {
  lv_obj_t* avatar = lv_obj_create(row);
  if (avatar == nullptr) {
    return false;
  }
  lv_obj_remove_flag(avatar, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(avatar, 68, 68);
  lv_obj_align(avatar, LV_ALIGN_LEFT_MID, 30, 0);
  lv_obj_set_style_radius(avatar, 34, LV_PART_MAIN);
  lv_obj_set_style_bg_color(
      avatar, lv_color_hex(item.color), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(avatar, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_border_width(avatar, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(avatar, 0, LV_PART_MAIN);
  lv_obj_t* chip = CreateLabel(
      avatar, item.short_name, 0xFFFFFF, Font22());
  if (chip != nullptr) {
    lv_obj_center(chip);
  }
  const bool has_unread_message = item.latest_message != nullptr &&
                                  item.latest_message[0] != '\0';
  if (has_unread_message) {
    lv_obj_t* dot = lv_obj_create(row);
    if (dot == nullptr) {
      return false;
    }
    lv_obj_remove_flag(dot, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(dot, 16, 16);
    lv_obj_align(dot, LV_ALIGN_LEFT_MID, 85, 27);
    lv_obj_set_style_radius(dot, 8, LV_PART_MAIN);
    lv_obj_set_style_bg_color(
        dot, lv_color_hex(kUnreadColor), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(dot,
                                  lv_color_hex(kMainBackgroundColor),
                                  LV_PART_MAIN);
    lv_obj_set_style_border_width(dot, 3, LV_PART_MAIN);
  }
  return true;
}

/**
 * @brief 创建单个射频模块列表行
 * @param parent 列表父对象
 * @param item 模块数据
 * @param state 射频页面状态
 * @param index 模块索引
 * @param y 行顶部坐标
 * @param width 行宽度
 * @return 创建成功返回 true，否则返回 false
 */
bool CreateModuleRow(lv_obj_t* parent, const RfModuleItem& item,
    RfViewState* state, size_t index, int y, int width) {
  lv_obj_t* row = lv_button_create(parent);
  if (row == nullptr) {
    return false;
  }
  lv_obj_remove_style_all(row);
  lv_obj_add_flag(row, LV_OBJ_FLAG_GESTURE_BUBBLE);
  lv_obj_set_size(row, width, kRowHeight);
  lv_obj_set_pos(row, 0, y);
  lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_set_style_bg_color(row, lv_color_hex(kSurfaceContainerLowColor),
                            LV_STATE_PRESSED);
  lv_obj_set_style_bg_opa(row, LV_OPA_COVER, LV_STATE_PRESSED);
  if (!AddPressCancelOnLeave(row) || !CreateModuleAvatar(row, item)) {
    lv_obj_delete(row);
    return false;
  }
  auto* action = new RfModuleAction{.state = state, .index = index};
  lv_obj_add_event_cb(row, ModuleRowClickedEventCallback,
                      LV_EVENT_CLICKED, action);
  lv_obj_add_event_cb(row, ModuleActionDeleteEventCallback,
                      LV_EVENT_DELETE, action);
  lv_obj_t* title = CreateLabel(
      row, item.name, kMainTextColor, Font28());
  if (title != nullptr) {
    lv_obj_set_size(title, width - 250, 36);
    lv_label_set_long_mode(title, LV_LABEL_LONG_DOT);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 120, 17);
  }
  lv_obj_t* time = CreateLabel(
      row, item.time, kSecondaryTextColor, Font22());
  if (time != nullptr) {
    lv_obj_align(time, LV_ALIGN_TOP_RIGHT, -28, 20);
  }
  if (item.latest_message != nullptr && item.latest_message[0] != '\0') {
    lv_obj_t* message = CreateLabel(
        row, item.latest_message, kSecondaryTextColor, Font22());
    if (message != nullptr) {
      lv_obj_set_size(message, width - 174, 30);
      lv_label_set_long_mode(message, LV_LABEL_LONG_DOT);
      lv_obj_align(message, LV_ALIGN_TOP_LEFT, 120, 60);
    }
  }
  lv_obj_t* divider = lv_obj_create(row);
  if (divider != nullptr) {
    lv_obj_remove_flag(divider, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(divider, width - 120, 1);
    lv_obj_align(divider, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
    lv_obj_set_style_bg_color(
        divider, lv_color_hex(kOutlineVariantColor), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(divider, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(divider, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(divider, 0, LV_PART_MAIN);
  }
  return true;
}

/**
 * @brief 重新构建射频模块列表并更新数量
 * @param state 射频页面状态
 * @return 构建成功返回 true，否则返回 false
 */
bool RenderModuleList(RfViewState* state) {
  if (state == nullptr || state->module_list == nullptr) {
    return false;
  }
  lv_obj_clean(state->module_list);
  for (size_t index = 0; index < state->module_count; ++index) {
    if (!CreateModuleRow(state->module_list, state->modules[index], state,
        index, static_cast<int>(index) * kRowHeight,
        state->config.width)) {
      return false;
    }
  }
  if (state->summary_label != nullptr) {
    char summary[32] = {};
    std::snprintf(summary, sizeof(summary), "%u modules",
        static_cast<unsigned>(state->module_count));
    lv_label_set_text(state->summary_label, summary);
  }
  return true;
}

/**
 * @brief 创建射频主页面顶部菜单和标题
 * @param parent 页面根对象
 * @param state 射频页面状态
 * @return 创建成功返回 true，否则返回 false
 */
bool CreateHeader(lv_obj_t* parent, RfViewState* state) {
  lv_obj_t* menu = lv_button_create(parent);
  if (menu == nullptr) {
    return false;
  }
  lv_obj_remove_style_all(menu);
  lv_obj_set_size(menu, 72, 72);
  lv_obj_align(menu, LV_ALIGN_TOP_LEFT, 20, kHeaderTop - 2);
  lv_obj_set_style_bg_opa(menu, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_set_style_bg_opa(menu, LV_OPA_TRANSP, LV_STATE_PRESSED);
  lv_obj_add_event_cb(
      menu, MenuClickedEventCallback, LV_EVENT_CLICKED, state);
  lv_obj_t* menu_icon = CreateLabel(
      menu, icon::kMenu, kMainTextColor, FillIconFont56());
  if (menu_icon != nullptr) {
    lv_obj_center(menu_icon);
  }
  lv_obj_t* title = CreateLabel(
      parent, "RF", kMainTextColor, Font36());
  if (title == nullptr) {
    return false;
  }
  lv_obj_align(title, LV_ALIGN_TOP_LEFT, 104, kHeaderTop);
  lv_obj_t* summary = CreateLabel(
      parent, "0 modules", kSecondaryTextColor, Font24());
  if (summary != nullptr) {
    state->summary_label = summary;
    lv_obj_align(summary, LV_ALIGN_TOP_LEFT, 104, kHeaderTop + 42);
  }
  return true;
}

/**
 * @brief 释放添加模块选项的点击参数
 * @param event LVGL 事件对象
 */
void AddOptionActionDeleteEventCallback(lv_event_t* event) {
  if (lv_event_get_code(event) == LV_EVENT_DELETE) {
    delete static_cast<RfAddOptionAction*>(lv_event_get_user_data(event));
  }
}

/**
 * @brief 更新一组选项按钮的选中样式
 * @param buttons 选项按钮数组
 * @param count 选项数量
 * @param selected_index 当前选中索引
 */
void UpdateOptionButtonGroup(
    lv_obj_t** buttons, int count, int selected_index) {
  if (buttons == nullptr) {
    return;
  }
  for (int index = 0; index < count; ++index) {
    lv_obj_t* button = buttons[index];
    if (button == nullptr) {
      continue;
    }
    const bool selected = index == selected_index;
    lv_obj_set_style_bg_color(button,
        lv_color_hex(selected ? kPrimaryColor
                              : kSurfaceContainerColor),
        LV_PART_MAIN);
    lv_obj_set_style_bg_color(button,
        lv_color_hex(selected ? kPrimaryPressedColor
                              : kSurfaceContainerHighColor),
        LV_STATE_PRESSED);
    lv_obj_t* label = lv_obj_get_child(button, 0);
    if (label != nullptr) {
      lv_obj_set_style_text_color(label,
          lv_color_hex(selected ? kOnPrimaryColor
                                : kMainTextColor),
          LV_PART_MAIN);
    }
  }
}

/**
 * @brief 更新添加模块页面的所有选项样式
 * @param state 射频页面状态
 */
void UpdateAddOptionSelection(RfViewState* state) {
  if (state == nullptr) {
    return;
  }
  UpdateOptionButtonGroup(
      state->add_chip_buttons, 5, state->selected_add_chip);
  UpdateOptionButtonGroup(
      state->add_protocol_buttons, 2, state->selected_add_protocol);
  UpdateOptionButtonGroup(
      state->add_sf_buttons, 7, state->selected_add_sf);
}

/**
 * @brief 判断当前芯片的工作频率是否处于可设置范围
 * @param chip_index 芯片选项索引
 * @param frequency_mhz 以 MHz 为单位的工作频率
 * @return 频率有效返回 true，否则返回 false
 */
bool IsFrequencyValidForChip(int chip_index, long frequency_mhz) {
  switch (chip_index) {
    case 0:
      return frequency_mhz >= 150 && frequency_mhz <= 960;
    case 1:
      return (frequency_mhz >= 150 && frequency_mhz <= 960) ||
             (frequency_mhz >= 2400 && frequency_mhz <= 2500);
    case 2:
      return (frequency_mhz >= 300 && frequency_mhz <= 348) ||
             (frequency_mhz >= 387 && frequency_mhz <= 464) ||
             (frequency_mhz >= 779 && frequency_mhz <= 928);
    case 3:
      return frequency_mhz >= 2400 && frequency_mhz <= 2525;
    case 4:
      return frequency_mhz >= 1 && frequency_mhz <= 9999;
    default:
      return false;
  }
}

/**
 * @brief 校验添加模块页面中输入的工作频率
 * @param state 射频页面状态
 * @return 频率格式和范围正确返回 true，否则返回 false
 */
bool IsAddFrequencyValid(const RfViewState* state) {
  if (state == nullptr || state->add_frequency_input == nullptr) {
    return false;
  }
  const char* text = lv_textarea_get_text(state->add_frequency_input);
  if (text == nullptr || text[0] == '\0') {
    return false;
  }
  char* end = nullptr;
  const long frequency_mhz = std::strtol(text, &end, 10);
  return end != nullptr && end[0] == '\0' &&
         IsFrequencyValidForChip(
             state->selected_add_chip, frequency_mhz);
}

/**
 * @brief 根据频率校验结果更新输入框错误边框
 * @param state 射频页面状态
 */
void UpdateAddFrequencyErrorStyle(RfViewState* state) {
  if (state == nullptr || state->add_frequency_input == nullptr) {
    return;
  }
  const char* text = lv_textarea_get_text(state->add_frequency_input);
  const bool show_error = text != nullptr && text[0] != '\0' &&
                          !IsAddFrequencyValid(state);
  const int outline_width = show_error ? 2 : 0;
  lv_obj_set_style_border_width(
      state->add_frequency_input, 0, LV_PART_MAIN);
  lv_obj_set_style_border_width(
      state->add_frequency_input, 0, LV_STATE_FOCUSED);
  lv_obj_set_style_outline_width(
      state->add_frequency_input, outline_width, LV_PART_MAIN);
  lv_obj_set_style_outline_width(
      state->add_frequency_input, outline_width, LV_STATE_FOCUSED);
  lv_obj_set_style_outline_color(state->add_frequency_input,
      lv_color_hex(kInputErrorColor), LV_PART_MAIN);
  lv_obj_set_style_outline_color(state->add_frequency_input,
      lv_color_hex(kInputErrorColor), LV_STATE_FOCUSED);
  lv_obj_set_style_outline_opa(
      state->add_frequency_input, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_outline_opa(
      state->add_frequency_input, LV_OPA_COVER, LV_STATE_FOCUSED);
  lv_obj_set_style_outline_pad(
      state->add_frequency_input, -2, LV_PART_MAIN);
  lv_obj_set_style_outline_pad(
      state->add_frequency_input, -2, LV_STATE_FOCUSED);
}

/**
 * @brief 判断添加模块页面的必填信息是否完整
 * @param state 射频页面状态
 * @return 信息完整返回 true，否则返回 false
 */
bool IsAddModuleFormComplete(const RfViewState* state) {
  if (state == nullptr || state->add_name_input == nullptr ||
      state->add_frequency_input == nullptr ||
      state->module_count >= kRfModuleCapacity) {
    return false;
  }
  const char* name = lv_textarea_get_text(state->add_name_input);
  const char* frequency =
      lv_textarea_get_text(state->add_frequency_input);
  return name != nullptr && name[0] != '\0' && frequency != nullptr &&
         frequency[0] != '\0' && IsAddFrequencyValid(state) &&
         state->selected_add_chip >= 0 &&
         state->selected_add_chip < 5 &&
         state->selected_add_protocol >= 0 &&
         state->selected_add_protocol < 2 && state->selected_add_sf >= 0 &&
         state->selected_add_sf < 7;
}

/**
 * @brief 更新添加模块提交按钮的启用状态
 * @param state 射频页面状态
 */
void UpdateAddSubmitButton(RfViewState* state) {
  if (state == nullptr || state->add_submit_button == nullptr) {
    return;
  }
  UpdateAddFrequencyErrorStyle(state);
  const bool enabled = IsAddModuleFormComplete(state);
  if (enabled) {
    lv_obj_remove_state(state->add_submit_button, LV_STATE_DISABLED);
  } else {
    lv_obj_add_state(state->add_submit_button, LV_STATE_DISABLED);
  }
  lv_obj_set_style_bg_color(state->add_submit_button,
      lv_color_hex(enabled ? kPrimaryColor : kDisabledContainerColor),
      LV_PART_MAIN);
  if (state->add_submit_label != nullptr) {
    lv_obj_set_style_text_color(state->add_submit_label,
        lv_color_hex(enabled ? kOnPrimaryColor : kDisabledTextColor),
        LV_PART_MAIN);
  }
}

/**
 * @brief 处理添加模块参数选项点击事件
 * @param event LVGL 事件对象
 */
void AddOptionClickedEventCallback(lv_event_t* event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
    return;
  }
  auto* action = static_cast<RfAddOptionAction*>(
      lv_event_get_user_data(event));
  if (action == nullptr || action->state == nullptr) {
    return;
  }
  if (action->group == RfAddOptionGroup::kChip) {
    action->state->selected_add_chip = action->index;
  } else if (action->group == RfAddOptionGroup::kProtocol) {
    action->state->selected_add_protocol = action->index;
  } else {
    action->state->selected_add_sf = action->index;
  }
  UpdateAddOptionSelection(action->state);
  UpdateAddSubmitButton(action->state);
}

/**
 * @brief 调整键盘显示状态并保证当前输入框可见
 * @param state 射频页面状态
 * @param input 当前编辑的输入框
 * @param visible 是否显示键盘
 */
void SetAddKeyboardVisible(
    RfViewState* state, lv_obj_t* input, bool visible) {
  if (state == nullptr || state->add_body == nullptr) {
    return;
  }
  const int normal_height = state->config.height -
      kAddPageHeaderHeight - kAddPageActionHeight;
  if (!visible) {
    HideSharedKeyboard(state->add_keyboard);
    lv_obj_set_height(state->add_body, normal_height);
    lv_obj_update_layout(state->add_body);
    return;
  }

  const int keyboard_height =
      state->config.height * kAddKeyboardHeightPercent / 100;
  const int visible_height = state->config.height - keyboard_height -
      kAddPageHeaderHeight - kAddKeyboardTopGap;
  if (visible_height <= 0 || input == nullptr) {
    return;
  }
  lv_obj_set_height(state->add_body, visible_height);
  lv_obj_update_layout(state->add_body);
  const int input_y = input == state->add_frequency_input
                          ? kAddFrequencyInputY
                          : kAddNameInputY;
  int scroll_y = input_y - 18;
  if (scroll_y < 0) {
    scroll_y = 0;
  }
  lv_obj_scroll_to_y(state->add_body, scroll_y, LV_ANIM_ON);
}

/**
 * @brief 处理添加模块输入框状态和内容变化事件
 * @param event LVGL 事件对象
 */
void AddInputEventCallback(lv_event_t* event) {
  auto* state = static_cast<RfViewState*>(lv_event_get_user_data(event));
  const lv_event_code_t code = lv_event_get_code(event);
  if (code == LV_EVENT_VALUE_CHANGED) {
    UpdateAddSubmitButton(state);
    return;
  }
  if (code == LV_EVENT_FOCUSED || code == LV_EVENT_CLICKED) {
    SetAddKeyboardVisible(
        state, lv_event_get_target_obj(event), true);
  } else if (code == LV_EVENT_READY || code == LV_EVENT_CANCEL ||
             code == LV_EVENT_DEFOCUSED) {
    SetAddKeyboardVisible(state, nullptr, false);
  }
}

/**
 * @brief 处理添加模块页面退出动画完成事件
 * @param animation LVGL 动画对象
 */
void AddPageCloseCompletedCallback(lv_anim_t* animation) {
  auto* state = static_cast<RfViewState*>(
      lv_anim_get_user_data(animation));
  if (state == nullptr || state->add_page == nullptr) {
    return;
  }
  lv_obj_t* page = state->add_page;
  state->add_page = nullptr;
  state->add_body = nullptr;
  state->add_name_input = nullptr;
  state->add_frequency_input = nullptr;
  state->add_keyboard = nullptr;
  state->add_submit_button = nullptr;
  state->add_submit_label = nullptr;
  state->add_edge_swipe = EdgeBackSwipeState();
  state->add_closing = false;
  lv_obj_delete(page);
}

/**
 * @brief 使用退出动画关闭添加模块页面
 * @param state 射频页面状态
 */
void CloseAddModulePage(RfViewState* state) {
  if (state == nullptr || state->add_page == nullptr ||
      state->add_closing) {
    return;
  }
  HideSharedKeyboard(state->add_keyboard);
  state->add_closing = true;
  if (!StartSlideRightWindowTransition(state->add_page,
      state->config.width, kAnimationMs, state,
      AddPageCloseCompletedCallback)) {
    lv_obj_t* page = state->add_page;
    state->add_page = nullptr;
    state->add_body = nullptr;
    state->add_name_input = nullptr;
    state->add_frequency_input = nullptr;
    state->add_keyboard = nullptr;
    state->add_submit_button = nullptr;
    state->add_submit_label = nullptr;
    state->add_edge_swipe = EdgeBackSwipeState();
    state->add_closing = false;
    lv_obj_delete(page);
  }
}

/**
 * @brief 处理添加模块页面返回按钮点击事件
 * @param event LVGL 事件对象
 */
void AddPageBackClickedEventCallback(lv_event_t* event) {
  if (lv_event_get_code(event) == LV_EVENT_CLICKED) {
    CloseAddModulePage(
        static_cast<RfViewState*>(lv_event_get_user_data(event)));
  }
}

/**
 * @brief 处理添加模块页面边缘返回手势
 * @param event LVGL 事件对象
 */
void AddPageEdgeBackEventCallback(lv_event_t* event) {
  auto* state = static_cast<RfViewState*>(lv_event_get_user_data(event));
  if (state == nullptr || state->add_page == nullptr ||
      !HandleEdgeBackSwipeEvent(event, state->config.width,
          &state->add_edge_swipe)) {
    return;
  }
  CloseAddModulePage(state);
  lv_event_stop_bubbling(event);
  lv_event_stop_processing(event);
}

/**
 * @brief 处理添加模块页面空白区域点击事件
 * @param event LVGL 事件对象
 */
void AddPageBackgroundClickedEventCallback(lv_event_t* event) {
  if (lv_event_get_code(event) == LV_EVENT_CLICKED &&
      lv_event_get_target_obj(event) ==
          lv_event_get_current_target_obj(event)) {
    auto* state = static_cast<RfViewState*>(
        lv_event_get_user_data(event));
    if (state != nullptr) {
      SetAddKeyboardVisible(state, nullptr, false);
    }
  }
}

/**
 * @brief 处理添加模块提交按钮点击事件
 * @param event LVGL 事件对象
 */
void AddModuleSubmitClickedEventCallback(lv_event_t* event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
    return;
  }
  auto* state = static_cast<RfViewState*>(lv_event_get_user_data(event));
  if (!IsAddModuleFormComplete(state)) {
    return;
  }
  const size_t index = state->module_count;
  const char* name = lv_textarea_get_text(state->add_name_input);
  std::snprintf(state->module_names[index],
      sizeof(state->module_names[index]), "%s", name);
  state->modules[index] = kNewModuleItems[state->selected_add_chip];
  state->modules[index].name = state->module_names[index];
  ++state->module_count;
  RenderModuleList(state);
  CloseAddModulePage(state);
}

/**
 * @brief 创建添加模块页面的参数标题
 * @param parent 父对象
 * @param text 标题文本
 * @param y 顶部坐标
 * @return 创建成功返回 true，否则返回 false
 */
bool CreateAddParameterTitle(lv_obj_t* parent, const char* text, int y) {
  lv_obj_t* label = CreateLabel(
      parent, text, kPrimaryColor, Font22());
  if (label == nullptr) {
    return false;
  }
  lv_obj_set_pos(label, 28, y);
  return true;
}

/**
 * @brief 创建添加模块页面的单个选项按钮
 * @param parent 父对象
 * @param state 射频页面状态
 * @param group 选项分组
 * @param index 选项索引
 * @param text 选项文本
 * @param x 左侧坐标
 * @param y 顶部坐标
 * @param width 按钮宽度
 * @param height 按钮高度
 * @return 创建成功返回按钮对象，否则返回 nullptr
 */
lv_obj_t* CreateAddOptionButton(lv_obj_t* parent, RfViewState* state,
    RfAddOptionGroup group, int index, const char* text, int x, int y,
    int width, int height) {
  lv_obj_t* button = lv_button_create(parent);
  if (button == nullptr) {
    return nullptr;
  }
  lv_obj_remove_flag(button, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(button, LV_OBJ_FLAG_GESTURE_BUBBLE);
  lv_obj_set_size(button, width, height);
  lv_obj_set_pos(button, x, y);
  lv_obj_set_style_radius(button, height / 2, LV_PART_MAIN);
  lv_obj_set_style_bg_color(
      button, lv_color_hex(kSurfaceContainerColor), LV_PART_MAIN);
  lv_obj_set_style_bg_color(
      button, lv_color_hex(kSurfaceContainerHighColor), LV_STATE_PRESSED);
  lv_obj_set_style_bg_opa(button, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_border_width(button, 0, LV_PART_MAIN);
  lv_obj_set_style_shadow_width(button, 0, LV_PART_MAIN);
  if (!AddPressCancelOnLeave(button)) {
    lv_obj_delete(button);
    return nullptr;
  }
  auto* action = new RfAddOptionAction{
      .state = state,
      .group = group,
      .index = index,
  };
  lv_obj_add_event_cb(button, AddOptionClickedEventCallback,
      LV_EVENT_CLICKED, action);
  lv_obj_add_event_cb(button, AddOptionActionDeleteEventCallback,
      LV_EVENT_DELETE, action);
  lv_obj_t* label = CreateLabel(button, text, kMainTextColor, Font22());
  if (label == nullptr) {
    lv_obj_delete(button);
    return nullptr;
  }
  lv_obj_center(label);
  return button;
}

/**
 * @brief 创建添加模块页面的文本输入框
 * @param parent 父对象
 * @param state 射频页面状态
 * @param placeholder 占位文本
 * @param text 初始文本
 * @param y 顶部坐标
 * @param max_length 最大输入长度
 * @return 创建成功返回输入框对象，否则返回 nullptr
 */
lv_obj_t* CreateAddTextArea(lv_obj_t* parent, RfViewState* state,
    const char* placeholder, const char* text, int y, int max_length) {
  lv_obj_t* input = lv_textarea_create(parent);
  if (input == nullptr) {
    return nullptr;
  }
  lv_obj_add_flag(input, LV_OBJ_FLAG_GESTURE_BUBBLE);
  lv_textarea_set_one_line(input, true);
  lv_obj_set_scrollbar_mode(input, LV_SCROLLBAR_MODE_OFF);
  lv_obj_set_size(
      input, state->config.width - 56, kAddInputHeight);
  lv_obj_set_pos(input, 28, y);
  lv_textarea_set_max_length(input, max_length);
  lv_textarea_set_placeholder_text(input, placeholder);
  lv_textarea_set_text(input, text);
  lv_obj_set_style_text_font(input, Font24(), LV_PART_MAIN);
  lv_obj_set_style_text_color(
      input, lv_color_hex(kMainTextColor), LV_PART_MAIN);
  lv_obj_set_style_bg_color(
      input, lv_color_hex(kSurfaceContainerLowColor), LV_PART_MAIN);
  lv_obj_set_style_bg_color(
      input, lv_color_hex(kSurfaceContainerLowColor), LV_STATE_FOCUSED);
  lv_obj_set_style_bg_opa(input, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_border_width(input, 0, LV_PART_MAIN);
  lv_obj_set_style_border_width(input, 0, LV_STATE_FOCUSED);
  lv_obj_set_style_outline_width(input, 0, LV_PART_MAIN);
  lv_obj_set_style_outline_width(input, 0, LV_STATE_FOCUSED);
  lv_obj_set_style_shadow_width(input, 0, LV_PART_MAIN);
  lv_obj_set_style_radius(input, 22, LV_PART_MAIN);
  lv_obj_set_style_pad_left(input, 20, LV_PART_MAIN);
  lv_obj_set_style_pad_right(input, 20, LV_PART_MAIN);
  const int vertical_padding =
      (kAddInputHeight - lv_font_get_line_height(Font24())) / 2;
  lv_obj_set_style_pad_top(input, vertical_padding, LV_PART_MAIN);
  lv_obj_set_style_pad_bottom(input, vertical_padding, LV_PART_MAIN);
  lv_obj_t* content_label = lv_textarea_get_label(input);
  if (content_label != nullptr) {
    lv_obj_align(content_label, LV_ALIGN_LEFT_MID, 0, 0);
  }
  lv_obj_add_event_cb(
      input, AddInputEventCallback, LV_EVENT_ALL, state);
  AddEdgeBackSwipeEvents(input, AddPageEdgeBackEventCallback, state);
  return input;
}

/**
 * @brief 创建添加模块页面的参数内容
 * @param state 射频页面状态
 * @return 创建成功返回 true，否则返回 false
 */
bool CreateAddModuleContent(RfViewState* state) {
  lv_obj_t* body = state->add_body;
  if (body == nullptr || !CreateAddParameterTitle(
      body, "DEVICE NAME", 8)) {
    return false;
  }
  state->add_name_input = CreateAddTextArea(
      body, state, "For example: Gateway Node #1", "",
      kAddNameInputY, 40);
  if (state->add_name_input == nullptr || !CreateAddParameterTitle(
      body, "RF CHIP", 138)) {
    return false;
  }

  const char* chip_names[] = {
      "SX1262", "LR2021", "CC1101", "nRF24", "Custom"};
  const int option_gap = 10;
  const int option_area_width = state->config.width - 56;
  const int chip_columns = 4;
  const int chip_width =
      (option_area_width - 3 * option_gap) / chip_columns;
  for (int index = 0; index < 5; ++index) {
    const int column = index % chip_columns;
    const int row = index / chip_columns;
    state->add_chip_buttons[index] = CreateAddOptionButton(body, state,
        RfAddOptionGroup::kChip, index, chip_names[index],
        28 + column * (chip_width + option_gap),
        174 + row * 74, chip_width, 64);
    if (state->add_chip_buttons[index] == nullptr) {
      return false;
    }
  }

  if (!CreateAddParameterTitle(body, "PROTOCOL", 330)) {
    return false;
  }
  const char* protocol_names[] = {"LoRa", "FSK"};
  const int protocol_x[] = {28, 148};
  const int protocol_width[] = {108, 82};
  for (int index = 0; index < 2; ++index) {
    state->add_protocol_buttons[index] = CreateAddOptionButton(
        body, state, RfAddOptionGroup::kProtocol, index,
        protocol_names[index], protocol_x[index], 366,
        protocol_width[index], 62);
    if (state->add_protocol_buttons[index] == nullptr) {
      return false;
    }
  }

  if (!CreateAddParameterTitle(body, "WORKING FREQUENCY", 450)) {
    return false;
  }
  state->add_frequency_input = CreateAddTextArea(
      body, state, "Frequency", "915", kAddFrequencyInputY, 4);
  if (state->add_frequency_input == nullptr) {
    return false;
  }
  lv_textarea_set_accepted_chars(
      state->add_frequency_input, kFrequencyAcceptedChars);
  lv_obj_set_width(
      state->add_frequency_input, state->config.width - 152);

  lv_obj_t* unit = lv_obj_create(body);
  if (unit == nullptr) {
    return false;
  }
  lv_obj_remove_flag(unit, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(unit, 84, 62);
  lv_obj_set_pos(unit, state->config.width - 112,
      kAddFrequencyInputY + 4);
  lv_obj_set_style_bg_color(
      unit, lv_color_hex(kSurfaceContainerHighColor), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(unit, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_border_width(unit, 0, LV_PART_MAIN);
  lv_obj_set_style_radius(unit, 22, LV_PART_MAIN);
  lv_obj_set_style_pad_all(unit, 0, LV_PART_MAIN);
  lv_obj_t* unit_label = CreateLabel(
      unit, "MHz", kSecondaryTextColor, Font22());
  if (unit_label == nullptr) {
    return false;
  }
  lv_obj_center(unit_label);

  if (!CreateAddParameterTitle(body, "SPREADING FACTOR", 582)) {
    return false;
  }
  const int sf_width = (option_area_width - 6 * option_gap) / 7;
  const char* sf_names[] = {"6", "7", "8", "9", "10", "11", "12"};
  for (int index = 0; index < 7; ++index) {
    state->add_sf_buttons[index] = CreateAddOptionButton(body, state,
        RfAddOptionGroup::kSpreadingFactor, index, sf_names[index],
        28 + index * (sf_width + option_gap), 618, sf_width, 60);
    if (state->add_sf_buttons[index] == nullptr) {
      return false;
    }
  }
  UpdateAddOptionSelection(state);
  return true;
}

/**
 * @brief 创建添加模块页面的标题栏
 * @param page 页面对象
 * @param state 射频页面状态
 * @return 创建成功返回 true，否则返回 false
 */
bool CreateAddModuleHeader(lv_obj_t* page, RfViewState* state) {
  lv_obj_t* back = lv_button_create(page);
  if (back == nullptr) {
    return false;
  }
  lv_obj_remove_style_all(back);
  lv_obj_add_flag(back, LV_OBJ_FLAG_GESTURE_BUBBLE);
  lv_obj_set_size(back, 62, 62);
  lv_obj_set_pos(back, 18, 66);
  lv_obj_add_event_cb(back, AddPageBackClickedEventCallback,
      LV_EVENT_CLICKED, state);
  lv_obj_t* icon_label = CreateLabel(
      back, icon::kArrowBack, kMainTextColor, OutlineIconFont44());
  if (icon_label == nullptr) {
    return false;
  }
  lv_obj_align(icon_label, LV_ALIGN_CENTER, -4, 0);
  lv_obj_t* title = CreateLabel(
      page, "Add RF module", kMainTextColor, Font48());
  if (title == nullptr) {
    return false;
  }
  lv_obj_set_pos(title, 34, 154);
  return true;
}

/**
 * @brief 创建添加模块页面底部提交区域
 * @param page 页面对象
 * @param state 射频页面状态
 * @return 创建成功返回 true，否则返回 false
 */
bool CreateAddModuleActionArea(lv_obj_t* page, RfViewState* state) {
  lv_obj_t* area = lv_obj_create(page);
  if (area == nullptr) {
    return false;
  }
  lv_obj_remove_flag(area, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(area, state->config.width, kAddPageActionHeight);
  lv_obj_align(area, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_obj_set_style_bg_opa(area, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_set_style_border_width(area, 0, LV_PART_MAIN);
  lv_obj_set_style_radius(area, 0, LV_PART_MAIN);
  lv_obj_set_style_shadow_width(area, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(area, 0, LV_PART_MAIN);

  lv_obj_t* button = lv_button_create(area);
  if (button == nullptr) {
    return false;
  }
  state->add_submit_button = button;
  lv_obj_set_size(button, state->config.width - 96, 84);
  lv_obj_align(button, LV_ALIGN_CENTER, 0, 0);
  lv_obj_set_style_radius(button, 42, LV_PART_MAIN);
  lv_obj_set_style_bg_color(button,
      lv_color_hex(kDisabledContainerColor), LV_PART_MAIN);
  lv_obj_set_style_bg_color(
      button, lv_color_hex(kPrimaryPressedColor), LV_STATE_PRESSED);
  lv_obj_set_style_bg_color(button,
      lv_color_hex(kDisabledContainerColor), LV_STATE_DISABLED);
  lv_obj_set_style_border_width(button, 0, LV_PART_MAIN);
  lv_obj_set_style_shadow_width(button, 0, LV_PART_MAIN);
  lv_obj_add_event_cb(button, AddModuleSubmitClickedEventCallback,
      LV_EVENT_CLICKED, state);
  state->add_submit_label = CreateLabel(
      button, "Add module", kDisabledTextColor, Font28());
  if (state->add_submit_label == nullptr) {
    return false;
  }
  lv_obj_center(state->add_submit_label);
  UpdateAddSubmitButton(state);
  return true;
}

/**
 * @brief 显示全屏添加射频模块页面
 * @param state 射频页面状态
 * @return 显示成功返回 true，否则返回 false
 */
bool ShowAddModulePage(RfViewState* state) {
  if (state == nullptr || state->root == nullptr) {
    return false;
  }
  if (state->add_page != nullptr) {
    lv_obj_move_to_index(state->add_page, -1);
    return true;
  }
  state->selected_add_chip = 0;
  state->selected_add_protocol = 0;
  state->selected_add_sf = 1;
  state->add_closing = false;
  state->add_edge_swipe = EdgeBackSwipeState();
  for (lv_obj_t*& button : state->add_chip_buttons) {
    button = nullptr;
  }
  for (lv_obj_t*& button : state->add_protocol_buttons) {
    button = nullptr;
  }
  for (lv_obj_t*& button : state->add_sf_buttons) {
    button = nullptr;
  }

  lv_obj_t* page = lv_obj_create(state->root);
  if (page == nullptr) {
    return false;
  }
  state->add_page = page;
  lv_obj_remove_flag(page, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(page, LV_OBJ_FLAG_GESTURE_BUBBLE);
  lv_obj_set_size(page, state->config.width, state->config.height);
  lv_obj_set_pos(page, 0, 0);
  lv_obj_set_style_bg_color(
      page, lv_color_hex(kMainBackgroundColor), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(page, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_border_width(page, 0, LV_PART_MAIN);
  lv_obj_set_style_radius(page, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(page, 0, LV_PART_MAIN);
  lv_obj_add_event_cb(page, AddPageBackgroundClickedEventCallback,
      LV_EVENT_CLICKED, state);
  AddEdgeBackSwipeEvents(page, AddPageEdgeBackEventCallback, state);

  if (!CreateAddModuleHeader(page, state)) {
    lv_obj_delete(page);
    state->add_page = nullptr;
    return false;
  }
  state->add_body = lv_obj_create(page);
  if (state->add_body == nullptr) {
    lv_obj_delete(page);
    state->add_page = nullptr;
    return false;
  }
  lv_obj_set_pos(state->add_body, 0, kAddPageHeaderHeight);
  lv_obj_set_size(state->add_body, state->config.width,
      state->config.height - kAddPageHeaderHeight -
          kAddPageActionHeight);
  lv_obj_set_style_bg_opa(state->add_body, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_set_style_border_width(state->add_body, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(state->add_body, 0, LV_PART_MAIN);
  lv_obj_set_scroll_dir(state->add_body, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(state->add_body, LV_SCROLLBAR_MODE_AUTO);
  lv_obj_add_flag(state->add_body, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(state->add_body, LV_OBJ_FLAG_GESTURE_BUBBLE);
  lv_obj_add_event_cb(state->add_body,
      AddPageBackgroundClickedEventCallback, LV_EVENT_CLICKED, state);
  AddEdgeBackSwipeEvents(
      state->add_body, AddPageEdgeBackEventCallback, state);

  if (!CreateAddModuleContent(state) ||
      !CreateAddModuleActionArea(page, state)) {
    lv_obj_delete(page);
    state->add_page = nullptr;
    state->add_body = nullptr;
    return false;
  }

  SharedKeyboardConfig keyboard_config;
  keyboard_config.width = state->config.width;
  keyboard_config.height =
      state->config.height * kAddKeyboardHeightPercent / 100;
  state->add_keyboard = CreateSharedKeyboard(page, keyboard_config);
  if (state->add_keyboard == nullptr ||
      !AttachSharedKeyboardToTextArea(
          state->add_keyboard, state->add_name_input, nullptr) ||
      !AttachSharedKeyboardToTextArea(state->add_keyboard,
          state->add_frequency_input, kFrequencyAcceptedChars)) {
    lv_obj_delete(page);
    state->add_page = nullptr;
    state->add_body = nullptr;
    state->add_keyboard = nullptr;
    return false;
  }
  lv_obj_add_flag(state->add_keyboard, LV_OBJ_FLAG_GESTURE_BUBBLE);
  AddEdgeBackSwipeEvents(
      state->add_keyboard, AddPageEdgeBackEventCallback, state);
  EnableEdgeBackSwipeEventBubble(page);
  if (!StartSlideLeftWindowTransition(page, state->config.width,
      kAnimationMs, state, nullptr)) {
    lv_obj_delete(page);
    state->add_page = nullptr;
    state->add_body = nullptr;
    state->add_keyboard = nullptr;
    return false;
  }
  return true;
}

/**
 * @brief 处理圆形添加按钮点击事件
 * @param event LVGL 事件对象
 */
void AddButtonClickedEventCallback(lv_event_t* event) {
  if (lv_event_get_code(event) == LV_EVENT_CLICKED) {
    ShowAddModulePage(
        static_cast<RfViewState*>(lv_event_get_user_data(event)));
  }
}

/**
 * @brief 在按钮中创建放大的加号图标
 * @param parent 按钮父对象
 * @return 创建成功返回 true，否则返回 false
 */
bool CreateLargeAddIcon(lv_obj_t* parent) {
  if (parent == nullptr) {
    return false;
  }
  lv_obj_t* horizontal = lv_obj_create(parent);
  lv_obj_t* vertical = lv_obj_create(parent);
  if (horizontal == nullptr || vertical == nullptr) {
    if (horizontal != nullptr) {
      lv_obj_delete(horizontal);
    }
    if (vertical != nullptr) {
      lv_obj_delete(vertical);
    }
    return false;
  }
  lv_obj_remove_flag(horizontal, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(vertical, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(horizontal, 38, 4);
  lv_obj_set_size(vertical, 4, 38);
  lv_obj_center(horizontal);
  lv_obj_center(vertical);
  lv_obj_set_style_bg_color(
      horizontal, lv_color_hex(kOnPrimaryColor), LV_PART_MAIN);
  lv_obj_set_style_bg_color(
      vertical, lv_color_hex(kOnPrimaryColor), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(horizontal, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_bg_opa(vertical, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_border_width(horizontal, 0, LV_PART_MAIN);
  lv_obj_set_style_border_width(vertical, 0, LV_PART_MAIN);
  lv_obj_set_style_radius(horizontal, 2, LV_PART_MAIN);
  lv_obj_set_style_radius(vertical, 2, LV_PART_MAIN);
  lv_obj_set_style_pad_all(horizontal, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(vertical, 0, LV_PART_MAIN);
  return true;
}

/**
 * @brief 创建右下角圆形添加按钮
 * @param parent 页面根对象
 * @param state 射频页面状态
 * @return 创建成功返回 true，否则返回 false
 */
bool CreateAddButton(lv_obj_t* parent, RfViewState* state) {
  lv_obj_t* button = lv_button_create(parent);
  if (button == nullptr) {
    return false;
  }
  lv_obj_set_size(button, 96, 96);
  lv_obj_align(button, LV_ALIGN_BOTTOM_RIGHT, -40, -42);
  lv_obj_set_style_radius(button, 48, LV_PART_MAIN);
  lv_obj_set_style_bg_color(
      button, lv_color_hex(kPrimaryColor), LV_PART_MAIN);
  lv_obj_set_style_bg_color(
      button, lv_color_hex(kPrimaryPressedColor), LV_STATE_PRESSED);
  lv_obj_set_style_bg_opa(button, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_border_width(button, 0, LV_PART_MAIN);
  lv_obj_set_style_shadow_width(button, 14, LV_PART_MAIN);
  lv_obj_set_style_shadow_color(
      button, lv_color_hex(0x8A8095), LV_PART_MAIN);
  lv_obj_set_style_shadow_opa(button, LV_OPA_40, LV_PART_MAIN);
  lv_obj_add_event_cb(
      button, AddButtonClickedEventCallback, LV_EVENT_CLICKED, state);
  if (!CreateLargeAddIcon(button)) {
    lv_obj_delete(button);
    return false;
  }
  return true;
}

}  // namespace

/**
 * @brief 创建射频控制应用主界面
 * @param parent 父对象
 * @param app_entry 应用条目
 * @param config 应用视图配置
 * @return 创建成功返回页面根对象，否则返回 nullptr
 */
lv_obj_t* CreateRfView(lv_obj_t* parent, const app::AppEntry& app_entry,
    const AppViewConfig& config) {
  static_cast<void>(app_entry);
  if (parent == nullptr || config.width <= 0 || config.height <= 0) {
    return nullptr;
  }
  auto* state = new RfViewState{};
  state->config = config;
  state->module_count = kInitialModuleCount;
  for (size_t index = 0; index < kInitialModuleCount; ++index) {
    state->modules[index] = kModuleItems[index];
  }
  lv_obj_t* root = lv_obj_create(parent);
  if (root == nullptr) {
    delete state;
    return nullptr;
  }
  state->root = root;
  lv_obj_remove_flag(root, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(root, config.width, config.height);
  lv_obj_align(root, LV_ALIGN_CENTER, 0, 0);
  lv_obj_set_style_bg_color(
      root, lv_color_hex(kMainBackgroundColor), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(root, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_border_width(root, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(root, 0, LV_PART_MAIN);
  lv_obj_add_event_cb(
      root, RfViewDeleteEventCallback, LV_EVENT_DELETE, state);
  if (config.set_status_bar_visible) {
    config.set_status_bar_visible(true);
  }
  if (config.set_status_bar_text_color) {
    config.set_status_bar_text_color(kMainTextColor);
  }
  lv_obj_t* list = lv_obj_create(root);
  if (list == nullptr) {
    lv_obj_delete(root);
    return nullptr;
  }
  lv_obj_set_pos(list, 0, kListTop);
  lv_obj_set_size(list, config.width, config.height - kListTop);
  lv_obj_set_style_bg_color(
      list, lv_color_hex(kMainBackgroundColor), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(list, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_border_width(list, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(list, 0, LV_PART_MAIN);
  lv_obj_set_scroll_dir(list, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_AUTO);
  state->module_list = list;
  if (!CreateHeader(root, state) || !CreateAddButton(root, state)) {
    lv_obj_delete(root);
    return nullptr;
  }
  if (!RenderModuleList(state)) {
    lv_obj_delete(root);
    return nullptr;
  }
  return root;
}

}  // namespace lilygo_box::ui
