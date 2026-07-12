/*
 * @Description: None
 * @Author: LILYGO_L
 * @Date: 2026-05-10 13:27:05
 * @LastEditTime: 2026-05-12 01:08:42
 * @License: GPL 3.0
 */
#include "ui/app_view_factory.h"

#include <cstring>

#include "base/logger.h"
#include "ui/resources/fonts/font_assets.h"
#include "ui/views/camera_view.h"
#include "ui/views/cit_view.h"
#include "ui/views/files_view.h"
#include "ui/views/music_view.h"
#include "ui/views/rf_view.h"
#include "ui/views/settings_view.h"

namespace lilygo_box::ui {
namespace {

constexpr int kViewRadius = 0;
constexpr int kButtonRadius = 12;
constexpr int kViewPadding = 22;
constexpr int kViewTopPadding = 70;
constexpr int kBackButtonWidth = 190;
constexpr int kBackButtonHeight = 70;

/**
 * @brief 设置文本对象的颜色和字体
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
 * @brief 获取 48 号 Google Sans 字体
 * @return 字体指针
 */
const lv_font_t* Font48() { return &lvgl_font_google_sans_flex_48; }

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

  lv_label_set_text(label, text);
  SetTextStyle(label, color, font);
  return label;
}

/**
 * @brief 创建占位应用视图的返回按钮
 * @param parent 父对象
 * @param config 应用视图配置
 * @return 创建成功返回对象指针，否则返回 nullptr
 */
lv_obj_t* CreateBackButton(lv_obj_t* parent, const AppViewConfig& config) {
  lv_obj_t* button = lv_button_create(parent);
  if (button == nullptr) {
    LogMessage(LogLevel::kError, __FILE__, __LINE__,
        "Create back button failed\n");
    return nullptr;
  }

  lv_obj_set_size(button, kBackButtonWidth, kBackButtonHeight);
  lv_obj_set_style_radius(button, kButtonRadius, LV_PART_MAIN);
  lv_obj_set_style_bg_color(button, lv_color_hex(0x2D3C48), LV_PART_MAIN);
  lv_obj_align(button, LV_ALIGN_BOTTOM_LEFT, 0, 0);

  if (config.back_callback != nullptr) {
    lv_obj_add_event_cb(
        button, config.back_callback, LV_EVENT_CLICKED, config.back_context);
  }

  lv_obj_t* label =
      CreateLabel(button, "Back", lv_color_hex(0xF5F7FA), Font24());
  if (label == nullptr) {
    LogMessage(LogLevel::kError, __FILE__, __LINE__,
        "Create back button label failed\n");
    lv_obj_delete(button);
    return nullptr;
  }

  lv_obj_center(label);
  return button;
}

/**
 * @brief 判断应用 ID 是否匹配
 * @param app_entry 应用条目
 * @param id 目标 ID
 * @return 匹配返回 true，否则返回 false
 */
bool IsAppId(const app::AppEntry& app_entry, const char* id) {
  if (app_entry.id == nullptr || id == nullptr) {
    return false;
  }
  return std::strcmp(app_entry.id, id) == 0;
}

/**
 * @brief 创建尚未实现的占位应用视图
 * @param parent 父对象
 * @param app_entry 应用条目
 * @param config 应用视图配置
 * @return 创建成功返回对象指针，否则返回 nullptr
 */
lv_obj_t* CreatePlaceholderAppView(lv_obj_t* parent,
    const app::AppEntry& app_entry, const AppViewConfig& config) {
  lv_obj_t* container = lv_obj_create(parent);
  if (container == nullptr) {
    LogMessage(LogLevel::kError, __FILE__, __LINE__,
        "Create placeholder app view container failed, app_id=%s\n",
        app_entry.id == nullptr ? "" : app_entry.id);
    return nullptr;
  }

  lv_obj_remove_flag(container, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_color(container, lv_color_hex(0x121820), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(container, 245, LV_PART_MAIN);
  lv_obj_set_style_radius(container, kViewRadius, LV_PART_MAIN);
  lv_obj_set_style_border_width(container, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(container, kViewPadding, LV_PART_MAIN);
  lv_obj_set_style_pad_top(container, kViewTopPadding, LV_PART_MAIN);
  lv_obj_set_size(container, config.width, config.height);
  lv_obj_align(container, LV_ALIGN_CENTER, 0, 0);

  lv_obj_t* title =
      CreateLabel(container, app_entry.title, lv_color_hex(0xF5F7FA), Font48());
  if (title == nullptr) {
    LogMessage(LogLevel::kError, __FILE__, __LINE__,
        "Create placeholder app title failed, app_id=%s\n",
        app_entry.id == nullptr ? "" : app_entry.id);
    lv_obj_delete(container);
    return nullptr;
  }
  lv_obj_align(title, LV_ALIGN_TOP_LEFT, 0, 0);

  lv_obj_t* subtitle = CreateLabel(
      container, app_entry.subtitle, lv_color_hex(0xAAB2BD), Font24());
  if (subtitle == nullptr) {
    LogMessage(LogLevel::kError, __FILE__, __LINE__,
        "Create placeholder app subtitle failed, app_id=%s\n",
        app_entry.id == nullptr ? "" : app_entry.id);
    lv_obj_delete(container);
    return nullptr;
  }
  lv_obj_align_to(subtitle, title, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 14);

  lv_obj_t* message = CreateLabel(container, "View implementation pending",
      lv_color_hex(0x51D88A), Font24());
  if (message == nullptr) {
    LogMessage(LogLevel::kError, __FILE__, __LINE__,
        "Create placeholder app message failed, app_id=%s\n",
        app_entry.id == nullptr ? "" : app_entry.id);
    lv_obj_delete(container);
    return nullptr;
  }
  lv_obj_align_to(message, subtitle, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 24);

  if (CreateBackButton(container, config) == nullptr) {
    LogMessage(LogLevel::kError, __FILE__, __LINE__,
        "Create placeholder app back button failed, app_id=%s\n",
        app_entry.id == nullptr ? "" : app_entry.id);
    lv_obj_delete(container);
    return nullptr;
  }

  return container;
}

}  // namespace

lv_obj_t* CreateAppView(lv_obj_t* parent, const app::AppEntry& app_entry,
    const AppViewConfig& config) {
  if (parent == nullptr || config.width <= 0 || config.height <= 0) {
    LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
        "CreateAppView received invalid input, parent=%p, width=%d, height=%d\n",
        parent, config.width, config.height);
    return nullptr;
  }

  if (IsAppId(app_entry, "camera")) {
    return CreateCameraView(parent, app_entry, config);
  }
  if (IsAppId(app_entry, "cit")) {
    return CreateCitView(parent, app_entry, config);
  }
  if (IsAppId(app_entry, "music")) {
    return CreateMusicView(parent, app_entry, config);
  }
  if (IsAppId(app_entry, "files")) {
    return CreateFilesView(parent, app_entry, config);
  }
  if (IsAppId(app_entry, "rf")) {
    return CreateRfView(parent, app_entry, config);
  }
  if (IsAppId(app_entry, "settings")) {
    return CreateSettingsView(parent, app_entry, config);
  }
  return CreatePlaceholderAppView(parent, app_entry, config);
}

}  // namespace lilygo_box::ui
