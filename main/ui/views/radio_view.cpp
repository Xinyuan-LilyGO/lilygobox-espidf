/*
 * @Description: Radio 射频控制应用页面实现
 * @Author: LILYGO_L
 * @Date: 2026-07-12 00:00:00
 * @LastEditTime: 2026-08-21 16:15:55
 * @License: GPL 3.0
 */
#include "ui/views/radio_view.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iterator>
#include <memory>
#include <new>

#include "app/radio_chat_repository.h"
#include "app/storage/radio_storage.h"
#include "app/system_status_cache.h"
#include "base/logger.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "hal/providers/radio/radio_provider.h"
#include "hal/providers/rtc_provider.h"
#include "ui/animation/transition_animation.h"
#include "ui/input/back_navigation_controller.h"
#include "ui/input/press_cancel.h"
#include "ui/resources/fonts/font_assets.h"
#include "ui/resources/fonts/icon_assets.h"
#include "ui/theme/theme_provider.h"
#include "ui/widgets/navigation_drawer.h"
#include "ui/widgets/prompt/prompt_dialog.h"
#include "ui/widgets/prompt/prompt_status.h"
#include "ui/widgets/shared_keyboard.h"

namespace lilygo_box::ui {
namespace {

constexpr uint32_t kPrimaryColor = 0x6750A4;
constexpr uint32_t kPrimaryPressedColor = 0x4F378B;
constexpr uint32_t kOnPrimaryColor = 0xFFFFFF;
constexpr int kHeaderTop = 68;
constexpr int kListTop = 154;
constexpr int kEmptyStatusGroupOffsetY = -100;
constexpr int kStatusGroupTopGap = 24;
constexpr int kRowHeight = 104;
constexpr int kProfileStatusIndicatorSize = 22;
constexpr int kAnimationMs = 240;
constexpr int kDeletePromptSideMargin = 34;
constexpr int kDeletePromptBottomMargin = 32;
constexpr int kDeletePromptRadius = 48;
constexpr int kDeletePromptInnerPadding = 32;
constexpr int kDeletePromptButtonGap = 20;
constexpr int kDeletePromptButtonHeight = 74;
constexpr int kDeletePromptButtonRadius = 24;
constexpr int kDeletePromptTitleTop = 34;
constexpr int kDeletePromptTitleHeight = 42;
constexpr int kDeletePromptTitleMessageGap = 8;
constexpr int kDeletePromptMessageButtonGap = 16;
constexpr int kProfileNameActionX = 158;
constexpr int kProfileNameActionRightMargin = 18;
constexpr int kProfileNameActionHeight = 58;
constexpr int kProfileNameActionHorizontalPadding = 12;
constexpr int kDefaultSpreadingFactorIndex = 7;
constexpr int kNavigationTitleTop = 78;
constexpr int kNavigationBodyTop = 148;
constexpr int kAddPageHeaderHeight = kNavigationBodyTop;
constexpr int kAddPageActionHeight = 124;
constexpr int kAddKeyboardHeightPercent = 35;
constexpr int kAddKeyboardTopGap = 12;
constexpr int kAddInputHeight = 70;
constexpr int kAddProfileNameSectionHeight = 126;
// 聊天时间线首尾与相邻区域保持一致的视觉间距。
constexpr int kChatTimelineInset = 18;
// 从底部离开该距离后显示“回到最新消息”按钮。
constexpr int kChatJumpRevealDistance = 24;
// 允许滚动回弹产生少量误差，避免按钮在底部反复闪烁。
constexpr int kChatBottomTolerance = 8;
constexpr int kChatJumpButtonSize = 70;
constexpr int kChatJumpButtonRightMargin = 24;
constexpr int kChatJumpButtonBottomGap = 16;
constexpr int kChatJumpButtonHiddenOffset = 12;
constexpr uint32_t kChatJumpAnimationMs = 180;
constexpr int kChatSignalMetricGap = 14;
// Font22 英文系统提示的保守字宽估算，避免保存回调同步遍历字体。
constexpr int kSystemMessageGlyphWidthEstimate = 13;
constexpr int kAddSwitchRowHeight = 108;
constexpr int kAddSwitchRowGap = 12;
constexpr int kProfileNameEditButtonSize = 62;
constexpr int kProfileNameEditButtonTop = 66;
constexpr int kProfileNameEditButtonSide = 18;
constexpr int kProfileNameEditTextAreaTop = 174;
constexpr int kProfileNameEditTextAreaHeight = 88;
constexpr int kProfileNameEditTextAreaSide = 26;
constexpr int kProfileNameEditHelpTop = 272;
constexpr int kProfileNameEditKeyboardHeightPercent = 35;
constexpr int kProfileSwitchWidth = 78;
constexpr int kProfileSwitchHeight = 44;
constexpr uint32_t kProfileSwitchAnimationMs = 180;
constexpr lv_style_selector_t kProfileSwitchCheckedIndicatorSelector =
    static_cast<lv_style_selector_t>(LV_PART_INDICATOR) |
    static_cast<lv_style_selector_t>(LV_STATE_CHECKED);
constexpr uint32_t kRadioCapabilitiesRefreshPeriodMs = 500;
constexpr uint32_t kRadioCommandTaskStackBytes = 8 * 1024;
constexpr UBaseType_t kRadioCommandTaskPriority = tskIDLE_PRIORITY;
constexpr uint32_t kRadioShutdownPollMs = 20;
// UI 回调超过该时间才记录回归日志，正常路径只读取 LVGL 毫秒 tick。
constexpr uint32_t kSlowRadioUiThresholdMs = 80;
constexpr int kFrequencyInputMaximumLength = 11;
constexpr int kFrequencyDecimalPlaces = 6;
constexpr char kFrequencyAcceptedChars[] = "0123456789.";
constexpr char kIntegerAcceptedChars[] = "0123456789";
constexpr char kHexAcceptedChars[] = "0123456789abcdefABCDEF";
constexpr char kProfileNameAcceptedChars[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_. ";
constexpr char kProfileCreatedMessage[] = "Radio profile created";
constexpr char kSettingsChangedMessage[] = "Settings changed";

enum class RadioActivationState {
  kNone,
  kPending,
  kInitializationFailed,
  kChipError,
  kHardwareUnavailable,
};

/**
 * @brief 保存本次运行期间各 Radio 配置的初始化状态
 *
 * 页面重建后仍保留真正的初始化失败和芯片错误，防止重新进入页面成为
 * 隐藏重试入口。硬件暂时不可用单独记录，重新接入后允许自动初始化一次。
 */
class RadioActivationRegistry {
 public:
  RadioActivationState GetState(uint32_t profile_id) {
    RadioActivationState state = RadioActivationState::kNone;
    portENTER_CRITICAL(&lock_);
    for (size_t index = 0; index < entry_count_; ++index) {
      if (entries_[index].profile_id == profile_id) {
        state = entries_[index].state;
        break;
      }
    }
    portEXIT_CRITICAL(&lock_);
    return state;
  }

  void SetState(uint32_t profile_id, RadioActivationState state) {
    if (profile_id == 0) {
      return;
    }
    portENTER_CRITICAL(&lock_);
    for (size_t index = 0; index < entry_count_; ++index) {
      if (entries_[index].profile_id != profile_id) {
        continue;
      }
      if (state == RadioActivationState::kNone) {
        for (size_t next = index + 1; next < entry_count_; ++next) {
          entries_[next - 1] = entries_[next];
        }
        --entry_count_;
      } else {
        entries_[index].state = state;
      }
      portEXIT_CRITICAL(&lock_);
      return;
    }
    if (state != RadioActivationState::kNone &&
        entry_count_ < std::size(entries_)) {
      entries_[entry_count_++] = {
          .profile_id = profile_id,
          .state = state,
      };
    }
    portEXIT_CRITICAL(&lock_);
  }

 private:
  struct Entry {
    uint32_t profile_id = 0;
    RadioActivationState state = RadioActivationState::kNone;
  };

  Entry entries_[app::kRadioProfileCapacity] = {};
  size_t entry_count_ = 0;
  portMUX_TYPE lock_ = portMUX_INITIALIZER_UNLOCKED;
};

RadioActivationRegistry& GetRadioActivationRegistry() {
  static RadioActivationRegistry registry;
  return registry;
}

bool IsProfileActivationBlocked(uint32_t profile_id) {
  const RadioActivationState state =
      GetRadioActivationRegistry().GetState(profile_id);
  return state == RadioActivationState::kPending ||
      state == RadioActivationState::kInitializationFailed ||
      state == RadioActivationState::kChipError;
}

void SetProfileActivationState(
    uint32_t profile_id, RadioActivationState state) {
  GetRadioActivationRegistry().SetState(profile_id, state);
}

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

struct RadioModuleItem {
  const char* short_name;
  const char* name;
  const char* latest_message;
  const char* time;
  uint32_t color;
  uint16_t unread_count = 0;
};

constexpr size_t kRadioModuleCapacity = app::kRadioProfileCapacity;
constexpr size_t kAddOutputPowerOptionCapacity = 8;
constexpr const char* kLr2021BandwidthNames[] = {
    "31.25", "41.67", "62.5", "83.34", "101.563", "125",
    "203", "250", "406", "500", "812", "1000"};
constexpr const char* kLr2021CodingRateNames[] = {
    "4/5", "4/6", "4/7", "4/8", "LI 4/5", "LI 4/6", "LI 4/8",
    "LI-C 4/6", "LI-C 4/8"};
constexpr const char* kLr2021RxBoostModeNames[] = {
    "Off", "1", "2", "3", "4", "5", "6", "7"};
static_assert(std::size(radio::kLr2021BandwidthsHz) ==
    std::size(kLr2021BandwidthNames));
static_assert(std::size(radio::kLr2021CodingRates) ==
    std::size(kLr2021CodingRateNames));
constexpr int8_t kCc1101OutputPowers[] = {
    -30, -20, -15, -10, 0, 5, 7, 10};
constexpr const char* kCc1101OutputPowerNames[] = {
    "-30", "-20", "-15", "-10", "0", "5", "7", "10"};
constexpr uint16_t kCc1101PreambleLengths[] = {
    16, 24, 32, 48, 64, 96, 128, 192};
constexpr const char* kCc1101PreambleLengthNames[] = {
    "16", "24", "32", "48", "64", "96", "128", "192"};
constexpr const char* kCc1101ReceiveBandwidthNames[] = {
    "58.036", "67.708", "81.250", "101.563",
    "116.071", "135.417", "162.500", "203.125",
    "232.143", "270.833", "325.000", "406.250",
    "464.286", "541.667", "650.000", "812.500"};
static_assert(std::size(kCc1101ReceiveBandwidthNames) ==
    std::size(radio::kCc1101ReceiveBandwidthsHz));
constexpr int8_t kNrf24l01OutputPowers[] = {-18, -12, -6, 0};
constexpr const char* kNrf24l01OutputPowerNames[] = {
    "-18", "-12", "-6", "0"};
constexpr uint32_t kNrf24l01DataRates[] = {
    250000, 1000000, 2000000};
constexpr const char* kNrf24l01DataRateNames[] = {"250K", "1M", "2M"};
using app::RadioChatDeliveryState;
using app::RadioChatMessage;
using app::RadioChatMessageType;

struct RenderedRadioChatMessage {
  // 仓库中的消息只在 LVGL 线程内更新，绘制期间保持有效。
  const RadioChatMessage* message = nullptr;
  // 消息缓存中的稳定序号，用于判断时间线内容是否变化。
  uint64_t sequence = 0;
  // 消息行相对时间线对象的顶部坐标。
  int y = 0;
  // 当前消息行的完整高度。
  int height = 0;
  // 气泡或系统提示容器的水平位置。
  int content_x = 0;
  // 气泡或系统提示容器的宽度。
  int content_width = 0;
  // 气泡或系统提示容器的高度。
  int content_height = 0;
  // 气泡正文的实际排版高度。
  int text_height = 0;
  // 发送状态或接收参数区域相对消息行的顶部坐标。
  int status_y = 0;
};

enum class RadioCommandType : uint8_t {
  kActivate,
  kDeactivate,
  kSend,
};

enum class RadioComposerMode : uint8_t {
  kActive,
  kInactive,
  kActivating,
  kChipError,
  kUnsupported,
  kUnavailable,
};

struct RadioCommandJob {
  // 后台任务发布结果前最后写入的完成标记。
  std::atomic<bool> completed = false;
  // 生命周期覆盖应用运行期的硬件 Provider。
  hal::RadioProvider* provider = nullptr;
  // 激活命令使用的完整配置快照。
  hal::RadioConfig config;
  // 发送命令使用的独立负载副本。
  uint8_t payload[hal::kRadioPayloadCapacity] = {};
  // payload 中的有效字节数量。
  size_t payload_size = 0;
  // 与聊天仓库发送状态关联的消息序号。
  uint64_t request_token = 0;
  // 当前任务需要执行的硬件命令。
  RadioCommandType type = RadioCommandType::kActivate;
  // 硬件命令的最终执行结果。
  bool success = false;
};

struct RadioShutdownJob {
  hal::RadioProvider* provider = nullptr;
  std::shared_ptr<RadioCommandJob> pending_command;
};

struct RadioViewState {
  AppViewConfig config;
  hal::RadioCapabilities capabilities;
  // 按配置索引保存各物理芯片会话的最近状态。
  hal::RadioStatus radio_statuses[kRadioModuleCapacity] = {};
  lv_obj_t* root = nullptr;
  lv_obj_t* detail_page = nullptr;
  lv_obj_t* app_settings_page = nullptr;
  lv_obj_t* profile_settings_page = nullptr;
  lv_obj_t* profile_settings_active_switch = nullptr;
  lv_obj_t* profile_settings_name_label = nullptr;
  lv_obj_t* profile_settings_chip_label = nullptr;
  lv_obj_t* profile_settings_header_status_label = nullptr;
  lv_obj_t* profile_name_edit_page = nullptr;
  lv_obj_t* profile_name_edit_text_area = nullptr;
  lv_obj_t* profile_name_edit_keyboard = nullptr;
  lv_obj_t* auto_send_page = nullptr;
  lv_obj_t* auto_send_body = nullptr;
  lv_obj_t* auto_send_switch = nullptr;
  lv_obj_t* auto_send_text_area = nullptr;
  lv_obj_t* auto_send_interval_area = nullptr;
  lv_obj_t* auto_send_action_area = nullptr;
  lv_obj_t* auto_send_keyboard = nullptr;
  lv_obj_t* detail_input = nullptr;
  lv_obj_t* detail_keyboard = nullptr;
  lv_obj_t* detail_composer_background = nullptr;
  lv_obj_t* detail_divider = nullptr;
  lv_obj_t* detail_send_button = nullptr;
  lv_obj_t* detail_composer_action_button = nullptr;
  lv_obj_t* detail_composer_action_label = nullptr;
  // 历史消息模式下显示的“回到最新消息”悬浮按钮。
  lv_obj_t* detail_chat_jump_button = nullptr;
  lv_obj_t* add_page = nullptr;
  lv_obj_t* add_body = nullptr;
  // 首次创建 Radio 配置时使用的名称输入框。
  lv_obj_t* add_name_input = nullptr;
  lv_obj_t* add_frequency_input = nullptr;
  lv_obj_t* add_power_input = nullptr;
  lv_obj_t* add_preamble_input = nullptr;
  lv_obj_t* add_sync_word_input = nullptr;
  lv_obj_t* add_data_rate_input = nullptr;
  lv_obj_t* add_frequency_deviation_input = nullptr;
  lv_obj_t* add_address_input = nullptr;
  lv_obj_t* add_address_width_input = nullptr;
  lv_obj_t* add_crc_length_input = nullptr;
  lv_obj_t* add_retransmit_count_input = nullptr;
  lv_obj_t* add_retransmit_delay_input = nullptr;
  lv_obj_t* add_crc_switch = nullptr;
  lv_obj_t* add_iq_switch = nullptr;
  lv_obj_t* add_rx_boost_switch = nullptr;
  lv_obj_t* add_whitening_switch = nullptr;
  lv_obj_t* add_fec_switch = nullptr;
  lv_obj_t* add_auto_ack_switch = nullptr;
  lv_obj_t* add_dynamic_payload_switch = nullptr;
  lv_obj_t* add_external_antenna_switch = nullptr;
  lv_obj_t* add_active_switch = nullptr;
  lv_obj_t* add_action_area = nullptr;
  lv_obj_t* add_keyboard = nullptr;
  lv_obj_t* add_submit_button = nullptr;
  lv_obj_t* add_submit_label = nullptr;
  lv_obj_t* add_button = nullptr;
  PromptDialogState delete_dialog;
  lv_obj_t* module_list = nullptr;
  lv_obj_t* header_area = nullptr;
  lv_obj_t* detail_chat_body = nullptr;
  // 聊天区仅保留一个无子控件的自绘时间线，避免滚动递归移动消息树。
  lv_obj_t* detail_chat_timeline = nullptr;
  lv_obj_t* detail_status_label = nullptr;
  lv_obj_t* detail_title_label = nullptr;
  lv_obj_t* detail_chip_label = nullptr;
  lv_timer_t* radio_timer = nullptr;
  lv_obj_t* add_chip_buttons[hal::kRadioCapabilityCapacity] = {};
  lv_obj_t* add_protocol_buttons[hal::kRadioCapabilityCapacity] = {};
  lv_obj_t* add_sf_buttons[8] = {};
  lv_obj_t* add_bandwidth_buttons[
      std::size(radio::kLr2021BandwidthsHz)] = {};
  lv_obj_t* add_coding_rate_buttons[
      std::size(radio::kLr2021CodingRates)] = {};
  lv_obj_t* add_rx_boost_buttons[8] = {};
  lv_obj_t* add_output_power_buttons[kAddOutputPowerOptionCapacity] = {};
  lv_obj_t* add_preamble_buttons[std::size(kCc1101PreambleLengths)] = {};
  lv_obj_t* add_receive_bandwidth_buttons[
      std::size(radio::kCc1101ReceiveBandwidthsHz)] = {};
  lv_obj_t* add_data_rate_buttons[std::size(kNrf24l01DataRates)] = {};
  NavigationDrawerState drawer;
  RadioModuleItem modules[kRadioModuleCapacity] = {};
  // 当前聊天页面最多保留 32 条轻量布局记录。
  RenderedRadioChatMessage
      rendered_chat_messages[app::kRadioChatPageCapacity] = {};
  char latest_messages[kRadioModuleCapacity][96] = {};
  char message_times[kRadioModuleCapacity][16] = {};
  uint16_t unread_counts[kRadioModuleCapacity] = {};
  bool selected_modules[kRadioModuleCapacity] = {};
  app::RadioPreferences preferences;
  // 正在后台执行的唯一射频硬件命令。
  std::shared_ptr<RadioCommandJob> radio_command_job;
  // 等待当前发送或控制命令结束后依次执行的多芯片控制请求。
  hal::RadioConfig pending_control_configs[kRadioModuleCapacity] = {};
  RadioCommandType pending_control_types[kRadioModuleCapacity] = {};
  size_t module_count = 0;
  // rendered_chat_messages 中当前有效布局数量。
  size_t rendered_chat_count = 0;
  // 当前增量时间线所属的 Radio 配置 ID。
  uint32_t rendered_chat_profile_id = 0;
  // 下一条聊天行在时间线中的顶部坐标。
  int rendered_chat_y = kChatTimelineInset;
  // 上一次滚动事件读取的纵向位置，用于识别用户滚动方向。
  int32_t chat_last_scroll_y = 0;
  int selected_add_chip = 0;
  int selected_add_protocol = 0;
  int selected_add_sf = kDefaultSpreadingFactorIndex;
  int selected_add_bandwidth = 1;
  int selected_add_coding_rate = 0;
  int selected_add_rx_boost_mode = 7;
  int selected_add_output_power = 0;
  int selected_add_preamble = 0;
  int selected_add_receive_bandwidth = 0;
  int selected_add_data_rate = 0;
  size_t detail_index = kRadioModuleCapacity;
  size_t profile_settings_index = kRadioModuleCapacity;
  size_t editing_index = kRadioModuleCapacity;
  // 单项删除确认期间使用配置 ID，避免列表索引变化后删错配置。
  uint32_t pending_delete_profile_id = 0;
  uint32_t last_capabilities_refresh_tick = 0;
  // 自动发送计时仅绑定当前启用配置，切换配置后重新开始一个完整周期。
  uint32_t auto_send_last_ticks[kRadioModuleCapacity] = {};
  size_t pending_control_count = 0;
  // 发送启动后到收到完成事件前保持为 true。
  bool transmit_in_flight = false;
  bool radio_status_available[kRadioModuleCapacity] = {};
  // 被子页面遮挡期间是否有会话摘要等待刷新。
  bool module_list_dirty = false;
  // 聊天页被上层页面遮挡时延后执行的自动滚动请求。
  bool chat_scroll_pending = false;
  // 当前视口是否继续自动跟随最新消息。
  bool chat_follow_latest = true;
  // 聊天输入键盘当前是否已经占用页面底部空间。
  bool detail_keyboard_visible = false;
  // 固定渲染页已满时是否有尚未装入时间线的新消息。
  bool chat_latest_page_pending = false;
  // 屏蔽程序滚动产生的 LVGL 滚动事件，避免误判为用户操作。
  bool chat_programmatic_scroll = false;
  // 回到底部按钮当前是否处于显示目标状态。
  bool chat_jump_button_visible = false;
  bool selection_mode = false;
  // 阻止详情页创建期间的快速重复点击再次进入配置。
  bool detail_opening = false;
  bool detail_closing = false;
  bool app_settings_closing = false;
  bool profile_settings_closing = false;
  bool profile_name_edit_closing = false;
  bool auto_send_closing = false;
  // 首次有效提交后锁定表单，防止连点重复创建或保存配置。
  bool add_submitting = false;
  bool add_closing = false;
};

struct RadioModuleAction {
  RadioViewState* state = nullptr;
  size_t index = 0;
  bool long_press_handled = false;
};

enum class RadioAddOptionGroup {
  kChip,
  kProtocol,
  kSpreadingFactor,
  kBandwidth,
  kCodingRate,
  kRxBoost,
  kOutputPower,
  kPreamble,
  kReceiveBandwidth,
  kDataRate,
};

struct RadioAddOptionAction {
  RadioViewState* state = nullptr;
  RadioAddOptionGroup group = RadioAddOptionGroup::kChip;
  int index = 0;
};

bool RenderModuleList(RadioViewState* state);
bool RenderHeader(RadioViewState* state);
bool RenderChatMessages(RadioViewState* state);
void MarkModuleListDirty(RadioViewState* state);
void RefreshModuleListIfVisible(RadioViewState* state);
void ResetRenderedChatState(RadioViewState* state);
void CloseSelectionMode(RadioViewState* state);
bool ShowAddModulePage(RadioViewState* state);
void CloseAddModulePage(RadioViewState* state);
bool ShowModuleSettings(RadioViewState* state, size_t index,
    bool from_detail);
bool ShowRadioSettingsPage(RadioViewState* state);
bool ShowProfileSettingsPage(RadioViewState* state, size_t index);
bool ShowProfileDeleteConfirmation(RadioViewState* state, size_t index);
bool ShowProfileNameEditPage(RadioViewState* state);
bool RebuildAddModuleForm(RadioViewState* state);
void ResetAddModuleFormPointers(RadioViewState* state);
void SetAddKeyboardVisible(
    RadioViewState* state, lv_obj_t* input, bool visible);
/**
 * @brief 解析文本输入框中的无符号 64 位整数
 * @param input 文本输入框
 * @param base 数字进制
 * @param minimum 最小允许值
 * @param maximum 最大允许值
 * @param value 解析结果输出
 * @return 文本完整且数值位于范围内时返回 true
 */
bool ParseTextAreaUint64(lv_obj_t* input, int base, uint64_t minimum,
    uint64_t maximum, uint64_t* value);

/**
 * @brief 解析 Enhanced ShockBurst 十六进制地址并推导地址宽度
 * @param input 地址文本输入框
 * @param address 地址数值输出
 * @param address_width 地址宽度输出，单位为字节
 * @return 地址包含 6、8 或 10 个十六进制字符时返回 true
 */
bool ParseEnhancedShockBurstAddress(
    lv_obj_t* input, uint64_t* address, uint8_t* address_width) {
  if (input == nullptr || address == nullptr || address_width == nullptr) {
    return false;
  }
  const char* text = lv_textarea_get_text(input);
  if (text == nullptr) {
    return false;
  }
  const size_t hex_digit_count = std::strlen(text);
  if (hex_digit_count != 6 && hex_digit_count != 8 &&
      hex_digit_count != 10) {
    return false;
  }
  for (size_t index = 0; index < hex_digit_count; ++index) {
    if (std::isxdigit(static_cast<unsigned char>(text[index])) == 0) {
      return false;
    }
  }
  if (!ParseTextAreaUint64(
          input, 16, 1, 0xFFFFFFFFFFULL, address)) {
    return false;
  }
  *address_width = static_cast<uint8_t>(hex_digit_count / 2);
  return true;
}

bool IsAddGfskFormComplete(const RadioViewState* state);
bool IsAddEnhancedShockBurstFormComplete(const RadioViewState* state);
/**
 * @brief 显示当前射频配置的自动发送设置页面
 * @param state Radio 页面状态
 * @return 显示成功返回 true，否则返回 false
 */
bool ShowAutoSendSettingsPage(RadioViewState* state);
/**
 * @brief 关闭自动发送设置页面
 * @param state Radio 页面状态
 * @param animated 是否播放退出动画
 */
void CloseAutoSendSettingsPage(RadioViewState* state, bool animated);
void RefreshProfileSettingsPage(RadioViewState* state);
bool SetProfileActiveState(
    RadioViewState* state, size_t index, bool active);
void UpdateChatComposerState(RadioViewState* state);

/**
 * @brief 获取 22 号 Google Sans 字体
 * @return 字体指针
 */
const lv_font_t* Font22() { return &lvgl_font_google_sans_flex_22; }

/**
 * @brief 应用 Radio 开关的当前主题颜色
 * @param switch_object 开关对象
 */
void ApplyRadioSwitchTheme(lv_obj_t* switch_object) {
  if (switch_object == nullptr) {
    return;
  }
  constexpr lv_style_selector_t kCheckedKnobSelector =
      static_cast<lv_style_selector_t>(LV_PART_KNOB) |
      static_cast<lv_style_selector_t>(LV_STATE_CHECKED);
  lv_obj_set_style_bg_color(switch_object,
      lv_color_hex(theme::ActiveThemeColors().surface_container_highest),
      LV_PART_MAIN);
  lv_obj_set_style_bg_opa(switch_object, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_bg_color(switch_object,
      lv_color_hex(theme::ActiveThemeColors().surface_container_lowest),
      LV_PART_KNOB);
  lv_obj_set_style_bg_color(switch_object, lv_color_hex(kOnPrimaryColor),
      kCheckedKnobSelector);
  lv_obj_set_style_bg_color(switch_object, lv_color_hex(kPrimaryColor),
      kProfileSwitchCheckedIndicatorSelector);
  lv_obj_set_style_bg_opa(switch_object, LV_OPA_COVER,
      kProfileSwitchCheckedIndicatorSelector);
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

const lv_font_t* Font32() { return &lvgl_font_google_sans_flex_32; }

/**
 * @brief 获取 36 号 Google Sans 字体
 * @return 字体指针
 */
const lv_font_t* Font36() { return &lvgl_font_google_sans_flex_36; }

/**
 * @brief 获取 44 号轮廓图标字体
 * @return 字体指针
 */
const lv_font_t* OutlineIconFont44() {
  return &lvgl_font_material_symbols_outline_44;
}

/**
 * @brief 获取 56 号轮廓图标字体
 * @return 字体指针
 */
const lv_font_t* OutlineIconFont56() {
  return &lvgl_font_material_symbols_outline_56;
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
 * @brief 仅在内容变化时更新标签，避免重复计算字体布局
 * @param label LVGL 标签
 * @param text 新文本
 * @return 标签内容发生变化时返回 true
 */
bool SetLabelTextIfChanged(lv_obj_t* label, const char* text) {
  if (label == nullptr) {
    return false;
  }
  const char* new_text = text == nullptr ? "" : text;
  const char* current_text = lv_label_get_text(label);
  if (current_text != nullptr &&
      std::strcmp(current_text, new_text) == 0) {
    return false;
  }
  lv_label_set_text(label, new_text);
  return true;
}

hal::RadioConfig ToRadioConfig(const app::RadioProfile& profile) {
  return {
      .client_token = profile.id,
      .chip = profile.chip,
      .protocol = profile.protocol,
      .antenna = profile.antenna,
      .lora = {
          .frequency_hz = profile.frequency_hz,
          .bandwidth_hz = profile.bandwidth_hz,
          .preamble_length = profile.preamble_length,
          .spreading_factor = profile.spreading_factor,
          .coding_rate_denominator = profile.coding_rate_denominator,
          .lr2021_coding_rate = profile.lr2021_coding_rate,
          .sync_word = profile.sync_word,
          .output_power_dbm = profile.output_power_dbm,
          .crc_enabled = profile.crc_enabled,
          .invert_iq = profile.invert_iq,
          .rx_boosted = profile.rx_boosted,
          .lr2021_rx_boost_mode = profile.lr2021_rx_boost_mode,
      },
      .gfsk = {
          .frequency_hz = profile.frequency_hz,
          .data_rate_bps = profile.gfsk_data_rate_bps,
          .frequency_deviation_hz = profile.gfsk_frequency_deviation_hz,
          .receive_bandwidth_hz = profile.gfsk_receive_bandwidth_hz,
          .preamble_length_bits = profile.preamble_length,
          .sync_word = profile.gfsk_sync_word,
          .output_power_dbm = profile.output_power_dbm,
          .crc_enabled = profile.crc_enabled,
          .whitening_enabled = profile.gfsk_whitening_enabled,
          .fec_enabled = profile.gfsk_fec_enabled,
      },
      .enhanced_shock_burst = {
          .channel = profile.esb_channel,
          .data_rate_bps = profile.esb_data_rate_bps,
          .address = profile.esb_address,
          .address_width = profile.esb_address_width,
          .output_power_dbm = profile.output_power_dbm,
          .crc_length_bits = profile.esb_crc_length_bits,
          .retransmit_count = profile.esb_retransmit_count,
          .retransmit_delay_us = profile.esb_retransmit_delay_us,
          .auto_ack_enabled = profile.esb_auto_ack_enabled,
          .dynamic_payload_enabled = profile.esb_dynamic_payload_enabled,
      },
  };
}

bool RadioConfigsEqual(
    const hal::RadioConfig& lhs, const hal::RadioConfig& rhs) {
  return lhs.client_token == rhs.client_token && lhs.chip == rhs.chip &&
      lhs.protocol == rhs.protocol && lhs.antenna == rhs.antenna &&
      lhs.lora.frequency_hz == rhs.lora.frequency_hz &&
      lhs.lora.bandwidth_hz == rhs.lora.bandwidth_hz &&
      lhs.lora.preamble_length == rhs.lora.preamble_length &&
      lhs.lora.spreading_factor == rhs.lora.spreading_factor &&
      lhs.lora.coding_rate_denominator == rhs.lora.coding_rate_denominator &&
      lhs.lora.lr2021_coding_rate == rhs.lora.lr2021_coding_rate &&
      lhs.lora.sync_word == rhs.lora.sync_word &&
      lhs.lora.output_power_dbm == rhs.lora.output_power_dbm &&
      lhs.lora.crc_enabled == rhs.lora.crc_enabled &&
      lhs.lora.invert_iq == rhs.lora.invert_iq &&
      lhs.lora.rx_boosted == rhs.lora.rx_boosted &&
      lhs.lora.lr2021_rx_boost_mode == rhs.lora.lr2021_rx_boost_mode &&
      lhs.gfsk.frequency_hz == rhs.gfsk.frequency_hz &&
      lhs.gfsk.data_rate_bps == rhs.gfsk.data_rate_bps &&
      lhs.gfsk.frequency_deviation_hz == rhs.gfsk.frequency_deviation_hz &&
      lhs.gfsk.receive_bandwidth_hz == rhs.gfsk.receive_bandwidth_hz &&
      lhs.gfsk.preamble_length_bits == rhs.gfsk.preamble_length_bits &&
      lhs.gfsk.sync_word == rhs.gfsk.sync_word &&
      lhs.gfsk.output_power_dbm == rhs.gfsk.output_power_dbm &&
      lhs.gfsk.crc_enabled == rhs.gfsk.crc_enabled &&
      lhs.gfsk.whitening_enabled == rhs.gfsk.whitening_enabled &&
      lhs.gfsk.fec_enabled == rhs.gfsk.fec_enabled &&
      lhs.enhanced_shock_burst.channel == rhs.enhanced_shock_burst.channel &&
      lhs.enhanced_shock_burst.data_rate_bps ==
          rhs.enhanced_shock_burst.data_rate_bps &&
      lhs.enhanced_shock_burst.address == rhs.enhanced_shock_burst.address &&
      lhs.enhanced_shock_burst.address_width ==
          rhs.enhanced_shock_burst.address_width &&
      lhs.enhanced_shock_burst.output_power_dbm ==
          rhs.enhanced_shock_burst.output_power_dbm &&
      lhs.enhanced_shock_burst.crc_length_bits ==
          rhs.enhanced_shock_burst.crc_length_bits &&
      lhs.enhanced_shock_burst.retransmit_count ==
          rhs.enhanced_shock_burst.retransmit_count &&
      lhs.enhanced_shock_burst.retransmit_delay_us ==
          rhs.enhanced_shock_burst.retransmit_delay_us &&
      lhs.enhanced_shock_burst.auto_ack_enabled ==
          rhs.enhanced_shock_burst.auto_ack_enabled &&
      lhs.enhanced_shock_burst.dynamic_payload_enabled ==
          rhs.enhanced_shock_burst.dynamic_payload_enabled;
}

const char* ChipDisplayName(radio::ChipType chip) {
  switch (chip) {
    case radio::ChipType::kSx1262:
      return "SX1262";
    case radio::ChipType::kLr2021:
      return "LR2021";
    case radio::ChipType::kLr1121:
      return "LR1121";
    case radio::ChipType::kCc1101:
      return "CC1101";
    case radio::ChipType::kNrf24l01:
      return "nRF24L01";
    default:
      return "Unknown chip";
  }
}

const char* ChipShortName(radio::ChipType chip) {
  switch (chip) {
    case radio::ChipType::kSx1262:
      return "SX";
    case radio::ChipType::kLr2021:
      return "LR";
    case radio::ChipType::kLr1121:
      return "LR";
    case radio::ChipType::kCc1101:
      return "CC";
    case radio::ChipType::kNrf24l01:
      return "nRF";
    default:
      return "Radio";
  }
}

const char* ProtocolDisplayName(radio::ProtocolType protocol) {
  switch (protocol) {
    case radio::ProtocolType::kLora:
      return "LoRa";
    case radio::ProtocolType::kGfsk:
      return "GFSK";
    case radio::ProtocolType::kEnhancedShockBurst:
      return "Enhanced ShockBurst";
    default:
      return "Unknown protocol";
  }
}

/**
 * @brief 判断射频能力是否覆盖指定中心频率
 * @param capability 当前板级射频能力
 * @param frequency_hz 中心频率，单位为 Hz
 * @return 任一有效频段覆盖目标频率时返回 true
 */
bool IsFrequencySupported(
    const hal::RadioCapability& capability, uint32_t frequency_hz) {
  const size_t band_count = std::min(
      capability.frequency_band_count, hal::kRadioFrequencyBandCapacity);
  for (size_t index = 0; index < band_count; ++index) {
    const hal::RadioFrequencyBand& band = capability.frequency_bands[index];
    if (band.minimum_hz <= band.maximum_hz &&
        frequency_hz >= band.minimum_hz &&
        frequency_hz <= band.maximum_hz) {
      return true;
    }
  }
  return false;
}

/**
 * @brief 判断两个射频配置的可编辑内容是否一致
 * @param lhs 第一个射频配置
 * @param rhs 第二个射频配置
 * @return 可编辑内容完全一致时返回 true
 */
bool AreProfileSettingsEqual(
    const app::RadioProfile& lhs, const app::RadioProfile& rhs) {
  return std::strcmp(lhs.name, rhs.name) == 0 &&
         lhs.chip == rhs.chip && lhs.protocol == rhs.protocol &&
         lhs.frequency_hz == rhs.frequency_hz &&
         lhs.bandwidth_hz == rhs.bandwidth_hz &&
         lhs.preamble_length == rhs.preamble_length &&
         lhs.spreading_factor == rhs.spreading_factor &&
         lhs.coding_rate_denominator == rhs.coding_rate_denominator &&
         lhs.lr2021_coding_rate == rhs.lr2021_coding_rate &&
         lhs.sync_word == rhs.sync_word &&
         lhs.output_power_dbm == rhs.output_power_dbm &&
         lhs.crc_enabled == rhs.crc_enabled &&
         lhs.invert_iq == rhs.invert_iq &&
         lhs.rx_boosted == rhs.rx_boosted &&
         lhs.lr2021_rx_boost_mode == rhs.lr2021_rx_boost_mode &&
         lhs.gfsk_data_rate_bps == rhs.gfsk_data_rate_bps &&
         lhs.gfsk_frequency_deviation_hz ==
             rhs.gfsk_frequency_deviation_hz &&
         lhs.gfsk_receive_bandwidth_hz == rhs.gfsk_receive_bandwidth_hz &&
         lhs.gfsk_sync_word == rhs.gfsk_sync_word &&
         lhs.gfsk_whitening_enabled == rhs.gfsk_whitening_enabled &&
         lhs.gfsk_fec_enabled == rhs.gfsk_fec_enabled &&
         lhs.esb_channel == rhs.esb_channel &&
         lhs.esb_data_rate_bps == rhs.esb_data_rate_bps &&
         lhs.esb_address == rhs.esb_address &&
         lhs.esb_address_width == rhs.esb_address_width &&
         lhs.esb_crc_length_bits == rhs.esb_crc_length_bits &&
         lhs.esb_retransmit_count == rhs.esb_retransmit_count &&
         lhs.esb_retransmit_delay_us == rhs.esb_retransmit_delay_us &&
         lhs.esb_auto_ack_enabled == rhs.esb_auto_ack_enabled &&
         lhs.esb_dynamic_payload_enabled == rhs.esb_dynamic_payload_enabled &&
         lhs.antenna == rhs.antenna;
}

bool IsProfileSupported(
    const RadioViewState* state, const app::RadioProfile& profile) {
  if (state == nullptr) {
    return false;
  }
  if (profile.antenna == radio::AntennaType::kExternal &&
      !state->capabilities.supports_external_antenna) {
    return false;
  }
  for (size_t index = 0; index < state->capabilities.count; ++index) {
    const hal::RadioCapability& capability =
        state->capabilities.entries[index];
    if (capability.chip == profile.chip &&
        capability.protocol == profile.protocol &&
        IsFrequencySupported(capability, profile.frequency_hz)) {
      return true;
    }
  }
  return false;
}

bool IsRadioConfigSupported(const hal::RadioCapabilities& capabilities,
    const hal::RadioConfig& config) {
  if (config.antenna == radio::AntennaType::kExternal &&
      !capabilities.supports_external_antenna) {
    return false;
  }
  const uint32_t frequency_hz =
      config.protocol == radio::ProtocolType::kEnhancedShockBurst
      ? static_cast<uint32_t>(2400U + config.enhanced_shock_burst.channel) *
          1000000U
      : (config.protocol == radio::ProtocolType::kGfsk
            ? config.gfsk.frequency_hz
            : config.lora.frequency_hz);
  const size_t capability_count = std::min(
      capabilities.count, hal::kRadioCapabilityCapacity);
  for (size_t index = 0; index < capability_count; ++index) {
    const hal::RadioCapability& capability = capabilities.entries[index];
    if (capability.chip == config.chip &&
        capability.protocol == config.protocol &&
        IsFrequencySupported(capability, frequency_hz)) {
      return true;
    }
  }
  return false;
}

/**
 * @brief 获取指定配置当前允许的最大负载长度
 * @param state Radio 页面状态
 * @param profile 射频配置
 * @return 能力可用时返回最大字节数，否则返回 0
 */
size_t MaximumPayloadSizeForProfile(
    const RadioViewState* state, const app::RadioProfile& profile) {
  if (state == nullptr) {
    return 0;
  }
  for (size_t index = 0; index < state->capabilities.count; ++index) {
    const hal::RadioCapability& capability =
        state->capabilities.entries[index];
    if (capability.chip == profile.chip &&
        capability.protocol == profile.protocol) {
      return capability.maximum_payload_size;
    }
  }
  return 0;
}

/**
 * @brief 获取当前设备首选的射频能力
 * @param state 射频页面状态
 * @return 能力可用时返回指针，否则返回 nullptr
 */
const hal::RadioCapability* PrimaryRadioCapability(
    const RadioViewState* state) {
  if (state == nullptr || state->capabilities.count == 0) {
    return nullptr;
  }
  const size_t index = state->selected_add_chip >= 0
      ? static_cast<size_t>(state->selected_add_chip)
      : 0;
  return &state->capabilities.entries[
      std::min(index, state->capabilities.count - 1)];
}

/**
 * @brief 让配置使用当前设备首选的射频芯片和协议
 * @param state 射频页面状态
 * @param profile 待更新的配置
 */
void ApplyPrimaryRadioCapability(
    const RadioViewState* state, app::RadioProfile* profile) {
  const hal::RadioCapability* capability = PrimaryRadioCapability(state);
  if (capability == nullptr || profile == nullptr) {
    return;
  }
  profile->chip = capability->chip;
  profile->protocol = capability->protocol;
}

/**
 * @brief 获取添加配置页面当前使用的射频芯片
 * @param state 射频页面状态
 * @return 当前设备首选芯片，无能力信息时返回 SX1262
 */
radio::ChipType AddProfileChip(const RadioViewState* state) {
  const hal::RadioCapability* capability = PrimaryRadioCapability(state);
  return capability == nullptr ? radio::ChipType::kSx1262 : capability->chip;
}

/**
 * @brief 获取当前芯片支持的离散发射功率数量
 * @param state 射频页面状态
 * @return 发射功率选项数量
 */
size_t AddProfileOutputPowerCount(const RadioViewState* state) {
  switch (AddProfileChip(state)) {
    case radio::ChipType::kCc1101:
      return std::size(kCc1101OutputPowers);
    case radio::ChipType::kNrf24l01:
      return std::size(kNrf24l01OutputPowers);
    default:
      return 0;
  }
}

/**
 * @brief 获取当前芯片指定索引的离散发射功率
 * @param state 射频页面状态
 * @param index 发射功率选项索引
 * @return 发射功率，索引无效时返回 0 dBm
 */
int8_t AddProfileOutputPower(const RadioViewState* state, size_t index) {
  switch (AddProfileChip(state)) {
    case radio::ChipType::kCc1101:
      return index < std::size(kCc1101OutputPowers)
          ? kCc1101OutputPowers[index]
          : 0;
    case radio::ChipType::kNrf24l01:
      return index < std::size(kNrf24l01OutputPowers)
          ? kNrf24l01OutputPowers[index]
          : 0;
    default:
      return 0;
  }
}

/**
 * @brief 获取当前芯片可编辑的 LoRa 带宽数量
 * @param state 射频页面状态
 * @return 带宽选项数量
 */
size_t AddProfileBandwidthCount(const RadioViewState* state) {
  switch (AddProfileChip(state)) {
    case radio::ChipType::kLr2021:
      return std::size(radio::kLr2021BandwidthsHz);
    case radio::ChipType::kLr1121:
      return 7;
    default:
      return 4;
  }
}

/**
 * @brief 获取当前芯片指定索引的 LoRa 带宽
 * @param state 射频页面状态
 * @param index 带宽选项索引
 * @return 带宽，索引无效时返回 0
 */
uint32_t AddProfileBandwidth(const RadioViewState* state, size_t index) {
  constexpr uint32_t kSx1262Bandwidths[] = {62500, 125000, 250000, 500000};
  constexpr uint32_t kLr1121Bandwidths[] = {
      62500, 125000, 200000, 250000, 400000, 500000, 800000};
  if (AddProfileChip(state) == radio::ChipType::kLr2021) {
    return index < std::size(radio::kLr2021BandwidthsHz)
        ? radio::kLr2021BandwidthsHz[index]
        : 0;
  }
  if (AddProfileChip(state) == radio::ChipType::kLr1121) {
    return index < std::size(kLr1121Bandwidths) ? kLr1121Bandwidths[index] : 0;
  }
  return index < std::size(kSx1262Bandwidths) ? kSx1262Bandwidths[index] : 0;
}

/**
 * @brief 获取当前芯片带宽选项的显示文本
 * @param state 射频页面状态
 * @param index 带宽选项索引
 * @return 带宽显示文本
 */
const char* AddProfileBandwidthName(const RadioViewState* state, size_t index) {
  constexpr const char* kSx1262Names[] = {"62.5", "125", "250", "500"};
  constexpr const char* kLr1121Names[] = {
      "62.5", "125", "203", "250", "406", "500", "812"};
  if (AddProfileChip(state) == radio::ChipType::kLr2021) {
    return index < std::size(kLr2021BandwidthNames)
        ? kLr2021BandwidthNames[index]
        : "";
  }
  if (AddProfileChip(state) == radio::ChipType::kLr1121) {
    return index < std::size(kLr1121Names) ? kLr1121Names[index] : "";
  }
  return index < std::size(kSx1262Names) ? kSx1262Names[index] : "";
}

size_t AddProfileCodingRateCount(const RadioViewState* state) {
  return AddProfileChip(state) == radio::ChipType::kLr2021
      ? std::size(radio::kLr2021CodingRates)
      : 4;
}

radio::Lr2021CodingRate AddProfileLr2021CodingRate(
    const RadioViewState* state, size_t index) {
  if (AddProfileChip(state) != radio::ChipType::kLr2021 ||
      index >= std::size(radio::kLr2021CodingRates)) {
    return radio::Lr2021CodingRate::kStandard4_5;
  }
  return radio::kLr2021CodingRates[index];
}

const char* AddProfileCodingRateName(
    const RadioViewState* state, size_t index) {
  constexpr const char* kStandardCodingRateNames[] = {
      "4/5", "4/6", "4/7", "4/8"};
  if (AddProfileChip(state) == radio::ChipType::kLr2021) {
    return index < std::size(kLr2021CodingRateNames)
        ? kLr2021CodingRateNames[index]
        : "";
  }
  return index < std::size(kStandardCodingRateNames)
      ? kStandardCodingRateNames[index]
      : "";
}

size_t FindProfileIndex(const RadioViewState* state, uint32_t profile_id) {
  if (state == nullptr) {
    return kRadioModuleCapacity;
  }
  for (size_t index = 0; index < state->module_count; ++index) {
    if (state->preferences.profiles[index].id == profile_id) {
      return index;
    }
  }
  return kRadioModuleCapacity;
}

void RecordProfileChipError(
    RadioViewState* state, uint32_t profile_id) {
  if (state == nullptr || profile_id == 0 ||
      GetRadioActivationRegistry().GetState(profile_id) ==
          RadioActivationState::kPending) {
    return;
  }
  const size_t profile_index = FindProfileIndex(state, profile_id);
  const bool hardware_available = profile_index < state->module_count &&
      IsProfileSupported(
          state, state->preferences.profiles[profile_index]);
  SetProfileActivationState(profile_id,
      hardware_available ? RadioActivationState::kChipError
                         : RadioActivationState::kHardwareUnavailable);
}

void FormatCurrentTime(const RadioViewState* state, char* output,
    size_t output_size) {
  if (output == nullptr || output_size == 0) {
    return;
  }
  const app::SystemStatusCache* system_status =
      state == nullptr ? nullptr : state->config.system_status;
  if (system_status != nullptr && system_status->rtc_status_valid()) {
    const hal::RtcStatus& status = system_status->rtc_status();
    std::snprintf(output, output_size, "%02u:%02u",
        static_cast<unsigned>(status.hour),
        static_cast<unsigned>(status.minute));
    return;
  }
  std::snprintf(output, output_size, "Now");
}

/**
 * @brief 判断指定配置是否有等待执行或正在执行的激活命令
 * @param state Radio 页面状态
 * @param profile_id 配置 ID
 * @return 激活命令尚未完成时返回 true
 */
bool IsProfileActivationPending(
    const RadioViewState* state, uint32_t profile_id) {
  if (state == nullptr || profile_id == 0) {
    return false;
  }
  for (size_t index = 0; index < state->pending_control_count; ++index) {
    if (state->pending_control_types[index] == RadioCommandType::kActivate &&
        state->pending_control_configs[index].client_token == profile_id) {
      return true;
    }
  }
  return state->radio_command_job != nullptr &&
      state->radio_command_job->type == RadioCommandType::kActivate &&
      state->radio_command_job->config.client_token == profile_id;
}

/**
 * @brief 获取聊天输入区域对应的运行状态
 * @param state Radio 页面状态
 * @param index Radio 配置索引
 * @return 当前输入区域模式
 */
RadioComposerMode GetRadioComposerMode(
    const RadioViewState* state, size_t index) {
  if (state == nullptr || index >= state->module_count) {
    return RadioComposerMode::kUnavailable;
  }
  if (state->config.radio == nullptr) {
    return RadioComposerMode::kUnavailable;
  }
  const app::RadioProfile& profile = state->preferences.profiles[index];
  if (!IsProfileSupported(state, profile)) {
    return RadioComposerMode::kUnsupported;
  }
  if (!profile.active) {
    return RadioComposerMode::kInactive;
  }
  if (IsProfileActivationPending(state, profile.id)) {
    return RadioComposerMode::kActivating;
  }
  if (!state->radio_status_available[index] ||
      state->radio_statuses[index].state ==
          hal::RadioLinkState::kChipError) {
    return RadioComposerMode::kChipError;
  }
  if (state->radio_statuses[index].state == hal::RadioLinkState::kActive &&
      state->radio_statuses[index].active_client_token == profile.id) {
    return RadioComposerMode::kActive;
  }
  return RadioComposerMode::kChipError;
}

const char* ProfileStatusText(const RadioViewState* state, size_t index) {
  switch (GetRadioComposerMode(state, index)) {
    case RadioComposerMode::kActive:
      return "Active";
    case RadioComposerMode::kActivating:
      return "Activating";
    case RadioComposerMode::kChipError:
      return "Chip error";
    case RadioComposerMode::kUnsupported:
      return "Unsupported";
    case RadioComposerMode::kUnavailable:
      return "Unavailable";
    case RadioComposerMode::kInactive:
    default:
      return "Inactive";
  }
}

uint32_t ProfileStatusColor(const char* status) {
  if (status != nullptr && std::strcmp(status, "Active") == 0) {
    return theme::ActiveThemeColors().success;
  }
  if (status != nullptr && std::strcmp(status, "Activating") == 0) {
    return kPrimaryColor;
  }
  if (status != nullptr && std::strcmp(status, "Chip error") == 0) {
    return theme::ActiveThemeColors().error;
  }
  if (status != nullptr &&
      (std::strcmp(status, "Unsupported") == 0 ||
          std::strcmp(status, "Unavailable") == 0)) {
    return theme::ActiveThemeColors().warning;
  }
  return theme::ActiveThemeColors().on_surface_variant;
}

/**
 * @brief 获取射频主列表状态标记颜色
 * @param state 射频页面状态
 * @param index 射频配置索引
 * @return 当前配置对应的状态标记颜色
 */
uint32_t ProfileIndicatorColor(
    const RadioViewState* state, size_t index) {
  const char* status = ProfileStatusText(state, index);
  if (std::strcmp(status, "Active") == 0) {
    return theme::ActiveThemeColors().success;
  }
  if (std::strcmp(status, "Activating") == 0) {
    return kPrimaryColor;
  }
  if (std::strcmp(status, "Chip error") == 0) {
    return theme::ActiveThemeColors().error;
  }
  return theme::ActiveThemeColors().outline_variant;
}

/**
 * @brief 同步 Radio 配置列表与聊天摘要的运行时显示数据
 * @param state Radio 页面状态
 */
void SyncModuleItems(RadioViewState* state) {
  if (state == nullptr) {
    return;
  }
  state->module_count = state->preferences.profile_count;
  app::RadioChatRepository& repository = app::GetRadioChatRepository();
  for (size_t index = 0; index < state->module_count; ++index) {
    app::RadioChatProfileSummary summary;
    if (repository.GetProfileSummary(
            state->preferences.profiles[index].id, &summary)) {
      CopyBoundedString(state->latest_messages[index],
          sizeof(state->latest_messages[index]), summary.latest_message);
      CopyBoundedString(state->message_times[index],
          sizeof(state->message_times[index]), summary.latest_time);
      state->unread_counts[index] = summary.unread_count;
    } else {
      state->latest_messages[index][0] = '\0';
      state->message_times[index][0] = '\0';
      state->unread_counts[index] = 0;
    }
    state->modules[index] = {
        .short_name = ChipShortName(state->preferences.profiles[index].chip),
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
 * @brief 等待正在执行的射频命令退出，然后让芯片进入休眠状态
 * @param context 射频关闭任务上下文
 */
void RadioShutdownTaskEntry(void* context) {
  std::unique_ptr<RadioShutdownJob> job(
      static_cast<RadioShutdownJob*>(context));
  if (job != nullptr) {
    while (job->pending_command != nullptr &&
           !job->pending_command->completed.load(std::memory_order_acquire)) {
      vTaskDelay(pdMS_TO_TICKS(kRadioShutdownPollMs));
    }
    if (job->provider != nullptr && !job->provider->DeactivateRadio()) {
      LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
          "Radio power down on view close failed\n");
    }
  }
  vTaskDelete(nullptr);
}

/**
 * @brief 释放射频页面状态，并在后台关闭射频芯片
 * @param event LVGL 事件对象
 */
void RadioViewDeleteEventCallback(lv_event_t* event) {
  if (lv_event_get_code(event) != LV_EVENT_DELETE) {
    return;
  }
  auto* state = static_cast<RadioViewState*>(lv_event_get_user_data(event));
  if (state != nullptr && state->radio_timer != nullptr) {
    lv_timer_delete(state->radio_timer);
    state->radio_timer = nullptr;
  }
  if (state != nullptr) {
    // 页面销毁后尚未启动的队列项会随状态一起释放，不能把它们继续标记
    // 为正在初始化。已经进入 worker 的命令由 worker 发布最终状态。
    for (size_t index = 0; index < state->pending_control_count; ++index) {
      if (state->pending_control_types[index] ==
          RadioCommandType::kActivate) {
        SetProfileActivationState(
            state->pending_control_configs[index].client_token,
            RadioActivationState::kNone);
      }
    }
  }
  if (state != nullptr && state->config.radio != nullptr) {
    auto* shutdown_job = new (std::nothrow) RadioShutdownJob{
        .provider = state->config.radio,
        .pending_command = state->radio_command_job,
    };
    if (shutdown_job == nullptr ||
        xTaskCreate(RadioShutdownTaskEntry, "radio_shutdown",
            kRadioCommandTaskStackBytes, shutdown_job,
            kRadioCommandTaskPriority, nullptr) != pdPASS) {
      delete shutdown_job;
      state->config.radio->DeactivateRadio();
    }
  }
  delete state;
}

/**
 * @brief 释放模块列表点击参数
 * @param event LVGL 事件对象
 */
void ModuleActionDeleteEventCallback(lv_event_t* event) {
  if (lv_event_get_code(event) == LV_EVENT_DELETE) {
    delete static_cast<RadioModuleAction*>(lv_event_get_user_data(event));
  }
}

/**
 * @brief 处理详情页退出动画完成事件
 * @param animation LVGL 动画对象
 */
void DetailCloseCompletedCallback(lv_anim_t* animation) {
  auto* state = static_cast<RadioViewState*>(
      lv_anim_get_user_data(animation));
  if (state != nullptr && state->detail_page != nullptr) {
    lv_obj_t* page = state->detail_page;
    state->detail_page = nullptr;
    state->detail_input = nullptr;
    state->detail_keyboard = nullptr;
    state->detail_keyboard_visible = false;
    state->detail_composer_background = nullptr;
    state->detail_divider = nullptr;
    state->detail_send_button = nullptr;
    state->detail_composer_action_button = nullptr;
    state->detail_composer_action_label = nullptr;
    state->detail_chat_jump_button = nullptr;
    state->detail_chat_body = nullptr;
    state->detail_chat_timeline = nullptr;
    state->detail_status_label = nullptr;
    state->detail_title_label = nullptr;
    state->detail_chip_label = nullptr;
    state->detail_index = kRadioModuleCapacity;
    state->detail_closing = false;
    ResetRenderedChatState(state);
    lv_obj_delete(page);
    RefreshModuleListIfVisible(state);
  }
}

/**
 * @brief 关闭射频模块信息详情页
 * @param state 射频页面状态
 */
void CloseModuleDetail(RadioViewState* state) {
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
    state->detail_keyboard_visible = false;
    state->detail_composer_background = nullptr;
    state->detail_divider = nullptr;
    state->detail_send_button = nullptr;
    state->detail_composer_action_button = nullptr;
    state->detail_composer_action_label = nullptr;
    state->detail_chat_jump_button = nullptr;
    state->detail_chat_body = nullptr;
    state->detail_chat_timeline = nullptr;
    state->detail_status_label = nullptr;
    state->detail_title_label = nullptr;
    state->detail_chip_label = nullptr;
    state->detail_index = kRadioModuleCapacity;
    state->detail_closing = false;
    ResetRenderedChatState(state);
    lv_obj_delete(page);
    RefreshModuleListIfVisible(state);
  }
}

/**
 * @brief 处理详情页返回按钮点击事件
 * @param event LVGL 事件对象
 */
void DetailBackClickedEventCallback(lv_event_t* event) {
  if (lv_event_get_code(event) == LV_EVENT_CLICKED) {
    CloseModuleDetail(
        static_cast<RadioViewState*>(lv_event_get_user_data(event)));
  }
}

void DetailHeaderClickedEventCallback(lv_event_t* event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
    return;
  }
  auto* state = static_cast<RadioViewState*>(lv_event_get_user_data(event));
  if (state != nullptr && state->detail_index < state->module_count) {
    ShowProfileSettingsPage(state, state->detail_index);
  }
}

/**
 * @brief 绘制一个聊天区域矩形
 * @param layer LVGL 绘制层
 * @param x 左侧坐标
 * @param y 顶部坐标
 * @param width 宽度
 * @param height 高度
 * @param color 填充颜色
 * @param radius 圆角半径
 */
void DrawChatRectangle(lv_layer_t* layer, int x, int y, int width,
    int height, uint32_t color, int radius) {
  if (layer == nullptr || width <= 0 || height <= 0) {
    return;
  }
  lv_draw_rect_dsc_t descriptor;
  lv_draw_rect_dsc_init(&descriptor);
  descriptor.bg_color = lv_color_hex(color);
  descriptor.bg_opa = LV_OPA_COVER;
  descriptor.border_opa = LV_OPA_TRANSP;
  descriptor.radius = radius;
  lv_area_t area = {};
  area.x1 = x;
  area.y1 = y;
  area.x2 = x + width - 1;
  area.y2 = y + height - 1;
  lv_draw_rect(layer, &descriptor, &area);
}

/**
 * @brief 在聊天自绘对象中绘制一段文本
 * @param layer LVGL 绘制层
 * @param text 文本
 * @param color 文本颜色
 * @param font 字体
 * @param x 左侧坐标
 * @param y 顶部坐标
 * @param width 文本区域宽度
 * @param height 文本区域高度
 * @param alignment 文本水平对齐方式
 * @param local_text 文本是否来自当前回调的临时缓冲区
 * @param text_flags LVGL 文本排版标志
 */
void DrawChatText(lv_layer_t* layer, const char* text, uint32_t color,
    const lv_font_t* font, int x, int y, int width, int height,
    lv_text_align_t alignment, bool local_text,
    lv_text_flag_t text_flags = LV_TEXT_FLAG_NONE) {
  if (layer == nullptr || text == nullptr || font == nullptr ||
      width <= 0 || height <= 0) {
    return;
  }
  lv_draw_label_dsc_t descriptor;
  lv_draw_label_dsc_init(&descriptor);
  descriptor.text = text;
  descriptor.text_local = local_text ? 1 : 0;
  descriptor.font = font;
  descriptor.color = lv_color_hex(color);
  descriptor.align = alignment;
  descriptor.flag = text_flags;
  lv_area_t area = {};
  area.x1 = x;
  area.y1 = y;
  area.x2 = x + width - 1;
  area.y2 = y + height - 1;
  lv_draw_label(layer, &descriptor, &area);
}

/**
 * @brief 绘制聊天时间线中一条系统提示
 * @param layer LVGL 绘制层
 * @param timeline_x 时间线对象左侧绝对坐标
 * @param row_y 消息行顶部绝对坐标
 * @param rendered 消息布局
 */
void DrawSystemChatMessage(lv_layer_t* layer, int timeline_x, int row_y,
    const RenderedRadioChatMessage& rendered) {
  const RadioChatMessage* message = rendered.message;
  if (message == nullptr) {
    return;
  }
  const int box_x = timeline_x + rendered.content_x;
  DrawChatRectangle(layer, box_x, row_y, rendered.content_width,
      rendered.content_height, theme::ActiveThemeColors().surface_container_highest, 21);
  DrawChatText(layer, message->text, theme::ActiveThemeColors().on_surface_variant, Font22(),
      box_x + 14, row_y + 8, rendered.content_width - 28,
      rendered.content_height - 16, LV_TEXT_ALIGN_CENTER, false);
}

/**
 * @brief 绘制聊天时间线中一条用户消息
 * @param layer LVGL 绘制层
 * @param timeline_x 时间线对象左侧绝对坐标
 * @param timeline_width 时间线宽度
 * @param row_y 消息行顶部绝对坐标
 * @param rendered 消息布局
 */
void DrawUserChatMessage(lv_layer_t* layer, int timeline_x,
    int timeline_width, int row_y,
    const RenderedRadioChatMessage& rendered) {
  const RadioChatMessage* message = rendered.message;
  if (message == nullptr) {
    return;
  }
  const bool outgoing =
      message->delivery != RadioChatDeliveryState::kReceived;
  const uint32_t bubble_color =
      outgoing ? kPrimaryColor : theme::ActiveThemeColors().surface_container;
  const uint32_t text_color = outgoing ? kOnPrimaryColor : theme::ActiveThemeColors().on_surface;
  const int bubble_x = timeline_x + rendered.content_x;
  DrawChatRectangle(layer, bubble_x, row_y, rendered.content_width,
      rendered.content_height, bubble_color, 20);
  const int corner_x = outgoing
      ? bubble_x + rendered.content_width - 20
      : bubble_x;
  DrawChatRectangle(layer, corner_x,
      row_y + rendered.content_height - 20, 20, 20,
      bubble_color, 6);
  DrawChatText(layer, message->text, text_color, Font24(),
      bubble_x + 18,
      row_y + (rendered.content_height - rendered.text_height) / 2,
      rendered.content_width - 36, rendered.text_height,
      LV_TEXT_ALIGN_LEFT, false);

  const int status_y = row_y + rendered.status_y;
  if (!outgoing) {
    if (!message->rssi_valid && !message->snr_valid) {
      DrawChatText(layer, message->time, theme::ActiveThemeColors().on_surface_variant, Font22(),
          timeline_x + 28, status_y, 180, 30,
          LV_TEXT_ALIGN_LEFT, false, LV_TEXT_FLAG_EXPAND);
      return;
    }
    char rssi[32] = {};
    char snr[32] = {};
    const int signal_metrics_x = timeline_x + 28;
    int next_signal_metric_x = signal_metrics_x;
    if (message->rssi_valid) {
      std::snprintf(rssi, sizeof(rssi), "RSSI %d dBm",
          static_cast<int>(message->rssi_dbm));
      lv_point_t rssi_size = {};
      lv_text_get_size(&rssi_size, rssi, Font22(), 0, 0,
          LV_COORD_MAX, LV_TEXT_FLAG_EXPAND);
      const int rssi_width = std::max(1, static_cast<int>(rssi_size.x));
      DrawChatText(layer, rssi, theme::ActiveThemeColors().on_surface_variant, Font22(),
          next_signal_metric_x, status_y, rssi_width, 30,
          LV_TEXT_ALIGN_LEFT, true, LV_TEXT_FLAG_EXPAND);
      next_signal_metric_x += rssi_width + kChatSignalMetricGap;
    }
    if (message->snr_valid) {
      std::snprintf(snr, sizeof(snr), "SNR %+d",
          static_cast<int>(message->snr_db));
      lv_point_t snr_size = {};
      lv_text_get_size(&snr_size, snr, Font22(), 0, 0,
          LV_COORD_MAX, LV_TEXT_FLAG_EXPAND);
      const int snr_width = std::max(1, static_cast<int>(snr_size.x));
      DrawChatText(layer, snr, theme::ActiveThemeColors().on_surface_variant, Font22(),
          next_signal_metric_x, status_y, snr_width, 30,
          LV_TEXT_ALIGN_LEFT, true, LV_TEXT_FLAG_EXPAND);
    }
    DrawChatText(layer, message->time, theme::ActiveThemeColors().on_surface_variant, Font22(),
        timeline_x + 28, status_y + 28, 180, 30,
        LV_TEXT_ALIGN_LEFT, false, LV_TEXT_FLAG_EXPAND);
    return;
  }

  const bool sending =
      message->delivery == RadioChatDeliveryState::kSending;
  const bool success =
      message->delivery == RadioChatDeliveryState::kSent;
  char status_text[32] = {};
  std::snprintf(status_text, sizeof(status_text), "%s%s",
      sending ? "Sending  " : "", message->time);
  const int time_right = timeline_x + timeline_width -
      (sending ? 28 : 66);
  DrawChatText(layer, status_text, theme::ActiveThemeColors().on_surface_variant, Font22(),
      timeline_x + 28, status_y, time_right - timeline_x - 28, 30,
      LV_TEXT_ALIGN_RIGHT, true);
  if (!sending) {
    DrawChatText(layer, success ? icon::kCheck : icon::kClose,
        success ? theme::ActiveThemeColors().success
                : theme::ActiveThemeColors().error,
        FillIconFont32(), timeline_x + timeline_width - 62,
        status_y - 5, 34, 40, LV_TEXT_ALIGN_RIGHT, false);
  }
}

/**
 * @brief 绘制聊天时间线当前可见的消息
 * @param event LVGL 绘制事件
 */
void ChatTimelineDrawEventCallback(lv_event_t* event) {
  if (lv_event_get_code(event) != LV_EVENT_DRAW_MAIN) {
    return;
  }
  auto* state =
      static_cast<RadioViewState*>(lv_event_get_user_data(event));
  lv_obj_t* timeline = lv_event_get_target_obj(event);
  lv_layer_t* layer = lv_event_get_layer(event);
  if (state == nullptr || timeline == nullptr || layer == nullptr ||
      state->detail_chat_body == nullptr) {
    return;
  }
  lv_area_t timeline_area = {};
  lv_area_t body_area = {};
  lv_obj_get_coords(timeline, &timeline_area);
  lv_obj_get_coords(state->detail_chat_body, &body_area);
  const int timeline_width = lv_area_get_width(&timeline_area);
  for (size_t index = 0; index < state->rendered_chat_count; ++index) {
    const RenderedRadioChatMessage& rendered =
        state->rendered_chat_messages[index];
    const int row_y = timeline_area.y1 + rendered.y;
    if (row_y + rendered.height < body_area.y1 ||
        row_y > body_area.y2) {
      continue;
    }
    if (rendered.message == nullptr ||
        rendered.message->sequence != rendered.sequence) {
      continue;
    }
    if (rendered.message->type == RadioChatMessageType::kSystem) {
      DrawSystemChatMessage(
          layer, timeline_area.x1, row_y, rendered);
    } else {
      DrawUserChatMessage(layer, timeline_area.x1,
          timeline_width, row_y, rendered);
    }
  }
}

/**
 * @brief 清空聊天自绘时间线的布局状态
 * @param state Radio 页面状态
 */
void ResetRenderedChatState(RadioViewState* state) {
  if (state == nullptr) {
    return;
  }
  for (RenderedRadioChatMessage& message :
       state->rendered_chat_messages) {
    message = RenderedRadioChatMessage();
  }
  state->rendered_chat_count = 0;
  state->rendered_chat_profile_id = 0;
  state->rendered_chat_y = kChatTimelineInset;
  state->chat_last_scroll_y = 0;
  state->chat_scroll_pending = false;
  state->chat_follow_latest = true;
  state->chat_latest_page_pending = false;
  state->chat_programmatic_scroll = false;
  state->chat_jump_button_visible = false;
}

/**
 * @brief 计算一条消息在自绘时间线中的布局
 * @param state Radio 页面状态
 * @param message 聊天消息
 * @param y 消息行顶部坐标
 * @param output 布局输出
 * @return 参数和布局有效时返回 true
 */
bool LayoutChatMessage(RadioViewState* state,
    const RadioChatMessage& message, int y,
    RenderedRadioChatMessage* output) {
  if (state == nullptr || output == nullptr) {
    return false;
  }
  RenderedRadioChatMessage rendered;
  rendered.message = &message;
  rendered.sequence = message.sequence;
  rendered.y = y;
  lv_point_t text_size = {};
  if (message.type == RadioChatMessageType::kSystem) {
    const int max_label_width =
        std::max(1, state->config.width - 92);
    const int estimated_width = std::max(1,
        static_cast<int>(std::strlen(message.text)) *
            kSystemMessageGlyphWidthEstimate);
    const int label_width =
        std::min(estimated_width, max_label_width);
    const int line_count = std::max(
        1, (estimated_width + max_label_width - 1) / max_label_width);
    const int text_height =
        lv_font_get_line_height(Font22()) * line_count;
    rendered.content_width = label_width + 28;
    rendered.content_height = text_height + 16;
    rendered.content_x =
        (state->config.width - rendered.content_width) / 2;
    rendered.text_height = text_height;
    rendered.height = rendered.content_height + 18;
    *output = rendered;
    return true;
  }

  const int max_bubble_width =
      std::max(90, state->config.width - 100);
  lv_text_get_size(&text_size, message.text, Font24(), 0, 0,
      LV_COORD_MAX, LV_TEXT_FLAG_NONE);
  const int unwrapped_width = static_cast<int>(text_size.x);
  rendered.content_width = std::clamp(
      unwrapped_width + 36, 90, max_bubble_width);
  const int label_width = rendered.content_width - 36;
  if (unwrapped_width > label_width) {
    lv_text_get_size(&text_size, message.text, Font24(), 0, 0,
        label_width, LV_TEXT_FLAG_NONE);
  }
  rendered.text_height = static_cast<int>(text_size.y);
  rendered.content_height = std::max(rendered.text_height + 28, 64);
  const bool outgoing =
      message.delivery != RadioChatDeliveryState::kReceived;
  rendered.content_x = outgoing
      ? state->config.width - rendered.content_width - 28
      : 28;
  rendered.status_y = rendered.content_height + 8;
  const bool signal_metrics_available =
      message.rssi_valid || message.snr_valid;
  rendered.height = rendered.status_y +
      (outgoing || !signal_metrics_available ? 48 : 76);
  *output = rendered;
  return true;
}

/**
 * @brief 计算聊天时间线底部对应的滚动位置
 * @param body 聊天消息区域
 * @param content_height 聊天时间线完整内容高度
 * @param viewport_height 已知的新视口高度，负数表示读取当前高度
 * @return 滚动到底部所需的纵向位置
 */
int32_t ChatBottomScrollY(
    lv_obj_t* body, int content_height, int viewport_height = -1) {
  if (body == nullptr) {
    return 0;
  }
  const int body_height = viewport_height >= 0
      ? viewport_height
      : lv_obj_get_height(body);
  return std::max<int32_t>(
      0, content_height + kChatTimelineInset - body_height);
}

/**
 * @brief 判断当前聊天视口是否位于最新消息附近
 * @param state Radio 页面状态
 * @return 距离底部不超过容差时返回 true
 */
bool IsChatAtBottom(const RadioViewState* state) {
  if (state == nullptr || state->detail_chat_body == nullptr) {
    return true;
  }
  const int32_t target = ChatBottomScrollY(state->detail_chat_body,
      state->rendered_chat_y);
  const int32_t current =
      lv_obj_get_scroll_y(state->detail_chat_body);
  return target - current <= kChatBottomTolerance;
}

/**
 * @brief 设置聊天回到底部按钮的纵向位置
 * @param object LVGL 按钮对象
 * @param y 目标纵坐标
 */
void SetChatJumpButtonY(void* object, int32_t y) {
  if (object != nullptr) {
    lv_obj_set_y(static_cast<lv_obj_t*>(object), y);
  }
}

/**
 * @brief 获取聊天回到底部按钮的显示位置
 * @param state Radio 页面状态
 * @return 按钮显示时的纵坐标
 */
int ChatJumpButtonVisibleY(const RadioViewState* state) {
  if (state == nullptr) {
    return 0;
  }
  const int divider_y = state->detail_divider == nullptr
      ? state->config.height - 108
      : lv_obj_get_y(state->detail_divider);
  return divider_y - kChatJumpButtonSize - kChatJumpButtonBottomGap;
}

/**
 * @brief 获取聊天回到底部按钮藏入输入区后的位置
 * @param state Radio 页面状态
 * @return 按钮隐藏时的纵坐标
 */
int ChatJumpButtonHiddenY(const RadioViewState* state) {
  if (state == nullptr) {
    return 0;
  }
  const int divider_y = state->detail_divider == nullptr
      ? state->config.height - 108
      : lv_obj_get_y(state->detail_divider);
  return divider_y + kChatJumpButtonHiddenOffset;
}

/**
 * @brief 在回到底部按钮下移动画结束后真正隐藏对象
 * @param animation LVGL 动画对象
 */
void ChatJumpButtonHideCompletedCallback(lv_anim_t* animation) {
  auto* state = static_cast<RadioViewState*>(
      lv_anim_get_user_data(animation));
  if (state == nullptr || state->detail_chat_jump_button == nullptr ||
      state->chat_jump_button_visible) {
    return;
  }
  lv_obj_add_flag(state->detail_chat_jump_button, LV_OBJ_FLAG_HIDDEN);
  lv_obj_set_y(state->detail_chat_jump_button,
      ChatJumpButtonHiddenY(state));
}

/**
 * @brief 按当前输入区位置同步回到底部按钮
 * @param state Radio 页面状态
 */
void PositionChatJumpButton(RadioViewState* state) {
  if (state == nullptr || state->detail_chat_jump_button == nullptr) {
    return;
  }
  lv_anim_delete(state->detail_chat_jump_button, SetChatJumpButtonY);
  if (state->chat_jump_button_visible) {
    lv_obj_remove_flag(
        state->detail_chat_jump_button, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(
        state->detail_chat_jump_button, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_y(state->detail_chat_jump_button,
        ChatJumpButtonVisibleY(state));
  } else {
    lv_obj_remove_flag(
        state->detail_chat_jump_button, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_y(state->detail_chat_jump_button,
        ChatJumpButtonHiddenY(state));
    lv_obj_add_flag(
        state->detail_chat_jump_button, LV_OBJ_FLAG_HIDDEN);
  }
}

/**
 * @brief 使用纵向滑动显示或隐藏回到底部按钮
 * @param state Radio 页面状态
 * @param visible 是否显示按钮
 */
void SetChatJumpButtonVisible(RadioViewState* state, bool visible) {
  if (state == nullptr) {
    return;
  }
  if (state->chat_jump_button_visible == visible) {
    return;
  }
  state->chat_jump_button_visible = visible;
  lv_obj_t* button = state->detail_chat_jump_button;
  if (button == nullptr) {
    return;
  }
  const bool was_hidden = lv_obj_has_flag(button, LV_OBJ_FLAG_HIDDEN);
  const int32_t start_y = was_hidden
      ? ChatJumpButtonHiddenY(state)
      : lv_obj_get_y(button);
  const int32_t end_y = visible
      ? ChatJumpButtonVisibleY(state)
      : ChatJumpButtonHiddenY(state);
  lv_anim_delete(button, SetChatJumpButtonY);
  if (visible) {
    lv_obj_remove_flag(button, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(button, LV_OBJ_FLAG_CLICKABLE);
  } else {
    lv_obj_remove_flag(button, LV_OBJ_FLAG_CLICKABLE);
  }
  if (start_y == end_y) {
    lv_obj_set_y(button, end_y);
    if (!visible) {
      lv_obj_add_flag(button, LV_OBJ_FLAG_HIDDEN);
    }
    return;
  }
  lv_obj_set_y(button, start_y);
  lv_anim_t animation;
  lv_anim_init(&animation);
  lv_anim_set_var(&animation, button);
  lv_anim_set_values(&animation, start_y, end_y);
  lv_anim_set_duration(&animation, kChatJumpAnimationMs);
  lv_anim_set_path_cb(&animation, visible
      ? lv_anim_path_ease_out
      : lv_anim_path_ease_in);
  lv_anim_set_exec_cb(&animation, SetChatJumpButtonY);
  if (!visible) {
    lv_anim_set_user_data(&animation, state);
    lv_anim_set_completed_cb(
        &animation, ChatJumpButtonHideCompletedCallback);
  }
  lv_anim_start(&animation);
}

/**
 * @brief 将聊天消息区域滚动到最后一条消息
 * @param body 聊天消息区域
 * @param content_height 聊天时间线完整内容高度
 * @param viewport_height 已知的新视口高度，负数表示读取当前高度
 */
void ScrollChatToBottom(
    lv_obj_t* body, int content_height, int viewport_height = -1) {
  if (body == nullptr) {
    return;
  }
  const int32_t target =
      ChatBottomScrollY(body, content_height, viewport_height);
  const int32_t current = lv_obj_get_scroll_y(body);
  const int32_t delta = current - target;
  if (delta != 0) {
    // content_height 已经给出精确边界，直接滚动可避免
    // lv_obj_scroll_to_y() 对整张 screen 执行同步布局更新。
    lv_obj_scroll_by(body, 0, delta, LV_ANIM_OFF);
  }
}

/**
 * @brief 判断聊天时间线当前是否真正显示在最上层
 * @param state Radio 页面状态
 * @return 没有设置子页面遮挡聊天页时返回 true
 */
bool IsChatTimelineVisible(const RadioViewState* state) {
  return state != nullptr && state->detail_page != nullptr &&
      state->profile_settings_page == nullptr &&
      state->profile_name_edit_page == nullptr &&
      state->add_page == nullptr && !state->detail_closing;
}

/**
 * @brief 立即或延后将聊天时间线滚动到底部
 * @param state Radio 页面状态
 * @param viewport_height 已知的新视口高度，负数表示读取当前高度
 */
void RequestChatScrollToBottom(
    RadioViewState* state, int viewport_height = -1) {
  if (state == nullptr || state->detail_chat_body == nullptr) {
    return;
  }
  if (!IsChatTimelineVisible(state)) {
    state->chat_scroll_pending = true;
    return;
  }
  state->chat_scroll_pending = false;
  state->chat_programmatic_scroll = true;
  ScrollChatToBottom(state->detail_chat_body,
      state->rendered_chat_y, viewport_height);
  state->chat_programmatic_scroll = false;
  state->chat_last_scroll_y =
      lv_obj_get_scroll_y(state->detail_chat_body);
  state->chat_follow_latest = true;
  SetChatJumpButtonVisible(state, false);
}

/**
 * @brief 在聊天页重新可见后应用等待中的自动滚动
 * @param state Radio 页面状态
 */
void ApplyPendingChatScroll(RadioViewState* state) {
  if (state == nullptr || !state->chat_scroll_pending ||
      !IsChatTimelineVisible(state)) {
    return;
  }
  RequestChatScrollToBottom(state);
}

/**
 * @brief 点击悬浮按钮后装入最新消息并回到底部
 * @param event LVGL 点击事件
 */
void ChatJumpButtonClickedEventCallback(lv_event_t* event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
    return;
  }
  auto* state = static_cast<RadioViewState*>(
      lv_event_get_user_data(event));
  if (state == nullptr) {
    return;
  }
  lv_event_stop_bubbling(event);
  lv_event_stop_processing(event);
  state->chat_follow_latest = true;
  state->chat_latest_page_pending = false;
  RenderChatMessages(state);
  RequestChatScrollToBottom(state);
}

/**
 * @brief 根据用户滚动方向维护 Telegram 风格回到底部按钮
 * @param event 聊天滚动区域事件
 */
void DetailChatScrollEventCallback(lv_event_t* event) {
  auto* state = static_cast<RadioViewState*>(
      lv_event_get_user_data(event));
  if (state == nullptr || state->detail_chat_body == nullptr) {
    return;
  }
  const lv_event_code_t code = lv_event_get_code(event);
  if (code != LV_EVENT_SCROLL_BEGIN && code != LV_EVENT_SCROLL &&
      code != LV_EVENT_SCROLL_END) {
    return;
  }
  const int32_t current =
      lv_obj_get_scroll_y(state->detail_chat_body);
  if (state->chat_programmatic_scroll) {
    state->chat_last_scroll_y = current;
    return;
  }
  if (code == LV_EVENT_SCROLL_BEGIN) {
    state->chat_last_scroll_y = current;
    return;
  }
  const int32_t target = ChatBottomScrollY(state->detail_chat_body,
      state->rendered_chat_y);
  const int32_t bottom_distance = std::max<int32_t>(0, target - current);
  const bool at_bottom = bottom_distance <= kChatBottomTolerance;
  if (code == LV_EVENT_SCROLL) {
    const bool moving_page_down = current > state->chat_last_scroll_y;
    const bool moving_page_up = current < state->chat_last_scroll_y;
    if (at_bottom && !state->chat_latest_page_pending) {
      state->chat_follow_latest = true;
      SetChatJumpButtonVisible(state, false);
    } else {
      state->chat_follow_latest = false;
      if (moving_page_up) {
        SetChatJumpButtonVisible(state, false);
      } else if (moving_page_down &&
          bottom_distance >= kChatJumpRevealDistance) {
        SetChatJumpButtonVisible(state, true);
      }
    }
    state->chat_last_scroll_y = current;
    return;
  }
  state->chat_last_scroll_y = current;
  if (!at_bottom) {
    return;
  }
  state->chat_follow_latest = true;
  if (state->chat_latest_page_pending) {
    state->chat_latest_page_pending = false;
    RenderChatMessages(state);
  } else {
    SetChatJumpButtonVisible(state, false);
  }
}

/**
 * @brief 查找旧布局后缀与新消息前缀的最大重合数量
 * @param state Radio 页面状态
 * @param messages 最新消息数组
 * @param message_count 最新消息数量
 * @return 可以直接复用的布局数量
 */
size_t FindChatLayoutOverlap(const RadioViewState* state,
    const RadioChatMessage* const* messages, size_t message_count) {
  if (state == nullptr || messages == nullptr) {
    return 0;
  }
  const size_t maximum =
      std::min(state->rendered_chat_count, message_count);
  for (size_t overlap = maximum; overlap > 0; --overlap) {
    const size_t old_first = state->rendered_chat_count - overlap;
    bool matches = true;
    for (size_t index = 0; index < overlap; ++index) {
      matches = messages[index] != nullptr &&
          state->rendered_chat_messages[old_first + index].sequence ==
              messages[index]->sequence;
      if (!matches) {
        break;
      }
    }
    if (matches) {
      return overlap;
    }
  }
  return 0;
}

/**
 * @brief 重新排列保留的轻量聊天布局
 * @param state Radio 页面状态
 */
void RelayoutChatMessages(RadioViewState* state) {
  if (state == nullptr) {
    return;
  }
  int chat_y = kChatTimelineInset;
  for (size_t index = 0; index < state->rendered_chat_count; ++index) {
    state->rendered_chat_messages[index].y = chat_y;
    chat_y += state->rendered_chat_messages[index].height;
  }
  state->rendered_chat_y = chat_y;
}

/**
 * @brief 渲染当前 Radio 配置最近的聊天记录
 * @param state Radio 页面状态
 * @return 时间线更新成功时返回 true
 */
bool RenderChatMessages(RadioViewState* state) {
  if (state == nullptr || state->detail_chat_body == nullptr ||
      state->detail_chat_timeline == nullptr ||
      state->detail_index >= state->module_count) {
    return false;
  }
  const uint32_t started_ms = lv_tick_get();
  const app::RadioProfile& profile =
      state->preferences.profiles[state->detail_index];
  const RadioChatMessage* messages[app::kRadioChatPageCapacity] = {};
  const size_t message_count = app::GetRadioChatRepository().GetRecent(
      profile.id, messages, app::kRadioChatPageCapacity);
  const uint32_t repository_done_ms = lv_tick_get();
  const bool same_profile =
      state->rendered_chat_profile_id == profile.id;
  const bool follow_latest =
      !same_profile || state->chat_follow_latest;
  const int32_t previous_scroll_y =
      lv_obj_get_scroll_y(state->detail_chat_body);
  size_t overlap = same_profile
      ? FindChatLayoutOverlap(state, messages, message_count)
      : 0;
  const bool full_historical_page = same_profile && !follow_latest &&
      state->rendered_chat_count == app::kRadioChatPageCapacity &&
      message_count == app::kRadioChatPageCapacity;
  if (full_historical_page &&
      (state->chat_latest_page_pending ||
          overlap < state->rendered_chat_count)) {
    // 历史窗口已满时保持当前页，避免每来一条消息就移除顶部内容。
    state->chat_latest_page_pending = true;
    lv_obj_invalidate(state->detail_chat_timeline);
    return true;
  }
  const bool rebuild = !same_profile ||
      (state->rendered_chat_count > 0 && message_count > 0 &&
          overlap == 0);
  bool timeline_changed = rebuild ||
      state->rendered_chat_count != message_count;
  int removed_height = 0;
  if (rebuild || message_count == 0) {
    ResetRenderedChatState(state);
    state->chat_follow_latest = message_count == 0 || follow_latest;
    PositionChatJumpButton(state);
    overlap = 0;
  } else if (state->rendered_chat_count > overlap) {
    const size_t removed_count = state->rendered_chat_count - overlap;
    for (size_t index = 0; index < removed_count; ++index) {
      removed_height += state->rendered_chat_messages[index].height;
    }
    for (size_t index = 0; index < overlap; ++index) {
      state->rendered_chat_messages[index] =
          state->rendered_chat_messages[removed_count + index];
    }
    for (size_t index = overlap;
         index < state->rendered_chat_count; ++index) {
      state->rendered_chat_messages[index] =
          RenderedRadioChatMessage();
    }
    state->rendered_chat_count = overlap;
    RelayoutChatMessages(state);
    timeline_changed = true;
  }
  state->rendered_chat_profile_id = profile.id;
  for (size_t index = 0; index < overlap; ++index) {
    if (messages[index] == nullptr) {
      return false;
    }
    state->rendered_chat_messages[index].message = messages[index];
  }
  const uint32_t diff_done_ms = lv_tick_get();
  for (size_t index = overlap; index < message_count; ++index) {
    if (messages[index] == nullptr || !LayoutChatMessage(state,
            *messages[index], state->rendered_chat_y,
            &state->rendered_chat_messages[index])) {
      return false;
    }
    state->rendered_chat_y +=
        state->rendered_chat_messages[index].height;
    ++state->rendered_chat_count;
    timeline_changed = true;
  }
  const uint32_t layout_done_ms = lv_tick_get();
  const int timeline_height = std::max(state->rendered_chat_y, 1);
  if (lv_obj_get_width(state->detail_chat_timeline) !=
          state->config.width ||
      lv_obj_get_height(state->detail_chat_timeline) != timeline_height) {
    lv_obj_set_size(state->detail_chat_timeline,
        state->config.width, timeline_height);
  }
  const uint32_t size_done_ms = lv_tick_get();
  lv_obj_invalidate(state->detail_chat_timeline);
  const uint32_t invalidate_done_ms = lv_tick_get();
  if (timeline_changed) {
    if (message_count == 0 || follow_latest) {
      state->chat_latest_page_pending = false;
      RequestChatScrollToBottom(state);
    } else {
      if (removed_height > 0) {
        const int32_t target =
            std::max<int32_t>(0, previous_scroll_y - removed_height);
        const int32_t current =
            lv_obj_get_scroll_y(state->detail_chat_body);
        state->chat_programmatic_scroll = true;
        lv_obj_scroll_by(state->detail_chat_body, 0,
            current - target, LV_ANIM_OFF);
        state->chat_programmatic_scroll = false;
        state->chat_last_scroll_y =
            lv_obj_get_scroll_y(state->detail_chat_body);
      }
      state->chat_follow_latest = false;
    }
  }
  const uint32_t finished_ms = lv_tick_get();
  if (finished_ms - started_ms >= kSlowRadioUiThresholdMs) {
    LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
        "Radio chat render slow: repository=%lu ms, diff=%lu ms, "
        "layout=%lu ms, size=%lu ms, invalidate=%lu ms, "
        "scroll=%lu ms\n",
        static_cast<unsigned long>(repository_done_ms - started_ms),
        static_cast<unsigned long>(diff_done_ms - repository_done_ms),
        static_cast<unsigned long>(layout_done_ms - diff_done_ms),
        static_cast<unsigned long>(size_done_ms - layout_done_ms),
        static_cast<unsigned long>(invalidate_done_ms - size_done_ms),
        static_cast<unsigned long>(finished_ms - invalidate_done_ms));
  }
  return true;
}

/**
 * @brief 追加系统提示并更新射频主页面的最新消息
 * @param state 射频页面状态
 * @param profile_index 射频配置索引
 * @param text 系统提示文本
 * @return 追加成功返回 true，否则返回 false
 */
bool AppendSystemMessage(RadioViewState* state, size_t profile_index,
    const char* text) {
  if (state == nullptr || text == nullptr ||
      profile_index >= state->preferences.profile_count) {
    return false;
  }
  RadioChatMessage message;
  message.profile_id = state->preferences.profiles[profile_index].id;
  message.type = RadioChatMessageType::kSystem;
  FormatCurrentTime(state, message.time, sizeof(message.time));
  CopyBoundedString(message.text, sizeof(message.text), text);
  const bool appended = app::GetRadioChatRepository().Append(message) != 0;
  if (appended) {
    SyncModuleItems(state);
  }
  return appended;
}

/**
 * @brief 将指定 Radio 配置中等待发送结果的消息标记为失败
 * @param state Radio 页面状态
 * @param profile_id Radio 配置 ID
 */
void FailPendingMessages(RadioViewState* state, uint32_t profile_id) {
  if (state == nullptr || profile_id == 0) {
    return;
  }
  app::GetRadioChatRepository().FailPending(profile_id);
}

/**
 * @brief 更新聊天页和配置页显示的射频状态
 * @param state Radio 页面状态
 */
void UpdateDetailStatus(RadioViewState* state) {
  if (state == nullptr) {
    return;
  }
  if (state->detail_status_label != nullptr &&
      state->detail_index < state->module_count) {
    const char* status = ProfileStatusText(state, state->detail_index);
    SetLabelTextIfChanged(state->detail_status_label, status);
    lv_obj_set_style_text_color(state->detail_status_label,
        lv_color_hex(ProfileStatusColor(status)), LV_PART_MAIN);
  }
  if (state->profile_settings_header_status_label != nullptr &&
      state->profile_settings_index < state->module_count) {
    const char* status =
        ProfileStatusText(state, state->profile_settings_index);
    const lv_color_t status_color =
        lv_color_hex(ProfileStatusColor(status));
    SetLabelTextIfChanged(
        state->profile_settings_header_status_label, status);
    lv_obj_set_style_text_color(
        state->profile_settings_header_status_label,
        status_color, LV_PART_MAIN);
  }
  UpdateChatComposerState(state);
}

/**
 * @brief 将接收数据格式化为可显示文本或连续十六进制文本
 * @param event 射频接收事件
 * @param output 文本输出缓冲区
 * @param output_size 输出缓冲区容量
 */
void FormatPacketText(const hal::RadioEvent& event, char* output,
    size_t output_size) {
  if (output == nullptr || output_size == 0) {
    return;
  }
  output[0] = '\0';
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
    const int result = std::snprintf(output + written, output_size - written,
        "%02X", static_cast<unsigned>(event.payload[index]));
    if (result <= 0 || static_cast<size_t>(result) >= output_size - written) {
      break;
    }
    written += static_cast<size_t>(result);
  }
}

/**
 * @brief 从内部存储补载全部 Radio 配置的最近聊天记录
 * @param state Radio 页面状态
 * @return 内部存储可用且补载完成时返回 true
 */
bool LoadCurrentChatProfiles(RadioViewState* state) {
  if (state == nullptr) {
    return false;
  }
  uint32_t profile_ids[kRadioModuleCapacity] = {};
  for (size_t index = 0; index < state->preferences.profile_count; ++index) {
    profile_ids[index] = state->preferences.profiles[index].id;
  }
  if (!app::GetRadioChatRepository().LoadProfiles(
          profile_ids, state->preferences.profile_count)) {
    return false;
  }
  SyncModuleItems(state);
  if (state->detail_page != nullptr) {
    RenderChatMessages(state);
  }
  return true;
}

/**
 * @brief 获取射频失败原因的日志文本
 * @param reason 射频失败原因
 * @return 生命周期覆盖当前进程的静态文本
 */
const char* RadioFailureReasonText(hal::RadioFailureReason reason) {
  switch (reason) {
    case hal::RadioFailureReason::kNone:
      return "none";
    case hal::RadioFailureReason::kHardwareUnavailable:
      return "hardware unavailable";
    case hal::RadioFailureReason::kIrqReadFailed:
      return "IRQ access failed";
    case hal::RadioFailureReason::kIrqClearFailed:
      return "IRQ clear failed";
    case hal::RadioFailureReason::kHardwareTimeout:
      return "hardware timeout";
    case hal::RadioFailureReason::kSoftwareTimeout:
      return "software timeout";
    case hal::RadioFailureReason::kReceiveRestartFailed:
      return "receive restart failed";
  }
  return "unknown";
}

/**
 * @brief 在独立任务中执行可能阻塞 SPI 总线的射频命令
 * @param context 共享射频命令任务上下文
 */
void RadioCommandTaskEntry(void* context) {
  auto* shared_job =
      static_cast<std::shared_ptr<RadioCommandJob>*>(context);
  if (shared_job == nullptr) {
    vTaskDelete(nullptr);
    return;
  }
  std::shared_ptr<RadioCommandJob> job = *shared_job;
  delete shared_job;
  if (job->provider != nullptr) {
    switch (job->type) {
      case RadioCommandType::kActivate: {
        job->success = job->provider->ActivateRadio(job->config);
        RadioActivationState activation_state =
            RadioActivationState::kNone;
        if (!job->success) {
          hal::RadioCapabilities capabilities;
          activation_state = RadioActivationState::kInitializationFailed;
          if (job->provider->ReadRadioCapabilities(&capabilities) &&
              !IsRadioConfigSupported(capabilities, job->config)) {
            activation_state = RadioActivationState::kHardwareUnavailable;
          }
        }
        // 页面退出或配置删除后，旧任务的结果不能污染已不存在或已修改
        // 的配置。profile ID 单调递增，回绕前再用完整配置进行二次确认。
        app::RadioPreferences preferences;
        if (app::GetRadioPreferences(&preferences)) {
          for (size_t index = 0; index < preferences.profile_count; ++index) {
            if (preferences.profiles[index].id != job->config.client_token) {
              continue;
            }
            const hal::RadioConfig current_config =
                ToRadioConfig(preferences.profiles[index]);
            if (RadioConfigsEqual(current_config, job->config)) {
              SetProfileActivationState(
                  job->config.client_token, activation_state);
            }
            break;
          }
        }
        break;
      }
      case RadioCommandType::kDeactivate:
        job->success = job->provider->DeactivateRadio(
            job->config.client_token);
        break;
      case RadioCommandType::kSend:
        job->success = job->provider->SendRadio(job->config.client_token,
            job->payload, job->payload_size, job->request_token);
        break;
    }
  }
  job->completed.store(true, std::memory_order_release);
  job.reset();
  vTaskDelete(nullptr);
}

/**
 * @brief 启动一个后台射频命令任务
 * @param state Radio 页面状态
 * @param job 射频命令任务
 * @return FreeRTOS 任务成功创建时返回 true
 */
bool StartRadioCommand(RadioViewState* state,
    const std::shared_ptr<RadioCommandJob>& job) {
  if (state == nullptr || job == nullptr ||
      state->radio_command_job != nullptr) {
    return false;
  }
  state->radio_command_job = job;
  auto* task_context =
      new (std::nothrow) std::shared_ptr<RadioCommandJob>(job);
  if (task_context != nullptr &&
      xTaskCreate(RadioCommandTaskEntry, "radio_cmd",
          kRadioCommandTaskStackBytes, task_context,
          kRadioCommandTaskPriority, nullptr) == pdPASS) {
    return true;
  }
  delete task_context;
  job->success = false;
  if (job->type == RadioCommandType::kActivate) {
    SetProfileActivationState(job->config.client_token,
        RadioActivationState::kInitializationFailed);
  }
  job->completed.store(true, std::memory_order_release);
  LogMessage(LogLevel::kError, __FILE__, __LINE__,
      "Radio command task could not be created\n");
  return false;
}

/**
 * @brief 在射频空闲后启动队首配置或停用请求
 * @param state Radio 页面状态
 * @return 后台命令已进入执行状态时返回 true
 */
bool StartPendingRadioControlCommand(RadioViewState* state) {
  if (state == nullptr || state->pending_control_count == 0 ||
      state->radio_command_job != nullptr || state->transmit_in_flight ||
      state->config.radio == nullptr) {
    return false;
  }
  auto job = std::make_shared<RadioCommandJob>();
  job->provider = state->config.radio;
  job->type = state->pending_control_types[0];
  job->config = state->pending_control_configs[0];
  for (size_t index = 1; index < state->pending_control_count; ++index) {
    state->pending_control_types[index - 1] =
        state->pending_control_types[index];
    state->pending_control_configs[index - 1] =
        state->pending_control_configs[index];
  }
  --state->pending_control_count;
  StartRadioCommand(state, job);
  return true;
}

/**
 * @brief 排队并异步执行 Radio 激活或停用请求
 * @param state Radio 页面状态
 * @param type 激活或停用命令类型
 * @param config 激活配置，停用命令可传默认配置
 */
void QueueRadioControlCommand(RadioViewState* state,
    RadioCommandType type, const hal::RadioConfig& config) {
  if (state == nullptr || state->config.radio == nullptr) {
    return;
  }
  if (state->pending_control_count >= kRadioModuleCapacity) {
    LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
        "Radio control queue is full\n");
    return;
  }
  if (type == RadioCommandType::kActivate) {
    // 页面重建期间也能识别尚未完成的初始化，避免并发访问同一芯片。
    SetProfileActivationState(
        config.client_token, RadioActivationState::kPending);
  }
  state->pending_control_types[state->pending_control_count] = type;
  state->pending_control_configs[state->pending_control_count] = config;
  ++state->pending_control_count;
  StartPendingRadioControlCommand(state);
  UpdateDetailStatus(state);
}

/**
 * @brief 异步启动一条等待消息，避免 SPI 操作阻塞 LVGL 回调
 * @param state Radio 页面状态
 * @param message 待发送聊天消息
 * @return 消息已交给后台任务时返回 true
 */
bool StartRadioSendCommand(
    RadioViewState* state, const RadioChatMessage& message) {
  const size_t length = std::strlen(message.text);
  if (state == nullptr || state->config.radio == nullptr || length == 0 ||
      length > hal::kRadioPayloadCapacity ||
      state->radio_command_job != nullptr ||
      state->pending_control_count != 0 || state->transmit_in_flight) {
    return false;
  }
  auto job = std::make_shared<RadioCommandJob>();
  job->provider = state->config.radio;
  job->type = RadioCommandType::kSend;
  job->config.client_token = message.profile_id;
  job->payload_size = length;
  job->request_token = message.sequence;
  std::memcpy(job->payload, message.text, length);
  state->transmit_in_flight = true;
  StartRadioCommand(state, job);
  return true;
}

/**
 * @brief 收割后台射频命令结果并更新最小范围的界面
 * @param state Radio 页面状态
 * @return 当前没有后台射频命令时返回 true
 */
bool FinishRadioCommand(RadioViewState* state) {
  if (state == nullptr) {
    return false;
  }
  if (state->radio_command_job == nullptr) {
    return true;
  }
  if (!state->radio_command_job->completed.load(
          std::memory_order_acquire)) {
    return false;
  }
  const std::shared_ptr<RadioCommandJob> job = state->radio_command_job;
  state->radio_command_job.reset();
  if (job->type == RadioCommandType::kSend && !job->success) {
    state->transmit_in_flight = false;
    app::GetRadioChatRepository().UpdateDelivery(
        job->request_token, RadioChatDeliveryState::kFailed);
    LogMessage(LogLevel::kError, __FILE__, __LINE__,
        "Radio queued message start failed: message=%lu, size=%u bytes\n",
        static_cast<unsigned long>(
            static_cast<uint32_t>(job->request_token)),
        static_cast<unsigned>(job->payload_size));
    SyncModuleItems(state);
    if (state->detail_page != nullptr) {
      RenderChatMessages(state);
    }
    MarkModuleListDirty(state);
  } else if (job->type != RadioCommandType::kSend) {
    const size_t profile_index = FindProfileIndex(
        state, job->config.client_token);
    if (profile_index < state->module_count) {
      state->radio_status_available[profile_index] = false;
    }
    if (job->type == RadioCommandType::kDeactivate &&
        profile_index < state->module_count) {
      state->radio_statuses[profile_index] = hal::RadioStatus();
      state->radio_status_available[profile_index] = job->success;
    }
    // 激活失败后也必须立即退出 Activating 状态，让聊天页重新显示
    // 可点击的 Retry initialization；后续是否再检测只由用户决定。
    UpdateDetailStatus(state);
    RefreshProfileSettingsPage(state);
    MarkModuleListDirty(state);
  }
  if (!state->transmit_in_flight) {
    StartPendingRadioControlCommand(state);
  }
  return state->radio_command_job == nullptr;
}

/**
 * @brief 在射频空闲时启动当前配置最早的等待消息
 * @param state Radio 页面状态
 * @return 成功启动发送时返回 true
 */
bool TryStartNextPendingMessage(RadioViewState* state) {
  if (state == nullptr || state->config.radio == nullptr ||
      state->radio_command_job != nullptr ||
      state->pending_control_count != 0 || state->transmit_in_flight) {
    return false;
  }
  RadioChatMessage message;
  size_t profile_index = state->module_count;
  for (size_t index = 0; index < state->module_count; ++index) {
    const app::RadioProfile& profile = state->preferences.profiles[index];
    if (profile.active &&
        app::GetRadioChatRepository().GetOldestPending(
            profile.id, &message)) {
      profile_index = index;
      break;
    }
  }
  if (profile_index >= state->module_count) {
    return false;
  }
  const size_t length = std::strlen(message.text);
  const size_t maximum_payload_size = profile_index < state->module_count
      ? MaximumPayloadSizeForProfile(
          state, state->preferences.profiles[profile_index])
      : 0;
  if (length == 0 || length > maximum_payload_size) {
    app::GetRadioChatRepository().UpdateDelivery(
        message.sequence, RadioChatDeliveryState::kFailed);
    LogMessage(LogLevel::kError, __FILE__, __LINE__,
        "Radio queued message rejected: profile=%lu, message=%lu, "
        "size=%u bytes\n",
        static_cast<unsigned long>(message.profile_id),
        static_cast<unsigned long>(static_cast<uint32_t>(message.sequence)),
        static_cast<unsigned>(length));
    SyncModuleItems(state);
    if (state->detail_page != nullptr) {
      RenderChatMessages(state);
    }
    MarkModuleListDirty(state);
    return false;
  }
  return StartRadioSendCommand(state, message);
}

/**
 * @brief 在射频空闲且周期到达时向现有聊天发送队列加入测试字符
 * @param state Radio 页面状态
 * @return 成功加入一条自动发送消息时返回 true
 */
bool TryQueueAutomaticMessage(RadioViewState* state) {
  if (state == nullptr || state->config.radio == nullptr ||
      state->radio_command_job != nullptr ||
      state->pending_control_count != 0 || state->transmit_in_flight) {
    return false;
  }
  const uint32_t now = lv_tick_get();
  size_t index = state->module_count;
  for (size_t candidate = 0; candidate < state->module_count; ++candidate) {
    const app::RadioProfile& profile =
        state->preferences.profiles[candidate];
    if (!profile.active || !profile.auto_send_enabled ||
        profile.auto_send_text[0] == '\0' ||
        !state->radio_status_available[candidate] ||
        state->radio_statuses[candidate].state !=
            hal::RadioLinkState::kActive ||
        state->radio_statuses[candidate].transmitting) {
      continue;
    }
    if (state->auto_send_last_ticks[candidate] == 0) {
      state->auto_send_last_ticks[candidate] = now;
      continue;
    }
    if (now - state->auto_send_last_ticks[candidate] >=
        profile.auto_send_interval_ms) {
      index = candidate;
      break;
    }
  }
  if (index >= state->module_count) {
    return false;
  }
  const app::RadioProfile& profile = state->preferences.profiles[index];
  state->auto_send_last_ticks[index] = now;

  RadioChatMessage message;
  message.profile_id = profile.id;
  message.delivery = RadioChatDeliveryState::kSending;
  FormatCurrentTime(state, message.time, sizeof(message.time));
  CopyBoundedString(
      message.text, sizeof(message.text), profile.auto_send_text);
  if (app::GetRadioChatRepository().Append(message) == 0) {
    LogMessage(LogLevel::kError, __FILE__, __LINE__,
        "Radio automatic message rejected: chat cache is unavailable, "
        "profile=%lu\n", static_cast<unsigned long>(profile.id));
    return false;
  }
  SyncModuleItems(state);
  if (state->detail_page != nullptr && state->detail_index == index) {
    state->chat_follow_latest = true;
    state->chat_latest_page_pending = false;
    RenderChatMessages(state);
  }
  MarkModuleListDirty(state);
  return TryStartNextPendingMessage(state);
}

/**
 * @brief 判断两组射频能力是否完全一致
 * @param lhs 第一组射频能力
 * @param rhs 第二组射频能力
 * @return 能力内容一致时返回 true
 */
bool RadioCapabilitiesEqual(
    const hal::RadioCapabilities& lhs, const hal::RadioCapabilities& rhs) {
  if (lhs.count != rhs.count ||
      lhs.supports_external_antenna != rhs.supports_external_antenna) {
    return false;
  }
  for (size_t index = 0; index < lhs.count; ++index) {
    const hal::RadioCapability& left = lhs.entries[index];
    const hal::RadioCapability& right = rhs.entries[index];
    if (left.chip != right.chip || left.protocol != right.protocol ||
        left.maximum_payload_size != right.maximum_payload_size ||
        left.frequency_band_count != right.frequency_band_count) {
      return false;
    }
    for (size_t band_index = 0;
         band_index < left.frequency_band_count; ++band_index) {
      if (left.frequency_bands[band_index].minimum_hz !=
              right.frequency_bands[band_index].minimum_hz ||
          left.frequency_bands[band_index].maximum_hz !=
              right.frequency_bands[band_index].maximum_hz) {
        return false;
      }
    }
  }
  return true;
}

/**
 * @brief 周期刷新可热插拔的射频能力并恢复可用配置
 * @param state Radio 页面状态
 */
void RefreshRadioCapabilities(RadioViewState* state) {
  if (state == nullptr || state->config.radio == nullptr) {
    return;
  }
  const uint32_t now = lv_tick_get();
  if (now - state->last_capabilities_refresh_tick <
      kRadioCapabilitiesRefreshPeriodMs) {
    return;
  }
  state->last_capabilities_refresh_tick = now;
  hal::RadioCapabilities capabilities;
  if (!state->config.radio->ReadRadioCapabilities(&capabilities)) {
    return;
  }
  capabilities.count = std::min(
      capabilities.count, hal::kRadioCapabilityCapacity);
  for (size_t index = 0; index < capabilities.count; ++index) {
    capabilities.entries[index].frequency_band_count = std::min(
        capabilities.entries[index].frequency_band_count,
        hal::kRadioFrequencyBandCapacity);
  }
  if (RadioCapabilitiesEqual(state->capabilities, capabilities)) {
    return;
  }
  bool was_supported[kRadioModuleCapacity] = {};
  for (size_t index = 0; index < state->module_count; ++index) {
    was_supported[index] =
        IsProfileSupported(state, state->preferences.profiles[index]);
  }
  state->capabilities = capabilities;
  if (state->add_page != nullptr) {
    CloseAddModulePage(state);
  }
  for (size_t index = 0; index < state->module_count; ++index) {
    const app::RadioProfile& profile = state->preferences.profiles[index];
    if (!profile.active) {
      continue;
    }
    const bool supported = IsProfileSupported(state, profile);
    state->radio_status_available[index] = false;
    if (!supported) {
      const RadioActivationState activation_state =
          GetRadioActivationRegistry().GetState(profile.id);
      if (activation_state != RadioActivationState::kPending) {
        SetProfileActivationState(
            profile.id, RadioActivationState::kHardwareUnavailable);
      }
      continue;
    }
    if (was_supported[index]) {
      continue;
    }
    if (GetRadioActivationRegistry().GetState(profile.id) ==
        RadioActivationState::kHardwareUnavailable) {
      SetProfileActivationState(profile.id, RadioActivationState::kNone);
    }
    if (!IsProfileActivationBlocked(profile.id) &&
        !IsProfileActivationPending(state, profile.id)) {
      QueueRadioControlCommand(state, RadioCommandType::kActivate,
          ToRadioConfig(profile));
    }
  }
  UpdateDetailStatus(state);
  RefreshProfileSettingsPage(state);
  MarkModuleListDirty(state);
}

/**
 * @brief 轮询射频事件并仅在发送空闲时处理聊天存储
 * @param timer Radio 页面定时器
 */
void RadioTimerCallback(lv_timer_t* timer) {
  auto* state = static_cast<RadioViewState*>(lv_timer_get_user_data(timer));
  if (state == nullptr) {
    return;
  }
  RefreshRadioCapabilities(state);
  if (!FinishRadioCommand(state) || state->radio_command_job != nullptr) {
    return;
  }
  if (state->config.radio == nullptr) {
    return;
  }
  bool any_active_profile = false;
  bool any_status_available = false;
  bool any_transmitting = false;
  bool status_changed = false;
  for (size_t index = 0; index < state->module_count; ++index) {
    const app::RadioProfile& profile = state->preferences.profiles[index];
    if (!profile.active) {
      continue;
    }
    any_active_profile = true;
    hal::RadioStatus status;
    const bool available = state->config.radio->ReadRadioStatus(
        profile.id, &status);
    status_changed |= state->radio_status_available[index] != available ||
        (available &&
            (state->radio_statuses[index].state != status.state ||
                state->radio_statuses[index].active_client_token !=
                    status.active_client_token ||
                state->radio_statuses[index].hardware_ready !=
                    status.hardware_ready ||
                state->radio_statuses[index].transmitting !=
                    status.transmitting));
    state->radio_status_available[index] = available;
    if (available) {
      state->radio_statuses[index] = status;
      any_status_available = true;
      any_transmitting |= status.transmitting;
      if (status.state == hal::RadioLinkState::kChipError) {
        RecordProfileChipError(state, profile.id);
      }
    }
  }
  if (!any_active_profile) {
    return;
  }
  if (status_changed) {
    UpdateDetailStatus(state);
    RefreshProfileSettingsPage(state);
    MarkModuleListDirty(state);
  }
  hal::RadioEvent event;
  const bool poll_succeeded = state->config.radio->PollRadioEvent(&event);
  if (event.type == hal::RadioEventType::kNone) {
    if (!poll_succeeded || !any_status_available || any_transmitting) {
      return;
    }
    if (!TryStartNextPendingMessage(state)) {
      TryQueueAutomaticMessage(state);
    }
    return;
  }
  const uint32_t profile_id = event.client_token;
  if (event.type == hal::RadioEventType::kTransmitComplete ||
      event.type == hal::RadioEventType::kTransmitFailed ||
      event.type == hal::RadioEventType::kChipError) {
    state->transmit_in_flight = false;
    const RadioChatDeliveryState delivery =
        event.type == hal::RadioEventType::kTransmitComplete
            ? RadioChatDeliveryState::kSent
            : RadioChatDeliveryState::kFailed;
    bool updated = false;
    if (event.request_token != 0) {
      updated = app::GetRadioChatRepository().UpdateDelivery(
          event.request_token, delivery);
    }
    if (event.type == hal::RadioEventType::kChipError) {
      RecordProfileChipError(state, profile_id);
      app::GetRadioChatRepository().FailPending(profile_id);
    }
    if (event.type == hal::RadioEventType::kTransmitComplete) {
      if (!updated) {
        LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
            "Radio message was sent, but it is missing from chat history: "
            "profile=%lu, message=%lu\n",
            static_cast<unsigned long>(profile_id),
            static_cast<unsigned long>(
                static_cast<uint32_t>(event.request_token)));
      }
    } else if (event.request_token != 0) {
      LogMessage(LogLevel::kError, __FILE__, __LINE__,
          "Radio message %lu failed on profile %lu: %s\n",
          static_cast<unsigned long>(
              static_cast<uint32_t>(event.request_token)),
          static_cast<unsigned long>(profile_id),
          RadioFailureReasonText(event.failure_reason));
    } else {
      LogMessage(LogLevel::kError, __FILE__, __LINE__,
          "Radio radio error on profile %lu: %s\n",
          static_cast<unsigned long>(profile_id),
          RadioFailureReasonText(event.failure_reason));
    }
  }
  const size_t profile_index = FindProfileIndex(state, profile_id);
  if (profile_index >= state->module_count) {
    LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
        "Radio event ignored: unknown profile=%lu, type=%u\n",
        static_cast<unsigned long>(profile_id),
        static_cast<unsigned>(event.type));
    StartPendingRadioControlCommand(state);
    return;
  }
  if (event.type == hal::RadioEventType::kPacketReceived) {
    RadioChatMessage message;
    message.profile_id = profile_id;
    message.delivery = RadioChatDeliveryState::kReceived;
    message.rssi_dbm = event.rssi_dbm;
    message.snr_db = event.snr_db;
    message.rssi_valid = event.rssi_valid;
    message.snr_valid = event.snr_valid;
    FormatCurrentTime(state, message.time, sizeof(message.time));
    FormatPacketText(event, message.text, sizeof(message.text));
    if (app::GetRadioChatRepository().Append(message) != 0) {
      if (state->detail_page == nullptr ||
          state->detail_index != profile_index) {
        app::GetRadioChatRepository().IncrementUnread(profile_id);
      }
    }
  }
  SyncModuleItems(state);
  RefreshProfileSettingsPage(state);
  UpdateDetailStatus(state);
  if (state->detail_page != nullptr && state->detail_index == profile_index) {
    RenderChatMessages(state);
  }
  MarkModuleListDirty(state);
  StartPendingRadioControlCommand(state);
  if (!TryStartNextPendingMessage(state)) {
    TryQueueAutomaticMessage(state);
  }
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
  lv_obj_t* icon_label =
      CreateLabel(parent, icon::kNearMe, kOnPrimaryColor, OutlineIconFont44());
  if (icon_label == nullptr) {
    return false;
  }
  lv_obj_align(icon_label, LV_ALIGN_CENTER, -1, 0);
  return true;
}

/**
 * @brief 校验聊天输入并启动可追踪的异步射频发送
 * @param state 射频页面状态
 */
void SendDetailMessage(RadioViewState* state) {
  if (state == nullptr || state->detail_input == nullptr ||
      state->detail_index >= state->module_count) {
    return;
  }
  if (GetRadioComposerMode(state, state->detail_index) !=
      RadioComposerMode::kActive) {
    return;
  }
  const uint32_t started_ms = lv_tick_get();
  const char* text = lv_textarea_get_text(state->detail_input);
  const size_t length = text == nullptr ? 0 : std::strlen(text);
  const app::RadioProfile& profile =
      state->preferences.profiles[state->detail_index];
  if (length == 0) {
    return;
  }
  if (length > hal::kRadioPayloadCapacity) {
    LogMessage(LogLevel::kError, __FILE__, __LINE__,
        "Radio message rejected: payload too large, bytes=%u, maximum=%u\n",
        static_cast<unsigned>(length),
        static_cast<unsigned>(hal::kRadioPayloadCapacity));
    return;
  }
  if (!profile.active) {
    LogMessage(LogLevel::kError, __FILE__, __LINE__,
        "Radio message rejected: profile is inactive, profile=%lu\n",
        static_cast<unsigned long>(profile.id));
    return;
  }
  if (state->config.radio == nullptr) {
    LogMessage(LogLevel::kError, __FILE__, __LINE__,
        "Radio message rejected: Radio provider is unavailable, profile=%lu\n",
        static_cast<unsigned long>(profile.id));
    return;
  }
  RadioChatMessage message;
  message.profile_id = profile.id;
  message.delivery = RadioChatDeliveryState::kSending;
  FormatCurrentTime(state, message.time, sizeof(message.time));
  CopyBoundedString(message.text, sizeof(message.text), text);
  const uint64_t sequence = app::GetRadioChatRepository().Append(message);
  if (sequence == 0) {
    LogMessage(LogLevel::kError, __FILE__, __LINE__,
        "Radio message rejected: chat cache is unavailable, profile=%lu\n",
        static_cast<unsigned long>(profile.id));
    return;
  }
  const uint32_t message_done_ms = lv_tick_get();
  lv_textarea_set_text(state->detail_input, "");
  SyncModuleItems(state);
  const uint32_t summary_done_ms = lv_tick_get();
  // 发送自己的消息时切换回最新会话，确保新气泡立即可见。
  state->chat_follow_latest = true;
  state->chat_latest_page_pending = false;
  RenderChatMessages(state);
  MarkModuleListDirty(state);
  const uint32_t finished_ms = lv_tick_get();
  if (finished_ms - started_ms >= kSlowRadioUiThresholdMs) {
    LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
        "Radio send UI slow: message=%lu ms, summary=%lu ms, "
        "render=%lu ms\n",
        static_cast<unsigned long>(message_done_ms - started_ms),
        static_cast<unsigned long>(summary_done_ms - message_done_ms),
        static_cast<unsigned long>(finished_ms - summary_done_ms));
  }
}

/**
 * @brief 处理聊天发送按钮点击事件
 * @param event LVGL 点击事件
 */
void DetailSendClickedEventCallback(lv_event_t* event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
    return;
  }
  lv_event_stop_bubbling(event);
  lv_event_stop_processing(event);
  auto* state = static_cast<RadioViewState*>(lv_event_get_user_data(event));
  const bool keep_input_active = state != nullptr &&
      state->detail_keyboard != nullptr && state->detail_input != nullptr &&
      lv_keyboard_get_textarea(state->detail_keyboard) ==
          state->detail_input;
  SendDetailMessage(state);
  if (keep_input_active) {
    lv_keyboard_set_textarea(state->detail_keyboard, state->detail_input);
    if (ShouldShowSharedKeyboard()) {
      lv_obj_remove_flag(state->detail_keyboard, LV_OBJ_FLAG_HIDDEN);
    }
  }
}

/**
 * @brief 调整消息输入区位置并控制共享键盘显示状态
 * @param state 射频页面状态
 * @param visible 是否显示键盘
 */
void SetDetailKeyboardVisible(RadioViewState* state, bool visible) {
  if (state == nullptr || state->detail_input == nullptr ||
      state->detail_composer_background == nullptr ||
      state->detail_divider == nullptr ||
      state->detail_send_button == nullptr ||
      state->detail_chat_body == nullptr) {
    return;
  }
  const bool input_active = visible;
  const bool keyboard_visible = input_active && ShouldShowSharedKeyboard();
  if (state->detail_keyboard_visible == keyboard_visible) {
    if (!input_active) {
      HideSharedKeyboard(state->detail_keyboard);
    }
    return;
  }
  const bool follow_latest =
      state->chat_follow_latest && IsChatAtBottom(state);
  state->detail_keyboard_visible = keyboard_visible;
  // 视口改高会让 LVGL 在布局阶段自动校正滚动位置，整个过程必须视为
  // 程序滚动，否则校正事件会被误判为用户离开了最新消息。
  const bool programmatic_scroll_was_active =
      state->chat_programmatic_scroll;
  state->chat_programmatic_scroll = true;
  const int keyboard_height = state->config.height *
      kAddKeyboardHeightPercent / 100;
  const int offset = keyboard_visible ? keyboard_height : 0;
  const int composer_top = state->config.height - 108 - offset;
  lv_obj_set_y(state->detail_composer_background,
      composer_top);
  lv_obj_set_y(state->detail_divider,
      composer_top);
  lv_obj_set_y(state->detail_input,
      state->config.height - 89 - offset);
  lv_obj_set_y(state->detail_send_button,
      state->config.height - 87 - offset);
  if (state->detail_composer_action_button != nullptr) {
    lv_obj_set_y(state->detail_composer_action_button,
        state->config.height - 87 - offset);
  }
  const int32_t chat_height = std::max<int32_t>(
      0, static_cast<int32_t>(composer_top) -
             lv_obj_get_y(state->detail_chat_body));
  lv_obj_set_height(state->detail_chat_body, chat_height);
  lv_obj_update_layout(state->detail_chat_body);
  if (follow_latest) {
    // 布局完成后以 LVGL 的真实内容边界为准，避免手工高度公式与
    // 内边距或坐标边界存在少量误差，导致键盘关闭后消息轻微上移。
    const int32_t current =
        lv_obj_get_scroll_y(state->detail_chat_body);
    const int32_t bottom =
        lv_obj_get_scroll_bottom(state->detail_chat_body);
    lv_obj_scroll_to_y(
        state->detail_chat_body, current + bottom, LV_ANIM_OFF);
  }
  state->chat_programmatic_scroll = programmatic_scroll_was_active;
  state->chat_last_scroll_y =
      lv_obj_get_scroll_y(state->detail_chat_body);
  state->chat_follow_latest = follow_latest;
  PositionChatJumpButton(state);
  if (follow_latest) {
    SetChatJumpButtonVisible(state, false);
  }
  if (!input_active) {
    HideSharedKeyboard(state->detail_keyboard);
  }
}

/**
 * @brief 处理消息输入框的键盘显示和隐藏事件
 * @param event LVGL 事件对象
 */
void DetailInputEventCallback(lv_event_t* event) {
  auto* state = static_cast<RadioViewState*>(lv_event_get_user_data(event));
  const lv_event_code_t code = lv_event_get_code(event);
  if (code == LV_EVENT_FOCUSED || code == LV_EVENT_CLICKED) {
    SetDetailKeyboardVisible(state, true);
  } else if (code == LV_EVENT_READY || code == LV_EVENT_CANCEL) {
    SetDetailKeyboardVisible(state, false);
  }
}

/**
 * @brief 处理聊天输入区域右侧的激活、重试或设置操作
 * @param event LVGL 点击事件
 */
void DetailComposerActionClickedEventCallback(lv_event_t* event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
    return;
  }
  lv_event_stop_bubbling(event);
  lv_event_stop_processing(event);
  auto* state = static_cast<RadioViewState*>(lv_event_get_user_data(event));
  if (state == nullptr || state->detail_index >= state->module_count) {
    return;
  }
  const size_t index = state->detail_index;
  const RadioComposerMode mode = GetRadioComposerMode(state, index);
  if (mode == RadioComposerMode::kInactive) {
    SetProfileActiveState(state, index, true);
    return;
  }
  if (mode == RadioComposerMode::kUnsupported) {
    ShowProfileSettingsPage(state, index);
    return;
  }
  if (mode != RadioComposerMode::kChipError) {
    return;
  }
  const app::RadioProfile& profile = state->preferences.profiles[index];
  SetProfileActivationState(profile.id, RadioActivationState::kNone);
  QueueRadioControlCommand(state, RadioCommandType::kActivate,
      ToRadioConfig(profile));
  RefreshProfileSettingsPage(state);
  MarkModuleListDirty(state);
}

/**
 * @brief 根据当前 Radio 状态切换聊天输入区和整行操作按钮
 * @param state Radio 页面状态
 */
void UpdateChatComposerState(RadioViewState* state) {
  if (state == nullptr || state->detail_input == nullptr ||
      state->detail_send_button == nullptr ||
      state->detail_composer_action_button == nullptr ||
      state->detail_composer_action_label == nullptr ||
      state->detail_index >= state->module_count) {
    return;
  }
  const RadioComposerMode mode =
      GetRadioComposerMode(state, state->detail_index);
  if (mode == RadioComposerMode::kActive) {
    lv_obj_remove_state(state->detail_input, LV_STATE_DISABLED);
    lv_obj_remove_flag(state->detail_input, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(state->detail_send_button, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(
        state->detail_composer_action_button, LV_OBJ_FLAG_HIDDEN);
    return;
  }

  lv_obj_remove_state(state->detail_input, LV_STATE_FOCUSED);
  SetDetailKeyboardVisible(state, false);
  lv_obj_add_state(state->detail_input, LV_STATE_DISABLED);
  lv_obj_add_flag(state->detail_input, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(state->detail_send_button, LV_OBJ_FLAG_HIDDEN);
  lv_obj_remove_flag(
      state->detail_composer_action_button, LV_OBJ_FLAG_HIDDEN);

  const char* action_text = "Activate this profile";
  bool action_enabled = true;
  switch (mode) {
    case RadioComposerMode::kActivating:
      action_text = "Activating...";
      action_enabled = false;
      break;
    case RadioComposerMode::kChipError:
      action_text = "Retry initialization";
      break;
    case RadioComposerMode::kUnsupported:
      action_text = "Open settings";
      break;
    case RadioComposerMode::kUnavailable:
      action_text = "Radio unavailable";
      action_enabled = false;
      break;
    case RadioComposerMode::kInactive:
    case RadioComposerMode::kActive:
    default:
      break;
  }
  SetLabelTextIfChanged(state->detail_composer_action_label, action_text);
  if (action_enabled) {
    lv_obj_remove_state(
        state->detail_composer_action_button, LV_STATE_DISABLED);
  } else {
    lv_obj_add_state(
        state->detail_composer_action_button, LV_STATE_DISABLED);
  }
  lv_obj_set_style_text_color(state->detail_composer_action_label,
      lv_color_hex(action_enabled ? kOnPrimaryColor : theme::ActiveThemeColors().disabled_content),
      LV_PART_MAIN);
}

/**
 * @brief 处理聊天背景点击并隐藏输入键盘
 * @param event LVGL 事件对象
 */
void DetailKeyboardDismissClickedEventCallback(lv_event_t* event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
    return;
  }
  auto* state = static_cast<RadioViewState*>(lv_event_get_user_data(event));
  if (state == nullptr || state->detail_input == nullptr) {
    return;
  }
  lv_obj_remove_state(state->detail_input, LV_STATE_FOCUSED);
  SetDetailKeyboardVisible(state, false);
}

/**
 * @brief 创建 Telegram 风格的回到底部悬浮按钮
 * @param page 聊天详情页面
 * @param state Radio 页面状态
 * @return 创建成功返回 true，否则返回 false
 */
bool CreateChatJumpButton(lv_obj_t* page, RadioViewState* state) {
  if (page == nullptr || state == nullptr) {
    return false;
  }
  lv_obj_t* button = lv_button_create(page);
  if (button == nullptr) {
    return false;
  }
  state->detail_chat_jump_button = button;
  lv_obj_set_size(button, kChatJumpButtonSize, kChatJumpButtonSize);
  lv_obj_set_x(button, state->config.width - kChatJumpButtonSize -
      kChatJumpButtonRightMargin);
  lv_obj_set_y(button, ChatJumpButtonHiddenY(state));
  lv_obj_set_style_radius(
      button, kChatJumpButtonSize / 2, LV_PART_MAIN);
  lv_obj_set_style_bg_color(
      button, lv_color_hex(theme::ActiveThemeColors().surface), LV_PART_MAIN);
  lv_obj_set_style_bg_color(
      button, lv_color_hex(theme::ActiveThemeColors().surface_container_high), LV_STATE_PRESSED);
  lv_obj_set_style_bg_opa(button, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_bg_opa(button, LV_OPA_COVER, LV_STATE_PRESSED);
  lv_obj_set_style_border_width(button, 1, LV_PART_MAIN);
  lv_obj_set_style_border_color(
      button, lv_color_hex(theme::ActiveThemeColors().outline_variant), LV_PART_MAIN);
  lv_obj_set_style_shadow_width(button, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(button, 0, LV_PART_MAIN);
  lv_obj_add_flag(button, LV_OBJ_FLAG_GESTURE_BUBBLE);
  lv_obj_remove_flag(button, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_flag(button, LV_OBJ_FLAG_HIDDEN);
  lv_obj_t* icon_label = CreateLabel(button, icon::kKeyboardArrowDown,
      theme::ActiveThemeColors().outline, OutlineIconFont56());
  if (icon_label == nullptr) {
    return false;
  }
  lv_obj_align(icon_label, LV_ALIGN_CENTER, 0, -1);
  lv_obj_add_event_cb(button, ChatJumpButtonClickedEventCallback,
      LV_EVENT_CLICKED, state);
  return true;
}

/**
 * @brief 创建聊天页面底部发送输入区域
 * @param page 详情页面对象
 * @param state 射频页面状态
 * @return 创建成功返回 true，否则返回 false
 */
bool CreateChatComposer(lv_obj_t* page, RadioViewState* state) {
  if (page == nullptr || state == nullptr) {
    return false;
  }
  const int divider_y = state->config.height - 108;
  if (!CreateChatJumpButton(page, state)) {
    return false;
  }
  lv_obj_t* background = lv_obj_create(page);
  if (background == nullptr) {
    return false;
  }
  lv_obj_remove_flag(background, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(background, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_size(background, state->config.width, 108);
  lv_obj_set_pos(background, 0, divider_y);
  lv_obj_set_style_bg_color(background,
      lv_color_hex(theme::ActiveThemeColors().surface), LV_PART_MAIN);
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
      divider, lv_color_hex(theme::ActiveThemeColors().outline_variant), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(divider, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_border_width(divider, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(divider, 0, LV_PART_MAIN);
  state->detail_divider = divider;

  lv_obj_t* input = lv_textarea_create(page);
  if (input == nullptr) {
    return false;
  }
  lv_obj_add_flag(input, LV_OBJ_FLAG_GESTURE_BUBBLE);
  lv_textarea_set_one_line(input, false);
  const size_t maximum_payload_size = state->detail_index < state->module_count
      ? MaximumPayloadSizeForProfile(
          state, state->preferences.profiles[state->detail_index])
      : hal::kRadioPayloadCapacity;
  lv_textarea_set_max_length(input, static_cast<uint32_t>(
      std::max<size_t>(1, maximum_payload_size)));
  lv_obj_set_size(input, state->config.width - 142, kAddInputHeight);
  lv_obj_set_pos(input, 20, state->config.height - 89);
  lv_textarea_set_placeholder_text(input, "Enter a message to send...");
  lv_obj_set_style_text_font(input, Font22(), LV_PART_MAIN);
  lv_obj_set_style_text_color(
      input, lv_color_hex(theme::ActiveThemeColors().on_surface), LV_PART_MAIN);
  lv_obj_set_style_bg_color(
      input, lv_color_hex(theme::ActiveThemeColors().surface_container_low), LV_PART_MAIN);
  lv_obj_set_style_bg_color(input,
      lv_color_hex(theme::ActiveThemeColors().surface_container_low), LV_STATE_FOCUSED);
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
  lv_obj_remove_flag(send, LV_OBJ_FLAG_CLICK_FOCUSABLE);
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

  lv_obj_t* composer_action = lv_button_create(page);
  if (composer_action == nullptr) {
    return false;
  }
  lv_obj_add_flag(composer_action, LV_OBJ_FLAG_GESTURE_BUBBLE);
  lv_obj_add_flag(composer_action, LV_OBJ_FLAG_HIDDEN);
  lv_obj_set_size(composer_action, state->config.width - 96, 66);
  lv_obj_set_pos(composer_action, 48, state->config.height - 87);
  lv_obj_set_style_radius(composer_action, 33, LV_PART_MAIN);
  lv_obj_set_style_bg_color(
      composer_action, lv_color_hex(kPrimaryColor), LV_PART_MAIN);
  lv_obj_set_style_bg_color(composer_action,
      lv_color_hex(kPrimaryPressedColor), LV_STATE_PRESSED);
  lv_obj_set_style_bg_color(composer_action,
      lv_color_hex(theme::ActiveThemeColors().disabled_container), LV_STATE_DISABLED);
  lv_obj_set_style_bg_opa(composer_action, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_border_width(composer_action, 0, LV_PART_MAIN);
  lv_obj_set_style_shadow_width(composer_action, 0, LV_PART_MAIN);
  if (!AddPressCancelOnLeave(composer_action)) {
    return false;
  }
  state->detail_composer_action_button = composer_action;
  state->detail_composer_action_label = CreateLabel(
      composer_action, "Activate this profile", kOnPrimaryColor, Font22());
  if (state->detail_composer_action_label == nullptr) {
    return false;
  }
  lv_obj_center(state->detail_composer_action_label);
  lv_obj_add_event_cb(composer_action,
      DetailComposerActionClickedEventCallback, LV_EVENT_CLICKED, state);

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
  UpdateChatComposerState(state);
  return true;
}

/**
 * @brief 创建射频模块信息详情页
 * @param state 射频页面状态
 * @param index 模块索引
 * @return 创建成功返回 true，否则返回 false
 */
bool ShowModuleDetail(RadioViewState* state, size_t index) {
  if (state == nullptr || state->root == nullptr ||
      index >= state->module_count || state->detail_page != nullptr ||
      state->detail_opening || state->detail_closing) {
    return false;
  }
  state->detail_opening = true;
  const uint32_t profile_id = state->preferences.profiles[index].id;
  app::RadioChatRepository& repository = app::GetRadioChatRepository();
  repository.TouchProfile(profile_id);
  if (repository.LoadProfiles(&profile_id, 1)) {
    SyncModuleItems(state);
  }
  const RadioModuleItem& item = state->modules[index];
  ResetRenderedChatState(state);
  lv_obj_t* page = lv_obj_create(state->root);
  if (page == nullptr) {
    state->detail_opening = false;
    return false;
  }
  state->detail_page = page;
  state->detail_index = index;
  repository.MarkRead(profile_id);
  SyncModuleItems(state);
  MarkModuleListDirty(state);
  state->detail_input = nullptr;
  state->detail_keyboard = nullptr;
  state->detail_keyboard_visible = false;
  state->detail_composer_background = nullptr;
  state->detail_divider = nullptr;
  state->detail_send_button = nullptr;
  state->detail_composer_action_button = nullptr;
  state->detail_composer_action_label = nullptr;
  state->detail_chat_jump_button = nullptr;
  state->detail_chat_body = nullptr;
  state->detail_chat_timeline = nullptr;
  state->detail_title_label = nullptr;
  state->detail_chip_label = nullptr;
  lv_obj_remove_flag(page, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(page, LV_OBJ_FLAG_GESTURE_BUBBLE);
  lv_obj_set_size(page, state->config.width, state->config.height);
  lv_obj_set_style_bg_color(
      page, lv_color_hex(theme::ActiveThemeColors().surface), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(page, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_border_width(page, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(page, 0, LV_PART_MAIN);
  lv_obj_t* back = lv_button_create(page);
  if (back == nullptr) {
    lv_obj_delete(page);
    state->detail_page = nullptr;
    state->detail_opening = false;
    return false;
  }
  lv_obj_remove_style_all(back);
  lv_obj_add_flag(back, LV_OBJ_FLAG_GESTURE_BUBBLE);
  lv_obj_set_size(back, 62, 62);
  lv_obj_set_pos(back, 18, 66);
  lv_obj_add_event_cb(
      back, DetailBackClickedEventCallback, LV_EVENT_CLICKED, state);
  lv_obj_t* back_icon = CreateLabel(
      back, icon::kArrowBack, theme::ActiveThemeColors().on_surface, OutlineIconFont44());
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
    state->detail_chip_label = CreateLabel(
        avatar, item.short_name, 0xFFFFFF, Font22());
    if (state->detail_chip_label != nullptr) {
      lv_obj_center(state->detail_chip_label);
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
      page, item.name, theme::ActiveThemeColors().on_surface, Font28());
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
    state->detail_chip_label = nullptr;
    state->detail_opening = false;
    return false;
  }
  lv_obj_set_pos(chat_body, 0, chat_top);
  lv_obj_set_size(
      chat_body, state->config.width, composer_top - chat_top);
  lv_obj_set_style_bg_opa(chat_body, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_set_style_border_width(chat_body, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(chat_body, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_bottom(
      chat_body, kChatTimelineInset, LV_PART_MAIN);
  lv_obj_set_scroll_dir(chat_body, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(chat_body, LV_SCROLLBAR_MODE_AUTO);
  lv_obj_add_flag(chat_body, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(chat_body, LV_OBJ_FLAG_GESTURE_BUBBLE);
  lv_obj_add_event_cb(chat_body,
      DetailKeyboardDismissClickedEventCallback, LV_EVENT_CLICKED, state);
  lv_obj_add_event_cb(chat_body,
      DetailChatScrollEventCallback, LV_EVENT_ALL, state);
  state->detail_chat_body = chat_body;
  lv_obj_t* timeline = lv_obj_create(chat_body);
  if (timeline == nullptr) {
    lv_obj_delete(page);
    state->detail_page = nullptr;
    state->detail_chip_label = nullptr;
    state->detail_chat_body = nullptr;
    state->detail_opening = false;
    return false;
  }
  lv_obj_remove_style_all(timeline);
  lv_obj_remove_flag(timeline, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_remove_flag(timeline, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(timeline, LV_OBJ_FLAG_GESTURE_BUBBLE);
  lv_obj_set_size(timeline, state->config.width, 1);
  lv_obj_set_pos(timeline, 0, 0);
  lv_obj_add_event_cb(timeline, ChatTimelineDrawEventCallback,
      LV_EVENT_DRAW_MAIN, state);
  state->detail_chat_timeline = timeline;

  if (!RenderChatMessages(state) || !CreateChatComposer(page, state)) {
    lv_obj_delete(page);
    state->detail_page = nullptr;
    state->detail_input = nullptr;
    state->detail_keyboard = nullptr;
    state->detail_keyboard_visible = false;
    state->detail_composer_background = nullptr;
    state->detail_divider = nullptr;
    state->detail_send_button = nullptr;
    state->detail_composer_action_button = nullptr;
    state->detail_composer_action_label = nullptr;
    state->detail_chat_jump_button = nullptr;
    state->detail_chat_body = nullptr;
    state->detail_chat_timeline = nullptr;
    state->detail_chip_label = nullptr;
    state->detail_opening = false;
    return false;
  }
  StartSlideLeftWindowTransition(
      page, state->config.width, kAnimationMs, nullptr, nullptr);
  state->detail_opening = false;
  if (!RegisterBackNavigationHandler(page, [state]() {
        CloseModuleDetail(state);
      })) {
    CloseModuleDetail(state);
    return false;
  }
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
  auto* action = static_cast<RadioModuleAction*>(
      lv_event_get_user_data(event));
  if (action == nullptr || action->state == nullptr) {
    return;
  }
  if (action->long_press_handled) {
    action->long_press_handled = false;
    return;
  }
  RadioViewState* state = action->state;
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

/**
 * @brief 处理 Radio 配置行长按并进入多选模式
 * @param event LVGL 事件对象
 */
void ModuleRowLongPressedEventCallback(lv_event_t* event) {
  if (lv_event_get_code(event) != LV_EVENT_LONG_PRESSED) {
    return;
  }
  auto* action = static_cast<RadioModuleAction*>(
      lv_event_get_user_data(event));
  if (action == nullptr || action->state == nullptr ||
      action->index >= action->state->module_count) {
    return;
  }
  action->long_press_handled = true;
  action->state->selection_mode = true;
  action->state->selected_modules[action->index] = true;
  RenderHeader(action->state);
  RenderModuleList(action->state);
}

/**
 * @brief 处理 Radio 设置页面退出动画完成事件
 * @param animation LVGL 动画对象
 */
void RadioSettingsCloseCompletedCallback(lv_anim_t* animation) {
  auto* state = static_cast<RadioViewState*>(lv_anim_get_user_data(animation));
  if (state == nullptr || state->app_settings_page == nullptr) {
    return;
  }
  lv_obj_t* page = state->app_settings_page;
  state->app_settings_page = nullptr;
  state->app_settings_closing = false;
  lv_obj_delete(page);
}

/**
 * @brief 使用退出动画关闭 Radio 应用设置页面
 * @param state Radio 页面状态
 */
void CloseRadioSettingsPage(RadioViewState* state) {
  if (state == nullptr || state->app_settings_page == nullptr ||
      state->app_settings_closing) {
    return;
  }
  state->app_settings_closing = true;
  if (StartSlideRightWindowTransition(state->app_settings_page,
          state->config.width, kAnimationMs, state,
          RadioSettingsCloseCompletedCallback)) {
    return;
  }
  lv_obj_t* page = state->app_settings_page;
  state->app_settings_page = nullptr;
  state->app_settings_closing = false;
  lv_obj_delete(page);
}

void RadioSettingsBackClickedEventCallback(lv_event_t* event) {
  if (lv_event_get_code(event) == LV_EVENT_CLICKED) {
    CloseRadioSettingsPage(
        static_cast<RadioViewState*>(lv_event_get_user_data(event)));
  }
}

/**
 * @brief 创建与音乐设置页面一致的 Radio 设置页标题栏
 * @param page 设置页面对象
 * @param state Radio 页面状态
 * @return 创建成功返回 true，否则返回 false
 */
bool CreateRadioSettingsHeader(lv_obj_t* page, RadioViewState* state) {
  lv_obj_t* back = lv_button_create(page);
  if (back == nullptr) {
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
  lv_obj_add_event_cb(
      back, RadioSettingsBackClickedEventCallback, LV_EVENT_CLICKED, state);
  lv_obj_t* back_icon = CreateLabel(back, icon::kArrowBack,
      theme::ActiveThemeColors().on_surface, OutlineIconFont44());
  if (back_icon == nullptr) {
    return false;
  }
  lv_obj_align(back_icon, LV_ALIGN_CENTER, -4, 0);
  lv_obj_t* title = CreateLabel(
      page, "Radio settings", theme::ActiveThemeColors().on_surface, Font32());
  if (title == nullptr) {
    return false;
  }
  lv_obj_set_width(title, state->config.width);
  lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
  lv_obj_align(title, LV_ALIGN_TOP_MID, 0, kNavigationTitleTop);
  return true;
}

/**
 * @brief 创建 Radio 聊天数据目录的只读设置行
 * @param page 设置页面对象
 * @param state Radio 页面状态
 * @return 创建成功返回 true，否则返回 false
 */
bool CreateRadioStorageSettingRow(lv_obj_t* page, RadioViewState* state) {
  lv_obj_t* row = lv_obj_create(page);
  if (row == nullptr) {
    return false;
  }
  lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(row, state->config.width, 120);
  lv_obj_align(row, LV_ALIGN_TOP_MID, 0, 210);
  lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_set_style_border_width(row, 0, LV_PART_MAIN);
  lv_obj_set_style_radius(row, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(row, 0, LV_PART_MAIN);
  lv_obj_t* title =
      CreateLabel(row, "Storage folder", theme::ActiveThemeColors().on_surface, Font28());
  if (title != nullptr) {
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 34, 23);
  }
  char path[192] = {};
  if (!app::GetRadioChatRepository().GetStorageDirectory(path, sizeof(path))) {
    CopyBoundedString(path, sizeof(path), "Storage unavailable");
  }
  lv_obj_t* subtitle =
      CreateLabel(row, path, theme::ActiveThemeColors().outline, Font24());
  if (subtitle == nullptr) {
    return false;
  }
  lv_obj_set_width(subtitle, state->config.width - 68);
  lv_label_set_long_mode(subtitle, LV_LABEL_LONG_SCROLL_CIRCULAR);
  lv_obj_align(subtitle, LV_ALIGN_TOP_LEFT, 34, 65);
  return title != nullptr;
}

/**
 * @brief 显示 Radio 应用设置页面和聊天数据目录
 * @param state Radio 页面状态
 * @return 显示成功返回 true，否则返回 false
 */
bool ShowRadioSettingsPage(RadioViewState* state) {
  if (state == nullptr || state->root == nullptr) {
    return false;
  }
  if (state->app_settings_page != nullptr) {
    lv_obj_move_to_index(state->app_settings_page, -1);
    return true;
  }
  lv_obj_t* page = lv_obj_create(state->root);
  if (page == nullptr) {
    return false;
  }
  state->app_settings_page = page;
  state->app_settings_closing = false;
  lv_obj_remove_flag(page, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(page, LV_OBJ_FLAG_GESTURE_BUBBLE);
  lv_obj_set_size(page, state->config.width, state->config.height);
  lv_obj_set_pos(page, 0, 0);
  lv_obj_set_style_bg_color(
      page, lv_color_hex(theme::ActiveThemeColors().surface), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(page, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_border_width(page, 0, LV_PART_MAIN);
  lv_obj_set_style_radius(page, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(page, 0, LV_PART_MAIN);
  if (!CreateRadioSettingsHeader(page, state)) {
    lv_obj_delete(page);
    state->app_settings_page = nullptr;
    return false;
  }
  lv_obj_t* section = CreateLabel(page, "STORAGE", kPrimaryColor, Font22());
  if (section != nullptr) {
    lv_obj_align(section, LV_ALIGN_TOP_LEFT, 28, 164);
  }
  if (section == nullptr || !CreateRadioStorageSettingRow(page, state)) {
    lv_obj_delete(page);
    state->app_settings_page = nullptr;
    return false;
  }
  if (!StartSlideLeftWindowTransition(
          page, state->config.width, kAnimationMs, state, nullptr)) {
    lv_obj_delete(page);
    state->app_settings_page = nullptr;
    return false;
  }
  if (!RegisterBackNavigationHandler(page, [state]() {
        CloseRadioSettingsPage(state);
      })) {
    CloseRadioSettingsPage(state);
    return false;
  }
  return true;
}

/**
 * @brief 关闭射频导航侧边栏
 * @param state 射频页面状态
 */
void CloseRadioDrawer(RadioViewState* state) {
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
  CloseRadioDrawer(
      static_cast<RadioViewState*>(lv_event_get_user_data(event)));
}

void DrawerSettingsClickedEventCallback(lv_event_t* event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
    return;
  }
  auto* state = static_cast<RadioViewState*>(lv_event_get_user_data(event));
  if (state == nullptr) {
    return;
  }
  CloseRadioDrawer(state);
  ShowRadioSettingsPage(state);
}

/**
 * @brief 显示射频页面导航侧边栏
 * @param state 射频页面状态
 */
void ShowRadioDrawer(RadioViewState* state) {
  if (state == nullptr || state->root == nullptr ||
      IsNavigationDrawerOpen(&state->drawer)) {
    return;
  }
  NavigationDrawerConfig config;
  config.screen_width = state->config.width;
  config.screen_height = state->config.height;
  config.background_color = theme::ActiveThemeColors().surface;
  config.primary_text_color = theme::ActiveThemeColors().on_surface;
  config.icon_color = theme::ActiveThemeColors().on_surface_variant;
  config.pressed_color = theme::ActiveThemeColors().state_layer;
  config.divider_color = theme::ActiveThemeColors().outline_variant;
  config.title = "Radio";
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
  CreateNavigationDrawerItem(&state->drawer, icon::kSettings, "Settings", y,
      DrawerSettingsClickedEventCallback, state);
  PresentNavigationDrawer(&state->drawer);
}

/**
 * @brief 处理射频主页面菜单按钮点击事件
 * @param event LVGL 事件对象
 */
void MenuClickedEventCallback(lv_event_t* event) {
  if (lv_event_get_code(event) == LV_EVENT_CLICKED) {
    ShowRadioDrawer(
        static_cast<RadioViewState*>(lv_event_get_user_data(event)));
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
bool CreateModuleAvatar(lv_obj_t* row, const RadioModuleItem& item,
    const RadioViewState* state, size_t index) {
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
        lv_color_hex(theme::ActiveThemeColors().success), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(selection, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(selection,
        lv_color_hex(theme::ActiveThemeColors().surface), LV_PART_MAIN);
    lv_obj_set_style_border_width(selection, 3, LV_PART_MAIN);
    lv_obj_set_style_pad_all(selection, 0, LV_PART_MAIN);
    lv_obj_t* check = CreateLabel(selection, icon::kCheck,
        theme::ActiveThemeColors().on_success, FillIconFont32());
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
bool CreateModuleRow(lv_obj_t* parent, const RadioModuleItem& item,
    RadioViewState* state, size_t index, int y, int width) {
  lv_obj_t* row = lv_button_create(parent);
  if (row == nullptr) {
    return false;
  }
  lv_obj_remove_style_all(row);
  lv_obj_add_flag(row, LV_OBJ_FLAG_GESTURE_BUBBLE);
  lv_obj_set_size(row, width, kRowHeight);
  lv_obj_set_pos(row, 0, y);
  lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_set_style_bg_color(row, lv_color_hex(theme::ActiveThemeColors().state_layer),
                            LV_STATE_PRESSED);
  lv_obj_set_style_bg_opa(row, LV_OPA_COVER, LV_STATE_PRESSED);
  if (!AddPressCancelOnLeave(row) ||
      !CreateModuleAvatar(row, item, state, index)) {
    lv_obj_delete(row);
    return false;
  }
  auto* action = new RadioModuleAction{.state = state, .index = index};
  lv_obj_add_event_cb(row, ModuleRowClickedEventCallback,
                      LV_EVENT_CLICKED, action);
  lv_obj_add_event_cb(row, ModuleRowLongPressedEventCallback,
                      LV_EVENT_LONG_PRESSED, action);
  lv_obj_add_event_cb(row, ModuleActionDeleteEventCallback,
                      LV_EVENT_DELETE, action);
  lv_obj_t* title = CreateLabel(
      row, item.name, theme::ActiveThemeColors().on_surface, Font28());
  if (title != nullptr) {
    lv_obj_set_size(title, width - 250, 34);
    lv_label_set_long_mode(title, LV_LABEL_LONG_DOT);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 120, 18);
  }
  lv_obj_t* time = CreateLabel(
      row, item.time, theme::ActiveThemeColors().on_surface_variant, Font22());
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
          lv_color_hex(theme::ActiveThemeColors().error), LV_PART_MAIN);
      lv_obj_set_style_bg_opa(unread, LV_OPA_COVER, LV_PART_MAIN);
      lv_obj_set_style_border_width(unread, 0, LV_PART_MAIN);
      lv_obj_set_style_pad_all(unread, 0, LV_PART_MAIN);
      lv_obj_t* unread_label = CreateLabel(unread, unread_text,
          theme::ActiveThemeColors().on_error, Font22());
      if (unread_label != nullptr) {
        lv_obj_center(unread_label);
      }
    }
  }
  if (item.latest_message != nullptr && item.latest_message[0] != '\0') {
    lv_obj_t* message = CreateLabel(
        row, item.latest_message, theme::ActiveThemeColors().on_surface_variant, Font22());
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
        divider, lv_color_hex(theme::ActiveThemeColors().outline_variant), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(divider, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(divider, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(divider, 0, LV_PART_MAIN);
  }
  return true;
}

/**
 * @brief 处理 Radio 空状态添加配置按钮点击事件
 * @param event LVGL 事件对象
 */
void EmptyAddProfileClickedEventCallback(lv_event_t* event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
    return;
  }
  ShowAddModulePage(
      static_cast<RadioViewState*>(lv_event_get_user_data(event)));
}

/**
 * @brief 根据屏幕方向定位 Radio 空状态提示
 * @param group 状态提示容器
 * @param state Radio 页面状态
 */
void PositionRadioPromptStatus(
    lv_obj_t* group, const RadioViewState* state) {
  if (group == nullptr || state == nullptr || state->module_list == nullptr) {
    return;
  }
  if (state->config.height > state->config.width) {
    lv_obj_align(
        group, LV_ALIGN_CENTER, 0, kEmptyStatusGroupOffsetY);
    return;
  }
  lv_obj_set_pos(group, 0, kStatusGroupTopGap);
}

/**
 * @brief 创建 Radio 主界面的空配置引导内容
 * @param state Radio 页面状态
 * @return 创建成功返回 true，否则返回 false
 */
bool CreateEmptyRadioContent(RadioViewState* state) {
  if (state == nullptr || state->module_list == nullptr) {
    return false;
  }
  PromptStatusConfig config;
  config.width = state->config.width;
  config.height = 280;
  config.icon = icon::kSettingsInputAntenna;
  config.icon_font = FillIconFont56();
  config.icon_background_color = theme::ActiveThemeColors().surface_container;
  config.icon_color = kPrimaryColor;
  config.title = "No Radio profiles";
  config.title_font = Font28();
  config.title_color = theme::ActiveThemeColors().on_surface;
  config.message =
      "Tap Add profile or use the + button in the bottom-right.";
  config.message_font = Font22();
  config.message_color = theme::ActiveThemeColors().on_surface_variant;
  config.horizontal_padding = 48;
  config.button_text = "Add profile";
  config.button_font = Font24();
  config.button_width = 220;
  config.button_background_color = kPrimaryColor;
  config.button_pressed_color = kPrimaryPressedColor;
  config.button_text_color = kOnPrimaryColor;
  config.button_callback = EmptyAddProfileClickedEventCallback;
  config.button_user_data = state;
  lv_obj_t* group = CreatePromptStatus(state->module_list, config);
  if (group == nullptr) {
    return false;
  }
  PositionRadioPromptStatus(group, state);
  return true;
}

/**
 * @brief 重新构建射频模块列表并更新数量
 * @param state 射频页面状态
 * @return 构建成功返回 true，否则返回 false
 */
bool RenderModuleList(RadioViewState* state) {
  if (state == nullptr || state->module_list == nullptr) {
    return false;
  }
  lv_obj_clean(state->module_list);
  if (state->module_count == 0) {
    const bool rendered = CreateEmptyRadioContent(state);
    if (rendered) {
    }
    state->module_list_dirty = !rendered;
    return rendered;
  }
  for (size_t index = 0; index < state->module_count; ++index) {
    if (!CreateModuleRow(state->module_list, state->modules[index], state,
        index, static_cast<int>(index) * kRowHeight,
        state->config.width)) {
      state->module_list_dirty = true;
      return false;
    }
  }
  state->module_list_dirty = false;
  return true;
}

/**
 * @brief 判断会话列表当前是否位于所有子页面上方
 * @param state Radio 页面状态
 * @return 主会话列表当前可见时返回 true
 */
bool IsModuleListVisible(const RadioViewState* state) {
  return state != nullptr && state->detail_page == nullptr &&
      state->app_settings_page == nullptr &&
      state->profile_settings_page == nullptr &&
      state->profile_name_edit_page == nullptr && state->add_page == nullptr;
}

/**
 * @brief 在主会话列表可见时处理一次延迟刷新
 * @param state Radio 页面状态
 */
void RefreshModuleListIfVisible(RadioViewState* state) {
  if (state != nullptr && state->module_list_dirty &&
      IsModuleListVisible(state)) {
    RenderModuleList(state);
  }
}

/**
 * @brief 标记会话摘要已变化，并避免重建被子页面遮挡的列表
 * @param state Radio 页面状态
 */
void MarkModuleListDirty(RadioViewState* state) {
  if (state == nullptr) {
    return;
  }
  state->module_list_dirty = true;
  RefreshModuleListIfVisible(state);
}

size_t SelectedModuleCount(const RadioViewState* state) {
  size_t count = 0;
  for (size_t index = 0; state != nullptr &&
       index < state->module_count; ++index) {
    if (state->selected_modules[index]) {
      ++count;
    }
  }
  return count;
}

void CloseSelectionMode(RadioViewState* state) {
  if (state == nullptr || !state->selection_mode) {
    return;
  }
  state->selection_mode = false;
  for (bool& selected : state->selected_modules) {
    selected = false;
  }
  RenderHeader(state);
  MarkModuleListDirty(state);
}

void SelectionCloseClickedEventCallback(lv_event_t* event) {
  if (lv_event_get_code(event) == LV_EVENT_CLICKED) {
    CloseSelectionMode(
        static_cast<RadioViewState*>(lv_event_get_user_data(event)));
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
    RadioViewState* state, size_t index, bool active) {
  if (state == nullptr || index >= state->module_count) {
    return false;
  }
  app::RadioProfile& profile = state->preferences.profiles[index];
  const bool currently_active = profile.active;
  if (active == currently_active) {
    return true;
  }
  if (active) {
    for (size_t candidate = 0; candidate < state->module_count; ++candidate) {
      app::RadioProfile& other = state->preferences.profiles[candidate];
      if (candidate != index && other.active && other.chip == profile.chip) {
        FailPendingMessages(state, other.id);
        other.active = false;
        state->radio_status_available[candidate] = false;
      }
    }
    profile.active = true;
    SetProfileActivationState(profile.id, RadioActivationState::kNone);
    QueueRadioControlCommand(state, RadioCommandType::kActivate,
        ToRadioConfig(profile));
  } else {
    profile.active = false;
    FailPendingMessages(state, profile.id);
    hal::RadioConfig deactivate_config;
    deactivate_config.client_token = profile.id;
    QueueRadioControlCommand(state, RadioCommandType::kDeactivate,
        deactivate_config);
  }
  app::UpdateRadioPreferences(state->preferences);
  UpdateDetailStatus(state);
  MarkModuleListDirty(state);
  RefreshProfileSettingsPage(state);
  return true;
}

/**
 * @brief 清空射频配置名称编辑页保存的控件引用
 * @param state 射频页面状态
 */
void ResetProfileNameEditReferences(RadioViewState* state) {
  if (state == nullptr) {
    return;
  }
  state->profile_name_edit_page = nullptr;
  state->profile_name_edit_text_area = nullptr;
  state->profile_name_edit_keyboard = nullptr;
  state->profile_name_edit_closing = false;
}

/**
 * @brief 处理射频配置名称编辑页退出动画完成事件
 * @param animation LVGL 动画对象
 */
void ProfileNameEditCloseCompletedCallback(lv_anim_t* animation) {
  auto* state = static_cast<RadioViewState*>(
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
void CloseProfileNameEditPage(RadioViewState* state, bool animated) {
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
 * @brief 处理射频配置名称编辑页空白区域点击事件
 * @param event LVGL 事件对象
 */
void ProfileNameEditBackgroundClickedEventCallback(lv_event_t* event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED ||
      lv_event_get_target_obj(event) !=
          lv_event_get_current_target_obj(event)) {
    return;
  }
  auto* state = static_cast<RadioViewState*>(lv_event_get_user_data(event));
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
        static_cast<RadioViewState*>(lv_event_get_user_data(event)), true);
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
  auto* state = static_cast<RadioViewState*>(lv_event_get_user_data(event));
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
  app::RadioProfile& profile = state->preferences.profiles[index];
  if (std::strcmp(profile.name, text) == 0) {
    CloseProfileNameEditPage(state, true);
    return;
  }
  CopyBoundedString(profile.name, sizeof(profile.name), text);
  app::UpdateRadioPreferences(state->preferences);
  AppendSystemMessage(state, index, kSettingsChangedMessage);
  SyncModuleItems(state);
  if (state->detail_title_label != nullptr &&
      state->detail_index == index) {
    SetLabelTextIfChanged(state->detail_title_label, profile.name);
  }
  RefreshProfileSettingsPage(state);
  MarkModuleListDirty(state);
  CloseProfileNameEditPage(state, true);
}

/**
 * @brief 处理射频配置资料页名称区域点击事件
 * @param event LVGL 事件对象
 */
void ProfileNameAreaClickedEventCallback(lv_event_t* event) {
  if (lv_event_get_code(event) == LV_EVENT_CLICKED) {
    ShowProfileNameEditPage(
        static_cast<RadioViewState*>(lv_event_get_user_data(event)));
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
    RadioViewState* state, const char* icon_text, int x,
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
      button, icon_text, theme::ActiveThemeColors().on_surface, OutlineIconFont44());
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
      text_area, lv_color_hex(theme::ActiveThemeColors().on_surface),
      LV_PART_MAIN);
  lv_obj_set_style_bg_color(text_area,
      lv_color_hex(theme::ActiveThemeColors().surface_container_low),
      LV_PART_MAIN);
  lv_obj_set_style_bg_color(text_area,
      lv_color_hex(theme::ActiveThemeColors().surface_container_low),
      LV_STATE_FOCUSED);
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
bool ShowProfileNameEditPage(RadioViewState* state) {
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
  lv_obj_remove_flag(page, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(page, LV_OBJ_FLAG_GESTURE_BUBBLE);
  lv_obj_set_size(page, state->config.width, state->config.height);
  lv_obj_set_pos(page, 0, 0);
  lv_obj_set_style_bg_color(
      page, lv_color_hex(theme::ActiveThemeColors().surface), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(page, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_border_width(page, 0, LV_PART_MAIN);
  lv_obj_set_style_radius(page, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(page, 0, LV_PART_MAIN);
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
      page, "Edit profile name", theme::ActiveThemeColors().on_surface, Font32());
  if (title == nullptr) {
    CloseProfileNameEditPage(state, false);
    return false;
  }
  lv_obj_set_width(title, state->config.width);
  lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
  lv_obj_align(title, LV_ALIGN_TOP_MID, 0, kNavigationTitleTop);

  lv_obj_t* text_area = lv_textarea_create(page);
  if (text_area == nullptr) {
    CloseProfileNameEditPage(state, false);
    return false;
  }
  state->profile_name_edit_text_area = text_area;
  lv_obj_add_flag(text_area, LV_OBJ_FLAG_GESTURE_BUBBLE);
  lv_obj_set_size(text_area,
      state->config.width - 2 * kProfileNameEditTextAreaSide,
      kProfileNameEditTextAreaHeight);
  lv_obj_align(text_area, LV_ALIGN_TOP_LEFT, kProfileNameEditTextAreaSide,
      kProfileNameEditTextAreaTop);
  lv_textarea_set_one_line(text_area, true);
  lv_textarea_set_max_length(
      text_area, app::kRadioProfileNameCapacity - 1);
  lv_textarea_set_accepted_chars(text_area, kProfileNameAcceptedChars);
  lv_textarea_set_text(text_area,
      state->preferences.profiles[state->profile_settings_index].name);
  lv_textarea_set_cursor_pos(text_area, LV_TEXTAREA_CURSOR_LAST);
  ApplyProfileNameEditTextAreaStyle(text_area);

  lv_obj_t* help = CreateLabel(page,
      "This name is used to identify this Radio profile.",
      theme::ActiveThemeColors().on_surface_variant, Font24());
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
  if (!StartSlideLeftWindowTransition(page, state->config.width,
      kAnimationMs, state, nullptr)) {
    CloseProfileNameEditPage(state, false);
    return false;
  }
  if (!RegisterBackNavigationHandler(page, [state]() {
        CloseProfileNameEditPage(state, true);
      })) {
    CloseProfileNameEditPage(state, false);
    return false;
  }
  return true;
}

/**
 * @brief 清空射频配置资料页保存的控件引用
 * @param state 射频页面状态
 */
void ResetProfileSettingsReferences(RadioViewState* state) {
  if (state == nullptr) {
    return;
  }
  state->profile_settings_page = nullptr;
  state->profile_settings_active_switch = nullptr;
  state->profile_settings_name_label = nullptr;
  state->profile_settings_chip_label = nullptr;
  state->profile_settings_header_status_label = nullptr;
  state->profile_settings_index = kRadioModuleCapacity;
  state->profile_settings_closing = false;
}

/**
 * @brief 根据射频配置名称调整名称按钮宽度和滚动方式
 * @param state 射频页面状态
 */
void UpdateProfileSettingsNameLayout(RadioViewState* state) {
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
void RefreshProfileSettingsPage(RadioViewState* state) {
  if (state == nullptr || state->profile_settings_page == nullptr ||
      state->profile_settings_index >= state->module_count) {
    return;
  }
  const size_t index = state->profile_settings_index;
  const app::RadioProfile& profile = state->preferences.profiles[index];
  if (state->profile_settings_chip_label != nullptr) {
    SetLabelTextIfChanged(state->profile_settings_chip_label,
        ChipShortName(profile.chip));
  }
  if (state->profile_settings_name_label != nullptr) {
    if (SetLabelTextIfChanged(
            state->profile_settings_name_label, profile.name)) {
      UpdateProfileSettingsNameLayout(state);
    }
  }
  if (state->profile_settings_header_status_label != nullptr) {
    const char* status = ProfileStatusText(state, index);
    const lv_color_t status_color =
        lv_color_hex(ProfileStatusColor(status));
    SetLabelTextIfChanged(
        state->profile_settings_header_status_label, status);
    lv_obj_set_style_text_color(
        state->profile_settings_header_status_label,
        status_color, LV_PART_MAIN);
  }
  if (state->profile_settings_active_switch != nullptr) {
    const bool active = profile.active;
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
  auto* state = static_cast<RadioViewState*>(
      lv_anim_get_user_data(animation));
  if (state == nullptr || state->profile_settings_page == nullptr) {
    return;
  }
  lv_obj_t* page = state->profile_settings_page;
  ResetProfileSettingsReferences(state);
  lv_obj_delete(page);
  ApplyPendingChatScroll(state);
  RefreshModuleListIfVisible(state);
}

/**
 * @brief 使用退出动画关闭射频配置资料页
 * @param state 射频页面状态
 */
void CloseProfileSettingsPage(RadioViewState* state) {
  if (state == nullptr || state->profile_settings_page == nullptr ||
      state->profile_settings_closing) {
    return;
  }
  CloseAutoSendSettingsPage(state, false);
  CloseProfileNameEditPage(state, false);
  state->profile_settings_closing = true;
  if (!StartSlideRightWindowTransition(state->profile_settings_page,
      state->config.width, kAnimationMs, state,
      ProfileSettingsCloseCompletedCallback)) {
    lv_obj_t* page = state->profile_settings_page;
    ResetProfileSettingsReferences(state);
    lv_obj_delete(page);
    ApplyPendingChatScroll(state);
    RefreshModuleListIfVisible(state);
  }
}

/**
 * @brief 处理射频配置资料页返回按钮点击事件
 * @param event LVGL 事件对象
 */
void ProfileSettingsBackClickedEventCallback(lv_event_t* event) {
  if (lv_event_get_code(event) == LV_EVENT_CLICKED) {
    CloseProfileSettingsPage(
        static_cast<RadioViewState*>(lv_event_get_user_data(event)));
  }
}

/**
 * @brief 处理射频配置启用开关变化事件
 * @param event LVGL 事件对象
 */
void ProfileSettingsActiveChangedEventCallback(lv_event_t* event) {
  if (lv_event_get_code(event) != LV_EVENT_VALUE_CHANGED) {
    return;
  }
  auto* state = static_cast<RadioViewState*>(lv_event_get_user_data(event));
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
void ProfileRadioSettingsClickedEventCallback(lv_event_t* event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
    return;
  }
  auto* state = static_cast<RadioViewState*>(lv_event_get_user_data(event));
  if (state != nullptr &&
      state->profile_settings_index < state->module_count) {
    ShowModuleSettings(state, state->profile_settings_index, true);
  }
}

/**
 * @brief 处理自动发送设置入口点击事件
 * @param event LVGL 事件对象
 */
void ProfileAutoSendClickedEventCallback(lv_event_t* event) {
  if (lv_event_get_code(event) == LV_EVENT_CLICKED) {
    ShowAutoSendSettingsPage(
        static_cast<RadioViewState*>(lv_event_get_user_data(event)));
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
lv_obj_t* CreateProfileSettingsRow(lv_obj_t* parent, RadioViewState* state,
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
      row, lv_color_hex(theme::ActiveThemeColors().state_layer), LV_STATE_PRESSED);
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
      row, title, theme::ActiveThemeColors().on_surface, Font28());
  lv_obj_t* subtitle_label = CreateLabel(
      row, subtitle, theme::ActiveThemeColors().outline, Font24());
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
        theme::ActiveThemeColors().on_surface_variant, OutlineIconFont44());
    if (chevron == nullptr) {
      lv_obj_delete(row);
      return nullptr;
    }
    lv_obj_align(chevron, LV_ALIGN_RIGHT_MID, -34, 0);
  }
  return row;
}

/**
 * @brief 处理配置详情页删除配置操作
 * @param event LVGL 事件对象
 */
void ProfileDeleteClickedEventCallback(lv_event_t* event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
    return;
  }
  auto* state = static_cast<RadioViewState*>(lv_event_get_user_data(event));
  if (state != nullptr) {
    ShowProfileDeleteConfirmation(state, state->profile_settings_index);
  }
  lv_event_stop_bubbling(event);
  lv_event_stop_processing(event);
}

/**
 * @brief 创建配置详情页底部的删除操作行
 * @param parent 父对象
 * @param state Radio 页面状态
 * @param y 顶部坐标
 * @return 创建成功返回 true，否则返回 false
 */
bool CreateProfileDeleteRow(
    lv_obj_t* parent, RadioViewState* state, int y) {
  if (parent == nullptr || state == nullptr) {
    return false;
  }
  lv_obj_t* row = lv_obj_create(parent);
  if (row == nullptr) {
    return false;
  }
  lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_flag(row, LV_OBJ_FLAG_GESTURE_BUBBLE);
  lv_obj_set_size(row, state->config.width, 108);
  lv_obj_set_pos(row, 0, y);
  lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_set_style_bg_color(
      row, lv_color_hex(theme::ActiveThemeColors().state_layer), LV_STATE_PRESSED);
  lv_obj_set_style_bg_opa(row, LV_OPA_COVER, LV_STATE_PRESSED);
  lv_obj_set_style_border_width(row, 0, LV_PART_MAIN);
  lv_obj_set_style_radius(row, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(row, 0, LV_PART_MAIN);
  if (!AddPressCancelOnLeave(row)) {
    lv_obj_delete(row);
    return false;
  }
  lv_obj_add_event_cb(row, ProfileDeleteClickedEventCallback,
      LV_EVENT_CLICKED, state);

  lv_obj_t* label = CreateLabel(row, "Delete profile",
      theme::ActiveThemeColors().error, Font28());
  lv_obj_t* chevron = CreateLabel(row, icon::kChevronRight,
      theme::ActiveThemeColors().on_surface_variant, OutlineIconFont44());
  if (label == nullptr || chevron == nullptr) {
    lv_obj_delete(row);
    return false;
  }
  lv_obj_set_width(label, state->config.width - 138);
  lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
  lv_obj_align(label, LV_ALIGN_LEFT_MID, 34, 0);
  lv_obj_align(chevron, LV_ALIGN_RIGHT_MID, -34, 0);
  return true;
}

/**
 * @brief 创建 Radio 配置详情页操作分割线
 * @param parent 父对象
 * @param state Radio 页面状态
 * @param y 顶部坐标
 * @return 创建成功返回 true，否则返回 false
 */
bool CreateProfileActionDivider(
    lv_obj_t* parent, RadioViewState* state, int y) {
  if (parent == nullptr || state == nullptr) {
    return false;
  }
  constexpr int kSidePadding = 28;
  lv_obj_t* divider = lv_obj_create(parent);
  if (divider == nullptr) {
    return false;
  }
  lv_obj_remove_flag(divider, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(divider, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_size(divider, state->config.width - 2 * kSidePadding, 1);
  lv_obj_set_pos(divider, kSidePadding, y);
  lv_obj_set_style_bg_color(
      divider, lv_color_hex(theme::ActiveThemeColors().outline_variant), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(divider, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_border_width(divider, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(divider, 0, LV_PART_MAIN);
  return true;
}

/**
 * @brief 显示射频配置资料与设置列表页
 * @param state 射频页面状态
 * @param index 配置索引
 * @return 显示成功返回 true，否则返回 false
 */
bool ShowProfileSettingsPage(RadioViewState* state, size_t index) {
  if (state == nullptr || state->root == nullptr ||
      index >= state->module_count) {
    return false;
  }
  if (state->profile_settings_page != nullptr) {
    lv_obj_move_to_index(state->profile_settings_page, -1);
    return true;
  }
  const RadioModuleItem& item = state->modules[index];
  const app::RadioProfile& profile = state->preferences.profiles[index];
  lv_obj_t* page = lv_obj_create(state->root);
  if (page == nullptr) {
    return false;
  }
  state->profile_settings_page = page;
  state->profile_settings_index = index;
  state->profile_settings_closing = false;
  lv_obj_remove_flag(page, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(page, LV_OBJ_FLAG_GESTURE_BUBBLE);
  lv_obj_set_size(page, state->config.width, state->config.height);
  lv_obj_set_pos(page, 0, 0);
  lv_obj_set_style_bg_color(
      page, lv_color_hex(theme::ActiveThemeColors().surface), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(page, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_border_width(page, 0, LV_PART_MAIN);
  lv_obj_set_style_radius(page, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(page, 0, LV_PART_MAIN);

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
      back, icon::kArrowBack, theme::ActiveThemeColors().on_surface, OutlineIconFont44());
  lv_obj_t* page_title = CreateLabel(
      page, "Profile settings", theme::ActiveThemeColors().on_surface, Font32());
  if (back_icon == nullptr || page_title == nullptr) {
    ResetProfileSettingsReferences(state);
    lv_obj_delete(page);
    return false;
  }
  lv_obj_align(back_icon, LV_ALIGN_CENTER, -4, 0);
  lv_obj_set_width(page_title, state->config.width);
  lv_obj_set_style_text_align(
      page_title, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
  lv_obj_align(page_title, LV_ALIGN_TOP_MID, 0, kNavigationTitleTop);

  lv_obj_t* body = lv_obj_create(page);
  if (body == nullptr) {
    ResetProfileSettingsReferences(state);
    lv_obj_delete(page);
    return false;
  }
  lv_obj_set_pos(body, 0, kNavigationBodyTop);
  lv_obj_set_size(body, state->config.width,
      state->config.height - kNavigationBodyTop);
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
  state->profile_settings_chip_label = CreateLabel(
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
      name_action, lv_color_hex(theme::ActiveThemeColors().state_layer), LV_STATE_PRESSED);
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
      name_action, profile.name, theme::ActiveThemeColors().on_surface, Font36());
  const char* status = ProfileStatusText(state, index);
  state->profile_settings_header_status_label = CreateLabel(
      body, status, ProfileStatusColor(status), Font24());
  if (state->profile_settings_chip_label == nullptr ||
      state->profile_settings_name_label == nullptr ||
      state->profile_settings_header_status_label == nullptr) {
    ResetProfileSettingsReferences(state);
    lv_obj_delete(page);
    return false;
  }
  lv_obj_center(state->profile_settings_chip_label);
  UpdateProfileSettingsNameLayout(state);
  lv_obj_set_pos(
      state->profile_settings_header_status_label, 170, 94);

  if (!CreateProfileSettingsSection(body, "Radio PROFILE", 164)) {
    ResetProfileSettingsReferences(state);
    lv_obj_delete(page);
    return false;
  }
  lv_obj_t* active_row = CreateProfileSettingsRow(body, state,
      "Active profile",
      "Use this profile for sending and receiving", 196, nullptr, false);
  lv_obj_t* radio_row = CreateProfileSettingsRow(body, state,
      "Radio settings", "Manage radio parameters and behavior", 332,
      ProfileRadioSettingsClickedEventCallback, true, -2, 136);
  lv_obj_t* auto_send_row = CreateProfileSettingsRow(body, state,
      "Automatic send", "Configure repeated test transmissions", 468,
      ProfileAutoSendClickedEventCallback, true, -2, 136);
  if (active_row == nullptr || radio_row == nullptr ||
      auto_send_row == nullptr ||
      !CreateProfileActionDivider(body, state, 612) ||
      !CreateProfileDeleteRow(body, state, 620)) {
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
  ApplyRadioSwitchTheme(state->profile_settings_active_switch);
  lv_obj_add_event_cb(state->profile_settings_active_switch,
      ProfileSettingsActiveChangedEventCallback,
      LV_EVENT_VALUE_CHANGED, state);
  if (!IsProfileSupported(state, profile) && !profile.active) {
    lv_obj_add_state(
        state->profile_settings_active_switch, LV_STATE_DISABLED);
  }

  RefreshProfileSettingsPage(state);
  if (!StartSlideLeftWindowTransition(
      page, state->config.width, kAnimationMs, state, nullptr)) {
    ResetProfileSettingsReferences(state);
    lv_obj_delete(page);
    return false;
  }
  if (!RegisterBackNavigationHandler(page, [state]() {
        CloseProfileSettingsPage(state);
      })) {
    CloseProfileSettingsPage(state);
    return false;
  }
  return true;
}

/**
 * @brief 删除当前选中的 Radio 配置及其聊天记录
 * @param state Radio 页面状态
 */
void DeleteSelectedProfiles(RadioViewState* state) {
  if (state == nullptr) {
    return;
  }
  app::RadioPreferences next = state->preferences;
  size_t write_index = 0;
  bool deleted_active_profile = false;
  for (size_t read_index = 0;
       read_index < state->module_count; ++read_index) {
    if (state->selected_modules[read_index]) {
      SetProfileActivationState(next.profiles[read_index].id,
          RadioActivationState::kNone);
      app::GetRadioChatRepository().RemoveProfile(next.profiles[read_index].id);
      if (next.profiles[read_index].active) {
        const uint32_t active_id = next.profiles[read_index].id;
        FailPendingMessages(state, active_id);
        deleted_active_profile = true;
      }
      continue;
    }
    next.profiles[write_index] = next.profiles[read_index];
    ++write_index;
  }
  next.profile_count = write_index;
  if (deleted_active_profile) {
    QueueRadioControlCommand(state, RadioCommandType::kDeactivate,
        hal::RadioConfig());
  }
  for (size_t index = write_index; index < kRadioModuleCapacity; ++index) {
    next.profiles[index] = app::RadioProfile{};
  }
  state->preferences = next;
  app::UpdateRadioPreferences(state->preferences);
  SyncModuleItems(state);
  CloseSelectionMode(state);
}

/**
 * @brief 删除指定 ID 的单个 Radio 配置及其聊天记录
 * @param state Radio 页面状态
 * @param profile_id 待删除配置 ID
 */
void DeleteProfileById(RadioViewState* state, uint32_t profile_id) {
  if (state == nullptr || profile_id == 0) {
    return;
  }
  const size_t index = FindProfileIndex(state, profile_id);
  if (index >= state->module_count) {
    return;
  }

  const bool close_detail = state->detail_page != nullptr &&
      state->detail_index == index;
  CloseProfileSettingsPage(state);
  if (close_detail) {
    CloseModuleDetail(state);
  }

  app::RadioPreferences next = state->preferences;
  SetProfileActivationState(profile_id, RadioActivationState::kNone);
  app::GetRadioChatRepository().RemoveProfile(profile_id);
  if (next.profiles[index].active) {
    FailPendingMessages(state, profile_id);
    hal::RadioConfig deactivate_config;
    deactivate_config.client_token = profile_id;
    QueueRadioControlCommand(state, RadioCommandType::kDeactivate,
        deactivate_config);
  }
  for (size_t read_index = index + 1;
       read_index < next.profile_count; ++read_index) {
    next.profiles[read_index - 1] = next.profiles[read_index];
  }
  --next.profile_count;
  next.profiles[next.profile_count] = app::RadioProfile{};
  state->preferences = next;
  app::UpdateRadioPreferences(state->preferences);
  SyncModuleItems(state);
  RenderHeader(state);
  MarkModuleListDirty(state);
}

/**
 * @brief 取消单项配置删除时清除待删除 ID
 * @param context Radio 页面状态
 */
void ProfileDeleteCancelled(void* context) {
  auto* state = static_cast<RadioViewState*>(context);
  if (state != nullptr) {
    state->pending_delete_profile_id = 0;
  }
}

/**
 * @brief 确认删除单个 Radio 配置
 * @param context Radio 页面状态
 */
void ProfileDeleteConfirmed(void* context) {
  auto* state = static_cast<RadioViewState*>(context);
  if (state == nullptr) {
    return;
  }
  const uint32_t profile_id = state->pending_delete_profile_id;
  state->pending_delete_profile_id = 0;
  DeleteProfileById(state, profile_id);
}

/**
 * @brief 显示通用 Radio 删除确认底部弹窗
 * @param state Radio 页面状态
 * @param title 弹窗标题
 * @param message 二级提示文本
 * @param confirm_callback 确认回调
 * @param cancel_callback 取消回调
 * @return 显示成功返回 true，否则返回 false
 */
bool ShowRadioDeletePrompt(RadioViewState* state, const char* title,
    const char* message, PromptDialogActionCallback confirm_callback,
    PromptDialogActionCallback cancel_callback = nullptr) {
  if (state == nullptr || state->root == nullptr || title == nullptr ||
      message == nullptr || confirm_callback == nullptr ||
      IsPromptDialogVisible(&state->delete_dialog)) {
    return false;
  }

  PromptDialogConfig config;
  config.screen_width = state->config.width;
  config.screen_height = state->config.height;
  config.dialog_width =
      state->config.width - 2 * kDeletePromptSideMargin;
  const int content_width =
      config.dialog_width - 2 * kDeletePromptInnerPadding;
  lv_point_t message_size = {};
  lv_text_get_size(&message_size, message, Font24(), 0, 0,
      content_width, LV_TEXT_FLAG_NONE);
  config.dialog_height = kDeletePromptTitleTop +
      kDeletePromptTitleHeight + kDeletePromptTitleMessageGap +
      static_cast<int>(message_size.y) + kDeletePromptMessageButtonGap +
      kDeletePromptButtonHeight + kDeletePromptInnerPadding;
  config.dialog_radius = kDeletePromptRadius;
  config.inner_padding = kDeletePromptInnerPadding;
  config.header_height = 0;
  config.title_y = 0;
  config.action_height =
      kDeletePromptInnerPadding + kDeletePromptButtonHeight;
  config.action_button_height = kDeletePromptButtonHeight;
  config.action_button_radius = kDeletePromptButtonRadius;
  config.action_button_gap = kDeletePromptButtonGap;
  config.action_bottom_padding = kDeletePromptInnerPadding;
  config.bottom_margin = kDeletePromptBottomMargin;
  config.animation_ms = kAnimationMs;
  config.title = "";
  config.cancel_text = "Cancel";
  config.confirm_text = "OK";
  config.title_font = Font32();
  config.action_font = Font28();
  config.cancel_callback = cancel_callback;
  config.confirm_callback = confirm_callback;
  config.callback_context = state;
  config.slide_from_bottom = true;
  lv_obj_t* body = ShowPromptDialog(
      state->root, &state->delete_dialog, config);
  if (body == nullptr || state->delete_dialog.panel == nullptr) {
    return false;
  }
  lv_obj_remove_flag(body, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t* title_label = CreateLabel(
      body, title, theme::ActiveThemeColors().on_surface, Font32());
  lv_obj_t* message_label = CreateLabel(
      body, message, theme::ActiveThemeColors().on_surface_variant, Font24());
  if (title_label == nullptr || message_label == nullptr) {
    ClosePromptDialog(&state->delete_dialog);
    return false;
  }
  lv_obj_set_size(
      title_label, content_width, kDeletePromptTitleHeight);
  lv_obj_set_pos(
      title_label, kDeletePromptInnerPadding, kDeletePromptTitleTop);
  lv_obj_set_style_text_align(
      title_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
  lv_obj_set_width(message_label, content_width);
  lv_obj_set_pos(message_label, kDeletePromptInnerPadding,
      kDeletePromptTitleTop + kDeletePromptTitleHeight +
          kDeletePromptTitleMessageGap);
  lv_label_set_long_mode(message_label, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_text_align(
      message_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
  return true;
}

/**
 * @brief 显示单个 Radio 配置删除确认底部弹窗
 * @param state Radio 页面状态
 * @param index 待删除配置索引
 * @return 显示成功返回 true，否则返回 false
 */
bool ShowProfileDeleteConfirmation(RadioViewState* state, size_t index) {
  if (state == nullptr || index >= state->module_count) {
    return false;
  }
  state->pending_delete_profile_id = state->preferences.profiles[index].id;
  if (ShowRadioDeletePrompt(state, "Delete profile",
          "Messages and settings for this profile will be removed.",
          ProfileDeleteConfirmed, ProfileDeleteCancelled)) {
    return true;
  }
  state->pending_delete_profile_id = 0;
  return false;
}

/**
 * @brief 确认删除选中的 Radio 配置
 * @param context Radio 页面状态
 */
void DeleteProfilesConfirmed(void* context) {
  DeleteSelectedProfiles(static_cast<RadioViewState*>(context));
}

bool ShowDeleteConfirmation(RadioViewState* state) {
  return ShowRadioDeletePrompt(state, "Delete profiles",
      "Messages and settings for the selected profiles will be removed.",
      DeleteProfilesConfirmed);
}

void SelectionDeleteClickedEventCallback(lv_event_t* event) {
  if (lv_event_get_code(event) == LV_EVENT_CLICKED) {
    ShowDeleteConfirmation(
        static_cast<RadioViewState*>(lv_event_get_user_data(event)));
  }
}

lv_obj_t* CreateHeaderIconButton(lv_obj_t* parent, const char* icon_text,
    int x, int size, const lv_font_t* icon_font,
    lv_event_cb_t callback, RadioViewState* state) {
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
      button, icon_text, theme::ActiveThemeColors().on_surface, icon_font);
  if (label != nullptr) {
    lv_obj_center(label);
  }
  return button;
}

bool RenderHeader(RadioViewState* state) {
  if (state == nullptr || state->header_area == nullptr) {
    return false;
  }
  lv_obj_clean(state->header_area);
  if (state->add_button != nullptr) {
    if (state->selection_mode ||
        state->module_count >= kRadioModuleCapacity) {
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
        state->header_area, "Radio", theme::ActiveThemeColors().on_surface, Font36());
    if (menu == nullptr || title == nullptr) {
      return false;
    }
    lv_obj_set_pos(title, 104, 2);
    char summary_text[48] = {};
    if (state->module_count >= kRadioModuleCapacity) {
      std::snprintf(summary_text, sizeof(summary_text),
          "%u profiles | limit reached",
          static_cast<unsigned>(state->module_count));
    } else {
      std::snprintf(summary_text, sizeof(summary_text), "%u profiles",
          static_cast<unsigned>(state->module_count));
    }
    lv_obj_t* summary_label = CreateLabel(state->header_area,
        summary_text, theme::ActiveThemeColors().on_surface_variant, Font24());
    if (summary_label != nullptr) {
      lv_obj_set_pos(summary_label, 104, 44);
    }
    return true;
  }

  const size_t selected_count = SelectedModuleCount(state);
  lv_obj_t* close = CreateHeaderIconButton(state->header_area,
      icon::kClose, 14, 64, OutlineIconFont44(),
      SelectionCloseClickedEventCallback, state);
  if (close == nullptr) {
    return false;
  }
  if (!RegisterConditionalBackNavigationHandler(state->root, [state]() {
        if (state == nullptr || !state->selection_mode) {
          return false;
        }
        CloseSelectionMode(state);
        return true;
      })) {
    return false;
  }
  char count_text[12] = {};
  std::snprintf(count_text, sizeof(count_text), "%u",
      static_cast<unsigned>(selected_count));
  lv_obj_t* count = CreateLabel(
      state->header_area, count_text, theme::ActiveThemeColors().on_surface, Font36());
  if (count != nullptr) {
    lv_obj_set_pos(count, 102, 10);
  }
  const int right = state->config.width;
  CreateHeaderIconButton(state->header_area, icon::kDelete,
      right - 74, 64, FillIconFont44(),
      SelectionDeleteClickedEventCallback, state);
  return true;
}

bool CreateHeader(lv_obj_t* parent, RadioViewState* state) {
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
    delete static_cast<RadioAddOptionAction*>(lv_event_get_user_data(event));
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
                              : theme::ActiveThemeColors().surface_container),
        LV_PART_MAIN);
    lv_obj_set_style_bg_color(button,
        lv_color_hex(selected ? kPrimaryPressedColor
                              : theme::ActiveThemeColors().surface_container_high),
        LV_STATE_PRESSED);
    lv_obj_t* label = lv_obj_get_child(button, 0);
    if (label != nullptr) {
      lv_obj_set_style_text_color(label,
          lv_color_hex(selected ? kOnPrimaryColor
                                : theme::ActiveThemeColors().on_surface),
          LV_PART_MAIN);
    }
  }
}

/**
 * @brief 更新添加模块页面的所有选项样式
 * @param state 射频页面状态
 */
void UpdateAddOptionSelection(RadioViewState* state) {
  if (state == nullptr) {
    return;
  }
  UpdateOptionButtonGroup(
      state->add_chip_buttons,
      static_cast<int>(state->capabilities.count),
      state->selected_add_chip);
  UpdateOptionButtonGroup(
      state->add_protocol_buttons, 1, state->selected_add_protocol);
  UpdateOptionButtonGroup(
      state->add_sf_buttons, 8, state->selected_add_sf);
  UpdateOptionButtonGroup(state->add_bandwidth_buttons,
      static_cast<int>(AddProfileBandwidthCount(state)),
      state->selected_add_bandwidth);
  UpdateOptionButtonGroup(state->add_coding_rate_buttons,
      static_cast<int>(AddProfileCodingRateCount(state)),
      state->selected_add_coding_rate);
  UpdateOptionButtonGroup(state->add_rx_boost_buttons, 8,
      state->selected_add_rx_boost_mode);
  UpdateOptionButtonGroup(state->add_output_power_buttons,
      static_cast<int>(AddProfileOutputPowerCount(state)),
      state->selected_add_output_power);
  UpdateOptionButtonGroup(state->add_preamble_buttons,
      static_cast<int>(std::size(kCc1101PreambleLengths)),
      state->selected_add_preamble);
  UpdateOptionButtonGroup(state->add_receive_bandwidth_buttons,
      static_cast<int>(std::size(radio::kCc1101ReceiveBandwidthsHz)),
      state->selected_add_receive_bandwidth);
  UpdateOptionButtonGroup(state->add_data_rate_buttons,
      static_cast<int>(std::size(kNrf24l01DataRates)),
      state->selected_add_data_rate);
}

/**
 * @brief 判断当前板级射频能力是否支持输入的工作频率
 * @param capability 当前板级射频能力
 * @param frequency_mhz 以 MHz 为单位的工作频率
 * @return 频率有效返回 true，否则返回 false
 */
bool IsFrequencyValidForCapability(
    const hal::RadioCapability* capability, double frequency_mhz) {
  if (capability == nullptr || frequency_mhz < 0.0 ||
      frequency_mhz > 4294.0) {
    return false;
  }
  const uint32_t frequency_hz =
      static_cast<uint32_t>(frequency_mhz * 1000000.0 + 0.5);
  return IsFrequencySupported(*capability, frequency_hz);
}

/**
 * @brief 判断当前频段与 LoRa 带宽组合是否受芯片支持
 * @param chip 射频芯片类型
 * @param frequency_mhz 以 MHz 为单位的工作频率
 * @param bandwidth_hz 以 Hz 为单位的 LoRa 带宽
 * @return 参数组合有效返回 true
 */
bool IsBandwidthValidForFrequency(
    radio::ChipType chip, double frequency_mhz, uint32_t bandwidth_hz) {
  if (chip == radio::ChipType::kLr2021) {
    const uint32_t frequency_hz =
        static_cast<uint32_t>(frequency_mhz * 1000000.0 + 0.5);
    return radio::IsLr2021BandwidthSupported(frequency_hz, bandwidth_hz);
  }
  const bool high_frequency = chip == radio::ChipType::kLr1121 &&
                              frequency_mhz >= 2400.0 &&
                              frequency_mhz <= 2500.0;
  if (high_frequency) {
    return bandwidth_hz == 200000 || bandwidth_hz == 400000 ||
           bandwidth_hz == 800000;
  }
  return bandwidth_hz == 62500 || bandwidth_hz == 125000 ||
         bandwidth_hz == 250000 || bandwidth_hz == 500000;
}

bool IsFrequencyInputPrecisionValid(const char* text);

/**
 * @brief 校验添加模块页面中输入的工作频率
 * @param state 射频页面状态
 * @return 频率格式和范围正确返回 true，否则返回 false
 */
bool IsAddFrequencyValid(const RadioViewState* state) {
  if (state == nullptr || state->add_frequency_input == nullptr) {
    return false;
  }
  const char* text = lv_textarea_get_text(state->add_frequency_input);
  if (text == nullptr || text[0] == '\0' ||
      !IsFrequencyInputPrecisionValid(text)) {
    return false;
  }
  char* end = nullptr;
  const double frequency_mhz = std::strtod(text, &end);
  return end != nullptr && end[0] == '\0' &&
         IsFrequencyValidForCapability(
             PrimaryRadioCapability(state), frequency_mhz);
}

/**
 * @brief 校验添加模块页面当前选择的频率和带宽组合
 * @param state 射频页面状态
 * @return 组合有效返回 true
 */
bool IsAddBandwidthValid(const RadioViewState* state) {
  if (state == nullptr ||
      state->add_frequency_input == nullptr ||
      state->selected_add_bandwidth < 0 ||
      static_cast<size_t>(state->selected_add_bandwidth) >=
          AddProfileBandwidthCount(state)) {
    return false;
  }
  const char* text = lv_textarea_get_text(state->add_frequency_input);
  if (text == nullptr || text[0] == '\0') {
    return false;
  }
  char* end = nullptr;
  const double frequency_mhz = std::strtod(text, &end);
  return end != nullptr && end[0] == '\0' &&
         IsFrequencyValidForCapability(
             PrimaryRadioCapability(state), frequency_mhz) &&
         IsBandwidthValidForFrequency(AddProfileChip(state), frequency_mhz,
             AddProfileBandwidth(state, state->selected_add_bandwidth));
}

/**
 * @brief 频段切换后为无效带宽自动选择兼容默认值
 * @param state 射频页面状态
 */
void NormalizeAddBandwidthSelection(RadioViewState* state) {
  if (state == nullptr || state->add_frequency_input == nullptr ||
      IsAddBandwidthValid(state)) {
    return;
  }
  const char* text = lv_textarea_get_text(state->add_frequency_input);
  char* end = nullptr;
  const double frequency_mhz = text == nullptr ? 0.0 : std::strtod(text, &end);
  if (end == nullptr || end[0] != '\0' ||
      !IsFrequencyValidForCapability(
          PrimaryRadioCapability(state), frequency_mhz)) {
    return;
  }
  const radio::ChipType chip = AddProfileChip(state);
  const bool lr1121_high_frequency =
      chip == radio::ChipType::kLr1121 && frequency_mhz >= 2400.0;
  const uint32_t default_bandwidth_hz =
      lr1121_high_frequency ? 200000U : 125000U;
  int compatible_bandwidth_index = -1;
  for (size_t index = 0; index < AddProfileBandwidthCount(state); ++index) {
    const uint32_t bandwidth_hz = AddProfileBandwidth(state, index);
    if (!IsBandwidthValidForFrequency(chip, frequency_mhz, bandwidth_hz)) {
      continue;
    }
    if (compatible_bandwidth_index < 0) {
      compatible_bandwidth_index = static_cast<int>(index);
    }
    if (bandwidth_hz == default_bandwidth_hz) {
      compatible_bandwidth_index = static_cast<int>(index);
      break;
    }
  }
  state->selected_add_bandwidth = compatible_bandwidth_index;
  UpdateAddOptionSelection(state);
}

/**
 * @brief 根据当前频段显示并重新排列可用的带宽选项
 * @param state 射频页面状态
 */
void UpdateAddBandwidthOptionLayout(RadioViewState* state) {
  if (state == nullptr || state->add_frequency_input == nullptr) {
    return;
  }
  const char* text = lv_textarea_get_text(state->add_frequency_input);
  char* end = nullptr;
  const double frequency_mhz = text == nullptr ? 0.0 : std::strtod(text, &end);
  if (end == nullptr || end[0] != '\0' ||
      !IsFrequencyValidForCapability(
          PrimaryRadioCapability(state), frequency_mhz)) {
    return;
  }

  const size_t option_count = AddProfileBandwidthCount(state);
  bool option_visible[std::size(radio::kLr2021BandwidthsHz)] = {};
  size_t visible_count = 0;
  for (size_t index = 0; index < option_count; ++index) {
    option_visible[index] = IsBandwidthValidForFrequency(
        AddProfileChip(state), frequency_mhz,
        AddProfileBandwidth(state, index));
    if (option_visible[index]) {
      ++visible_count;
    }
  }
  if (visible_count == 0) {
    for (size_t index = 0; index < option_count; ++index) {
      if (state->add_bandwidth_buttons[index] != nullptr) {
        lv_obj_add_flag(
            state->add_bandwidth_buttons[index], LV_OBJ_FLAG_HIDDEN);
      }
    }
    return;
  }

  constexpr int kOptionLeft = 28;
  constexpr int kOptionGap = 10;
  const int option_area_width = state->config.width - 56;
  const size_t column_count = AddProfileChip(state) ==
          radio::ChipType::kLr2021
      ? 4
      : visible_count;
  const int option_width =
      (option_area_width -
          static_cast<int>(column_count - 1) * kOptionGap) /
      static_cast<int>(column_count);
  const int first_row_y = state->add_bandwidth_buttons[0] == nullptr
      ? 0
      : lv_obj_get_y(state->add_bandwidth_buttons[0]);
  size_t visible_index = 0;
  for (size_t index = 0; index < option_count; ++index) {
    lv_obj_t* button = state->add_bandwidth_buttons[index];
    if (button == nullptr) {
      continue;
    }
    if (!option_visible[index]) {
      lv_obj_add_flag(button, LV_OBJ_FLAG_HIDDEN);
      continue;
    }
    lv_obj_remove_flag(button, LV_OBJ_FLAG_HIDDEN);
    const size_t column = visible_index % column_count;
    const size_t row = visible_index / column_count;
    lv_obj_set_pos(button,
        kOptionLeft + static_cast<int>(column) * (option_width + kOptionGap),
        first_row_y + static_cast<int>(row) * 70);
    lv_obj_set_width(button, option_width);
    ++visible_index;
  }
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
 * @brief 校验当前芯片和频段对应的发射功率
 * @param state 射频页面状态
 * @return 发射功率有效返回 true
 */
bool IsAddOutputPowerValid(const RadioViewState* state) {
  if (state == nullptr) {
    return false;
  }
  const radio::ChipType chip = AddProfileChip(state);
  if (chip == radio::ChipType::kCc1101 ||
      chip == radio::ChipType::kNrf24l01) {
    return state->selected_add_output_power >= 0 &&
           static_cast<size_t>(state->selected_add_output_power) <
               AddProfileOutputPowerCount(state);
  }
  if (state->add_frequency_input == nullptr) {
    return false;
  }
  const char* frequency_text = lv_textarea_get_text(state->add_frequency_input);
  char* end = nullptr;
  const double frequency_mhz =
      frequency_text == nullptr ? 0.0 : std::strtod(frequency_text, &end);
  const bool lr1121_hf = AddProfileChip(state) == radio::ChipType::kLr1121 &&
                         end != nullptr && end[0] == '\0' &&
                         frequency_mhz >= 2400.0;
  const bool lr2021_hf = AddProfileChip(state) == radio::ChipType::kLr2021 &&
                         end != nullptr && end[0] == '\0' &&
                         frequency_mhz >= 2400.0;
  long output_power = 0;
  return ParseTextAreaLong(state->add_power_input, 10,
      lr2021_hf ? -19 : -9, lr2021_hf ? 5 : (lr1121_hf ? 13 : 22),
      &output_power);
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
      lv_color_hex(theme::ActiveThemeColors().error), LV_PART_MAIN);
  lv_obj_set_style_outline_color(input,
      lv_color_hex(theme::ActiveThemeColors().error), LV_STATE_FOCUSED);
  lv_obj_set_style_outline_opa(input, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_outline_opa(input, LV_OPA_COVER, LV_STATE_FOCUSED);
  lv_obj_set_style_outline_pad(input, -2, LV_PART_MAIN);
  lv_obj_set_style_outline_pad(input, -2, LV_STATE_FOCUSED);
}

/**
 * @brief 更新射频参数页所有输入框的错误边框
 * @param state 射频页面状态
 */
void UpdateAddInputErrorStyles(RadioViewState* state) {
  if (state == nullptr) {
    return;
  }
  long parsed_value = 0;
  uint64_t parsed_address = 0;
  const radio::ChipType chip = AddProfileChip(state);
  if (chip == radio::ChipType::kCc1101) {
    UpdateAddTextAreaErrorStyle(
        state->add_frequency_input, IsAddFrequencyValid(state));
    long data_rate = 0;
    UpdateAddTextAreaErrorStyle(state->add_data_rate_input,
        ParseTextAreaLong(state->add_data_rate_input,
            10, 600, 250000, &data_rate));
    UpdateAddTextAreaErrorStyle(state->add_frequency_deviation_input,
        ParseTextAreaLong(state->add_frequency_deviation_input,
            10, 1600, 380000, &parsed_value));
    UpdateAddTextAreaErrorStyle(state->add_sync_word_input,
        ParseTextAreaLong(state->add_sync_word_input,
            16, 0, 65535, &parsed_value));
    return;
  }
  if (chip == radio::ChipType::kNrf24l01) {
    long retry_delay = 0;
    uint8_t address_width = 0;
    const bool retry_delay_valid = ParseTextAreaLong(
        state->add_retransmit_delay_input, 10, 250, 4000, &retry_delay) &&
        retry_delay % 250 == 0;
    UpdateAddTextAreaErrorStyle(
        state->add_frequency_input,
        ParseTextAreaLong(state->add_frequency_input,
            10, 2400, 2525, &parsed_value));
    UpdateAddTextAreaErrorStyle(state->add_address_input,
        ParseEnhancedShockBurstAddress(state->add_address_input,
            &parsed_address, &address_width));
    UpdateAddTextAreaErrorStyle(state->add_retransmit_count_input,
        ParseTextAreaLong(state->add_retransmit_count_input,
            10, 0, 15, &parsed_value));
    UpdateAddTextAreaErrorStyle(
        state->add_retransmit_delay_input, retry_delay_valid);
    return;
  }
  const bool power_valid = IsAddOutputPowerValid(state);
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
bool IsAddModuleFormComplete(const RadioViewState* state) {
  if (state == nullptr) {
    return false;
  }
  const bool editing = state->editing_index < state->module_count;
  if ((!editing && state->add_name_input == nullptr) ||
      (!editing && state->add_active_switch == nullptr) ||
      (state->editing_index >= state->module_count &&
       state->module_count >= kRadioModuleCapacity)) {
    return false;
  }
  const char* profile_name = editing
      ? nullptr
      : lv_textarea_get_text(state->add_name_input);
  if (!editing && (profile_name == nullptr || profile_name[0] == '\0')) {
    return false;
  }
  const radio::ChipType chip = AddProfileChip(state);
  if (chip == radio::ChipType::kCc1101) {
    return IsAddGfskFormComplete(state);
  }
  if (chip == radio::ChipType::kNrf24l01) {
    return IsAddEnhancedShockBurstFormComplete(state);
  }
  const bool lr2021 = chip == radio::ChipType::kLr2021;
  if (state->add_frequency_input == nullptr ||
      state->add_power_input == nullptr ||
      state->add_preamble_input == nullptr ||
      state->add_sync_word_input == nullptr ||
      state->add_crc_switch == nullptr || state->add_iq_switch == nullptr ||
      (!lr2021 && state->add_rx_boost_switch == nullptr) ||
      (state->capabilities.supports_external_antenna &&
          state->add_external_antenna_switch == nullptr)) {
    return false;
  }
  const char* frequency =
      lv_textarea_get_text(state->add_frequency_input);
  long preamble = 0;
  long sync_word = 0;
  return frequency != nullptr && frequency[0] != '\0' &&
         IsAddFrequencyValid(state) && IsAddBandwidthValid(state) &&
         IsAddOutputPowerValid(state) &&
         ParseTextAreaLong(state->add_preamble_input, 10, 1, 65535,
             &preamble) &&
         ParseTextAreaLong(state->add_sync_word_input, 16, 0, 255,
             &sync_word) &&
         state->selected_add_chip >= 0 &&
         static_cast<size_t>(state->selected_add_chip) <
             state->capabilities.count &&
         state->selected_add_protocol >= 0 &&
         state->selected_add_protocol == 0 && state->selected_add_sf >= 0 &&
         state->selected_add_sf < 8 &&
         state->selected_add_bandwidth >= 0 &&
         static_cast<size_t>(state->selected_add_bandwidth) <
             AddProfileBandwidthCount(state) &&
         state->selected_add_coding_rate >= 0 &&
         static_cast<size_t>(state->selected_add_coding_rate) <
             AddProfileCodingRateCount(state) &&
         (!lr2021 || (state->selected_add_rx_boost_mode >= 0 &&
             state->selected_add_rx_boost_mode <= 7));
}

/**
 * @brief 更新添加模块提交按钮的启用状态
 * @param state 射频页面状态
 */
void UpdateAddSubmitButton(RadioViewState* state) {
  if (state == nullptr || state->add_submit_button == nullptr) {
    return;
  }
  UpdateAddInputErrorStyles(state);
  const bool form_complete = IsAddModuleFormComplete(state);
  if (state->add_submitting) {
    lv_obj_remove_state(state->add_submit_button, LV_STATE_DISABLED);
    lv_obj_remove_flag(state->add_submit_button, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(state->add_submit_button,
        lv_color_hex(kPrimaryColor), LV_PART_MAIN);
    if (state->add_submit_label != nullptr) {
      lv_obj_set_style_text_color(state->add_submit_label,
          lv_color_hex(kOnPrimaryColor), LV_PART_MAIN);
    }
    return;
  }
  lv_obj_add_flag(state->add_submit_button, LV_OBJ_FLAG_CLICKABLE);
  const bool enabled = form_complete;
  if (enabled) {
    lv_obj_remove_state(state->add_submit_button, LV_STATE_DISABLED);
  } else {
    lv_obj_add_state(state->add_submit_button, LV_STATE_DISABLED);
  }
  lv_obj_set_style_bg_color(state->add_submit_button,
      lv_color_hex(enabled ? kPrimaryColor : theme::ActiveThemeColors().disabled_container),
      LV_PART_MAIN);
  if (state->add_submit_label != nullptr) {
    lv_obj_set_style_text_color(state->add_submit_label,
        lv_color_hex(enabled ? kOnPrimaryColor : theme::ActiveThemeColors().disabled_content),
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
  auto* action = static_cast<RadioAddOptionAction*>(
      lv_event_get_user_data(event));
  if (action == nullptr || action->state == nullptr) {
    return;
  }
  SetAddKeyboardVisible(action->state, nullptr, false);
  if (action->group == RadioAddOptionGroup::kChip) {
    if (action->state->selected_add_chip == action->index) {
      return;
    }
    action->state->selected_add_chip = action->index;
    action->state->selected_add_protocol = 0;
    lv_async_call([](void* context) {
      RebuildAddModuleForm(static_cast<RadioViewState*>(context));
    }, action->state);
    return;
  } else if (action->group == RadioAddOptionGroup::kProtocol) {
    action->state->selected_add_protocol = action->index;
  } else if (action->group == RadioAddOptionGroup::kSpreadingFactor) {
    action->state->selected_add_sf = action->index;
  } else if (action->group == RadioAddOptionGroup::kBandwidth) {
    action->state->selected_add_bandwidth = action->index;
  } else if (action->group == RadioAddOptionGroup::kCodingRate) {
    action->state->selected_add_coding_rate = action->index;
  } else if (action->group == RadioAddOptionGroup::kRxBoost) {
    action->state->selected_add_rx_boost_mode = action->index;
  } else if (action->group == RadioAddOptionGroup::kOutputPower) {
    action->state->selected_add_output_power = action->index;
  } else if (action->group == RadioAddOptionGroup::kPreamble) {
    action->state->selected_add_preamble = action->index;
  } else if (action->group == RadioAddOptionGroup::kReceiveBandwidth) {
    action->state->selected_add_receive_bandwidth = action->index;
  } else if (action->group == RadioAddOptionGroup::kDataRate) {
    action->state->selected_add_data_rate = action->index;
  }
  UpdateAddOptionSelection(action->state);
  UpdateAddSubmitButton(action->state);
}

/**
 * @brief 调整添加模块页面的键盘、操作区和滚动区域
 * @param state 射频页面状态
 * @param input 当前编辑的输入框
 * @param visible 是否显示键盘
 */
void SetAddKeyboardVisible(
    RadioViewState* state, lv_obj_t* input, bool visible) {
  if (state == nullptr || state->add_body == nullptr ||
      state->add_action_area == nullptr) {
    return;
  }
  const bool input_active = visible;
  const bool keyboard_visible = input_active && ShouldShowSharedKeyboard();
  const int normal_height = state->config.height -
      kAddPageHeaderHeight - kAddPageActionHeight;
  if (!keyboard_visible) {
    if (!input_active) {
      HideSharedKeyboard(state->add_keyboard);
    }
    lv_obj_align(
        state->add_action_area, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_height(state->add_body, normal_height);
    lv_obj_update_layout(state->add_body);
    return;
  }

  const int keyboard_height =
      state->config.height * kAddKeyboardHeightPercent / 100;
  const int visible_height = state->config.height - keyboard_height -
      kAddPageHeaderHeight - kAddPageActionHeight - kAddKeyboardTopGap;
  if (visible_height <= 0 || input == nullptr) {
    return;
  }
  lv_obj_align(state->add_action_area, LV_ALIGN_BOTTOM_MID, 0,
      -keyboard_height - kAddKeyboardTopGap);
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
 * @brief 根据自动应答状态更新动态负载选项
 * @param state Radio 页面状态
 */
void UpdateEnhancedShockBurstDependencyControls(RadioViewState* state) {
  if (state == nullptr || state->add_auto_ack_switch == nullptr ||
      state->add_dynamic_payload_switch == nullptr) {
    return;
  }
  if (lv_obj_has_state(state->add_auto_ack_switch, LV_STATE_CHECKED)) {
    lv_obj_remove_state(
        state->add_dynamic_payload_switch, LV_STATE_DISABLED);
    return;
  }
  lv_obj_remove_state(
      state->add_dynamic_payload_switch, LV_STATE_CHECKED);
  lv_obj_add_state(state->add_dynamic_payload_switch, LV_STATE_DISABLED);
}

/**
 * @brief 判断频率文本是否符合最多六位 MHz 小数的输入格式
 * @param text 待检查文本
 * @return 格式符合返回 true
 */
bool IsFrequencyInputPrecisionValid(const char* text) {
  if (text == nullptr) {
    return false;
  }
  const char* decimal_point = std::strchr(text, '.');
  if (decimal_point == nullptr) {
    return true;
  }
  return std::strchr(decimal_point + 1, '.') == nullptr &&
         std::strlen(decimal_point + 1) <= kFrequencyDecimalPlaces;
}

/**
 * @brief 处理添加模块输入框状态和内容变化事件
 * @param event LVGL 事件对象
 */
void AddInputEventCallback(lv_event_t* event) {
  auto* state = static_cast<RadioViewState*>(lv_event_get_user_data(event));
  const lv_event_code_t code = lv_event_get_code(event);
  lv_obj_t* target = lv_event_get_target_obj(event);
  if (code == LV_EVENT_INSERT && state != nullptr &&
      target == state->add_frequency_input) {
    const char* text = lv_textarea_get_text(target);
    const char* inserted_text =
        static_cast<const char*>(lv_event_get_param(event));
    if (text == nullptr || inserted_text == nullptr ||
        inserted_text[0] == LV_KEY_DEL) {
      return;
    }
    const size_t text_length = std::strlen(text);
    const size_t inserted_length = std::strlen(inserted_text);
    const size_t cursor_position = std::min(
        static_cast<size_t>(lv_textarea_get_cursor_pos(target)), text_length);
    char candidate[kFrequencyInputMaximumLength + 2] = {};
    if (text_length + inserted_length >= sizeof(candidate)) {
      lv_textarea_set_insert_replace(target, "");
      return;
    }
    std::memcpy(candidate, text, cursor_position);
    std::memcpy(candidate + cursor_position, inserted_text, inserted_length);
    std::memcpy(candidate + cursor_position + inserted_length,
        text + cursor_position, text_length - cursor_position + 1);
    if (!IsFrequencyInputPrecisionValid(candidate)) {
      lv_textarea_set_insert_replace(target, "");
    }
    return;
  }
  if (code == LV_EVENT_VALUE_CHANGED) {
    if (state != nullptr) {
      if (lv_event_get_target_obj(event) == state->add_frequency_input) {
        NormalizeAddBandwidthSelection(state);
        UpdateAddBandwidthOptionLayout(state);
      } else if (lv_event_get_target_obj(event) ==
                 state->add_auto_ack_switch) {
        UpdateEnhancedShockBurstDependencyControls(state);
      }
    }
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
  auto* state = static_cast<RadioViewState*>(
      lv_anim_get_user_data(animation));
  if (state == nullptr || state->add_page == nullptr) {
    return;
  }
  lv_obj_t* page = state->add_page;
  state->add_page = nullptr;
  state->add_body = nullptr;
  state->add_name_input = nullptr;
  state->add_frequency_input = nullptr;
  state->add_power_input = nullptr;
  state->add_preamble_input = nullptr;
  state->add_sync_word_input = nullptr;
  state->add_crc_switch = nullptr;
  state->add_iq_switch = nullptr;
  state->add_rx_boost_switch = nullptr;
  state->add_external_antenna_switch = nullptr;
  state->add_active_switch = nullptr;
  state->add_action_area = nullptr;
  state->add_keyboard = nullptr;
  state->add_submit_button = nullptr;
  state->add_submit_label = nullptr;
  ResetAddModuleFormPointers(state);
  state->editing_index = kRadioModuleCapacity;
  state->add_submitting = false;
  state->add_closing = false;
  lv_obj_delete(page);
  ApplyPendingChatScroll(state);
  RefreshModuleListIfVisible(state);
}

/**
 * @brief 使用退出动画关闭添加模块页面
 * @param state 射频页面状态
 */
void CloseAddModulePage(RadioViewState* state) {
  if (state == nullptr || state->add_page == nullptr ||
      state->add_closing) {
    return;
  }
  // 键盘是 add_page 的子对象，会随页面一起滑出并删除。此处不再解绑
  // textarea，避免关闭回调中触发一次无意义的焦点和字体布局刷新。
  state->add_closing = true;
  if (!StartSlideRightWindowTransition(state->add_page,
      state->config.width, kAnimationMs, state,
      AddPageCloseCompletedCallback)) {
    lv_obj_t* page = state->add_page;
    state->add_page = nullptr;
    state->add_body = nullptr;
    state->add_name_input = nullptr;
    state->add_frequency_input = nullptr;
    state->add_power_input = nullptr;
    state->add_preamble_input = nullptr;
    state->add_sync_word_input = nullptr;
    state->add_crc_switch = nullptr;
    state->add_iq_switch = nullptr;
    state->add_rx_boost_switch = nullptr;
    state->add_external_antenna_switch = nullptr;
    state->add_active_switch = nullptr;
    state->add_action_area = nullptr;
    state->add_keyboard = nullptr;
    state->add_submit_button = nullptr;
    state->add_submit_label = nullptr;
    ResetAddModuleFormPointers(state);
    state->editing_index = kRadioModuleCapacity;
    state->add_submitting = false;
    state->add_closing = false;
    lv_obj_delete(page);
    ApplyPendingChatScroll(state);
    RefreshModuleListIfVisible(state);
  }
}

/**
 * @brief 处理添加模块页面返回按钮点击事件
 * @param event LVGL 事件对象
 */
void AddPageBackClickedEventCallback(lv_event_t* event) {
  if (lv_event_get_code(event) == LV_EVENT_CLICKED) {
    CloseAddModulePage(
        static_cast<RadioViewState*>(lv_event_get_user_data(event)));
  }
}

/**
 * @brief 处理添加模块页面空白区域点击事件
 * @param event LVGL 事件对象
 */
void AddPageBackgroundClickedEventCallback(lv_event_t* event) {
  if (lv_event_get_code(event) == LV_EVENT_CLICKED &&
      lv_event_get_target_obj(event) ==
          lv_event_get_current_target_obj(event)) {
    auto* state = static_cast<RadioViewState*>(
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
  auto* state = static_cast<RadioViewState*>(lv_event_get_user_data(event));
  if (state == nullptr || state->add_submitting ||
      !IsAddModuleFormComplete(state)) {
    return;
  }
  state->add_submitting = true;
  UpdateAddSubmitButton(state);
  lv_event_stop_bubbling(event);
  lv_event_stop_processing(event);
  const uint32_t started_ms = lv_tick_get();
  const bool editing = state->editing_index < state->module_count;
  const size_t index = editing
      ? state->editing_index
      : state->module_count;
  app::RadioProfile profile = editing
      ? state->preferences.profiles[index]
      : app::RadioProfile{};
  const app::RadioProfile previous_profile = profile;
  ApplyPrimaryRadioCapability(state, &profile);
  if (!editing) {
    profile.id = state->preferences.next_profile_id++;
    if (profile.id == 0) {
      profile.id = state->preferences.next_profile_id++;
    }
    // ID 回绕或旧配置删除后重新使用 ID 时，不继承旧配置的失败锁存。
    SetProfileActivationState(profile.id, RadioActivationState::kNone);
    CopyBoundedString(profile.name, sizeof(profile.name),
        lv_textarea_get_text(state->add_name_input));
  }
  if (profile.protocol == radio::ProtocolType::kGfsk ||
      profile.protocol == radio::ProtocolType::kEnhancedShockBurst) {
    profile.output_power_dbm = AddProfileOutputPower(
        state, static_cast<size_t>(state->selected_add_output_power));
  } else {
    long output_power = 0;
    ParseTextAreaLong(state->add_power_input, 10, -9, 22, &output_power);
    profile.output_power_dbm = static_cast<int8_t>(output_power);
  }
  profile.antenna = radio::AntennaType::kInternal;
  if (profile.protocol == radio::ProtocolType::kGfsk) {
    const double frequency_mhz = std::strtod(
        lv_textarea_get_text(state->add_frequency_input), nullptr);
    long data_rate = 0;
    long deviation = 0;
    long sync_word = 0;
    ParseTextAreaLong(state->add_data_rate_input,
        10, 600, 250000, &data_rate);
    ParseTextAreaLong(state->add_frequency_deviation_input,
        10, 1600, 380000, &deviation);
    ParseTextAreaLong(
        state->add_sync_word_input, 16, 0, 65535, &sync_word);
    profile.frequency_hz = static_cast<uint32_t>(
        frequency_mhz * 1000000.0 + 0.5);
    profile.gfsk_data_rate_bps = static_cast<uint32_t>(data_rate);
    profile.gfsk_frequency_deviation_hz =
        static_cast<uint32_t>(deviation);
    profile.gfsk_receive_bandwidth_hz =
        radio::kCc1101ReceiveBandwidthsHz[
            state->selected_add_receive_bandwidth];
    profile.preamble_length = kCc1101PreambleLengths[
        state->selected_add_preamble];
    profile.gfsk_sync_word = static_cast<uint16_t>(sync_word);
    profile.crc_enabled = lv_obj_has_state(
        state->add_crc_switch, LV_STATE_CHECKED);
    profile.gfsk_whitening_enabled = lv_obj_has_state(
        state->add_whitening_switch, LV_STATE_CHECKED);
    if (state->add_fec_switch != nullptr) {
      profile.gfsk_fec_enabled = lv_obj_has_state(
          state->add_fec_switch, LV_STATE_CHECKED);
    }
  } else if (profile.protocol ==
             radio::ProtocolType::kEnhancedShockBurst) {
    long frequency_mhz = 0;
    long retransmit_count = 0;
    long retransmit_delay = 0;
    uint64_t address = 0;
    uint8_t address_width = 0;
    ParseTextAreaLong(
        state->add_frequency_input, 10, 2400, 2525, &frequency_mhz);
    ParseEnhancedShockBurstAddress(
        state->add_address_input, &address, &address_width);
    ParseTextAreaLong(state->add_retransmit_count_input,
        10, 0, 15, &retransmit_count);
    ParseTextAreaLong(state->add_retransmit_delay_input,
        10, 250, 4000, &retransmit_delay);
    profile.esb_channel = static_cast<uint8_t>(frequency_mhz - 2400);
    profile.frequency_hz =
        static_cast<uint32_t>(frequency_mhz) * 1000000U;
    profile.esb_data_rate_bps =
        kNrf24l01DataRates[state->selected_add_data_rate];
    profile.esb_address = address;
    profile.esb_address_width = address_width;
    profile.esb_retransmit_count =
        static_cast<uint8_t>(retransmit_count);
    profile.esb_retransmit_delay_us =
        static_cast<uint16_t>(retransmit_delay);
    profile.esb_auto_ack_enabled = lv_obj_has_state(
        state->add_auto_ack_switch, LV_STATE_CHECKED);
    profile.esb_dynamic_payload_enabled =
        profile.esb_auto_ack_enabled && lv_obj_has_state(
            state->add_dynamic_payload_switch, LV_STATE_CHECKED);
  } else {
    const char* frequency_text = lv_textarea_get_text(
        state->add_frequency_input);
    const double frequency_mhz = std::strtod(frequency_text, nullptr);
    long preamble = 0;
    long sync_word = 0;
    ParseTextAreaLong(state->add_preamble_input, 10, 1, 65535,
        &preamble);
    ParseTextAreaLong(state->add_sync_word_input, 16, 0, 255,
        &sync_word);
    profile.frequency_hz = static_cast<uint32_t>(
        frequency_mhz * 1000000.0 + 0.5);
    profile.bandwidth_hz =
        AddProfileBandwidth(state, state->selected_add_bandwidth);
    profile.preamble_length = static_cast<uint16_t>(preamble);
    profile.spreading_factor = static_cast<uint8_t>(
        state->selected_add_sf + 5);
    if (profile.chip == radio::ChipType::kLr2021) {
      profile.lr2021_coding_rate = AddProfileLr2021CodingRate(
          state, state->selected_add_coding_rate);
      profile.coding_rate_denominator =
          radio::Lr2021CodingRateDenominator(
              profile.lr2021_coding_rate);
    } else {
      profile.coding_rate_denominator = static_cast<uint8_t>(
          state->selected_add_coding_rate + 5);
      profile.lr2021_coding_rate = static_cast<radio::Lr2021CodingRate>(
          profile.coding_rate_denominator - 4);
    }
    profile.sync_word = static_cast<uint8_t>(sync_word);
    profile.crc_enabled = lv_obj_has_state(
        state->add_crc_switch, LV_STATE_CHECKED);
    profile.invert_iq = lv_obj_has_state(
        state->add_iq_switch, LV_STATE_CHECKED);
    if (profile.chip == radio::ChipType::kLr2021) {
      profile.lr2021_rx_boost_mode = static_cast<uint8_t>(
          state->selected_add_rx_boost_mode);
      profile.rx_boosted = profile.lr2021_rx_boost_mode != 0;
    } else {
      profile.rx_boosted = lv_obj_has_state(
          state->add_rx_boost_switch, LV_STATE_CHECKED);
      profile.lr2021_rx_boost_mode = profile.rx_boosted ? 7 : 0;
    }
    profile.antenna = state->add_external_antenna_switch != nullptr &&
        lv_obj_has_state(state->add_external_antenna_switch, LV_STATE_CHECKED)
        ? radio::AntennaType::kExternal
        : radio::AntennaType::kInternal;
  }
  const bool settings_changed = editing &&
      !AreProfileSettingsEqual(previous_profile, profile);
  if (settings_changed) {
    SetProfileActivationState(profile.id, RadioActivationState::kNone);
  }
  state->preferences.profiles[index] = profile;
  if (!editing) {
    ++state->preferences.profile_count;
  }
  const bool activate_new_profile = !editing && lv_obj_has_state(
      state->add_active_switch, LV_STATE_CHECKED);
  const uint32_t form_done_ms = lv_tick_get();
  const bool requires_reconfigure = editing && settings_changed &&
      profile.active;
  bool preferences_persisted = false;
  if (activate_new_profile) {
    for (size_t candidate = 0;
         candidate < state->preferences.profile_count; ++candidate) {
      app::RadioProfile& other = state->preferences.profiles[candidate];
      if (candidate != index && other.active && other.chip == profile.chip) {
        FailPendingMessages(state, other.id);
        other.active = false;
        state->radio_status_available[candidate] = false;
      }
    }
    profile.active = true;
    state->preferences.profiles[index].active = true;
    SetProfileActivationState(profile.id, RadioActivationState::kNone);
    // 先持久化最终配置，再启动异步检测，保证任务结果能按完整配置身份
    // 校验，不会把新配置误认为已删除或已修改的旧请求。
    preferences_persisted =
        app::UpdateRadioPreferences(state->preferences);
    QueueRadioControlCommand(state, RadioCommandType::kActivate,
        ToRadioConfig(profile));
  } else if (requires_reconfigure) {
    if (previous_profile.chip != profile.chip) {
      hal::RadioConfig deactivate_config;
      deactivate_config.client_token = previous_profile.id;
      QueueRadioControlCommand(state, RadioCommandType::kDeactivate,
          deactivate_config);
      for (size_t candidate = 0;
           candidate < state->preferences.profile_count; ++candidate) {
        app::RadioProfile& other = state->preferences.profiles[candidate];
        if (candidate != index && other.active && other.chip == profile.chip) {
          FailPendingMessages(state, other.id);
          other.active = false;
          state->radio_status_available[candidate] = false;
        }
      }
    }
    FailPendingMessages(state, profile.id);
    SetProfileActivationState(profile.id, RadioActivationState::kNone);
    preferences_persisted =
        app::UpdateRadioPreferences(state->preferences);
    QueueRadioControlCommand(state, RadioCommandType::kActivate,
        ToRadioConfig(profile));
  }
  const uint32_t command_done_ms = lv_tick_get();
  if (!preferences_persisted &&
      !app::UpdateRadioPreferences(state->preferences)) {
    LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
        "Persist Radio settings failed\n");
  }
  if (!editing) {
    AppendSystemMessage(state, index, kProfileCreatedMessage);
  } else if (settings_changed) {
    AppendSystemMessage(state, index, kSettingsChangedMessage);
  }
  SyncModuleItems(state);
  if (!editing) {
    RenderHeader(state);
  }
  MarkModuleListDirty(state);
  RefreshProfileSettingsPage(state);
  CloseSelectionMode(state);
  const uint32_t data_done_ms = lv_tick_get();
  if (state->detail_index == index) {
    if (state->detail_title_label != nullptr) {
      SetLabelTextIfChanged(state->detail_title_label, profile.name);
    }
    if (state->detail_chip_label != nullptr) {
      SetLabelTextIfChanged(
          state->detail_chip_label, ChipShortName(profile.chip));
    }
    RenderChatMessages(state);
  }
  const uint32_t detail_done_ms = lv_tick_get();
  UpdateDetailStatus(state);
  const uint32_t status_done_ms = lv_tick_get();
  CloseAddModulePage(state);
  const uint32_t finished_ms = lv_tick_get();
  if (finished_ms - started_ms >= kSlowRadioUiThresholdMs) {
    LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
        "Radio settings UI slow: form=%lu ms, command=%lu ms, "
        "data=%lu ms, detail=%lu ms, status=%lu ms, close=%lu ms\n",
        static_cast<unsigned long>(form_done_ms - started_ms),
        static_cast<unsigned long>(command_done_ms - form_done_ms),
        static_cast<unsigned long>(data_done_ms - command_done_ms),
        static_cast<unsigned long>(detail_done_ms - data_done_ms),
        static_cast<unsigned long>(status_done_ms - detail_done_ms),
        static_cast<unsigned long>(finished_ms - status_done_ms));
  }
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
lv_obj_t* CreateAddOptionButton(lv_obj_t* parent, RadioViewState* state,
    RadioAddOptionGroup group, int index, const char* text, int x, int y,
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
      button, lv_color_hex(theme::ActiveThemeColors().surface_container), LV_PART_MAIN);
  lv_obj_set_style_bg_color(
      button, lv_color_hex(theme::ActiveThemeColors().surface_container_high), LV_STATE_PRESSED);
  lv_obj_set_style_bg_opa(button, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_border_width(button, 0, LV_PART_MAIN);
  lv_obj_set_style_shadow_width(button, 0, LV_PART_MAIN);
  if (!AddPressCancelOnLeave(button)) {
    lv_obj_delete(button);
    return nullptr;
  }
  auto* action = new RadioAddOptionAction{
      .state = state,
      .group = group,
      .index = index,
  };
  lv_obj_add_event_cb(button, AddOptionClickedEventCallback,
      LV_EVENT_CLICKED, action);
  lv_obj_add_event_cb(button, AddOptionActionDeleteEventCallback,
      LV_EVENT_DELETE, action);
  lv_obj_t* label = CreateLabel(button, text, theme::ActiveThemeColors().on_surface, Font22());
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
lv_obj_t* CreateAddTextArea(lv_obj_t* parent, RadioViewState* state,
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
      input, lv_color_hex(theme::ActiveThemeColors().on_surface), LV_PART_MAIN);
  lv_obj_set_style_bg_color(
      input, lv_color_hex(theme::ActiveThemeColors().surface_container_low), LV_PART_MAIN);
  lv_obj_set_style_bg_color(
      input, lv_color_hex(theme::ActiveThemeColors().surface_container_low), LV_STATE_FOCUSED);
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
  return input;
}

/**
 * @brief 为十六进制输入框创建与 LoRa 参数一致的 0x 前缀
 * @param parent 输入框父对象
 * @param state Radio 页面状态
 * @param input 十六进制输入框
 * @param y 输入框顶部坐标
 * @return 前缀创建成功返回 true，否则返回 false
 */
bool CreateAddHexPrefix(lv_obj_t* parent, RadioViewState* state,
    lv_obj_t* input, int y) {
  if (parent == nullptr || state == nullptr || input == nullptr) {
    return false;
  }
  constexpr int kSideMargin = 28;
  constexpr int kPrefixWidth = 72;
  constexpr int kInputGap = 12;
  const int input_x = kSideMargin + kPrefixWidth + kInputGap;
  lv_obj_set_x(input, input_x);
  lv_obj_set_width(input, state->config.width - input_x - kSideMargin);

  lv_obj_t* prefix = lv_obj_create(parent);
  if (prefix == nullptr) {
    return false;
  }
  lv_obj_remove_flag(prefix, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(prefix, kPrefixWidth, kAddInputHeight);
  lv_obj_set_pos(prefix, kSideMargin, y);
  lv_obj_set_style_bg_color(
      prefix, lv_color_hex(theme::ActiveThemeColors().surface_container_high), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(prefix, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_border_width(prefix, 0, LV_PART_MAIN);
  lv_obj_set_style_radius(prefix, 22, LV_PART_MAIN);
  lv_obj_set_style_pad_all(prefix, 0, LV_PART_MAIN);
  lv_obj_t* label = CreateLabel(
      prefix, "0x", theme::ActiveThemeColors().on_surface_variant, Font22());
  if (label == nullptr) {
    lv_obj_delete(prefix);
    return false;
  }
  lv_obj_center(label);
  return true;
}

lv_obj_t* CreateAddSwitchRow(lv_obj_t* parent, RadioViewState* state,
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
      row, lv_color_hex(theme::ActiveThemeColors().surface_container_low),
      LV_PART_MAIN);
  lv_obj_set_style_bg_opa(row, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_border_width(row, 0, LV_PART_MAIN);
  lv_obj_set_style_radius(row, 22, LV_PART_MAIN);
  lv_obj_set_style_pad_all(row, 0, LV_PART_MAIN);
  lv_obj_t* title_label = CreateLabel(
      row, title, theme::ActiveThemeColors().on_surface, Font24());
  lv_obj_t* subtitle_label = CreateLabel(
      row, subtitle, theme::ActiveThemeColors().on_surface_variant, Font22());
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
  ApplyRadioSwitchTheme(toggle);
  if (checked) {
    lv_obj_add_state(toggle, LV_STATE_CHECKED);
  }
  lv_obj_add_event_cb(
      toggle, AddInputEventCallback, LV_EVENT_VALUE_CHANGED, state);
  return toggle;
}

/**
 * @brief 清空自动发送设置页面保存的控件引用
 * @param state Radio 页面状态
 */
void ResetAutoSendReferences(RadioViewState* state) {
  if (state == nullptr) {
    return;
  }
  state->auto_send_page = nullptr;
  state->auto_send_body = nullptr;
  state->auto_send_switch = nullptr;
  state->auto_send_text_area = nullptr;
  state->auto_send_interval_area = nullptr;
  state->auto_send_action_area = nullptr;
  state->auto_send_keyboard = nullptr;
  state->auto_send_closing = false;
}

/**
 * @brief 处理自动发送设置页面退出动画完成事件
 * @param animation LVGL 动画对象
 */
void AutoSendCloseCompletedCallback(lv_anim_t* animation) {
  auto* state = static_cast<RadioViewState*>(
      lv_anim_get_user_data(animation));
  if (state == nullptr || state->auto_send_page == nullptr) {
    return;
  }
  lv_obj_t* page = state->auto_send_page;
  ResetAutoSendReferences(state);
  lv_obj_delete(page);
}

/**
 * @brief 关闭自动发送设置页面
 * @param state Radio 页面状态
 * @param animated 是否播放退出动画
 */
void CloseAutoSendSettingsPage(RadioViewState* state, bool animated) {
  if (state == nullptr || state->auto_send_page == nullptr ||
      state->auto_send_closing) {
    return;
  }
  HideSharedKeyboard(state->auto_send_keyboard);
  if (animated && StartSlideRightWindowTransition(
      state->auto_send_page, state->config.width, kAnimationMs,
      state, AutoSendCloseCompletedCallback)) {
    state->auto_send_closing = true;
    return;
  }
  lv_obj_t* page = state->auto_send_page;
  ResetAutoSendReferences(state);
  lv_obj_delete(page);
}

/**
 * @brief 处理自动发送设置页面返回按钮
 * @param event LVGL 事件对象
 */
void AutoSendBackClickedEventCallback(lv_event_t* event) {
  if (lv_event_get_code(event) == LV_EVENT_CLICKED) {
    CloseAutoSendSettingsPage(
        static_cast<RadioViewState*>(lv_event_get_user_data(event)), true);
  }
}

/**
 * @brief 调整自动发送页面的键盘、操作区和滚动区域
 * @param state Radio 页面状态
 * @param input 当前输入框
 * @param visible 是否显示键盘
 */
void SetAutoSendKeyboardVisible(
    RadioViewState* state, lv_obj_t* input, bool visible) {
  if (state == nullptr || state->auto_send_body == nullptr ||
      state->auto_send_action_area == nullptr) {
    return;
  }
  const bool input_active = visible;
  const bool keyboard_visible = input_active && ShouldShowSharedKeyboard();
  const int normal_height = state->config.height -
      kAddPageHeaderHeight - kAddPageActionHeight;
  if (!keyboard_visible) {
    if (!input_active) {
      HideSharedKeyboard(state->auto_send_keyboard);
    }
    lv_obj_align(
        state->auto_send_action_area, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_height(state->auto_send_body, normal_height);
    lv_obj_update_layout(state->auto_send_body);
    return;
  }
  const int keyboard_height =
      state->config.height * kAddKeyboardHeightPercent / 100;
  const int visible_height = state->config.height - keyboard_height -
      kAddPageHeaderHeight - kAddPageActionHeight - kAddKeyboardTopGap;
  if (input == nullptr || visible_height <= 0) {
    return;
  }
  lv_obj_align(state->auto_send_action_area, LV_ALIGN_BOTTOM_MID, 0,
      -keyboard_height - kAddKeyboardTopGap);
  lv_obj_set_height(state->auto_send_body, visible_height);
  lv_obj_update_layout(state->auto_send_body);
  int32_t scroll_y = static_cast<int32_t>(lv_obj_get_y(input)) - 18;
  if (scroll_y < 0) {
    scroll_y = 0;
  }
  lv_obj_scroll_to_y(
      state->auto_send_body, scroll_y, LV_ANIM_ON);
}

/**
 * @brief 处理自动发送文本和周期输入事件
 * @param event LVGL 事件对象
 */
void AutoSendInputEventCallback(lv_event_t* event) {
  auto* state = static_cast<RadioViewState*>(lv_event_get_user_data(event));
  const lv_event_code_t code = lv_event_get_code(event);
  if (code == LV_EVENT_FOCUSED) {
    SetAutoSendKeyboardVisible(
        state, lv_event_get_target_obj(event), true);
  } else if (code == LV_EVENT_CLICKED && state != nullptr &&
             state->auto_send_keyboard != nullptr &&
             lv_obj_has_flag(
                 state->auto_send_keyboard, LV_OBJ_FLAG_HIDDEN)) {
    SetAutoSendKeyboardVisible(
        state, lv_event_get_target_obj(event), true);
  } else if (code == LV_EVENT_READY || code == LV_EVENT_CANCEL) {
    SetAutoSendKeyboardVisible(state, nullptr, false);
  }
}

/**
 * @brief 处理自动发送页面空白区域点击并收起键盘
 * @param event LVGL 事件对象
 */
void AutoSendBackgroundClickedEventCallback(lv_event_t* event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED ||
      lv_event_get_target_obj(event) !=
          lv_event_get_current_target_obj(event)) {
    return;
  }
  auto* state = static_cast<RadioViewState*>(lv_event_get_user_data(event));
  SetAutoSendKeyboardVisible(state, nullptr, false);
}

/**
 * @brief 创建自动发送页面的单行输入框
 * @param parent 父对象
 * @param state Radio 页面状态
 * @param placeholder 占位文本
 * @param text 初始文本
 * @param y 顶部坐标
 * @param max_length 最大输入长度
 * @return 创建成功返回输入框，否则返回 nullptr
 */
lv_obj_t* CreateAutoSendTextArea(lv_obj_t* parent, RadioViewState* state,
    const char* placeholder, const char* text, int y, int max_length) {
  lv_obj_t* input = lv_textarea_create(parent);
  if (input == nullptr) {
    return nullptr;
  }
  lv_obj_add_flag(input, LV_OBJ_FLAG_GESTURE_BUBBLE);
  lv_obj_remove_flag(input, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
  lv_textarea_set_one_line(input, true);
  lv_obj_set_scrollbar_mode(input, LV_SCROLLBAR_MODE_OFF);
  lv_obj_set_size(input, state->config.width - 56, kAddInputHeight);
  lv_obj_set_pos(input, 28, y);
  lv_textarea_set_max_length(input, max_length);
  lv_textarea_set_placeholder_text(input, placeholder);
  lv_textarea_set_text(input, text);
  lv_obj_set_style_text_font(input, Font24(), LV_PART_MAIN);
  lv_obj_set_style_text_color(
      input, lv_color_hex(theme::ActiveThemeColors().on_surface), LV_PART_MAIN);
  lv_obj_set_style_bg_color(
      input, lv_color_hex(theme::ActiveThemeColors().surface_container_low), LV_PART_MAIN);
  lv_obj_set_style_bg_color(
      input, lv_color_hex(theme::ActiveThemeColors().surface_container_low), LV_STATE_FOCUSED);
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
      input, AutoSendInputEventCallback, LV_EVENT_ALL, state);
  return input;
}

/**
 * @brief 创建自动发送页面的启用开关卡片
 * @param parent 父对象
 * @param state Radio 页面状态
 * @param y 顶部坐标
 * @param checked 初始开关状态
 * @return 创建成功返回开关对象，否则返回 nullptr
 */
lv_obj_t* CreateAutoSendSwitch(
    lv_obj_t* parent, RadioViewState* state, int y, bool checked) {
  lv_obj_t* row = lv_obj_create(parent);
  if (row == nullptr) {
    return nullptr;
  }
  lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(row, LV_OBJ_FLAG_GESTURE_BUBBLE);
  lv_obj_set_size(row, state->config.width - 56, kAddSwitchRowHeight);
  lv_obj_set_pos(row, 28, y);
  lv_obj_set_style_bg_color(
      row, lv_color_hex(theme::ActiveThemeColors().surface_container_low), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(row, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_border_width(row, 0, LV_PART_MAIN);
  lv_obj_set_style_radius(row, 22, LV_PART_MAIN);
  lv_obj_set_style_pad_all(row, 0, LV_PART_MAIN);
  lv_obj_t* title = CreateLabel(
      row, "Automatic send", theme::ActiveThemeColors().on_surface, Font24());
  lv_obj_t* subtitle = CreateLabel(row,
      "Repeat while this profile is active",
      theme::ActiveThemeColors().on_surface_variant, Font22());
  lv_obj_t* toggle = lv_switch_create(row);
  if (title == nullptr || subtitle == nullptr || toggle == nullptr) {
    lv_obj_delete(row);
    return nullptr;
  }
  constexpr int kTitleHeight = 32;
  constexpr int kSubtitleHeight = 30;
  constexpr int kTextGap = 6;
  const int text_top = (kAddSwitchRowHeight - kTitleHeight -
      kTextGap - kSubtitleHeight) / 2;
  lv_obj_set_size(
      title, state->config.width - 190, kTitleHeight);
  lv_label_set_long_mode(title, LV_LABEL_LONG_DOT);
  lv_obj_set_pos(title, 20, text_top);
  lv_obj_set_size(
      subtitle, state->config.width - 190, kSubtitleHeight);
  lv_label_set_long_mode(subtitle, LV_LABEL_LONG_SCROLL_CIRCULAR);
  lv_obj_set_pos(
      subtitle, 20, text_top + kTitleHeight + kTextGap);
  lv_obj_add_flag(toggle, LV_OBJ_FLAG_GESTURE_BUBBLE);
  lv_obj_set_size(toggle, kProfileSwitchWidth, kProfileSwitchHeight);
  lv_obj_align(toggle, LV_ALIGN_RIGHT_MID, -18, 0);
  lv_obj_set_style_anim_duration(
      toggle, kProfileSwitchAnimationMs, LV_PART_MAIN);
  ApplyRadioSwitchTheme(toggle);
  if (checked) {
    lv_obj_add_state(toggle, LV_STATE_CHECKED);
  }
  return toggle;
}

/**
 * @brief 保存当前配置的自动发送参数
 * @param event LVGL 事件对象
 */
void AutoSendSaveClickedEventCallback(lv_event_t* event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
    return;
  }
  auto* state = static_cast<RadioViewState*>(lv_event_get_user_data(event));
  if (state == nullptr || state->auto_send_switch == nullptr ||
      state->auto_send_text_area == nullptr ||
      state->auto_send_interval_area == nullptr ||
      state->profile_settings_index >= state->module_count) {
    return;
  }
  const char* text = lv_textarea_get_text(state->auto_send_text_area);
  const char* interval_text =
      lv_textarea_get_text(state->auto_send_interval_area);
  char* interval_end = nullptr;
  const unsigned long interval = interval_text == nullptr
      ? 0
      : std::strtoul(interval_text, &interval_end, 10);
  const bool enabled = lv_obj_has_state(
      state->auto_send_switch, LV_STATE_CHECKED);
  if (text == nullptr || (enabled && text[0] == '\0') ||
      interval_text == nullptr || interval_text[0] == '\0' ||
      interval_end == interval_text || *interval_end != '\0' ||
      interval < app::kRadioAutoSendMinimumIntervalMs ||
      interval > app::kRadioAutoSendMaximumIntervalMs) {
    return;
  }
  app::RadioProfile& profile =
      state->preferences.profiles[state->profile_settings_index];
  profile.auto_send_enabled = enabled;
  CopyBoundedString(profile.auto_send_text,
      sizeof(profile.auto_send_text), text);
  profile.auto_send_interval_ms = static_cast<uint32_t>(interval);
  app::UpdateRadioPreferences(state->preferences);
  if (profile.active) {
    state->auto_send_last_ticks[state->profile_settings_index] = 0;
  }
  AppendSystemMessage(
      state, state->profile_settings_index, kSettingsChangedMessage);
  MarkModuleListDirty(state);
  CloseAutoSendSettingsPage(state, true);
}

/**
 * @brief 显示当前射频配置的自动发送设置页面
 * @param state Radio 页面状态
 * @return 显示成功返回 true，否则返回 false
 */
bool ShowAutoSendSettingsPage(RadioViewState* state) {
  if (state == nullptr || state->root == nullptr ||
      state->profile_settings_page == nullptr ||
      state->profile_settings_index >= state->module_count) {
    return false;
  }
  if (state->auto_send_page != nullptr) {
    lv_obj_move_to_index(state->auto_send_page, -1);
    return true;
  }
  const app::RadioProfile& profile =
      state->preferences.profiles[state->profile_settings_index];
  lv_obj_t* page = lv_obj_create(state->root);
  if (page == nullptr) {
    return false;
  }
  state->auto_send_page = page;
  state->auto_send_closing = false;
  lv_obj_remove_flag(page, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(page, LV_OBJ_FLAG_GESTURE_BUBBLE);
  lv_obj_set_size(page, state->config.width, state->config.height);
  lv_obj_set_pos(page, 0, 0);
  lv_obj_set_style_bg_color(
      page, lv_color_hex(theme::ActiveThemeColors().surface), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(page, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_border_width(page, 0, LV_PART_MAIN);
  lv_obj_set_style_radius(page, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(page, 0, LV_PART_MAIN);

  lv_obj_t* back = lv_button_create(page);
  if (back == nullptr) {
    CloseAutoSendSettingsPage(state, false);
    return false;
  }
  lv_obj_remove_style_all(back);
  lv_obj_add_flag(back, LV_OBJ_FLAG_GESTURE_BUBBLE);
  lv_obj_set_size(back, 62, 62);
  lv_obj_set_pos(back, 18, 66);
  lv_obj_add_event_cb(back, AutoSendBackClickedEventCallback,
      LV_EVENT_CLICKED, state);
  lv_obj_t* back_icon = CreateLabel(
      back, icon::kArrowBack, theme::ActiveThemeColors().on_surface, OutlineIconFont44());
  lv_obj_t* title = CreateLabel(
      page, "Automatic send", theme::ActiveThemeColors().on_surface, Font32());
  if (back_icon == nullptr || title == nullptr) {
    CloseAutoSendSettingsPage(state, false);
    return false;
  }
  lv_obj_align(back_icon, LV_ALIGN_CENTER, -4, 0);
  lv_obj_set_width(title, state->config.width);
  lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
  lv_obj_align(title, LV_ALIGN_TOP_MID, 0, kNavigationTitleTop);

  lv_obj_t* body = lv_obj_create(page);
  if (body == nullptr) {
    CloseAutoSendSettingsPage(state, false);
    return false;
  }
  state->auto_send_body = body;
  lv_obj_set_pos(body, 0, kNavigationBodyTop);
  lv_obj_set_size(body, state->config.width,
      state->config.height - kNavigationBodyTop - kAddPageActionHeight);
  lv_obj_set_style_bg_opa(body, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_set_style_border_width(body, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(body, 0, LV_PART_MAIN);
  lv_obj_set_scroll_dir(body, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(body, LV_SCROLLBAR_MODE_AUTO);
  lv_obj_add_flag(body, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(body, LV_OBJ_FLAG_GESTURE_BUBBLE);
  lv_obj_add_event_cb(body, AutoSendBackgroundClickedEventCallback,
      LV_EVENT_CLICKED, state);

  if (!CreateAddParameterTitle(body, "SEND TEXT", 8)) {
    CloseAutoSendSettingsPage(state, false);
    return false;
  }
  state->auto_send_text_area = CreateAutoSendTextArea(body, state,
      "Test characters", profile.auto_send_text, 44,
      app::kRadioAutoSendTextCapacity - 1);
  if (!CreateAddParameterTitle(body, "CYCLE INTERVAL (MS)", 140)) {
    CloseAutoSendSettingsPage(state, false);
    return false;
  }
  char interval_text[12] = {};
  std::snprintf(interval_text, sizeof(interval_text), "%lu",
      static_cast<unsigned long>(profile.auto_send_interval_ms));
  state->auto_send_interval_area = CreateAutoSendTextArea(body, state,
      "100 - 60000", interval_text, 176, 5);
  state->auto_send_switch = CreateAutoSendSwitch(
      body, state, 326, profile.auto_send_enabled);
  if (state->auto_send_switch == nullptr ||
      state->auto_send_text_area == nullptr ||
      state->auto_send_interval_area == nullptr) {
    CloseAutoSendSettingsPage(state, false);
    return false;
  }
  lv_textarea_set_accepted_chars(
      state->auto_send_interval_area, kIntegerAcceptedChars);
  lv_obj_t* interval_help = CreateLabel(body,
      "100-60000 ms; each cycle waits for the previous send to finish.",
      theme::ActiveThemeColors().outline, Font22());
  if (interval_help == nullptr) {
    CloseAutoSendSettingsPage(state, false);
    return false;
  }
  lv_obj_set_width(interval_help, state->config.width - 76);
  lv_label_set_long_mode(interval_help, LV_LABEL_LONG_WRAP);
  lv_obj_set_pos(interval_help, 38, 258);

  lv_obj_t* action_area = lv_obj_create(page);
  lv_obj_t* save = action_area == nullptr
      ? nullptr
      : lv_button_create(action_area);
  if (action_area == nullptr || save == nullptr) {
    CloseAutoSendSettingsPage(state, false);
    return false;
  }
  state->auto_send_action_area = action_area;
  lv_obj_remove_flag(action_area, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(action_area, state->config.width, kAddPageActionHeight);
  lv_obj_align(action_area, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_obj_set_style_bg_opa(action_area, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_set_style_border_width(action_area, 0, LV_PART_MAIN);
  lv_obj_set_style_radius(action_area, 0, LV_PART_MAIN);
  lv_obj_set_style_shadow_width(action_area, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(action_area, 0, LV_PART_MAIN);
  lv_obj_set_size(save, state->config.width - 96, 84);
  lv_obj_align(save, LV_ALIGN_CENTER, 0, 0);
  lv_obj_set_style_radius(save, 42, LV_PART_MAIN);
  lv_obj_set_style_bg_color(
      save, lv_color_hex(kPrimaryColor), LV_PART_MAIN);
  lv_obj_set_style_bg_color(
      save, lv_color_hex(kPrimaryPressedColor), LV_STATE_PRESSED);
  lv_obj_set_style_border_width(save, 0, LV_PART_MAIN);
  lv_obj_set_style_shadow_width(save, 0, LV_PART_MAIN);
  lv_obj_add_event_cb(save, AutoSendSaveClickedEventCallback,
      LV_EVENT_CLICKED, state);
  lv_obj_t* save_label = CreateLabel(
      save, "Save settings", kOnPrimaryColor, Font28());
  if (save_label == nullptr) {
    CloseAutoSendSettingsPage(state, false);
    return false;
  }
  lv_obj_center(save_label);

  SharedKeyboardConfig keyboard_config;
  keyboard_config.width = state->config.width;
  keyboard_config.height =
      state->config.height * kAddKeyboardHeightPercent / 100;
  state->auto_send_keyboard =
      CreateSharedKeyboard(page, keyboard_config);
  if (state->auto_send_keyboard == nullptr ||
      !AttachSharedKeyboardToTextArea(state->auto_send_keyboard,
          state->auto_send_text_area, nullptr) ||
      !AttachSharedKeyboardToTextArea(state->auto_send_keyboard,
          state->auto_send_interval_area, kIntegerAcceptedChars)) {
    CloseAutoSendSettingsPage(state, false);
    return false;
  }
  lv_obj_add_flag(
      state->auto_send_keyboard, LV_OBJ_FLAG_GESTURE_BUBBLE);
  if (!StartSlideLeftWindowTransition(page, state->config.width,
      kAnimationMs, state, nullptr)) {
    CloseAutoSendSettingsPage(state, false);
    return false;
  }
  if (!RegisterBackNavigationHandler(page, [state]() {
        CloseAutoSendSettingsPage(state, true);
      })) {
    CloseAutoSendSettingsPage(state, false);
    return false;
  }
  return true;
}

bool ParseTextAreaUint64(lv_obj_t* input, int base, uint64_t minimum,
    uint64_t maximum, uint64_t* value) {
  if (input == nullptr || value == nullptr) {
    return false;
  }
  const char* text = lv_textarea_get_text(input);
  if (text == nullptr || text[0] == '\0') {
    return false;
  }
  char* end = nullptr;
  const unsigned long long parsed = std::strtoull(text, &end, base);
  if (end == nullptr || end[0] != '\0' || parsed < minimum ||
      parsed > maximum) {
    return false;
  }
  *value = static_cast<uint64_t>(parsed);
  return true;
}

/**
 * @brief 校验 CC1101 GFSK 配置表单
 * @param state Radio 页面状态
 * @return 所有参数有效时返回 true
 */
bool IsAddGfskFormComplete(const RadioViewState* state) {
  long data_rate = 0;
  long value = 0;
  return IsAddFrequencyValid(state) && IsAddOutputPowerValid(state) &&
         ParseTextAreaLong(
             state->add_data_rate_input, 10, 600, 250000, &data_rate) &&
         ParseTextAreaLong(state->add_frequency_deviation_input,
             10, 1600, 380000, &value) &&
         state->selected_add_receive_bandwidth >= 0 &&
         static_cast<size_t>(state->selected_add_receive_bandwidth) <
             std::size(radio::kCc1101ReceiveBandwidthsHz) &&
         state->selected_add_preamble >= 0 &&
         static_cast<size_t>(state->selected_add_preamble) <
             std::size(kCc1101PreambleLengths) &&
         ParseTextAreaLong(
             state->add_sync_word_input, 16, 0, 65535, &value) &&
         state->add_crc_switch != nullptr &&
         state->add_whitening_switch != nullptr;
}

/**
 * @brief 校验 nRF24L01 Enhanced ShockBurst 配置表单
 * @param state Radio 页面状态
 * @return 所有参数有效时返回 true
 */
bool IsAddEnhancedShockBurstFormComplete(const RadioViewState* state) {
  if (state == nullptr) {
    return false;
  }
  long retransmit_count = 0;
  long retransmit_delay = 0;
  long value = 0;
  uint64_t address = 0;
  uint8_t address_width = 0;
  if (!IsAddOutputPowerValid(state) ||
      !ParseTextAreaLong(
          state->add_frequency_input, 10, 2400, 2525, &value) ||
      state->selected_add_data_rate < 0 ||
      static_cast<size_t>(state->selected_add_data_rate) >=
          std::size(kNrf24l01DataRates) ||
      !ParseEnhancedShockBurstAddress(
          state->add_address_input, &address, &address_width) ||
      !ParseTextAreaLong(
          state->add_retransmit_count_input, 10, 0, 15,
          &retransmit_count) ||
      !ParseTextAreaLong(state->add_retransmit_delay_input,
          10, 250, 4000, &retransmit_delay) ||
      retransmit_delay % 250 != 0 ||
      state->add_auto_ack_switch == nullptr ||
      state->add_dynamic_payload_switch == nullptr) {
    return false;
  }
  const bool auto_ack_enabled = lv_obj_has_state(
      state->add_auto_ack_switch, LV_STATE_CHECKED);
  const bool dynamic_payload_enabled = lv_obj_has_state(
      state->add_dynamic_payload_switch, LV_STATE_CHECKED);
  return (!dynamic_payload_enabled || auto_ack_enabled) &&
         (kNrf24l01DataRates[state->selected_add_data_rate] != 250000 ||
             !auto_ack_enabled ||
             retransmit_count == 0 || retransmit_delay >= 500);
}

/**
 * @brief 根据芯片和协议能力应用默认射频参数
 * @param capability 当前选择的射频能力
 * @param profile 待更新配置
 */
void ApplyRadioCapabilityDefaults(const hal::RadioCapability& capability,
    app::RadioProfile* profile) {
  if (profile == nullptr) {
    return;
  }
  profile->chip = capability.chip;
  profile->protocol = capability.protocol;
  profile->antenna = radio::AntennaType::kInternal;
  if (capability.protocol == radio::ProtocolType::kGfsk) {
    profile->frequency_hz = 868000000;
    profile->output_power_dbm = 10;
    profile->preamble_length = 32;
    profile->crc_enabled = true;
    profile->gfsk_data_rate_bps = 4800;
    profile->gfsk_frequency_deviation_hz = 5000;
    profile->gfsk_receive_bandwidth_hz =
        radio::kCc1101ReceiveBandwidthsHz[0];
    profile->gfsk_sync_word = 0x12AD;
    profile->gfsk_whitening_enabled = false;
    profile->gfsk_fec_enabled = false;
  } else if (capability.protocol ==
             radio::ProtocolType::kEnhancedShockBurst) {
    profile->frequency_hz = 2400000000U;
    profile->output_power_dbm = 0;
    profile->esb_channel = 0;
    profile->esb_data_rate_bps = 250000;
    profile->esb_address = 0xE7E7E7E7E7ULL;
    profile->esb_address_width = 5;
    profile->esb_crc_length_bits = 16;
    profile->esb_retransmit_count = 0;
    profile->esb_retransmit_delay_us = 750;
    profile->esb_auto_ack_enabled = false;
    profile->esb_dynamic_payload_enabled = false;
  } else {
    profile->frequency_hz = 868000000U;
    profile->bandwidth_hz = 125000U;
    profile->preamble_length = 8;
    profile->spreading_factor = 7;
    profile->coding_rate_denominator = 5;
    profile->lr2021_coding_rate =
        radio::Lr2021CodingRate::kStandard4_5;
    profile->sync_word = 0x12;
    profile->output_power_dbm = 22;
    profile->crc_enabled = true;
    profile->invert_iq = false;
    profile->rx_boosted = true;
    profile->lr2021_rx_boost_mode = 7;
  }
}

/**
 * @brief 创建芯片和协议选择区域
 * @param body 表单滚动区域
 * @param state Radio 页面状态
 * @param profile 当前表单配置
 * @param content_offset 新建配置使用的纵向偏移
 * @return 创建成功返回 true
 */
bool CreateAddChipAndProtocolOptions(lv_obj_t* body, RadioViewState* state,
    const app::RadioProfile& profile, int content_offset) {
  if (!CreateAddParameterTitle(body, "RADIO CHIP", 8 + content_offset)) {
    return false;
  }
  constexpr int kOptionGap = 10;
  const int option_area_width = state->config.width - 56;
  const size_t capability_count = state->capabilities.count;
  const int chip_width = capability_count == 0
      ? option_area_width
      : (option_area_width -
            static_cast<int>(capability_count - 1) * kOptionGap) /
            static_cast<int>(capability_count);
  for (size_t index = 0; index < capability_count; ++index) {
    const char* chip_name =
        ChipDisplayName(state->capabilities.entries[index].chip);
    int button_width = chip_width;
    if (capability_count == 1) {
      lv_point_t text_size = {};
      lv_text_get_size(&text_size, chip_name, Font22(), 0, 0,
          LV_COORD_MAX, LV_TEXT_FLAG_EXPAND);
      constexpr int kHorizontalPadding = 28;
      button_width = std::min(option_area_width,
          static_cast<int>(text_size.x) + 2 * kHorizontalPadding);
    }
    state->add_chip_buttons[index] = CreateAddOptionButton(body, state,
        RadioAddOptionGroup::kChip, static_cast<int>(index),
        chip_name,
        28 + static_cast<int>(index) * (chip_width + kOptionGap),
        44 + content_offset, button_width, 62);
    if (state->add_chip_buttons[index] == nullptr) {
      return false;
    }
  }
  if (!CreateAddParameterTitle(body, "PROTOCOL", 134 + content_offset)) {
    return false;
  }
  state->add_protocol_buttons[0] = CreateAddOptionButton(body, state,
      RadioAddOptionGroup::kProtocol, 0,
      ProtocolDisplayName(profile.protocol), 28, 170 + content_offset,
      profile.protocol == radio::ProtocolType::kEnhancedShockBurst
          ? 280
          : 140,
      62);
  return state->add_protocol_buttons[0] != nullptr;
}

/**
 * @brief 创建 GFSK 或 Enhanced ShockBurst 参数表单
 * @param state Radio 页面状态
 * @param profile 当前表单配置
 * @return 创建成功返回 true
 */
bool CreateNonLoraAddModuleContent(RadioViewState* state,
    const app::RadioProfile& profile) {
  lv_obj_t* body = state->add_body;
  const bool editing = state->editing_index < state->module_count;
  const int content_offset = editing ? 0 : kAddProfileNameSectionHeight;
  if (body == nullptr) {
    return false;
  }
  state->add_name_input = nullptr;
  if (!editing) {
    if (!CreateAddParameterTitle(body, "PROFILE NAME", 8)) {
      return false;
    }
    state->add_name_input = CreateAddTextArea(body, state,
        "Profile name", "", 44, app::kRadioProfileNameCapacity - 1);
    if (state->add_name_input == nullptr) {
      return false;
    }
    lv_textarea_set_accepted_chars(
        state->add_name_input, kProfileNameAcceptedChars);
  }
  if (!CreateAddChipAndProtocolOptions(
          body, state, profile, content_offset)) {
    return false;
  }

  char value[24] = {};
  constexpr int kFirstTitleY = 262;
  constexpr int kRowPitch = 138;
  constexpr int kSwitchPitch = kAddSwitchRowHeight + kAddSwitchRowGap;
  int row = 0;
  int additional_content_height = 0;
  const auto create_integer_input = [&](const char* title,
                                        const char* placeholder,
                                        uint64_t number,
                                        int maximum_length) -> lv_obj_t* {
    const int title_y = kFirstTitleY + row * kRowPitch +
        additional_content_height + content_offset;
    ++row;
    std::snprintf(value, sizeof(value), "%llu",
        static_cast<unsigned long long>(number));
    if (!CreateAddParameterTitle(body, title, title_y)) {
      return nullptr;
    }
    lv_obj_t* input = CreateAddTextArea(body, state, placeholder, value,
        title_y + 36, maximum_length);
    if (input != nullptr) {
      lv_textarea_set_accepted_chars(input, kIntegerAcceptedChars);
    }
    return input;
  };
  const auto create_option_row = [&](const char* title,
                                     RadioAddOptionGroup group,
                                     lv_obj_t** buttons,
                                     const char* const* names,
                                     size_t count,
                                     size_t column_count) -> bool {
    if (buttons == nullptr || names == nullptr || count == 0 ||
        column_count == 0) {
      return false;
    }
    const int title_y = kFirstTitleY + row * kRowPitch +
        additional_content_height + content_offset;
    ++row;
    if (!CreateAddParameterTitle(body, title, title_y)) {
      return false;
    }
    constexpr int kOptionLeft = 28;
    constexpr int kOptionGap = 10;
    const int option_area_width = state->config.width - 56;
    const size_t visible_column_count = std::min(count, column_count);
    const int option_width = (option_area_width -
        static_cast<int>(visible_column_count - 1) * kOptionGap) /
        static_cast<int>(visible_column_count);
    for (size_t index = 0; index < count; ++index) {
      const size_t option_row = index / column_count;
      const size_t option_column = index % column_count;
      buttons[index] = CreateAddOptionButton(body, state, group,
          static_cast<int>(index), names[index],
          kOptionLeft + static_cast<int>(option_column) *
              (option_width + kOptionGap),
          title_y + 36 + static_cast<int>(option_row) * 68,
          option_width, 62);
      if (buttons[index] == nullptr) {
        return false;
      }
    }
    const size_t option_row_count =
        (count + column_count - 1) / column_count;
    additional_content_height +=
        static_cast<int>(option_row_count - 1) * 68;
    return true;
  };
  state->add_active_switch = nullptr;
  const auto create_active_switch = [&](int y) -> bool {
    if (editing) {
      return true;
    }
    state->add_active_switch = CreateAddSwitchRow(body, state,
        "Active profile", "Only one profile can use the radio chip",
        y, true);
    return state->add_active_switch != nullptr;
  };

  if (profile.protocol == radio::ProtocolType::kGfsk) {
    std::snprintf(value, sizeof(value), "%.6f",
        static_cast<double>(profile.frequency_hz) / 1000000.0);
    if (!CreateAddParameterTitle(body, "WORKING FREQUENCY (MHz)",
            kFirstTitleY + content_offset)) {
      return false;
    }
    state->add_frequency_input = CreateAddTextArea(body, state,
        "Frequency", value, kFirstTitleY + 36 + content_offset,
        kFrequencyInputMaximumLength);
    ++row;
    if (state->add_frequency_input == nullptr) {
      return false;
    }
    lv_textarea_set_accepted_chars(
        state->add_frequency_input, kFrequencyAcceptedChars);
    state->add_data_rate_input = create_integer_input(
        "DATA RATE (bit/s)", "4800", profile.gfsk_data_rate_bps, 6);
    state->add_frequency_deviation_input = create_integer_input(
        "FREQUENCY DEVIATION (Hz)", "5000",
        profile.gfsk_frequency_deviation_hz, 6);
    state->selected_add_receive_bandwidth = 0;
    uint32_t smallest_bandwidth_difference = UINT32_MAX;
    for (size_t index = 0;
         index < std::size(radio::kCc1101ReceiveBandwidthsHz); ++index) {
      const uint32_t bandwidth = radio::kCc1101ReceiveBandwidthsHz[index];
      const uint32_t difference = profile.gfsk_receive_bandwidth_hz > bandwidth
          ? profile.gfsk_receive_bandwidth_hz - bandwidth
          : bandwidth - profile.gfsk_receive_bandwidth_hz;
      if (difference < smallest_bandwidth_difference) {
        smallest_bandwidth_difference = difference;
        state->selected_add_receive_bandwidth = static_cast<int>(index);
      }
    }
    if (!create_option_row("RECEIVE BANDWIDTH (kHz)",
            RadioAddOptionGroup::kReceiveBandwidth,
            state->add_receive_bandwidth_buttons,
            kCc1101ReceiveBandwidthNames,
            std::size(kCc1101ReceiveBandwidthNames), 4)) {
      return false;
    }
    state->selected_add_output_power =
        static_cast<int>(std::size(kCc1101OutputPowers) - 1);
    for (size_t index = 0; index < std::size(kCc1101OutputPowers); ++index) {
      if (profile.output_power_dbm == kCc1101OutputPowers[index]) {
        state->selected_add_output_power = static_cast<int>(index);
        break;
      }
    }
    if (!create_option_row("TX POWER (dBm)",
            RadioAddOptionGroup::kOutputPower,
            state->add_output_power_buttons,
            kCc1101OutputPowerNames,
            std::size(kCc1101OutputPowerNames), 4)) {
      return false;
    }
    state->selected_add_preamble = 2;
    for (size_t index = 0;
         index < std::size(kCc1101PreambleLengths); ++index) {
      if (profile.preamble_length == kCc1101PreambleLengths[index]) {
        state->selected_add_preamble = static_cast<int>(index);
        break;
      }
    }
    state->add_preamble_input = nullptr;
    if (!create_option_row("PREAMBLE LENGTH (bit)",
            RadioAddOptionGroup::kPreamble,
            state->add_preamble_buttons,
            kCc1101PreambleLengthNames,
            std::size(kCc1101PreambleLengthNames), 4)) {
      return false;
    }
    const int sync_title_y =
        kFirstTitleY + row * kRowPitch + additional_content_height +
        content_offset;
    ++row;
    std::snprintf(value, sizeof(value), "%04X", profile.gfsk_sync_word);
    if (!CreateAddParameterTitle(body, "SYNC WORD (HEX)", sync_title_y)) {
      return false;
    }
    state->add_sync_word_input = CreateAddTextArea(body, state,
        "12AD", value, sync_title_y + 36, 4);
    if (state->add_sync_word_input != nullptr) {
      lv_textarea_set_accepted_chars(
          state->add_sync_word_input, kHexAcceptedChars);
    }
    if (!CreateAddHexPrefix(body, state, state->add_sync_word_input,
            sync_title_y + 36)) {
      return false;
    }
    const int switches_y =
        kFirstTitleY + row * kRowPitch + additional_content_height +
        content_offset;
    int switch_row = 0;
    if (!create_active_switch(switches_y)) {
      return false;
    }
    if (!editing) {
      ++switch_row;
    }
    state->add_crc_switch = CreateAddSwitchRow(body, state,
        "CRC", "Reject damaged GFSK packets",
        switches_y + switch_row++ * kSwitchPitch,
        profile.crc_enabled);
    state->add_whitening_switch = CreateAddSwitchRow(body, state,
        "Data whitening", "Enable only when the peer also uses whitening",
        switches_y + switch_row * kSwitchPitch,
        profile.gfsk_whitening_enabled);
    // FEC 属于少用高级参数，保留底层配置和存储兼容但不占用常用界面。
    state->add_fec_switch = nullptr;
  } else {
    state->add_frequency_input = create_integer_input(
        "WORKING FREQUENCY (MHz)", "2400",
        2400U + profile.esb_channel, 4);
    state->selected_add_data_rate = 0;
    for (size_t index = 0; index < std::size(kNrf24l01DataRates); ++index) {
      if (profile.esb_data_rate_bps == kNrf24l01DataRates[index]) {
        state->selected_add_data_rate = static_cast<int>(index);
        break;
      }
    }
    state->add_data_rate_input = nullptr;
    if (!create_option_row("DATA RATE",
            RadioAddOptionGroup::kDataRate,
            state->add_data_rate_buttons,
            kNrf24l01DataRateNames,
            std::size(kNrf24l01DataRateNames), 3)) {
      return false;
    }
    const int address_title_y =
        kFirstTitleY + row * kRowPitch + additional_content_height +
        content_offset;
    ++row;
    const int address_hex_digits =
        std::clamp<int>(profile.esb_address_width, 3, 5) * 2;
    std::snprintf(value, sizeof(value), "%0*llX", address_hex_digits,
        static_cast<unsigned long long>(profile.esb_address));
    if (!CreateAddParameterTitle(body, "ADDRESS (HEX)", address_title_y)) {
      return false;
    }
    state->add_address_input = CreateAddTextArea(body, state,
        "E7E7E7E7E7", value, address_title_y + 36, 10);
    if (state->add_address_input != nullptr) {
      lv_textarea_set_accepted_chars(
          state->add_address_input, kHexAcceptedChars);
    }
    if (!CreateAddHexPrefix(body, state, state->add_address_input,
            address_title_y + 36)) {
      return false;
    }
    // 地址宽度由输入的 6、8 或 10 位十六进制字符自动确定。
    // CRC 长度继续使用 Enhanced ShockBurst 常用默认值。
    state->add_address_width_input = nullptr;
    state->selected_add_output_power =
        static_cast<int>(std::size(kNrf24l01OutputPowers) - 1);
    for (size_t index = 0; index < std::size(kNrf24l01OutputPowers); ++index) {
      if (profile.output_power_dbm == kNrf24l01OutputPowers[index]) {
        state->selected_add_output_power = static_cast<int>(index);
        break;
      }
    }
    if (!create_option_row("TX POWER (dBm)",
            RadioAddOptionGroup::kOutputPower,
            state->add_output_power_buttons,
            kNrf24l01OutputPowerNames,
            std::size(kNrf24l01OutputPowerNames), 4)) {
      return false;
    }
    state->add_crc_length_input = nullptr;
    state->add_retransmit_count_input = create_integer_input(
        "RETRANSMIT COUNT", "3", profile.esb_retransmit_count, 2);
    state->add_retransmit_delay_input = create_integer_input(
        "RETRANSMIT DELAY (us)", "750",
        profile.esb_retransmit_delay_us, 4);
    const int switches_y =
        kFirstTitleY + row * kRowPitch + additional_content_height +
        content_offset;
    int switch_row = 0;
    if (!create_active_switch(switches_y)) {
      return false;
    }
    if (!editing) {
      ++switch_row;
    }
    state->add_auto_ack_switch = CreateAddSwitchRow(body, state,
        "Auto acknowledgment", "Use Enhanced ShockBurst ACK and retries",
        switches_y + switch_row++ * kSwitchPitch,
        profile.esb_auto_ack_enabled);
    state->add_dynamic_payload_switch = CreateAddSwitchRow(body, state,
        "Dynamic payload", "Transmit only the bytes used by each message",
        switches_y + switch_row * kSwitchPitch,
        profile.esb_dynamic_payload_enabled);
    UpdateEnhancedShockBurstDependencyControls(state);
  }
  UpdateAddOptionSelection(state);
  const bool common_ready = IsAddOutputPowerValid(state) &&
      (editing || state->add_active_switch != nullptr);
  if (profile.protocol == radio::ProtocolType::kGfsk) {
    return common_ready && state->add_frequency_input != nullptr &&
        state->add_data_rate_input != nullptr &&
        state->add_frequency_deviation_input != nullptr &&
        state->add_sync_word_input != nullptr &&
        state->add_crc_switch != nullptr &&
        state->add_whitening_switch != nullptr;
  }
  return common_ready && state->add_frequency_input != nullptr &&
      state->add_address_input != nullptr &&
      state->add_retransmit_count_input != nullptr &&
      state->add_retransmit_delay_input != nullptr &&
      state->add_auto_ack_switch != nullptr &&
      state->add_dynamic_payload_switch != nullptr;
}

/**
 * @brief 创建添加模块页面的参数内容
 * @param state 射频页面状态
 * @return 创建成功返回 true，否则返回 false
 */
bool CreateAddModuleContent(RadioViewState* state) {
  lv_obj_t* body = state->add_body;
  const bool editing = state->editing_index < state->module_count;
  const int content_offset = editing ? 0 : kAddProfileNameSectionHeight;
  app::RadioProfile profile = editing
      ? state->preferences.profiles[state->editing_index]
      : app::RadioProfile{};
  const hal::RadioCapability* capability = PrimaryRadioCapability(state);
  if (capability == nullptr) {
    return false;
  }
  if (!editing || profile.chip != capability->chip ||
      profile.protocol != capability->protocol) {
    ApplyRadioCapabilityDefaults(*capability, &profile);
  }
  if (profile.protocol != radio::ProtocolType::kLora) {
    return CreateNonLoraAddModuleContent(state, profile);
  }
  char frequency[16] = {};
  char power[8] = {};
  char preamble[12] = {};
  char sync_word[3] = {};
  std::snprintf(frequency, sizeof(frequency), "%.6f",
      static_cast<double>(profile.frequency_hz) / 1000000.0);
  std::snprintf(power, sizeof(power), "%d",
      static_cast<int>(profile.output_power_dbm));
  std::snprintf(preamble, sizeof(preamble), "%u",
      static_cast<unsigned>(profile.preamble_length));
  std::snprintf(sync_word, sizeof(sync_word), "%02hhX",
      static_cast<unsigned>(profile.sync_word));
  if (body == nullptr) {
    return false;
  }
  state->add_name_input = nullptr;
  if (!editing) {
    if (!CreateAddParameterTitle(body, "PROFILE NAME", 8)) {
      return false;
    }
    state->add_name_input = CreateAddTextArea(body, state,
        "Profile name", "", 44, app::kRadioProfileNameCapacity - 1);
    if (state->add_name_input == nullptr) {
      return false;
    }
    lv_textarea_set_accepted_chars(
        state->add_name_input, kProfileNameAcceptedChars);
  }
  const int option_gap = 10;
  const int option_area_width = state->config.width - 56;
  if (!CreateAddChipAndProtocolOptions(
          body, state, profile, content_offset)) {
    return false;
  }

  if (!CreateAddParameterTitle(
      body, "WORKING FREQUENCY", 262 + content_offset)) {
    return false;
  }
  state->add_frequency_input = CreateAddTextArea(
      body, state, "Frequency", frequency, 298 + content_offset,
      kFrequencyInputMaximumLength);
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
      302 + content_offset);
  lv_obj_set_style_bg_color(
      unit, lv_color_hex(theme::ActiveThemeColors().surface_container_high), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(unit, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_border_width(unit, 0, LV_PART_MAIN);
  lv_obj_set_style_radius(unit, 22, LV_PART_MAIN);
  lv_obj_set_style_pad_all(unit, 0, LV_PART_MAIN);
  lv_obj_t* unit_label = CreateLabel(
      unit, "MHz", theme::ActiveThemeColors().on_surface_variant, Font22());
  if (unit_label == nullptr) {
    return false;
  }
  lv_obj_center(unit_label);

  if (!CreateAddParameterTitle(
      body, "SPREADING FACTOR", 400 + content_offset)) {
    return false;
  }
  const int option_width = (option_area_width - 3 * option_gap) / 4;
  const char* sf_names[] = {
      "5", "6", "7", "8", "9", "10", "11", "12"};
  for (int index = 0; index < 8; ++index) {
    const int column = index % 4;
    const int row = index / 4;
    state->add_sf_buttons[index] = CreateAddOptionButton(body, state,
        RadioAddOptionGroup::kSpreadingFactor, index, sf_names[index],
        28 + column * (option_width + option_gap),
        436 + content_offset + row * 68, option_width, 58);
    if (state->add_sf_buttons[index] == nullptr) {
      return false;
    }
  }

  if (!CreateAddParameterTitle(
      body, "BANDWIDTH (kHz)", 582 + content_offset)) {
    return false;
  }
  const bool lr2021 = profile.chip == radio::ChipType::kLr2021;
  const size_t bandwidth_count = AddProfileBandwidthCount(state);
  const size_t bandwidth_column_count = lr2021 ? 4 : bandwidth_count;
  const int bandwidth_option_width =
      (option_area_width -
          static_cast<int>(bandwidth_column_count - 1) * option_gap) /
      static_cast<int>(bandwidth_column_count);
  for (size_t index = 0; index < bandwidth_count; ++index) {
    const size_t column = index % bandwidth_column_count;
    const size_t row = index / bandwidth_column_count;
    state->add_bandwidth_buttons[index] = CreateAddOptionButton(
        body, state, RadioAddOptionGroup::kBandwidth,
        static_cast<int>(index), AddProfileBandwidthName(state, index),
        28 + static_cast<int>(column) *
            (bandwidth_option_width + option_gap),
        618 + content_offset + static_cast<int>(row) * 70,
        bandwidth_option_width, 60);
    if (state->add_bandwidth_buttons[index] == nullptr) {
      return false;
    }
  }

  const int lr2021_bandwidth_extra = lr2021 ? 140 : 0;
  const int coding_title_y =
      710 + content_offset + lr2021_bandwidth_extra;
  if (!CreateAddParameterTitle(body, "CODING RATE", coding_title_y)) {
    return false;
  }
  const size_t coding_rate_count = AddProfileCodingRateCount(state);
  const size_t coding_column_count = lr2021 ? 3 : coding_rate_count;
  const int coding_option_width =
      (option_area_width -
          static_cast<int>(coding_column_count - 1) * option_gap) /
      static_cast<int>(coding_column_count);
  for (size_t index = 0; index < coding_rate_count; ++index) {
    const size_t column = index % coding_column_count;
    const size_t row = index / coding_column_count;
    state->add_coding_rate_buttons[index] = CreateAddOptionButton(
        body, state, RadioAddOptionGroup::kCodingRate,
        static_cast<int>(index), AddProfileCodingRateName(state, index),
        28 + static_cast<int>(column) *
            (coding_option_width + option_gap),
        coding_title_y + 36 + static_cast<int>(row) * 70,
        coding_option_width, 60);
    if (state->add_coding_rate_buttons[index] == nullptr) {
      return false;
    }
  }

  const int lr2021_coding_extra = lr2021 ? 140 : 0;
  const int lr2021_option_extra =
      lr2021_bandwidth_extra + lr2021_coding_extra;
  constexpr int kLr2021RxBoostSectionHeight = 198;
  state->add_rx_boost_switch = nullptr;
  if (lr2021) {
    const int rx_boost_title_y =
        838 + content_offset + lr2021_option_extra;
    if (!CreateAddParameterTitle(body, "RX BOOST MODE", rx_boost_title_y)) {
      return false;
    }
    const int rx_boost_option_width =
        (option_area_width - 3 * option_gap) / 4;
    for (int index = 0; index < 8; ++index) {
      const int column = index % 4;
      const int row = index / 4;
      state->add_rx_boost_buttons[index] = CreateAddOptionButton(
          body, state, RadioAddOptionGroup::kRxBoost, index,
          kLr2021RxBoostModeNames[index],
          28 + column * (rx_boost_option_width + option_gap),
          rx_boost_title_y + 36 + row * 70, rx_boost_option_width, 60);
      if (state->add_rx_boost_buttons[index] == nullptr) {
        return false;
      }
    }
  }
  const int following_content_extra =
      lr2021_option_extra +
      (lr2021 ? kLr2021RxBoostSectionHeight : 0);
  if (!CreateAddParameterTitle(
      body, "TX POWER", 838 + content_offset + following_content_extra)) {
    return false;
  }
  state->add_power_input = CreateAddTextArea(
      body, state, "Output power", power,
      874 + content_offset + following_content_extra, 3);
  if (state->add_power_input == nullptr) {
    return false;
  }
  lv_textarea_set_accepted_chars(
      state->add_power_input, "-0123456789");

  if (!CreateAddParameterTitle(
      body, "PREAMBLE LENGTH",
      976 + content_offset + following_content_extra)) {
    return false;
  }
  state->add_preamble_input = CreateAddTextArea(
      body, state, "Preamble symbols", preamble,
      1012 + content_offset + following_content_extra, 5);
  if (state->add_preamble_input == nullptr) {
    return false;
  }
  lv_textarea_set_accepted_chars(
      state->add_preamble_input, kIntegerAcceptedChars);

  if (!CreateAddParameterTitle(
      body, "SYNC WORD (HEX)",
      1114 + content_offset + following_content_extra)) {
    return false;
  }
  state->add_sync_word_input = CreateAddTextArea(
      body, state, "12", sync_word,
      1150 + content_offset + following_content_extra, 2);
  if (state->add_sync_word_input == nullptr) {
    return false;
  }
  lv_textarea_set_accepted_chars(state->add_sync_word_input,
      kHexAcceptedChars);
  if (!CreateAddHexPrefix(body, state, state->add_sync_word_input,
          1150 + content_offset + following_content_extra)) {
    return false;
  }

  const int switch_rows_top =
      1258 + content_offset + following_content_extra;
  constexpr int kSwitchRowPitch =
      kAddSwitchRowHeight + kAddSwitchRowGap;
  int switch_row_index = 0;
  state->add_active_switch = nullptr;
  if (!editing) {
    state->add_active_switch = CreateAddSwitchRow(body, state,
        "Active profile",
        "Only one profile can use the radio chip",
        switch_rows_top + switch_row_index++ * kSwitchRowPitch, true);
  }
  state->add_external_antenna_switch = nullptr;
  if (state->capabilities.supports_external_antenna) {
    state->add_external_antenna_switch = CreateAddSwitchRow(body, state,
        "External antenna",
        "Enabling without an external antenna may cause permanent damage",
        switch_rows_top + switch_row_index * kSwitchRowPitch,
        profile.antenna == radio::AntennaType::kExternal);
    ++switch_row_index;
  }
  state->add_crc_switch = CreateAddSwitchRow(body, state,
      "CRC", "Reject damaged LoRa packets",
      switch_rows_top + switch_row_index++ * kSwitchRowPitch,
      profile.crc_enabled);
  state->add_iq_switch = CreateAddSwitchRow(body, state,
      "Invert IQ", "Enable only when the peer also inverts IQ",
      switch_rows_top + switch_row_index++ * kSwitchRowPitch,
      profile.invert_iq);
  if (!lr2021) {
    state->add_rx_boost_switch = CreateAddSwitchRow(body, state,
        "RX boost", "Higher receive sensitivity",
        switch_rows_top + switch_row_index++ * kSwitchRowPitch,
        profile.rx_boosted);
  }
  if (state->add_crc_switch == nullptr ||
      state->add_iq_switch == nullptr ||
      (!lr2021 && state->add_rx_boost_switch == nullptr) ||
      (state->capabilities.supports_external_antenna &&
          state->add_external_antenna_switch == nullptr) ||
      (!editing && state->add_active_switch == nullptr)) {
    return false;
  }
  NormalizeAddBandwidthSelection(state);
  UpdateAddBandwidthOptionLayout(state);
  UpdateAddOptionSelection(state);
  return true;
}

/**
 * @brief 清空添加模块表单中的控件引用
 * @param state Radio 页面状态
 */
void ResetAddModuleFormPointers(RadioViewState* state) {
  state->add_name_input = nullptr;
  state->add_frequency_input = nullptr;
  state->add_power_input = nullptr;
  state->add_preamble_input = nullptr;
  state->add_sync_word_input = nullptr;
  state->add_data_rate_input = nullptr;
  state->add_frequency_deviation_input = nullptr;
  state->add_address_input = nullptr;
  state->add_address_width_input = nullptr;
  state->add_crc_length_input = nullptr;
  state->add_retransmit_count_input = nullptr;
  state->add_retransmit_delay_input = nullptr;
  state->add_crc_switch = nullptr;
  state->add_iq_switch = nullptr;
  state->add_rx_boost_switch = nullptr;
  state->add_whitening_switch = nullptr;
  state->add_fec_switch = nullptr;
  state->add_auto_ack_switch = nullptr;
  state->add_dynamic_payload_switch = nullptr;
  state->add_external_antenna_switch = nullptr;
  state->add_active_switch = nullptr;
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
  for (lv_obj_t*& button : state->add_rx_boost_buttons) {
    button = nullptr;
  }
  for (lv_obj_t*& button : state->add_output_power_buttons) {
    button = nullptr;
  }
  for (lv_obj_t*& button : state->add_preamble_buttons) {
    button = nullptr;
  }
  for (lv_obj_t*& button : state->add_receive_bandwidth_buttons) {
    button = nullptr;
  }
  for (lv_obj_t*& button : state->add_data_rate_buttons) {
    button = nullptr;
  }
}

/**
 * @brief 创建共享键盘并绑定当前协议的所有输入框
 * @param state Radio 页面状态
 * @return 创建和绑定成功时返回 true
 */
bool CreateAddModuleKeyboard(RadioViewState* state) {
  if (state == nullptr || state->add_page == nullptr) {
    return false;
  }
  SharedKeyboardConfig keyboard_config;
  keyboard_config.width = state->config.width;
  keyboard_config.height =
      state->config.height * kAddKeyboardHeightPercent / 100;
  state->add_keyboard = CreateSharedKeyboard(
      state->add_page, keyboard_config);
  if (state->add_keyboard == nullptr) {
    return false;
  }
  const auto attach = [&](lv_obj_t* input,
                          const char* accepted_chars) -> bool {
    return input == nullptr || AttachSharedKeyboardToTextArea(
        state->add_keyboard, input, accepted_chars);
  };
  const bool editing = state->editing_index < state->module_count;
  const bool result =
      (editing || attach(state->add_name_input, kProfileNameAcceptedChars)) &&
      attach(state->add_frequency_input, kFrequencyAcceptedChars) &&
      attach(state->add_power_input, "-0123456789") &&
      attach(state->add_preamble_input, kIntegerAcceptedChars) &&
      attach(state->add_sync_word_input, kHexAcceptedChars) &&
      attach(state->add_data_rate_input, kIntegerAcceptedChars) &&
      attach(state->add_frequency_deviation_input, kIntegerAcceptedChars) &&
      attach(state->add_address_input, kHexAcceptedChars) &&
      attach(state->add_address_width_input, kIntegerAcceptedChars) &&
      attach(state->add_crc_length_input, kIntegerAcceptedChars) &&
      attach(state->add_retransmit_count_input, kIntegerAcceptedChars) &&
      attach(state->add_retransmit_delay_input, kIntegerAcceptedChars);
  if (!result) {
    lv_obj_delete(state->add_keyboard);
    state->add_keyboard = nullptr;
    return false;
  }
  lv_obj_add_flag(state->add_keyboard, LV_OBJ_FLAG_GESTURE_BUBBLE);
  return true;
}

/**
 * @brief 芯片切换后按对应协议重新创建参数表单
 * @param state Radio 页面状态
 * @return 表单和共享键盘重建成功时返回 true
 */
bool RebuildAddModuleForm(RadioViewState* state) {
  if (state == nullptr || state->add_body == nullptr ||
      state->add_page == nullptr || state->add_closing) {
    return false;
  }
  char profile_name[app::kRadioProfileNameCapacity] = {};
  if (state->add_name_input != nullptr) {
    CopyBoundedString(profile_name, sizeof(profile_name),
        lv_textarea_get_text(state->add_name_input));
  }
  lv_obj_clean(state->add_body);
  if (state->add_keyboard != nullptr) {
    lv_obj_delete(state->add_keyboard);
    state->add_keyboard = nullptr;
  }
  ResetAddModuleFormPointers(state);
  if (!CreateAddModuleContent(state)) {
    return false;
  }
  if (state->add_name_input != nullptr) {
    lv_textarea_set_text(state->add_name_input, profile_name);
  }
  if (!CreateAddModuleKeyboard(state)) {
    return false;
  }
  UpdateAddSubmitButton(state);
  return true;
}

/**
 * @brief 创建添加模块页面的标题栏
 * @param page 页面对象
 * @param state 射频页面状态
 * @return 创建成功返回 true，否则返回 false
 */
bool CreateAddModuleHeader(lv_obj_t* page, RadioViewState* state) {
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
      back, icon::kArrowBack, theme::ActiveThemeColors().on_surface, OutlineIconFont44());
  if (icon_label == nullptr) {
    return false;
  }
  lv_obj_align(icon_label, LV_ALIGN_CENTER, -4, 0);
  lv_obj_t* title = CreateLabel(
      page, state->editing_index < state->module_count
          ? "Radio settings"
          : "Add Radio profile",
      theme::ActiveThemeColors().on_surface, Font32());
  if (title == nullptr) {
    return false;
  }
  lv_obj_set_width(title, state->config.width);
  lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
  lv_obj_align(title, LV_ALIGN_TOP_MID, 0, kNavigationTitleTop);
  return true;
}

/**
 * @brief 创建添加模块页面底部提交区域
 * @param page 页面对象
 * @param state 射频页面状态
 * @return 创建成功返回 true，否则返回 false
 */
bool CreateAddModuleActionArea(lv_obj_t* page, RadioViewState* state) {
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
  state->add_action_area = area;
  state->add_submit_button = button;
  lv_obj_set_size(button, state->config.width - 96, 84);
  lv_obj_align(button, LV_ALIGN_CENTER, 0, 0);
  lv_obj_set_style_radius(button, 42, LV_PART_MAIN);
  lv_obj_set_style_bg_color(button,
      lv_color_hex(theme::ActiveThemeColors().disabled_container), LV_PART_MAIN);
  lv_obj_set_style_bg_color(
      button, lv_color_hex(kPrimaryPressedColor), LV_STATE_PRESSED);
  lv_obj_set_style_bg_color(button,
      lv_color_hex(theme::ActiveThemeColors().disabled_container), LV_STATE_DISABLED);
  lv_obj_set_style_border_width(button, 0, LV_PART_MAIN);
  lv_obj_set_style_shadow_width(button, 0, LV_PART_MAIN);
  lv_obj_add_event_cb(button, AddModuleSubmitClickedEventCallback,
      LV_EVENT_CLICKED, state);
  state->add_submit_label = CreateLabel(
      button, state->editing_index < state->module_count
          ? "Save profile"
          : "Add profile",
      theme::ActiveThemeColors().disabled_content, Font28());
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
bool ShowAddModulePage(RadioViewState* state) {
  if (state == nullptr || state->root == nullptr) {
    return false;
  }
  if (state->editing_index >= state->module_count &&
      state->module_count >= kRadioModuleCapacity) {
    return false;
  }
  if (state->add_page != nullptr) {
    lv_obj_move_to_index(state->add_page, -1);
    return true;
  }
  state->selected_add_chip = 0;
  state->selected_add_protocol = 0;
  const bool editing = state->editing_index < state->module_count;
  app::RadioProfile profile = editing
      ? state->preferences.profiles[state->editing_index]
      : app::RadioProfile{};
  if (editing) {
    bool capability_found = false;
    for (size_t capability_index = 0;
         capability_index < state->capabilities.count; ++capability_index) {
      const hal::RadioCapability& capability =
          state->capabilities.entries[capability_index];
      if (capability.chip == profile.chip &&
          capability.protocol == profile.protocol) {
        state->selected_add_chip = static_cast<int>(capability_index);
        capability_found = true;
        break;
      }
    }
    if (!capability_found) {
      state->editing_index = kRadioModuleCapacity;
      return false;
    }
  } else {
    ApplyPrimaryRadioCapability(state, &profile);
  }
  state->selected_add_sf = editing
      ? std::clamp(static_cast<int>(profile.spreading_factor) - 5, 0, 7)
      : kDefaultSpreadingFactorIndex;
  state->selected_add_bandwidth = 1;
  for (size_t index = 0; index < AddProfileBandwidthCount(state); ++index) {
    if (profile.bandwidth_hz == AddProfileBandwidth(state, index)) {
      state->selected_add_bandwidth = static_cast<int>(index);
    }
  }
  state->selected_add_coding_rate = std::clamp(
      static_cast<int>(profile.coding_rate_denominator) - 5, 0, 3);
  if (profile.chip == radio::ChipType::kLr2021) {
    for (size_t index = 0;
         index < std::size(radio::kLr2021CodingRates); ++index) {
      if (profile.lr2021_coding_rate ==
          radio::kLr2021CodingRates[index]) {
        state->selected_add_coding_rate = static_cast<int>(index);
        break;
      }
    }
    state->selected_add_rx_boost_mode =
        std::min<int>(profile.lr2021_rx_boost_mode, 7);
  } else {
    state->selected_add_rx_boost_mode = profile.rx_boosted ? 7 : 0;
  }
  state->add_submitting = false;
  state->add_closing = false;
  ResetAddModuleFormPointers(state);

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
      page, lv_color_hex(theme::ActiveThemeColors().surface), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(page, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_border_width(page, 0, LV_PART_MAIN);
  lv_obj_set_style_radius(page, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(page, 0, LV_PART_MAIN);
  lv_obj_add_event_cb(page, AddPageBackgroundClickedEventCallback,
      LV_EVENT_CLICKED, state);

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

  if (!CreateAddModuleContent(state) ||
      !CreateAddModuleActionArea(page, state)) {
    lv_obj_delete(page);
    state->add_page = nullptr;
    state->add_body = nullptr;
    state->add_name_input = nullptr;
    return false;
  }

  if (!CreateAddModuleKeyboard(state)) {
    lv_obj_delete(page);
    state->add_page = nullptr;
    state->add_body = nullptr;
    state->add_name_input = nullptr;
    state->add_keyboard = nullptr;
    return false;
  }
  if (!StartSlideLeftWindowTransition(page, state->config.width,
      kAnimationMs, state, nullptr)) {
    lv_obj_delete(page);
    state->add_page = nullptr;
    state->add_body = nullptr;
    state->add_name_input = nullptr;
    state->add_keyboard = nullptr;
    return false;
  }
  if (!RegisterBackNavigationHandler(page, [state]() {
        CloseAddModulePage(state);
      })) {
    CloseAddModulePage(state);
    return false;
  }
  return true;
}

bool ShowModuleSettings(RadioViewState* state, size_t index,
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
    auto* state = static_cast<RadioViewState*>(
        lv_event_get_user_data(event));
    if (state != nullptr) {
      state->editing_index = kRadioModuleCapacity;
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
bool CreateAddButton(lv_obj_t* parent, RadioViewState* state) {
  lv_obj_t* button = lv_button_create(parent);
  if (button == nullptr) {
    return false;
  }
  state->add_button = button;
  if (state->module_count >= kRadioModuleCapacity) {
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

lv_obj_t* CreateRadioView(lv_obj_t* parent, const app::AppEntry& app_entry,
    const AppViewConfig& config) {
  if (parent == nullptr || app_entry.id == nullptr ||
      config.width <= 0 || config.height <= 0) {
    return nullptr;
  }
  auto* state = new RadioViewState{};
  state->config = config;
  if (config.radio != nullptr) {
    if (config.radio->ReadRadioCapabilities(&state->capabilities)) {
      state->capabilities.count = std::min(
          state->capabilities.count, hal::kRadioCapabilityCapacity);
      for (size_t index = 0; index < state->capabilities.count; ++index) {
        state->capabilities.entries[index].frequency_band_count = std::min(
            state->capabilities.entries[index].frequency_band_count,
            hal::kRadioFrequencyBandCapacity);
      }
    }
  }
  app::GetRadioPreferences(&state->preferences);
  app::RadioChatRepository& chat_repository = app::GetRadioChatRepository();
  chat_repository.Initialize();
  for (size_t index = 0;
       index < state->preferences.profile_count; ++index) {
    if (state->preferences.profiles[index].active) {
      chat_repository.TouchProfile(state->preferences.profiles[index].id);
    }
  }
  if (!LoadCurrentChatProfiles(state)) {
    SyncModuleItems(state);
  }
  if (config.radio != nullptr) {
    for (size_t index = 0; index < state->module_count; ++index) {
      const app::RadioProfile& profile = state->preferences.profiles[index];
      if (!profile.active) {
        continue;
      }
      if (!IsProfileSupported(state, profile)) {
        if (GetRadioActivationRegistry().GetState(profile.id) !=
            RadioActivationState::kPending) {
          SetProfileActivationState(
              profile.id, RadioActivationState::kHardwareUnavailable);
        }
        continue;
      }
      if (GetRadioActivationRegistry().GetState(profile.id) ==
          RadioActivationState::kHardwareUnavailable) {
        SetProfileActivationState(profile.id, RadioActivationState::kNone);
      }
      if (!IsProfileActivationBlocked(profile.id)) {
        QueueRadioControlCommand(state, RadioCommandType::kActivate,
            ToRadioConfig(profile));
      }
    }
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
      root, lv_color_hex(theme::ActiveThemeColors().surface), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(root, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_border_width(root, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(root, 0, LV_PART_MAIN);
  lv_obj_add_event_cb(
      root, RadioViewDeleteEventCallback, LV_EVENT_DELETE, state);
  if (config.set_status_bar_visible) {
    config.set_status_bar_visible(true);
  }
  if (config.set_status_bar_text_color) {
    config.set_status_bar_text_color(theme::ActiveThemeColors().on_surface);
  }
  lv_obj_t* list = lv_obj_create(root);
  if (list == nullptr) {
    lv_obj_delete(root);
    return nullptr;
  }
  lv_obj_set_pos(list, 0, kListTop);
  lv_obj_set_size(list, config.width, config.height - kListTop);
  lv_obj_set_style_bg_color(
      list, lv_color_hex(theme::ActiveThemeColors().surface), LV_PART_MAIN);
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
