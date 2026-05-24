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
constexpr uint32_t kWifiConnectTimeoutMs = 5 * 1000;
constexpr uint32_t kWifiConnectSettleMs = 1500;
constexpr int kWifiConnectSheetRadius = 48;
constexpr int kWifiConnectSheetSideMargin = 34;
constexpr int kWifiConnectSheetBottomMargin = 32;
constexpr int kWifiConnectSheetInnerPadding = 32;
constexpr int kWifiConnectButtonGap = 20;
constexpr int kWifiConnectButtonHeight = 74;
constexpr int kWifiPasswordInputHeight = 78;
constexpr int kWifiPasswordInputRadius = 30;
constexpr int kWifiPasswordKeyboardHeightPercent = 35;
constexpr size_t kWifiPasswordMinLength = 8;
constexpr size_t kWifiSavedNetworkCapacity = hal::kMaxWifiScanNetworkCount;
constexpr size_t kWifiActionCapacity = hal::kMaxWifiScanNetworkCount * 2 + 6;
constexpr uint32_t kWifiBlueColor = 0x4D82F5;
constexpr uint32_t kWifiCardColor = 0xF6F7F9;
constexpr uint32_t kWifiMutedColor = 0xA5A5AD;
constexpr uint32_t kWifiControlColor = 0xF0F1F3;
constexpr uint32_t kWifiConnectDisabledColor = 0xBFD7FB;
constexpr uint32_t kWifiConnectSecondaryColor = 0xF1F2F4;
constexpr uint32_t kNameEditInputColor = 0xF2F2F2;
constexpr uint32_t kNameEditInputBorderColor = 0x4A86F7;
constexpr const char* kDeviceNameAcceptedChars =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_. ";
constexpr const char* kWifiPasswordAcceptedChars =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz"
    "0123456789"
    " !@#$%^&*()-_=+[]{};:'\",.<>/?\\|`~";

struct SettingsViewState;

// WLAN 列表行的点击动作参数，LVGL 回调会读取这些稳定地址。
struct WifiNetworkAction {
  // 所属设置页状态，用于回调里发起连接请求。
  SettingsViewState* state = nullptr;
  // 网络名称副本，避免扫描列表重建后回调访问临时字符串。
  char ssid[hal::kWifiSsidMaxLength + 1] = {};
  // 连接密码副本，开放网络会保持为空字符串。
  char password[hal::kWifiPasswordMaxLength + 1] = {};
  // 热点是否需要密码，连接时决定弹出密码框还是确认框。
  bool secure = false;
  // 热点是否为 5 GHz 频段，用于详情页展示。
  bool is_5g = false;
  // 热点信号强度，用于详情页展示信号等级。
  int rssi = 0;
  // 是否为已保存网络，用于管理已保存网络页面。
  bool saved = false;
};

// WLAN 已保存网络凭据，保存用户确认连接后的 SSID 与连接元数据。
struct WifiSavedNetwork {
  // 已保存热点 SSID，用来在 Saved WLAN 与附近 WLAN 间去重。
  char ssid[hal::kWifiSsidMaxLength + 1] = {};
  // 用户确认连接时输入的密码，开放网络保持为空字符串。
  char password[hal::kWifiPasswordMaxLength + 1] = {};
  // 热点是否需要密码，用于重连和详情页安全性显示。
  bool secure = false;
  // 热点是否位于 5 GHz 频段，只使用扫描结果或连接状态更新。
  bool is_5g = false;
  // 最近一次已知 RSSI，用于 Saved WLAN 行和详情页展示。
  int rssi = 0;
};

// 设置页运行状态，跨主列表和多个详情页共享。
struct SettingsViewState {
  AppViewConfig config;
  lv_obj_t* root = nullptr;
  lv_obj_t* detail_page = nullptr;
  lv_obj_t* name_edit_page = nullptr;
  lv_obj_t* name_edit_text_area = nullptr;
  lv_obj_t* name_edit_keyboard = nullptr;
  lv_obj_t* wifi_page = nullptr;
  lv_obj_t* wifi_sub_page = nullptr;
  lv_obj_t* wifi_modal_overlay = nullptr;
  lv_obj_t* wifi_modal_sheet = nullptr;
  lv_obj_t* wifi_password_text_area = nullptr;
  lv_obj_t* wifi_password_keyboard = nullptr;
  lv_obj_t* wifi_connect_button = nullptr;
  lv_obj_t* wifi_connect_button_label = nullptr;
  // WLAN 页面内容容器，扫描或连接状态变化时会整体重建。
  lv_obj_t* wifi_body = nullptr;
  // 已连接卡片里的信号图标，RSSI 变化时可单独刷新。
  lv_obj_t* wifi_connected_signal_icon = nullptr;
  lv_obj_t* wifi_refresh_icon = nullptr;
  lv_obj_t* settings_extra_page = nullptr;
  lv_obj_t* settings_nested_page = nullptr;
  // WLAN 页面定时刷新器，用来轮询 HAL 扫描和连接状态。
  lv_timer_t* wifi_refresh_timer = nullptr;
  lv_obj_t* device_name_value_label = nullptr;
  // WLAN 列表行点击参数池，避免 LVGL 回调使用临时地址。
  WifiNetworkAction wifi_actions[kWifiActionCapacity] = {};
  // 当前弹窗正在处理的 WLAN，复制出来避免列表刷新后地址失效。
  WifiNetworkAction wifi_pending_action = {};
  EdgeBackSwipeState detail_swipe = {};
  EdgeBackSwipeState name_edit_swipe = {};
  EdgeBackSwipeState wifi_swipe = {};
  EdgeBackSwipeState wifi_sub_swipe = {};
  EdgeBackSwipeState settings_extra_swipe = {};
  EdgeBackSwipeState settings_nested_swipe = {};
  size_t wifi_action_count = 0;
  // 上一次渲染的 WLAN 状态摘要，用来跳过重复刷新。
  uint32_t wifi_refresh_key = 0;
  bool detail_closing = false;
  bool name_edit_closing = false;
  bool wifi_closing = false;
  bool wifi_sub_closing = false;
  bool settings_extra_closing = false;
  bool settings_nested_closing = false;
  // 用户期望的 WLAN 开关状态，硬件初始化期间保持 UI 一致。
  bool wifi_enabled_requested = false;
  // 驱动初始化完成后补发一次 WLAN 扫描请求。
  bool wifi_scan_on_ready = false;
  // 本次自动扫描请求发起前的扫描结果版本号。
  uint32_t wifi_scan_request_generation = 0;
  // 标记下一次定时刷新必须重建 WLAN 页面。
  bool wifi_refresh_force = false;
  // WLAN 页面发起连接后等待连接结果。
  bool wifi_connect_waiting = false;
  // 用户确认连接后才允许失败卡片直接重试。
  bool wifi_connection_retry_ready = false;
  // 本次 WLAN 连接请求开始的 LVGL tick，单位为毫秒。
  uint32_t wifi_connect_started_ms = 0;
  // 上次连接命令失败后的短暂等待窗口，避免连续点击重复发命令。
  uint32_t wifi_connect_block_until_ms = 0;
  bool bluetooth_enabled = false;
  bool hotspot_enabled = false;
  bool haptics_enabled = true;
  bool battery_protection_enabled = true;
  int display_brightness_percent = 70;
  int audio_volume_percent = 60;
  int haptic_strength_percent = 45;
  int auto_lock_minutes = 5;
  char wifi_auto_connect_ssid[hal::kWifiSsidMaxLength + 1] = {};
};

/**
 * @brief 设置文本对象的颜色和字体
 * @param object LVGL 对象
 * @param color 文本颜色
 * @param font 文本字体
 */
void SetTextStyle(lv_obj_t* object, lv_color_t color, const lv_font_t* font);

/**
 * @brief 获取 22 号 Google Sans 字体
 * @return 字体指针
 */
const lv_font_t* Font22();

/**
 * @brief 获取 24 号 Google Sans 字体
 * @return 字体指针
 */
const lv_font_t* Font24();

/**
 * @brief 获取 28 号 Google Sans 字体
 * @return 字体指针
 */
const lv_font_t* Font28();

/**
 * @brief 获取 32 号 Google Sans 字体
 * @return 字体指针
 */
const lv_font_t* Font32();

/**
 * @brief 获取 36 号 Google Sans 字体
 * @return 字体指针
 */
const lv_font_t* Font36();

/**
 * @brief 获取 48 号 Google Sans 字体
 * @return 字体指针
 */
const lv_font_t* Font48();

/**
 * @brief 获取 32 号 Material Symbols 字体
 * @return 字体指针
 */
const lv_font_t* MaterialIconFont32();

/**
 * @brief 创建文本标签
 * @param parent 父对象
 * @param text 文本内容
 * @param color 文本颜色
 * @param font 文本字体
 * @return 创建成功返回对象指针，否则返回 nullptr
 */
lv_obj_t* CreateLabel(
    lv_obj_t* parent, const char* text, lv_color_t color,
    const lv_font_t* font);

/**
 * @brief 设置对象为透明背景
 * @param object LVGL 对象
 */
void MakeTransparent(lv_obj_t* object);

/**
 * @brief 判断两个 ID 字符串是否相同
 * @param left 左侧 ID
 * @param right 右侧 ID
 * @return 相同返回 true，否则返回 false
 */
bool IsId(const char* left, const char* right);

/**
 * @brief 创建基础容器对象
 * @param parent 父对象
 * @param width 对象宽度
 * @param height 对象高度
 * @param color 背景颜色
 * @param opacity 背景透明度
 * @param radius 圆角半径
 * @return 创建成功返回对象指针，否则返回 nullptr
 */
lv_obj_t* CreateBox(lv_obj_t* parent, int width, int height, uint32_t color,
    lv_opa_t opacity, int radius);

/**
 * @brief 判断对象是否为指定父对象或其子对象
 * @param object 待判断对象
 * @param parent 目标父对象
 * @return 是目标对象或子对象返回 true，否则返回 false
 */
bool IsObjectOrChildOf(lv_obj_t* object, lv_obj_t* parent);

/**
 * @brief 创建分组分割线
 * @param parent 父对象
 * @param width 分割线宽度
 * @return 创建成功返回对象指针，否则返回 nullptr
 */
lv_obj_t* CreateDivider(lv_obj_t* parent, int width);

/**
 * @brief 恢复设置主页的 launcher 手势
 * @param state 设置页状态
 */
void RestoreSettingsListGestures(SettingsViewState* state);

/**
 * @brief 创建透明工具按钮
 * @param parent 父对象
 * @param x X 坐标
 * @param y Y 坐标
 * @param callback 点击回调
 * @param state 设置页状态
 * @return 创建成功返回按钮对象，否则返回 nullptr
 */
lv_obj_t* CreateToolbarButton(
    lv_obj_t* parent, int x, int y, lv_event_cb_t callback,
    SettingsViewState* state);

/**
 * @brief 从设置主页打开我的设备详情页
 * @param state 设置页状态
 * @return 打开成功返回 true，否则返回 false
 */
bool ShowMyDevicePage(SettingsViewState* state);

/**
 * @brief 从设置主页打开 WLAN 详情页
 * @param state 设置页状态
 * @return 打开成功返回 true，否则返回 false
 */
bool ShowWifiPage(SettingsViewState* state);

/**
 * @brief 从设置主页打开蓝牙详情页
 * @param state 设置页状态
 * @return 打开成功返回 true，否则返回 false
 */
bool ShowBluetoothPage(SettingsViewState* state);

/**
 * @brief 从设置主页打开个人热点详情页
 * @param state 设置页状态
 * @return 打开成功返回 true，否则返回 false
 */
bool ShowPersonalHotspotPage(SettingsViewState* state);

/**
 * @brief 从设置主页打开锁屏详情页
 * @param state 设置页状态
 * @return 打开成功返回 true，否则返回 false
 */
bool ShowLockScreenPage(SettingsViewState* state);

/**
 * @brief 从设置主页打开显示与亮度详情页
 * @param state 设置页状态
 * @return 打开成功返回 true，否则返回 false
 */
bool ShowDisplayBrightnessPage(SettingsViewState* state);

/**
 * @brief 从设置主页打开声音与触感详情页
 * @param state 设置页状态
 * @return 打开成功返回 true，否则返回 false
 */
bool ShowSoundHapticsPage(SettingsViewState* state);

/**
 * @brief 从设置主页打开省电与电池详情页
 * @param state 设置页状态
 * @return 打开成功返回 true，否则返回 false
 */
bool ShowPowerBatteryPage(SettingsViewState* state);

}  // namespace lilygo_box::ui
