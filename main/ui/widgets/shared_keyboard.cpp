/*
 * @Description: None
 * @Author: LILYGO_L
 * @Date: 2026-05-18 12:08:00
 * @LastEditTime: 2026-05-18 12:08:00
 * @License: GPL 3.0
 */
#include "ui/widgets/shared_keyboard.h"

#include <cstdint>
#include <cstring>
#include <new>

namespace lilygo_box::ui {
namespace {

constexpr int kKeyboardRadius = 8;
constexpr int kKeyboardPadRow = 8;
constexpr int kKeyboardPadColumn = 4;
constexpr uint32_t kKeyboardBackgroundColor = 0xE7E7E7;
constexpr uint32_t kKeyboardKeyColor = 0xFFFFFF;
constexpr uint32_t kKeyboardPressedKeyColor = 0xD4D4D4;
constexpr uint32_t kKeyboardTextColor = 0x202020;
constexpr uint32_t kKeyboardSpecialKeyColor = 0xC9CDD4;

const char* const kKeyboardLowerMap[] = {
    "q", "w", "e", "r", "t", "y", "u", "i", "o", "p", "\n",
    " ", "a", "s", "d", "f", "g", "h", "j", "k", "l", " ", "\n",
    LV_SYMBOL_EJECT, "z", "x", "c", "v", "b", "n", "m",
    LV_SYMBOL_BACKSPACE, "\n",
    LV_SYMBOL_KEYBOARD, LV_SYMBOL_LEFT, " ", LV_SYMBOL_RIGHT,
    LV_SYMBOL_NEW_LINE, nullptr};

const char* const kKeyboardUpperMap[] = {
    "Q", "W", "E", "R", "T", "Y", "U", "I", "O", "P", "\n",
    " ", "A", "S", "D", "F", "G", "H", "J", "K", "L", " ", "\n",
    LV_SYMBOL_EJECT, "Z", "X", "C", "V", "B", "N", "M",
    LV_SYMBOL_BACKSPACE, "\n",
    LV_SYMBOL_KEYBOARD, LV_SYMBOL_LEFT, " ", LV_SYMBOL_RIGHT,
    LV_SYMBOL_NEW_LINE, nullptr};

const char* const kKeyboardNumberMap[] = {
    "1", "2", "3", LV_SYMBOL_BACKSPACE, "\n",
    "4", "5", "6", LV_SYMBOL_LEFT, LV_SYMBOL_RIGHT, "\n",
    "7", "8", "9", LV_SYMBOL_NEW_LINE, "\n",
    "+/-", "0", ".", LV_SYMBOL_KEYBOARD, nullptr};

const char* const kKeyboardSymbolMap[] = {
    "!", "@", "#", "$", "%", "^", "&", "*", "(", ")", "\n",
    "_", "-", "+", "=", "[", "]", "{", "}", "|", ";", "\n",
    ":", "'", "\"", ",", "<", ">", ".", "?", "/", "\\", "`", "~", "\n",
    LV_SYMBOL_KEYBOARD, LV_SYMBOL_LEFT, LV_SYMBOL_RIGHT, LV_SYMBOL_BACKSPACE,
    nullptr};

const lv_buttonmatrix_ctrl_t kKeyboardLetterCtrl[] = {
    LV_BUTTONMATRIX_CTRL_WIDTH_1,
    LV_BUTTONMATRIX_CTRL_WIDTH_1,
    LV_BUTTONMATRIX_CTRL_WIDTH_1,
    LV_BUTTONMATRIX_CTRL_WIDTH_1,
    LV_BUTTONMATRIX_CTRL_WIDTH_1,
    LV_BUTTONMATRIX_CTRL_WIDTH_1,
    LV_BUTTONMATRIX_CTRL_WIDTH_1,
    LV_BUTTONMATRIX_CTRL_WIDTH_1,
    LV_BUTTONMATRIX_CTRL_WIDTH_1,
    LV_BUTTONMATRIX_CTRL_WIDTH_1,
    static_cast<lv_buttonmatrix_ctrl_t>(
        LV_BUTTONMATRIX_CTRL_HIDDEN | LV_BUTTONMATRIX_CTRL_WIDTH_1),
    LV_BUTTONMATRIX_CTRL_WIDTH_2,
    LV_BUTTONMATRIX_CTRL_WIDTH_2,
    LV_BUTTONMATRIX_CTRL_WIDTH_2,
    LV_BUTTONMATRIX_CTRL_WIDTH_2,
    LV_BUTTONMATRIX_CTRL_WIDTH_2,
    LV_BUTTONMATRIX_CTRL_WIDTH_2,
    LV_BUTTONMATRIX_CTRL_WIDTH_2,
    LV_BUTTONMATRIX_CTRL_WIDTH_2,
    LV_BUTTONMATRIX_CTRL_WIDTH_2,
    static_cast<lv_buttonmatrix_ctrl_t>(
        LV_BUTTONMATRIX_CTRL_HIDDEN | LV_BUTTONMATRIX_CTRL_WIDTH_1),
    static_cast<lv_buttonmatrix_ctrl_t>(
        LV_BUTTONMATRIX_CTRL_CHECKED | LV_BUTTONMATRIX_CTRL_WIDTH_2),
    LV_BUTTONMATRIX_CTRL_WIDTH_1,
    LV_BUTTONMATRIX_CTRL_WIDTH_1,
    LV_BUTTONMATRIX_CTRL_WIDTH_1,
    LV_BUTTONMATRIX_CTRL_WIDTH_1,
    LV_BUTTONMATRIX_CTRL_WIDTH_1,
    LV_BUTTONMATRIX_CTRL_WIDTH_1,
    LV_BUTTONMATRIX_CTRL_WIDTH_1,
    static_cast<lv_buttonmatrix_ctrl_t>(
        LV_BUTTONMATRIX_CTRL_CHECKED | LV_BUTTONMATRIX_CTRL_WIDTH_2),
    static_cast<lv_buttonmatrix_ctrl_t>(
        LV_BUTTONMATRIX_CTRL_CHECKED | LV_BUTTONMATRIX_CTRL_WIDTH_2),
    static_cast<lv_buttonmatrix_ctrl_t>(
        LV_BUTTONMATRIX_CTRL_CHECKED | LV_BUTTONMATRIX_CTRL_WIDTH_1),
    LV_BUTTONMATRIX_CTRL_WIDTH_3,
    static_cast<lv_buttonmatrix_ctrl_t>(
        LV_BUTTONMATRIX_CTRL_CHECKED | LV_BUTTONMATRIX_CTRL_WIDTH_1),
    static_cast<lv_buttonmatrix_ctrl_t>(
        LV_BUTTONMATRIX_CTRL_CHECKED | LV_BUTTONMATRIX_CTRL_WIDTH_2),
};

const lv_buttonmatrix_ctrl_t kKeyboardNumberCtrl[] = {
    LV_BUTTONMATRIX_CTRL_WIDTH_1,
    LV_BUTTONMATRIX_CTRL_WIDTH_1,
    LV_BUTTONMATRIX_CTRL_WIDTH_1,
    static_cast<lv_buttonmatrix_ctrl_t>(
        LV_BUTTONMATRIX_CTRL_CHECKED | LV_BUTTONMATRIX_CTRL_WIDTH_2),
    LV_BUTTONMATRIX_CTRL_WIDTH_1,
    LV_BUTTONMATRIX_CTRL_WIDTH_1,
    LV_BUTTONMATRIX_CTRL_WIDTH_1,
    static_cast<lv_buttonmatrix_ctrl_t>(
        LV_BUTTONMATRIX_CTRL_CHECKED | LV_BUTTONMATRIX_CTRL_WIDTH_1),
    static_cast<lv_buttonmatrix_ctrl_t>(
        LV_BUTTONMATRIX_CTRL_CHECKED | LV_BUTTONMATRIX_CTRL_WIDTH_1),
    LV_BUTTONMATRIX_CTRL_WIDTH_1,
    LV_BUTTONMATRIX_CTRL_WIDTH_1,
    LV_BUTTONMATRIX_CTRL_WIDTH_1,
    static_cast<lv_buttonmatrix_ctrl_t>(
        LV_BUTTONMATRIX_CTRL_CHECKED | LV_BUTTONMATRIX_CTRL_WIDTH_2),
    LV_BUTTONMATRIX_CTRL_WIDTH_1,
    LV_BUTTONMATRIX_CTRL_WIDTH_1,
    LV_BUTTONMATRIX_CTRL_WIDTH_1,
    static_cast<lv_buttonmatrix_ctrl_t>(
        LV_BUTTONMATRIX_CTRL_CHECKED | LV_BUTTONMATRIX_CTRL_WIDTH_2),
};

const lv_buttonmatrix_ctrl_t kKeyboardSymbolCtrl[] = {
    LV_BUTTONMATRIX_CTRL_WIDTH_1,
    LV_BUTTONMATRIX_CTRL_WIDTH_1,
    LV_BUTTONMATRIX_CTRL_WIDTH_1,
    LV_BUTTONMATRIX_CTRL_WIDTH_1,
    LV_BUTTONMATRIX_CTRL_WIDTH_1,
    LV_BUTTONMATRIX_CTRL_WIDTH_1,
    LV_BUTTONMATRIX_CTRL_WIDTH_1,
    LV_BUTTONMATRIX_CTRL_WIDTH_1,
    LV_BUTTONMATRIX_CTRL_WIDTH_1,
    LV_BUTTONMATRIX_CTRL_WIDTH_1,
    LV_BUTTONMATRIX_CTRL_WIDTH_1,
    LV_BUTTONMATRIX_CTRL_WIDTH_1,
    LV_BUTTONMATRIX_CTRL_WIDTH_1,
    LV_BUTTONMATRIX_CTRL_WIDTH_1,
    LV_BUTTONMATRIX_CTRL_WIDTH_1,
    LV_BUTTONMATRIX_CTRL_WIDTH_1,
    LV_BUTTONMATRIX_CTRL_WIDTH_1,
    LV_BUTTONMATRIX_CTRL_WIDTH_1,
    LV_BUTTONMATRIX_CTRL_WIDTH_1,
    LV_BUTTONMATRIX_CTRL_WIDTH_1,
    LV_BUTTONMATRIX_CTRL_WIDTH_1,
    LV_BUTTONMATRIX_CTRL_WIDTH_1,
    LV_BUTTONMATRIX_CTRL_WIDTH_1,
    LV_BUTTONMATRIX_CTRL_WIDTH_1,
    LV_BUTTONMATRIX_CTRL_WIDTH_1,
    LV_BUTTONMATRIX_CTRL_WIDTH_1,
    LV_BUTTONMATRIX_CTRL_WIDTH_1,
    LV_BUTTONMATRIX_CTRL_WIDTH_1,
    LV_BUTTONMATRIX_CTRL_WIDTH_1,
    LV_BUTTONMATRIX_CTRL_WIDTH_1,
    LV_BUTTONMATRIX_CTRL_WIDTH_1,
    LV_BUTTONMATRIX_CTRL_WIDTH_1,
    static_cast<lv_buttonmatrix_ctrl_t>(
        LV_BUTTONMATRIX_CTRL_CHECKED | LV_BUTTONMATRIX_CTRL_WIDTH_2),
    static_cast<lv_buttonmatrix_ctrl_t>(
        LV_BUTTONMATRIX_CTRL_CHECKED | LV_BUTTONMATRIX_CTRL_WIDTH_1),
    static_cast<lv_buttonmatrix_ctrl_t>(
        LV_BUTTONMATRIX_CTRL_CHECKED | LV_BUTTONMATRIX_CTRL_WIDTH_1),
    static_cast<lv_buttonmatrix_ctrl_t>(
        LV_BUTTONMATRIX_CTRL_CHECKED | LV_BUTTONMATRIX_CTRL_WIDTH_2),
};

// 文本框和共享键盘的绑定状态
struct TextAreaKeyboardBinding {
  lv_obj_t* keyboard = nullptr;
  const char* accepted_chars = nullptr;
};

/**
 * @brief 获取共享键盘字体
 * @return 字体指针
 */
const lv_font_t* KeyboardFont() { return &lv_font_montserrat_26; }

/**
 * @brief 合并 LVGL 样式部件和状态选择器
 * @param part 样式部件
 * @param state 样式状态
 * @return LVGL 样式选择器
 */
lv_style_selector_t StyleSelector(lv_part_t part, lv_state_t state) {
  return static_cast<lv_style_selector_t>(
      static_cast<lv_style_selector_t>(part) |
      static_cast<lv_style_selector_t>(state));
}

/**
 * @brief 设置共享键盘的按键布局
 * @param keyboard 键盘对象
 */
void ConfigureSharedKeyboardMap(lv_obj_t* keyboard) {
  lv_keyboard_set_map(keyboard, LV_KEYBOARD_MODE_USER_1, kKeyboardLowerMap,
      kKeyboardLetterCtrl);
  lv_keyboard_set_map(keyboard, LV_KEYBOARD_MODE_USER_2, kKeyboardUpperMap,
      kKeyboardLetterCtrl);
  lv_keyboard_set_map(keyboard, LV_KEYBOARD_MODE_USER_3, kKeyboardNumberMap,
      kKeyboardNumberCtrl);
  lv_keyboard_set_map(keyboard, LV_KEYBOARD_MODE_USER_4, kKeyboardSymbolMap,
      kKeyboardSymbolCtrl);
}

/**
 * @brief 设置共享键盘的视觉样式
 * @param keyboard 键盘对象
 */
void ConfigureSharedKeyboardStyle(lv_obj_t* keyboard) {
  lv_obj_set_style_bg_color(
      keyboard, lv_color_hex(kKeyboardBackgroundColor), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(keyboard, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_border_width(keyboard, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_row(keyboard, kKeyboardPadRow, LV_PART_MAIN);
  lv_obj_set_style_pad_column(keyboard, kKeyboardPadColumn, LV_PART_MAIN);
  lv_obj_set_style_text_font(keyboard, KeyboardFont(), LV_PART_MAIN);
  lv_obj_set_style_text_color(
      keyboard, lv_color_hex(kKeyboardTextColor), LV_PART_MAIN);

  lv_obj_set_style_radius(keyboard, kKeyboardRadius, LV_PART_ITEMS);
  lv_obj_set_style_bg_color(
      keyboard, lv_color_hex(kKeyboardKeyColor), LV_PART_ITEMS);
  lv_obj_set_style_bg_opa(keyboard, LV_OPA_COVER, LV_PART_ITEMS);
  lv_obj_set_style_border_width(keyboard, 0, LV_PART_ITEMS);
  lv_obj_set_style_text_color(
      keyboard, lv_color_hex(kKeyboardTextColor), LV_PART_ITEMS);

  lv_obj_set_style_bg_color(
      keyboard, lv_color_hex(kKeyboardPressedKeyColor),
      StyleSelector(LV_PART_ITEMS, LV_STATE_PRESSED));
  lv_obj_set_style_bg_color(
      keyboard, lv_color_hex(kKeyboardSpecialKeyColor),
      StyleSelector(LV_PART_ITEMS, LV_STATE_CHECKED));
}

/**
 * @brief 处理共享键盘模式切换事件
 * @param event LVGL 事件
 */
void SharedKeyboardModeEventCallback(lv_event_t* event) {
  if (lv_event_get_code(event) != LV_EVENT_VALUE_CHANGED) {
    return;
  }

  lv_obj_t* keyboard = static_cast<lv_obj_t*>(lv_event_get_target_obj(event));
  if (keyboard == nullptr) {
    return;
  }

  const uint32_t button_id = lv_keyboard_get_selected_button(keyboard);
  const char* text = lv_keyboard_get_button_text(keyboard, button_id);
  if (text == nullptr) {
    return;
  }

  if (std::strcmp(text, LV_SYMBOL_KEYBOARD) == 0) {
    const lv_keyboard_mode_t mode = lv_keyboard_get_mode(keyboard);
    lv_keyboard_mode_t next_mode = LV_KEYBOARD_MODE_USER_1;
    if (mode == LV_KEYBOARD_MODE_USER_1 || mode == LV_KEYBOARD_MODE_USER_2) {
      next_mode = LV_KEYBOARD_MODE_USER_3;
    } else if (mode == LV_KEYBOARD_MODE_USER_3) {
      next_mode = LV_KEYBOARD_MODE_USER_4;
    }
    lv_keyboard_set_mode(keyboard, next_mode);
    lv_event_stop_processing(event);
    return;
  }

  if (std::strcmp(text, LV_SYMBOL_EJECT) != 0) {
    return;
  }

  const lv_keyboard_mode_t mode = lv_keyboard_get_mode(keyboard);
  if (mode == LV_KEYBOARD_MODE_USER_1) {
    lv_keyboard_set_mode(keyboard, LV_KEYBOARD_MODE_USER_2);
    lv_event_stop_processing(event);
  } else if (mode == LV_KEYBOARD_MODE_USER_2) {
    lv_keyboard_set_mode(keyboard, LV_KEYBOARD_MODE_USER_1);
    lv_event_stop_processing(event);
  }
}

/**
 * @brief 处理文本框聚焦和释放事件
 * @param event LVGL 事件
 */
void TextAreaKeyboardEventCallback(lv_event_t* event) {
  auto* binding =
      static_cast<TextAreaKeyboardBinding*>(lv_event_get_user_data(event));
  if (binding == nullptr) {
    return;
  }

  const lv_event_code_t code = lv_event_get_code(event);
  if (code == LV_EVENT_DELETE) {
    delete binding;
    return;
  }

  lv_obj_t* text_area = static_cast<lv_obj_t*>(lv_event_get_target_obj(event));
  if (text_area == nullptr || binding->keyboard == nullptr) {
    return;
  }

  if (code == LV_EVENT_FOCUSED || code == LV_EVENT_CLICKED) {
    lv_keyboard_set_textarea(binding->keyboard, text_area);
    if (binding->accepted_chars != nullptr) {
      lv_textarea_set_accepted_chars(text_area, binding->accepted_chars);
    }
    lv_keyboard_set_mode(binding->keyboard, LV_KEYBOARD_MODE_USER_1);
    lv_obj_remove_flag(binding->keyboard, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_to_index(binding->keyboard, -1);
    return;
  }

  if (code == LV_EVENT_READY || code == LV_EVENT_CANCEL) {
    HideSharedKeyboard(binding->keyboard);
  }
}

}  // namespace

/**
 * @brief 创建共享屏幕键盘
 * @param parent 父对象
 * @param config 键盘配置
 * @return 创建成功返回键盘对象，否则返回 nullptr
 */
lv_obj_t* CreateSharedKeyboard(
    lv_obj_t* parent, const SharedKeyboardConfig& config) {
  if (parent == nullptr || config.width <= 0 || config.height <= 0) {
    return nullptr;
  }

  lv_obj_t* keyboard = lv_keyboard_create(parent);
  if (keyboard == nullptr) {
    return nullptr;
  }

  lv_obj_set_size(keyboard, config.width, config.height);
  ConfigureSharedKeyboardStyle(keyboard);
  ConfigureSharedKeyboardMap(keyboard);
  lv_obj_align(keyboard, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_obj_add_flag(keyboard, LV_OBJ_FLAG_HIDDEN);
  lv_keyboard_set_mode(keyboard, LV_KEYBOARD_MODE_USER_1);
  const lv_event_code_t mode_event =
      static_cast<lv_event_code_t>(LV_EVENT_VALUE_CHANGED |
                                   LV_EVENT_PREPROCESS);
  lv_obj_add_event_cb(
      keyboard, SharedKeyboardModeEventCallback, mode_event, nullptr);
  return keyboard;
}

/**
 * @brief 绑定共享键盘和文本输入框
 * @param keyboard 共享键盘对象
 * @param text_area 文本输入框对象
 * @param accepted_chars 允许输入的字符集合
 * @return 绑定成功返回 true，否则返回 false
 */
bool AttachSharedKeyboardToTextArea(
    lv_obj_t* keyboard, lv_obj_t* text_area, const char* accepted_chars) {
  if (keyboard == nullptr || text_area == nullptr) {
    return false;
  }

  auto* binding = new (std::nothrow) TextAreaKeyboardBinding();
  if (binding == nullptr) {
    return false;
  }

  binding->keyboard = keyboard;
  binding->accepted_chars = accepted_chars;
  lv_obj_add_event_cb(
      text_area, TextAreaKeyboardEventCallback, LV_EVENT_ALL, binding);
  return true;
}

/**
 * @brief 隐藏共享键盘
 * @param keyboard 共享键盘对象
 */
void HideSharedKeyboard(lv_obj_t* keyboard) {
  if (keyboard == nullptr) {
    return;
  }

  lv_keyboard_set_textarea(keyboard, nullptr);
  lv_obj_add_flag(keyboard, LV_OBJ_FLAG_HIDDEN);
}

}  // namespace lilygo_box::ui
