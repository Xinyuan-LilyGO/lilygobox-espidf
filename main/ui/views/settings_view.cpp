/*
 * @Description: Settings main list view
 * @Author: LILYGO_L
 * @Date: 2026-05-18 09:20:00
 * @LastEditTime: 2026-05-23 00:00:00
 * @License: GPL 3.0
 */
#include "ui/views/settings_view.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <new>

#include "app/settings_catalog.h"
#include "app/storage/display_storage.h"
#include "app/storage/haptic_storage.h"
#include "app/storage/sound_storage.h"
#include "hal/providers/wifi_provider.h"
#include "ui/font/material_symbols_assets.h"
#include "ui/input/press_cancel.h"
#include "ui/haptic_feedback.h"
#include "ui/views/settings/settings_view_internal.h"

namespace lilygo_box::ui {
namespace {

constexpr uint32_t kSettingsStatusBarTextColor = 0x111111;

// 保存旋转后需要自动恢复的子页面 ID
const char* g_restore_sub_page = nullptr;
// 旋转恢复时跳过页面切换动画，避免看到列表页闪现
bool g_skip_page_animation = false;

}  // namespace

bool IsSkipPageAnimation() { return g_skip_page_animation; }

bool ConsumeSkipPageAnimation() {
  const bool skip = g_skip_page_animation;
  g_skip_page_animation = false;
  return skip;
}

void SetSettingsRestoreSubPage(const char* page_id) {
  g_restore_sub_page = page_id;
}

const char* GetSettingsRestoreSubPage() {
  return g_restore_sub_page;
}

// 设置入口图标样式。
struct SettingsIconStyle {
  const char* symbol = nullptr;
  uint32_t color = 0x3F82F6;
};

/**
 * @brief 解析设置入口图标样式
 * @param icon_type 设置入口图标类型
 * @return 图标样式
 */
SettingsIconStyle ResolveSettingsIconStyle(app::SettingsIcon icon_type) {
  switch (icon_type) {
    case app::SettingsIcon::kInfo:
      return {.symbol = icon::kInfo, .color = 0x8790B0};
    case app::SettingsIcon::kWifi:
      return {.symbol = icon::kSignalWifi4Bar, .color = 0x3F82F6};
    case app::SettingsIcon::kBluetooth:
      return {.symbol = icon::kBluetooth, .color = 0x3E7FF1};
    case app::SettingsIcon::kCellTower:
      return {.symbol = icon::kCellTower, .color = 0x59C96B};
    case app::SettingsIcon::kAppList:
      return {.symbol = icon::kAppList, .color = 0xF2F2F2};
    case app::SettingsIcon::kAntenna:
      return {.symbol = icon::kSettingsInputAntenna, .color = 0x347BF2};
    case app::SettingsIcon::kHome:
      return {.symbol = icon::kHome, .color = 0x55C76C};
    case app::SettingsIcon::kFile:
      return {.symbol = icon::kFile, .color = 0x8890AF};
    case app::SettingsIcon::kImage:
      return {.symbol = icon::kImage, .color = 0x347DF5};
    case app::SettingsIcon::kSunny:
      return {.symbol = icon::kSunny, .color = 0xF4C95D};
    case app::SettingsIcon::kFolder:
      return {.symbol = icon::kFolder, .color = 0xF05B34};
    case app::SettingsIcon::kLock:
      return {.symbol = icon::kLock, .color = 0xF05B34};
    case app::SettingsIcon::kWarning:
      return {.symbol = icon::kWarning, .color = 0x3F82F6};
    case app::SettingsIcon::kVolumeUp:
      return {.symbol = icon::kVolumeUp, .color = 0xB9A0F6};
    case app::SettingsIcon::kBattery:
      return {.symbol = icon::kBatteryAndroidFull, .color = 0x55C76C};
    case app::SettingsIcon::kSettings:
      return {.symbol = icon::kSettings, .color = 0x8790B0};
  }
  return {.symbol = icon::kInfo, .color = 0x8790B0};
}

/**
 * @brief 创建设置项图标背景
 * @param parent 父对象
 * @param item 设置项
 * @return 创建成功返回对象指针，否则返回 nullptr
 */
lv_obj_t* CreateIconBox(lv_obj_t* parent, const app::SettingsEntry& item) {
  const SettingsIconStyle icon_style = ResolveSettingsIconStyle(item.icon);
  lv_obj_t* box = lv_obj_create(parent);
  if (box == nullptr) {
    return nullptr;
  }

  lv_obj_remove_flag(box, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(box, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_size(box, kIconBoxSize, kIconBoxSize);
  lv_obj_set_style_radius(box, kIconBoxRadius, LV_PART_MAIN);
  lv_obj_set_style_bg_color(
      box, lv_color_hex(icon_style.color), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(box, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_border_width(box, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(box, 0, LV_PART_MAIN);

  lv_obj_t* icon = CreateLabel(
      box, icon_style.symbol, lv_color_hex(0xFFFFFF), MaterialIconFont32());
  if (icon == nullptr) {
    lv_obj_delete(box);
    return nullptr;
  }
  lv_obj_center(icon);
  return box;
}

/**
 * @brief 处理设置页释放事件
 * @param event LVGL 事件对象
 */
void SettingsViewDeleteEventCallback(lv_event_t* event) {
  if (lv_event_get_code(event) != LV_EVENT_DELETE) {
    return;
  }

  auto* state = static_cast<SettingsViewState*>(lv_event_get_user_data(event));
  if (state != nullptr && state->wifi_refresh_timer != nullptr) {
    lv_timer_delete(state->wifi_refresh_timer);
    state->wifi_refresh_timer = nullptr;
  }
  delete state;
}

/**
 * @brief 处理我的设备设置项点击
 * @param event LVGL 事件对象
 */
void MyDeviceRowClickedEventCallback(lv_event_t* event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
    return;
  }

  auto* state = static_cast<SettingsViewState*>(lv_event_get_user_data(event));
  if (state == nullptr) {
    return;
  }
  ShowMyDevicePage(state);
}

/**
 * @brief 从设置主页打开 WLAN 详情页
 * @param event LVGL 事件对象
 */
void WifiRowClickedEventCallback(lv_event_t* event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
    return;
  }

  auto* state = static_cast<SettingsViewState*>(lv_event_get_user_data(event));
  if (state == nullptr) {
    return;
  }
  ShowWifiPage(state);
}

/**
 * @brief 从设置主页打开蓝牙详情页
 * @param event LVGL 事件对象
 */
void BluetoothRowClickedEventCallback(lv_event_t* event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
    return;
  }

  ShowBluetoothPage(
      static_cast<SettingsViewState*>(lv_event_get_user_data(event)));
}

/**
 * @brief 从设置主页打开个人热点详情页
 * @param event LVGL 事件对象
 */
void HotspotRowClickedEventCallback(lv_event_t* event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
    return;
  }

  ShowPersonalHotspotPage(
      static_cast<SettingsViewState*>(lv_event_get_user_data(event)));
}

/**
 * @brief 从设置主页打开锁屏详情页
 * @param event LVGL 事件对象
 */
void LockScreenRowClickedEventCallback(lv_event_t* event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
    return;
  }

  ShowLockScreenPage(
      static_cast<SettingsViewState*>(lv_event_get_user_data(event)));
}

/**
 * @brief 从设置主页打开显示与亮度详情页
 * @param event LVGL 事件对象
 */
void DisplayBrightnessRowClickedEventCallback(lv_event_t* event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
    return;
  }

  ShowDisplayBrightnessPage(
      static_cast<SettingsViewState*>(lv_event_get_user_data(event)));
}

/**
 * @brief 从设置主页打开声音与触感详情页
 * @param event LVGL 事件对象
 */
void SoundRowClickedEventCallback(lv_event_t* event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
    return;
  }

  ShowSoundHapticsPage(
      static_cast<SettingsViewState*>(lv_event_get_user_data(event)));
}

/**
 * @brief 从设置主页打开省电与电池详情页
 * @param event LVGL 事件对象
 */
void PowerBatteryRowClickedEventCallback(lv_event_t* event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
    return;
  }

  ShowPowerBatteryPage(
      static_cast<SettingsViewState*>(lv_event_get_user_data(event)));
}

/**
 * @brief 创建单个设置项
 * @param parent 父对象
 * @param item 设置项
 * @param width 设置项宽度
 * @param value_label_output 右侧值标签输出地址，可为 nullptr
 * @return 创建成功返回对象指针，否则返回 nullptr
 */
lv_obj_t* CreateSettingsRow(lv_obj_t* parent,
    const app::SettingsEntry& item, int width,
    lv_obj_t** value_label_output) {
  if (value_label_output != nullptr) {
    *value_label_output = nullptr;
  }

  lv_obj_t* row = lv_obj_create(parent);
  if (row == nullptr) {
    return nullptr;
  }

  lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_flag(row, LV_OBJ_FLAG_GESTURE_BUBBLE);
  lv_obj_set_size(row, width, kRowHeight);
  lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_set_style_bg_color(
      row, lv_color_hex(kPressedColor), LV_STATE_PRESSED);
  lv_obj_set_style_bg_opa(row, kPressedOpacity, LV_STATE_PRESSED);
  lv_obj_set_style_border_width(row, 0, LV_PART_MAIN);
  lv_obj_set_style_radius(row, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(row, 0, LV_PART_MAIN);
  if (!AddPressCancelOnLeave(row)) {
    lv_obj_delete(row);
    return nullptr;
  }

  lv_obj_t* icon_box = CreateIconBox(row, item);
  if (icon_box == nullptr) {
    lv_obj_delete(row);
    return nullptr;
  }
  lv_obj_align(icon_box, LV_ALIGN_LEFT_MID, kPagePaddingX + kIconLeft, 0);

  lv_obj_t* title =
      CreateLabel(row, item.title, lv_color_hex(kPrimaryTextColor), Font28());
  if (title == nullptr) {
    lv_obj_delete(row);
    return nullptr;
  }
  lv_obj_set_width(title, width - 2 * kPagePaddingX - kTextLeft - 120);
  lv_obj_align(title, LV_ALIGN_LEFT_MID, kPagePaddingX + kTextLeft, 0);

  lv_obj_t* arrow = CreateLabel(row, icon::kChevronRight,
      lv_color_hex(kSecondaryTextColor), MaterialIconFont32());
  if (arrow == nullptr) {
    lv_obj_delete(row);
    return nullptr;
  }
  lv_obj_align(
      arrow, LV_ALIGN_RIGHT_MID, -(kPagePaddingX + kArrowRight), 0);

  if (item.value != nullptr && item.value[0] != '\0') {
    lv_obj_t* value = CreateLabel(
        row, item.value, lv_color_hex(kSecondaryTextColor), Font24());
    if (value == nullptr) {
      lv_obj_delete(row);
      return nullptr;
    }
    lv_obj_set_style_text_align(value, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);
    lv_obj_set_width(
        value, width - 2 * kPagePaddingX - kTextLeft - kValueRight - 40);
    lv_obj_align(
        value, LV_ALIGN_RIGHT_MID, -(kPagePaddingX + kValueRight), 1);
    if (value_label_output != nullptr) {
      *value_label_output = value;
    }
  }

  return row;
}

/**
 * @brief 读取当前 WLAN 是否处于开启或正在开启状态
 * @param config app 页面配置
 * @return WLAN 正在运行、连接、初始化或扫描时返回 true
 */
bool IsWifiCurrentlyEnabled(const AppViewConfig& config) {
  if (config.wifi == nullptr) {
    return false;
  }

  hal::WifiStatus status;
  hal::WifiScanStatus scan_status;
  config.wifi->ReadWifiStatus(&status);
  config.wifi->ReadWifiScanStatus(&scan_status);
  return status.running || status.connected || status.got_ip ||
         status.init_task_running || scan_status.scan_running;
}

/**
 * @brief 根据 WLAN 开关状态生成设置主页右侧 On/Off 文本
 * @param state 设置页状态
 * @return WLAN 打开返回 On，否则返回 Off
 */
const char* WifiValueText(const SettingsViewState* state) {
  return state != nullptr && state->wifi_enabled_requested ? "On" : "Off";
}

/**
 * @brief 创建设置列表
 * @param parent 父对象
 * @param width 列表宽度
 * @param state 设置页面状态
 * @return 创建成功返回 true，否则返回 false
 */
bool CreateSettingsList(
    lv_obj_t* parent, int width, SettingsViewState* state) {
  const app::SettingsCatalog& catalog = app::GetSettingsCatalog();
  int y = 0;
  for (size_t i = 0; i < catalog.entry_count; ++i) {
    app::SettingsEntry item = catalog.entries[i];
    if (item.divider_before) {
      lv_obj_t* divider =
          CreateDivider(parent, width - 2 * kPagePaddingX - kDividerLeft);
      if (divider == nullptr) {
        return false;
      }
      lv_obj_set_pos(divider, kPagePaddingX + kDividerLeft,
          y + kGroupDividerTopPadding);
      y += kGroupDividerTopPadding + kDividerHeight +
           kGroupDividerBottomPadding;
    }

    if (IsId(item.id, "wlan")) {
      item.value = WifiValueText(state);
    }

    lv_obj_t* wifi_value_label = nullptr;
    lv_obj_t* row = CreateSettingsRow(parent, item, width,
        IsId(item.id, "wlan") ? &wifi_value_label : nullptr);
    if (row == nullptr) {
      return false;
    }
    if (IsId(item.id, "my_device")) {
      lv_obj_add_event_cb(
          row, MyDeviceRowClickedEventCallback, LV_EVENT_CLICKED, state);
    } else if (IsId(item.id, "wlan")) {
      state->wifi_value_label = wifi_value_label;
      lv_obj_add_event_cb(
          row, WifiRowClickedEventCallback, LV_EVENT_CLICKED, state);
    } else if (IsId(item.id, "bluetooth")) {
      lv_obj_add_event_cb(
          row, BluetoothRowClickedEventCallback, LV_EVENT_CLICKED, state);
    } else if (IsId(item.id, "personal_hotspot")) {
      lv_obj_add_event_cb(
          row, HotspotRowClickedEventCallback, LV_EVENT_CLICKED, state);
    } else if (IsId(item.id, "lock_screen")) {
      lv_obj_add_event_cb(
          row, LockScreenRowClickedEventCallback, LV_EVENT_CLICKED, state);
    } else if (IsId(item.id, "display_brightness")) {
      lv_obj_add_event_cb(row, DisplayBrightnessRowClickedEventCallback,
          LV_EVENT_CLICKED, state);
    } else if (IsId(item.id, "sound")) {
      lv_obj_add_event_cb(
          row, SoundRowClickedEventCallback, LV_EVENT_CLICKED, state);
    } else if (IsId(item.id, "power_battery")) {
      lv_obj_add_event_cb(
          row, PowerBatteryRowClickedEventCallback, LV_EVENT_CLICKED, state);
    }
    lv_obj_set_pos(row, 0, y);
    y += kRowHeight;
  }
  return true;
}

/**
 * @brief 按当前 WLAN 开关请求状态刷新设置主页 WLAN 行右侧文字
 * @param state 设置页状态
 */
void UpdateSettingsWifiValue(SettingsViewState* state) {
  if (state == nullptr || state->wifi_value_label == nullptr) {
    return;
  }
  lv_label_set_text(state->wifi_value_label, WifiValueText(state));
}

/**
 * @brief 创建设置主页视图
 * @param parent 父对象
 * @param config app 页面配置
 * @return 创建成功返回页面对象指针，否则返回 nullptr
 */
lv_obj_t* CreateSettingsView(lv_obj_t* parent, const app::AppEntry&,
    const AppViewConfig& config) {
  if (parent == nullptr || config.width <= 0 || config.height <= 0) {
    return nullptr;
  }

  if (config.set_status_bar_text_color) {
    config.set_status_bar_text_color(kSettingsStatusBarTextColor);
  }

  lv_obj_t* root = lv_obj_create(parent);
  if (root == nullptr) {
    return nullptr;
  }
  auto* state = new (std::nothrow) SettingsViewState();
  if (state == nullptr) {
    lv_obj_delete(root);
    return nullptr;
  }
  state->config = config;
  state->root = root;
  LoadWifiSettingsFromNvs(state, IsWifiCurrentlyEnabled(config));
  app::DisplayPreferences display_preferences = app::GetDisplayPreferences();
  state->display_brightness_percent = display_preferences.brightness_percent;
  state->auto_lock_seconds = display_preferences.lock_timeout_seconds;
  state->screen_rotation_angle = display_preferences.screen_rotation_angle;
  app::SoundPreferences sound_preferences = app::GetSoundPreferences();
  state->audio_volume_percent = sound_preferences.volume_percent;
  app::HapticPreferences haptic_preferences = app::GetHapticPreferences();
  state->haptics_enabled = haptic_preferences.enabled;
  state->haptic_strength_percent = haptic_preferences.strength_percent;
  SetUiHapticPreferences(
      state->haptics_enabled, state->haptic_strength_percent);
  lv_obj_add_event_cb(
      root, SettingsViewDeleteEventCallback, LV_EVENT_DELETE, state);

  lv_obj_remove_flag(root, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(root, config.width, config.height);
  lv_obj_set_pos(root, 0, 0);
  lv_obj_set_style_bg_color(
      root, lv_color_hex(kBackgroundColor), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(root, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_border_width(root, 0, LV_PART_MAIN);
  lv_obj_set_style_radius(root, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(root, 0, LV_PART_MAIN);

  lv_obj_t* title =
      CreateLabel(root, "Settings", lv_color_hex(kTitleColor), Font48());
  if (title == nullptr) {
    lv_obj_delete(root);
    return nullptr;
  }
  lv_obj_align(title, LV_ALIGN_TOP_LEFT, kPagePaddingX, kTitleTop);

  lv_obj_t* list = lv_obj_create(root);
  if (list == nullptr) {
    lv_obj_delete(root);
    return nullptr;
  }
  MakeTransparent(list);
  lv_obj_set_size(list, config.width, config.height - kListTop);
  lv_obj_align(list, LV_ALIGN_TOP_LEFT, 0, kListTop);
  lv_obj_set_scroll_dir(list, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_OFF);
  lv_obj_add_flag(list, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(list, LV_OBJ_FLAG_GESTURE_BUBBLE);
  lv_obj_remove_flag(list, LV_OBJ_FLAG_SCROLL_ELASTIC);

  if (!CreateSettingsList(list, config.width, state)) {
    lv_obj_delete(root);
    return nullptr;
  }

  if (g_restore_sub_page != nullptr) {
    g_skip_page_animation = true;
    if (std::strcmp(g_restore_sub_page, "display_brightness") == 0) {
      ShowDisplayBrightnessPage(state);
    }
    // 后续其他子页面可以在这里扩展
  }

  return root;
}

}  // namespace lilygo_box::ui
