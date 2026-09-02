/*
 * @Description: Material 风格首次开机欢迎页实现
 * @Author: LILYGO_L
 * @Date: 2026-07-15 00:00:00
 * @LastEditTime: 2026-07-15 00:00:00
 * @License: GPL 3.0
 */
#include "ui/views/first_boot_welcome_view.h"

#include <algorithm>
#include <new>

#include "ui/haptic_feedback.h"
#include "ui/input/press_cancel.h"
#include "ui/resources/fonts/font_assets.h"
#include "ui/widgets/brand_icon.h"

namespace lilygo_box::ui {
namespace {

constexpr int kPageSidePadding = 34;
constexpr int kHeroSize = 148;
constexpr int kCompactHeroSize = 112;
constexpr int kButtonMaxWidth = 520;
constexpr int kButtonHeight = 76;
constexpr int kCompactButtonHeight = 68;
constexpr int kButtonRadius = 24;
constexpr int kContentMaxWidth = 620;

struct FirstBootWelcomeState {
  std::function<bool()> completion_callback;
  bool completion_in_progress = false;
};

const lv_font_t* Font24() { return &lvgl_font_google_sans_flex_24; }
const lv_font_t* Font28() { return &lvgl_font_google_sans_flex_28; }
const lv_font_t* Font36() { return &lvgl_font_google_sans_flex_36; }
const lv_font_t* Font48() { return &lvgl_font_google_sans_flex_48; }

void MakeTransparent(lv_obj_t* object) {
  lv_obj_set_style_bg_opa(object, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_set_style_border_width(object, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(object, 0, LV_PART_MAIN);
}

lv_obj_t* CreateLabel(lv_obj_t* parent, const char* text, uint32_t color,
    const lv_font_t* font) {
  lv_obj_t* label = lv_label_create(parent);
  if (label == nullptr) {
    return nullptr;
  }
  lv_label_set_text(label, text);
  lv_obj_set_style_text_color(label, lv_color_hex(color), LV_PART_MAIN);
  lv_obj_set_style_text_font(label, font, LV_PART_MAIN);
  return label;
}

void WelcomeViewDeleteEventCallback(lv_event_t* event) {
  if (lv_event_get_code(event) == LV_EVENT_DELETE) {
    delete static_cast<FirstBootWelcomeState*>(
        lv_event_get_user_data(event));
  }
}

void GetStartedButtonClickedEventCallback(lv_event_t* event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
    return;
  }

  auto* state = static_cast<FirstBootWelcomeState*>(
      lv_event_get_user_data(event));
  if (state == nullptr || state->completion_in_progress ||
      !state->completion_callback) {
    return;
  }

  state->completion_in_progress = true;
  PlayUiHapticFeedback();
  std::function<bool()> completion_callback = state->completion_callback;
  const bool completed = completion_callback();
  if (!completed) {
    state->completion_in_progress = false;
  }
}

lv_obj_t* CreateHero(lv_obj_t* parent, int size) {
  return CreateLilygoBoxBrandIcon(parent, size);
}

lv_obj_t* CreateGetStartedButton(lv_obj_t* parent, int width, int height,
    const theme::ThemeColors& colors, FirstBootWelcomeState* state) {
  lv_obj_t* button = lv_button_create(parent);
  if (button == nullptr) {
    return nullptr;
  }
  lv_obj_remove_flag(button, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(button, width, height);
  lv_obj_set_style_bg_color(
      button, lv_color_hex(theme::FixedColors().action), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(button, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_bg_color(
      button, lv_color_hex(theme::FixedColors().action_pressed),
      LV_STATE_PRESSED);
  lv_obj_set_style_border_width(button, 0, LV_PART_MAIN);
  lv_obj_set_style_radius(button, kButtonRadius, LV_PART_MAIN);
  lv_obj_set_style_shadow_width(button, 10, LV_PART_MAIN);
  lv_obj_set_style_shadow_opa(button, 24, LV_PART_MAIN);
  lv_obj_set_style_shadow_color(
      button, lv_color_hex(theme::FixedColors().action), LV_PART_MAIN);
  if (!AddPressCancelOnLeave(button)) {
    lv_obj_delete(button);
    return nullptr;
  }
  lv_obj_add_event_cb(button, GetStartedButtonClickedEventCallback,
      LV_EVENT_CLICKED, state);

  lv_obj_t* label =
      CreateLabel(
          button, "Get started", theme::FixedColors().on_action, Font28());
  if (label == nullptr) {
    lv_obj_delete(button);
    return nullptr;
  }
  lv_obj_center(label);
  return button;
}

bool BuildPortraitLayout(lv_obj_t* page, int width, int height,
    const theme::ThemeColors& colors, FirstBootWelcomeState* state) {
  const bool compact = height < 800;
  const int hero_size = compact ? kCompactHeroSize : kHeroSize;
  const int content_width =
      std::min(kContentMaxWidth, width - 2 * kPageSidePadding);
  const int button_width =
      std::min(kButtonMaxWidth, width - 2 * kPageSidePadding);
  const int button_height = compact ? kCompactButtonHeight : kButtonHeight;

  lv_obj_t* hero = CreateHero(page, hero_size);
  if (hero == nullptr) {
    return false;
  }
  lv_obj_align(hero, LV_ALIGN_TOP_MID, 0, compact ? 104 : height / 7);

  lv_obj_t* title = CreateLabel(page, "Welcome to LilygoBox",
      colors.on_surface, compact ? Font36() : Font48());
  if (title == nullptr) {
    return false;
  }
  lv_obj_set_width(title, content_width);
  lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
  lv_obj_align_to(title, hero, LV_ALIGN_OUT_BOTTOM_MID, 0, compact ? 34 : 52);

  lv_obj_t* body = CreateLabel(page,
      "Your apps, connections, and device controls are ready in one place.",
      colors.on_surface_variant, compact ? Font24() : Font28());
  if (body == nullptr) {
    return false;
  }
  lv_obj_set_width(body, content_width);
  lv_label_set_long_mode(body, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_text_align(body, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
  lv_obj_set_style_text_line_space(body, compact ? 4 : 7, LV_PART_MAIN);
  lv_obj_align_to(body, title, LV_ALIGN_OUT_BOTTOM_MID, 0, compact ? 18 : 26);

  lv_obj_t* button = CreateGetStartedButton(
      page, button_width, button_height, colors, state);
  if (button == nullptr) {
    return false;
  }
  lv_obj_align(button, LV_ALIGN_BOTTOM_MID, 0, compact ? -24 : -42);
  return true;
}

bool BuildLandscapeLayout(lv_obj_t* page, int width, int height,
    const theme::ThemeColors& colors, FirstBootWelcomeState* state) {
  const int side_padding = std::max(kPageSidePadding, width / 24);
  const int hero_size = std::min(kHeroSize, height / 3);
  const int content_width = std::max(260, width / 2 - 2 * side_padding);
  const int button_width = std::min(kButtonMaxWidth, content_width);

  lv_obj_t* hero = CreateHero(page, hero_size);
  if (hero == nullptr) {
    return false;
  }
  lv_obj_align(hero, LV_ALIGN_LEFT_MID,
      std::max(side_padding, (width / 2 - hero_size) / 2), 10);

  lv_obj_t* content = lv_obj_create(page);
  if (content == nullptr) {
    return false;
  }
  lv_obj_remove_flag(content, LV_OBJ_FLAG_SCROLLABLE);
  MakeTransparent(content);
  lv_obj_set_size(content, content_width, height - 2 * side_padding);
  lv_obj_align(content, LV_ALIGN_RIGHT_MID, -side_padding, 0);

  lv_obj_t* title = CreateLabel(
      content, "Welcome to LilygoBox", colors.on_surface, Font48());
  if (title == nullptr) {
    return false;
  }
  lv_obj_set_width(title, content_width);
  lv_label_set_long_mode(title, LV_LABEL_LONG_WRAP);
  lv_obj_align(title, LV_ALIGN_TOP_LEFT, 0, 42);

  lv_obj_t* body = CreateLabel(content,
      "Your apps, connections, and device controls are ready in one place.",
      colors.on_surface_variant, Font28());
  if (body == nullptr) {
    return false;
  }
  lv_obj_set_width(body, content_width);
  lv_label_set_long_mode(body, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_text_line_space(body, 6, LV_PART_MAIN);
  lv_obj_align_to(body, title, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 22);

  lv_obj_t* button = CreateGetStartedButton(
      content, button_width, kCompactButtonHeight, colors, state);
  if (button == nullptr) {
    return false;
  }
  lv_obj_align(button, LV_ALIGN_BOTTOM_LEFT, 0, -48);
  return true;
}

}  // namespace

lv_obj_t* CreateFirstBootWelcomeView(
    lv_obj_t* parent, const FirstBootWelcomeViewOptions& options) {
  if (parent == nullptr || options.screen_width <= 0 ||
      options.screen_height <= 0 || !options.completion_callback) {
    return nullptr;
  }

  auto* state = new (std::nothrow) FirstBootWelcomeState{
      .completion_callback = options.completion_callback,
  };
  if (state == nullptr) {
    return nullptr;
  }

  lv_obj_t* page = lv_obj_create(parent);
  if (page == nullptr) {
    delete state;
    return nullptr;
  }
  lv_obj_add_event_cb(
      page, WelcomeViewDeleteEventCallback, LV_EVENT_DELETE, state);
  lv_obj_remove_flag(page, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(page, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_size(page, options.screen_width, options.screen_height);
  lv_obj_set_pos(page, 0, 0);

  const theme::ThemeColors& colors =
      options.colors == nullptr ? theme::ActiveThemeColors() : *options.colors;
  lv_obj_set_style_bg_color(page, lv_color_hex(colors.surface), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(page, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_border_width(page, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(page, 0, LV_PART_MAIN);
  lv_obj_set_style_radius(page, 0, LV_PART_MAIN);

  const bool built = options.screen_width > options.screen_height
                         ? BuildLandscapeLayout(page, options.screen_width,
                               options.screen_height, colors, state)
                         : BuildPortraitLayout(page, options.screen_width,
                               options.screen_height, colors, state);
  if (!built) {
    lv_obj_delete(page);
    return nullptr;
  }

  lv_obj_move_to_index(page, -1);
  return page;
}

}  // namespace lilygo_box::ui
