/*
 * @Description: 设置固件更新界面与组合 OTA 状态交互
 * @Author: LILYGO_L
 * @Date: 2026-07-19 00:00:00
 * @LastEditTime: 2026-07-21 09:55:44
 * @License: GPL 3.0
 */
#include "ui/views/settings/settings_view_internal.h"

#include <algorithm>
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

constexpr int kUpdateBottomAreaHeight = 204;
constexpr int kUpdateHeadingTop = 14;
constexpr int kUpdateCardTop = 82;
constexpr int kUpdateCardHeight = 690;
constexpr int kUpdateCardSide = 26;
constexpr int kUpdateCardPadding = 34;
constexpr int kUpdateBrandTop = 34;
constexpr int kUpdateBrandIconSize = 58;
constexpr int kUpdateBrandGap = 14;
constexpr int kUpdateVersionTop = 112;
constexpr int kUpdateReleaseTimeTop = kUpdateVersionTop + 2;
constexpr int kUpdateChannelTop = kUpdateReleaseTimeTop - 28;
constexpr int kUpdateReleaseMetadataWidth = 190;
constexpr int kUpdateReleaseMetadataGap = 12;
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
constexpr int kUpdateActionButtonHeight = 48;
constexpr int kUpdateActionButtonGap = 12;
constexpr int kUpdateMaxContentWidth = 516;
constexpr int kUpdateScanGroupHeight = 352;
constexpr int kUpdateScanGroupTop = 150;
constexpr int kUpdateSpinnerSize = 68;
constexpr int kUpdateStatusBrandGap = 10;
constexpr int kUpdateStatusLogButtonTop = 70;
constexpr int kUpdateStatusLogButtonHeight = 46;
constexpr int kUpdateStatusLogButtonPaddingX = 12;
constexpr int kUpdateStatusLogButtonContentGap = 4;
constexpr int kUpdateStatusSpinnerTop = 166;
constexpr int kUpdateStatusPrimaryTop = kUpdateStatusSpinnerTop + 96;
constexpr int kUpdateStatusHintTop = kUpdateStatusSpinnerTop + 138;
constexpr int kUpdatePageIndicatorWidth = 48;
constexpr int kUpdatePageIndicatorHeight = 18;
constexpr int kUpdatePageIndicatorBottom = 116;
constexpr int kUpdatePageIndicatorStackedBottom =
    kUpdateButtonBottom + 2 * kUpdateButtonHeight +
    kUpdateActionButtonGap + 16;
constexpr int kUpdatePageDotSize = 12;
constexpr uint32_t kUpdateCardColor =
    theme::LightNeutralTheme().surface_container_lowest;
constexpr uint32_t kUpdateFeatureColor =
    theme::LightNeutralTheme().surface_container_low;
constexpr uint32_t kUpdateStableChannelColor =
    theme::LightNeutralTheme().action;
constexpr uint32_t kUpdateBetaChannelColor = 0xF5A623;
constexpr uint32_t kUpdateDevChannelColor = 0xBA1A1A;
constexpr uint32_t kUpdateCancelButtonColor = kDetailOptionPressedColor;
constexpr uint32_t kUpdateCancelButtonPressedColor =
    theme::LightNeutralTheme().button_secondary_pressed;

/**
 * @brief 创建固件更新版本卡片
 * @param body 页面可滚动内容区域
 * @param state 设置页面状态
 * @param width 页面宽度
 * @return 创建成功返回 true，否则返回 false
 */
bool CreateFirmwareUpdateCard(
    lv_obj_t* body, SettingsViewState* state, int width);

/**
 * @brief 打开固件版本日志二级页面
 * @param state 设置页面状态
 * @return 打开成功返回 true，否则返回 false
 */
bool ShowFirmwareUpdateLogPage(SettingsViewState* state);

/**
 * @brief 关闭固件版本日志二级页面
 * @param state 设置页面状态
 * @param animated 是否播放关闭动画
 */
void CloseFirmwareUpdateLogPage(
    SettingsViewState* state, bool animated);

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
  state->firmware_update_current_page = nullptr;
  state->firmware_update_new_page = nullptr;
  state->firmware_update_scan_group = nullptr;
  state->firmware_update_scan_message_label = nullptr;
  state->firmware_update_scan_hint_label = nullptr;
  state->firmware_update_status_version_label = nullptr;
  state->firmware_update_status_log_button = nullptr;
  state->firmware_update_status_log_button_label = nullptr;
  state->firmware_update_page_indicator = nullptr;
  state->firmware_update_current_page_dot = nullptr;
  state->firmware_update_new_page_dot = nullptr;
  state->firmware_update_heading_label = nullptr;
  state->firmware_update_card = nullptr;
  state->firmware_update_release_label = nullptr;
  state->firmware_update_channel_label = nullptr;
  state->firmware_update_release_time_label = nullptr;
  state->firmware_update_components_title = nullptr;
  state->firmware_update_main_row = nullptr;
  state->firmware_update_main_chip_label = nullptr;
  state->firmware_update_main_version_label = nullptr;
  state->firmware_update_wireless_row = nullptr;
  state->firmware_update_wireless_chip_label = nullptr;
  state->firmware_update_wireless_version_label = nullptr;
  state->firmware_update_components_divider = nullptr;
  state->firmware_update_notes_title = nullptr;
  state->firmware_update_notes_label = nullptr;
  state->firmware_update_download_button = nullptr;
  state->firmware_update_progress_fill = nullptr;
  state->firmware_update_download_button_label = nullptr;
  state->firmware_update_pause_button = nullptr;
  state->firmware_update_pause_button_label = nullptr;
  state->firmware_update_cancel_button = nullptr;
  state->firmware_update_cancel_button_label = nullptr;
  state->firmware_update_spinner = nullptr;
  state->firmware_update_closing = false;
  state->firmware_update_page_index = 0;
  state->firmware_update_auto_show_new_page = false;
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
  if (app::GetFirmwareUpdateSnapshot().stage ==
      app::FirmwareUpdateStage::kReadyToInstall) {
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
  const bool install_choice_required =
      app::GetFirmwareUpdateSnapshot().stage ==
      app::FirmwareUpdateStage::kReadyToInstall;
  if (state == nullptr || state->firmware_update_page == nullptr ||
      state->firmware_update_closing || install_choice_required ||
      state->config.screen == nullptr ||
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
    std::snprintf(
        output, output_size, "v%s -> v%s", current, target_version);
    return;
  }
  std::snprintf(output, output_size, "v%s", current);
}

/**
 * @brief 将清单发布时间格式化为卡片显示文本
 * @param release_time 带时区的 ISO 8601 发布时间
 * @param output 输出缓冲区
 * @param output_size 输出缓冲区长度
 */
void FormatReleaseTimeForDisplay(
    const char* release_time, char* output, size_t output_size) {
  if (output == nullptr || output_size == 0) {
    return;
  }
  if (release_time == nullptr || std::strlen(release_time) < 16 ||
      release_time[10] != 'T') {
    std::snprintf(output, output_size, "Unavailable");
    return;
  }
  std::snprintf(output, output_size, "%.10s %.5s",
      release_time, release_time + 11);
}

/**
 * @brief 设置固件发布频道标签文字和颜色
 * @param label 发布频道标签
 * @param channel manifest 中的发布频道
 */
void SetReleaseChannelLabel(lv_obj_t* label, const char* channel) {
  if (label == nullptr) {
    return;
  }
  const char* channel_text = "Stable";
  uint32_t channel_color = kUpdateStableChannelColor;
  if (channel != nullptr && std::strcmp(channel, "beta") == 0) {
    channel_text = "Beta";
    channel_color = kUpdateBetaChannelColor;
  } else if (channel != nullptr && std::strcmp(channel, "dev") == 0) {
    channel_text = "Development";
    channel_color = kUpdateDevChannelColor;
  }
  lv_label_set_text(label, channel_text);
  lv_obj_set_style_text_color(
      label, lv_color_hex(channel_color), LV_PART_MAIN);
}

/**
 * @brief 将芯片型号和对应固件大小组合为组件说明
 * @param chip 芯片型号
 * @param firmware_size 固件大小文本
 * @param output 输出缓冲区
 * @param output_size 输出缓冲区长度
 */
void FormatFirmwareChipText(const char* chip, const char* firmware_size,
    char* output, size_t output_size) {
  if (output == nullptr || output_size == 0) {
    return;
  }
  const char* chip_text =
      chip != nullptr && chip[0] != '\0' ? chip : "Unknown chip";
  if (firmware_size == nullptr || firmware_size[0] == '\0') {
    std::snprintf(output, output_size, "%s", chip_text);
    return;
  }
  std::snprintf(
      output, output_size, "%s | %s", chip_text, firmware_size);
}

void SetFirmwareObjectVisible(lv_obj_t* object, bool visible) {
  if (object == nullptr) {
    return;
  }
  if (visible) {
    lv_obj_remove_flag(object, LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_obj_add_flag(object, LV_OBJ_FLAG_HIDDEN);
  }
}

/**
 * @brief 将固件版本日志格式化为多行列表
 * @param notes 日志条目数组
 * @param note_count 日志条目数量
 * @param empty_text 没有日志时显示的文本
 * @param output 输出缓冲区
 * @param output_size 输出缓冲区长度
 */
void FormatFirmwareNotesText(
    const char notes[][128], size_t note_count, const char* empty_text,
    char* output, size_t output_size) {
  if (output == nullptr || output_size == 0) {
    return;
  }
  if (note_count == 0) {
    std::snprintf(output, output_size, "%s", empty_text);
    return;
  }
  size_t used = 0;
  for (size_t index = 0; index < note_count; ++index) {
    const int written = std::snprintf(output + used, output_size - used,
        "%s- %s", index == 0 ? "" : "\n", notes[index]);
    if (written < 0 ||
        static_cast<size_t>(written) >= output_size - used) {
      break;
    }
    used += static_cast<size_t>(written);
  }
}

/**
 * @brief 按换行后的更新日志实际高度调整固件卡片
 * @param card 固件信息卡片
 * @param notes_label 更新日志标签
 * @param notes_top 更新日志标签顶部坐标
 */
void FitFirmwareCardToNotes(
    lv_obj_t* card, lv_obj_t* notes_label, int notes_top) {
  if (card == nullptr || notes_label == nullptr) {
    return;
  }
  lv_obj_set_height(notes_label, LV_SIZE_CONTENT);
  lv_obj_update_layout(notes_label);
  const int notes_height = std::max(
      1, static_cast<int>(lv_obj_get_height(notes_label)));
  lv_obj_set_height(
      card, notes_top + notes_height + kUpdateCardPadding);
}

void UpdateFirmwareComponentLayout(SettingsViewState* state,
    const app::FirmwareUpdateSnapshot& snapshot) {
  if (state == nullptr || state->firmware_update_card == nullptr) {
    return;
  }
  const bool show_main = snapshot.main_update_available;
  const bool show_wireless = snapshot.wireless_update_available;
  const int component_count = static_cast<int>(show_main) +
                              static_cast<int>(show_wireless);
  SetFirmwareObjectVisible(
      state->firmware_update_components_title, component_count > 0);
  SetFirmwareObjectVisible(state->firmware_update_main_row, show_main);
  SetFirmwareObjectVisible(
      state->firmware_update_wireless_row, show_wireless);
  SetFirmwareObjectVisible(state->firmware_update_components_divider,
      component_count > 0);

  int next_y = kUpdateComponentsTop;
  if (show_main) {
    lv_obj_set_y(state->firmware_update_main_row, next_y);
    next_y += kUpdateComponentHeight + kUpdateComponentGap;
  }
  if (show_wireless) {
    lv_obj_set_y(state->firmware_update_wireless_row, next_y);
    next_y += kUpdateComponentHeight + kUpdateComponentGap;
  }
  if (component_count == 0) {
    lv_obj_set_y(
        state->firmware_update_notes_title, kUpdateComponentsTitleTop);
    lv_obj_set_y(state->firmware_update_notes_label, kUpdateComponentsTop);
    FitFirmwareCardToNotes(state->firmware_update_card,
        state->firmware_update_notes_label, kUpdateComponentsTop);
    return;
  }

  lv_obj_set_y(state->firmware_update_components_divider, next_y);
  lv_obj_set_y(state->firmware_update_notes_title, next_y + 34);
  const int notes_top = next_y + 80;
  lv_obj_set_y(state->firmware_update_notes_label, notes_top);
  FitFirmwareCardToNotes(state->firmware_update_card,
      state->firmware_update_notes_label, notes_top);
}

/**
 * @brief 根据后台快照刷新固件更新页面
 * @param state 设置页面状态
 */
void RefreshFirmwareUpdateView(SettingsViewState* state) {
  if (state == nullptr || state->firmware_update_page == nullptr ||
      state->firmware_update_body == nullptr ||
      state->firmware_update_current_page == nullptr ||
      state->firmware_update_new_page == nullptr ||
      state->firmware_update_scan_group == nullptr ||
      state->firmware_update_scan_message_label == nullptr ||
      state->firmware_update_scan_hint_label == nullptr ||
      state->firmware_update_status_version_label == nullptr ||
      state->firmware_update_status_log_button == nullptr ||
      state->firmware_update_status_log_button_label == nullptr ||
      state->firmware_update_page_indicator == nullptr ||
      state->firmware_update_current_page_dot == nullptr ||
      state->firmware_update_new_page_dot == nullptr ||
      state->firmware_update_heading_label == nullptr ||
      state->firmware_update_download_button == nullptr ||
      state->firmware_update_progress_fill == nullptr ||
      state->firmware_update_download_button_label == nullptr ||
      state->firmware_update_pause_button == nullptr ||
      state->firmware_update_pause_button_label == nullptr ||
      state->firmware_update_cancel_button == nullptr ||
      state->firmware_update_cancel_button_label == nullptr ||
      state->firmware_update_spinner == nullptr) {
    return;
  }

  const app::FirmwareUpdateSnapshot snapshot =
      app::GetFirmwareUpdateSnapshot();
  const bool new_version_available =
      snapshot.manifest_available && snapshot.update_available;
  const bool scanning =
      snapshot.stage == app::FirmwareUpdateStage::kChecking ||
      snapshot.stage == app::FirmwareUpdateStage::kWaitingForNetwork;
  const bool manual_update_required = snapshot.manual_update_required;
  const bool new_page_was_hidden = lv_obj_has_flag(
      state->firmware_update_new_page, LV_OBJ_FLAG_HIDDEN);
  SetFirmwareObjectVisible(
      state->firmware_update_new_page, new_version_available);
  SetFirmwareObjectVisible(
      state->firmware_update_page_indicator, new_version_available);
  if (!new_version_available && state->firmware_update_page_index != 0) {
    state->firmware_update_page_index = 0;
    lv_obj_scroll_to_x(state->firmware_update_body, 0, LV_ANIM_OFF);
  }
  if (new_page_was_hidden == new_version_available) {
    lv_obj_update_snap(state->firmware_update_body, LV_ANIM_OFF);
  }

  bool card_ready = new_version_available;
  if (card_ready && state->firmware_update_card == nullptr) {
    card_ready = CreateFirmwareUpdateCard(state->firmware_update_new_page,
        state, state->config.width);
  }
  card_ready = card_ready && state->firmware_update_card != nullptr &&
      state->firmware_update_release_label != nullptr &&
      state->firmware_update_channel_label != nullptr &&
      state->firmware_update_release_time_label != nullptr &&
      state->firmware_update_main_chip_label != nullptr &&
      state->firmware_update_main_version_label != nullptr &&
      state->firmware_update_wireless_chip_label != nullptr &&
      state->firmware_update_wireless_version_label != nullptr &&
      state->firmware_update_notes_title != nullptr &&
      state->firmware_update_notes_label != nullptr;

  if (card_ready) {
    lv_obj_remove_flag(
        state->firmware_update_heading_label, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(state->firmware_update_card, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(
        state->firmware_update_heading_label, "New version available");

    char release_text[64] = {};
    std::snprintf(release_text, sizeof(release_text), "%s  |  %s",
        snapshot.release_version, snapshot.package_size);
    lv_label_set_text(state->firmware_update_release_label, release_text);
    SetReleaseChannelLabel(state->firmware_update_channel_label,
        snapshot.release_channel);
    char release_time_text[48] = {};
    FormatReleaseTimeForDisplay(snapshot.release_time,
        release_time_text, sizeof(release_time_text));
    lv_label_set_text(
        state->firmware_update_release_time_label, release_time_text);

    char version_text[80] = {};
    FormatFirmwareVersion(snapshot.main_current_version,
        snapshot.main_target_version, version_text, sizeof(version_text));
    lv_label_set_text(
        state->firmware_update_main_version_label, version_text);
    FormatFirmwareVersion(snapshot.wireless_current_version,
        snapshot.wireless_target_version, version_text, sizeof(version_text));
    lv_label_set_text(
        state->firmware_update_wireless_version_label, version_text);
    char chip_text[48] = {};
    FormatFirmwareChipText(
        "ESP32-P4", snapshot.main_size, chip_text, sizeof(chip_text));
    lv_label_set_text(
        state->firmware_update_main_chip_label, chip_text);
    FormatFirmwareChipText("ESP32-C6", snapshot.wireless_size,
        chip_text, sizeof(chip_text));
    lv_label_set_text(
        state->firmware_update_wireless_chip_label, chip_text);
    lv_label_set_text(state->firmware_update_notes_title, "What's new");

    char notes_text[420] = {};
    FormatFirmwareNotesText(snapshot.notes, snapshot.note_count,
        "No release notes were provided.", notes_text, sizeof(notes_text));
    lv_label_set_text(state->firmware_update_notes_label, notes_text);
    UpdateFirmwareComponentLayout(state, snapshot);
  } else {
    lv_obj_add_flag(
        state->firmware_update_heading_label, LV_OBJ_FLAG_HIDDEN);
    if (state->firmware_update_card != nullptr) {
      lv_obj_add_flag(state->firmware_update_card, LV_OBJ_FLAG_HIDDEN);
    }
  }

  if (state->firmware_update_auto_show_new_page && !scanning) {
    if (snapshot.stage == app::FirmwareUpdateStage::kUpdateAvailable &&
        card_ready) {
      state->firmware_update_auto_show_new_page = false;
      state->firmware_update_page_index = 1;
      lv_obj_update_snap(state->firmware_update_body, LV_ANIM_OFF);
      lv_obj_scroll_to_x(
          state->firmware_update_body, state->config.width, LV_ANIM_ON);
    } else if (snapshot.stage == app::FirmwareUpdateStage::kUpToDate ||
               snapshot.stage == app::FirmwareUpdateStage::kFailed ||
               !snapshot.device_supported) {
      state->firmware_update_auto_show_new_page = false;
    }
  }
  lv_obj_set_style_bg_opa(state->firmware_update_current_page_dot,
      state->firmware_update_page_index == 0 ? 240 : 110, LV_PART_MAIN);
  lv_obj_set_style_bg_opa(state->firmware_update_new_page_dot,
      state->firmware_update_page_index == 0 ? 110 : 240, LV_PART_MAIN);

  char current_version[64] = {};
  FormatFirmwareVersion(snapshot.main_current_version, nullptr,
      current_version, sizeof(current_version));
  lv_label_set_text(
      state->firmware_update_status_version_label, current_version);

  SetFirmwareObjectVisible(state->firmware_update_spinner, scanning);
  SetFirmwareObjectVisible(
      state->firmware_update_status_log_button, true);
  SetFirmwareObjectVisible(
      state->firmware_update_scan_message_label, true);
  SetFirmwareObjectVisible(
      state->firmware_update_scan_hint_label, true);
  if (scanning) {
    lv_label_set_text(state->firmware_update_scan_message_label,
        "Checking for updates...");
    lv_label_set_text(state->firmware_update_scan_hint_label,
        "Downloading update information");
  } else if (new_version_available) {
    lv_label_set_text(state->firmware_update_scan_message_label,
        "New version available");
    lv_label_set_text(state->firmware_update_scan_hint_label,
        "Swipe left to view the update details");
  } else if (snapshot.stage == app::FirmwareUpdateStage::kUpToDate) {
    lv_label_set_text(state->firmware_update_scan_message_label,
        "Firmware is up to date");
    lv_label_set_text(state->firmware_update_scan_hint_label,
        "You are using the latest available version");
  } else {
    lv_label_set_text(state->firmware_update_scan_message_label,
        snapshot.message[0] == '\0'
            ? "Unable to check for updates"
            : snapshot.message);
    const bool network_error =
        std::strstr(snapshot.message, "Wi-Fi") != nullptr;
    const char* failure_hint = "Check the release package and try again";
    if (!snapshot.device_supported) {
      failure_hint = "This device has no matching firmware package";
    } else if (network_error) {
      failure_hint = "Connect to Wi-Fi and try again";
    } else if (manual_update_required) {
      failure_hint = "This updater does not support the manifest format";
    }
    lv_label_set_text(
        state->firmware_update_scan_hint_label, failure_hint);
  }

  const bool downloading =
      snapshot.stage == app::FirmwareUpdateStage::kDownloadingWireless ||
      snapshot.stage == app::FirmwareUpdateStage::kDownloadingMain;
  const bool installing_wireless =
      snapshot.stage == app::FirmwareUpdateStage::kInstallingWireless;
  const bool paused =
      snapshot.stage == app::FirmwareUpdateStage::kPaused;
  const bool ready =
      snapshot.stage == app::FirmwareUpdateStage::kReadyToInstall;
  const bool cancelling =
      std::strcmp(snapshot.message, "Cancelling update") == 0;
  const bool current_page_active =
      state->firmware_update_page_index == 0;
  SetFirmwareObjectVisible(
      state->firmware_update_download_button, true);
  char button_text[64] = {};
  bool button_enabled = false;
  if (current_page_active) {
    if (!snapshot.device_supported) {
      std::snprintf(button_text, sizeof(button_text), "Unavailable");
    } else if (scanning) {
      std::snprintf(button_text, sizeof(button_text), "Checking...");
    } else if (ready) {
      std::snprintf(button_text, sizeof(button_text), "Installation pending");
    } else if (snapshot.busy) {
      std::snprintf(button_text, sizeof(button_text), "Update in progress");
    } else if (manual_update_required) {
      std::snprintf(
          button_text, sizeof(button_text), "Manual update required");
    } else {
      std::snprintf(button_text, sizeof(button_text), "Check again");
      button_enabled = true;
    }
  } else {
    button_enabled = !snapshot.busy && snapshot.device_supported && !paused;
    if (cancelling) {
      std::snprintf(button_text, sizeof(button_text), "Cancelling...");
    } else {
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
      case app::FirmwareUpdateStage::kDownloadingWireless:
        std::snprintf(button_text, sizeof(button_text),
            "Downloading Wireless firmware  %d%%",
            snapshot.progress_percent);
        break;
      case app::FirmwareUpdateStage::kInstallingWireless:
        std::snprintf(button_text, sizeof(button_text),
            "Installing Wireless firmware  %d%%",
            snapshot.progress_percent);
        break;
      case app::FirmwareUpdateStage::kDownloadingMain:
        std::snprintf(button_text, sizeof(button_text),
            "Downloading Main firmware  %d%%",
            snapshot.progress_percent);
        break;
      case app::FirmwareUpdateStage::kPaused:
        std::snprintf(button_text, sizeof(button_text),
            "Download paused  %d%%", snapshot.progress_percent);
        break;
      case app::FirmwareUpdateStage::kReadyToInstall:
        std::snprintf(button_text, sizeof(button_text),
            "Restart and update now");
        break;
      case app::FirmwareUpdateStage::kRestarting:
        std::snprintf(button_text, sizeof(button_text), "Restarting...");
        break;
      case app::FirmwareUpdateStage::kFailed:
        std::snprintf(button_text, sizeof(button_text), "Retry download");
        break;
      case app::FirmwareUpdateStage::kUpToDate:
      case app::FirmwareUpdateStage::kIdle:
      default:
        std::snprintf(button_text, sizeof(button_text), "Download firmware");
        break;
      }
    }
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

  const bool show_progress =
      !current_page_active && (downloading || installing_wireless || paused);
  SetFirmwareObjectVisible(
      state->firmware_update_progress_fill, show_progress);
  if (show_progress) {
    const int button_width =
        lv_obj_get_width(state->firmware_update_download_button);
    const int fill_width = std::max(1,
        button_width * std::clamp(snapshot.progress_percent, 0, 100) / 100);
    lv_obj_set_width(state->firmware_update_progress_fill, fill_width);
  }
  lv_obj_set_style_bg_color(state->firmware_update_download_button,
      lv_color_hex(show_progress
              ? theme::LightNeutralTheme().surface_container_high
              : kDetailBlueColor),
      LV_PART_MAIN);
  lv_obj_set_style_text_color(
      state->firmware_update_download_button_label,
      lv_color_hex(theme::LightNeutralTheme().on_action),
      LV_PART_MAIN);
  lv_obj_move_to_index(state->firmware_update_download_button_label, -1);

  const bool show_cancel =
      !current_page_active && (downloading || paused || ready);
  SetFirmwareObjectVisible(
      state->firmware_update_pause_button, false);
  SetFirmwareObjectVisible(
      state->firmware_update_cancel_button, show_cancel);
  lv_label_set_text(state->firmware_update_cancel_button_label,
      ready ? "Cancel current update"
            : cancelling ? "Cancelling..." : "Cancel download");
  if (cancelling) {
    lv_obj_add_state(
        state->firmware_update_cancel_button, LV_STATE_DISABLED);
  } else {
    lv_obj_remove_state(
        state->firmware_update_cancel_button, LV_STATE_DISABLED);
  }
  const int primary_width =
      lv_obj_get_width(state->firmware_update_download_button);
  if (show_cancel) {
    lv_obj_set_size(state->firmware_update_cancel_button,
        primary_width, kUpdateButtonHeight);
    lv_obj_set_style_radius(state->firmware_update_cancel_button,
        kUpdateButtonHeight / 3, LV_PART_MAIN);
    lv_obj_align(state->firmware_update_cancel_button,
        LV_ALIGN_BOTTOM_MID, 0, -kUpdateButtonBottom);
    lv_obj_align_to(state->firmware_update_download_button,
        state->firmware_update_cancel_button, LV_ALIGN_OUT_TOP_MID,
        0, -kUpdateActionButtonGap);
  } else {
    lv_obj_align(state->firmware_update_download_button,
        LV_ALIGN_BOTTOM_MID, 0, -kUpdateButtonBottom);
  }
  lv_obj_align(state->firmware_update_page_indicator,
      LV_ALIGN_BOTTOM_MID, 0,
      show_cancel ? -kUpdatePageIndicatorStackedBottom
                  : -kUpdatePageIndicatorBottom);
}

/**
 * @brief 在固件当前版本页和新版本页之间切换状态
 * @param event LVGL 滚动结束事件
 */
void FirmwareUpdatePageScrollEventCallback(lv_event_t* event) {
  if (lv_event_get_code(event) != LV_EVENT_SCROLL_END) {
    return;
  }
  auto* state = static_cast<SettingsViewState*>(
      lv_event_get_user_data(event));
  if (state == nullptr || state->firmware_update_body == nullptr) {
    return;
  }
  const app::FirmwareUpdateSnapshot snapshot =
      app::GetFirmwareUpdateSnapshot();
  const bool new_version_available =
      snapshot.manifest_available && snapshot.update_available;
  const int scroll_x =
      static_cast<int>(lv_obj_get_scroll_x(state->firmware_update_body));
  state->firmware_update_page_index =
      new_version_available && scroll_x >= state->config.width / 2 ? 1 : 0;
  RefreshFirmwareUpdateView(state);
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
  if (state == nullptr) {
    return;
  }
  const app::FirmwareUpdateSnapshot snapshot =
      app::GetFirmwareUpdateSnapshot();
  if (state->firmware_update_page_index == 0) {
    if (snapshot.busy || !snapshot.device_supported ||
        snapshot.stage == app::FirmwareUpdateStage::kReadyToInstall) {
      return;
    }
    state->firmware_update_auto_show_new_page =
        app::RequestFirmwareUpdateCheck();
    RefreshFirmwareUpdateView(state);
    return;
  }
  if (snapshot.busy || !snapshot.device_supported) {
    return;
  }
  if (snapshot.stage == app::FirmwareUpdateStage::kReadyToInstall) {
    app::InstallFirmwareUpdateAndRestart();
  } else if (snapshot.manifest_available && snapshot.update_available) {
    app::StartFirmwareUpdate();
  } else {
    app::RequestFirmwareUpdateCheck();
  }
  RefreshFirmwareUpdateView(state);
}

void FirmwareUpdatePauseClickedEventCallback(lv_event_t* event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
    return;
  }
  auto* state = static_cast<SettingsViewState*>(
      lv_event_get_user_data(event));
  const app::FirmwareUpdateSnapshot snapshot =
      app::GetFirmwareUpdateSnapshot();
  if (snapshot.stage == app::FirmwareUpdateStage::kPaused) {
    app::ResumeFirmwareUpdate();
  } else {
    app::PauseFirmwareUpdate();
  }
  RefreshFirmwareUpdateView(state);
}

void FirmwareUpdateCancelClickedEventCallback(lv_event_t* event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
    return;
  }
  auto* state = static_cast<SettingsViewState*>(
      lv_event_get_user_data(event));
  app::CancelFirmwareUpdate();
  RefreshFirmwareUpdateView(state);
}

/**
 * @brief 从当前版本状态区打开当前版本日志页面
 * @param event LVGL 事件对象
 */
void FirmwareCurrentLogClickedEventCallback(lv_event_t* event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
    return;
  }
  auto* state = static_cast<SettingsViewState*>(
      lv_event_get_user_data(event));
  if (state == nullptr) {
    return;
  }
  ShowFirmwareUpdateLogPage(state);
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
bool CreateFirmwareBrand(lv_obj_t* card, int card_width) {
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
  const int brand_width = card_width - 2 * kUpdateCardPadding;
  const int text_width = std::max(1,
      brand_width - kUpdateBrandIconSize - kUpdateBrandGap);
  lv_obj_set_size(brand_group, brand_width, kUpdateBrandIconSize);
  lv_obj_align(brand_group, LV_ALIGN_TOP_LEFT, kUpdateCardPadding,
      kUpdateBrandTop);
  lv_obj_align(brand_icon, LV_ALIGN_LEFT_MID, 0, 0);
  lv_obj_set_size(brand_text, text_width, kUpdateBrandIconSize);
  lv_label_set_long_mode(brand_text, LV_LABEL_LONG_DOT);
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
 * @param row_output 组件行输出地址
 * @param chip_label_output 芯片型号和大小标签输出地址
 * @param version_label_output 版本标签输出地址
 * @return 创建成功返回 true，否则返回 false
 */
bool CreateFirmwareComponentRow(lv_obj_t* card, int y, int width,
    const char* symbol, const char* title, const char* chip,
    const char* version, uint32_t color,
    lv_obj_t** row_output, lv_obj_t** chip_label_output,
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
  if (chip_label_output != nullptr) {
    *chip_label_output = component_chip;
  }
  if (version_label_output != nullptr) {
    *version_label_output = component_version;
  }
  if (row_output != nullptr) {
    *row_output = tile;
  }
  return true;
}

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
    state->firmware_update_channel_label = nullptr;
    state->firmware_update_release_time_label = nullptr;
    state->firmware_update_components_title = nullptr;
    state->firmware_update_main_row = nullptr;
    state->firmware_update_main_chip_label = nullptr;
    state->firmware_update_main_version_label = nullptr;
    state->firmware_update_wireless_row = nullptr;
    state->firmware_update_wireless_chip_label = nullptr;
    state->firmware_update_wireless_version_label = nullptr;
    state->firmware_update_components_divider = nullptr;
    state->firmware_update_notes_title = nullptr;
    state->firmware_update_notes_label = nullptr;
    return false;
  };
  lv_obj_remove_flag(card, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_flag(card, LV_OBJ_FLAG_GESTURE_BUBBLE);
  lv_obj_align(card, LV_ALIGN_TOP_MID, 0, kUpdateCardTop);

  if (!CreateFirmwareBrand(card, card_width)) {
    return discard_card();
  }

  lv_obj_t* version = CreateLabel(card, "Checking...",
      lv_color_hex(kSecondaryTextColor), Font24());
  if (version == nullptr) {
    return discard_card();
  }
  state->firmware_update_release_label = version;
  const int release_version_width = std::max(1,
      card_width - 2 * kUpdateCardPadding - kUpdateReleaseMetadataWidth -
          kUpdateReleaseMetadataGap);
  lv_obj_set_width(version, release_version_width);
  lv_label_set_long_mode(version, LV_LABEL_LONG_DOT);
  lv_obj_align(version, LV_ALIGN_TOP_LEFT, kUpdateCardPadding,
      kUpdateVersionTop);

  lv_obj_t* channel_label = CreateLabel(card, "Stable",
      lv_color_hex(kUpdateStableChannelColor), Font22());
  lv_obj_t* release_time = CreateLabel(card, "Unavailable",
      lv_color_hex(kSecondaryTextColor), Font22());
  if (channel_label == nullptr || release_time == nullptr) {
    return discard_card();
  }
  state->firmware_update_channel_label = channel_label;
  state->firmware_update_release_time_label = release_time;
  lv_obj_set_width(channel_label, kUpdateReleaseMetadataWidth);
  lv_obj_set_width(release_time, kUpdateReleaseMetadataWidth);
  lv_obj_set_style_text_align(
      channel_label, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);
  lv_obj_set_style_text_align(
      release_time, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);
  lv_obj_align(channel_label, LV_ALIGN_TOP_RIGHT, -kUpdateCardPadding,
      kUpdateChannelTop);
  lv_obj_align(release_time, LV_ALIGN_TOP_RIGHT, -kUpdateCardPadding,
      kUpdateReleaseTimeTop);

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
  state->firmware_update_components_title = components_title;
  lv_obj_align(components_title, LV_ALIGN_TOP_LEFT, kUpdateCardPadding,
      kUpdateComponentsTitleTop);

  const int component_width = card_width - 2 * kUpdateCardPadding;
  if (!CreateFirmwareComponentRow(card, kUpdateComponentsTop,
          component_width, icon::kMemory, "Main firmware", "ESP32-P4",
          "vunknown", 0x3F82F6, &state->firmware_update_main_row,
          &state->firmware_update_main_chip_label,
          &state->firmware_update_main_version_label) ||
      !CreateFirmwareComponentRow(card,
          kUpdateComponentsTop + kUpdateComponentHeight +
              kUpdateComponentGap,
          component_width, icon::kSignalWifi4Bar, "Wireless firmware",
          "ESP32-C6", "vunknown", 0x8B68F6,
          &state->firmware_update_wireless_row,
          &state->firmware_update_wireless_chip_label,
          &state->firmware_update_wireless_version_label)) {
    return discard_card();
  }

  lv_obj_t* second_divider = CreateDivider(card, component_width);
  if (second_divider == nullptr) {
    return discard_card();
  }
  state->firmware_update_components_divider = second_divider;
  lv_obj_set_pos(
      second_divider, kUpdateCardPadding, kUpdateSecondDividerTop);

  lv_obj_t* whats_new_title = CreateLabel(card, "What's new",
      lv_color_hex(kPrimaryTextColor), Font28());
  if (whats_new_title == nullptr) {
    return discard_card();
  }
  state->firmware_update_notes_title = whats_new_title;
  lv_obj_align(whats_new_title, LV_ALIGN_TOP_LEFT, kUpdateCardPadding,
      kUpdateWhatsNewTitleTop);

  lv_obj_t* notes = CreateLabel(card,
      "Release notes will appear after checking.",
      lv_color_hex(kSecondaryTextColor), Font22());
  if (notes == nullptr) {
    return discard_card();
  }
  lv_obj_set_width(notes, card_width - 2 * kUpdateCardPadding);
  lv_label_set_long_mode(notes, LV_LABEL_LONG_WRAP);
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
  lv_obj_set_scroll_dir(body, LV_DIR_HOR);
  lv_obj_set_scroll_snap_x(body, LV_SCROLL_SNAP_CENTER);
  lv_obj_set_scrollbar_mode(body, LV_SCROLLBAR_MODE_OFF);
  lv_obj_add_flag(body, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(body, LV_OBJ_FLAG_SCROLL_ONE);
  lv_obj_add_flag(body, LV_OBJ_FLAG_GESTURE_BUBBLE);
  lv_obj_remove_flag(body, LV_OBJ_FLAG_SCROLL_ELASTIC);
  lv_obj_remove_flag(body, LV_OBJ_FLAG_SCROLL_MOMENTUM);
  lv_obj_set_style_anim_duration(body, 120, LV_PART_MAIN);
  lv_obj_add_event_cb(body, FirmwareUpdatePageScrollEventCallback,
      LV_EVENT_SCROLL_END, state);
  AddEdgeBackSwipeEvents(body, FirmwareUpdateEdgeBackEventCallback, state);

  const int content_width = FirmwareUpdateContentWidth(width);
  const int content_left = (width - content_width) / 2;
  lv_obj_t* current_page = lv_obj_create(body);
  lv_obj_t* new_page = lv_obj_create(body);
  if (current_page == nullptr || new_page == nullptr) {
    return false;
  }
  state->firmware_update_current_page = current_page;
  state->firmware_update_new_page = new_page;
  MakeTransparent(current_page);
  MakeTransparent(new_page);
  lv_obj_remove_flag(current_page, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(new_page, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(new_page, LV_OBJ_FLAG_SCROLL_CHAIN_HOR);
  lv_obj_set_scroll_dir(new_page, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(new_page, LV_SCROLLBAR_MODE_OFF);
  lv_obj_remove_flag(new_page, LV_OBJ_FLAG_SCROLL_ELASTIC);
  lv_obj_remove_flag(new_page, LV_OBJ_FLAG_SCROLL_MOMENTUM);
  lv_obj_add_flag(current_page, LV_OBJ_FLAG_SNAPPABLE);
  lv_obj_add_flag(new_page, LV_OBJ_FLAG_SNAPPABLE);
  lv_obj_add_flag(current_page, LV_OBJ_FLAG_GESTURE_BUBBLE);
  lv_obj_add_flag(new_page, LV_OBJ_FLAG_GESTURE_BUBBLE);
  lv_obj_set_size(current_page, width, body_height);
  lv_obj_set_size(new_page, width, body_height);
  lv_obj_set_pos(current_page, 0, 0);
  lv_obj_set_pos(new_page, width, 0);
  lv_obj_add_flag(new_page, LV_OBJ_FLAG_HIDDEN);

  lv_obj_t* heading = CreateLabel(new_page, "New version available",
      lv_color_hex(kPrimaryTextColor), Font36());
  if (heading == nullptr) {
    return false;
  }
  lv_obj_set_width(heading, content_width);
  lv_obj_set_pos(heading, content_left, kUpdateHeadingTop);
  lv_obj_add_flag(heading, LV_OBJ_FLAG_HIDDEN);
  state->firmware_update_heading_label = heading;

  lv_obj_t* scan_group = lv_obj_create(current_page);
  if (scan_group == nullptr) {
    return false;
  }
  MakeTransparent(scan_group);
  lv_obj_remove_flag(scan_group, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(scan_group, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_size(scan_group, content_width, kUpdateScanGroupHeight);
  lv_obj_set_pos(scan_group, content_left, kUpdateScanGroupTop);
  state->firmware_update_scan_group = scan_group;

  const int status_brand_width = std::min(content_width, 480);
  lv_obj_t* brand_group = lv_obj_create(scan_group);
  if (brand_group == nullptr) {
    return false;
  }
  MakeTransparent(brand_group);
  lv_obj_remove_flag(brand_group, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(brand_group, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_size(
      brand_group, status_brand_width, kUpdateBrandIconSize);
  lv_obj_align(brand_group, LV_ALIGN_TOP_MID, 0, 0);
  lv_obj_set_flex_flow(brand_group, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(brand_group, LV_FLEX_ALIGN_CENTER,
      LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(
      brand_group, kUpdateStatusBrandGap, LV_PART_MAIN);

  lv_obj_t* brand_icon =
      CreateLilygoBoxBrandIcon(brand_group, kUpdateBrandIconSize);
  lv_obj_t* brand_text = CreateLabel(brand_group, "LilygoBox",
      lv_color_hex(kPrimaryTextColor), Font48());
  if (brand_icon == nullptr || brand_text == nullptr) {
    return false;
  }
  lv_obj_set_width(brand_text, LV_SIZE_CONTENT);

  lv_obj_t* version = CreateLabel(brand_group, "vunknown",
      lv_color_hex(kSecondaryTextColor), Font24());
  if (version == nullptr) {
    return false;
  }
  lv_obj_set_width(version, LV_SIZE_CONTENT);
  state->firmware_update_status_version_label = version;

  lv_obj_t* spinner = lv_spinner_create(scan_group);
  if (spinner == nullptr) {
    return false;
  }
  lv_obj_set_size(spinner, kUpdateSpinnerSize, kUpdateSpinnerSize);
  lv_spinner_set_anim_params(spinner, 850, 250);
  lv_obj_set_style_arc_color(spinner,
      lv_color_hex(theme::LightNeutralTheme().surface_container_high),
      LV_PART_MAIN);
  lv_obj_set_style_arc_color(spinner,
      lv_color_hex(theme::LightNeutralTheme().action), LV_PART_INDICATOR);
  lv_obj_set_style_arc_width(spinner, 7, LV_PART_MAIN);
  lv_obj_set_style_arc_width(spinner, 7, LV_PART_INDICATOR);
  lv_obj_align(spinner, LV_ALIGN_TOP_MID, 0, kUpdateStatusSpinnerTop);
  lv_obj_add_flag(spinner, LV_OBJ_FLAG_HIDDEN);
  state->firmware_update_spinner = spinner;

  lv_obj_t* message = CreateLabel(scan_group, "Checking for updates...",
      lv_color_hex(kPrimaryTextColor), Font28());
  lv_obj_t* hint = CreateLabel(scan_group,
      "Downloading update information",
      lv_color_hex(kSecondaryTextColor), Font22());
  if (message == nullptr || hint == nullptr) {
    return false;
  }
  lv_obj_set_width(message, content_width);
  lv_obj_set_width(hint, content_width);
  lv_obj_set_style_text_align(message, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
  lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
  lv_obj_align(message, LV_ALIGN_TOP_MID, 0, kUpdateStatusPrimaryTop);
  lv_obj_align(hint, LV_ALIGN_TOP_MID, 0, kUpdateStatusHintTop);
  state->firmware_update_scan_message_label = message;
  state->firmware_update_scan_hint_label = hint;

  lv_obj_t* log_button = lv_button_create(scan_group);
  if (log_button == nullptr) {
    return false;
  }
  // 清除按钮主题自带状态，按压颜色与“我的设备”出厂设置入口一致。
  lv_obj_remove_style_all(log_button);
  lv_obj_remove_flag(log_button, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(log_button, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
  lv_obj_add_flag(log_button, LV_OBJ_FLAG_GESTURE_BUBBLE);
  lv_obj_set_size(
      log_button, LV_SIZE_CONTENT, kUpdateStatusLogButtonHeight);
  lv_obj_align(
      log_button, LV_ALIGN_TOP_MID, 0, kUpdateStatusLogButtonTop);
  lv_obj_set_style_bg_opa(log_button, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_set_style_bg_color(log_button,
      lv_color_hex(kDetailOptionPressedColor), LV_STATE_PRESSED);
  lv_obj_set_style_bg_opa(
      log_button, kDetailOptionPressedOpacity, LV_STATE_PRESSED);
  lv_obj_set_style_border_width(log_button, 0, LV_PART_MAIN);
  lv_obj_set_style_radius(log_button, LV_RADIUS_CIRCLE, LV_PART_MAIN);
  lv_obj_set_style_shadow_width(log_button, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_left(
      log_button, kUpdateStatusLogButtonPaddingX, LV_PART_MAIN);
  lv_obj_set_style_pad_right(
      log_button, kUpdateStatusLogButtonPaddingX, LV_PART_MAIN);
  lv_obj_set_style_pad_top(log_button, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_bottom(log_button, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_column(
      log_button, kUpdateStatusLogButtonContentGap, LV_PART_MAIN);
  lv_obj_set_flex_flow(log_button, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(log_button, LV_FLEX_ALIGN_CENTER,
      LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  if (!AddPressCancelOnLeave(log_button)) {
    return false;
  }
  lv_obj_add_event_cb(log_button,
      FirmwareCurrentLogClickedEventCallback, LV_EVENT_CLICKED, state);

  lv_obj_t* log_label = CreateLabel(log_button,
      "Current version update log", lv_color_hex(kSecondaryTextColor),
      Font24());
  lv_obj_t* log_arrow = CreateLabel(log_button, icon::kChevronRight,
      lv_color_hex(kSecondaryTextColor), MaterialIconFont32());
  if (log_label == nullptr || log_arrow == nullptr) {
    return false;
  }
  state->firmware_update_status_log_button = log_button;
  state->firmware_update_status_log_button_label = log_label;

  lv_obj_t* indicator = lv_obj_create(page);
  if (indicator == nullptr) {
    return false;
  }
  state->firmware_update_page_indicator = indicator;
  MakeTransparent(indicator);
  lv_obj_remove_flag(indicator, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(indicator, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_flag(indicator, LV_OBJ_FLAG_HIDDEN);
  lv_obj_set_size(indicator,
      kUpdatePageIndicatorWidth, kUpdatePageIndicatorHeight);
  lv_obj_align(indicator, LV_ALIGN_BOTTOM_MID, 0,
      -kUpdatePageIndicatorBottom);

  lv_obj_t* current_dot = lv_obj_create(indicator);
  lv_obj_t* new_dot = lv_obj_create(indicator);
  if (current_dot == nullptr || new_dot == nullptr) {
    return false;
  }
  state->firmware_update_current_page_dot = current_dot;
  state->firmware_update_new_page_dot = new_dot;
  lv_obj_t* dots[] = {current_dot, new_dot};
  for (lv_obj_t* dot : dots) {
    lv_obj_remove_flag(dot, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(dot, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(dot, kUpdatePageDotSize, kUpdatePageDotSize);
    lv_obj_set_style_bg_color(
        dot, lv_color_hex(kDetailBlueColor), LV_PART_MAIN);
    lv_obj_set_style_border_width(dot, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_pad_all(dot, 0, LV_PART_MAIN);
  }
  lv_obj_align(current_dot, LV_ALIGN_CENTER, -10, 0);
  lv_obj_align(new_dot, LV_ALIGN_CENTER, 10, 0);
  lv_obj_set_style_bg_opa(current_dot, 240, LV_PART_MAIN);
  lv_obj_set_style_bg_opa(new_dot, 110, LV_PART_MAIN);
  lv_obj_update_snap(body, LV_ANIM_OFF);
  return true;
}

/**
 * @brief 清理固件版本日志二级页面引用
 * @param state 设置页面状态
 */
void ClearFirmwareUpdateLogReferences(SettingsViewState* state) {
  if (state == nullptr) {
    return;
  }
  state->firmware_update_log_page = nullptr;
  state->firmware_update_log_body = nullptr;
  state->firmware_update_log_closing = false;
  state->firmware_update_log_swipe = EdgeBackSwipeState();
}

/**
 * @brief 处理固件版本日志页面关闭动画完成事件
 * @param animation LVGL 动画对象
 */
void FirmwareUpdateLogCloseCompletedCallback(lv_anim_t* animation) {
  auto* state =
      static_cast<SettingsViewState*>(lv_anim_get_user_data(animation));
  if (state == nullptr || state->firmware_update_log_page == nullptr) {
    return;
  }
  lv_obj_t* page = state->firmware_update_log_page;
  ClearFirmwareUpdateLogReferences(state);
  lv_obj_delete(page);
}

void CloseFirmwareUpdateLogPage(
    SettingsViewState* state, bool animated) {
  if (state == nullptr || state->firmware_update_log_page == nullptr ||
      state->firmware_update_log_closing) {
    return;
  }
  if (animated &&
      StartSlideRightWindowTransition(state->firmware_update_log_page,
          state->config.width, kDetailSlideAnimationMs, state,
          FirmwareUpdateLogCloseCompletedCallback)) {
    state->firmware_update_log_closing = true;
    return;
  }
  lv_obj_t* page = state->firmware_update_log_page;
  ClearFirmwareUpdateLogReferences(state);
  lv_obj_delete(page);
}

/**
 * @brief 处理固件版本日志页面返回按钮
 * @param event LVGL 事件对象
 */
void FirmwareUpdateLogBackClickedEventCallback(lv_event_t* event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
    return;
  }
  CloseFirmwareUpdateLogPage(
      static_cast<SettingsViewState*>(lv_event_get_user_data(event)), true);
}

/**
 * @brief 处理固件版本日志页面边缘返回手势
 * @param event LVGL 事件对象
 */
void FirmwareUpdateLogEdgeBackEventCallback(lv_event_t* event) {
  auto* state = static_cast<SettingsViewState*>(lv_event_get_user_data(event));
  if (state == nullptr || state->firmware_update_log_page == nullptr ||
      state->firmware_update_log_closing || state->config.screen == nullptr ||
      !HandleEdgeBackSwipeEvent(event, state->config.width,
          &state->firmware_update_log_swipe)) {
    return;
  }
  CloseFirmwareUpdateLogPage(state, true);
  lv_event_stop_bubbling(event);
  lv_event_stop_processing(event);
}

/**
 * @brief 创建与新版本更新卡片一致的当前版本日志卡片
 * @param body 日志页面滚动区域
 * @param x 卡片左侧坐标
 * @param y 卡片顶部坐标
 * @param width 卡片宽度
 * @param snapshot 当前固件状态快照
 * @return 创建成功返回卡片对象，否则返回 nullptr
 */
lv_obj_t* CreateCurrentFirmwareLogCard(lv_obj_t* body, int x, int y,
    int width, const app::FirmwareUpdateSnapshot& snapshot) {
  lv_obj_t* card = CreateBox(body, width, kUpdateCardHeight,
      kUpdateCardColor, LV_OPA_COVER, kDetailCardRadius);
  if (card == nullptr) {
    return nullptr;
  }
  const auto discard_card = [card]() -> lv_obj_t* {
    lv_obj_delete(card);
    return nullptr;
  };
  lv_obj_remove_flag(card, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_flag(card, LV_OBJ_FLAG_GESTURE_BUBBLE);
  lv_obj_set_pos(card, x, y);

  if (!CreateFirmwareBrand(card, width)) {
    return discard_card();
  }

  char current_release[64] = {};
  if (snapshot.current_release_version[0] != '\0') {
    std::snprintf(current_release, sizeof(current_release), "%s",
        snapshot.current_release_version);
  } else {
    FormatFirmwareVersion(snapshot.main_current_version, nullptr,
        current_release, sizeof(current_release));
  }
  lv_obj_t* version = CreateLabel(card, current_release,
      lv_color_hex(kSecondaryTextColor), Font24());
  if (version == nullptr) {
    return discard_card();
  }
  const int release_version_width = std::max(1,
      width - 2 * kUpdateCardPadding - kUpdateReleaseMetadataWidth -
          kUpdateReleaseMetadataGap);
  lv_obj_set_width(version, release_version_width);
  lv_label_set_long_mode(version, LV_LABEL_LONG_DOT);
  lv_obj_set_pos(version, kUpdateCardPadding, kUpdateVersionTop);

  char release_time_text[48] = {};
  FormatReleaseTimeForDisplay(snapshot.current_release_time,
      release_time_text, sizeof(release_time_text));
  lv_obj_t* channel_label = CreateLabel(card, "Stable",
      lv_color_hex(kUpdateStableChannelColor), Font22());
  lv_obj_t* release_time = CreateLabel(card, release_time_text,
      lv_color_hex(kSecondaryTextColor), Font22());
  if (channel_label == nullptr || release_time == nullptr) {
    return discard_card();
  }
  SetReleaseChannelLabel(channel_label, snapshot.current_release_channel);
  lv_obj_set_width(channel_label, kUpdateReleaseMetadataWidth);
  lv_obj_set_width(release_time, kUpdateReleaseMetadataWidth);
  lv_obj_set_style_text_align(
      channel_label, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);
  lv_obj_set_style_text_align(
      release_time, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);
  lv_obj_align(channel_label, LV_ALIGN_TOP_RIGHT, -kUpdateCardPadding,
      kUpdateChannelTop);
  lv_obj_align(release_time, LV_ALIGN_TOP_RIGHT, -kUpdateCardPadding,
      kUpdateReleaseTimeTop);

  lv_obj_t* divider =
      CreateDivider(card, width - 2 * kUpdateCardPadding);
  lv_obj_t* components_title = CreateLabel(card, "Installed components",
      lv_color_hex(kPrimaryTextColor), Font28());
  if (divider == nullptr || components_title == nullptr) {
    return discard_card();
  }
  lv_obj_set_pos(divider, kUpdateCardPadding, kUpdateDividerTop);
  lv_obj_set_pos(
      components_title, kUpdateCardPadding, kUpdateComponentsTitleTop);

  char main_version[64] = {};
  char wireless_version[64] = {};
  char main_chip[48] = {};
  char wireless_chip[48] = {};
  FormatFirmwareVersion(snapshot.main_current_version, nullptr,
      main_version, sizeof(main_version));
  FormatFirmwareVersion(snapshot.wireless_current_version, nullptr,
      wireless_version, sizeof(wireless_version));
  FormatFirmwareChipText("ESP32-P4", snapshot.current_main_size,
      main_chip, sizeof(main_chip));
  FormatFirmwareChipText("ESP32-C6", snapshot.current_wireless_size,
      wireless_chip, sizeof(wireless_chip));
  const int component_width = width - 2 * kUpdateCardPadding;
  if (!CreateFirmwareComponentRow(card, kUpdateComponentsTop,
          component_width, icon::kMemory, "Main firmware", main_chip,
          main_version, 0x3F82F6, nullptr, nullptr, nullptr) ||
      !CreateFirmwareComponentRow(card,
          kUpdateComponentsTop + kUpdateComponentHeight +
              kUpdateComponentGap,
          component_width, icon::kSignalWifi4Bar, "Wireless firmware",
          wireless_chip, wireless_version, 0x8B68F6, nullptr, nullptr,
          nullptr)) {
    return discard_card();
  }

  lv_obj_t* second_divider = CreateDivider(card, component_width);
  lv_obj_t* whats_new_title = CreateLabel(card, "What's new",
      lv_color_hex(kPrimaryTextColor), Font28());
  if (second_divider == nullptr || whats_new_title == nullptr) {
    return discard_card();
  }
  lv_obj_set_pos(
      second_divider, kUpdateCardPadding, kUpdateSecondDividerTop);
  lv_obj_set_pos(
      whats_new_title, kUpdateCardPadding, kUpdateWhatsNewTitleTop);

  char notes_text[420] = {};
  FormatFirmwareNotesText(snapshot.current_notes,
      snapshot.current_note_count,
      "No local release notes are available for this version.",
      notes_text, sizeof(notes_text));
  lv_obj_t* notes = CreateLabel(card, notes_text,
      lv_color_hex(kSecondaryTextColor), Font22());
  if (notes == nullptr) {
    return discard_card();
  }
  lv_obj_set_width(notes, component_width);
  lv_label_set_long_mode(notes, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_text_line_space(notes, 12, LV_PART_MAIN);
  lv_obj_set_pos(notes, kUpdateCardPadding, kUpdateWhatsNewTop);
  FitFirmwareCardToNotes(card, notes, kUpdateWhatsNewTop);
  return card;
}

/**
 * @brief 创建固件版本日志页面顶部导航栏
 * @param page 日志页面
 * @param state 设置页面状态
 * @param width 页面宽度
 * @return 创建成功返回 true，否则返回 false
 */
bool CreateFirmwareUpdateLogHeader(
    lv_obj_t* page, SettingsViewState* state, int width) {
  lv_obj_t* title = CreateLabel(
      page, "Current version update log",
      lv_color_hex(kTitleColor), Font32());
  if (title == nullptr) {
    return false;
  }
  lv_obj_set_width(title, width);
  lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
  lv_obj_align(title, LV_ALIGN_TOP_MID, 0, kDetailTitleTop);
  lv_obj_t* back_button = CreateToolbarButton(page,
      kDetailBackButtonLeft, kDetailBackButtonTop,
      FirmwareUpdateLogBackClickedEventCallback, state);
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
 * @brief 创建固件版本日志页面内容
 * @param page 日志页面
 * @param state 设置页面状态
 * @param width 页面宽度
 * @param height 页面高度
 * @return 创建成功返回 true，否则返回 false
 */
bool CreateFirmwareUpdateLogBody(lv_obj_t* page, SettingsViewState* state,
    int width, int height) {
  const int body_height = height - kDetailBodyTop;
  if (body_height <= 0) {
    return false;
  }
  lv_obj_t* body = lv_obj_create(page);
  if (body == nullptr) {
    return false;
  }
  state->firmware_update_log_body = body;
  MakeTransparent(body);
  lv_obj_set_size(body, width, body_height);
  lv_obj_align(body, LV_ALIGN_TOP_LEFT, 0, kDetailBodyTop);
  lv_obj_set_scroll_dir(body, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(body, LV_SCROLLBAR_MODE_OFF);
  lv_obj_add_flag(body, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(body, LV_OBJ_FLAG_GESTURE_BUBBLE);
  lv_obj_remove_flag(body, LV_OBJ_FLAG_SCROLL_ELASTIC);
  AddEdgeBackSwipeEvents(body, FirmwareUpdateLogEdgeBackEventCallback, state);

  const app::FirmwareUpdateSnapshot snapshot =
      app::GetFirmwareUpdateSnapshot();
  const int content_width = FirmwareUpdateContentWidth(width);
  const int content_left = (width - content_width) / 2;
  lv_obj_t* current_card = CreateCurrentFirmwareLogCard(
      body, content_left, 18, content_width, snapshot);
  return current_card != nullptr;
}

bool ShowFirmwareUpdateLogPage(SettingsViewState* state) {
  if (state == nullptr || state->root == nullptr ||
      state->firmware_update_page == nullptr ||
      state->firmware_update_closing) {
    return false;
  }
  if (state->firmware_update_log_closing) {
    return true;
  }
  if (state->firmware_update_log_page != nullptr) {
    lv_obj_move_to_index(state->firmware_update_log_page, -1);
    return true;
  }

  const AppViewConfig& config = state->config;
  lv_obj_t* page = lv_obj_create(state->root);
  if (page == nullptr) {
    return false;
  }
  state->firmware_update_log_page = page;
  state->firmware_update_log_closing = false;
  state->firmware_update_log_swipe = EdgeBackSwipeState();
  lv_obj_remove_flag(page, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(page, LV_OBJ_FLAG_GESTURE_BUBBLE);
  AddEdgeBackSwipeEvents(page, FirmwareUpdateLogEdgeBackEventCallback, state);
  lv_obj_set_size(page, config.width, config.height);
  lv_obj_set_pos(page, 0, 0);
  lv_obj_set_style_bg_color(
      page, lv_color_hex(kDetailBackgroundColor), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(page, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_border_width(page, 0, LV_PART_MAIN);
  lv_obj_set_style_radius(page, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(page, 0, LV_PART_MAIN);

  const bool created =
      CreateFirmwareUpdateLogHeader(page, state, config.width) &&
      CreateFirmwareUpdateLogBody(
          page, state, config.width, config.height);
  if (!created) {
    CloseFirmwareUpdateLogPage(state, false);
    return false;
  }
  EnableEdgeBackSwipeEventBubble(page);
  if (!StartSlideLeftWindowTransition(
          page, config.width, kDetailSlideAnimationMs, state, nullptr)) {
    CloseFirmwareUpdateLogPage(state, false);
    return false;
  }
  return true;
}

bool CreateFirmwareSecondaryButton(lv_obj_t* page, int width,
    const char* text, lv_event_cb_t callback, SettingsViewState* state,
    lv_obj_t** button_output, lv_obj_t** label_output, bool destructive) {
  lv_obj_t* button = lv_button_create(page);
  if (button == nullptr) {
    return false;
  }
  lv_obj_remove_flag(button, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(button, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
  lv_obj_add_flag(button, LV_OBJ_FLAG_GESTURE_BUBBLE);
  lv_obj_add_flag(button, LV_OBJ_FLAG_HIDDEN);
  lv_obj_set_size(button, width, kUpdateActionButtonHeight);
  const uint32_t background_color = destructive
      ? kUpdateCancelButtonColor
      : theme::LightNeutralTheme().button_secondary;
  const uint32_t pressed_color = destructive
      ? kUpdateCancelButtonPressedColor
      : theme::LightNeutralTheme().button_secondary_pressed;
  const uint32_t text_color = destructive
      ? theme::LightNeutralTheme().on_surface
      : theme::LightNeutralTheme().on_button_secondary;
  lv_obj_set_style_bg_color(button,
      lv_color_hex(background_color), LV_PART_MAIN);
  lv_obj_set_style_bg_color(button,
      lv_color_hex(pressed_color), LV_STATE_PRESSED);
  lv_obj_set_style_bg_opa(button, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_bg_opa(button, LV_OPA_COVER, LV_STATE_PRESSED);
  lv_obj_set_style_border_width(button, 0, LV_PART_MAIN);
  lv_obj_set_style_radius(button, kUpdateActionButtonHeight / 2,
      LV_PART_MAIN);
  lv_obj_set_style_shadow_width(button, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(button, 0, LV_PART_MAIN);
  if (!AddPressCancelOnLeave(button)) {
    return false;
  }
  lv_obj_add_event_cb(button, callback, LV_EVENT_CLICKED, state);

  lv_obj_t* label =
      CreateLabel(button, text, lv_color_hex(text_color), Font28());
  if (label == nullptr) {
    return false;
  }
  lv_obj_center(label);
  *button_output = button;
  *label_output = label;
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
  lv_obj_set_style_opa(button, LV_OPA_COVER,
      static_cast<lv_style_selector_t>(LV_PART_MAIN) |
          static_cast<lv_style_selector_t>(LV_STATE_DISABLED));
  lv_obj_set_style_border_width(button, 0, LV_PART_MAIN);
  lv_obj_set_style_radius(button, kUpdateButtonHeight / 3, LV_PART_MAIN);
  lv_obj_set_style_shadow_width(button, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(button, 0, LV_PART_MAIN);
  lv_obj_set_style_clip_corner(button, true, LV_PART_MAIN);
  if (!AddPressCancelOnLeave(button)) {
    return false;
  }
  lv_obj_add_event_cb(button, FirmwareUpdateDownloadClickedEventCallback,
      LV_EVENT_CLICKED, state);

  lv_obj_t* progress_fill = lv_obj_create(button);
  if (progress_fill == nullptr) {
    return false;
  }
  lv_obj_remove_flag(progress_fill, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(progress_fill, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_flag(progress_fill, LV_OBJ_FLAG_HIDDEN);
  lv_obj_set_size(progress_fill, 1, kUpdateButtonHeight);
  lv_obj_set_pos(progress_fill, 0, 0);
  lv_obj_set_style_bg_color(
      progress_fill, lv_color_hex(kDetailBlueColor), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(progress_fill, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_border_width(progress_fill, 0, LV_PART_MAIN);
  lv_obj_set_style_radius(progress_fill, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(progress_fill, 0, LV_PART_MAIN);

  lv_obj_t* label = CreateLabel(
      button, "Download firmware", lv_color_hex(0xFFFFFF), Font28());
  if (label == nullptr) {
    return false;
  }
  lv_obj_center(label);
  state->firmware_update_download_button = button;
  state->firmware_update_progress_fill = progress_fill;
  state->firmware_update_download_button_label = label;
  const int action_width =
      (button_width - kUpdateActionButtonGap) / 2;
  return CreateFirmwareSecondaryButton(page, action_width, "Pause",
             FirmwareUpdatePauseClickedEventCallback, state,
             &state->firmware_update_pause_button,
             &state->firmware_update_pause_button_label, false) &&
         CreateFirmwareSecondaryButton(page, action_width, "Cancel download",
             FirmwareUpdateCancelClickedEventCallback, state,
             &state->firmware_update_cancel_button,
             &state->firmware_update_cancel_button_label, true);
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
  if (state->firmware_update_log_page != nullptr ||
      state->firmware_update_log_closing) {
    CloseFirmwareUpdateLogPage(state, animated);
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
    lv_obj_t* top_page = state->firmware_update_log_page != nullptr
        ? state->firmware_update_log_page
        : state->firmware_update_page;
    lv_obj_move_to_index(top_page, -1);
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
  state->firmware_update_auto_show_new_page =
      app::RequestFirmwareUpdateCheck();
  RefreshFirmwareUpdateView(state);
  return true;
}

}  // namespace lilygo_box::ui
