/*
 * @Description: 设置固件更新界面与组合 OTA 状态交互
 * @Author: LILYGO_L
 * @Date: 2026-07-19 00:00:00
 * @LastEditTime: 2026-07-19 00:00:00
 * @License: GPL 3.0
 */
#include "ui/views/settings/settings_view_internal.h"

#include <cstdio>
#include <cstring>

#include "app/firmware_update_manager.h"
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
constexpr int kUpdateScanGroupHeight = 180;
constexpr int kUpdateScanGroupOffsetY = -100;
constexpr int kUpdateSpinnerSize = 68;
constexpr uint32_t kUpdateCardColor =
    theme::LightNeutralTheme().surface_container_lowest;
constexpr uint32_t kUpdateFeatureColor =
    theme::LightNeutralTheme().surface_container_low;

bool CreateFirmwareUpdateCard(
    lv_obj_t* body, SettingsViewState* state, int width);

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

  if (state->firmware_update_refresh_timer != nullptr) {
    lv_timer_delete(state->firmware_update_refresh_timer);
    state->firmware_update_refresh_timer = nullptr;
  }
  state->firmware_update_page = nullptr;
  state->firmware_update_body = nullptr;
  state->firmware_update_scan_group = nullptr;
  state->firmware_update_scan_message_label = nullptr;
  state->firmware_update_scan_hint_label = nullptr;
  state->firmware_update_heading_label = nullptr;
  state->firmware_update_card = nullptr;
  state->firmware_update_release_label = nullptr;
  state->firmware_update_main_version_label = nullptr;
  state->firmware_update_wireless_version_label = nullptr;
  state->firmware_update_notes_label = nullptr;
  state->firmware_update_download_button = nullptr;
  state->firmware_update_download_button_label = nullptr;
  state->firmware_update_spinner = nullptr;
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
 * @brief 将固件版本格式化为当前版本或升级方向文本
 * @param current_version 当前版本
 * @param target_version 目标版本
 * @param output 输出缓冲区
 * @param output_size 输出缓冲区长度
 */
void FormatFirmwareVersion(const char* current_version,
    const char* target_version, char* output, size_t output_size) {
  const char* current =
      current_version != nullptr && current_version[0] != '\0'
          ? current_version
          : "unknown";
  if (target_version != nullptr && target_version[0] != '\0' &&
      std::strcmp(current, target_version) != 0) {
    std::snprintf(output, output_size, "v%s > v%s", current, target_version);
    return;
  }
  std::snprintf(output, output_size, "v%s", current);
}

/**
 * @brief 根据后台快照刷新固件更新页面
 * @param state 设置页面状态
 */
void RefreshFirmwareUpdateView(SettingsViewState* state) {
  if (state == nullptr || state->firmware_update_page == nullptr ||
      state->firmware_update_body == nullptr ||
      state->firmware_update_scan_group == nullptr ||
      state->firmware_update_scan_message_label == nullptr ||
      state->firmware_update_scan_hint_label == nullptr ||
      state->firmware_update_heading_label == nullptr ||
      state->firmware_update_download_button == nullptr ||
      state->firmware_update_download_button_label == nullptr ||
      state->firmware_update_spinner == nullptr) {
    return;
  }

  const app::FirmwareUpdateSnapshot snapshot =
      app::GetFirmwareUpdateSnapshot();
  bool card_ready = snapshot.manifest_available;
  if (card_ready && state->firmware_update_card == nullptr) {
    card_ready = CreateFirmwareUpdateCard(state->firmware_update_body,
        state, state->config.width);
  }
  card_ready = card_ready && state->firmware_update_card != nullptr &&
      state->firmware_update_release_label != nullptr &&
      state->firmware_update_main_version_label != nullptr &&
      state->firmware_update_wireless_version_label != nullptr &&
      state->firmware_update_notes_label != nullptr;

  if (card_ready) {
    lv_obj_add_flag(
        state->firmware_update_scan_group, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(
        state->firmware_update_heading_label, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(state->firmware_update_card, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(state->firmware_update_heading_label,
        snapshot.message[0] == '\0' ? "Firmware update" : snapshot.message);

    char release_text[64] = {};
    std::snprintf(release_text, sizeof(release_text), "%s  |  %s",
        snapshot.release_version, snapshot.package_size);
    lv_label_set_text(state->firmware_update_release_label, release_text);

    char version_text[80] = {};
    FormatFirmwareVersion(snapshot.main_current_version,
        snapshot.main_target_version, version_text, sizeof(version_text));
    lv_label_set_text(
        state->firmware_update_main_version_label, version_text);
    FormatFirmwareVersion(snapshot.wireless_current_version,
        snapshot.wireless_target_version, version_text, sizeof(version_text));
    lv_label_set_text(
        state->firmware_update_wireless_version_label, version_text);

    char notes_text[420] = {};
    if (snapshot.note_count == 0) {
      std::snprintf(notes_text, sizeof(notes_text),
          "No release notes were provided.");
    } else {
      size_t used = 0;
      for (size_t index = 0; index < snapshot.note_count; ++index) {
        const int written = std::snprintf(notes_text + used,
            sizeof(notes_text) - used, "%s- %s",
            index == 0 ? "" : "\n", snapshot.notes[index]);
        if (written < 0 ||
            static_cast<size_t>(written) >= sizeof(notes_text) - used) {
          break;
        }
        used += static_cast<size_t>(written);
      }
    }
    lv_label_set_text(state->firmware_update_notes_label, notes_text);
  } else {
    lv_obj_remove_flag(
        state->firmware_update_scan_group, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(
        state->firmware_update_heading_label, LV_OBJ_FLAG_HIDDEN);
    if (state->firmware_update_card != nullptr) {
      lv_obj_add_flag(state->firmware_update_card, LV_OBJ_FLAG_HIDDEN);
    }
    const bool scanning =
        snapshot.stage == app::FirmwareUpdateStage::kChecking ||
        snapshot.stage == app::FirmwareUpdateStage::kWaitingForNetwork;
    if (scanning) {
      lv_obj_remove_flag(
          state->firmware_update_spinner, LV_OBJ_FLAG_HIDDEN);
      lv_label_set_text(state->firmware_update_scan_message_label,
          "Checking for updates...");
      lv_label_set_text(state->firmware_update_scan_hint_label,
          "Downloading update information from GitHub");
    } else {
      lv_obj_add_flag(
          state->firmware_update_spinner, LV_OBJ_FLAG_HIDDEN);
      lv_label_set_text(state->firmware_update_scan_message_label,
          snapshot.message[0] == '\0'
              ? "Unable to check for updates"
              : snapshot.message);
      const bool network_error =
          std::strstr(snapshot.message, "Wi-Fi") != nullptr;
      lv_label_set_text(state->firmware_update_scan_hint_label,
          !snapshot.device_supported
              ? "This device has no matching firmware package"
              : network_error
                  ? "Turn on Wi-Fi, connect to a network, and try again"
                  : "Check the release package and try again");
    }
  }

  char button_text[64] = {};
  bool button_enabled = !snapshot.busy && snapshot.device_supported;
  switch (snapshot.stage) {
    case app::FirmwareUpdateStage::kWaitingForNetwork:
      std::snprintf(button_text, sizeof(button_text), "Waiting for Wi-Fi");
      break;
    case app::FirmwareUpdateStage::kChecking:
      std::snprintf(button_text, sizeof(button_text), "Checking...");
      break;
    case app::FirmwareUpdateStage::kUpdateAvailable:
      std::snprintf(button_text, sizeof(button_text), "Download firmware");
      break;
    case app::FirmwareUpdateStage::kUpToDate:
      std::snprintf(button_text, sizeof(button_text), "Check again");
      break;
    case app::FirmwareUpdateStage::kDownloadingWireless:
      std::snprintf(button_text, sizeof(button_text), "Downloading C6  %d%%",
          snapshot.progress_percent);
      break;
    case app::FirmwareUpdateStage::kInstallingWireless:
      std::snprintf(button_text, sizeof(button_text), "Installing C6  %d%%",
          snapshot.progress_percent);
      break;
    case app::FirmwareUpdateStage::kDownloadingMain:
      std::snprintf(button_text, sizeof(button_text), "Downloading P4  %d%%",
          snapshot.progress_percent);
      break;
    case app::FirmwareUpdateStage::kRestarting:
      std::snprintf(button_text, sizeof(button_text), "Restarting...");
      break;
    case app::FirmwareUpdateStage::kFailed:
      std::snprintf(button_text, sizeof(button_text),
          snapshot.manifest_available && snapshot.update_available
              ? "Retry update"
              : "Try again");
      break;
    case app::FirmwareUpdateStage::kIdle:
    default:
      std::snprintf(button_text, sizeof(button_text), "Check for updates");
      break;
  }
  if (!snapshot.device_supported) {
    std::snprintf(button_text, sizeof(button_text), "Unavailable");
  }
  lv_label_set_text(
      state->firmware_update_download_button_label, button_text);
  if (button_enabled) {
    lv_obj_remove_state(
        state->firmware_update_download_button, LV_STATE_DISABLED);
  } else {
    lv_obj_add_state(
        state->firmware_update_download_button, LV_STATE_DISABLED);
  }
}

/**
 * @brief 定时读取后台固件更新状态并刷新界面
 * @param timer LVGL 定时器
 */
void FirmwareUpdateRefreshTimerCallback(lv_timer_t* timer) {
  RefreshFirmwareUpdateView(
      static_cast<SettingsViewState*>(lv_timer_get_user_data(timer)));
}

/**
 * @brief 处理固件检查、下载和失败重试按钮点击
 * @param event LVGL 事件对象
 */
void FirmwareUpdateDownloadClickedEventCallback(lv_event_t* event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
    return;
  }
  auto* state = static_cast<SettingsViewState*>(
      lv_event_get_user_data(event));
  const app::FirmwareUpdateSnapshot snapshot =
      app::GetFirmwareUpdateSnapshot();
  if (snapshot.busy || !snapshot.device_supported) {
    return;
  }
  if (snapshot.manifest_available && snapshot.update_available) {
    app::StartFirmwareUpdate();
  } else {
    app::RequestFirmwareUpdateCheck();
  }
  RefreshFirmwareUpdateView(state);
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
 * @param version_label_output 版本标签输出地址
 * @return 创建成功返回 true，否则返回 false
 */
bool CreateFirmwareComponentRow(lv_obj_t* card, int y, int width,
    const char* symbol, const char* title, const char* chip,
    const char* version, uint32_t color,
    lv_obj_t** version_label_output) {
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
  if (version_label_output != nullptr) {
    *version_label_output = component_version;
  }
  return true;
}

/**
 * @brief 创建固件更新版本卡片
 * @param body 页面可滚动内容区域
 * @param state 设置页面状态
 * @param width 页面宽度
 * @return 创建成功返回 true，否则返回 false
 */
bool CreateFirmwareUpdateCard(
    lv_obj_t* body, SettingsViewState* state, int width) {
  const int card_width = FirmwareUpdateContentWidth(width);
  lv_obj_t* card = CreateBox(body, card_width, kUpdateCardHeight,
      kUpdateCardColor, LV_OPA_COVER, kDetailCardRadius);
  if (card == nullptr) {
    return false;
  }
  state->firmware_update_card = card;
  const auto discard_card = [state, card]() {
    lv_obj_delete(card);
    state->firmware_update_card = nullptr;
    state->firmware_update_release_label = nullptr;
    state->firmware_update_main_version_label = nullptr;
    state->firmware_update_wireless_version_label = nullptr;
    state->firmware_update_notes_label = nullptr;
    return false;
  };
  lv_obj_remove_flag(card, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_align(card, LV_ALIGN_TOP_MID, 0, kUpdateCardTop);

  if (!CreateFirmwareBrand(card)) {
    return discard_card();
  }

  lv_obj_t* version = CreateLabel(card, "Checking...",
      lv_color_hex(kSecondaryTextColor), Font24());
  if (version == nullptr) {
    return discard_card();
  }
  state->firmware_update_release_label = version;
  lv_obj_align(version, LV_ALIGN_TOP_LEFT, kUpdateCardPadding,
      kUpdateVersionTop);

  lv_obj_t* divider =
      CreateDivider(card, card_width - 2 * kUpdateCardPadding);
  if (divider == nullptr) {
    return discard_card();
  }
  lv_obj_set_pos(divider, kUpdateCardPadding, kUpdateDividerTop);

  lv_obj_t* components_title = CreateLabel(card, "Update components",
      lv_color_hex(kPrimaryTextColor), Font28());
  if (components_title == nullptr) {
    return discard_card();
  }
  lv_obj_align(components_title, LV_ALIGN_TOP_LEFT, kUpdateCardPadding,
      kUpdateComponentsTitleTop);

  const int component_width = card_width - 2 * kUpdateCardPadding;
  if (!CreateFirmwareComponentRow(card, kUpdateComponentsTop,
          component_width, icon::kMemory, "Main firmware", "ESP32-P4",
          "vunknown", 0x3F82F6,
          &state->firmware_update_main_version_label) ||
      !CreateFirmwareComponentRow(card,
          kUpdateComponentsTop + kUpdateComponentHeight +
              kUpdateComponentGap,
          component_width, icon::kSignalWifi4Bar, "Wireless firmware",
          "ESP32-C6", "vunknown", 0x8B68F6,
          &state->firmware_update_wireless_version_label)) {
    return discard_card();
  }

  lv_obj_t* second_divider = CreateDivider(card, component_width);
  if (second_divider == nullptr) {
    return discard_card();
  }
  lv_obj_set_pos(
      second_divider, kUpdateCardPadding, kUpdateSecondDividerTop);

  lv_obj_t* whats_new_title = CreateLabel(card, "What's new",
      lv_color_hex(kPrimaryTextColor), Font28());
  if (whats_new_title == nullptr) {
    return discard_card();
  }
  lv_obj_align(whats_new_title, LV_ALIGN_TOP_LEFT, kUpdateCardPadding,
      kUpdateWhatsNewTitleTop);

  lv_obj_t* notes = CreateLabel(card,
      "Release notes will appear after checking.",
      lv_color_hex(kSecondaryTextColor), Font22());
  if (notes == nullptr) {
    return discard_card();
  }
  lv_obj_set_width(notes, card_width - 2 * kUpdateCardPadding);
  lv_obj_set_style_text_line_space(notes, 12, LV_PART_MAIN);
  lv_obj_align(notes, LV_ALIGN_TOP_LEFT, kUpdateCardPadding,
      kUpdateWhatsNewTop);
  state->firmware_update_notes_label = notes;
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
  state->firmware_update_body = body;
  MakeTransparent(body);
  lv_obj_set_size(body, width, body_height);
  lv_obj_align(body, LV_ALIGN_TOP_LEFT, 0, kDetailBodyTop);
  lv_obj_set_scroll_dir(body, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(body, LV_SCROLLBAR_MODE_OFF);
  lv_obj_add_flag(body, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(body, LV_OBJ_FLAG_GESTURE_BUBBLE);
  lv_obj_remove_flag(body, LV_OBJ_FLAG_SCROLL_ELASTIC);
  AddEdgeBackSwipeEvents(body, FirmwareUpdateEdgeBackEventCallback, state);

  const int content_width = FirmwareUpdateContentWidth(width);
  const int content_left = (width - content_width) / 2;
  lv_obj_t* heading = CreateLabel(body, "New version available",
      lv_color_hex(kPrimaryTextColor), Font36());
  if (heading == nullptr) {
    return false;
  }
  lv_obj_set_width(heading, content_width);
  lv_obj_set_pos(heading, content_left, kUpdateHeadingTop);
  lv_obj_add_flag(heading, LV_OBJ_FLAG_HIDDEN);
  state->firmware_update_heading_label = heading;

  lv_obj_t* scan_group = lv_obj_create(body);
  if (scan_group == nullptr) {
    return false;
  }
  MakeTransparent(scan_group);
  lv_obj_remove_flag(scan_group, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(scan_group, width, kUpdateScanGroupHeight);
  lv_obj_align(
      scan_group, LV_ALIGN_CENTER, 0, kUpdateScanGroupOffsetY);
  state->firmware_update_scan_group = scan_group;

  lv_obj_t* spinner = lv_spinner_create(scan_group);
  if (spinner == nullptr) {
    return false;
  }
  lv_obj_set_size(spinner, kUpdateSpinnerSize, kUpdateSpinnerSize);
  lv_spinner_set_anim_params(spinner, 850, 250);
  lv_obj_set_style_arc_color(spinner,
      lv_color_hex(theme::LightNeutralTheme().surface_container_high),
      LV_PART_MAIN);
  lv_obj_set_style_arc_color(
      spinner, lv_color_hex(kDetailBlueColor), LV_PART_INDICATOR);
  lv_obj_set_style_arc_width(spinner, 7, LV_PART_MAIN);
  lv_obj_set_style_arc_width(spinner, 7, LV_PART_INDICATOR);
  lv_obj_align(spinner, LV_ALIGN_TOP_MID, 0, 0);
  state->firmware_update_spinner = spinner;

  lv_obj_t* message = CreateLabel(scan_group, "Checking for updates...",
      lv_color_hex(kPrimaryTextColor), Font28());
  lv_obj_t* hint = CreateLabel(scan_group,
      "Downloading update information from GitHub",
      lv_color_hex(kSecondaryTextColor), Font22());
  if (message == nullptr || hint == nullptr) {
    return false;
  }
  lv_obj_align(message, LV_ALIGN_TOP_MID, 0, 96);
  lv_obj_align(hint, LV_ALIGN_TOP_MID, 0, 138);
  state->firmware_update_scan_message_label = message;
  state->firmware_update_scan_hint_label = hint;
  return true;
}

/**
 * @brief 创建下载固件按钮
 * @param page 固件更新页面
 * @param state 设置页面状态
 * @param width 页面宽度
 * @return 创建成功返回 true，否则返回 false
 */
bool CreateDownloadUpdateButton(
    lv_obj_t* page, SettingsViewState* state, int width) {
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
  lv_obj_set_style_bg_color(button,
      lv_color_hex(theme::LightNeutralTheme().outline), LV_STATE_DISABLED);
  lv_obj_set_style_bg_opa(button, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_bg_opa(button, LV_OPA_COVER, LV_STATE_PRESSED);
  lv_obj_set_style_border_width(button, 0, LV_PART_MAIN);
  lv_obj_set_style_radius(button, kUpdateButtonHeight / 3, LV_PART_MAIN);
  lv_obj_set_style_shadow_width(button, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(button, 0, LV_PART_MAIN);
  if (!AddPressCancelOnLeave(button)) {
    return false;
  }
  lv_obj_add_event_cb(button, FirmwareUpdateDownloadClickedEventCallback,
      LV_EVENT_CLICKED, state);

  lv_obj_t* label = CreateLabel(
      button, "Download firmware", lv_color_hex(0xFFFFFF), Font28());
  if (label == nullptr) {
    return false;
  }
  lv_obj_center(label);
  state->firmware_update_download_button = button;
  state->firmware_update_download_button_label = label;
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
      CreateDownloadUpdateButton(page, state, config.width);
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
  state->firmware_update_refresh_timer =
      lv_timer_create(FirmwareUpdateRefreshTimerCallback, 250, state);
  if (state->firmware_update_refresh_timer == nullptr) {
    CloseFirmwareUpdatePage(state, false);
    return false;
  }
  app::RequestFirmwareUpdateCheck();
  RefreshFirmwareUpdateView(state);
  return true;
}

}  // namespace lilygo_box::ui
