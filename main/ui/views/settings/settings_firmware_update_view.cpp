/*
 * @Description: 设置固件更新界面预览
 * @Author: LILYGO_L
 * @Date: 2026-07-19 00:00:00
 * @LastEditTime: 2026-07-19 00:00:00
 * @License: GPL 3.0
 */
#include "ui/views/settings/settings_view_internal.h"

#include <cstdio>

#include "ui/animation/transition_animation.h"
#include "ui/input/app_view_gesture_flags.h"
#include "ui/input/edge_back_gesture.h"
#include "ui/input/press_cancel.h"
#include "ui/resources/fonts/icon_assets.h"
#include "ui/widgets/brand_icon.h"

namespace lilygo_box::ui {
namespace {

constexpr int kUpdateBottomAreaHeight = 116;
constexpr int kUpdateHeadingTop = 14;
constexpr int kUpdateCardTop = 82;
constexpr int kUpdateCardHeight = 690;
constexpr int kUpdateCardSide = 26;
constexpr int kUpdateCardPadding = 34;
constexpr int kUpdateBrandTop = 34;
constexpr int kUpdateBrandIconSize = 58;
constexpr int kUpdateBrandGap = 14;
constexpr int kUpdateVersionTop = 112;
constexpr int kUpdateDividerTop = 170;
constexpr int kUpdateComponentsTitleTop = 204;
constexpr int kUpdateComponentsTop = 250;
constexpr int kUpdateComponentHeight = 82;
constexpr int kUpdateComponentGap = 12;
constexpr int kUpdateComponentIconLeft = 18;
constexpr int kUpdateComponentTextLeft = 62;
constexpr int kUpdateComponentVersionWidth = 190;
constexpr int kUpdateSecondDividerTop = 438;
constexpr int kUpdateWhatsNewTitleTop = 472;
constexpr int kUpdateWhatsNewTop = 518;
constexpr int kUpdateButtonWidth = 500;
constexpr int kUpdateButtonHeight = 76;
constexpr int kUpdateButtonBottom = 24;
constexpr int kUpdateMaxContentWidth = 516;
constexpr uint32_t kUpdateCardColor =
    theme::LightNeutralTheme().surface_container_lowest;
constexpr uint32_t kUpdateFeatureColor =
    theme::LightNeutralTheme().surface_container_low;
constexpr char kPreviewVersion[] = "v1.1.0";
constexpr char kPreviewPackageSize[] = "12 MB";

/**
 * @brief 计算固件更新页面内容区域宽度
 * @param width 页面宽度
 * @return 限制最大宽度后的内容区域宽度
 */
int FirmwareUpdateContentWidth(int width) {
  const int available_width = width - 2 * kUpdateCardSide;
  if (available_width < kUpdateMaxContentWidth) {
    return available_width;
  }
  return kUpdateMaxContentWidth;
}

/**
 * @brief 清除固件更新页面相关状态引用
 * @param state 设置页面状态
 */
void ClearFirmwareUpdateReferences(SettingsViewState* state) {
  if (state == nullptr) {
    return;
  }

  state->firmware_update_page = nullptr;
  state->firmware_update_closing = false;
  state->firmware_update_swipe = EdgeBackSwipeState();
}

/**
 * @brief 处理固件更新页面关闭动画完成事件
 * @param animation LVGL 动画对象
 */
void FirmwareUpdateCloseCompletedCallback(lv_anim_t* animation) {
  auto* state =
      static_cast<SettingsViewState*>(lv_anim_get_user_data(animation));
  if (state == nullptr || state->firmware_update_page == nullptr) {
    return;
  }

  lv_obj_t* page = state->firmware_update_page;
  ClearFirmwareUpdateReferences(state);
  lv_obj_delete(page);
}

/**
 * @brief 处理固件更新页面返回按钮点击事件
 * @param event LVGL 事件对象
 */
void FirmwareUpdateBackClickedEventCallback(lv_event_t* event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
    return;
  }

  CloseFirmwareUpdatePage(
      static_cast<SettingsViewState*>(lv_event_get_user_data(event)), true);
}

/**
 * @brief 处理固件更新页面边缘滑动返回事件
 * @param event LVGL 事件对象
 */
void FirmwareUpdateEdgeBackEventCallback(lv_event_t* event) {
  auto* state = static_cast<SettingsViewState*>(lv_event_get_user_data(event));
  if (state == nullptr || state->firmware_update_page == nullptr ||
      state->firmware_update_closing || state->config.screen == nullptr ||
      !HandleEdgeBackSwipeEvent(event, state->config.width,
          &state->firmware_update_swipe)) {
    return;
  }

  CloseFirmwareUpdatePage(state, true);
  lv_event_stop_bubbling(event);
  lv_event_stop_processing(event);
}

/**
 * @brief 创建固件更新页面顶部导航栏
 * @param parent 父对象
 * @param state 设置页面状态
 * @param width 页面宽度
 * @return 创建成功返回 true，否则返回 false
 */
bool CreateFirmwareUpdateHeader(
    lv_obj_t* parent, SettingsViewState* state, int width) {
  lv_obj_t* title = CreateLabel(
      parent, "Firmware update", lv_color_hex(kTitleColor), Font32());
  if (title == nullptr) {
    return false;
  }
  lv_obj_set_width(title, width);
  lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
  lv_obj_align(title, LV_ALIGN_TOP_MID, 0, kDetailTitleTop);

  lv_obj_t* back_button = CreateToolbarButton(parent,
      kDetailBackButtonLeft, kDetailBackButtonTop,
      FirmwareUpdateBackClickedEventCallback, state);
  if (back_button == nullptr) {
    return false;
  }

  lv_obj_t* back_icon = CreateLabel(back_button, icon::kArrowBack,
      lv_color_hex(kDetailBackColor), MaterialIconFont44());
  if (back_icon == nullptr) {
    return false;
  }
  lv_obj_align(back_icon, LV_ALIGN_CENTER, kDetailBackIconOffsetX, 0);
  return true;
}

/**
 * @brief 创建固件更新卡片中的 LilygoBox 品牌区域
 * @param card 固件更新卡片
 * @return 创建成功返回 true，否则返回 false
 */
bool CreateFirmwareBrand(lv_obj_t* card) {
  lv_obj_t* brand_group = lv_obj_create(card);
  if (brand_group == nullptr) {
    return false;
  }
  MakeTransparent(brand_group);
  lv_obj_remove_flag(brand_group, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(brand_group, LV_OBJ_FLAG_CLICKABLE);

  lv_obj_t* brand_icon =
      CreateLilygoBoxBrandIcon(brand_group, kUpdateBrandIconSize);
  if (brand_icon == nullptr) {
    return false;
  }

  lv_obj_t* brand_text = CreateLabel(
      brand_group, "LilygoBox", lv_color_hex(kPrimaryTextColor), Font48());
  if (brand_text == nullptr) {
    return false;
  }
  lv_obj_update_layout(brand_text);
  const int brand_width = kUpdateBrandIconSize + kUpdateBrandGap +
                          lv_obj_get_width(brand_text);
  lv_obj_set_size(brand_group, brand_width, kUpdateBrandIconSize);
  lv_obj_align(brand_group, LV_ALIGN_TOP_LEFT, kUpdateCardPadding,
      kUpdateBrandTop);
  lv_obj_align(brand_icon, LV_ALIGN_LEFT_MID, 0, 0);
  lv_obj_align_to(brand_text, brand_icon, LV_ALIGN_OUT_RIGHT_MID,
      kUpdateBrandGap, 0);
  return true;
}

/**
 * @brief 创建单个固件组件信息行
 * @param card 固件更新卡片
 * @param y 信息行顶部坐标
 * @param width 信息行宽度
 * @param symbol 组件图标
 * @param title 组件名称
 * @param chip 芯片型号
 * @param version 当前版本与目标版本说明
 * @param color 图标颜色
 * @return 创建成功返回 true，否则返回 false
 */
bool CreateFirmwareComponentRow(lv_obj_t* card, int y, int width,
    const char* symbol, const char* title, const char* chip,
    const char* version, uint32_t color) {
  lv_obj_t* tile = CreateBox(card, width, kUpdateComponentHeight,
      kUpdateFeatureColor, LV_OPA_COVER, 20);
  if (tile == nullptr) {
    return false;
  }
  lv_obj_remove_flag(tile, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_pos(tile, kUpdateCardPadding, y);

  lv_obj_t* component_icon = CreateLabel(
      tile, symbol, lv_color_hex(color), MaterialIconFont32());
  if (component_icon == nullptr) {
    return false;
  }
  lv_obj_align(component_icon, LV_ALIGN_LEFT_MID,
      kUpdateComponentIconLeft, 0);

  lv_obj_t* component_title =
      CreateLabel(tile, title, lv_color_hex(kPrimaryTextColor), Font22());
  lv_obj_t* component_chip =
      CreateLabel(tile, chip, lv_color_hex(kSecondaryTextColor), Font22());
  lv_obj_t* component_version =
      CreateLabel(tile, version, lv_color_hex(kSecondaryTextColor), Font22());
  if (component_title == nullptr || component_chip == nullptr ||
      component_version == nullptr) {
    return false;
  }
  lv_obj_align(component_title, LV_ALIGN_LEFT_MID,
      kUpdateComponentTextLeft, -15);
  lv_obj_align(component_chip, LV_ALIGN_LEFT_MID,
      kUpdateComponentTextLeft, 15);
  lv_obj_set_width(component_version, kUpdateComponentVersionWidth);
  lv_obj_set_style_text_align(
      component_version, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);
  lv_obj_align(component_version, LV_ALIGN_RIGHT_MID, -18, 0);
  return true;
}

/**
 * @brief 创建固件更新版本卡片
 * @param body 页面可滚动内容区域
 * @param width 页面宽度
 * @return 创建成功返回 true，否则返回 false
 */
bool CreateFirmwareUpdateCard(lv_obj_t* body, int width) {
  const int card_width = FirmwareUpdateContentWidth(width);
  lv_obj_t* card = CreateBox(body, card_width, kUpdateCardHeight,
      kUpdateCardColor, LV_OPA_COVER, kDetailCardRadius);
  if (card == nullptr) {
    return false;
  }
  lv_obj_remove_flag(card, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_align(card, LV_ALIGN_TOP_MID, 0, kUpdateCardTop);

  if (!CreateFirmwareBrand(card)) {
    return false;
  }

  char version_text[48] = {};
  std::snprintf(version_text, sizeof(version_text), "%s  |  %s",
      kPreviewVersion, kPreviewPackageSize);
  lv_obj_t* version = CreateLabel(card, version_text,
      lv_color_hex(kSecondaryTextColor), Font24());
  if (version == nullptr) {
    return false;
  }
  lv_obj_align(version, LV_ALIGN_TOP_LEFT, kUpdateCardPadding,
      kUpdateVersionTop);

  lv_obj_t* divider =
      CreateDivider(card, card_width - 2 * kUpdateCardPadding);
  if (divider == nullptr) {
    return false;
  }
  lv_obj_set_pos(divider, kUpdateCardPadding, kUpdateDividerTop);

  lv_obj_t* components_title = CreateLabel(card, "Update components",
      lv_color_hex(kPrimaryTextColor), Font28());
  if (components_title == nullptr) {
    return false;
  }
  lv_obj_align(components_title, LV_ALIGN_TOP_LEFT, kUpdateCardPadding,
      kUpdateComponentsTitleTop);

  const int component_width = card_width - 2 * kUpdateCardPadding;
  if (!CreateFirmwareComponentRow(card, kUpdateComponentsTop,
          component_width, icon::kMemory, "Main firmware", "ESP32-P4",
          "v1.0.0 > v1.1.0", 0x3F82F6) ||
      !CreateFirmwareComponentRow(card,
          kUpdateComponentsTop + kUpdateComponentHeight +
              kUpdateComponentGap,
          component_width, icon::kSignalWifi4Bar, "Wireless firmware",
          "ESP32-C6", "v2.12.3 > v2.13.0", 0x8B68F6)) {
    return false;
  }

  lv_obj_t* second_divider = CreateDivider(card, component_width);
  if (second_divider == nullptr) {
    return false;
  }
  lv_obj_set_pos(
      second_divider, kUpdateCardPadding, kUpdateSecondDividerTop);

  lv_obj_t* whats_new_title = CreateLabel(card, "What's new",
      lv_color_hex(kPrimaryTextColor), Font28());
  if (whats_new_title == nullptr) {
    return false;
  }
  lv_obj_align(whats_new_title, LV_ALIGN_TOP_LEFT, kUpdateCardPadding,
      kUpdateWhatsNewTitleTop);

  lv_obj_t* notes = CreateLabel(card,
      "- Improved system stability\n"
      "- Updated wireless firmware\n"
      "- Optimized app experience",
      lv_color_hex(kSecondaryTextColor), Font22());
  if (notes == nullptr) {
    return false;
  }
  lv_obj_set_width(notes, card_width - 2 * kUpdateCardPadding);
  lv_obj_set_style_text_line_space(notes, 12, LV_PART_MAIN);
  lv_obj_align(notes, LV_ALIGN_TOP_LEFT, kUpdateCardPadding,
      kUpdateWhatsNewTop);
  return true;
}

/**
 * @brief 创建固件更新页面可滚动内容区域
 * @param page 固件更新页面
 * @param state 设置页面状态
 * @param width 页面宽度
 * @param height 页面高度
 * @return 创建成功返回 true，否则返回 false
 */
bool CreateFirmwareUpdateBody(
    lv_obj_t* page, SettingsViewState* state, int width, int height) {
  const int body_height = height - kDetailBodyTop - kUpdateBottomAreaHeight;
  if (body_height <= 0) {
    return false;
  }

  lv_obj_t* body = lv_obj_create(page);
  if (body == nullptr) {
    return false;
  }
  MakeTransparent(body);
  lv_obj_set_size(body, width, body_height);
  lv_obj_align(body, LV_ALIGN_TOP_LEFT, 0, kDetailBodyTop);
  lv_obj_set_scroll_dir(body, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(body, LV_SCROLLBAR_MODE_OFF);
  lv_obj_add_flag(body, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(body, LV_OBJ_FLAG_GESTURE_BUBBLE);
  lv_obj_remove_flag(body, LV_OBJ_FLAG_SCROLL_ELASTIC);
  AddEdgeBackSwipeEvents(body, FirmwareUpdateEdgeBackEventCallback, state);

  lv_obj_t* heading = CreateLabel(body, "New version available",
      lv_color_hex(kPrimaryTextColor), Font36());
  if (heading == nullptr) {
    return false;
  }
  lv_obj_set_width(heading, FirmwareUpdateContentWidth(width));
  lv_obj_align(heading, LV_ALIGN_TOP_MID, 0, kUpdateHeadingTop);
  return CreateFirmwareUpdateCard(body, width);
}

/**
 * @brief 创建下载固件按钮
 * @param page 固件更新页面
 * @param width 页面宽度
 * @return 创建成功返回 true，否则返回 false
 */
bool CreateDownloadUpdateButton(lv_obj_t* page, int width) {
  const int content_width = FirmwareUpdateContentWidth(width);
  const int button_width =
      content_width < kUpdateButtonWidth ? content_width : kUpdateButtonWidth;
  lv_obj_t* button = lv_button_create(page);
  if (button == nullptr) {
    return false;
  }
  lv_obj_remove_flag(button, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(button, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
  lv_obj_add_flag(button, LV_OBJ_FLAG_GESTURE_BUBBLE);
  lv_obj_set_size(button, button_width, kUpdateButtonHeight);
  lv_obj_align(button, LV_ALIGN_BOTTOM_MID, 0, -kUpdateButtonBottom);
  lv_obj_set_style_bg_color(
      button, lv_color_hex(kDetailBlueColor), LV_PART_MAIN);
  lv_obj_set_style_bg_color(button,
      lv_color_hex(theme::LightNeutralTheme().action_pressed),
      LV_STATE_PRESSED);
  lv_obj_set_style_bg_opa(button, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_bg_opa(button, LV_OPA_COVER, LV_STATE_PRESSED);
  lv_obj_set_style_border_width(button, 0, LV_PART_MAIN);
  lv_obj_set_style_radius(button, kUpdateButtonHeight / 3, LV_PART_MAIN);
  lv_obj_set_style_shadow_width(button, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(button, 0, LV_PART_MAIN);
  if (!AddPressCancelOnLeave(button)) {
    return false;
  }

  lv_obj_t* label = CreateLabel(
      button, "Download firmware", lv_color_hex(0xFFFFFF), Font28());
  if (label == nullptr) {
    return false;
  }
  lv_obj_center(label);
  // 当前仅提供界面预览，后续再接入 OTA 更新逻辑。
  return true;
}

}  // namespace

/**
 * @brief 关闭固件更新页面
 * @param state 设置页面状态
 * @param animated 是否播放关闭动画
 */
void CloseFirmwareUpdatePage(SettingsViewState* state, bool animated) {
  if (state == nullptr || state->firmware_update_page == nullptr ||
      state->firmware_update_closing) {
    return;
  }

  if (animated &&
      StartSlideRightWindowTransition(state->firmware_update_page,
          state->config.width, kDetailSlideAnimationMs, state,
          FirmwareUpdateCloseCompletedCallback)) {
    state->firmware_update_closing = true;
    return;
  }

  lv_obj_t* page = state->firmware_update_page;
  ClearFirmwareUpdateReferences(state);
  lv_obj_delete(page);
}

/**
 * @brief 从我的设备页面打开固件更新页面
 * @param state 设置页面状态
 * @return 打开成功返回 true，否则返回 false
 */
bool ShowFirmwareUpdatePage(SettingsViewState* state) {
  if (state == nullptr || state->root == nullptr ||
      state->detail_page == nullptr || state->detail_closing) {
    return false;
  }
  if (state->firmware_update_closing) {
    return true;
  }
  if (state->firmware_update_page != nullptr) {
    lv_obj_move_to_index(state->firmware_update_page, -1);
    return true;
  }

  const AppViewConfig& config = state->config;
  lv_obj_t* page = lv_obj_create(state->root);
  if (page == nullptr) {
    return false;
  }
  state->firmware_update_page = page;
  state->firmware_update_closing = false;
  state->firmware_update_swipe = EdgeBackSwipeState();
  lv_obj_add_flag(state->root, kBlockLauncherGestureFlag);
  lv_obj_remove_flag(state->root, LV_OBJ_FLAG_GESTURE_BUBBLE);

  lv_obj_remove_flag(page, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(page, LV_OBJ_FLAG_GESTURE_BUBBLE);
  AddEdgeBackSwipeEvents(page, FirmwareUpdateEdgeBackEventCallback, state);
  lv_obj_set_size(page, config.width, config.height);
  lv_obj_set_pos(page, 0, 0);
  lv_obj_set_style_bg_color(
      page, lv_color_hex(kDetailBackgroundColor), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(page, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_border_width(page, 0, LV_PART_MAIN);
  lv_obj_set_style_radius(page, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(page, 0, LV_PART_MAIN);

  const bool created =
      CreateFirmwareUpdateHeader(page, state, config.width) &&
      CreateFirmwareUpdateBody(
          page, state, config.width, config.height) &&
      CreateDownloadUpdateButton(page, config.width);
  if (!created) {
    CloseFirmwareUpdatePage(state, false);
    return false;
  }

  EnableEdgeBackSwipeEventBubble(page);
  if (!StartSlideLeftWindowTransition(
          page, config.width, kDetailSlideAnimationMs, state, nullptr)) {
    CloseFirmwareUpdatePage(state, false);
    return false;
  }
  return true;
}

}  // namespace lilygo_box::ui
