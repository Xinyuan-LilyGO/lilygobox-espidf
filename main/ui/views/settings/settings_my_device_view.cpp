/*
 * @Description: Settings My Device detail page
 * @Author: LILYGO_L
 * @Date: 2026-05-23 00:00:00
 * @LastEditTime: 2026-07-15 16:35:56
 * @License: GPL 3.0
 */
#include "ui/views/settings/settings_view_internal.h"

#include <cstdio>

#include "app/device_identity.h"
#include "app/device_info_snapshot.h"
#include "app/settings_catalog.h"
#include "app/storage/storage.h"
#include "hal/providers/screen_provider.h"
#include "ui/animation/transition_animation.h"
#include "ui/input/app_view_gesture_flags.h"
#include "ui/input/edge_back_gesture.h"
#include "ui/input/press_cancel.h"
#include "ui/resources/fonts/icon_assets.h"
#include "ui/views/settings/settings_basic_view_common.h"
#include "ui/widgets/prompt/prompt_sheet.h"
#include "ui/widgets/shared_keyboard.h"

namespace lilygo_box::ui {
namespace {

constexpr int kFactoryResetCountdownSeconds = 10;
constexpr uint32_t kFactoryResetCountdownPeriodMs = 1000;
constexpr uint32_t kFactoryResetColor = 0xBA1A1A;
constexpr uint32_t kFactoryResetPressedColor = 0x93000A;
constexpr uint32_t kFactoryResetContainerColor = 0xFFDAD6;
constexpr uint32_t kFactoryResetContainerTextColor = 0x410002;
constexpr int kFactoryResetButtonSide = 26;
constexpr int kFactoryResetButtonHeight = 76;

void CloseFactoryResetPage(SettingsViewState* state, bool animated);

/**
 * @brief 处理设备名称行点击事件并打开编辑页
 * @param event LVGL 事件对象
 */
void DeviceNameRowClickedEventCallback(lv_event_t* event);

/**
 * @brief 读取当前显示用设备名称
 * @param config app 页面配置
 * @return 设备名称字符串
 */
const char* ReadDisplayDeviceName(const AppViewConfig& config) {
  app::CurrentDeviceInfoSnapshot info;
  if (app::ReadCurrentDeviceInfoSnapshot(config.device_info, &info)) {
    return info.software.device_name;
  }

  const char* device_name = app::ConfiguredDeviceName();
  return (device_name == nullptr || device_name[0] == '\0') ? "unknown"
                                                            : device_name;
}

/**
 * @brief 格式化容量数值
 * @param bytes 容量字节数
 * @param buffer 文本缓冲区
 * @param size 文本缓冲区大小
 */
void FormatByteSize(uint64_t bytes, char* buffer, size_t size) {
  if (buffer == nullptr || size == 0) {
    return;
  }

  constexpr uint64_t kMegabyte = 1024ULL * 1024ULL;
  const uint64_t value_tenths = bytes * 10ULL / kMegabyte;
  if (value_tenths >= 100ULL) {
    std::snprintf(buffer, size, "%llu MB",
        static_cast<unsigned long long>(value_tenths / 10ULL));
    return;
  }

  std::snprintf(buffer, size, "%llu.%llu MB",
      static_cast<unsigned long long>(value_tenths / 10ULL),
      static_cast<unsigned long long>(value_tenths % 10ULL));
}

/**
 * @brief 格式化存储空间占用
 * @param info 当前设备信息
 * @param buffer 文本缓冲区
 * @param size 文本缓冲区大小
 */
void FormatStorageUsage(
    const app::CurrentDeviceInfoSnapshot& info, char* buffer, size_t size) {
  if (buffer == nullptr || size == 0) {
    return;
  }

  if (info.chip.flash_total_bytes == 0) {
    std::snprintf(buffer, size, "unknown / unknown");
    return;
  }

  char total_text[24] = {};
  FormatByteSize(info.chip.flash_total_bytes, total_text, sizeof(total_text));

  if (!info.chip.running_image_size_valid) {
    std::snprintf(buffer, size, "unknown / %s", total_text);
    return;
  }

  char used_text[24] = {};
  FormatByteSize(info.chip.running_image_bytes, used_text, sizeof(used_text));
  std::snprintf(buffer, size, "%s / %s", used_text, total_text);
}

/**
 * @brief 格式化屏幕分辨率
 * @param info 当前设备信息
 * @param buffer 文本缓冲区
 * @param size 文本缓冲区大小
 */
void FormatResolution(
    const app::CurrentDeviceInfoSnapshot& info, char* buffer, size_t size) {
  if (buffer == nullptr || size == 0) {
    return;
  }
  if (info.screen.width <= 0 || info.screen.height <= 0) {
    std::snprintf(buffer, size, "unknown");
    return;
  }

  std::snprintf(buffer, size, "%d*%d", info.screen.width, info.screen.height);
}

/**
 * @brief 格式化运行内存容量
 * @param info 当前设备信息
 * @param buffer 文本缓冲区
 * @param size 文本缓冲区大小
 */
void FormatMemorySize(
    const app::CurrentDeviceInfoSnapshot& info, char* buffer, size_t size) {
  if (buffer == nullptr || size == 0) {
    return;
  }

  const size_t total_bytes =
      info.memory.internal_total_bytes + info.memory.psram_total_bytes;
  const size_t total_kb = total_bytes / 1024;
  if (total_kb >= 1024) {
    std::snprintf(buffer, size, "%lu MB",
        static_cast<unsigned long>(total_kb / 1024));
    return;
  }
  std::snprintf(buffer, size, "%lu KB", static_cast<unsigned long>(total_kb));
}

/**
 * @brief 格式化电池容量
 * @param config app 页面配置
 * @param buffer 文本缓冲区
 * @param size 文本缓冲区大小
 */
void FormatBatteryCapacity(
    const app::CurrentDeviceInfoSnapshot& info, char* buffer, size_t size) {
  if (buffer == nullptr || size == 0) {
    return;
  }

  if (info.battery.capacity_mah <= 0) {
    std::snprintf(buffer, size, "unknown");
    return;
  }

  std::snprintf(buffer, size, "%d mAh", info.battery.capacity_mah);
}

/**
 * @brief 处理设备名称编辑页关闭动画完成
 * @param animation LVGL 动画对象
 */
void DeviceNameEditCloseCompletedCallback(lv_anim_t* animation) {
  auto* state =
      static_cast<SettingsViewState*>(lv_anim_get_user_data(animation));
  if (state == nullptr || state->name_edit_page == nullptr) {
    return;
  }

  lv_obj_t* page = state->name_edit_page;
  state->name_edit_page = nullptr;
  state->name_edit_text_area = nullptr;
  state->name_edit_keyboard = nullptr;
  state->name_edit_closing = false;
  state->name_edit_swipe = EdgeBackSwipeState();
  lv_obj_delete(page);
}

/**
 * @brief 关闭设备名称编辑页
 * @param state 设置页面状态
 * @param animated 是否播放关闭动画
 */
void CloseDeviceNameEditPage(SettingsViewState* state, bool animated) {
  if (state == nullptr || state->name_edit_page == nullptr ||
      state->name_edit_closing) {
    return;
  }

  if (animated &&
      StartSlideRightWindowTransition(state->name_edit_page,
          state->config.width, kDetailSlideAnimationMs, state,
          DeviceNameEditCloseCompletedCallback)) {
    state->name_edit_closing = true;
    return;
  }

  lv_obj_t* page = state->name_edit_page;
  state->name_edit_page = nullptr;
  state->name_edit_text_area = nullptr;
  state->name_edit_keyboard = nullptr;
  state->name_edit_closing = false;
  state->name_edit_swipe = EdgeBackSwipeState();
  lv_obj_delete(page);
}

/**
 * @brief 处理我的设备详情页关闭动画完成
 * @param animation LVGL 动画对象
 */
void MyDeviceCloseCompletedCallback(lv_anim_t* animation) {
  auto* state =
      static_cast<SettingsViewState*>(lv_anim_get_user_data(animation));
  if (state == nullptr || state->detail_page == nullptr) {
    return;
  }

  lv_obj_t* detail_page = state->detail_page;
  state->detail_page = nullptr;
  state->device_name_value_label = nullptr;
  state->detail_closing = false;
  state->detail_swipe = EdgeBackSwipeState();
  lv_obj_delete(detail_page);
  RestoreSettingsListGestures(state);
}

/**
 * @brief 关闭我的设备详情页
 * @param state 设置页面状态
 * @param animated 是否播放关闭动画
 */
void CloseMyDevicePage(SettingsViewState* state, bool animated) {
  if (state == nullptr || state->detail_page == nullptr ||
      state->detail_closing || state->factory_reset_started) {
    return;
  }

  if (state->factory_reset_page != nullptr || state->factory_reset_closing) {
    CloseFactoryResetPage(state, animated);
    return;
  }
  CloseDeviceNameEditPage(state, false);

  if (animated &&
      StartSlideRightWindowTransition(state->detail_page, state->config.width,
          kDetailSlideAnimationMs, state, MyDeviceCloseCompletedCallback)) {
    state->detail_closing = true;
    return;
  }

  lv_obj_t* detail_page = state->detail_page;
  state->detail_page = nullptr;
  state->device_name_value_label = nullptr;
  state->detail_closing = false;
  state->detail_swipe = EdgeBackSwipeState();
  lv_obj_delete(detail_page);
  RestoreSettingsListGestures(state);
}

/**
 * @brief 处理我的设备详情页返回点击
 * @param event LVGL 事件对象
 */
void MyDeviceBackClickedEventCallback(lv_event_t* event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
    return;
  }

  CloseMyDevicePage(
      static_cast<SettingsViewState*>(lv_event_get_user_data(event)), true);
}

/**
 * @brief 处理我的设备详情页边缘返回
 * @param event LVGL 事件对象
 */
void MyDeviceEdgeBackEventCallback(lv_event_t* event) {
  auto* state = static_cast<SettingsViewState*>(lv_event_get_user_data(event));
  if (state == nullptr || state->detail_page == nullptr ||
      state->detail_closing || state->name_edit_page != nullptr ||
      state->name_edit_closing || state->factory_reset_page != nullptr ||
      state->factory_reset_closing || state->config.screen == nullptr ||
      !HandleEdgeBackSwipeEvent(event, state->config.width,
          &state->detail_swipe)) {
    return;
  }

  CloseMyDevicePage(state, true);
  lv_event_stop_bubbling(event);
  lv_event_stop_processing(event);
}

/**
 * @brief 处理设备名称编辑页边缘返回手势
 * @param event LVGL 事件对象
 */
void DeviceNameEditSwipeEventCallback(lv_event_t* event) {
  auto* state = static_cast<SettingsViewState*>(lv_event_get_user_data(event));
  if (state == nullptr || state->name_edit_page == nullptr ||
      state->name_edit_closing || state->config.screen == nullptr ||
      !HandleEdgeBackSwipeEvent(event, state->config.width,
          &state->name_edit_swipe)) {
    return;
  }

  CloseDeviceNameEditPage(state, true);
  lv_event_stop_bubbling(event);
  lv_event_stop_processing(event);
}

/**
 * @brief 处理设备名称编辑页键盘外点击隐藏键盘
 * @param event LVGL 事件对象
 */
void DeviceNameEditKeyboardDismissEventCallback(lv_event_t* event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
    return;
  }

  auto* state = static_cast<SettingsViewState*>(lv_event_get_user_data(event));
  if (state == nullptr || state->name_edit_keyboard == nullptr ||
      state->name_edit_text_area == nullptr) {
    return;
  }

  lv_obj_t* target = lv_event_get_target_obj(event);
  if (IsObjectOrChildOf(target, state->name_edit_keyboard) ||
      IsObjectOrChildOf(target, state->name_edit_text_area)) {
    return;
  }

  HideSharedKeyboard(state->name_edit_keyboard);
}

/**
 * @brief 处理设备名称编辑取消按钮
 * @param event LVGL 事件对象
 */
void DeviceNameEditCancelClickedEventCallback(lv_event_t* event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
    return;
  }

  CloseDeviceNameEditPage(
      static_cast<SettingsViewState*>(lv_event_get_user_data(event)), true);
}

/**
 * @brief 刷新设备名称显示对象
 * @param state 设置页面状态
 */
void RefreshDeviceNameLabels(SettingsViewState* state) {
  if (state == nullptr) {
    return;
  }

  const char* device_name = ReadDisplayDeviceName(state->config);
  if (state->device_name_value_label != nullptr) {
    lv_label_set_text(state->device_name_value_label, device_name);
  }
}

/**
 * @brief 处理设备名称编辑确认按钮
 * @param event LVGL 事件对象
 */
void DeviceNameEditConfirmClickedEventCallback(lv_event_t* event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
    return;
  }

  auto* state = static_cast<SettingsViewState*>(lv_event_get_user_data(event));
  if (state == nullptr || state->name_edit_text_area == nullptr) {
    return;
  }

  const char* text = lv_textarea_get_text(state->name_edit_text_area);
  if (!app::SetConfiguredDeviceName(text)) {
    lv_textarea_set_text(
        state->name_edit_text_area, ReadDisplayDeviceName(state->config));
    return;
  }

  RefreshDeviceNameLabels(state);
  CloseDeviceNameEditPage(state, true);
}

/**
 * @brief 停止恢复出厂设置倒计时
 * @param state 设置页面状态
 */
void StopFactoryResetCountdown(SettingsViewState* state) {
  if (state == nullptr || state->factory_reset_countdown_timer == nullptr) {
    return;
  }

  lv_timer_delete(state->factory_reset_countdown_timer);
  state->factory_reset_countdown_timer = nullptr;
}

/**
 * @brief 清理恢复出厂设置页面引用
 * @param state 设置页面状态
 */
void ClearFactoryResetPageReferences(SettingsViewState* state) {
  if (state == nullptr) {
    return;
  }

  state->factory_reset_page = nullptr;
  state->factory_reset_confirm_button = nullptr;
  state->factory_reset_confirm_label = nullptr;
  state->factory_reset_seconds_remaining = 0;
  state->factory_reset_closing = false;
  state->factory_reset_started = false;
  state->factory_reset_swipe = EdgeBackSwipeState();
}

/**
 * @brief 处理恢复出厂设置警告页关闭动画完成
 * @param animation LVGL 动画对象
 */
void FactoryResetCloseCompletedCallback(lv_anim_t* animation) {
  auto* state =
      static_cast<SettingsViewState*>(lv_anim_get_user_data(animation));
  if (state == nullptr || state->factory_reset_page == nullptr) {
    return;
  }

  lv_obj_t* page = state->factory_reset_page;
  StopFactoryResetCountdown(state);
  ClearFactoryResetPageReferences(state);
  lv_obj_delete(page);
}

/**
 * @brief 关闭恢复出厂设置警告页
 * @param state 设置页面状态
 * @param animated 是否播放关闭动画
 */
void CloseFactoryResetPage(SettingsViewState* state, bool animated) {
  if (state == nullptr || state->factory_reset_page == nullptr ||
      state->factory_reset_closing || state->factory_reset_started) {
    return;
  }

  StopFactoryResetCountdown(state);
  if (animated &&
      StartSlideRightWindowTransition(state->factory_reset_page,
          state->config.width, kDetailSlideAnimationMs, state,
          FactoryResetCloseCompletedCallback)) {
    state->factory_reset_closing = true;
    return;
  }

  lv_obj_t* page = state->factory_reset_page;
  ClearFactoryResetPageReferences(state);
  lv_obj_delete(page);
}

/**
 * @brief 刷新恢复出厂设置确认按钮状态
 * @param state 设置页面状态
 */
void UpdateFactoryResetConfirmButton(SettingsViewState* state) {
  if (state == nullptr || state->factory_reset_confirm_button == nullptr ||
      state->factory_reset_confirm_label == nullptr) {
    return;
  }

  char text[40] = {};
  if (state->factory_reset_seconds_remaining > 0) {
    std::snprintf(text, sizeof(text), "Erase all data (%d)",
        state->factory_reset_seconds_remaining);
  } else {
    std::snprintf(text, sizeof(text), "Erase all data");
  }
  lv_label_set_text(state->factory_reset_confirm_label, text);

  const bool enabled = state->factory_reset_seconds_remaining == 0 &&
                       !state->factory_reset_started;
  if (enabled) {
    lv_obj_remove_state(
        state->factory_reset_confirm_button, LV_STATE_DISABLED);
  } else {
    lv_obj_add_state(state->factory_reset_confirm_button, LV_STATE_DISABLED);
  }
  lv_obj_set_style_bg_color(state->factory_reset_confirm_button,
      lv_color_hex(enabled ? kFactoryResetColor
                           : theme::LightNeutralTheme().disabled_container),
      LV_PART_MAIN);
  lv_obj_set_style_text_color(state->factory_reset_confirm_label,
      lv_color_hex(enabled ? 0xFFFFFF
                           : theme::LightNeutralTheme().outline),
      LV_PART_MAIN);
}

/**
 * @brief 处理恢复出厂设置倒计时
 * @param timer LVGL 定时器
 */
void FactoryResetCountdownTimerCallback(lv_timer_t* timer) {
  if (timer == nullptr) {
    return;
  }

  auto* state = static_cast<SettingsViewState*>(lv_timer_get_user_data(timer));
  if (state == nullptr || state->factory_reset_countdown_timer != timer) {
    return;
  }
  if (state->factory_reset_page == nullptr) {
    state->factory_reset_countdown_timer = nullptr;
    lv_timer_delete(timer);
    return;
  }

  if (state->factory_reset_seconds_remaining > 0) {
    --state->factory_reset_seconds_remaining;
  }
  UpdateFactoryResetConfirmButton(state);
  if (state->factory_reset_seconds_remaining == 0) {
    state->factory_reset_countdown_timer = nullptr;
    lv_timer_delete(timer);
  }
}

/**
 * @brief 处理恢复出厂设置警告页返回点击
 * @param event LVGL 事件对象
 */
void FactoryResetBackClickedEventCallback(lv_event_t* event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
    return;
  }

  CloseFactoryResetPage(
      static_cast<SettingsViewState*>(lv_event_get_user_data(event)), true);
}

/**
 * @brief 处理恢复出厂设置警告页边缘返回
 * @param event LVGL 事件对象
 */
void FactoryResetEdgeBackEventCallback(lv_event_t* event) {
  auto* state = static_cast<SettingsViewState*>(lv_event_get_user_data(event));
  if (state == nullptr || state->factory_reset_page == nullptr ||
      state->factory_reset_closing || state->factory_reset_started ||
      state->config.screen == nullptr ||
      !HandleEdgeBackSwipeEvent(event, state->config.width,
          &state->factory_reset_swipe)) {
    return;
  }

  CloseFactoryResetPage(state, true);
  lv_event_stop_bubbling(event);
  lv_event_stop_processing(event);
}

/**
 * @brief 处理恢复出厂设置确认按钮
 * @param event LVGL 事件对象
 */
void FactoryResetConfirmClickedEventCallback(lv_event_t* event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
    return;
  }

  auto* state = static_cast<SettingsViewState*>(lv_event_get_user_data(event));
  if (state == nullptr || state->factory_reset_page == nullptr ||
      state->factory_reset_seconds_remaining != 0 ||
      state->factory_reset_started) {
    return;
  }

  state->factory_reset_started = true;
  UpdateFactoryResetConfirmButton(state);
  hal::ScreenProvider* screen = state->config.screen;
  const int previous_brightness = state->display_brightness_percent;
  // 复用锁屏的轻度休眠流程，确保背光和屏幕完全关闭后再擦除 NVS。
  const bool screen_sleeping =
      screen == nullptr || screen->EnterDeviceSleep();
  if (!screen_sleeping) {
    state->factory_reset_started = false;
    UpdateFactoryResetConfirmButton(state);
    return;
  }
  if (!app::StartFactoryReset()) {
    if (screen != nullptr) {
      screen->ExitDeviceSleep();
      screen->SetScreenBrightnessPercent(previous_brightness);
    }
    state->factory_reset_started = false;
    UpdateFactoryResetConfirmButton(state);
  }
}

/**
 * @brief 创建恢复出厂设置警告页顶部导航栏
 * @param parent 父对象
 * @param state 设置页面状态
 * @param width 页面宽度
 * @return 创建成功返回 true，否则返回 false
 */
bool CreateFactoryResetHeader(
    lv_obj_t* parent, SettingsViewState* state, int width) {
  lv_obj_t* back_button = CreateToolbarButton(parent, kDetailBackButtonLeft,
      kDetailBackButtonTop, FactoryResetBackClickedEventCallback, state);
  if (back_button == nullptr) {
    return false;
  }

  lv_obj_t* back_icon = CreateLabel(back_button, icon::kArrowBack,
      lv_color_hex(kDetailBackColor), MaterialIconFont44());
  if (back_icon == nullptr) {
    return false;
  }
  lv_obj_align(back_icon, LV_ALIGN_CENTER, kDetailBackIconOffsetX, 0);

  lv_obj_t* title = CreateLabel(
      parent, "Factory reset", lv_color_hex(kTitleColor), Font32());
  if (title == nullptr) {
    return false;
  }
  lv_obj_set_width(title, width);
  lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
  lv_obj_align(title, LV_ALIGN_TOP_MID, 0, kDetailTitleTop);
  return true;
}

/**
 * @brief 创建恢复出厂设置警告内容和操作按钮
 * @param parent 父对象
 * @param state 设置页面状态
 * @param width 页面宽度
 * @param height 页面高度
 * @return 创建成功返回 true，否则返回 false
 */
bool CreateFactoryResetContent(lv_obj_t* parent, SettingsViewState* state,
    int width, int height) {
  const bool compact = height < 800;
  const int icon_size = compact ? 82 : 112;
  const int icon_top = compact ? 132 : 220;
  const int heading_top = compact ? 226 : 376;
  const int message_top = compact ? 274 : 438;
  const int notice_top = compact ? 350 : 620;
  const int notice_height = compact ? 74 : 118;
  const int button_height = compact ? 66 : kFactoryResetButtonHeight;

  lv_obj_t* icon_box = CreateBox(parent, icon_size, icon_size,
      kFactoryResetContainerColor, LV_OPA_COVER, icon_size / 2);
  if (icon_box == nullptr) {
    return false;
  }
  lv_obj_align(icon_box, LV_ALIGN_TOP_MID, 0, icon_top);

  lv_obj_t* warning_icon = CreateLabel(icon_box, icon::kWarning,
      lv_color_hex(kFactoryResetColor), MaterialIconFont44());
  if (warning_icon == nullptr) {
    return false;
  }
  lv_obj_align(warning_icon, LV_ALIGN_CENTER, 0, -3);

  lv_obj_t* heading = CreateLabel(
      parent, "Erase all saved data?", lv_color_hex(kTitleColor), Font36());
  if (heading == nullptr) {
    return false;
  }
  lv_obj_set_width(heading, width - 2 * kFactoryResetButtonSide);
  lv_obj_set_style_text_align(heading, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
  lv_obj_align(heading, LV_ALIGN_TOP_MID, 0, heading_top);

  lv_obj_t* message = CreateLabel(parent,
      "All saved settings, Wi-Fi networks, and device preferences will be "
      "permanently erased. Files on the SD card will not be deleted.",
      lv_color_hex(kSecondaryTextColor), Font28());
  if (message == nullptr) {
    return false;
  }
  lv_obj_set_width(message, width - 2 * (kFactoryResetButtonSide + 16));
  lv_label_set_long_mode(message, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_text_align(message, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
  lv_obj_set_style_text_line_space(message, 7, LV_PART_MAIN);
  lv_obj_align(message, LV_ALIGN_TOP_MID, 0, message_top);

  const int notice_width = width - 2 * kFactoryResetButtonSide;
  lv_obj_t* notice = CreateBox(parent, notice_width, notice_height,
      kFactoryResetContainerColor, LV_OPA_COVER, 24);
  if (notice == nullptr) {
    return false;
  }
  lv_obj_align(notice, LV_ALIGN_TOP_MID, 0, notice_top);

  lv_obj_t* notice_icon = CreateLabel(notice, icon::kWarning,
      lv_color_hex(kFactoryResetContainerTextColor), MaterialIconFont32());
  if (notice_icon == nullptr) {
    return false;
  }
  lv_obj_align(notice_icon, LV_ALIGN_LEFT_MID, 22, 0);

  lv_obj_t* notice_text = CreateLabel(notice,
      "This action can't be undone. The device will restart with factory "
      "defaults.",
      lv_color_hex(kFactoryResetContainerTextColor), Font24());
  if (notice_text == nullptr) {
    return false;
  }
  lv_obj_set_width(notice_text, notice_width - 86);
  lv_label_set_long_mode(notice_text, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_text_line_space(notice_text, 4, LV_PART_MAIN);
  lv_obj_align(notice_text, LV_ALIGN_LEFT_MID, 68, 0);

  PromptSheetButtonConfig confirm_config;
  confirm_config.text = "Erase all data (10)";
  confirm_config.x = kFactoryResetButtonSide;
  confirm_config.y =
      height - kFactoryResetButtonSide - button_height;
  confirm_config.width = width - 2 * kFactoryResetButtonSide;
  confirm_config.height = button_height;
  confirm_config.radius = button_height / 3;
  confirm_config.background_color = kFactoryResetColor;
  confirm_config.disabled_background_color =
      theme::LightNeutralTheme().disabled_container;
  confirm_config.pressed_background_color = kFactoryResetPressedColor;
  confirm_config.text_color = 0xFFFFFF;
  confirm_config.font = Font28();
  confirm_config.callback = FactoryResetConfirmClickedEventCallback;
  confirm_config.user_data = state;
  confirm_config.enabled = false;
  state->factory_reset_confirm_button =
      CreatePromptSheetButton(parent, confirm_config);
  if (state->factory_reset_confirm_button == nullptr) {
    return false;
  }
  state->factory_reset_confirm_label =
      lv_obj_get_child(state->factory_reset_confirm_button, 0);
  if (state->factory_reset_confirm_label == nullptr) {
    return false;
  }
  UpdateFactoryResetConfirmButton(state);
  return true;
}

/**
 * @brief 打开恢复出厂设置警告页
 * @param state 设置页面状态
 * @return 打开成功返回 true，否则返回 false
 */
bool ShowFactoryResetPage(SettingsViewState* state) {
  if (state == nullptr || state->root == nullptr ||
      state->detail_page == nullptr) {
    return false;
  }
  if (state->factory_reset_closing) {
    return true;
  }
  if (state->factory_reset_page != nullptr) {
    lv_obj_move_to_index(state->factory_reset_page, -1);
    return true;
  }

  const AppViewConfig& config = state->config;
  lv_obj_t* page = lv_obj_create(state->root);
  if (page == nullptr) {
    return false;
  }
  state->factory_reset_page = page;
  state->factory_reset_confirm_button = nullptr;
  state->factory_reset_confirm_label = nullptr;
  state->factory_reset_seconds_remaining = kFactoryResetCountdownSeconds;
  state->factory_reset_closing = false;
  state->factory_reset_started = false;
  state->factory_reset_swipe = EdgeBackSwipeState();

  lv_obj_remove_flag(page, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(page, LV_OBJ_FLAG_GESTURE_BUBBLE);
  AddEdgeBackSwipeEvents(page, FactoryResetEdgeBackEventCallback, state);
  lv_obj_set_size(page, config.width, config.height);
  lv_obj_set_pos(page, 0, 0);
  lv_obj_set_style_bg_color(
      page, lv_color_hex(kBackgroundColor), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(page, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_border_width(page, 0, LV_PART_MAIN);
  lv_obj_set_style_radius(page, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(page, 0, LV_PART_MAIN);

  if (!CreateFactoryResetHeader(page, state, config.width) ||
      !CreateFactoryResetContent(
          page, state, config.width, config.height)) {
    CloseFactoryResetPage(state, false);
    return false;
  }

  state->factory_reset_countdown_timer = lv_timer_create(
      FactoryResetCountdownTimerCallback, kFactoryResetCountdownPeriodMs,
      state);
  if (state->factory_reset_countdown_timer == nullptr) {
    CloseFactoryResetPage(state, false);
    return false;
  }

  EnableEdgeBackSwipeEventBubble(page);
  if (!StartSlideLeftWindowTransition(
          page, config.width, kDetailSlideAnimationMs, state, nullptr)) {
    CloseFactoryResetPage(state, false);
    return false;
  }
  return true;
}

/**
 * @brief 处理恢复出厂设置选项点击事件
 * @param event LVGL 事件对象
 */
void FactoryResetRowClickedEventCallback(lv_event_t* event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
    return;
  }

  ShowFactoryResetPage(
      static_cast<SettingsViewState*>(lv_event_get_user_data(event)));
}

/**
 * @brief 创建取消图标
 * @param parent 父对象
 * @return 创建成功返回 true，否则返回 false
 */
bool CreateCloseIcon(lv_obj_t* parent) {
  lv_obj_t* icon = CreateLabel(parent, icon::kClose,
      lv_color_hex(kDetailBackColor), MaterialIconFont44());
  if (icon == nullptr) {
    return false;
  }
  lv_obj_center(icon);
  return true;
}

/**
 * @brief 创建确认图标
 * @param parent 父对象
 * @return 创建成功返回 true，否则返回 false
 */
bool CreateCheckIcon(lv_obj_t* parent) {
  lv_obj_t* icon = CreateLabel(parent, icon::kCheck,
      lv_color_hex(kDetailBackColor), MaterialIconFont44());
  if (icon == nullptr) {
    return false;
  }
  lv_obj_center(icon);
  return true;
}

/**
 * @brief 创建我的设备页面顶部导航栏
 * @param parent 父对象
 * @param state 设置页面状态
 * @param width 页面宽度
 * @return 创建成功返回 true，否则返回 false
 */
bool CreateMyDeviceHeader(
    lv_obj_t* parent, SettingsViewState* state, int width) {
  lv_obj_t* title =
      CreateLabel(parent, "My Device", lv_color_hex(kTitleColor), Font32());
  if (title == nullptr) {
    return false;
  }
  lv_obj_set_width(title, width);
  lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
  lv_obj_align(title, LV_ALIGN_TOP_MID, 0, kDetailTitleTop);

  lv_obj_t* back_button = lv_button_create(parent);
  if (back_button == nullptr) {
    return false;
  }
  lv_obj_remove_style_all(back_button);
  lv_obj_remove_flag(back_button, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(back_button, LV_OBJ_FLAG_PRESS_LOCK);
  lv_obj_add_flag(back_button, LV_OBJ_FLAG_GESTURE_BUBBLE);
  lv_obj_set_size(back_button, kDetailBackButtonSize, kDetailBackButtonSize);
  lv_obj_set_pos(back_button, kDetailBackButtonLeft, kDetailBackButtonTop);
  lv_obj_set_style_bg_opa(back_button, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_set_style_bg_opa(back_button, LV_OPA_TRANSP, LV_STATE_PRESSED);
  lv_obj_set_style_bg_opa(back_button, LV_OPA_TRANSP, LV_STATE_FOCUSED);
  lv_obj_set_style_bg_opa(back_button, LV_OPA_TRANSP, LV_STATE_FOCUS_KEY);
  lv_obj_set_style_border_width(back_button, 0, LV_PART_MAIN);
  lv_obj_set_style_outline_width(back_button, 0, LV_PART_MAIN);
  lv_obj_set_style_outline_width(back_button, 0, LV_STATE_PRESSED);
  lv_obj_set_style_outline_width(back_button, 0, LV_STATE_FOCUSED);
  lv_obj_set_style_outline_width(back_button, 0, LV_STATE_FOCUS_KEY);
  lv_obj_set_style_shadow_width(back_button, 0, LV_PART_MAIN);
  lv_obj_set_style_shadow_width(back_button, 0, LV_STATE_PRESSED);
  lv_obj_set_style_shadow_width(back_button, 0, LV_STATE_FOCUSED);
  lv_obj_set_style_shadow_width(back_button, 0, LV_STATE_FOCUS_KEY);
  lv_obj_set_style_radius(back_button, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(back_button, 0, LV_PART_MAIN);
  if (!AddPressCancelOnLeave(back_button)) {
    return false;
  }
  lv_obj_add_event_cb(
      back_button, MyDeviceBackClickedEventCallback, LV_EVENT_CLICKED, state);

  lv_obj_t* back_icon = CreateLabel(
      back_button, icon::kArrowBack, lv_color_hex(kDetailBackColor),
      MaterialIconFont44());
  if (back_icon == nullptr) {
    return false;
  }
  lv_obj_align(back_icon, LV_ALIGN_CENTER, kDetailBackIconOffsetX, 0);
  return true;
}

/**
 * @brief 创建我的设备页面系统标识区域
 * @param parent 父对象
 * @param width 内容宽度
 * @param info 当前设备信息
 * @return 创建成功返回 true，否则返回 false
 */
bool CreateMyDeviceSnapshotArea(lv_obj_t* parent, int width,
    const app::CurrentDeviceInfoSnapshot& info) {
  lv_obj_t* brand =
      CreateLabel(parent, "LilygoBox", lv_color_hex(0x050505), Font48());
  if (brand == nullptr) {
    return false;
  }
  lv_obj_set_width(brand, width);
  lv_obj_set_style_text_align(brand, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
  lv_obj_align(brand, LV_ALIGN_TOP_MID, 0, kDetailBrandTop);

  lv_obj_t* version =
      CreateLabel(parent, info.software.software_version,
          lv_color_hex(kSecondaryTextColor), Font24());
  if (version == nullptr) {
    return false;
  }
  lv_obj_set_width(version, width);
  lv_obj_set_style_text_align(version, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
  lv_obj_align(version, LV_ALIGN_TOP_MID, 0, kDetailVersionTop);

  lv_obj_t* update_button = lv_button_create(parent);
  if (update_button == nullptr) {
    return false;
  }
  lv_obj_remove_flag(update_button, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(update_button, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
  lv_obj_add_flag(update_button, LV_OBJ_FLAG_GESTURE_BUBBLE);
  lv_obj_set_size(update_button, kDetailUpdateWidth, kDetailUpdateHeight);
  lv_obj_align(update_button, LV_ALIGN_TOP_MID, 0, kDetailUpdateTop);
  lv_obj_set_style_bg_color(update_button, lv_color_hex(kDetailBlueColor),
      LV_PART_MAIN);
  lv_obj_set_style_bg_opa(update_button, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_border_width(update_button, 0, LV_PART_MAIN);
  lv_obj_set_style_radius(update_button, kDetailUpdateHeight / 3,
      LV_PART_MAIN);
  lv_obj_set_style_shadow_width(update_button, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(update_button, 0, LV_PART_MAIN);
  if (!AddPressCancelOnLeave(update_button)) {
    return false;
  }

  lv_obj_t* update_text =
      CreateLabel(update_button, "New version", lv_color_hex(0xFFFFFF),
          Font28());
  if (update_text == nullptr) {
    return false;
  }
  lv_obj_center(update_text);
  return true;
}

/**
 * @brief 创建我的设备信息行
 * @param parent 父对象
 * @param title 标题文本
 * @param value 右侧值文本
 * @param y Y 坐标
 * @param width 行宽度
 * @param show_arrow 是否显示右侧箭头
 * @param callback 点击回调
 * @param user_data 点击回调用户数据
 * @param value_label_output 右侧值文本输出
 * @return 创建成功返回 true，否则返回 false
 */
bool CreateDeviceInfoRow(lv_obj_t* parent, const char* title,
    const char* value, int y, int width, bool show_arrow,
    lv_event_cb_t callback, void* user_data,
    lv_obj_t** value_label_output) {
  lv_obj_t* row = lv_obj_create(parent);
  if (row == nullptr) {
    return false;
  }

  lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(row, LV_OBJ_FLAG_GESTURE_BUBBLE);
  lv_obj_set_size(row, width, kDetailInfoRowHeight);
  lv_obj_set_pos(row, 0, y);
  lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_set_style_border_width(row, 0, LV_PART_MAIN);
  lv_obj_set_style_radius(row, 0, LV_PART_MAIN);
  lv_obj_set_style_radius(row, 0, LV_STATE_PRESSED);
  lv_obj_set_style_pad_all(row, 0, LV_PART_MAIN);
  if (callback != nullptr) {
    lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_PRESS_LOCK);
    lv_obj_set_style_bg_color(
        row, lv_color_hex(kDeviceInfoPressedColor), LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(row, LV_OPA_COVER, LV_STATE_PRESSED);
    if (!AddPressCancelOnLeave(row)) {
      return false;
    }
    lv_obj_add_event_cb(row, callback, LV_EVENT_CLICKED, user_data);
  } else {
    lv_obj_remove_flag(row, LV_OBJ_FLAG_CLICKABLE);
  }

  lv_obj_t* title_label =
      CreateLabel(row, title, lv_color_hex(kPrimaryTextColor), Font28());
  if (title_label == nullptr) {
    return false;
  }
  lv_obj_align(title_label, LV_ALIGN_LEFT_MID, kDetailCardPaddingX, 0);

  lv_obj_t* anchor = nullptr;
  if (show_arrow) {
    anchor = CreateLabel(row, icon::kChevronRight,
        lv_color_hex(kSecondaryTextColor), MaterialIconFont32());
    if (anchor == nullptr) {
      return false;
    }
    lv_obj_align(anchor, LV_ALIGN_RIGHT_MID, -kDetailCardPaddingX, 0);
  }

  lv_obj_t* value_label =
      CreateLabel(row, value, lv_color_hex(kSecondaryTextColor), Font24());
  if (value_label == nullptr) {
    return false;
  }
  lv_obj_set_width(value_label, width / 2);
  lv_obj_set_style_text_align(value_label, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);
  if (anchor != nullptr) {
    lv_obj_align_to(value_label, anchor, LV_ALIGN_OUT_LEFT_MID, -10, 0);
  } else {
    lv_obj_align(value_label, LV_ALIGN_RIGHT_MID, -kDetailCardPaddingX, 0);
  }

  if (value_label_output != nullptr) {
    *value_label_output = value_label;
  }
  return true;
}

/**
 * @brief 创建我的设备基础信息卡片
 * @param parent 父对象
 * @param config app 页面配置
 * @param width 页面宽度
 * @param state 设置页面状态
 * @param info 当前设备信息
 * @return 创建成功返回 true，否则返回 false
 */
bool CreateDeviceInfoCard(
    lv_obj_t* parent, const AppViewConfig&, int width,
    SettingsViewState* state, const app::CurrentDeviceInfoSnapshot& info) {
  char storage_text[48] = {};
  FormatStorageUsage(info, storage_text, sizeof(storage_text));

  const int card_width = width - 2 * kDetailSidePadding;
  lv_obj_t* card = CreateBox(parent, card_width, kDetailFirstCardHeight,
      kDetailCardColor, LV_OPA_COVER,
      kDetailCardRadius);
  if (card == nullptr) {
    return false;
  }
  lv_obj_align(card, LV_ALIGN_TOP_MID, 0, kDetailFirstCardTop);

  return CreateDeviceInfoRow(card, "Device name",
             info.software.device_name, 22, card_width, true,
             DeviceNameRowClickedEventCallback, state,
             state == nullptr ? nullptr : &state->device_name_value_label) &&
         CreateDeviceInfoRow(card, "Storage space", storage_text,
             22 + kDetailInfoRowHeight, card_width, false, nullptr, nullptr,
             nullptr);
}

/**
 * @brief 创建我的设备规格文本
 * @param parent 父对象
 * @param value 参数值
 * @param label 参数名称
 * @param x X 坐标
 * @param y Y 坐标
 * @return 创建成功返回 true，否则返回 false
 */
bool CreateSpecText(lv_obj_t* parent, const char* value, const char* label,
    int x, int y) {
  lv_obj_t* value_label =
      CreateLabel(parent, value, lv_color_hex(kPrimaryTextColor), Font28());
  if (value_label == nullptr) {
    return false;
  }
  lv_obj_align(value_label, LV_ALIGN_TOP_LEFT, x, y);

  lv_obj_t* title_label =
      CreateLabel(parent, label, lv_color_hex(kSecondaryTextColor), Font22());
  if (title_label == nullptr) {
    return false;
  }
  lv_obj_align(title_label, LV_ALIGN_TOP_LEFT, x, y + 34);
  return true;
}

/**
 * @brief 创建我的设备规格卡片
 * @param parent 父对象
 * @param config app 页面配置
 * @param width 页面宽度
 * @param state 设置页面状态
 * @param info 当前设备信息
 * @return 创建成功返回 true，否则返回 false
 */
bool CreateDeviceSpecCard(
    lv_obj_t* parent, const AppViewConfig&, int width,
    SettingsViewState*, const app::CurrentDeviceInfoSnapshot& info) {
  char memory_text[32] = {};
  char battery_text[32] = {};
  char resolution_text[32] = {};
  FormatMemorySize(info, memory_text, sizeof(memory_text));
  FormatBatteryCapacity(info, battery_text, sizeof(battery_text));
  FormatResolution(info, resolution_text, sizeof(resolution_text));

  lv_obj_t* card = CreateBox(parent, width - 2 * kDetailSidePadding,
      kDetailSecondCardHeight, kDetailCardColor, LV_OPA_COVER,
      kDetailCardRadius);
  if (card == nullptr) {
    return false;
  }
  lv_obj_align(card, LV_ALIGN_TOP_MID, 0, kDetailSecondCardTop);

  lv_obj_t* title = CreateLabel(card, info.software.device_model_name,
      lv_color_hex(kPrimaryTextColor), Font36());
  if (title == nullptr) {
    return false;
  }
  lv_obj_align(title, LV_ALIGN_TOP_LEFT, kDetailCardPaddingX,
      kDetailCardPaddingTop);

  lv_obj_t* model_version = CreateLabel(card,
      info.software.device_model_version, lv_color_hex(kSecondaryTextColor),
      Font28());
  if (model_version == nullptr) {
    return false;
  }
  lv_obj_align_to(model_version, title, LV_ALIGN_OUT_RIGHT_MID, 14, 2);

  const int first_y = kDetailCardPaddingTop + 82;
  return CreateSpecText(card, info.chip.model, "Processor",
             kDetailCardPaddingX, first_y) &&
         CreateSpecText(card, memory_text, "Runtime memory",
             kDetailCardPaddingX, first_y + 86) &&
         CreateSpecText(card, battery_text, "Battery capacity",
             kDetailCardPaddingX, first_y + 172) &&
         CreateSpecText(card, resolution_text, "Resolution",
             kDetailCardPaddingX, first_y + 258) &&
         CreateSpecText(card, info.screen.type, "Screen type",
             kDetailCardPaddingX, first_y + 344) &&
         CreateSpecText(card, info.camera.type, "Camera type",
             kDetailCardPaddingX, first_y + 430) &&
         CreateSpecText(card, info.software.software_build_date,
             "Firmware build date", kDetailCardPaddingX, first_y + 516);
}

/**
 * @brief 创建我的设备选项行
 * @param parent 父对象
 * @param text 选项文本
 * @param y Y 坐标
 * @param width 页面宽度
 * @param callback 点击回调
 * @param user_data 点击回调用户数据
 * @return 创建成功返回 true，否则返回 false
 */
bool CreateDeviceOptionRow(lv_obj_t* parent, const char* text, int y,
    int width, lv_event_cb_t callback, void* user_data) {
  lv_obj_t* row = lv_obj_create(parent);
  if (row == nullptr) {
    return false;
  }

  lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_flag(row, LV_OBJ_FLAG_GESTURE_BUBBLE);
  lv_obj_set_size(row, width, kDetailOptionRowHeight);
  lv_obj_set_pos(row, 0, y);
  lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_set_style_bg_color(
      row, lv_color_hex(kDetailOptionPressedColor), LV_STATE_PRESSED);
  lv_obj_set_style_bg_opa(row, kDetailOptionPressedOpacity, LV_STATE_PRESSED);
  lv_obj_set_style_border_width(row, 0, LV_PART_MAIN);
  lv_obj_set_style_radius(row, 0, LV_PART_MAIN);
  lv_obj_set_style_radius(row, 0, LV_STATE_PRESSED);
  lv_obj_set_style_pad_all(row, 0, LV_PART_MAIN);
  if (!AddPressCancelOnLeave(row)) {
    return false;
  }
  if (callback != nullptr) {
    lv_obj_add_event_cb(row, callback, LV_EVENT_CLICKED, user_data);
  }

  lv_obj_t* label =
      CreateLabel(row, text, lv_color_hex(kPrimaryTextColor), Font28());
  if (label == nullptr) {
    return false;
  }
  lv_obj_align(label, LV_ALIGN_LEFT_MID, kDetailSidePadding + 8, 0);

  lv_obj_t* arrow = CreateLabel(row, icon::kChevronRight,
      lv_color_hex(kSecondaryTextColor), MaterialIconFont32());
  if (arrow == nullptr) {
    return false;
  }
  lv_obj_align(arrow, LV_ALIGN_RIGHT_MID, -(kDetailSidePadding + 6), 0);
  return true;
}

/**
 * @brief 创建我的设备下方选项列表
 * @param parent 父对象
 * @param width 页面宽度
 * @param state 设置页面状态
 * @return 创建成功返回 true，否则返回 false
 */
bool CreateDeviceOptions(
    lv_obj_t* parent, int width, SettingsViewState* state) {
  const app::SettingsDeviceOptionCatalog& catalog =
      app::GetSettingsDeviceOptionCatalog();
  int y = kDetailSecondCardTop + kDetailSecondCardHeight + kDetailOptionTopGap;
  for (size_t i = 0; i < catalog.option_count; ++i) {
    const lv_event_cb_t callback = IsId(catalog.options[i].id, "factory_reset")
                                       ? FactoryResetRowClickedEventCallback
                                       : nullptr;
    if (!CreateDeviceOptionRow(parent, catalog.options[i].title, y, width,
            callback, state)) {
      return false;
    }
    y += kDetailOptionRowHeight;
  }
  return true;
}

/**
 * @brief 打开我的设备详情页
 * @param state 设置页状态
 * @return 打开成功返回 true，否则返回 false
 */
bool ShowMyDevicePageInternal(SettingsViewState* state) {
  if (state == nullptr || state->root == nullptr) {
    return false;
  }
  if (state->detail_closing) {
    return true;
  }
  if (state->detail_page != nullptr) {
    lv_obj_add_flag(state->root, kBlockLauncherGestureFlag);
    lv_obj_remove_flag(state->root, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_move_to_index(state->detail_page, -1);
    return true;
  }

  const AppViewConfig& config = state->config;
  lv_obj_t* page = lv_obj_create(state->root);
  if (page == nullptr) {
    return false;
  }
  state->detail_page = page;
  state->detail_closing = false;
  state->detail_swipe = EdgeBackSwipeState();
  lv_obj_add_flag(state->root, kBlockLauncherGestureFlag);
  lv_obj_remove_flag(state->root, LV_OBJ_FLAG_GESTURE_BUBBLE);

  lv_obj_remove_flag(page, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(page, LV_OBJ_FLAG_GESTURE_BUBBLE);
  AddEdgeBackSwipeEvents(page, MyDeviceEdgeBackEventCallback, state);
  lv_obj_set_size(page, config.width, config.height);
  lv_obj_set_pos(page, 0, 0);
  lv_obj_set_style_bg_color(
      page, lv_color_hex(kDetailBackgroundColor), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(page, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_border_width(page, 0, LV_PART_MAIN);
  lv_obj_set_style_radius(page, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(page, 0, LV_PART_MAIN);

  if (!CreateMyDeviceHeader(page, state, config.width)) {
    CloseMyDevicePage(state, false);
    return false;
  }

  lv_obj_t* body = lv_obj_create(page);
  if (body == nullptr) {
    CloseMyDevicePage(state, false);
    return false;
  }
  MakeTransparent(body);
  lv_obj_set_size(body, config.width, config.height - kDetailBodyTop);
  lv_obj_align(body, LV_ALIGN_TOP_LEFT, 0, kDetailBodyTop);
  lv_obj_set_scroll_dir(body, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(body, LV_SCROLLBAR_MODE_OFF);
  lv_obj_add_flag(body, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(body, LV_OBJ_FLAG_GESTURE_BUBBLE);
  lv_obj_remove_flag(body, LV_OBJ_FLAG_SCROLL_ELASTIC);
  AddEdgeBackSwipeEvents(body, MyDeviceEdgeBackEventCallback, state);

  app::CurrentDeviceInfoSnapshot device_info;
  if (!app::ReadCurrentDeviceInfoSnapshot(config.device_info, &device_info)) {
    CloseMyDevicePage(state, false);
    return false;
  }

  const bool created = CreateMyDeviceSnapshotArea(
                           body, config.width, device_info) &&
                       CreateDeviceInfoCard(
                           body, config, config.width, state, device_info) &&
                       CreateDeviceSpecCard(
                           body, config, config.width, state, device_info) &&
                       CreateDeviceOptions(body, config.width, state);
  if (!created) {
    CloseMyDevicePage(state, false);
    return false;
  }

  EnableEdgeBackSwipeEventBubble(page);
  if (!StartSlideLeftWindowTransition(
          page, config.width, kDetailSlideAnimationMs, state, nullptr)) {
    CloseMyDevicePage(state, false);
    return false;
  }
  return true;
}

/**
 * @brief 创建设备名称编辑顶部按钮
 * @param parent 父对象
 * @param state 设置页面状态
 * @param width 页面宽度
 * @return 创建成功返回 true，否则返回 false
 */
bool CreateDeviceNameEditHeader(
    lv_obj_t* parent, SettingsViewState* state, int width) {
  lv_obj_t* cancel = CreateToolbarButton(parent, kNameEditButtonSide,
      kNameEditButtonTop, DeviceNameEditCancelClickedEventCallback, state);
  if (cancel == nullptr || !CreateCloseIcon(cancel)) {
    return false;
  }

  lv_obj_t* confirm = CreateToolbarButton(parent,
      width - kNameEditButtonSide - kNameEditButtonSize, kNameEditButtonTop,
      DeviceNameEditConfirmClickedEventCallback, state);
  if (confirm == nullptr || !CreateCheckIcon(confirm)) {
    return false;
  }

  return true;
}

/**
 * @brief 创建设备名称编辑输入区域
 * @param parent 父对象
 * @param state 设置页面状态
 * @param config app 页面配置
 * @return 创建成功返回 true，否则返回 false
 */
bool CreateDeviceNameEditContent(
    lv_obj_t* parent, SettingsViewState* state, const AppViewConfig& config) {
  lv_obj_t* title = CreateLabel(
      parent, "Edit device name", lv_color_hex(kTitleColor), Font48());
  if (title == nullptr) {
    return false;
  }
  lv_obj_align(title, LV_ALIGN_TOP_LEFT, kNameEditTextAreaSide,
      kNameEditTitleTop);

  lv_obj_t* text_area = lv_textarea_create(parent);
  if (text_area == nullptr) {
    return false;
  }
  state->name_edit_text_area = text_area;
  lv_obj_add_flag(text_area, LV_OBJ_FLAG_GESTURE_BUBBLE);
  AddEdgeBackSwipeEvents(text_area, DeviceNameEditSwipeEventCallback, state);
  lv_obj_set_size(text_area, config.width - 2 * kNameEditTextAreaSide,
      kNameEditTextAreaHeight);
  lv_obj_align(text_area, LV_ALIGN_TOP_LEFT, kNameEditTextAreaSide,
      kNameEditTextAreaTop);
  lv_textarea_set_one_line(text_area, true);
  lv_textarea_set_max_length(text_area, app::kMaxDeviceNameLength);
  lv_textarea_set_accepted_chars(text_area, kDeviceNameAcceptedChars);
  lv_textarea_set_text(text_area, ReadDisplayDeviceName(config));
  lv_textarea_set_cursor_pos(text_area, LV_TEXTAREA_CURSOR_LAST);
  ApplySettingsTextAreaStyle(
      text_area, Font32(), kNameEditTextAreaHeight);

  lv_obj_t* help = CreateLabel(parent,
      "This name is shown when identifying this device.",
      lv_color_hex(kSecondaryTextColor), Font24());
  if (help == nullptr) {
    return false;
  }
  lv_obj_set_width(help, config.width - 2 * (kNameEditTextAreaSide + 10));
  lv_label_set_long_mode(help, LV_LABEL_LONG_WRAP);
  lv_obj_align(help, LV_ALIGN_TOP_LEFT, kNameEditTextAreaSide + 10,
      kNameEditHelpTop);

  SharedKeyboardConfig keyboard_config;
  keyboard_config.width = config.width;
  keyboard_config.height =
      config.height * kNameEditKeyboardHeightPercent / 100;
  lv_obj_t* keyboard = CreateSharedKeyboard(parent, keyboard_config);
  if (keyboard == nullptr) {
    return false;
  }
  state->name_edit_keyboard = keyboard;
  lv_obj_add_flag(keyboard, LV_OBJ_FLAG_GESTURE_BUBBLE);
  AddEdgeBackSwipeEvents(keyboard, DeviceNameEditSwipeEventCallback, state);

  return AttachSharedKeyboardToTextArea(
      keyboard, text_area, kDeviceNameAcceptedChars);
}

/**
 * @brief 显示设备名称编辑页
 * @param state 设置页面状态
 * @return 显示成功返回 true，否则返回 false
 */
bool ShowDeviceNameEditPage(SettingsViewState* state) {
  if (state == nullptr || state->root == nullptr) {
    return false;
  }
  if (state->name_edit_closing) {
    return true;
  }
  if (state->name_edit_page != nullptr) {
    lv_obj_move_to_index(state->name_edit_page, -1);
    return true;
  }

  const AppViewConfig& config = state->config;
  lv_obj_t* page = lv_obj_create(state->root);
  if (page == nullptr) {
    return false;
  }

  state->name_edit_page = page;
  state->name_edit_text_area = nullptr;
  state->name_edit_closing = false;
  state->name_edit_swipe = EdgeBackSwipeState();
  lv_obj_remove_flag(page, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(page, LV_OBJ_FLAG_GESTURE_BUBBLE);
  AddEdgeBackSwipeEvents(page, DeviceNameEditSwipeEventCallback, state);
  lv_obj_add_event_cb(
      page, DeviceNameEditKeyboardDismissEventCallback, LV_EVENT_CLICKED,
      state);
  lv_obj_set_size(page, config.width, config.height);
  lv_obj_set_pos(page, 0, 0);
  lv_obj_set_style_bg_color(
      page, lv_color_hex(kBackgroundColor), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(page, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_border_width(page, 0, LV_PART_MAIN);
  lv_obj_set_style_radius(page, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(page, 0, LV_PART_MAIN);

  if (!CreateDeviceNameEditHeader(page, state, config.width) ||
      !CreateDeviceNameEditContent(page, state, config)) {
    CloseDeviceNameEditPage(state, false);
    return false;
  }

  EnableEdgeBackSwipeEventBubble(page);
  if (!StartSlideLeftWindowTransition(
          page, config.width, kDetailSlideAnimationMs, state, nullptr)) {
    CloseDeviceNameEditPage(state, false);
    return false;
  }
  return true;
}

void DeviceNameRowClickedEventCallback(lv_event_t* event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
    return;
  }

  auto* state = static_cast<SettingsViewState*>(lv_event_get_user_data(event));
  if (state == nullptr) {
    return;
  }
  ShowDeviceNameEditPage(state);
}

}  // namespace

bool ShowMyDevicePage(SettingsViewState* state) {
  return ShowMyDevicePageInternal(state);
}

}  // namespace lilygo_box::ui
