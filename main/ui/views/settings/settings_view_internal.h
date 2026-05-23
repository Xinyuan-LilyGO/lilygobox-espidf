/*
 * @Description: Settings view internal helpers
 * @Author: LILYGO_L
 * @Date: 2026-05-23 00:00:00
 * @LastEditTime: 2026-05-23 00:00:00
 * @License: GPL 3.0
 */
#pragma once

#include <cstddef>
#include <cstdint>

#include "hal/providers/wifi_provider.h"
#include "lvgl.h"
#include "ui/input/edge_back_gesture.h"
#include "ui/views/app_view_config.h"

namespace lilygo_box::ui {

constexpr int kPagePaddingX = 34;
constexpr int kTitleTop = 100;
constexpr int kListTop = 184;
constexpr int kRowHeight = 104;
constexpr int kIconBoxSize = 54;
constexpr int kIconBoxRadius = 13;
constexpr int kIconLeft = 0;
constexpr int kTextLeft = 86;
constexpr int kValueRight = 42;
constexpr int kArrowRight = 4;
constexpr int kDividerLeft = 0;
constexpr int kDividerHeight = 2;
constexpr int kGroupDividerTopPadding = 16;
constexpr int kGroupDividerBottomPadding = 22;
constexpr uint32_t kBackgroundColor = 0xFFFFFF;
constexpr uint32_t kTitleColor = 0x222222;
constexpr uint32_t kPrimaryTextColor = 0x101010;
constexpr uint32_t kSecondaryTextColor = 0x969696;
constexpr uint32_t kDividerColor = 0xE9E9E9;
constexpr uint32_t kPressedColor = 0xEBEBEB;
constexpr lv_opa_t kPressedOpacity = 190;
constexpr int kDetailBackButtonSize = 62;
constexpr int kDetailBackButtonLeft = 18;
constexpr int kDetailBackButtonTop = 66;
constexpr int kDetailTitleTop = 78;
constexpr int kDetailBodyTop = 148;
constexpr int kDetailSidePadding = 26;
constexpr int kDetailBrandTop = 14;
constexpr int kDetailVersionTop = 88;
constexpr int kDetailUpdateTop = 150;
constexpr int kDetailUpdateHeight = 72;
constexpr int kDetailUpdateWidth = 400;
constexpr int kDetailFirstCardTop = 260;
constexpr int kDetailFirstCardHeight = 188;
constexpr int kDetailSecondCardTop = 478;
constexpr int kDetailSecondCardHeight = 730;
constexpr int kDetailOptionTopGap = 36;
constexpr int kDetailOptionRowHeight = 98;
constexpr int kDetailCardRadius = 26;
constexpr int kDetailCardPaddingX = 34;
constexpr int kDetailCardPaddingTop = 34;
constexpr int kDetailInfoRowHeight = 72;
constexpr uint32_t kDetailSlideAnimationMs = 180;
constexpr uint32_t kDetailBackgroundColor = 0xF4F4F4;
constexpr uint32_t kDetailCardColor = 0xFFFFFF;
constexpr uint32_t kDeviceInfoPressedColor = 0xEBEBEB;
constexpr uint32_t kDetailBlueColor = 0x3F7EF5;
constexpr uint32_t kDetailBackColor = 0x222222;
constexpr uint32_t kDetailOptionPressedColor = 0xE0E0E0;
constexpr lv_opa_t kDetailOptionPressedOpacity = LV_OPA_COVER;
constexpr int kNameEditButtonSize = kDetailBackButtonSize;
constexpr int kNameEditButtonTop = kDetailBackButtonTop;
constexpr int kNameEditButtonSide = kDetailBackButtonLeft;
constexpr int kNameEditTitleTop = 170;
constexpr int kNameEditTextAreaTop = 280;
constexpr int kNameEditTextAreaHeight = 88;
constexpr int kNameEditTextAreaSide = 26;
constexpr int kNameEditTextAreaRadius = 28;
constexpr int kNameEditHelpTop =
    kNameEditTextAreaTop + kNameEditTextAreaHeight + 10;
constexpr int kNameEditKeyboardHeightPercent = 35;
constexpr int kWifiTitleTop = 154;
constexpr int kWifiBodyTop = 238;
constexpr int kWifiSidePadding = 34;
constexpr int kWifiRowHeight = 80;
constexpr int kWifiSectionHeight = 54;
constexpr int kWifiNetworkRowHeight = 82;
constexpr int kWifiConnectedCardHeight = 104;
constexpr int kWifiConnectedCardRadius = 28;
constexpr int kWifiNetworkIconLeft = 24;
constexpr int kWifiNetworkTextLeft = 86;
constexpr int kWifiNetworkRightControlWidth = 126;
constexpr int kWifiNetworkArrowRight = 18;
constexpr int kWifiCircleButtonSize = 46;
constexpr int kWifiSwitchWidth = 78;
constexpr int kWifiSwitchHeight = 44;
constexpr uint32_t kWifiSwitchAnimationMs = 180;
constexpr uint32_t kWifiRefreshSpinMs = 850;
constexpr lv_style_selector_t kWifiSwitchCheckedIndicatorSelector =
    static_cast<lv_style_selector_t>(LV_PART_INDICATOR) |
    static_cast<lv_style_selector_t>(LV_STATE_CHECKED);
constexpr uint32_t kWifiRefreshPeriodMs = 1000;
constexpr const char* kWifiSavedSsid = "LilyGo-AABB";
constexpr const char* kWifiSavedSsid5G = "LilyGo-AABB-5G";
constexpr const char* kWifiDefaultPassword = "xinyuandianzi";
constexpr size_t kWifiActionCapacity = hal::kMaxWifiScanNetworkCount + 4;
constexpr uint32_t kWifiBlueColor = 0x4D82F5;
constexpr uint32_t kWifiCardColor = 0xF6F7F9;
constexpr uint32_t kWifiMutedColor = 0xA5A5AD;
constexpr uint32_t kWifiControlColor = 0xF0F1F3;
constexpr uint32_t kNameEditInputColor = 0xF2F2F2;
constexpr uint32_t kNameEditInputBorderColor = 0x4A86F7;
constexpr const char* kDeviceNameAcceptedChars =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_. ";

struct SettingsViewState;

// WiFi 列表行的点击动作参数，LVGL 回调会在稍后读取这些稳定地址。
struct WifiNetworkAction {
  // 所属设置页状态，用于回调里发起连接请求。
  SettingsViewState* state = nullptr;
  // 网络名称副本，避免扫描列表重建后回调访问临时字符串。
  char ssid[hal::kWifiSsidMaxLength + 1] = {};
  // 连接密码副本，开放网络会保持为空字符串。
  char password[hal::kWifiPasswordMaxLength + 1] = {};
};

// 设置页运行状态，跨主列表、WLAN、我的设备和设备名编辑页共享。
struct SettingsViewState {
  AppViewConfig config;
  lv_obj_t* root = nullptr;
  lv_obj_t* detail_page = nullptr;
  lv_obj_t* name_edit_page = nullptr;
  lv_obj_t* name_edit_text_area = nullptr;
  lv_obj_t* name_edit_keyboard = nullptr;
  lv_obj_t* wifi_page = nullptr;
  // WLAN 页面内容容器，扫描或连接状态变化时会整体重建。
  lv_obj_t* wifi_body = nullptr;
  // 已连接卡片里的信号图标，RSSI 变化时可单独刷新。
  lv_obj_t* wifi_connected_signal_icon = nullptr;
  lv_obj_t* wifi_refresh_icon = nullptr;
  // WLAN 页面定时刷新器，用来轮询 HAL 扫描和连接状态。
  lv_timer_t* wifi_refresh_timer = nullptr;
  lv_obj_t* device_name_value_label = nullptr;
  // WLAN 列表行点击参数池，避免 LVGL 回调使用临时地址。
  WifiNetworkAction wifi_actions[kWifiActionCapacity] = {};
  EdgeBackSwipeState detail_swipe = {};
  EdgeBackSwipeState name_edit_swipe = {};
  EdgeBackSwipeState wifi_swipe = {};
  size_t wifi_action_count = 0;
  // 上一次渲染的 WLAN 状态摘要，用来跳过重复刷新。
  uint32_t wifi_refresh_key = 0;
  bool detail_closing = false;
  bool name_edit_closing = false;
  bool wifi_closing = false;
  // 用户期望的 WLAN 开关状态，硬件初始化期间保持 UI 一致。
  bool wifi_enabled_requested = false;
  // 驱动初始化完成后补发一次 WLAN 扫描请求。
  bool wifi_scan_on_ready = false;
  // 本次自动扫描请求发起前的扫描结果版本号。
  uint32_t wifi_scan_request_generation = 0;
  // 标记下一次定时刷新必须重建 WLAN 页面。
  bool wifi_refresh_force = false;
};

void SetTextStyle(lv_obj_t* object, lv_color_t color, const lv_font_t* font);
const lv_font_t* Font22();
const lv_font_t* Font24();
const lv_font_t* Font28();
const lv_font_t* Font32();
const lv_font_t* Font36();
const lv_font_t* Font48();
const lv_font_t* MaterialIconFont32();
lv_obj_t* CreateLabel(
    lv_obj_t* parent, const char* text, lv_color_t color,
    const lv_font_t* font);
void MakeTransparent(lv_obj_t* object);
bool IsId(const char* left, const char* right);
lv_obj_t* CreateBox(lv_obj_t* parent, int width, int height, uint32_t color,
    lv_opa_t opacity, int radius);
bool IsObjectOrChildOf(lv_obj_t* object, lv_obj_t* parent);
lv_obj_t* CreateDivider(lv_obj_t* parent, int width);
void RestoreSettingsListGestures(SettingsViewState* state);
lv_obj_t* CreateToolbarButton(
    lv_obj_t* parent, int x, int y, lv_event_cb_t callback,
    SettingsViewState* state);
bool ShowMyDevicePage(SettingsViewState* state);
bool ShowWifiPage(SettingsViewState* state);

}  // namespace lilygo_box::ui