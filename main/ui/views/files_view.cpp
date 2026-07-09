/*
 * @Description: Files app view
 * @Author: LILYGO_L
 * @Date: 2026-07-09 00:00:00
 * @LastEditTime: 2026-07-09 00:00:00
 * @License: GPL 3.0
 */
#include "ui/views/files_view.h"

#include "base/logger.h"
#include "ui/font/font_assets.h"
#include "ui/font/material_symbols_assets.h"
#include "ui/theme/theme_provider.h"

namespace lilygo_box::ui {
namespace {

constexpr uint32_t kBackgroundColor = theme::LightNeutralTheme().surface;
constexpr uint32_t kPrimaryTextColor = theme::LightNeutralTheme().on_surface;
constexpr uint32_t kSecondaryTextColor =
    theme::LightNeutralTheme().on_surface_variant;
constexpr uint32_t kIconColor = theme::LightNeutralTheme().on_surface_variant;
constexpr uint32_t kDividerColor = theme::LightNeutralTheme().outline_variant;
constexpr uint32_t kDrawerScrimColor = 0x000000;
constexpr uint32_t kPressedColor = theme::LightNeutralTheme().state_layer;
constexpr uint32_t kRefreshButtonColor = theme::LightNeutralTheme().action;
constexpr uint32_t kRefreshButtonTextColor =
    theme::LightNeutralTheme().on_action;
constexpr int kHeaderTop = 72;
constexpr int kHeaderSidePadding = 28;
constexpr int kDrawerWidthPercent = 78;
constexpr int kDrawerAnimationMs = 220;
constexpr int kDrawerItemHeight = 104;

struct FilesViewState {
  AppViewConfig config;
  lv_obj_t* root = nullptr;
  lv_obj_t* drawer_overlay = nullptr;
  lv_obj_t* drawer_panel = nullptr;
};

/**
 * @brief 设置对象背景、边框和内边距为透明
 * @param object LVGL 对象
 */
void MakeTransparent(lv_obj_t* object) {
  lv_obj_set_style_bg_opa(object, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_set_style_border_width(object, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(object, 0, LV_PART_MAIN);
}

/**
 * @brief 设置文本对象颜色和字体
 * @param object LVGL 对象
 * @param color 文本颜色
 * @param font 文本字体
 */
void SetTextStyle(lv_obj_t* object, lv_color_t color, const lv_font_t* font) {
  lv_obj_set_style_text_color(object, color, LV_PART_MAIN);
  lv_obj_set_style_text_font(object, font, LV_PART_MAIN);
}

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
 * @brief 获取 48 号 Google Sans 字体
 * @return 字体指针
 */
const lv_font_t* Font48() { return &lvgl_font_google_sans_flex_48; }

/**
 * @brief 获取 44 号文件管理抽屉 Material Symbols 字体
 * @return 字体指针
 */
const lv_font_t* FilesDrawerIconFont44() {
  return &lvgl_font_material_symbols_fill_44;
}

/**
 * @brief 获取 56 号文件管理 Material Symbols 字体
 * @return 字体指针
 */
const lv_font_t* FilesMaterialIconFont56() {
  return &lvgl_font_material_symbols_fill_56;
}

/**
 * @brief 设置对象 X 坐标
 * @param object LVGL 对象
 * @param x X 坐标
 */
void SetObjectX(void* object, int32_t x) {
  lv_obj_set_x(static_cast<lv_obj_t*>(object), x);
}

/**
 * @brief 创建文本标签
 * @param parent 父对象
 * @param text 标签文本
 * @param color 文本颜色
 * @param font 文本字体
 * @return 创建成功返回标签对象，否则返回 nullptr
 */
lv_obj_t* CreateLabel(lv_obj_t* parent, const char* text, lv_color_t color,
    const lv_font_t* font) {
  lv_obj_t* label = lv_label_create(parent);
  if (label == nullptr) {
    return nullptr;
  }
  lv_label_set_text(label, text);
  SetTextStyle(label, color, font);
  return label;
}

/**
 * @brief 创建大号 Material Symbols 图标标签
 * @param parent 父对象
 * @param symbol 图标文本
 * @param color 图标颜色
 * @return 创建成功返回图标对象，否则返回 nullptr
 */
lv_obj_t* CreateLargeIcon(
    lv_obj_t* parent, const char* symbol, lv_color_t color) {
  lv_obj_t* icon = CreateLabel(parent, symbol, color, FilesMaterialIconFont56());
  if (icon != nullptr) {
    lv_obj_set_style_text_align(icon, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
  }
  return icon;
}

/**
 * @brief 创建抽屉 Material Symbols 图标标签
 * @param parent 父对象
 * @param symbol 图标文本
 * @param color 图标颜色
 * @return 创建成功返回图标对象，否则返回 nullptr
 */
lv_obj_t* CreateDrawerIcon(
    lv_obj_t* parent, const char* symbol, lv_color_t color) {
  lv_obj_t* icon = CreateLabel(parent, symbol, color, FilesDrawerIconFont44());
  if (icon != nullptr) {
    lv_obj_set_style_text_align(icon, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
  }
  return icon;
}

/**
 * @brief 创建扁平图标按钮
 * @param parent 父对象
 * @param symbol 图标文本
 * @return 创建成功返回按钮对象，否则返回 nullptr
 */
lv_obj_t* CreateIconButton(lv_obj_t* parent, const char* symbol) {
  lv_obj_t* button = lv_button_create(parent);
  if (button == nullptr) {
    return nullptr;
  }
  lv_obj_set_size(button, 76, 76);
  lv_obj_set_style_bg_opa(button, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_set_style_bg_opa(button, LV_OPA_TRANSP, LV_STATE_PRESSED);
  lv_obj_set_style_radius(button, 38, LV_PART_MAIN);
  lv_obj_set_style_border_width(button, 0, LV_PART_MAIN);
  lv_obj_set_style_shadow_width(button, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(button, 0, LV_PART_MAIN);

  lv_obj_t* icon =
      CreateLargeIcon(button, symbol, lv_color_hex(kPrimaryTextColor));
  if (icon != nullptr) {
    lv_obj_center(icon);
  }
  return button;
}

/**
 * @brief 抽屉关闭动画完成后删除遮罩
 * @param animation LVGL 动画对象
 */
void DrawerCloseCompletedCallback(lv_anim_t* animation) {
  auto* state = static_cast<FilesViewState*>(lv_anim_get_user_data(animation));
  if (state == nullptr || state->drawer_overlay == nullptr) {
    return;
  }

  lv_obj_t* overlay = state->drawer_overlay;
  state->drawer_overlay = nullptr;
  state->drawer_panel = nullptr;
  lv_obj_delete(overlay);
}

/**
 * @brief 关闭文件管理抽屉
 * @param state 文件管理视图状态
 */
void CloseDrawer(FilesViewState* state) {
  if (state == nullptr || state->drawer_overlay == nullptr) {
    return;
  }

  if (state->drawer_panel == nullptr) {
    lv_obj_t* overlay = state->drawer_overlay;
    state->drawer_overlay = nullptr;
    lv_obj_delete(overlay);
    return;
  }

  const int drawer_width = lv_obj_get_width(state->drawer_panel);
  lv_anim_delete(state->drawer_panel, SetObjectX);
  lv_anim_t animation;
  lv_anim_init(&animation);
  lv_anim_set_var(&animation, state->drawer_panel);
  lv_anim_set_values(&animation, lv_obj_get_x(state->drawer_panel),
      -drawer_width);
  lv_anim_set_duration(&animation, kDrawerAnimationMs);
  lv_anim_set_path_cb(&animation, lv_anim_path_ease_in);
  lv_anim_set_exec_cb(&animation, SetObjectX);
  lv_anim_set_user_data(&animation, state);
  lv_anim_set_completed_cb(&animation, DrawerCloseCompletedCallback);
  lv_anim_start(&animation);
}

/**
 * @brief 处理抽屉灰色遮罩点击事件
 * @param event LVGL 事件对象
 */
void DrawerOverlayClickedEventCallback(lv_event_t* event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
    return;
  }
  lv_obj_t* target = lv_event_get_target_obj(event);
  lv_obj_t* current_target = lv_event_get_current_target_obj(event);
  if (target != current_target) {
    return;
  }
  auto* state = static_cast<FilesViewState*>(lv_event_get_user_data(event));
  CloseDrawer(state);
}

/**
 * @brief 处理抽屉菜单项点击事件
 * @param event LVGL 事件对象
 */
void DrawerItemClickedEventCallback(lv_event_t* event) {
  const lv_event_code_t code = lv_event_get_code(event);
  if (code != LV_EVENT_CLICKED && code != LV_EVENT_RELEASED &&
      code != LV_EVENT_PRESS_LOST) {
    return;
  }
  lv_obj_t* row = lv_event_get_current_target_obj(event);
  if (row != nullptr) {
    lv_obj_clear_state(row, LV_STATE_PRESSED);
    lv_obj_clear_state(row, LV_STATE_FOCUSED);
    lv_obj_clear_state(row, LV_STATE_CHECKED);
    lv_obj_clear_state(row, LV_STATE_FOCUS_KEY);
  }
  lv_event_stop_bubbling(event);
}

/**
 * @brief 创建抽屉菜单行
 * @param parent 父对象
 * @param symbol 图标文本
 * @param text 菜单文本
 * @param y Y 坐标
 * @param state 文件管理视图状态
 * @return 创建成功返回菜单行对象，否则返回 nullptr
 */
lv_obj_t* CreateDrawerItem(lv_obj_t* parent, int drawer_width,
    const char* symbol, const char* text, int y, FilesViewState* state) {
  lv_obj_t* row = lv_button_create(parent);
  if (row == nullptr) {
    return nullptr;
  }
  lv_obj_remove_style_all(row);
  lv_obj_set_size(row, drawer_width, kDrawerItemHeight);
  lv_obj_set_pos(row, 0, y);
  lv_obj_clear_state(row, LV_STATE_CHECKED);
  lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_set_style_bg_color(row, lv_color_hex(kPressedColor),
      LV_STATE_PRESSED);
  lv_obj_set_style_bg_opa(row, LV_OPA_80, LV_STATE_PRESSED);
  lv_obj_set_style_bg_color(row, lv_color_hex(kPressedColor),
      LV_STATE_FOCUSED);
  lv_obj_set_style_bg_opa(row, LV_OPA_80, LV_STATE_FOCUSED);
  lv_obj_set_style_bg_color(row, lv_color_hex(kPressedColor),
      LV_STATE_CHECKED);
  lv_obj_set_style_bg_opa(row, LV_OPA_80, LV_STATE_CHECKED);
  lv_obj_set_style_bg_color(row, lv_color_hex(kPressedColor),
      LV_STATE_FOCUS_KEY);
  lv_obj_set_style_bg_opa(row, LV_OPA_80, LV_STATE_FOCUS_KEY);
  lv_obj_set_style_border_width(row, 0, LV_PART_MAIN);
  lv_obj_set_style_shadow_width(row, 0, LV_PART_MAIN);
  lv_obj_set_style_radius(row, 0, LV_PART_MAIN);
  lv_obj_set_style_radius(row, 0, LV_STATE_PRESSED);
  lv_obj_set_style_radius(row, 0, LV_STATE_FOCUSED);
  lv_obj_set_style_radius(row, 0, LV_STATE_CHECKED);
  lv_obj_set_style_radius(row, 0, LV_STATE_FOCUS_KEY);
  lv_obj_add_event_cb(row, DrawerItemClickedEventCallback, LV_EVENT_CLICKED,
      state);
  lv_obj_add_event_cb(row, DrawerItemClickedEventCallback, LV_EVENT_RELEASED,
      state);
  lv_obj_add_event_cb(row, DrawerItemClickedEventCallback, LV_EVENT_PRESS_LOST,
      state);

  lv_obj_t* icon = CreateDrawerIcon(row, symbol, lv_color_hex(kIconColor));
  if (icon != nullptr) {
    lv_obj_align(icon, LV_ALIGN_LEFT_MID, 34, 0);
  }
  lv_obj_t* label = CreateLabel(row, text, lv_color_hex(kPrimaryTextColor),
      Font28());
  if (label != nullptr) {
    lv_obj_set_width(label, drawer_width - 132);
    lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
    lv_obj_align(label, LV_ALIGN_LEFT_MID, 112, 0);
  }
  return row;
}

/**
 * @brief 创建左侧文件管理抽屉
 * @param state 文件管理视图状态
 */
void ShowDrawer(FilesViewState* state) {
  if (state == nullptr || state->root == nullptr ||
      state->drawer_overlay != nullptr) {
    return;
  }

  lv_obj_t* overlay = lv_obj_create(state->root);
  if (overlay == nullptr) {
    return;
  }
  state->drawer_overlay = overlay;
  lv_obj_set_size(overlay, state->config.width, state->config.height);
  lv_obj_set_style_bg_color(overlay, lv_color_hex(kDrawerScrimColor),
      LV_PART_MAIN);
  lv_obj_set_style_bg_opa(overlay, LV_OPA_50, LV_PART_MAIN);
  lv_obj_set_style_border_width(overlay, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(overlay, 0, LV_PART_MAIN);
  lv_obj_remove_flag(overlay, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_event_cb(overlay, DrawerOverlayClickedEventCallback,
      LV_EVENT_CLICKED, state);

  const int drawer_width = state->config.width * kDrawerWidthPercent / 100;
  lv_obj_t* drawer = lv_obj_create(overlay);
  if (drawer == nullptr) {
    CloseDrawer(state);
    return;
  }
  state->drawer_panel = drawer;
  lv_obj_set_size(drawer, drawer_width, state->config.height);
  lv_obj_set_pos(drawer, -drawer_width, 0);
  lv_obj_set_style_bg_color(drawer, lv_color_hex(kBackgroundColor),
      LV_PART_MAIN);
  lv_obj_set_style_bg_opa(drawer, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_border_width(drawer, 0, LV_PART_MAIN);
  lv_obj_set_style_radius(drawer, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(drawer, 0, LV_PART_MAIN);
  lv_obj_remove_flag(drawer, LV_OBJ_FLAG_SCROLLABLE);

  CreateDrawerItem(
      drawer, drawer_width, icon::kRefresh, "Refresh Storage", 96, state);

  lv_obj_t* divider = lv_obj_create(drawer);
  if (divider != nullptr) {
    lv_obj_set_size(divider, drawer_width, 2);
    lv_obj_set_pos(divider, 0, 216);
    lv_obj_set_style_bg_color(divider, lv_color_hex(kDividerColor),
        LV_PART_MAIN);
    lv_obj_set_style_bg_opa(divider, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(divider, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(divider, 0, LV_PART_MAIN);
  }

  CreateDrawerItem(
      drawer, drawer_width, icon::kSettings, "Settings", 234, state);

  lv_anim_delete(drawer, SetObjectX);
  lv_anim_t animation;
  lv_anim_init(&animation);
  lv_anim_set_var(&animation, drawer);
  lv_anim_set_values(&animation, -drawer_width, 0);
  lv_anim_set_duration(&animation, kDrawerAnimationMs);
  lv_anim_set_path_cb(&animation, lv_anim_path_ease_out);
  lv_anim_set_exec_cb(&animation, SetObjectX);
  lv_anim_start(&animation);
}

/**
 * @brief 处理顶部菜单按钮点击事件
 * @param event LVGL 事件对象
 */
void MenuButtonClickedEventCallback(lv_event_t* event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
    return;
  }
  auto* state = static_cast<FilesViewState*>(lv_event_get_user_data(event));
  ShowDrawer(state);
}

/**
 * @brief 创建文件管理顶部栏
 * @param parent 父对象
 * @param state 文件管理视图状态
 * @return 创建成功返回 true，否则返回 false
 */
bool CreateHeader(lv_obj_t* parent, FilesViewState* state) {
  lv_obj_t* menu = CreateIconButton(parent, icon::kMenu);
  if (menu == nullptr) {
    return false;
  }
  lv_obj_align(menu, LV_ALIGN_TOP_LEFT, kHeaderSidePadding - 6,
      kHeaderTop + 2);
  lv_obj_add_event_cb(menu, MenuButtonClickedEventCallback, LV_EVENT_CLICKED,
      state);

  lv_obj_t* title =
      CreateLabel(parent, "Files", lv_color_hex(kPrimaryTextColor), Font48());
  if (title == nullptr) {
    return false;
  }
  lv_obj_align(title, LV_ALIGN_TOP_LEFT, 116, kHeaderTop);

  lv_obj_t* subtitle =
      CreateLabel(parent, "No storage", lv_color_hex(kSecondaryTextColor),
          Font28());
  if (subtitle != nullptr) {
    lv_obj_align_to(subtitle, title, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 4);
  }
  return true;
}

/**
 * @brief 创建文件管理空状态内容
 * @param parent 父对象
 * @param config 应用视图配置
 * @return 创建成功返回 true，否则返回 false
 */
bool CreateEmptyStorageContent(lv_obj_t* parent, const AppViewConfig& config) {
  lv_obj_t* group = lv_obj_create(parent);
  if (group == nullptr) {
    return false;
  }
  MakeTransparent(group);
  lv_obj_remove_flag(group, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(group, config.width, 136);
  lv_obj_align(group, LV_ALIGN_CENTER, 0, 64);

  lv_obj_t* message =
      CreateLabel(group, "No storage found", lv_color_hex(kPrimaryTextColor),
          Font28());
  if (message != nullptr) {
    lv_obj_align(message, LV_ALIGN_TOP_MID, 0, 0);
  }

  lv_obj_t* refresh = lv_button_create(group);
  if (refresh == nullptr) {
    return false;
  }
  lv_obj_set_size(refresh, 230, 62);
  lv_obj_set_style_radius(refresh, 31, LV_PART_MAIN);
  lv_obj_set_style_bg_color(refresh, lv_color_hex(kRefreshButtonColor),
      LV_PART_MAIN);
  lv_obj_set_style_bg_opa(refresh, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_border_width(refresh, 0, LV_PART_MAIN);
  lv_obj_set_style_shadow_width(refresh, 0, LV_PART_MAIN);
  lv_obj_align(refresh, LV_ALIGN_TOP_MID, 0, 56);

  lv_obj_t* label =
      CreateLabel(refresh, "Refresh Storage",
          lv_color_hex(kRefreshButtonTextColor), Font24());
  if (label != nullptr) {
    lv_obj_center(label);
  }
  return true;
}

}  // namespace

lv_obj_t* CreateFilesView(lv_obj_t* parent, const app::AppEntry&,
    const AppViewConfig& config) {
  if (parent == nullptr || config.width <= 0 || config.height <= 0) {
    LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
        "CreateFilesView received invalid input, parent=%p, width=%d, "
        "height=%d\n",
        parent, config.width, config.height);
    return nullptr;
  }

  auto* state = new FilesViewState{
      .config = config,
  };

  lv_obj_t* root = lv_obj_create(parent);
  if (root == nullptr) {
    delete state;
    return nullptr;
  }
  state->root = root;
  lv_obj_set_size(root, config.width, config.height);
  lv_obj_align(root, LV_ALIGN_CENTER, 0, 0);
  lv_obj_set_style_bg_color(root, lv_color_hex(kBackgroundColor),
      LV_PART_MAIN);
  lv_obj_set_style_bg_opa(root, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_border_width(root, 0, LV_PART_MAIN);
  lv_obj_set_style_radius(root, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(root, 0, LV_PART_MAIN);
  lv_obj_remove_flag(root, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_event_cb(
      root,
      [](lv_event_t* event) {
        if (lv_event_get_code(event) == LV_EVENT_DELETE) {
          delete static_cast<FilesViewState*>(lv_event_get_user_data(event));
        }
      },
      LV_EVENT_DELETE, state);

  if (config.set_status_bar_visible) {
    config.set_status_bar_visible(true);
  }
  if (config.set_status_bar_text_color) {
    config.set_status_bar_text_color(kPrimaryTextColor);
  }

  if (!CreateHeader(root, state) ||
      !CreateEmptyStorageContent(root, config)) {
    lv_obj_delete(root);
    return nullptr;
  }

  return root;
}

}  // namespace lilygo_box::ui
