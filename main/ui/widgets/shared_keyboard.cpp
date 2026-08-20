/*
 * @Description: 可复用 LVGL 屏幕键盘布局与输入绑定实现
 * @Author: LILYGO_L
 * @Date: 2026-05-18 12:08:00
 * @LastEditTime: 2026-07-16 20:53:26
 * @License: GPL 3.0
 */
#include "ui/widgets/shared_keyboard.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <limits>
#include <new>

#include "app/storage/input_method_storage.h"
#include "hal/providers/keyboard_expansion_provider.h"
#include "ui/haptic_feedback.h"

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
constexpr int kTextAreaActivationDragThreshold = 12;
constexpr size_t kMaximumSharedKeyboardCount = 16;

hal::KeyboardExpansionProvider* g_physical_keyboard_provider = nullptr;
std::array<lv_obj_t*, kMaximumSharedKeyboardCount> g_shared_keyboards = {};

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
  lv_point_t start_point = {};
  uint32_t cursor_on_press = 0;
  bool has_start_point = false;
  bool cancelled = false;
  bool was_focused_on_press = false;
  bool allow_focus_event = false;
};

bool IsPhysicalKeyboardConnected() {
  if (g_physical_keyboard_provider == nullptr) {
    return false;
  }
  hal::KeyboardExpansionStatus status;
  return g_physical_keyboard_provider->ReadKeyboardExpansionStatus(&status) &&
      status.state == hal::KeyboardExpansionState::kReady &&
      status.tca8418 == hal::KeyboardExpansionComponentState::kReady;
}

bool ShouldShowSharedKeyboardInternal() {
  return !IsPhysicalKeyboardConnected() ||
      app::GetInputMethodPreferences().use_on_screen_keyboard;
}

void SharedKeyboardDeleteEventCallback(lv_event_t* event) {
  if (lv_event_get_code(event) != LV_EVENT_DELETE) {
    return;
  }
  lv_obj_t* keyboard = lv_event_get_target_obj(event);
  for (lv_obj_t*& registered_keyboard : g_shared_keyboards) {
    if (registered_keyboard == keyboard) {
      registered_keyboard = nullptr;
      return;
    }
  }
}

void RegisterSharedKeyboard(lv_obj_t* keyboard) {
  if (keyboard == nullptr) {
    return;
  }
  for (lv_obj_t*& registered_keyboard : g_shared_keyboards) {
    if (registered_keyboard == nullptr) {
      registered_keyboard = keyboard;
      lv_obj_add_event_cb(keyboard, SharedKeyboardDeleteEventCallback,
          LV_EVENT_DELETE, nullptr);
      return;
    }
  }
}

/**
 * @brief 计算整数绝对值
 * @param value 原始值
 * @return 绝对值
 */
int AbsInt(int value) { return value < 0 ? -value : value; }

/**
 * @brief 判断当前指针是否仍位于文本输入框内
 * @param text_area 文本输入框
 * @return 指针位于输入框内返回 true
 */
bool IsPointerInsideTextArea(lv_obj_t* text_area) {
  lv_indev_t* indev = lv_indev_active();
  if (text_area == nullptr || indev == nullptr) {
    return false;
  }

  lv_point_t point = {};
  lv_area_t area = {};
  lv_indev_get_point(indev, &point);
  lv_obj_get_coords(text_area, &area);
  return point.x >= area.x1 && point.x <= area.x2 &&
         point.y >= area.y1 && point.y <= area.y2;
}

/**
 * @brief 判断当前指针是否已超过输入激活允许的移动距离
 * @param binding 文本框键盘绑定状态
 * @return 已超过阈值返回 true
 */
bool HasTextAreaPointerMoved(const TextAreaKeyboardBinding& binding) {
  if (!binding.has_start_point) {
    return false;
  }

  lv_indev_t* indev = lv_indev_active();
  if (indev == nullptr) {
    return false;
  }

  lv_point_t point = {};
  lv_indev_get_point(indev, &point);
  return AbsInt(point.x - binding.start_point.x) >=
             kTextAreaActivationDragThreshold ||
         AbsInt(point.y - binding.start_point.y) >=
             kTextAreaActivationDragThreshold;
}

/**
 * @brief 恢复被取消触摸开始前的文本光标位置
 * @param text_area 文本输入框
 * @param binding 文本框键盘绑定状态
 */
void RestoreTextAreaCursor(lv_obj_t* text_area,
    const TextAreaKeyboardBinding& binding) {
  if (text_area == nullptr || !binding.cancelled) {
    return;
  }
  lv_textarea_set_cursor_pos(
      text_area, static_cast<int32_t>(binding.cursor_on_press));
}

/**
 * @brief 在确认释放后聚焦文本框并显示共享键盘
 * @param text_area 文本输入框
 * @param binding 文本框键盘绑定状态
 */
void ActivateTextAreaAfterRelease(
    lv_obj_t* text_area, TextAreaKeyboardBinding* binding) {
  if (text_area == nullptr || binding == nullptr ||
      binding->keyboard == nullptr) {
    return;
  }

  lv_obj_t* previous = lv_keyboard_get_textarea(binding->keyboard);
  lv_indev_t* indev = lv_indev_active();
  if (previous != nullptr && previous != text_area) {
    lv_obj_send_event(previous, LV_EVENT_DEFOCUSED, indev);
  }
  lv_keyboard_set_textarea(binding->keyboard, text_area);
  if (binding->accepted_chars != nullptr) {
    lv_textarea_set_accepted_chars(text_area, binding->accepted_chars);
  }
  lv_keyboard_set_mode(binding->keyboard, LV_KEYBOARD_MODE_USER_1);
  if (ShouldShowSharedKeyboardInternal()) {
    lv_obj_remove_flag(binding->keyboard, LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_obj_add_flag(binding->keyboard, LV_OBJ_FLAG_HIDDEN);
  }
  lv_obj_move_to_index(binding->keyboard, -1);
  binding->allow_focus_event = true;
  lv_obj_send_event(text_area, LV_EVENT_FOCUSED, indev);
  binding->allow_focus_event = false;
}

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
  if (button_id != std::numeric_limits<uint32_t>::max()) {
    PlayUiHapticFeedback();
  }
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
 * @brief 在控件默认处理前仲裁文本框的释放激活事件
 * @param event LVGL 事件
 */
void TextAreaKeyboardPreprocessEventCallback(lv_event_t* event) {
  auto* binding =
      static_cast<TextAreaKeyboardBinding*>(lv_event_get_user_data(event));
  if (binding == nullptr) {
    return;
  }

  const lv_event_code_t code = lv_event_get_code(event);
  if (code == LV_EVENT_DELETE) {
    return;
  }

  lv_obj_t* text_area = lv_event_get_target_obj(event);
  if (text_area == nullptr || binding->keyboard == nullptr) {
    return;
  }

  if (code == LV_EVENT_KEY) {
    const uint32_t key = lv_event_get_key(event);
    if (key == LV_KEY_UP || key == LV_KEY_DOWN || key == LV_KEY_LEFT ||
        key == LV_KEY_RIGHT) {
      // 方向键仅由当前输入框处理，避免继续冒泡导致父容器滚动。
      lv_event_stop_bubbling(event);
    }
    return;
  }

  if (code == LV_EVENT_FOCUSED && !binding->allow_focus_event) {
    lv_event_stop_processing(event);
    return;
  }

  if (code == LV_EVENT_PRESSED) {
    binding->has_start_point = false;
    binding->cancelled = false;
    binding->was_focused_on_press =
        lv_obj_has_state(text_area, LV_STATE_FOCUSED);
    binding->cursor_on_press = lv_textarea_get_cursor_pos(text_area);
    lv_indev_t* indev = lv_indev_active();
    if (indev != nullptr) {
      lv_indev_get_point(indev, &binding->start_point);
      binding->has_start_point = true;
    }
    return;
  }

  if (code == LV_EVENT_PRESSING || code == LV_EVENT_RELEASED) {
    if (HasTextAreaPointerMoved(*binding) ||
        !IsPointerInsideTextArea(text_area)) {
      binding->cancelled = true;
      lv_obj_remove_state(text_area, LV_STATE_PRESSED);
    }
    return;
  }

  if (code == LV_EVENT_PRESS_LOST || code == LV_EVENT_INDEV_RESET) {
    binding->cancelled = true;
    lv_obj_remove_state(text_area, LV_STATE_PRESSED);
    return;
  }

  if (code == LV_EVENT_CLICKED) {
    const bool cancelled = binding->cancelled ||
        HasTextAreaPointerMoved(*binding) ||
        !IsPointerInsideTextArea(text_area);
    binding->has_start_point = false;
    binding->cancelled = cancelled;
    if (cancelled) {
      RestoreTextAreaCursor(text_area, *binding);
      if (!binding->was_focused_on_press) {
        lv_obj_remove_state(text_area, LV_STATE_FOCUSED);
      }
      binding->cancelled = false;
      lv_event_stop_bubbling(event);
      lv_event_stop_processing(event);
      return;
    }

    binding->cancelled = false;
    ActivateTextAreaAfterRelease(text_area, binding);
    return;
  }

  if (code == LV_EVENT_READY || code == LV_EVENT_CANCEL) {
    HideSharedKeyboard(binding->keyboard);
  }
}

/**
 * @brief 在控件默认处理后恢复被滑动取消的文本光标
 * @param event LVGL 事件
 */
void TextAreaKeyboardPostprocessEventCallback(lv_event_t* event) {
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
  if (code != LV_EVENT_PRESSING && code != LV_EVENT_RELEASED &&
      code != LV_EVENT_PRESS_LOST && code != LV_EVENT_INDEV_RESET) {
    return;
  }

  lv_obj_t* text_area = lv_event_get_target_obj(event);
  RestoreTextAreaCursor(text_area, *binding);
  if (binding->cancelled && !binding->was_focused_on_press &&
      text_area != nullptr) {
    lv_obj_remove_state(text_area, LV_STATE_FOCUSED);
  }
}

}  // namespace

void RegisterSharedKeyboardPhysicalKeyboardProvider(
    hal::KeyboardExpansionProvider* provider) {
  g_physical_keyboard_provider = provider;
}

bool ShouldShowSharedKeyboard() {
  return ShouldShowSharedKeyboardInternal();
}

void RefreshSharedKeyboardVisibility() {
  const bool visible = ShouldShowSharedKeyboardInternal();
  for (lv_obj_t* keyboard : g_shared_keyboards) {
    if (keyboard == nullptr || lv_keyboard_get_textarea(keyboard) == nullptr) {
      continue;
    }
    if (visible) {
      lv_obj_remove_flag(keyboard, LV_OBJ_FLAG_HIDDEN);
    } else {
      lv_obj_add_flag(keyboard, LV_OBJ_FLAG_HIDDEN);
    }
  }
}

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
  RegisterSharedKeyboard(keyboard);
  return keyboard;
}

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
  const lv_event_code_t preprocess_event =
      static_cast<lv_event_code_t>(LV_EVENT_ALL | LV_EVENT_PREPROCESS);
  lv_obj_add_event_cb(
      text_area, TextAreaKeyboardPreprocessEventCallback,
      preprocess_event, binding);
  lv_obj_add_event_cb(text_area, TextAreaKeyboardPostprocessEventCallback,
      LV_EVENT_ALL, binding);
  return true;
}

void HideSharedKeyboard(lv_obj_t* keyboard) {
  if (keyboard == nullptr) {
    return;
  }

  lv_keyboard_set_textarea(keyboard, nullptr);
  lv_obj_add_flag(keyboard, LV_OBJ_FLAG_HIDDEN);
}

}  // namespace lilygo_box::ui
