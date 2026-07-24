/*
 * @Description: Settings view internal helpers
 * @Author: LILYGO_L
 * @Date: 2026-05-23 00:00:00
 * @LastEditTime: 2026-07-19 11:17:26
 * @License: GPL 3.0
 */
#pragma once

#include <cstddef>
#include <cstdint>

#include "app/storage/wifi_storage.h"
#include "hal/providers/wifi_provider.h"
#include "lvgl.h"
#include "ui/input/edge_back_gesture.h"
#include "ui/theme/theme_provider.h"
#include "ui/views/app_view_config.h"
#include "ui/widgets/prompt/prompt_select_sheet.h"

namespace lilygo_box::hal {
class LvglPort;
}  // namespace lilygo_box::hal

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
constexpr uint32_t kBackgroundColor = theme::LightNeutralTheme().surface;
constexpr uint32_t kTitleColor = theme::LightNeutralTheme().on_surface;
constexpr uint32_t kPrimaryTextColor = theme::LightNeutralTheme().on_surface;
constexpr uint32_t kSecondaryTextColor =
    theme::LightNeutralTheme().on_surface_variant;
constexpr uint32_t kDividerColor = theme::LightNeutralTheme().outline_variant;
constexpr uint32_t kPressedColor = theme::LightNeutralTheme().state_layer;
constexpr lv_opa_t kPressedOpacity = 190;
constexpr int kDetailBackButtonSize = 62;
constexpr int kDetailBackButtonLeft = 18;
constexpr int kDetailBackButtonTop = 66;
constexpr int kDetailBackIconOffsetX = -4;
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
constexpr uint32_t kDetailBackgroundColor =
    theme::LightNeutralTheme().surface_container;
constexpr uint32_t kDetailCardColor =
    theme::LightNeutralTheme().surface_container_lowest;
constexpr uint32_t kDeviceInfoPressedColor =
    theme::LightNeutralTheme().state_layer;
constexpr uint32_t kDetailBlueColor = theme::LightNeutralTheme().action;
constexpr uint32_t kDetailBackColor = theme::LightNeutralTheme().on_surface;
constexpr uint32_t kDetailOptionPressedColor =
    theme::LightNeutralTheme().state_layer_strong;
constexpr lv_opa_t kDetailOptionPressedOpacity = LV_OPA_COVER;
constexpr int kNameEditButtonSize = kDetailBackButtonSize;
constexpr int kNameEditButtonTop = kDetailBackButtonTop;
constexpr int kNameEditButtonSide = kDetailBackButtonLeft;
constexpr int kNameEditTextAreaTop = 174;
constexpr int kNameEditTextAreaHeight = 88;
constexpr int kNameEditTextAreaSide = 26;
constexpr int kNameEditHelpTop =
    kNameEditTextAreaTop + kNameEditTextAreaHeight + 10;
constexpr int kNameEditKeyboardHeightPercent = 35;
constexpr int kWifiBodyTop = kDetailBodyTop;
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
constexpr uint32_t kWifiConnectTimeoutMs = 15 * 1000;
constexpr int kWifiConnectSheetRadius = 48;
constexpr int kWifiConnectSheetSideMargin = 34;
constexpr int kWifiConnectSheetBottomMargin = 32;
constexpr int kWifiConnectSheetInnerPadding = 32;
constexpr int kWifiConnectButtonGap = 20;
constexpr int kWifiConnectButtonHeight = 74;
constexpr int kWifiPasswordInputTop = 126;
constexpr int kWifiPasswordInputHeight = 78;
constexpr int kWifiPasswordKeyboardHeightPercent = 35;
constexpr size_t kWifiPasswordMinLength = 8;
constexpr size_t kWifiActionCapacity = hal::kMaxWifiScanNetworkCount * 2 + 6;
constexpr size_t kWifiSubPageStackCapacity = 4;
constexpr uint32_t kWifiBlueColor = theme::LightNeutralTheme().action;
constexpr uint32_t kWifiActionPressedColor =
    theme::LightNeutralTheme().action_pressed;
constexpr uint32_t kWifiConnectingColor =
    0xF5A623;
constexpr uint32_t kWifiCardColor =
    theme::LightNeutralTheme().surface_container_low;
constexpr uint32_t kWifiMutedColor = theme::LightNeutralTheme().outline;
constexpr uint32_t kWifiControlColor =
    theme::LightNeutralTheme().surface_container;
constexpr uint32_t kWifiConnectDisabledColor =
    theme::LightNeutralTheme().action_disabled;
constexpr uint32_t kWifiConnectSecondaryColor =
    theme::LightNeutralTheme().button_secondary;
constexpr uint32_t kWifiConnectSecondaryPressedColor =
    theme::LightNeutralTheme().button_secondary_pressed;
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

// 设置页运行状态，跨主列表和多个详情页共享。
struct SettingsViewState {
  AppViewConfig config;
  lv_obj_t* root = nullptr;
  lv_obj_t* detail_page = nullptr;
  lv_obj_t* firmware_update_page = nullptr;
  lv_obj_t* firmware_update_body = nullptr;
  lv_obj_t* firmware_update_current_page = nullptr;
  lv_obj_t* firmware_update_new_page = nullptr;
  lv_obj_t* firmware_update_scan_group = nullptr;
  lv_obj_t* firmware_update_scan_message_label = nullptr;
  lv_obj_t* firmware_update_scan_hint_label = nullptr;
  lv_obj_t* firmware_update_status_version_label = nullptr;
  lv_obj_t* firmware_update_status_log_button = nullptr;
  lv_obj_t* firmware_update_status_log_button_label = nullptr;
  lv_obj_t* firmware_update_page_indicator = nullptr;
  lv_obj_t* firmware_update_current_page_dot = nullptr;
  lv_obj_t* firmware_update_new_page_dot = nullptr;
  lv_obj_t* firmware_update_heading_label = nullptr;
  lv_obj_t* firmware_update_card = nullptr;
  lv_obj_t* firmware_update_release_label = nullptr;
  lv_obj_t* firmware_update_channel_label = nullptr;
  lv_obj_t* firmware_update_release_time_label = nullptr;
  lv_obj_t* firmware_update_components_title = nullptr;
  lv_obj_t* firmware_update_main_row = nullptr;
  lv_obj_t* firmware_update_main_chip_label = nullptr;
  lv_obj_t* firmware_update_main_version_label = nullptr;
  lv_obj_t* firmware_update_wireless_row = nullptr;
  lv_obj_t* firmware_update_wireless_chip_label = nullptr;
  lv_obj_t* firmware_update_wireless_version_label = nullptr;
  lv_obj_t* firmware_update_components_divider = nullptr;
  lv_obj_t* firmware_update_notes_title = nullptr;
  lv_obj_t* firmware_update_notes_label = nullptr;
  lv_obj_t* firmware_update_download_button = nullptr;
  lv_obj_t* firmware_update_progress_fill = nullptr;
  lv_obj_t* firmware_update_download_button_label = nullptr;
  lv_obj_t* firmware_update_pause_button = nullptr;
  lv_obj_t* firmware_update_pause_button_label = nullptr;
  lv_obj_t* firmware_update_cancel_button = nullptr;
  lv_obj_t* firmware_update_cancel_button_label = nullptr;
  lv_obj_t* firmware_update_spinner = nullptr;
  lv_obj_t* firmware_update_log_page = nullptr;
  lv_obj_t* firmware_update_log_body = nullptr;
  lv_obj_t* name_edit_page = nullptr;
  lv_obj_t* name_edit_text_area = nullptr;
  lv_obj_t* name_edit_keyboard = nullptr;
  lv_obj_t* factory_reset_page = nullptr;
  lv_obj_t* factory_reset_confirm_button = nullptr;
  lv_obj_t* factory_reset_confirm_label = nullptr;
  lv_obj_t* wifi_page = nullptr;
  lv_obj_t* wifi_sub_page = nullptr;
  // WLAN 二级页面栈，保持高级设置、已保存网络和网络详情的返回层级。
  lv_obj_t* wifi_sub_pages[kWifiSubPageStackCapacity] = {};
  lv_obj_t* wifi_modal_overlay = nullptr;
  lv_obj_t* wifi_modal_sheet = nullptr;
  lv_obj_t* wifi_password_text_area = nullptr;
  lv_obj_t* wifi_password_error_label = nullptr;
  lv_obj_t* wifi_password_keyboard = nullptr;
  lv_obj_t* wifi_connect_button = nullptr;
  lv_obj_t* wifi_connect_button_label = nullptr;
  lv_obj_t* auto_lock_value_label = nullptr;
  PromptSelectSheetState auto_lock_select_sheet = {};
  bool lock_screen_double_tap_to_turn_screen_on_and_off = true;
  lv_obj_t* screen_rotation_value_label = nullptr;
  PromptSelectSheetState screen_rotation_select_sheet = {};
  // 管理已保存网络页中等待确认删除的行对象。
  lv_obj_t* wifi_saved_delete_row = nullptr;
  // WLAN 页面内容容器，扫描或连接状态变化时会整体重建。
  lv_obj_t* wifi_body = nullptr;
  // 已连接卡片里的信号图标，RSSI 变化时可单独刷新。
  lv_obj_t* wifi_connected_signal_icon = nullptr;
  lv_obj_t* wifi_refresh_icon = nullptr;
  lv_obj_t* settings_extra_page = nullptr;
  lv_obj_t* settings_nested_page = nullptr;
  // WLAN 页面定时刷新器，用来轮询 HAL 扫描和连接状态。
  lv_timer_t* wifi_refresh_timer = nullptr;
  lv_timer_t* battery_refresh_timer = nullptr;
  lv_timer_t* factory_reset_countdown_timer = nullptr;
  lv_timer_t* firmware_update_refresh_timer = nullptr;
  lv_obj_t* battery_overview_fill = nullptr;
  lv_obj_t* battery_overview_time_label = nullptr;
  lv_obj_t* battery_overview_status_icon_label = nullptr;
  lv_obj_t* battery_overview_status_label = nullptr;
  lv_obj_t* battery_health_value_label = nullptr;
  lv_obj_t* battery_cycle_value_label = nullptr;
  lv_obj_t* device_name_value_label = nullptr;
  // 设置主页 WLAN 行右侧的 On/Off 文本。
  lv_obj_t* wifi_value_label = nullptr;
  // WLAN 列表行点击参数池，避免 LVGL 回调使用临时地址。
  WifiNetworkAction wifi_actions[kWifiActionCapacity] = {};
  // 已保存网络删除按钮参数池，不占用 WLAN 列表行点击参数。
  WifiNetworkAction wifi_saved_delete_actions[
      app::kWifiSavedNetworkCapacity] = {};
  // 当前弹窗正在处理的 WLAN，复制出来避免列表刷新后地址失效。
  WifiNetworkAction wifi_pending_action = {};
  EdgeBackSwipeState detail_swipe = {};
  EdgeBackSwipeState firmware_update_swipe = {};
  EdgeBackSwipeState firmware_update_log_swipe = {};
  EdgeBackSwipeState name_edit_swipe = {};
  EdgeBackSwipeState factory_reset_swipe = {};
  EdgeBackSwipeState wifi_swipe = {};
  EdgeBackSwipeState wifi_sub_swipe = {};
  EdgeBackSwipeState settings_extra_swipe = {};
  EdgeBackSwipeState settings_nested_swipe = {};
  size_t wifi_action_count = 0;
  size_t wifi_saved_delete_action_count = 0;
  // 当前 WLAN 二级页面栈深度，wifi_sub_page 始终指向栈顶页面。
  size_t wifi_sub_page_count = 0;
  // 上一次渲染的 WLAN 状态摘要，用来跳过重复刷新。
  uint32_t wifi_refresh_key = 0;
  bool detail_closing = false;
  bool firmware_update_closing = false;
  bool firmware_update_log_closing = false;
  int firmware_update_page_index = 0;
  // 检查完成并发现新版本后，自动切换到新版本详情页。
  bool firmware_update_auto_show_new_page = false;
  bool name_edit_closing = false;
  bool factory_reset_closing = false;
  bool factory_reset_started = false;
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
  // 驱动初始化完成后补发一次自动连接请求。
  // 标记下一次定时刷新必须重建 WLAN 页面。
  bool wifi_refresh_force = false;
  // WLAN 页面发起连接后等待连接结果。
  bool wifi_connect_waiting = false;
  // 用户确认连接后才允许失败卡片直接重试。
  bool wifi_connection_retry_ready = false;
  // 确认删除网络后是否关闭当前 WLAN 详情页。
  bool wifi_delete_close_sub_page = false;
  // 本次 WLAN 连接请求开始的 LVGL tick，单位为毫秒。
  uint32_t wifi_connect_started_ms = 0;
  bool bluetooth_enabled = false;
  bool hotspot_enabled = false;
  bool haptics_enabled = true;
  // 声音与振动页面中的振动强度控件容器。
  lv_obj_t* haptic_strength_controls = nullptr;
  bool battery_protection_enabled = true;
  int display_brightness_percent = 90;
  int audio_volume_percent = 90;
  int haptic_strength_percent = 90;
  int auto_lock_seconds = 5 * 60;
  int screen_rotation_angle = 0;
  int factory_reset_seconds_remaining = 0;
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
 * @brief 获取 64 号 Google Sans 字体
 * @return 字体指针
 */
const lv_font_t* Font64();

/**
 * @brief 获取 32 号 Material Symbols 字体
 * @return 字体指针
 */
const lv_font_t* MaterialIconFont32();

/**
 * @brief 获取 44 号 Material Symbols 字体
 * @return 字体指针
 */
const lv_font_t* MaterialIconFont44();

/**
 * @brief 获取 56 号 Material Symbols 字体
 * @return 字体指针
 */
const lv_font_t* MaterialIconFont56();

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
 * @brief 从长期 RAM 缓存读取已保存 WLAN 凭据和用户偏好
 * @param state 设置页状态
 * @param fallback_enabled 未保存开关状态时使用的默认 WLAN 开关状态
 */
void LoadWifiSettingsFromCache(
    SettingsViewState* state, bool fallback_enabled);

/**
 * @brief 按当前 WLAN 开关请求状态刷新设置主页 WLAN 行右侧文字
 * @param state 设置页状态
 */
void UpdateSettingsWifiValue(SettingsViewState* state);

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
 * @brief 从我的设备页面打开固件更新页面
 * @param state 设置页面状态
 * @return 打开成功返回 true，否则返回 false
 */
bool ShowFirmwareUpdatePage(SettingsViewState* state);

/**
 * @brief 关闭固件更新页面
 * @param state 设置页面状态
 * @param animated 是否播放关闭动画
 */
void CloseFirmwareUpdatePage(SettingsViewState* state, bool animated);

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
 * @brief 从设置主页打开更多设置详情页
 * @param state 设置页状态
 * @return 打开成功返回 true，否则返回 false
 */
bool ShowMoreSettingsPage(SettingsViewState* state);

/**
 * @brief 设置旋转后自动恢复的子页面 ID
 * @param page_id 子页面 ID（如 "display_brightness"），nullptr 表示清除
 */
void SetSettingsRestoreSubPage(const char* page_id);

/**
 * @brief 获取旋转后自动恢复的子页面 ID
 * @return 子页面 ID，nullptr 表示不需要恢复
 */
const char* GetSettingsRestoreSubPage();

/**
 * @brief 标记旋转恢复时是否需要跳过页面动画
 * @return true 表示跳过动画
 */
bool IsSkipPageAnimation();

/**
 * @brief 消耗旋转恢复跳过动画标志，读取并清除
 * @return true 表示需要跳过动画，且标志已清除
 */
bool ConsumeSkipPageAnimation();

/**
 * @brief 从设置主页打开显示与亮度详情页
 * @param state 设置页状态
 * @return 打开成功返回 true，否则返回 false
 */
bool ShowDisplayBrightnessPage(SettingsViewState* state);

/**
 * @brief 设置屏幕旋转使用的 LVGL 端口
 * @param port LVGL 端口指针
 */
void SetLvglPortForRotation(hal::LvglPort* port);

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
