/*
 * @Description: RF control app view
 * @Author: LILYGO_L
 * @Date: 2026-07-12 00:00:00
 * @LastEditTime: 2026-07-16 19:30:00
 * @License: GPL 3.0
 */
#include "ui/views/rf_view.h"

#include <algorithm>
#include <cstddef>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <cstdio>

#include "app/storage/rf_storage.h"
#include "hal/providers/rf_provider.h"
#include "hal/providers/rtc_provider.h"
#include "ui/animation/transition_animation.h"
#include "ui/resources/fonts/font_assets.h"
#include "ui/resources/fonts/icon_assets.h"
#include "ui/input/edge_back_gesture.h"
#include "ui/input/press_cancel.h"
#include "ui/widgets/navigation_drawer.h"
#include "ui/widgets/prompt/prompt_dialog.h"
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
constexpr uint32_t kSettingsSecondaryTextColor = 0x79747E;
constexpr uint32_t kOutlineVariantColor = 0xCAC4D0;
constexpr uint32_t kPressedColor = kSurfaceContainerLowColor;
constexpr uint32_t kDisabledContainerColor = 0xE4E1E6;
constexpr uint32_t kDisabledTextColor = 0xA7A2AA;
constexpr uint32_t kSendSuccessColor = 0x2E7D32;
constexpr uint32_t kSendFailureColor = 0xBA1A1A;
constexpr uint32_t kActiveIndicatorColor = 0x23A55A;
constexpr uint32_t kInactiveIndicatorColor = 0xC7C5CC;
constexpr uint32_t kInputErrorColor = 0xBA1A1A;
constexpr uint32_t kWarningColor = 0x8A4F00;
constexpr int kHeaderTop = 68;
constexpr int kListTop = 154;
constexpr int kRowHeight = 104;
constexpr int kProfileStatusIndicatorSize = 22;
constexpr int kAnimationMs = 240;
constexpr int kDeletePromptHeight = 312;
constexpr int kDeletePromptSideMargin = 34;
constexpr int kDeletePromptBottomMargin = 32;
constexpr int kDeletePromptRadius = 48;
constexpr int kDeletePromptInnerPadding = 32;
constexpr int kDeletePromptButtonGap = 20;
constexpr int kDeletePromptButtonHeight = 74;
constexpr int kProfileNameActionX = 158;
constexpr int kProfileNameActionRightMargin = 18;
constexpr int kProfileNameActionHeight = 58;
constexpr int kProfileNameActionHorizontalPadding = 12;
constexpr int kDefaultSpreadingFactorIndex = 7;
constexpr int kAddPageHeaderHeight = 232;
constexpr int kAddPageActionHeight = 124;
constexpr int kAddKeyboardHeightPercent = 35;
constexpr int kAddKeyboardTopGap = 12;
constexpr int kAddInputHeight = 70;
constexpr int kAddSwitchRowHeight = 108;
constexpr int kAddSwitchRowGap = 12;
constexpr int kProfileNameEditButtonSize = 62;
constexpr int kProfileNameEditButtonTop = 66;
constexpr int kProfileNameEditButtonSide = 18;
constexpr int kProfileNameEditTitleTop = 170;
constexpr int kProfileNameEditTextAreaTop = 280;
constexpr int kProfileNameEditTextAreaHeight = 88;
constexpr int kProfileNameEditTextAreaSide = 26;
constexpr int kProfileNameEditHelpTop = 378;
constexpr int kProfileNameEditKeyboardHeightPercent = 35;
constexpr int kProfileSwitchWidth = 78;
constexpr int kProfileSwitchHeight = 44;
constexpr uint32_t kProfileSwitchAnimationMs = 180;
constexpr lv_style_selector_t kProfileSwitchCheckedIndicatorSelector =
    static_cast<lv_style_selector_t>(LV_PART_INDICATOR) |
    static_cast<lv_style_selector_t>(LV_STATE_CHECKED);
constexpr uint32_t kActivationRetryPeriodMs = 2000;
constexpr uint8_t kActivationRetryLimit = 10;
constexpr char kFrequencyAcceptedChars[] = "0123456789.";
constexpr char kIntegerAcceptedChars[] = "0123456789";
constexpr char kHexAcceptedChars[] = "0123456789abcdefABCDEF";
constexpr char kProfileNameAcceptedChars[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_. ";
constexpr char kProfileCreatedMessage[] = "RF profile created";
constexpr char kSettingsChangedMessage[] = "Settings changed";

/**
 * @brief 将字符串安全复制到固定长度缓冲区
 * @param destination 目标缓冲区
 * @param destination_size 目标缓冲区大小
 * @param source 源字符串
 */
void CopyBoundedString(
    char* destination, size_t destination_size, const char* source) {
  if (destination == nullptr || destination_size == 0) {
    return;
  }
  if (source == nullptr) {
    destination[0] = '\0';
    return;
  }
  const size_t copy_size =
      std::min(std::strlen(source), destination_size - 1);
  std::memmove(destination, source, copy_size);
  destination[copy_size] = '\0';
}

struct RfModuleItem {
  const char* short_name;
  const char* name;
  const char* latest_message;
  const char* time;
  uint32_t color;
  uint16_t unread_count = 0;
};

constexpr size_t kRfModuleCapacity = app::kRfProfileCapacity;
constexpr size_t kChatMessageCapacity = 48;
constexpr size_t kChatTextCapacity = hal::kRfPayloadCapacity * 2 + 1;

enum class ChatDeliveryState {
  kReceived,
  kSending,
  kSent,
  kFailed,
};

enum class ChatMessageType {
  kUser,
  kSystem,
};

struct RfChatMessage {
  uint32_t profile_id = 0;
  char text[kChatTextCapacity] = {};
  char time[16] = {};
  ChatMessageType type = ChatMessageType::kUser;
  ChatDeliveryState delivery = ChatDeliveryState::kReceived;
  int8_t rssi_dbm = 0;
  int8_t snr_db = 0;
};

struct RfViewState {
  AppViewConfig config;
  hal::RfCapabilities capabilities;
  lv_obj_t* root = nullptr;
  lv_obj_t* detail_page = nullptr;
  lv_obj_t* profile_settings_page = nullptr;
  lv_obj_t* profile_settings_active_switch = nullptr;
  lv_obj_t* profile_settings_name_label = nullptr;
  lv_obj_t* profile_settings_header_status_label = nullptr;
  lv_obj_t* profile_name_edit_page = nullptr;
  lv_obj_t* profile_name_edit_text_area = nullptr;
  lv_obj_t* profile_name_edit_keyboard = nullptr;
  lv_obj_t* detail_input = nullptr;
  lv_obj_t* detail_keyboard = nullptr;
  lv_obj_t* detail_composer_background = nullptr;
  lv_obj_t* detail_divider = nullptr;
  lv_obj_t* detail_send_button = nullptr;
  lv_obj_t* add_page = nullptr;
  lv_obj_t* add_body = nullptr;
  lv_obj_t* add_frequency_input = nullptr;
  lv_obj_t* add_power_input = nullptr;
  lv_obj_t* add_preamble_input = nullptr;
  lv_obj_t* add_sync_word_input = nullptr;
  lv_obj_t* add_crc_switch = nullptr;
  lv_obj_t* add_iq_switch = nullptr;
  lv_obj_t* add_rx_boost_switch = nullptr;
  lv_obj_t* add_active_switch = nullptr;
  lv_obj_t* add_keyboard = nullptr;
  lv_obj_t* add_submit_button = nullptr;
  lv_obj_t* add_submit_label = nullptr;
  lv_obj_t* add_button = nullptr;
  PromptDialogState delete_dialog;
  lv_obj_t* module_list = nullptr;
  lv_obj_t* header_area = nullptr;
  lv_obj_t* detail_chat_body = nullptr;
  lv_obj_t* detail_status_label = nullptr;
  lv_obj_t* detail_title_label = nullptr;
  lv_obj_t* detail_notice_label = nullptr;
  lv_timer_t* radio_timer = nullptr;
  lv_obj_t* add_chip_buttons[hal::kRfCapabilityCapacity] = {};
  lv_obj_t* add_protocol_buttons[hal::kRfCapabilityCapacity] = {};
  lv_obj_t* add_sf_buttons[8] = {};
  lv_obj_t* add_bandwidth_buttons[4] = {};
  lv_obj_t* add_coding_rate_buttons[4] = {};
  NavigationDrawerState drawer;
  EdgeBackSwipeState selection_edge_swipe = {};
  EdgeBackSwipeState detail_edge_swipe = {};
  EdgeBackSwipeState profile_settings_edge_swipe = {};
  EdgeBackSwipeState profile_name_edit_edge_swipe = {};
  EdgeBackSwipeState add_edge_swipe = {};
  RfModuleItem modules[kRfModuleCapacity] = {};
  char latest_messages[kRfModuleCapacity][96] = {};
  char message_times[kRfModuleCapacity][16] = {};
  uint16_t unread_counts[kRfModuleCapacity] = {};
  bool selected_modules[kRfModuleCapacity] = {};
  app::RfPreferences preferences;
  RfChatMessage chat_messages[kChatMessageCapacity] = {};
  size_t chat_message_count = 0;
  size_t module_count = 0;
  int selected_add_chip = 0;
  int selected_add_protocol = 0;
  int selected_add_sf = kDefaultSpreadingFactorIndex;
  int selected_add_bandwidth = 1;
  int selected_add_coding_rate = 0;
  size_t detail_index = kRfModuleCapacity;
  size_t profile_settings_index = kRfModuleCapacity;
  size_t editing_index = kRfModuleCapacity;
  uint32_t last_activation_retry_tick = 0;
  uint8_t activation_retry_count = 0;
  bool selection_mode = false;
  bool detail_closing = false;
  bool profile_settings_closing = false;
  bool profile_name_edit_closing = false;
  bool add_closing = false;
};

struct RfModuleAction {
  RfViewState* state = nullptr;
  size_t index = 0;
  bool long_press_handled = false;
};

enum class RfAddOptionGroup {
  kChip,
  kProtocol,
  kSpreadingFactor,
  kBandwidth,
  kCodingRate,
};

struct RfAddOptionAction {
  RfViewState* state = nullptr;
  RfAddOptionGroup group = RfAddOptionGroup::kChip;
  int index = 0;
};

bool RenderModuleList(RfViewState* state);
bool RenderHeader(RfViewState* state);
void CloseSelectionMode(RfViewState* state);
bool ShowAddModulePage(RfViewState* state);
bool ShowModuleSettings(RfViewState* state, size_t index,
    bool from_detail);
bool ShowProfileSettingsPage(RfViewState* state, size_t index);
bool ShowProfileNameEditPage(RfViewState* state);
void RefreshProfileSettingsPage(RfViewState* state);

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

const lv_font_t* Font32() { return &lvgl_font_google_sans_flex_32; }

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

hal::RfRadioConfig ToRadioConfig(const app::RfProfile& profile) {
  return {
      .client_token = profile.id,
      .chip = profile.chip,
      .protocol = profile.protocol,
      .lora = {
          .frequency_hz = profile.frequency_hz,
          .bandwidth_hz = profile.bandwidth_hz,
          .preamble_length = profile.preamble_length,
          .spreading_factor = profile.spreading_factor,
          .coding_rate_denominator = profile.coding_rate_denominator,
          .sync_word = profile.sync_word,
          .output_power_dbm = profile.output_power_dbm,
          .crc_enabled = profile.crc_enabled,
          .invert_iq = profile.invert_iq,
          .rx_boosted = profile.rx_boosted,
      },
  };
}

const char* ChipDisplayName(rf::ChipType chip) {
  switch (chip) {
    case rf::ChipType::kSx1262:
      return "SX1262";
    default:
      return "Unknown chip";
  }
}

const char* ChipShortName(rf::ChipType chip) {
  return chip == rf::ChipType::kSx1262 ? "SX" : "RF";
}

const char* ProtocolDisplayName(rf::ProtocolType protocol) {
  switch (protocol) {
    case rf::ProtocolType::kLora:
      return "LoRa";
    default:
      return "Unknown protocol";
  }
}

/**
 * @brief 判断两个射频配置的可编辑内容是否一致
 * @param lhs 第一个射频配置
 * @param rhs 第二个射频配置
 * @return 可编辑内容完全一致时返回 true
 */
bool AreProfileSettingsEqual(
    const app::RfProfile& lhs, const app::RfProfile& rhs) {
  return std::strcmp(lhs.name, rhs.name) == 0 &&
         lhs.chip == rhs.chip && lhs.protocol == rhs.protocol &&
         lhs.frequency_hz == rhs.frequency_hz &&
         lhs.bandwidth_hz == rhs.bandwidth_hz &&
         lhs.preamble_length == rhs.preamble_length &&
         lhs.spreading_factor == rhs.spreading_factor &&
         lhs.coding_rate_denominator == rhs.coding_rate_denominator &&
         lhs.sync_word == rhs.sync_word &&
         lhs.output_power_dbm == rhs.output_power_dbm &&
         lhs.crc_enabled == rhs.crc_enabled &&
         lhs.invert_iq == rhs.invert_iq &&
         lhs.rx_boosted == rhs.rx_boosted;
}

bool IsProfileSupported(
    const RfViewState* state, const app::RfProfile& profile) {
  if (state == nullptr) {
    return false;
  }
  for (size_t index = 0; index < state->capabilities.count; ++index) {
    const hal::RfCapability& capability =
        state->capabilities.entries[index];
    if (capability.chip == profile.chip &&
        capability.protocol == profile.protocol) {
      return true;
    }
  }
  return false;
}

size_t FindProfileIndex(const RfViewState* state, uint32_t profile_id) {
  if (state == nullptr) {
    return kRfModuleCapacity;
  }
  for (size_t index = 0; index < state->module_count; ++index) {
    if (state->preferences.profiles[index].id == profile_id) {
      return index;
    }
  }
  return kRfModuleCapacity;
}

void FormatCurrentTime(const RfViewState* state, char* output,
    size_t output_size) {
  if (output == nullptr || output_size == 0) {
    return;
  }
  hal::RtcStatus status;
  if (state != nullptr && state->config.rtc != nullptr &&
      state->config.rtc->ReadRtcStatus(&status) && status.ready) {
    std::snprintf(output, output_size, "%02u:%02u",
        static_cast<unsigned>(status.hour),
        static_cast<unsigned>(status.minute));
    return;
  }
  std::snprintf(output, output_size, "Now");
}

const char* ProfileStatusText(const RfViewState* state, size_t index) {
  if (state == nullptr || index >= state->module_count) {
    return "Inactive";
  }
  if (!IsProfileSupported(
          state, state->preferences.profiles[index])) {
    return "Unsupported";
  }
  if (state->preferences.profiles[index].id !=
          state->preferences.active_profile_id) {
    return "Inactive";
  }
  hal::RfStatus status;
  if (state->config.rf == nullptr ||
      !state->config.rf->ReadRfStatus(&status) ||
      status.state == hal::RfLinkState::kChipError) {
    return "Chip error";
  }
  return status.state == hal::RfLinkState::kActive &&
      status.active_client_token ==
          state->preferences.profiles[index].id
      ? "Active"
      : "Inactive";
}

uint32_t ProfileStatusColor(const char* status) {
  if (status != nullptr && std::strcmp(status, "Active") == 0) {
    return kSendSuccessColor;
  }
  if (status != nullptr && std::strcmp(status, "Chip error") == 0) {
    return kSendFailureColor;
  }
  if (status != nullptr && std::strcmp(status, "Unsupported") == 0) {
    return kWarningColor;
  }
  return kSecondaryTextColor;
}

/**
 * @brief 获取射频主列表状态标记颜色
 * @param state 射频页面状态
 * @param index 射频配置索引
 * @return 当前配置对应的状态标记颜色
 */
uint32_t ProfileIndicatorColor(
    const RfViewState* state, size_t index) {
  const char* status = ProfileStatusText(state, index);
  if (std::strcmp(status, "Active") == 0) {
    return kActiveIndicatorColor;
  }
  if (std::strcmp(status, "Chip error") == 0) {
    return kSendFailureColor;
  }
  return kInactiveIndicatorColor;
}

void SyncModuleItems(RfViewState* state) {
  if (state == nullptr) {
    return;
  }
  state->module_count = state->preferences.profile_count;
  for (size_t index = 0; index < state->module_count; ++index) {
    state->modules[index] = {
        .short_name = ChipShortName(
            state->preferences.profiles[index].chip),
        .name = state->preferences.profiles[index].name,
        .latest_message = state->latest_messages[index][0] == '\0'
            ? nullptr
            : state->latest_messages[index],
        .time = state->message_times[index][0] == '\0'
            ? ""
            : state->message_times[index],
        .color = 0x006B5F,
        .unread_count = state->unread_counts[index],
    };
  }
}

/**
 * @brief 释放射频页面状态
 * @param event LVGL 事件对象
 */
void RfViewDeleteEventCallback(lv_event_t* event) {
  if (lv_event_get_code(event) != LV_EVENT_DELETE) {
    return;
  }
  auto* state = static_cast<RfViewState*>(lv_event_get_user_data(event));
  if (state != nullptr && state->radio_timer != nullptr) {
    lv_timer_delete(state->radio_timer);
    state->radio_timer = nullptr;
  }
  delete state;
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
    state->detail_chat_body = nullptr;
    state->detail_status_label = nullptr;
    state->detail_title_label = nullptr;
    state->detail_notice_label = nullptr;
    state->detail_index = kRfModuleCapacity;
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
    state->detail_chat_body = nullptr;
    state->detail_status_label = nullptr;
    state->detail_title_label = nullptr;
    state->detail_notice_label = nullptr;
    state->detail_index = kRfModuleCapacity;
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

void DetailHeaderClickedEventCallback(lv_event_t* event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
    return;
  }
  auto* state = static_cast<RfViewState*>(lv_event_get_user_data(event));
  if (state != nullptr && state->detail_index < state->module_count) {
    ShowProfileSettingsPage(state, state->detail_index);
  }
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
bool CreateSendStatus(lv_obj_t* parent, const char* time,
    ChatDeliveryState delivery, int y) {
  if (parent == nullptr || time == nullptr) {
    return false;
  }
  const bool sending = delivery == ChatDeliveryState::kSending;
  const bool success = delivery == ChatDeliveryState::kSent;
  if (!sending) {
    lv_obj_t* icon_label = CreateLabel(parent,
        success ? icon::kCheck : icon::kClose,
        success ? kSendSuccessColor : kSendFailureColor,
        FillIconFont32());
    if (icon_label == nullptr) {
      return false;
    }
    lv_obj_align(icon_label, LV_ALIGN_TOP_RIGHT, -28, y - 5);
  }
  const char* prefix = sending ? "Sending  " : "";
  char status_text[32] = {};
  std::snprintf(status_text, sizeof(status_text), "%s%s", prefix, time);
  lv_obj_t* time_label = CreateLabel(
      parent, status_text, kSecondaryTextColor, Font22());
  if (time_label == nullptr) {
    return false;
  }
  lv_obj_align(time_label, LV_ALIGN_TOP_RIGHT,
      sending ? -28 : -66, y);
  return true;
}

/**
 * @brief 将聊天消息区域滚动到最后一条消息
 * @param body 聊天消息区域
 */
void ScrollChatToBottom(lv_obj_t* body) {
  if (body == nullptr) {
    return;
  }
  lv_obj_update_layout(body);
  const int32_t bottom =
      lv_obj_get_scroll_top(body) + lv_obj_get_scroll_bottom(body);
  lv_obj_scroll_to_y(body, bottom, LV_ANIM_OFF);
  lv_obj_invalidate(body);
}

/**
 * @brief 创建聊天时间线中的系统提示
 * @param parent 聊天消息区域
 * @param text 提示文本
 * @param y 顶部坐标
 * @param page_width 页面宽度
 * @param message_height 实际提示高度输出
 * @return 创建成功返回 true，否则返回 false
 */
bool CreateSystemMessage(lv_obj_t* parent, const char* text, int y,
    int page_width, int* message_height) {
  if (parent == nullptr || text == nullptr || message_height == nullptr) {
    return false;
  }
  lv_obj_t* notice_box = lv_obj_create(parent);
  if (notice_box == nullptr) {
    return false;
  }
  lv_obj_remove_flag(notice_box, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_color(notice_box,
      lv_color_hex(kNoticeContainerColor), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(notice_box, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_border_width(notice_box, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(notice_box, 0, LV_PART_MAIN);
  lv_obj_t* notice_label = CreateLabel(
      notice_box, text, kSecondaryTextColor, Font22());
  if (notice_label == nullptr) {
    return false;
  }
  lv_obj_set_width(notice_label, LV_SIZE_CONTENT);
  lv_label_set_long_mode(notice_label, LV_LABEL_LONG_WRAP);
  lv_obj_update_layout(notice_label);
  const int32_t max_label_width = std::max<int32_t>(
      1, static_cast<int32_t>(page_width) - 92);
  const int32_t label_width = std::min<int32_t>(
      lv_obj_get_width(notice_label), max_label_width);
  lv_obj_set_width(notice_label, label_width);
  lv_obj_update_layout(notice_label);
  const int32_t box_width = label_width + 28;
  const int32_t box_height = lv_obj_get_height(notice_label) + 16;
  lv_obj_set_size(notice_box, box_width, box_height);
  lv_obj_align(notice_box, LV_ALIGN_TOP_MID, 0, y);
  lv_obj_set_style_radius(notice_box, 21, LV_PART_MAIN);
  lv_obj_set_style_text_align(
      notice_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
  lv_obj_center(notice_label);
  *message_height = static_cast<int>(box_height);
  return true;
}

bool RenderChatMessages(RfViewState* state) {
  if (state == nullptr || state->detail_chat_body == nullptr ||
      state->detail_index >= state->module_count) {
    return false;
  }
  lv_obj_t* body = state->detail_chat_body;
  lv_obj_clean(body);
  const app::RfProfile& profile =
      state->preferences.profiles[state->detail_index];
  int chat_y = 18;
  for (size_t index = 0; index < state->chat_message_count; ++index) {
    const RfChatMessage& message = state->chat_messages[index];
    if (message.profile_id != profile.id) {
      continue;
    }
    if (message.type == ChatMessageType::kSystem) {
      int message_height = 0;
      if (!CreateSystemMessage(
              body, message.text, chat_y, state->config.width,
              &message_height)) {
        return false;
      }
      chat_y += message_height + 18;
      continue;
    }
    const bool outgoing = message.delivery != ChatDeliveryState::kReceived;
    int bubble_height = 0;
    if (CreateChatBubble(body, message.text, chat_y,
            state->config.width - 100, outgoing,
            &bubble_height) == nullptr) {
      return false;
    }
    chat_y += bubble_height + 8;
    if (outgoing) {
      if (!CreateSendStatus(
              body, message.time, message.delivery, chat_y)) {
        return false;
      }
      chat_y += 48;
    } else {
      char rssi[32] = {};
      char snr[32] = {};
      std::snprintf(rssi, sizeof(rssi), "RSSI  %d dBm",
          static_cast<int>(message.rssi_dbm));
      std::snprintf(snr, sizeof(snr), "SNR  %+d",
          static_cast<int>(message.snr_db));
      if (!CreateReceiveTelemetry(
              body, rssi, snr, message.time, chat_y)) {
        return false;
      }
      chat_y += 76;
    }
  }
  ScrollChatToBottom(body);
  return true;
}

RfChatMessage* AppendChatMessage(RfViewState* state, uint32_t profile_id) {
  if (state == nullptr) {
    return nullptr;
  }
  if (state->chat_message_count == kChatMessageCapacity) {
    for (size_t index = 1; index < kChatMessageCapacity; ++index) {
      state->chat_messages[index - 1] = state->chat_messages[index];
    }
    --state->chat_message_count;
  }
  RfChatMessage* message =
      &state->chat_messages[state->chat_message_count++];
  *message = RfChatMessage();
  message->profile_id = profile_id;
  FormatCurrentTime(state, message->time, sizeof(message->time));
  return message;
}

/**
 * @brief 追加系统提示并更新射频主页面的最新消息
 * @param state 射频页面状态
 * @param profile_index 射频配置索引
 * @param text 系统提示文本
 * @return 追加成功返回 true，否则返回 false
 */
bool AppendSystemMessage(RfViewState* state, size_t profile_index,
    const char* text) {
  if (state == nullptr || text == nullptr ||
      profile_index >= state->preferences.profile_count) {
    return false;
  }
  RfChatMessage* message = AppendChatMessage(
      state, state->preferences.profiles[profile_index].id);
  if (message == nullptr) {
    return false;
  }
  message->type = ChatMessageType::kSystem;
  CopyBoundedString(message->text, sizeof(message->text), text);
  CopyBoundedString(state->latest_messages[profile_index],
      sizeof(state->latest_messages[profile_index]), message->text);
  CopyBoundedString(state->message_times[profile_index],
      sizeof(state->message_times[profile_index]), message->time);
  return true;
}

void FailPendingMessages(RfViewState* state, uint32_t profile_id) {
  if (state == nullptr || profile_id == 0) {
    return;
  }
  for (size_t index = 0; index < state->chat_message_count; ++index) {
    RfChatMessage& message = state->chat_messages[index];
    if (message.profile_id == profile_id &&
        message.delivery == ChatDeliveryState::kSending) {
      message.delivery = ChatDeliveryState::kFailed;
    }
  }
}

void UpdateDetailStatus(RfViewState* state) {
  if (state == nullptr) {
    return;
  }
  if (state->detail_status_label != nullptr &&
      state->detail_index < state->module_count) {
    const char* status = ProfileStatusText(state, state->detail_index);
    lv_label_set_text(state->detail_status_label, status);
    lv_obj_set_style_text_color(state->detail_status_label,
        lv_color_hex(ProfileStatusColor(status)), LV_PART_MAIN);
  }
  if (state->profile_settings_header_status_label != nullptr &&
      state->profile_settings_index < state->module_count) {
    const char* status =
        ProfileStatusText(state, state->profile_settings_index);
    const lv_color_t status_color =
        lv_color_hex(ProfileStatusColor(status));
    lv_label_set_text(
        state->profile_settings_header_status_label, status);
    lv_obj_set_style_text_color(
        state->profile_settings_header_status_label,
        status_color, LV_PART_MAIN);
  }
}

void FormatPacketText(const hal::RfEvent& event, char* output,
    size_t output_size) {
  if (output == nullptr || output_size == 0) {
    return;
  }
  bool printable = event.payload_size > 0;
  for (size_t index = 0; index < event.payload_size; ++index) {
    if (!std::isprint(event.payload[index]) &&
        !std::isspace(event.payload[index])) {
      printable = false;
      break;
    }
  }
  if (printable) {
    const size_t length = std::min(event.payload_size, output_size - 1);
    std::memcpy(output, event.payload, length);
    output[length] = '\0';
    return;
  }
  size_t written = 0;
  for (size_t index = 0; index < event.payload_size; ++index) {
    const int result = std::snprintf(output + written,
        output_size - written, index == 0 ? "%02X" : " %02X",
        static_cast<unsigned>(event.payload[index]));
    if (result <= 0 || static_cast<size_t>(result) >=
        output_size - written) {
      break;
    }
    written += static_cast<size_t>(result);
  }
}

void RadioTimerCallback(lv_timer_t* timer) {
  auto* state = static_cast<RfViewState*>(lv_timer_get_user_data(timer));
  if (state == nullptr || state->config.rf == nullptr ||
      state->preferences.active_profile_id == 0) {
    return;
  }
  hal::RfStatus status;
  if (state->config.rf->ReadRfStatus(&status) &&
      status.state == hal::RfLinkState::kChipError &&
      state->activation_retry_count < kActivationRetryLimit &&
      lv_tick_get() - state->last_activation_retry_tick >=
          kActivationRetryPeriodMs) {
    const size_t retry_index = FindProfileIndex(
        state, state->preferences.active_profile_id);
    state->last_activation_retry_tick = lv_tick_get();
    ++state->activation_retry_count;
    if (retry_index < state->module_count &&
        IsProfileSupported(
            state, state->preferences.profiles[retry_index])) {
      state->config.rf->ActivateRf(ToRadioConfig(
          state->preferences.profiles[retry_index]));
      UpdateDetailStatus(state);
      RenderModuleList(state);
    }
  }
  hal::RfEvent event;
  state->config.rf->PollRfEvent(&event);
  if (event.type == hal::RfEventType::kNone) {
    return;
  }
  const uint32_t profile_id = event.client_token;
  const size_t profile_index = FindProfileIndex(state, profile_id);
  if (profile_index >= state->module_count) {
    return;
  }
  if (event.type == hal::RfEventType::kTransmitComplete ||
      event.type == hal::RfEventType::kTransmitFailed) {
    for (size_t offset = 0; offset < state->chat_message_count; ++offset) {
      RfChatMessage& message = state->chat_messages[
          state->chat_message_count - offset - 1];
      if (message.profile_id == profile_id &&
          message.delivery == ChatDeliveryState::kSending) {
        message.delivery = event.type == hal::RfEventType::kTransmitComplete
            ? ChatDeliveryState::kSent
            : ChatDeliveryState::kFailed;
        break;
      }
    }
  } else if (event.type == hal::RfEventType::kPacketReceived) {
    RfChatMessage* message = AppendChatMessage(state, profile_id);
    if (message != nullptr) {
      message->delivery = ChatDeliveryState::kReceived;
      message->rssi_dbm = event.rssi_dbm;
      message->snr_db = event.snr_db;
      FormatPacketText(event, message->text, sizeof(message->text));
      CopyBoundedString(state->latest_messages[profile_index],
          sizeof(state->latest_messages[profile_index]), message->text);
      CopyBoundedString(state->message_times[profile_index],
          sizeof(state->message_times[profile_index]), message->time);
      if (state->detail_page == nullptr ||
          state->detail_index != profile_index) {
        if (state->unread_counts[profile_index] < UINT16_MAX) {
          ++state->unread_counts[profile_index];
        }
      }
    }
  }
  SyncModuleItems(state);
  RefreshProfileSettingsPage(state);
  UpdateDetailStatus(state);
  if (state->detail_page != nullptr &&
      state->detail_index == profile_index) {
    RenderChatMessages(state);
  }
  RenderModuleList(state);
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

void DetailSendClickedEventCallback(lv_event_t* event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
    return;
  }
  lv_event_stop_bubbling(event);
  lv_event_stop_processing(event);
  auto* state = static_cast<RfViewState*>(lv_event_get_user_data(event));
  if (state == nullptr || state->detail_input == nullptr ||
      state->detail_index >= state->module_count) {
    return;
  }
  const char* text = lv_textarea_get_text(state->detail_input);
  const size_t length = text == nullptr ? 0 : std::strlen(text);
  const app::RfProfile& profile =
      state->preferences.profiles[state->detail_index];
  if (length == 0 || length > hal::kRfPayloadCapacity ||
      profile.id != state->preferences.active_profile_id) {
    return;
  }
  RfChatMessage* message = AppendChatMessage(state, profile.id);
  if (message == nullptr) {
    return;
  }
  CopyBoundedString(message->text, sizeof(message->text), text);
  message->delivery = ChatDeliveryState::kSending;
  const bool started = state->config.rf != nullptr &&
      state->config.rf->SendRf(
          reinterpret_cast<const uint8_t*>(text), length);
  if (!started) {
    message->delivery = ChatDeliveryState::kFailed;
  }
  CopyBoundedString(state->latest_messages[state->detail_index],
      sizeof(state->latest_messages[state->detail_index]), text);
  FormatCurrentTime(state, state->message_times[state->detail_index],
      sizeof(state->message_times[state->detail_index]));
  lv_textarea_set_text(state->detail_input, "");
  SyncModuleItems(state);
  RenderChatMessages(state);
  RenderModuleList(state);
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
      state->detail_send_button == nullptr ||
      state->detail_chat_body == nullptr) {
    return;
  }
  const int keyboard_height = state->config.height *
      kAddKeyboardHeightPercent / 100;
  const int offset = visible ? keyboard_height : 0;
  const int composer_top = state->config.height - 108 - offset;
  lv_obj_set_y(state->detail_composer_background,
      composer_top);
  lv_obj_set_y(state->detail_divider,
      composer_top);
  lv_obj_set_y(state->detail_input,
      state->config.height - 89 - offset);
  lv_obj_set_y(state->detail_send_button,
      state->config.height - 87 - offset);
  const int32_t chat_height = std::max<int32_t>(
      0, static_cast<int32_t>(composer_top) -
             lv_obj_get_y(state->detail_chat_body));
  lv_obj_set_height(state->detail_chat_body, chat_height);
  ScrollChatToBottom(state->detail_chat_body);
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
  } else if (code == LV_EVENT_READY || code == LV_EVENT_CANCEL) {
    SetDetailKeyboardVisible(state, false);
  }
}

/**
 * @brief 处理聊天背景点击并隐藏输入键盘
 * @param event LVGL 事件对象
 */
void DetailKeyboardDismissClickedEventCallback(lv_event_t* event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
    return;
  }
  auto* state = static_cast<RfViewState*>(lv_event_get_user_data(event));
  if (state == nullptr || state->detail_input == nullptr) {
    return;
  }
  lv_obj_remove_state(state->detail_input, LV_STATE_FOCUSED);
  SetDetailKeyboardVisible(state, false);
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
  lv_obj_add_flag(background, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_size(background, state->config.width, 108);
  lv_obj_set_pos(background, 0, divider_y);
  lv_obj_set_style_bg_color(background,
      lv_color_hex(kMainBackgroundColor), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(background, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_border_width(background, 0, LV_PART_MAIN);
  lv_obj_set_style_radius(background, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(background, 0, LV_PART_MAIN);
  state->detail_composer_background = background;
  lv_obj_add_event_cb(background,
      DetailKeyboardDismissClickedEventCallback, LV_EVENT_CLICKED, state);

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
  lv_textarea_set_placeholder_text(input, "Enter a message to send...");
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
  lv_obj_add_event_cb(send, DetailSendClickedEventCallback,
      LV_EVENT_CLICKED, state);

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
  state->detail_index = index;
  state->unread_counts[index] = 0;
  SyncModuleItems(state);
  RenderModuleList(state);
  state->detail_input = nullptr;
  state->detail_keyboard = nullptr;
  state->detail_composer_background = nullptr;
  state->detail_divider = nullptr;
  state->detail_send_button = nullptr;
  state->detail_title_label = nullptr;
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
  lv_obj_t* header_action = lv_button_create(page);
  if (header_action != nullptr) {
    lv_obj_remove_style_all(header_action);
    lv_obj_set_size(header_action, state->config.width - 90, 76);
    lv_obj_set_pos(header_action, 88, 60);
    lv_obj_set_style_bg_opa(
        header_action, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_add_event_cb(header_action, DetailHeaderClickedEventCallback,
        LV_EVENT_CLICKED, state);
    lv_obj_move_to_index(header_action, -1);
  }
  lv_obj_t* title = CreateLabel(
      page, item.name, kMainTextColor, Font28());
  if (title != nullptr) {
    state->detail_title_label = title;
    lv_obj_set_width(title, state->config.width - 190);
    lv_label_set_long_mode(title, LV_LABEL_LONG_DOT);
    lv_obj_set_pos(title, 170, 72);
  }
  const char* status_text = ProfileStatusText(state, index);
  lv_obj_t* status = CreateLabel(page, status_text,
      ProfileStatusColor(status_text), Font22());
  if (status != nullptr) {
    state->detail_status_label = status;
    lv_obj_set_pos(status, 170, 108);
  }

  const int composer_top = state->config.height - 108;
  const int chat_top = 146;
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
  lv_obj_add_event_cb(chat_body,
      DetailKeyboardDismissClickedEventCallback, LV_EVENT_CLICKED, state);
  state->detail_chat_body = chat_body;

  if (!RenderChatMessages(state) || !CreateChatComposer(page, state)) {
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
  if (action == nullptr || action->state == nullptr) {
    return;
  }
  if (action->long_press_handled) {
    action->long_press_handled = false;
    return;
  }
  RfViewState* state = action->state;
  if (state->selection_mode) {
    state->selected_modules[action->index] =
        !state->selected_modules[action->index];
    bool any_selected = false;
    for (size_t index = 0; index < state->module_count; ++index) {
      any_selected = any_selected || state->selected_modules[index];
    }
    if (any_selected) {
      RenderHeader(state);
      RenderModuleList(state);
    } else {
      CloseSelectionMode(state);
    }
  } else {
    ShowModuleDetail(state, action->index);
  }
}

void ModuleRowLongPressedEventCallback(lv_event_t* event) {
  if (lv_event_get_code(event) != LV_EVENT_LONG_PRESSED) {
    return;
  }
  auto* action = static_cast<RfModuleAction*>(
      lv_event_get_user_data(event));
  if (action == nullptr || action->state == nullptr ||
      action->index >= action->state->module_count) {
    return;
  }
  action->long_press_handled = true;
  action->state->selection_edge_swipe = EdgeBackSwipeState();
  action->state->selection_mode = true;
  action->state->selected_modules[action->index] = true;
  RenderHeader(action->state);
  RenderModuleList(action->state);
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
 * @brief 创建射频模块头像、状态标记和选中标记
 * @param row 模块列表行
 * @param item 模块数据
 * @param state 射频页面状态
 * @param index 射频配置索引
 * @return 创建成功返回 true，否则返回 false
 */
bool CreateModuleAvatar(lv_obj_t* row, const RfModuleItem& item,
    const RfViewState* state, size_t index) {
  lv_obj_t* status_indicator = lv_obj_create(row);
  if (status_indicator == nullptr) {
    return false;
  }
  lv_obj_remove_flag(status_indicator, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(status_indicator, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_size(status_indicator, kProfileStatusIndicatorSize,
      kProfileStatusIndicatorSize);
  lv_obj_align(status_indicator, LV_ALIGN_LEFT_MID,
      -kProfileStatusIndicatorSize / 2, 0);
  lv_obj_set_style_radius(status_indicator,
      kProfileStatusIndicatorSize / 2, LV_PART_MAIN);
  lv_obj_set_style_bg_color(status_indicator,
      lv_color_hex(ProfileIndicatorColor(state, index)), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(status_indicator, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_border_width(status_indicator, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(status_indicator, 0, LV_PART_MAIN);

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
  if (state != nullptr && state->selection_mode &&
      state->selected_modules[index]) {
    lv_obj_t* selection = lv_obj_create(row);
    if (selection == nullptr) {
      return false;
    }
    lv_obj_remove_flag(selection, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(selection, 30, 30);
    lv_obj_align(selection, LV_ALIGN_LEFT_MID, 80, 25);
    lv_obj_set_style_radius(selection, 15, LV_PART_MAIN);
    lv_obj_set_style_bg_color(selection,
        lv_color_hex(kSendSuccessColor), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(selection, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(selection,
        lv_color_hex(kMainBackgroundColor), LV_PART_MAIN);
    lv_obj_set_style_border_width(selection, 3, LV_PART_MAIN);
    lv_obj_set_style_pad_all(selection, 0, LV_PART_MAIN);
    lv_obj_t* check = CreateLabel(
        selection, icon::kCheck, 0xFFFFFF, FillIconFont32());
    if (check != nullptr) {
      lv_obj_center(check);
    }
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
  lv_obj_set_style_bg_color(row, lv_color_hex(kPressedColor),
                            LV_STATE_PRESSED);
  lv_obj_set_style_bg_opa(row, LV_OPA_COVER, LV_STATE_PRESSED);
  if (!AddPressCancelOnLeave(row) ||
      !CreateModuleAvatar(row, item, state, index)) {
    lv_obj_delete(row);
    return false;
  }
  auto* action = new RfModuleAction{.state = state, .index = index};
  lv_obj_add_event_cb(row, ModuleRowClickedEventCallback,
                      LV_EVENT_CLICKED, action);
  lv_obj_add_event_cb(row, ModuleRowLongPressedEventCallback,
                      LV_EVENT_LONG_PRESSED, action);
  lv_obj_add_event_cb(row, ModuleActionDeleteEventCallback,
                      LV_EVENT_DELETE, action);
  lv_obj_t* title = CreateLabel(
      row, item.name, kMainTextColor, Font28());
  if (title != nullptr) {
    lv_obj_set_size(title, width - 250, 34);
    lv_label_set_long_mode(title, LV_LABEL_LONG_DOT);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 120, 18);
  }
  lv_obj_t* time = CreateLabel(
      row, item.time, kSecondaryTextColor, Font22());
  if (time != nullptr) {
    lv_obj_align(time, LV_ALIGN_TOP_RIGHT, -28, 20);
  }
  if (item.unread_count > 0) {
    char unread_text[12] = {};
    std::snprintf(unread_text, sizeof(unread_text), "%u",
        static_cast<unsigned>(item.unread_count));
    lv_obj_t* unread = lv_obj_create(row);
    if (unread != nullptr) {
      int unread_width = 54;
      if (item.unread_count >= 10000) {
        unread_width = 88;
      } else if (item.unread_count >= 100) {
        unread_width = 72;
      }
      lv_obj_remove_flag(unread, LV_OBJ_FLAG_SCROLLABLE);
      lv_obj_set_size(unread, unread_width, 32);
      lv_obj_align(unread, LV_ALIGN_TOP_RIGHT, -28, 54);
      lv_obj_set_style_radius(unread, 16, LV_PART_MAIN);
      lv_obj_set_style_bg_color(unread,
          lv_color_hex(kSendSuccessColor), LV_PART_MAIN);
      lv_obj_set_style_bg_opa(unread, LV_OPA_COVER, LV_PART_MAIN);
      lv_obj_set_style_border_width(unread, 0, LV_PART_MAIN);
      lv_obj_set_style_pad_all(unread, 0, LV_PART_MAIN);
      lv_obj_t* unread_label = CreateLabel(
          unread, unread_text, 0xFFFFFF, Font22());
      if (unread_label != nullptr) {
        lv_obj_center(unread_label);
      }
    }
  }
  if (item.latest_message != nullptr && item.latest_message[0] != '\0') {
    lv_obj_t* message = CreateLabel(
        row, item.latest_message, kSecondaryTextColor, Font22());
    if (message != nullptr) {
      lv_obj_set_size(message,
          width - (item.unread_count > 0 ? 230 : 174), 30);
      lv_label_set_long_mode(message, LV_LABEL_LONG_DOT);
      lv_obj_align(message, LV_ALIGN_TOP_LEFT, 120, 57);
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
  return true;
}

size_t SelectedModuleCount(const RfViewState* state) {
  size_t count = 0;
  for (size_t index = 0; state != nullptr &&
       index < state->module_count; ++index) {
    if (state->selected_modules[index]) {
      ++count;
    }
  }
  return count;
}

void CloseSelectionMode(RfViewState* state) {
  if (state == nullptr) {
    return;
  }
  state->selection_mode = false;
  state->selection_edge_swipe = EdgeBackSwipeState();
  for (bool& selected : state->selected_modules) {
    selected = false;
  }
  RenderHeader(state);
  RenderModuleList(state);
}

/**
 * @brief 处理 RF 主界面多选状态下的左右边缘滑动
 * @param event LVGL 事件对象
 */
void SelectionEdgeBackEventCallback(lv_event_t* event) {
  auto* state = static_cast<RfViewState*>(lv_event_get_user_data(event));
  if (state == nullptr || !state->selection_mode ||
      !HandleEdgeBackSwipeEvent(event, state->config.width,
          &state->selection_edge_swipe)) {
    return;
  }

  CloseSelectionMode(state);
  lv_event_stop_bubbling(event);
  lv_event_stop_processing(event);
}

void SelectionCloseClickedEventCallback(lv_event_t* event) {
  if (lv_event_get_code(event) == LV_EVENT_CLICKED) {
    CloseSelectionMode(
        static_cast<RfViewState*>(lv_event_get_user_data(event)));
  }
}

/**
 * @brief 修改指定射频配置的启用状态
 * @param state 射频页面状态
 * @param index 配置索引
 * @param active 是否启用该配置
 * @return 状态有效且更新完成时返回 true
 */
bool SetProfileActiveState(
    RfViewState* state, size_t index, bool active) {
  if (state == nullptr || index >= state->module_count) {
    return false;
  }
  app::RfProfile& profile = state->preferences.profiles[index];
  const bool currently_active =
      state->preferences.active_profile_id == profile.id;
  if (active == currently_active) {
    return true;
  }
  FailPendingMessages(state, state->preferences.active_profile_id);
  state->activation_retry_count = 0;
  state->last_activation_retry_tick = lv_tick_get();
  if (active) {
    state->preferences.active_profile_id = profile.id;
    if (state->config.rf != nullptr) {
      state->config.rf->ActivateRf(ToRadioConfig(profile));
    }
  } else {
    if (state->config.rf != nullptr) {
      state->config.rf->DeactivateRf();
    }
    state->preferences.active_profile_id = 0;
  }
  app::UpdateRfPreferences(state->preferences);
  UpdateDetailStatus(state);
  RenderModuleList(state);
  RefreshProfileSettingsPage(state);
  return true;
}

/**
 * @brief 清空射频配置名称编辑页保存的控件引用
 * @param state 射频页面状态
 */
void ResetProfileNameEditReferences(RfViewState* state) {
  if (state == nullptr) {
    return;
  }
  state->profile_name_edit_page = nullptr;
  state->profile_name_edit_text_area = nullptr;
  state->profile_name_edit_keyboard = nullptr;
  state->profile_name_edit_edge_swipe = EdgeBackSwipeState();
  state->profile_name_edit_closing = false;
}

/**
 * @brief 处理射频配置名称编辑页退出动画完成事件
 * @param animation LVGL 动画对象
 */
void ProfileNameEditCloseCompletedCallback(lv_anim_t* animation) {
  auto* state = static_cast<RfViewState*>(
      lv_anim_get_user_data(animation));
  if (state == nullptr || state->profile_name_edit_page == nullptr) {
    return;
  }
  lv_obj_t* page = state->profile_name_edit_page;
  ResetProfileNameEditReferences(state);
  lv_obj_delete(page);
}

/**
 * @brief 关闭射频配置名称编辑页
 * @param state 射频页面状态
 * @param animated 是否播放退出动画
 */
void CloseProfileNameEditPage(RfViewState* state, bool animated) {
  if (state == nullptr || state->profile_name_edit_page == nullptr ||
      state->profile_name_edit_closing) {
    return;
  }
  HideSharedKeyboard(state->profile_name_edit_keyboard);
  if (animated && StartSlideRightWindowTransition(
      state->profile_name_edit_page, state->config.width, kAnimationMs,
      state, ProfileNameEditCloseCompletedCallback)) {
    state->profile_name_edit_closing = true;
    return;
  }
  lv_obj_t* page = state->profile_name_edit_page;
  ResetProfileNameEditReferences(state);
  lv_obj_delete(page);
}

/**
 * @brief 处理射频配置名称编辑页边缘返回手势
 * @param event LVGL 事件对象
 */
void ProfileNameEditEdgeBackEventCallback(lv_event_t* event) {
  auto* state = static_cast<RfViewState*>(lv_event_get_user_data(event));
  if (state == nullptr || state->profile_name_edit_page == nullptr ||
      state->profile_name_edit_closing ||
      !HandleEdgeBackSwipeEvent(event, state->config.width,
          &state->profile_name_edit_edge_swipe)) {
    return;
  }
  CloseProfileNameEditPage(state, true);
  lv_event_stop_bubbling(event);
  lv_event_stop_processing(event);
}

/**
 * @brief 处理射频配置名称编辑页空白区域点击事件
 * @param event LVGL 事件对象
 */
void ProfileNameEditBackgroundClickedEventCallback(lv_event_t* event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED ||
      lv_event_get_target_obj(event) !=
          lv_event_get_current_target_obj(event)) {
    return;
  }
  auto* state = static_cast<RfViewState*>(lv_event_get_user_data(event));
  if (state != nullptr) {
    HideSharedKeyboard(state->profile_name_edit_keyboard);
  }
}

/**
 * @brief 处理射频配置名称编辑取消按钮点击事件
 * @param event LVGL 事件对象
 */
void ProfileNameEditCancelClickedEventCallback(lv_event_t* event) {
  if (lv_event_get_code(event) == LV_EVENT_CLICKED) {
    CloseProfileNameEditPage(
        static_cast<RfViewState*>(lv_event_get_user_data(event)), true);
  }
}

/**
 * @brief 处理射频配置名称编辑确认按钮点击事件
 * @param event LVGL 事件对象
 */
void ProfileNameEditConfirmClickedEventCallback(lv_event_t* event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
    return;
  }
  auto* state = static_cast<RfViewState*>(lv_event_get_user_data(event));
  if (state == nullptr || state->profile_name_edit_text_area == nullptr ||
      state->profile_settings_index >= state->module_count) {
    return;
  }
  const char* text = lv_textarea_get_text(
      state->profile_name_edit_text_area);
  if (text == nullptr || text[0] == '\0') {
    return;
  }
  const size_t index = state->profile_settings_index;
  app::RfProfile& profile = state->preferences.profiles[index];
  if (std::strcmp(profile.name, text) == 0) {
    CloseProfileNameEditPage(state, true);
    return;
  }
  CopyBoundedString(profile.name, sizeof(profile.name), text);
  app::UpdateRfPreferences(state->preferences);
  AppendSystemMessage(state, index, kSettingsChangedMessage);
  SyncModuleItems(state);
  if (state->detail_title_label != nullptr &&
      state->detail_index == index) {
    lv_label_set_text(state->detail_title_label, profile.name);
  }
  RefreshProfileSettingsPage(state);
  RenderModuleList(state);
  CloseProfileNameEditPage(state, true);
}

/**
 * @brief 处理射频配置资料页名称区域点击事件
 * @param event LVGL 事件对象
 */
void ProfileNameAreaClickedEventCallback(lv_event_t* event) {
  if (lv_event_get_code(event) == LV_EVENT_CLICKED) {
    ShowProfileNameEditPage(
        static_cast<RfViewState*>(lv_event_get_user_data(event)));
  }
}

/**
 * @brief 创建射频配置名称编辑页的透明工具按钮
 * @param parent 父对象
 * @param state 射频页面状态
 * @param icon_text 图标文本
 * @param x 左侧坐标
 * @param callback 点击事件回调
 * @return 创建成功返回 true，否则返回 false
 */
bool CreateProfileNameEditToolbarButton(lv_obj_t* parent,
    RfViewState* state, const char* icon_text, int x,
    lv_event_cb_t callback) {
  if (parent == nullptr || state == nullptr || icon_text == nullptr ||
      callback == nullptr) {
    return false;
  }
  lv_obj_t* button = lv_button_create(parent);
  if (button == nullptr) {
    return false;
  }
  lv_obj_remove_style_all(button);
  lv_obj_remove_flag(button, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(button, LV_OBJ_FLAG_PRESS_LOCK);
  lv_obj_add_flag(button, LV_OBJ_FLAG_GESTURE_BUBBLE);
  lv_obj_set_size(
      button, kProfileNameEditButtonSize, kProfileNameEditButtonSize);
  lv_obj_set_pos(button, x, kProfileNameEditButtonTop);
  lv_obj_set_style_bg_opa(button, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_set_style_bg_opa(button, LV_OPA_TRANSP, LV_STATE_PRESSED);
  lv_obj_set_style_border_width(button, 0, LV_PART_MAIN);
  lv_obj_set_style_outline_width(button, 0, LV_PART_MAIN);
  lv_obj_set_style_shadow_width(button, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(button, 0, LV_PART_MAIN);
  if (!AddPressCancelOnLeave(button)) {
    lv_obj_delete(button);
    return false;
  }
  lv_obj_add_event_cb(button, callback, LV_EVENT_CLICKED, state);
  lv_obj_t* icon_label = CreateLabel(
      button, icon_text, kMainTextColor, OutlineIconFont44());
  if (icon_label == nullptr) {
    lv_obj_delete(button);
    return false;
  }
  lv_obj_center(icon_label);
  return true;
}

/**
 * @brief 设置射频配置名称编辑输入框样式
 * @param text_area 文本输入框对象
 */
void ApplyProfileNameEditTextAreaStyle(lv_obj_t* text_area) {
  if (text_area == nullptr) {
    return;
  }
  lv_obj_set_scrollbar_mode(text_area, LV_SCROLLBAR_MODE_OFF);
  lv_obj_set_style_text_font(text_area, Font32(), LV_PART_MAIN);
  lv_obj_set_style_text_color(
      text_area, lv_color_hex(kMainTextColor), LV_PART_MAIN);
  lv_obj_set_style_bg_color(text_area,
      lv_color_hex(kSurfaceContainerLowColor), LV_PART_MAIN);
  lv_obj_set_style_bg_color(text_area,
      lv_color_hex(kSurfaceContainerLowColor), LV_STATE_FOCUSED);
  lv_obj_set_style_bg_opa(text_area, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_bg_opa(text_area, LV_OPA_COVER, LV_STATE_FOCUSED);
  lv_obj_set_style_border_width(text_area, 0, LV_PART_MAIN);
  lv_obj_set_style_border_width(text_area, 0, LV_STATE_FOCUSED);
  lv_obj_set_style_outline_width(text_area, 0, LV_PART_MAIN);
  lv_obj_set_style_outline_width(text_area, 0, LV_STATE_FOCUSED);
  lv_obj_set_style_shadow_width(text_area, 0, LV_PART_MAIN);
  lv_obj_set_style_radius(text_area, 22, LV_PART_MAIN);
  lv_obj_set_style_pad_left(text_area, 20, LV_PART_MAIN);
  lv_obj_set_style_pad_right(text_area, 20, LV_PART_MAIN);
  const int vertical_padding = std::max(0,
      (kProfileNameEditTextAreaHeight -
          static_cast<int>(lv_font_get_line_height(Font32()))) /
          2);
  lv_obj_set_style_pad_top(text_area, vertical_padding, LV_PART_MAIN);
  lv_obj_set_style_pad_bottom(text_area, vertical_padding, LV_PART_MAIN);
  lv_obj_t* content_label = lv_textarea_get_label(text_area);
  if (content_label != nullptr) {
    lv_obj_align(content_label, LV_ALIGN_LEFT_MID, 0, 0);
  }
}

/**
 * @brief 显示射频配置名称编辑页
 * @param state 射频页面状态
 * @return 显示成功返回 true，否则返回 false
 */
bool ShowProfileNameEditPage(RfViewState* state) {
  if (state == nullptr || state->root == nullptr ||
      state->profile_settings_page == nullptr ||
      state->profile_settings_index >= state->module_count) {
    return false;
  }
  if (state->profile_name_edit_closing) {
    return true;
  }
  if (state->profile_name_edit_page != nullptr) {
    lv_obj_move_to_index(state->profile_name_edit_page, -1);
    return true;
  }
  lv_obj_t* page = lv_obj_create(state->root);
  if (page == nullptr) {
    return false;
  }
  state->profile_name_edit_page = page;
  state->profile_name_edit_text_area = nullptr;
  state->profile_name_edit_keyboard = nullptr;
  state->profile_name_edit_closing = false;
  state->profile_name_edit_edge_swipe = EdgeBackSwipeState();
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
  AddEdgeBackSwipeEvents(page, ProfileNameEditEdgeBackEventCallback, state);
  lv_obj_add_event_cb(page, ProfileNameEditBackgroundClickedEventCallback,
      LV_EVENT_CLICKED, state);

  const int confirm_x = state->config.width -
      kProfileNameEditButtonSide - kProfileNameEditButtonSize;
  if (!CreateProfileNameEditToolbarButton(page, state, icon::kClose,
          kProfileNameEditButtonSide,
          ProfileNameEditCancelClickedEventCallback) ||
      !CreateProfileNameEditToolbarButton(page, state, icon::kCheck,
          confirm_x, ProfileNameEditConfirmClickedEventCallback)) {
    CloseProfileNameEditPage(state, false);
    return false;
  }
  lv_obj_t* title = CreateLabel(
      page, "Edit profile name", kMainTextColor, Font48());
  if (title == nullptr) {
    CloseProfileNameEditPage(state, false);
    return false;
  }
  lv_obj_align(title, LV_ALIGN_TOP_LEFT, kProfileNameEditTextAreaSide,
      kProfileNameEditTitleTop);

  lv_obj_t* text_area = lv_textarea_create(page);
  if (text_area == nullptr) {
    CloseProfileNameEditPage(state, false);
    return false;
  }
  state->profile_name_edit_text_area = text_area;
  lv_obj_add_flag(text_area, LV_OBJ_FLAG_GESTURE_BUBBLE);
  AddEdgeBackSwipeEvents(
      text_area, ProfileNameEditEdgeBackEventCallback, state);
  lv_obj_set_size(text_area,
      state->config.width - 2 * kProfileNameEditTextAreaSide,
      kProfileNameEditTextAreaHeight);
  lv_obj_align(text_area, LV_ALIGN_TOP_LEFT, kProfileNameEditTextAreaSide,
      kProfileNameEditTextAreaTop);
  lv_textarea_set_one_line(text_area, true);
  lv_textarea_set_max_length(
      text_area, app::kRfProfileNameCapacity - 1);
  lv_textarea_set_accepted_chars(text_area, kProfileNameAcceptedChars);
  lv_textarea_set_text(text_area,
      state->preferences.profiles[state->profile_settings_index].name);
  lv_textarea_set_cursor_pos(text_area, LV_TEXTAREA_CURSOR_LAST);
  ApplyProfileNameEditTextAreaStyle(text_area);

  lv_obj_t* help = CreateLabel(page,
      "This name is used to identify this RF profile.",
      kSecondaryTextColor, Font24());
  if (help == nullptr) {
    CloseProfileNameEditPage(state, false);
    return false;
  }
  lv_obj_set_width(
      help, state->config.width - 2 * (kProfileNameEditTextAreaSide + 10));
  lv_label_set_long_mode(help, LV_LABEL_LONG_WRAP);
  lv_obj_align(help, LV_ALIGN_TOP_LEFT,
      kProfileNameEditTextAreaSide + 10, kProfileNameEditHelpTop);

  SharedKeyboardConfig keyboard_config;
  keyboard_config.width = state->config.width;
  keyboard_config.height = state->config.height *
      kProfileNameEditKeyboardHeightPercent / 100;
  state->profile_name_edit_keyboard =
      CreateSharedKeyboard(page, keyboard_config);
  if (state->profile_name_edit_keyboard == nullptr ||
      !AttachSharedKeyboardToTextArea(state->profile_name_edit_keyboard,
          text_area, kProfileNameAcceptedChars)) {
    CloseProfileNameEditPage(state, false);
    return false;
  }
  lv_obj_add_flag(
      state->profile_name_edit_keyboard, LV_OBJ_FLAG_GESTURE_BUBBLE);
  AddEdgeBackSwipeEvents(state->profile_name_edit_keyboard,
      ProfileNameEditEdgeBackEventCallback, state);
  EnableEdgeBackSwipeEventBubble(page);
  if (!StartSlideLeftWindowTransition(page, state->config.width,
      kAnimationMs, state, nullptr)) {
    CloseProfileNameEditPage(state, false);
    return false;
  }
  return true;
}

/**
 * @brief 清空射频配置资料页保存的控件引用
 * @param state 射频页面状态
 */
void ResetProfileSettingsReferences(RfViewState* state) {
  if (state == nullptr) {
    return;
  }
  state->profile_settings_page = nullptr;
  state->profile_settings_active_switch = nullptr;
  state->profile_settings_name_label = nullptr;
  state->profile_settings_header_status_label = nullptr;
  state->profile_settings_index = kRfModuleCapacity;
  state->profile_settings_edge_swipe = EdgeBackSwipeState();
  state->profile_settings_closing = false;
}

/**
 * @brief 根据射频配置名称调整名称按钮宽度和滚动方式
 * @param state 射频页面状态
 */
void UpdateProfileSettingsNameLayout(RfViewState* state) {
  if (state == nullptr || state->profile_settings_name_label == nullptr) {
    return;
  }
  lv_obj_t* name_action =
      lv_obj_get_parent(state->profile_settings_name_label);
  const char* name = lv_label_get_text(state->profile_settings_name_label);
  if (name_action == nullptr || name == nullptr) {
    return;
  }

  lv_point_t text_size = {};
  lv_text_get_size(&text_size, name, Font36(),
      lv_obj_get_style_text_letter_space(
          state->profile_settings_name_label, LV_PART_MAIN),
      0, LV_COORD_MAX, LV_TEXT_FLAG_EXPAND);
  const int max_action_width = state->config.width -
      kProfileNameActionX - kProfileNameActionRightMargin;
  if (max_action_width <= 2 * kProfileNameActionHorizontalPadding) {
    return;
  }
  const int text_width = std::max(1, static_cast<int>(text_size.x));
  const int action_width = std::min(max_action_width,
      text_width + 2 * kProfileNameActionHorizontalPadding);
  const int label_width =
      action_width - 2 * kProfileNameActionHorizontalPadding;
  const bool scroll_name = text_width > label_width;
  lv_obj_set_width(name_action, action_width);
  lv_label_set_long_mode(state->profile_settings_name_label,
      scroll_name ? LV_LABEL_LONG_SCROLL_CIRCULAR : LV_LABEL_LONG_CLIP);
  lv_obj_set_width(state->profile_settings_name_label, label_width);
  lv_obj_align(state->profile_settings_name_label,
      LV_ALIGN_LEFT_MID, kProfileNameActionHorizontalPadding, 0);
}

/**
 * @brief 刷新射频配置资料页中的动态信息
 * @param state 射频页面状态
 */
void RefreshProfileSettingsPage(RfViewState* state) {
  if (state == nullptr || state->profile_settings_page == nullptr ||
      state->profile_settings_index >= state->module_count) {
    return;
  }
  const size_t index = state->profile_settings_index;
  const app::RfProfile& profile = state->preferences.profiles[index];
  if (state->profile_settings_name_label != nullptr) {
    lv_label_set_text(state->profile_settings_name_label, profile.name);
    UpdateProfileSettingsNameLayout(state);
  }
  if (state->profile_settings_header_status_label != nullptr) {
    const char* status = ProfileStatusText(state, index);
    const lv_color_t status_color =
        lv_color_hex(ProfileStatusColor(status));
    lv_label_set_text(
        state->profile_settings_header_status_label, status);
    lv_obj_set_style_text_color(
        state->profile_settings_header_status_label,
        status_color, LV_PART_MAIN);
  }
  if (state->profile_settings_active_switch != nullptr) {
    const bool active =
        state->preferences.active_profile_id == profile.id;
    if (active) {
      lv_obj_add_state(
          state->profile_settings_active_switch, LV_STATE_CHECKED);
    } else {
      lv_obj_clear_state(
          state->profile_settings_active_switch, LV_STATE_CHECKED);
    }
    if (!active && !IsProfileSupported(state, profile)) {
      lv_obj_add_state(
          state->profile_settings_active_switch, LV_STATE_DISABLED);
    } else {
      lv_obj_clear_state(
          state->profile_settings_active_switch, LV_STATE_DISABLED);
    }
  }
}

/**
 * @brief 处理射频配置资料页退出动画完成事件
 * @param animation LVGL 动画对象
 */
void ProfileSettingsCloseCompletedCallback(lv_anim_t* animation) {
  auto* state = static_cast<RfViewState*>(
      lv_anim_get_user_data(animation));
  if (state == nullptr || state->profile_settings_page == nullptr) {
    return;
  }
  lv_obj_t* page = state->profile_settings_page;
  ResetProfileSettingsReferences(state);
  lv_obj_delete(page);
}

/**
 * @brief 使用退出动画关闭射频配置资料页
 * @param state 射频页面状态
 */
void CloseProfileSettingsPage(RfViewState* state) {
  if (state == nullptr || state->profile_settings_page == nullptr ||
      state->profile_settings_closing) {
    return;
  }
  CloseProfileNameEditPage(state, false);
  state->profile_settings_closing = true;
  if (!StartSlideRightWindowTransition(state->profile_settings_page,
      state->config.width, kAnimationMs, state,
      ProfileSettingsCloseCompletedCallback)) {
    lv_obj_t* page = state->profile_settings_page;
    ResetProfileSettingsReferences(state);
    lv_obj_delete(page);
  }
}

/**
 * @brief 处理射频配置资料页返回按钮点击事件
 * @param event LVGL 事件对象
 */
void ProfileSettingsBackClickedEventCallback(lv_event_t* event) {
  if (lv_event_get_code(event) == LV_EVENT_CLICKED) {
    CloseProfileSettingsPage(
        static_cast<RfViewState*>(lv_event_get_user_data(event)));
  }
}

/**
 * @brief 处理射频配置资料页边缘返回手势
 * @param event LVGL 事件对象
 */
void ProfileSettingsEdgeBackEventCallback(lv_event_t* event) {
  auto* state = static_cast<RfViewState*>(lv_event_get_user_data(event));
  if (state == nullptr || state->profile_settings_page == nullptr ||
      !HandleEdgeBackSwipeEvent(event, state->config.width,
          &state->profile_settings_edge_swipe)) {
    return;
  }
  CloseProfileSettingsPage(state);
  lv_event_stop_bubbling(event);
  lv_event_stop_processing(event);
}

/**
 * @brief 处理射频配置启用开关变化事件
 * @param event LVGL 事件对象
 */
void ProfileSettingsActiveChangedEventCallback(lv_event_t* event) {
  if (lv_event_get_code(event) != LV_EVENT_VALUE_CHANGED) {
    return;
  }
  auto* state = static_cast<RfViewState*>(lv_event_get_user_data(event));
  if (state == nullptr ||
      state->profile_settings_index >= state->module_count) {
    return;
  }
  const bool active = lv_obj_has_state(
      lv_event_get_target_obj(event), LV_STATE_CHECKED);
  SetProfileActiveState(state, state->profile_settings_index, active);
}

/**
 * @brief 处理射频参数设置入口点击事件
 * @param event LVGL 事件对象
 */
void ProfileRfSettingsClickedEventCallback(lv_event_t* event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
    return;
  }
  auto* state = static_cast<RfViewState*>(lv_event_get_user_data(event));
  if (state != nullptr &&
      state->profile_settings_index < state->module_count) {
    ShowModuleSettings(state, state->profile_settings_index, true);
  }
}

/**
 * @brief 创建射频配置资料页中的分组标题
 * @param parent 父对象
 * @param text 标题文本
 * @param y 顶部坐标
 * @return 创建成功返回 true，否则返回 false
 */
bool CreateProfileSettingsSection(
    lv_obj_t* parent, const char* text, int y) {
  lv_obj_t* label = CreateLabel(parent, text, kPrimaryColor, Font22());
  if (label == nullptr) {
    return false;
  }
  lv_obj_set_pos(label, 28, y);
  return true;
}

/**
 * @brief 创建射频配置资料页中的列表行
 * @param parent 父对象
 * @param state 射频页面状态
 * @param title 行标题
 * @param subtitle 行副标题
 * @param y 顶部坐标
 * @param callback 可选的点击事件回调
 * @param show_chevron 是否显示右侧箭头
 * @param text_y_offset 文字组垂直偏移
 * @param height 列表行高度
 * @return 创建成功返回列表行，否则返回 nullptr
 */
lv_obj_t* CreateProfileSettingsRow(lv_obj_t* parent, RfViewState* state,
    const char* title, const char* subtitle, int y, lv_event_cb_t callback,
    bool show_chevron, int text_y_offset = 0, int height = 120) {
  if (parent == nullptr || state == nullptr || title == nullptr ||
      subtitle == nullptr) {
    return nullptr;
  }
  lv_obj_t* row = lv_button_create(parent);
  if (row == nullptr) {
    return nullptr;
  }
  lv_obj_remove_style_all(row);
  lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(row, LV_OBJ_FLAG_GESTURE_BUBBLE);
  lv_obj_set_size(row, state->config.width, height);
  lv_obj_set_pos(row, 0, y);
  lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_set_style_bg_color(
      row, lv_color_hex(kPressedColor), LV_STATE_PRESSED);
  lv_obj_set_style_bg_opa(row, LV_OPA_COVER, LV_STATE_PRESSED);
  lv_obj_set_style_radius(row, 0, LV_PART_MAIN);
  lv_obj_set_style_border_width(row, 0, LV_PART_MAIN);
  lv_obj_set_style_shadow_width(row, 0, LV_PART_MAIN);
  if (!AddPressCancelOnLeave(row)) {
    lv_obj_delete(row);
    return nullptr;
  }
  if (callback != nullptr) {
    lv_obj_add_event_cb(row, callback, LV_EVENT_CLICKED, state);
  } else {
    lv_obj_remove_flag(row, LV_OBJ_FLAG_CLICKABLE);
  }

  lv_obj_t* title_label = CreateLabel(
      row, title, kMainTextColor, Font28());
  lv_obj_t* subtitle_label = CreateLabel(
      row, subtitle, kSettingsSecondaryTextColor, Font24());
  if (title_label == nullptr || subtitle_label == nullptr) {
    lv_obj_delete(row);
    return nullptr;
  }
  lv_obj_set_width(title_label, state->config.width - 166);
  lv_label_set_long_mode(title_label, LV_LABEL_LONG_DOT);
  lv_obj_align(
      title_label, LV_ALIGN_TOP_LEFT, 34, 23 + text_y_offset);
  lv_obj_set_width(subtitle_label, state->config.width - 166);
  lv_label_set_long_mode(subtitle_label, LV_LABEL_LONG_DOT);
  lv_obj_align(
      subtitle_label, LV_ALIGN_TOP_LEFT, 34, 65 + text_y_offset);
  if (show_chevron) {
    lv_obj_t* chevron = CreateLabel(row, icon::kChevronRight,
        kSecondaryTextColor, OutlineIconFont44());
    if (chevron == nullptr) {
      lv_obj_delete(row);
      return nullptr;
    }
    lv_obj_align(chevron, LV_ALIGN_RIGHT_MID, -34, 0);
  }
  return row;
}

/**
 * @brief 显示射频配置资料与设置列表页
 * @param state 射频页面状态
 * @param index 配置索引
 * @return 显示成功返回 true，否则返回 false
 */
bool ShowProfileSettingsPage(RfViewState* state, size_t index) {
  if (state == nullptr || state->root == nullptr ||
      index >= state->module_count) {
    return false;
  }
  if (state->profile_settings_page != nullptr) {
    lv_obj_move_to_index(state->profile_settings_page, -1);
    return true;
  }
  const RfModuleItem& item = state->modules[index];
  const app::RfProfile& profile = state->preferences.profiles[index];
  lv_obj_t* page = lv_obj_create(state->root);
  if (page == nullptr) {
    return false;
  }
  state->profile_settings_page = page;
  state->profile_settings_index = index;
  state->profile_settings_closing = false;
  state->profile_settings_edge_swipe = EdgeBackSwipeState();
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
  AddEdgeBackSwipeEvents(page, ProfileSettingsEdgeBackEventCallback, state);

  lv_obj_t* back = lv_button_create(page);
  if (back == nullptr) {
    ResetProfileSettingsReferences(state);
    lv_obj_delete(page);
    return false;
  }
  lv_obj_remove_style_all(back);
  lv_obj_remove_flag(back, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(back, LV_OBJ_FLAG_PRESS_LOCK);
  lv_obj_add_flag(back, LV_OBJ_FLAG_GESTURE_BUBBLE);
  lv_obj_set_size(back, 62, 62);
  lv_obj_set_pos(back, 18, 66);
  lv_obj_set_style_bg_opa(back, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_set_style_bg_opa(back, LV_OPA_TRANSP, LV_STATE_PRESSED);
  lv_obj_add_event_cb(back, ProfileSettingsBackClickedEventCallback,
      LV_EVENT_CLICKED, state);
  lv_obj_t* back_icon = CreateLabel(
      back, icon::kArrowBack, kMainTextColor, OutlineIconFont44());
  lv_obj_t* page_title = CreateLabel(
      page, "Profile settings", kMainTextColor, Font48());
  if (back_icon == nullptr || page_title == nullptr) {
    ResetProfileSettingsReferences(state);
    lv_obj_delete(page);
    return false;
  }
  lv_obj_align(back_icon, LV_ALIGN_CENTER, -4, 0);
  lv_obj_set_pos(page_title, 34, 154);

  lv_obj_t* body = lv_obj_create(page);
  if (body == nullptr) {
    ResetProfileSettingsReferences(state);
    lv_obj_delete(page);
    return false;
  }
  lv_obj_set_pos(body, 0, 224);
  lv_obj_set_size(body, state->config.width, state->config.height - 224);
  lv_obj_set_style_bg_opa(body, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_set_style_border_width(body, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(body, 0, LV_PART_MAIN);
  lv_obj_set_scroll_dir(body, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(body, LV_SCROLLBAR_MODE_AUTO);
  lv_obj_add_flag(body, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(body, LV_OBJ_FLAG_GESTURE_BUBBLE);

  lv_obj_t* avatar = lv_obj_create(body);
  if (avatar == nullptr) {
    ResetProfileSettingsReferences(state);
    lv_obj_delete(page);
    return false;
  }
  lv_obj_remove_flag(avatar, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(avatar, 112, 112);
  lv_obj_set_pos(avatar, 34, 18);
  lv_obj_set_style_radius(avatar, 56, LV_PART_MAIN);
  lv_obj_set_style_bg_color(
      avatar, lv_color_hex(item.color), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(avatar, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_border_width(avatar, 0, LV_PART_MAIN);
  lv_obj_t* avatar_text = CreateLabel(
      avatar, item.short_name, kOnPrimaryColor, Font36());

  lv_obj_t* name_action = lv_button_create(body);
  if (name_action == nullptr) {
    ResetProfileSettingsReferences(state);
    lv_obj_delete(page);
    return false;
  }
  lv_obj_remove_style_all(name_action);
  lv_obj_remove_flag(name_action, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(name_action, LV_OBJ_FLAG_GESTURE_BUBBLE);
  lv_obj_set_size(name_action,
      state->config.width - kProfileNameActionX -
          kProfileNameActionRightMargin,
      kProfileNameActionHeight);
  lv_obj_set_pos(name_action, kProfileNameActionX, 30);
  lv_obj_set_style_bg_opa(name_action, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_set_style_bg_color(
      name_action, lv_color_hex(kPressedColor), LV_STATE_PRESSED);
  lv_obj_set_style_bg_opa(name_action, LV_OPA_COVER, LV_STATE_PRESSED);
  lv_obj_set_style_radius(name_action, 22, LV_PART_MAIN);
  lv_obj_set_style_border_width(name_action, 0, LV_PART_MAIN);
  lv_obj_set_style_shadow_width(name_action, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(name_action, 0, LV_PART_MAIN);
  if (!AddPressCancelOnLeave(name_action)) {
    ResetProfileSettingsReferences(state);
    lv_obj_delete(page);
    return false;
  }
  lv_obj_add_event_cb(name_action, ProfileNameAreaClickedEventCallback,
      LV_EVENT_CLICKED, state);
  state->profile_settings_name_label = CreateLabel(
      name_action, profile.name, kMainTextColor, Font36());
  const char* status = ProfileStatusText(state, index);
  state->profile_settings_header_status_label = CreateLabel(
      body, status, ProfileStatusColor(status), Font24());
  if (avatar_text == nullptr ||
      state->profile_settings_name_label == nullptr ||
      state->profile_settings_header_status_label == nullptr) {
    ResetProfileSettingsReferences(state);
    lv_obj_delete(page);
    return false;
  }
  lv_obj_center(avatar_text);
  UpdateProfileSettingsNameLayout(state);
  lv_obj_set_pos(
      state->profile_settings_header_status_label, 170, 94);

  if (!CreateProfileSettingsSection(body, "RF PROFILE", 164)) {
    ResetProfileSettingsReferences(state);
    lv_obj_delete(page);
    return false;
  }
  lv_obj_t* active_row = CreateProfileSettingsRow(body, state,
      "Active profile",
      "Use this profile for sending and receiving", 196, nullptr, false);
  lv_obj_t* rf_row = CreateProfileSettingsRow(body, state,
      "RF settings", "Manage radio parameters and behavior", 332,
      ProfileRfSettingsClickedEventCallback, true, -2, 136);
  if (active_row == nullptr || rf_row == nullptr) {
    ResetProfileSettingsReferences(state);
    lv_obj_delete(page);
    return false;
  }
  state->profile_settings_active_switch = lv_switch_create(active_row);
  if (state->profile_settings_active_switch == nullptr) {
    ResetProfileSettingsReferences(state);
    lv_obj_delete(page);
    return false;
  }
  lv_obj_add_flag(
      state->profile_settings_active_switch, LV_OBJ_FLAG_GESTURE_BUBBLE);
  lv_obj_set_size(state->profile_settings_active_switch,
      kProfileSwitchWidth, kProfileSwitchHeight);
  lv_obj_align(state->profile_settings_active_switch,
      LV_ALIGN_RIGHT_MID, -34, 0);
  lv_obj_set_style_anim_duration(state->profile_settings_active_switch,
      kProfileSwitchAnimationMs, LV_PART_MAIN);
  lv_obj_set_style_bg_color(state->profile_settings_active_switch,
      lv_color_hex(kPrimaryColor), kProfileSwitchCheckedIndicatorSelector);
  lv_obj_set_style_bg_opa(state->profile_settings_active_switch,
      LV_OPA_COVER, kProfileSwitchCheckedIndicatorSelector);
  lv_obj_add_event_cb(state->profile_settings_active_switch,
      ProfileSettingsActiveChangedEventCallback,
      LV_EVENT_VALUE_CHANGED, state);
  if (!IsProfileSupported(state, profile) &&
      state->preferences.active_profile_id != profile.id) {
    lv_obj_add_state(
        state->profile_settings_active_switch, LV_STATE_DISABLED);
  }

  RefreshProfileSettingsPage(state);
  EnableEdgeBackSwipeEventBubble(page);
  if (!StartSlideLeftWindowTransition(
      page, state->config.width, kAnimationMs, state, nullptr)) {
    ResetProfileSettingsReferences(state);
    lv_obj_delete(page);
    return false;
  }
  return true;
}

void DeleteSelectedProfiles(RfViewState* state) {
  if (state == nullptr) {
    return;
  }
  app::RfPreferences next = state->preferences;
  size_t message_write_index = 0;
  for (size_t index = 0; index < state->chat_message_count; ++index) {
    const size_t profile_index = FindProfileIndex(
        state, state->chat_messages[index].profile_id);
    if (profile_index < state->module_count &&
        state->selected_modules[profile_index]) {
      continue;
    }
    state->chat_messages[message_write_index++] =
        state->chat_messages[index];
  }
  state->chat_message_count = message_write_index;
  size_t write_index = 0;
  for (size_t read_index = 0;
       read_index < state->module_count; ++read_index) {
    if (state->selected_modules[read_index]) {
      if (next.profiles[read_index].id == next.active_profile_id) {
        FailPendingMessages(state, next.active_profile_id);
        next.active_profile_id = 0;
        if (state->config.rf != nullptr) {
          state->config.rf->DeactivateRf();
        }
      }
      continue;
    }
    next.profiles[write_index] = next.profiles[read_index];
    if (write_index != read_index) {
      CopyBoundedString(state->latest_messages[write_index],
          sizeof(state->latest_messages[write_index]),
          state->latest_messages[read_index]);
      CopyBoundedString(state->message_times[write_index],
          sizeof(state->message_times[write_index]),
          state->message_times[read_index]);
      state->unread_counts[write_index] =
          state->unread_counts[read_index];
    }
    ++write_index;
  }
  next.profile_count = write_index;
  for (size_t index = write_index; index < kRfModuleCapacity; ++index) {
    next.profiles[index] = app::RfProfile{};
    state->latest_messages[index][0] = '\0';
    state->message_times[index][0] = '\0';
    state->unread_counts[index] = 0;
  }
  state->preferences = next;
  app::UpdateRfPreferences(state->preferences);
  SyncModuleItems(state);
  CloseSelectionMode(state);
}

/**
 * @brief 确认删除选中的 RF 配置
 * @param context RF 页面状态
 */
void DeleteProfilesConfirmed(void* context) {
  DeleteSelectedProfiles(static_cast<RfViewState*>(context));
}

bool ShowDeleteConfirmation(RfViewState* state) {
  if (state == nullptr || state->root == nullptr ||
      IsPromptDialogVisible(&state->delete_dialog)) {
    return false;
  }

  PromptDialogConfig config;
  config.screen_width = state->config.width;
  config.screen_height = state->config.height;
  config.dialog_width =
      state->config.width - 2 * kDeletePromptSideMargin;
  config.dialog_height = kDeletePromptHeight;
  config.dialog_radius = kDeletePromptRadius;
  config.inner_padding = kDeletePromptInnerPadding;
  config.header_height = 0;
  config.title_y = 0;
  config.action_height =
      kDeletePromptInnerPadding + kDeletePromptButtonHeight;
  config.action_button_height = kDeletePromptButtonHeight;
  config.action_button_gap = kDeletePromptButtonGap;
  config.action_bottom_padding = kDeletePromptInnerPadding;
  config.bottom_margin = kDeletePromptBottomMargin;
  config.animation_ms = kAnimationMs;
  config.title = "";
  config.cancel_text = "Cancel";
  config.confirm_text = "OK";
  config.title_font = Font32();
  config.action_font = Font28();
  config.confirm_callback = DeleteProfilesConfirmed;
  config.callback_context = state;
  config.slide_from_bottom = true;
  lv_obj_t* body = ShowPromptDialog(
      state->root, &state->delete_dialog, config);
  if (body == nullptr || state->delete_dialog.panel == nullptr) {
    return false;
  }
  lv_obj_remove_flag(body, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t* title = CreateLabel(
      body, "Delete profiles", kMainTextColor, Font32());
  lv_obj_t* message = CreateLabel(body,
      "Messages and settings for the selected profiles will be removed.",
      kSecondaryTextColor, Font24());
  if (title == nullptr || message == nullptr) {
    ClosePromptDialog(&state->delete_dialog);
    return false;
  }
  const int content_width =
      config.dialog_width - 2 * kDeletePromptInnerPadding;
  lv_obj_set_size(title, content_width, 42);
  lv_obj_set_pos(title, kDeletePromptInnerPadding, 34);
  lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
  lv_obj_set_width(message, content_width);
  lv_obj_set_pos(message, kDeletePromptInnerPadding, 78);
  lv_label_set_long_mode(message, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_text_align(message, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
  return true;
}

void SelectionDeleteClickedEventCallback(lv_event_t* event) {
  if (lv_event_get_code(event) == LV_EVENT_CLICKED) {
    ShowDeleteConfirmation(
        static_cast<RfViewState*>(lv_event_get_user_data(event)));
  }
}

lv_obj_t* CreateHeaderIconButton(lv_obj_t* parent, const char* icon_text,
    int x, int size, const lv_font_t* icon_font,
    lv_event_cb_t callback, RfViewState* state) {
  if (parent == nullptr || icon_text == nullptr || icon_font == nullptr ||
      size <= 0 || callback == nullptr) {
    return nullptr;
  }
  lv_obj_t* button = lv_button_create(parent);
  if (button == nullptr) {
    return nullptr;
  }
  lv_obj_remove_style_all(button);
  lv_obj_set_size(button, size, size);
  lv_obj_set_pos(button, x, 0);
  lv_obj_add_event_cb(button, callback, LV_EVENT_CLICKED, state);
  lv_obj_t* label = CreateLabel(
      button, icon_text, kMainTextColor, icon_font);
  if (label != nullptr) {
    lv_obj_center(label);
  }
  return button;
}

bool RenderHeader(RfViewState* state) {
  if (state == nullptr || state->header_area == nullptr) {
    return false;
  }
  lv_obj_clean(state->header_area);
  if (state->add_button != nullptr) {
    if (state->selection_mode ||
        state->module_count >= kRfModuleCapacity) {
      lv_obj_add_flag(state->add_button, LV_OBJ_FLAG_HIDDEN);
    } else {
      lv_obj_remove_flag(state->add_button, LV_OBJ_FLAG_HIDDEN);
    }
  }
  if (!state->selection_mode) {
    lv_obj_t* menu = CreateHeaderIconButton(state->header_area,
        icon::kMenu, 20, 72, &lvgl_font_material_symbols_fill_56,
        MenuClickedEventCallback, state);
    lv_obj_t* title = CreateLabel(
        state->header_area, "RF", kMainTextColor, Font36());
    if (menu == nullptr || title == nullptr) {
      return false;
    }
    lv_obj_set_pos(title, 104, 2);
    char summary_text[48] = {};
    if (state->module_count >= kRfModuleCapacity) {
      std::snprintf(summary_text, sizeof(summary_text),
          "%u profiles | limit reached",
          static_cast<unsigned>(state->module_count));
    } else {
      std::snprintf(summary_text, sizeof(summary_text), "%u profiles",
          static_cast<unsigned>(state->module_count));
    }
    lv_obj_t* summary_label = CreateLabel(state->header_area,
        summary_text, kSecondaryTextColor, Font24());
    if (summary_label != nullptr) {
      lv_obj_set_pos(summary_label, 104, 44);
    }
    return true;
  }

  const size_t selected_count = SelectedModuleCount(state);
  if (CreateHeaderIconButton(state->header_area, icon::kClose, 14, 64,
          OutlineIconFont44(), SelectionCloseClickedEventCallback,
          state) == nullptr) {
    return false;
  }
  char count_text[12] = {};
  std::snprintf(count_text, sizeof(count_text), "%u",
      static_cast<unsigned>(selected_count));
  lv_obj_t* count = CreateLabel(
      state->header_area, count_text, kMainTextColor, Font36());
  if (count != nullptr) {
    lv_obj_set_pos(count, 102, 10);
  }
  const int right = state->config.width;
  CreateHeaderIconButton(state->header_area, icon::kDelete,
      right - 74, 64, FillIconFont44(),
      SelectionDeleteClickedEventCallback, state);
  return true;
}

bool CreateHeader(lv_obj_t* parent, RfViewState* state) {
  lv_obj_t* area = lv_obj_create(parent);
  if (area == nullptr) {
    return false;
  }
  state->header_area = area;
  lv_obj_remove_flag(area, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(area, state->config.width, 82);
  lv_obj_set_pos(area, 0, kHeaderTop - 2);
  lv_obj_set_style_bg_opa(area, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_set_style_border_width(area, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(area, 0, LV_PART_MAIN);
  return RenderHeader(state);
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
      state->add_chip_buttons, 1, state->selected_add_chip);
  UpdateOptionButtonGroup(
      state->add_protocol_buttons, 1, state->selected_add_protocol);
  UpdateOptionButtonGroup(
      state->add_sf_buttons, 8, state->selected_add_sf);
  UpdateOptionButtonGroup(state->add_bandwidth_buttons, 4,
      state->selected_add_bandwidth);
  UpdateOptionButtonGroup(state->add_coding_rate_buttons, 4,
      state->selected_add_coding_rate);
}

/**
 * @brief 判断当前芯片的工作频率是否处于可设置范围
 * @param chip_index 芯片选项索引
 * @param frequency_mhz 以 MHz 为单位的工作频率
 * @return 频率有效返回 true，否则返回 false
 */
bool IsFrequencyValidForChip(int chip_index, double frequency_mhz) {
  return chip_index == 0 && frequency_mhz >= 150 && frequency_mhz <= 960;
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
  const double frequency_mhz = std::strtod(text, &end);
  return end != nullptr && end[0] == '\0' &&
         IsFrequencyValidForChip(
             state->selected_add_chip, frequency_mhz);
}

bool ParseTextAreaLong(lv_obj_t* input, int base, long minimum,
    long maximum, long* value) {
  if (input == nullptr || value == nullptr) {
    return false;
  }
  const char* text = lv_textarea_get_text(input);
  if (text == nullptr || text[0] == '\0') {
    return false;
  }
  char* end = nullptr;
  const long parsed = std::strtol(text, &end, base);
  if (end == nullptr || end[0] != '\0' || parsed < minimum ||
      parsed > maximum) {
    return false;
  }
  *value = parsed;
  return true;
}

/**
 * @brief 根据校验结果更新射频参数输入框错误边框
 * @param input 文本输入框
 * @param valid 当前内容是否有效
 */
void UpdateAddTextAreaErrorStyle(lv_obj_t* input, bool valid) {
  if (input == nullptr) {
    return;
  }
  const char* text = lv_textarea_get_text(input);
  const bool show_error = text != nullptr && text[0] != '\0' &&
                          !valid;
  const int outline_width = show_error ? 2 : 0;
  lv_obj_set_style_border_width(input, 0, LV_PART_MAIN);
  lv_obj_set_style_border_width(input, 0, LV_STATE_FOCUSED);
  lv_obj_set_style_outline_width(input, outline_width, LV_PART_MAIN);
  lv_obj_set_style_outline_width(input, outline_width, LV_STATE_FOCUSED);
  lv_obj_set_style_outline_color(input,
      lv_color_hex(kInputErrorColor), LV_PART_MAIN);
  lv_obj_set_style_outline_color(input,
      lv_color_hex(kInputErrorColor), LV_STATE_FOCUSED);
  lv_obj_set_style_outline_opa(input, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_outline_opa(input, LV_OPA_COVER, LV_STATE_FOCUSED);
  lv_obj_set_style_outline_pad(input, -2, LV_PART_MAIN);
  lv_obj_set_style_outline_pad(input, -2, LV_STATE_FOCUSED);
}

/**
 * @brief 更新射频参数页所有输入框的错误边框
 * @param state 射频页面状态
 */
void UpdateAddInputErrorStyles(RfViewState* state) {
  if (state == nullptr) {
    return;
  }
  long parsed_value = 0;
  const bool power_valid = ParseTextAreaLong(
      state->add_power_input, 10, -9, 22, &parsed_value);
  const bool preamble_valid = ParseTextAreaLong(
      state->add_preamble_input, 10, 1, 65535, &parsed_value);
  const bool sync_word_valid = ParseTextAreaLong(
      state->add_sync_word_input, 16, 0, 255, &parsed_value);
  UpdateAddTextAreaErrorStyle(
      state->add_frequency_input, IsAddFrequencyValid(state));
  UpdateAddTextAreaErrorStyle(state->add_power_input, power_valid);
  UpdateAddTextAreaErrorStyle(state->add_preamble_input, preamble_valid);
  UpdateAddTextAreaErrorStyle(state->add_sync_word_input, sync_word_valid);
}

/**
 * @brief 判断添加模块页面的必填信息是否完整
 * @param state 射频页面状态
 * @return 信息完整返回 true，否则返回 false
 */
bool IsAddModuleFormComplete(const RfViewState* state) {
  if (state == nullptr || state->add_frequency_input == nullptr ||
      state->add_power_input == nullptr ||
      state->add_preamble_input == nullptr ||
      state->add_sync_word_input == nullptr ||
      (state->editing_index >= state->module_count &&
       state->module_count >= kRfModuleCapacity)) {
    return false;
  }
  const char* frequency =
      lv_textarea_get_text(state->add_frequency_input);
  long power = 0;
  long preamble = 0;
  long sync_word = 0;
  return frequency != nullptr && frequency[0] != '\0' &&
         IsAddFrequencyValid(state) &&
         ParseTextAreaLong(state->add_power_input, 10, -9, 22, &power) &&
         ParseTextAreaLong(state->add_preamble_input, 10, 1, 65535,
             &preamble) &&
         ParseTextAreaLong(state->add_sync_word_input, 16, 0, 255,
             &sync_word) &&
         state->selected_add_chip >= 0 &&
         state->selected_add_chip == 0 &&
         state->selected_add_protocol >= 0 &&
         state->selected_add_protocol == 0 && state->selected_add_sf >= 0 &&
         state->selected_add_sf < 8 &&
         state->selected_add_bandwidth >= 0 &&
         state->selected_add_bandwidth < 4 &&
         state->selected_add_coding_rate >= 0 &&
         state->selected_add_coding_rate < 4;
}

/**
 * @brief 更新添加模块提交按钮的启用状态
 * @param state 射频页面状态
 */
void UpdateAddSubmitButton(RfViewState* state) {
  if (state == nullptr || state->add_submit_button == nullptr) {
    return;
  }
  UpdateAddInputErrorStyles(state);
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
  } else if (action->group == RfAddOptionGroup::kSpreadingFactor) {
    action->state->selected_add_sf = action->index;
  } else if (action->group == RfAddOptionGroup::kBandwidth) {
    action->state->selected_add_bandwidth = action->index;
  } else {
    action->state->selected_add_coding_rate = action->index;
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
  const int input_y = lv_obj_get_y(input);
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
  if (code == LV_EVENT_FOCUSED) {
    SetAddKeyboardVisible(
        state, lv_event_get_target_obj(event), true);
  } else if (code == LV_EVENT_CLICKED && state != nullptr &&
             state->add_keyboard != nullptr &&
             lv_obj_has_flag(state->add_keyboard, LV_OBJ_FLAG_HIDDEN)) {
    SetAddKeyboardVisible(
        state, lv_event_get_target_obj(event), true);
  } else if (code == LV_EVENT_READY || code == LV_EVENT_CANCEL) {
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
  state->add_frequency_input = nullptr;
  state->add_power_input = nullptr;
  state->add_preamble_input = nullptr;
  state->add_sync_word_input = nullptr;
  state->add_crc_switch = nullptr;
  state->add_iq_switch = nullptr;
  state->add_rx_boost_switch = nullptr;
  state->add_active_switch = nullptr;
  state->add_keyboard = nullptr;
  state->add_submit_button = nullptr;
  state->add_submit_label = nullptr;
  state->add_edge_swipe = EdgeBackSwipeState();
  state->editing_index = kRfModuleCapacity;
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
    state->add_frequency_input = nullptr;
    state->add_power_input = nullptr;
    state->add_preamble_input = nullptr;
    state->add_sync_word_input = nullptr;
    state->add_crc_switch = nullptr;
    state->add_iq_switch = nullptr;
    state->add_rx_boost_switch = nullptr;
    state->add_active_switch = nullptr;
    state->add_keyboard = nullptr;
    state->add_submit_button = nullptr;
    state->add_submit_label = nullptr;
    state->add_edge_swipe = EdgeBackSwipeState();
    state->editing_index = kRfModuleCapacity;
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
  const bool editing = state->editing_index < state->module_count;
  const size_t index = editing
      ? state->editing_index
      : state->module_count;
  app::RfProfile profile = editing
      ? state->preferences.profiles[index]
      : app::RfProfile{};
  const app::RfProfile previous_profile = profile;
  if (!editing) {
    profile.id = state->preferences.next_profile_id++;
    if (profile.id == 0) {
      profile.id = state->preferences.next_profile_id++;
    }
    std::snprintf(profile.name, sizeof(profile.name), "RF profile %u",
        static_cast<unsigned>(profile.id));
  }
  const char* frequency_text = lv_textarea_get_text(
      state->add_frequency_input);
  const double frequency_mhz = std::strtod(
      frequency_text, nullptr);
  long output_power = 0;
  long preamble = 0;
  long sync_word = 0;
  ParseTextAreaLong(state->add_power_input, 10, -9, 22,
      &output_power);
  ParseTextAreaLong(state->add_preamble_input, 10, 1, 65535,
      &preamble);
  ParseTextAreaLong(state->add_sync_word_input, 16, 0, 255,
      &sync_word);
  constexpr uint32_t kBandwidths[] = {
      62500, 125000, 250000, 500000};
  profile.frequency_hz = static_cast<uint32_t>(
      frequency_mhz * 1000000.0 + 0.5);
  profile.bandwidth_hz =
      kBandwidths[state->selected_add_bandwidth];
  profile.preamble_length = static_cast<uint16_t>(preamble);
  profile.spreading_factor = static_cast<uint8_t>(
      state->selected_add_sf + 5);
  profile.coding_rate_denominator = static_cast<uint8_t>(
      state->selected_add_coding_rate + 5);
  profile.sync_word = static_cast<uint8_t>(sync_word);
  profile.output_power_dbm = static_cast<int8_t>(output_power);
  profile.crc_enabled = lv_obj_has_state(
      state->add_crc_switch, LV_STATE_CHECKED);
  profile.invert_iq = lv_obj_has_state(
      state->add_iq_switch, LV_STATE_CHECKED);
  profile.rx_boosted = lv_obj_has_state(
      state->add_rx_boost_switch, LV_STATE_CHECKED);
  const bool settings_changed = editing &&
      !AreProfileSettingsEqual(previous_profile, profile);
  state->preferences.profiles[index] = profile;
  if (!editing) {
    ++state->preferences.profile_count;
  }
  const bool activate = lv_obj_has_state(
      state->add_active_switch, LV_STATE_CHECKED);
  if (activate) {
    FailPendingMessages(
        state, state->preferences.active_profile_id);
    state->preferences.active_profile_id = profile.id;
    state->activation_retry_count = 0;
    state->last_activation_retry_tick = lv_tick_get();
    if (state->config.rf != nullptr) {
      state->config.rf->ActivateRf(ToRadioConfig(profile));
    }
  } else if (state->preferences.active_profile_id == profile.id) {
    state->preferences.active_profile_id = 0;
    FailPendingMessages(state, profile.id);
    if (state->config.rf != nullptr) {
      state->config.rf->DeactivateRf();
    }
  }
  app::UpdateRfPreferences(state->preferences);
  if (!editing) {
    AppendSystemMessage(state, index, kProfileCreatedMessage);
  } else if (settings_changed) {
    AppendSystemMessage(state, index, kSettingsChangedMessage);
  }
  SyncModuleItems(state);
  RefreshProfileSettingsPage(state);
  CloseSelectionMode(state);
  if (state->detail_title_label != nullptr &&
      state->detail_index == index) {
    lv_label_set_text(state->detail_title_label, profile.name);
    RenderChatMessages(state);
  }
  UpdateDetailStatus(state);
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
  // 页面统一控制滚动，避免与 LVGL 聚焦滚动重复触发。
  lv_obj_remove_flag(input, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
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

lv_obj_t* CreateAddSwitchRow(lv_obj_t* parent, RfViewState* state,
    const char* title, const char* subtitle, int y, bool checked) {
  lv_obj_t* row = lv_obj_create(parent);
  if (row == nullptr) {
    return nullptr;
  }
  lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(row, LV_OBJ_FLAG_GESTURE_BUBBLE);
  lv_obj_set_size(
      row, state->config.width - 56, kAddSwitchRowHeight);
  lv_obj_set_pos(row, 28, y);
  lv_obj_set_style_bg_color(
      row, lv_color_hex(kSurfaceContainerLowColor), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(row, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_border_width(row, 0, LV_PART_MAIN);
  lv_obj_set_style_radius(row, 22, LV_PART_MAIN);
  lv_obj_set_style_pad_all(row, 0, LV_PART_MAIN);
  lv_obj_t* title_label = CreateLabel(
      row, title, kMainTextColor, Font24());
  lv_obj_t* subtitle_label = CreateLabel(
      row, subtitle, kSecondaryTextColor, Font22());
  lv_obj_t* toggle = lv_switch_create(row);
  if (title_label == nullptr || subtitle_label == nullptr ||
      toggle == nullptr) {
    lv_obj_delete(row);
    return nullptr;
  }
  constexpr int kTitleHeight = 32;
  constexpr int kSubtitleHeight = 30;
  constexpr int kTextGap = 6;
  const int text_top = (kAddSwitchRowHeight - kTitleHeight -
      kTextGap - kSubtitleHeight) / 2;
  lv_obj_set_size(
      title_label, state->config.width - 190, kTitleHeight);
  lv_label_set_long_mode(title_label, LV_LABEL_LONG_DOT);
  lv_obj_set_pos(title_label, 20, text_top);
  lv_obj_set_size(
      subtitle_label, state->config.width - 190, kSubtitleHeight);
  lv_label_set_long_mode(
      subtitle_label, LV_LABEL_LONG_SCROLL_CIRCULAR);
  lv_obj_set_pos(
      subtitle_label, 20, text_top + kTitleHeight + kTextGap);
  lv_obj_add_flag(toggle, LV_OBJ_FLAG_GESTURE_BUBBLE);
  lv_obj_set_size(toggle, kProfileSwitchWidth, kProfileSwitchHeight);
  lv_obj_align(toggle, LV_ALIGN_RIGHT_MID, -18, 0);
  lv_obj_set_style_anim_duration(
      toggle, kProfileSwitchAnimationMs, LV_PART_MAIN);
  lv_obj_set_style_bg_color(toggle, lv_color_hex(kPrimaryColor),
      kProfileSwitchCheckedIndicatorSelector);
  lv_obj_set_style_bg_opa(
      toggle, LV_OPA_COVER, kProfileSwitchCheckedIndicatorSelector);
  if (checked) {
    lv_obj_add_state(toggle, LV_STATE_CHECKED);
  }
  lv_obj_add_event_cb(
      toggle, AddInputEventCallback, LV_EVENT_VALUE_CHANGED, state);
  AddEdgeBackSwipeEvents(toggle, AddPageEdgeBackEventCallback, state);
  return toggle;
}

/**
 * @brief 创建添加模块页面的参数内容
 * @param state 射频页面状态
 * @return 创建成功返回 true，否则返回 false
 */
bool CreateAddModuleContent(RfViewState* state) {
  lv_obj_t* body = state->add_body;
  const bool editing = state->editing_index < state->module_count;
  const app::RfProfile profile = editing
      ? state->preferences.profiles[state->editing_index]
      : app::RfProfile{};
  char frequency[16] = {};
  char power[8] = {};
  char preamble[12] = {};
  char sync_word[3] = {};
  std::snprintf(frequency, sizeof(frequency), "%.3f",
      static_cast<double>(profile.frequency_hz) / 1000000.0);
  std::snprintf(power, sizeof(power), "%d",
      static_cast<int>(profile.output_power_dbm));
  std::snprintf(preamble, sizeof(preamble), "%u",
      static_cast<unsigned>(profile.preamble_length));
  std::snprintf(sync_word, sizeof(sync_word), "%02hhX",
      static_cast<unsigned>(profile.sync_word));
  if (body == nullptr ||
      !CreateAddParameterTitle(body, "RF CHIP", 8)) {
    return false;
  }

  const int option_gap = 10;
  const int option_area_width = state->config.width - 56;
  state->add_chip_buttons[0] = CreateAddOptionButton(body, state,
      RfAddOptionGroup::kChip, 0,
      ChipDisplayName(profile.chip), 28, 44, 150, 62);
  if (state->add_chip_buttons[0] == nullptr) {
    return false;
  }

  if (!CreateAddParameterTitle(body, "PROTOCOL", 134)) {
    return false;
  }
  state->add_protocol_buttons[0] = CreateAddOptionButton(body, state,
      RfAddOptionGroup::kProtocol, 0,
      ProtocolDisplayName(profile.protocol), 28, 170, 116, 62);
  if (state->add_protocol_buttons[0] == nullptr) {
    return false;
  }

  if (!CreateAddParameterTitle(body, "WORKING FREQUENCY", 262)) {
    return false;
  }
  state->add_frequency_input = CreateAddTextArea(
      body, state, "Frequency", frequency, 298, 7);
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
      302);
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

  if (!CreateAddParameterTitle(body, "SPREADING FACTOR", 400)) {
    return false;
  }
  const int option_width = (option_area_width - 3 * option_gap) / 4;
  const char* sf_names[] = {
      "5", "6", "7", "8", "9", "10", "11", "12"};
  for (int index = 0; index < 8; ++index) {
    const int column = index % 4;
    const int row = index / 4;
    state->add_sf_buttons[index] = CreateAddOptionButton(body, state,
        RfAddOptionGroup::kSpreadingFactor, index, sf_names[index],
        28 + column * (option_width + option_gap),
        436 + row * 68, option_width, 58);
    if (state->add_sf_buttons[index] == nullptr) {
      return false;
    }
  }

  if (!CreateAddParameterTitle(body, "BANDWIDTH (kHz)", 582)) {
    return false;
  }
  const char* bandwidth_names[] = {"62.5", "125", "250", "500"};
  for (int index = 0; index < 4; ++index) {
    state->add_bandwidth_buttons[index] = CreateAddOptionButton(
        body, state, RfAddOptionGroup::kBandwidth, index,
        bandwidth_names[index],
        28 + index * (option_width + option_gap), 618,
        option_width, 60);
    if (state->add_bandwidth_buttons[index] == nullptr) {
      return false;
    }
  }

  if (!CreateAddParameterTitle(body, "CODING RATE", 710)) {
    return false;
  }
  const char* coding_names[] = {"4/5", "4/6", "4/7", "4/8"};
  for (int index = 0; index < 4; ++index) {
    state->add_coding_rate_buttons[index] = CreateAddOptionButton(
        body, state, RfAddOptionGroup::kCodingRate, index,
        coding_names[index],
        28 + index * (option_width + option_gap), 746,
        option_width, 60);
    if (state->add_coding_rate_buttons[index] == nullptr) {
      return false;
    }
  }

  if (!CreateAddParameterTitle(body, "TX POWER", 838)) {
    return false;
  }
  state->add_power_input = CreateAddTextArea(
      body, state, "Output power", power, 874, 3);
  if (state->add_power_input == nullptr) {
    return false;
  }
  lv_textarea_set_accepted_chars(
      state->add_power_input, "-0123456789");

  if (!CreateAddParameterTitle(body, "PREAMBLE LENGTH", 976)) {
    return false;
  }
  state->add_preamble_input = CreateAddTextArea(
      body, state, "Preamble symbols", preamble, 1012, 5);
  if (state->add_preamble_input == nullptr) {
    return false;
  }
  lv_textarea_set_accepted_chars(
      state->add_preamble_input, kIntegerAcceptedChars);

  if (!CreateAddParameterTitle(body, "SYNC WORD (HEX)", 1114)) {
    return false;
  }
  state->add_sync_word_input = CreateAddTextArea(
      body, state, "12", sync_word, 1150, 2);
  if (state->add_sync_word_input == nullptr) {
    return false;
  }
  lv_textarea_set_accepted_chars(state->add_sync_word_input,
      kHexAcceptedChars);
  constexpr int kSyncWordSideMargin = 28;
  constexpr int kSyncWordPrefixWidth = 72;
  constexpr int kSyncWordInputGap = 12;
  const int sync_word_input_x = kSyncWordSideMargin +
      kSyncWordPrefixWidth + kSyncWordInputGap;
  lv_obj_set_width(state->add_sync_word_input,
      state->config.width - sync_word_input_x - kSyncWordSideMargin);
  lv_obj_set_x(state->add_sync_word_input, sync_word_input_x);

  lv_obj_t* prefix = lv_obj_create(body);
  if (prefix == nullptr) {
    return false;
  }
  lv_obj_remove_flag(prefix, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(prefix, kSyncWordPrefixWidth, 62);
  lv_obj_set_pos(prefix, kSyncWordSideMargin, 1154);
  lv_obj_set_style_bg_color(prefix,
      lv_color_hex(kSurfaceContainerHighColor), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(prefix, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_border_width(prefix, 0, LV_PART_MAIN);
  lv_obj_set_style_radius(prefix, 22, LV_PART_MAIN);
  lv_obj_set_style_pad_all(prefix, 0, LV_PART_MAIN);
  lv_obj_t* prefix_label = CreateLabel(
      prefix, "0x", kSecondaryTextColor, Font22());
  if (prefix_label == nullptr) {
    return false;
  }
  lv_obj_center(prefix_label);

  constexpr int kSwitchRowsTop = 1258;
  constexpr int kSwitchRowPitch =
      kAddSwitchRowHeight + kAddSwitchRowGap;
  state->add_crc_switch = CreateAddSwitchRow(body, state,
      "CRC", "Reject damaged LoRa packets", kSwitchRowsTop,
      profile.crc_enabled);
  state->add_iq_switch = CreateAddSwitchRow(body, state,
      "Invert IQ", "Enable only when the peer also inverts IQ",
      kSwitchRowsTop + kSwitchRowPitch,
      profile.invert_iq);
  state->add_rx_boost_switch = CreateAddSwitchRow(body, state,
      "RX boost", "Higher receive sensitivity",
      kSwitchRowsTop + 2 * kSwitchRowPitch,
      profile.rx_boosted);
  state->add_active_switch = CreateAddSwitchRow(body, state,
      "Active profile", "Only one profile can use the SX1262",
      kSwitchRowsTop + 3 * kSwitchRowPitch,
      !editing || state->preferences.active_profile_id == profile.id);
  if (state->add_crc_switch == nullptr ||
      state->add_iq_switch == nullptr ||
      state->add_rx_boost_switch == nullptr ||
      state->add_active_switch == nullptr) {
    return false;
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
      page, state->editing_index < state->module_count
          ? "RF profile"
          : "Add RF profile",
      kMainTextColor, Font48());
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
      button, state->editing_index < state->module_count
          ? "Save profile"
          : "Add profile",
      kDisabledTextColor, Font28());
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
  if (state->editing_index >= state->module_count &&
      state->module_count >= kRfModuleCapacity) {
    return false;
  }
  if (state->add_page != nullptr) {
    lv_obj_move_to_index(state->add_page, -1);
    return true;
  }
  state->selected_add_chip = 0;
  state->selected_add_protocol = 0;
  const bool editing = state->editing_index < state->module_count;
  const app::RfProfile profile = editing
      ? state->preferences.profiles[state->editing_index]
      : app::RfProfile{};
  state->selected_add_sf = editing
      ? std::clamp(static_cast<int>(profile.spreading_factor) - 5, 0, 7)
      : kDefaultSpreadingFactorIndex;
  constexpr uint32_t kBandwidths[] = {
      62500, 125000, 250000, 500000};
  state->selected_add_bandwidth = 1;
  for (int index = 0; index < 4; ++index) {
    if (profile.bandwidth_hz == kBandwidths[index]) {
      state->selected_add_bandwidth = index;
    }
  }
  state->selected_add_coding_rate = std::clamp(
      static_cast<int>(profile.coding_rate_denominator) - 5, 0, 3);
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
  for (lv_obj_t*& button : state->add_bandwidth_buttons) {
    button = nullptr;
  }
  for (lv_obj_t*& button : state->add_coding_rate_buttons) {
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
      !AttachSharedKeyboardToTextArea(state->add_keyboard,
          state->add_frequency_input, kFrequencyAcceptedChars) ||
      !AttachSharedKeyboardToTextArea(state->add_keyboard,
          state->add_power_input, "-0123456789") ||
      !AttachSharedKeyboardToTextArea(state->add_keyboard,
          state->add_preamble_input, kIntegerAcceptedChars) ||
      !AttachSharedKeyboardToTextArea(state->add_keyboard,
          state->add_sync_word_input,
          kHexAcceptedChars)) {
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

bool ShowModuleSettings(RfViewState* state, size_t index,
    bool from_detail) {
  if (state == nullptr || index >= state->module_count) {
    return false;
  }
  state->editing_index = index;
  if (from_detail) {
    SetDetailKeyboardVisible(state, false);
  }
  return ShowAddModulePage(state);
}

/**
 * @brief 处理圆形添加按钮点击事件
 * @param event LVGL 事件对象
 */
void AddButtonClickedEventCallback(lv_event_t* event) {
  if (lv_event_get_code(event) == LV_EVENT_CLICKED) {
    auto* state = static_cast<RfViewState*>(
        lv_event_get_user_data(event));
    if (state != nullptr) {
      state->editing_index = kRfModuleCapacity;
      ShowAddModulePage(state);
    }
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
  state->add_button = button;
  if (state->module_count >= kRfModuleCapacity) {
    lv_obj_add_flag(button, LV_OBJ_FLAG_HIDDEN);
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

lv_obj_t* CreateRfView(lv_obj_t* parent, const app::AppEntry& app_entry,
    const AppViewConfig& config) {
  if (parent == nullptr || app_entry.id == nullptr ||
      config.width <= 0 || config.height <= 0) {
    return nullptr;
  }
  auto* state = new RfViewState{};
  state->config = config;
  if (config.rf != nullptr) {
    if (config.rf->ReadRfCapabilities(&state->capabilities)) {
      state->capabilities.count = std::min(
          state->capabilities.count, hal::kRfCapabilityCapacity);
    }
  }
  app::GetRfPreferences(&state->preferences);
  SyncModuleItems(state);
  const size_t active_index = FindProfileIndex(
      state, state->preferences.active_profile_id);
  if (config.rf != nullptr && active_index < state->module_count &&
      IsProfileSupported(
          state, state->preferences.profiles[active_index])) {
    state->last_activation_retry_tick = lv_tick_get();
    config.rf->ActivateRf(ToRadioConfig(
        state->preferences.profiles[active_index]));
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
  AddEdgeBackSwipeEvents(root, SelectionEdgeBackEventCallback, state);
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
  state->radio_timer = lv_timer_create(
      RadioTimerCallback, 120, state);
  return root;
}

}  // namespace lilygo_box::ui
