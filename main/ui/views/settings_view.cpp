/*
 * @Description: None
 * @Author: LILYGO_L
 * @Date: 2026-05-18 09:20:00
 * @LastEditTime: 2026-05-18 17:35:45
 * @License: GPL 3.0
 */
#include "ui/views/settings_view.h"

#include <cstddef>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <new>

#include "app/device_identity.h"
#include "app/device_info_snapshot.h"
#include "app/settings_catalog.h"
#include "hal/providers/screen_provider.h"
#include "hal/providers/wifi_provider.h"
#include "ui/animation/transition_animation.h"
#include "ui/input/app_view_gesture_flags.h"
#include "ui/input/edge_back_gesture.h"
#include "ui/input/press_cancel.h"
#include "ui/font/font_assets.h"
#include "ui/font/material_symbols_assets.h"
#include "ui/widgets/shared_keyboard.h"

namespace lilygo_box::ui {
namespace {

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

// 设置页面运行状态
struct SettingsViewState;

struct WifiNetworkAction {
  SettingsViewState* state = nullptr;
  char ssid[hal::kWifiSsidMaxLength + 1] = {};
  char password[hal::kWifiPasswordMaxLength + 1] = {};
};

struct SettingsViewState {
  AppViewConfig config;
  lv_obj_t* root = nullptr;
  lv_obj_t* detail_page = nullptr;
  lv_obj_t* name_edit_page = nullptr;
  lv_obj_t* name_edit_text_area = nullptr;
  lv_obj_t* name_edit_keyboard = nullptr;
  lv_obj_t* wifi_page = nullptr;
  // WiFi 页面内容容器，扫描或连接状态变化时会重建。
  lv_obj_t* wifi_body = nullptr;
  // 已连接卡片中的信号图标，RSSI 变化时单独更新。
  lv_obj_t* wifi_connected_signal_icon = nullptr;
  lv_obj_t* wifi_refresh_icon = nullptr;
  // WiFi 页面定时刷新器，用来轮询 HAL 扫描和连接状态。
  lv_timer_t* wifi_refresh_timer = nullptr;
  lv_obj_t* device_name_value_label = nullptr;
  // WiFi 列表行点击参数池，避免 LVGL 回调使用临时地址。
  WifiNetworkAction wifi_actions[kWifiActionCapacity] = {};
  EdgeBackSwipeState detail_swipe = {};
  EdgeBackSwipeState name_edit_swipe = {};
  EdgeBackSwipeState wifi_swipe = {};
  size_t wifi_action_count = 0;
  // 上一次渲染的 WiFi 状态摘要，用来跳过重复刷新。
  uint32_t wifi_refresh_key = 0;
  bool detail_closing = false;
  bool name_edit_closing = false;
  bool wifi_closing = false;
  // 用户期望的 WiFi 开关状态，硬件初始化期间保持 UI 一致。
  bool wifi_enabled_requested = false;
  // 驱动初始化完成后补发一次 WiFi 扫描请求。
  bool wifi_scan_on_ready = false;
  // 本次自动扫描请求发起前的扫描结果版本号。
  uint32_t wifi_scan_request_generation = 0;
  // 标记下一次定时刷新必须重建 WiFi 页面。
  bool wifi_refresh_force = false;
};

// 设置入口图标样式。
struct SettingsIconStyle {
  const char* symbol = nullptr;
  uint32_t color = 0x3F82F6;
};


/**
 * @brief 显示设备名称编辑页
 * @param state 设置页面状态
 * @return 显示成功返回 true，否则返回 false
 */
bool ShowDeviceNameEditPage(SettingsViewState* state);

/**
 * @brief 显示 WLAN 详情页并绑定 WiFi HAL
 * @param state 设置页状态
 * @return 显示成功返回 true，否则返回 false
 */
bool ShowWifiPage(SettingsViewState* state);

/**
 * @brief 关闭设备名称编辑页
 * @param state 设置页面状态
 * @param animated 是否播放关闭动画
 */
void CloseDeviceNameEditPage(SettingsViewState* state, bool animated);

/**
 * @brief 关闭 WLAN 详情页并释放刷新定时器
 * @param state 设置页状态
 * @param animated 是否播放关闭动画
 */
void CloseWifiPage(SettingsViewState* state, bool animated);

/**
 * @brief 处理设备名称行点击事件
 * @param event LVGL 事件对象
 */
void DeviceNameRowClickedEventCallback(lv_event_t* event);

/**
 * @brief 处理设置主页 WLAN 行点击
 * @param event LVGL 事件对象
 */
void WifiRowClickedEventCallback(lv_event_t* event);

/**
 * @brief WiFi 状态或扫描数据变化时重建 WLAN 页面
 * @param state 设置页状态
 * @param force 是否忽略状态摘要并强制重建
 */
void RefreshWifiPage(SettingsViewState* state, bool force);
void UpdateWifiConnectedSignalIcon(SettingsViewState* state);
void StopWifiRefreshIconSpin(SettingsViewState* state);
void UpdateWifiRefreshAnimation(SettingsViewState* state);

/**
 * @brief 从配置的 WiFi Provider 读取连接和扫描快照
 * @param config app 页面配置
 * @param status WiFi 连接状态输出地址，可为 nullptr
 * @param scan_status WiFi 扫描状态输出地址，可为 nullptr
 */
void ReadWifiSnapshots(const AppViewConfig& config, hal::WifiStatus* status,
    hal::WifiScanStatus* scan_status);

/**
 * @brief 请求 HAL 执行 WiFi 扫描并标记 WLAN 页面刷新
 * @param state 设置页状态
 */
void RequestWifiScan(SettingsViewState* state);

/**
 * @brief WLAN 页面显示期间定时轮询 WiFi 状态
 * @param timer LVGL 定时器对象
 */
void WifiRefreshTimerCallback(lv_timer_t* timer);

/**
 * @brief 处理 WLAN 刷新按钮点击
 * @param event LVGL 事件对象
 */
void WifiRefreshButtonClickedEventCallback(lv_event_t* event);

/**
 * @brief 处理已保存或扫描到的 WiFi 热点行点击
 * @param event 携带 WifiNetworkAction 的 LVGL 事件对象
 */
void WifiNetworkClickedEventCallback(lv_event_t* event);

/**
 * @brief 设置文本对象的颜色和字体
 * @param object LVGL 对象
 * @param color 文本颜色
 * @param font 文本字体
 */
void SetTextStyle(lv_obj_t* object, lv_color_t color, const lv_font_t* font) {
  lv_obj_set_style_text_color(object, color, LV_PART_MAIN);
  lv_obj_set_style_text_font(object, font, LV_PART_MAIN);
}

/**
 * @brief 设置 WLAN 刷新图标旋转角度
 * @param object 刷新图标对象
 * @param value 旋转角度，单位为 0.1 度
 */
void SetWifiRefreshIconRotation(void* object, int32_t value) {
  if (object == nullptr) {
    return;
  }
  lv_obj_set_style_transform_rotation(
      static_cast<lv_obj_t*>(object), value % 3600, LV_PART_MAIN);
}

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

/**
 * @brief 获取 32 号 Google Sans 字体
 * @return 字体指针
 */
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
 * @brief 获取 32 号 Material Symbols 字体
 * @return 字体指针
 */
const lv_font_t* MaterialIconFont32() {
  return &lvgl_font_material_symbols_32;
}

/**
 * @brief 创建文本标签
 * @param parent 父对象
 * @param text 文本内容
 * @param color 文本颜色
 * @param font 文本字体
 * @return 创建成功返回对象指针，否则返回 nullptr
 */
lv_obj_t* CreateLabel(lv_obj_t* parent, const char* text, lv_color_t color,
    const lv_font_t* font) {
  lv_obj_t* label = lv_label_create(parent);
  if (label == nullptr) {
    return nullptr;
  }

  lv_label_set_text(label, text);
  lv_obj_add_flag(label, LV_OBJ_FLAG_GESTURE_BUBBLE);
  SetTextStyle(label, color, font);
  return label;
}

/**
 * @brief 设置对象为透明背景
 * @param object LVGL 对象
 */
void MakeTransparent(lv_obj_t* object) {
  lv_obj_set_style_bg_opa(object, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_set_style_border_width(object, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(object, 0, LV_PART_MAIN);
}

/**
 * @brief 判断两个 ID 字符串是否相同
 * @param left 左侧 ID
 * @param right 右侧 ID
 * @return 相同返回 true，否则返回 false
 */
bool IsId(const char* left, const char* right) {
  if (left == nullptr || right == nullptr) {
    return false;
  }
  return std::strcmp(left, right) == 0;
}

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
    lv_opa_t opacity, int radius) {
  lv_obj_t* object = lv_obj_create(parent);
  if (object == nullptr) {
    return nullptr;
  }

  lv_obj_remove_flag(object, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(object, LV_OBJ_FLAG_GESTURE_BUBBLE);
  lv_obj_set_size(object, width, height);
  lv_obj_set_style_bg_color(object, lv_color_hex(color), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(object, opacity, LV_PART_MAIN);
  lv_obj_set_style_border_width(object, 0, LV_PART_MAIN);
  lv_obj_set_style_radius(object, radius, LV_PART_MAIN);
  lv_obj_set_style_pad_all(object, 0, LV_PART_MAIN);
  return object;
}

/**
 * @brief 判断对象是否为指定父对象或其子对象
 * @param object 待判断对象
 * @param parent 目标父对象
 * @return 是目标对象或子对象返回 true，否则返回 false
 */
bool IsObjectOrChildOf(lv_obj_t* object, lv_obj_t* parent) {
  while (object != nullptr) {
    if (object == parent) {
      return true;
    }
    object = lv_obj_get_parent(object);
  }
  return false;
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
 * @brief 创建分组分割线
 * @param parent 父对象
 * @param width 分割线宽度
 * @return 创建成功返回对象指针，否则返回 nullptr
 */
lv_obj_t* CreateDivider(lv_obj_t* parent, int width) {
  lv_obj_t* divider = lv_obj_create(parent);
  if (divider == nullptr) {
    return nullptr;
  }

  lv_obj_remove_flag(divider, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(divider, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_size(divider, width, kDividerHeight);
  lv_obj_set_style_bg_color(
      divider, lv_color_hex(kDividerColor), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(divider, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_border_width(divider, 0, LV_PART_MAIN);
  lv_obj_set_style_radius(divider, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(divider, 0, LV_PART_MAIN);
  return divider;
}

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
      return {.symbol = icon::kWifi, .color = 0x3F82F6};
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
    case app::SettingsIcon::kFolder:
      return {.symbol = icon::kFolder, .color = 0xF05B34};
    case app::SettingsIcon::kWarning:
      return {.symbol = icon::kWarning, .color = 0x3F82F6};
    case app::SettingsIcon::kVolumeUp:
      return {.symbol = icon::kVolumeUp, .color = 0x3F82F6};
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
 * @brief 恢复设置列表页面的 launcher 手势
 * @param state 设置页面状态
 */
void RestoreSettingsListGestures(SettingsViewState* state) {
  if (state == nullptr || state->root == nullptr) {
    return;
  }

  lv_obj_remove_flag(state->root, kBlockLauncherGestureFlag);
  lv_obj_add_flag(state->root, LV_OBJ_FLAG_GESTURE_BUBBLE);
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
      state->detail_closing) {
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
 * @brief WLAN 页面关闭动画完成后执行清理
 * @param animation LVGL 动画对象
 */
void WifiCloseCompletedCallback(lv_anim_t* animation) {
  auto* state =
      static_cast<SettingsViewState*>(lv_anim_get_user_data(animation));
  if (state == nullptr || state->wifi_page == nullptr) {
    return;
  }

  lv_obj_t* page = state->wifi_page;
  StopWifiRefreshIconSpin(state);
  state->wifi_page = nullptr;
  state->wifi_body = nullptr;
  state->wifi_connected_signal_icon = nullptr;
  state->wifi_refresh_icon = nullptr;
  state->wifi_closing = false;
  state->wifi_swipe = EdgeBackSwipeState();
  if (state->wifi_refresh_timer != nullptr) {
    lv_timer_delete(state->wifi_refresh_timer);
    state->wifi_refresh_timer = nullptr;
  }
  lv_obj_delete(page);
  RestoreSettingsListGestures(state);
}

/**
 * @brief 关闭 WLAN 详情页并释放轮询资源
 * @param state 设置页状态
 * @param animated 是否播放向右滑出的关闭动画
 */
void CloseWifiPage(SettingsViewState* state, bool animated) {
  if (state == nullptr || state->wifi_page == nullptr ||
      state->wifi_closing) {
    return;
  }

  if (state->wifi_refresh_timer != nullptr) {
    lv_timer_delete(state->wifi_refresh_timer);
    state->wifi_refresh_timer = nullptr;
  }

  if (animated &&
      StartSlideRightWindowTransition(state->wifi_page, state->config.width,
          kDetailSlideAnimationMs, state, WifiCloseCompletedCallback)) {
    state->wifi_closing = true;
    return;
  }

  lv_obj_t* page = state->wifi_page;
  StopWifiRefreshIconSpin(state);
  state->wifi_page = nullptr;
  state->wifi_body = nullptr;
  state->wifi_connected_signal_icon = nullptr;
  state->wifi_refresh_icon = nullptr;
  state->wifi_closing = false;
  state->wifi_swipe = EdgeBackSwipeState();
  if (state->wifi_refresh_timer != nullptr) {
    lv_timer_delete(state->wifi_refresh_timer);
    state->wifi_refresh_timer = nullptr;
  }
  lv_obj_delete(page);
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
      state->name_edit_closing || state->config.screen == nullptr ||
      !HandleEdgeBackSwipeEvent(event, state->config.screen->ScreenWidth(),
          &state->detail_swipe)) {
    return;
  }

  CloseMyDevicePage(state, true);
  lv_event_stop_bubbling(event);
  lv_event_stop_processing(event);
}

/**
 * @brief 处理 WLAN 页面顶部返回按钮
 * @param event LVGL 事件对象
 */
void WifiBackClickedEventCallback(lv_event_t* event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
    return;
  }

  CloseWifiPage(
      static_cast<SettingsViewState*>(lv_event_get_user_data(event)), true);
}

/**
 * @brief 处理 WLAN 页面边缘返回手势
 * @param event LVGL 事件对象
 */
void WifiEdgeBackEventCallback(lv_event_t* event) {
  auto* state = static_cast<SettingsViewState*>(lv_event_get_user_data(event));
  if (state == nullptr || state->wifi_page == nullptr ||
      state->wifi_closing || state->config.screen == nullptr ||
      !HandleEdgeBackSwipeEvent(event, state->config.screen->ScreenWidth(),
          &state->wifi_swipe)) {
    return;
  }

  CloseWifiPage(state, true);
  lv_event_stop_bubbling(event);
  lv_event_stop_processing(event);
}

/**
 * @brief 通过 WLAN 开关启动扫描或关闭 WiFi
 * @param event LVGL 事件对象
 */
void WifiSwitchValueChangedEventCallback(lv_event_t* event) {
  if (lv_event_get_code(event) != LV_EVENT_VALUE_CHANGED) {
    return;
  }

  auto* state = static_cast<SettingsViewState*>(lv_event_get_user_data(event));
  lv_obj_t* target = lv_event_get_target_obj(event);
  if (state == nullptr || target == nullptr || state->config.wifi == nullptr) {
    return;
  }

  if (lv_obj_has_state(target, LV_STATE_CHECKED)) {
    state->wifi_enabled_requested = true;
    RequestWifiScan(state);
  } else {
    state->wifi_enabled_requested = false;
    state->wifi_scan_on_ready = false;
    state->wifi_scan_request_generation = 0;
    state->config.wifi->StopWifi();
  }
  state->wifi_refresh_force = true;
}

/**
 * @brief 处理 WLAN 刷新按钮并请求重新扫描
 * @param event LVGL 事件对象
 */
void WifiRefreshButtonClickedEventCallback(lv_event_t* event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
    return;
  }

  auto* state = static_cast<SettingsViewState*>(lv_event_get_user_data(event));
  RequestWifiScan(state);
  if (state != nullptr) {
    UpdateWifiRefreshAnimation(state);
  }
}

/**
 * @brief 连接被点击行对应的 WiFi 热点
 * @param event 携带 WifiNetworkAction 的 LVGL 事件对象
 */
void WifiNetworkClickedEventCallback(lv_event_t* event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
    return;
  }

  auto* action =
      static_cast<WifiNetworkAction*>(lv_event_get_user_data(event));
  if (action == nullptr || action->state == nullptr ||
      action->state->config.wifi == nullptr || action->ssid[0] == '\0') {
    return;
  }

  action->state->config.wifi->ConnectWifi(action->ssid, action->password);
  action->state->wifi_refresh_force = true;
}

/**
 * @brief WLAN 页面打开期间轮询 WiFi 状态并刷新内容
 * @param timer LVGL 定时器对象
 */
void WifiRefreshTimerCallback(lv_timer_t* timer) {
  auto* state = static_cast<SettingsViewState*>(lv_timer_get_user_data(timer));
  if (state != nullptr && state->wifi_enabled_requested &&
      state->wifi_scan_on_ready) {
    hal::WifiStatus status;
    hal::WifiScanStatus scan_status;
    ReadWifiSnapshots(state->config, &status, &scan_status);
    if (scan_status.scan_running ||
        scan_status.generation != state->wifi_scan_request_generation) {
      state->wifi_scan_on_ready = false;
    } else if (!status.init_task_running) {
      if (state->config.wifi != nullptr) {
        state->config.wifi->StartWifiScan();
      }
    }
  }
  RefreshWifiPage(state, false);
  UpdateWifiRefreshAnimation(state);
  UpdateWifiConnectedSignalIcon(state);
}

/**
 * @brief 处理设备名称编辑页边缘返回手势
 * @param event LVGL 事件对象
 */
void DeviceNameEditSwipeEventCallback(lv_event_t* event) {
  auto* state = static_cast<SettingsViewState*>(lv_event_get_user_data(event));
  if (state == nullptr || state->name_edit_page == nullptr ||
      state->name_edit_closing || state->config.screen == nullptr ||
      !HandleEdgeBackSwipeEvent(event, state->config.screen->ScreenWidth(),
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
 * @brief 创建透明工具按钮
 * @param parent 父对象
 * @param x X 坐标
 * @param y Y 坐标
 * @param callback 点击回调
 * @param state 设置页面状态
 * @return 创建成功返回按钮对象，否则返回 nullptr
 */
lv_obj_t* CreateToolbarButton(lv_obj_t* parent, int x, int y,
    lv_event_cb_t callback, SettingsViewState* state) {
  lv_obj_t* button = lv_button_create(parent);
  if (button == nullptr) {
    return nullptr;
  }

  lv_obj_remove_flag(button, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(button, LV_OBJ_FLAG_PRESS_LOCK);
  lv_obj_add_flag(button, LV_OBJ_FLAG_GESTURE_BUBBLE);
  lv_obj_set_size(button, kNameEditButtonSize, kNameEditButtonSize);
  lv_obj_set_pos(button, x, y);
  lv_obj_set_style_bg_opa(button, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_set_style_bg_color(
      button, lv_color_hex(kPressedColor), LV_STATE_PRESSED);
  lv_obj_set_style_bg_opa(button, kPressedOpacity, LV_STATE_PRESSED);
  lv_obj_set_style_border_width(button, 0, LV_PART_MAIN);
  lv_obj_set_style_radius(button, kNameEditButtonSize / 2, LV_PART_MAIN);
  lv_obj_set_style_pad_all(button, 0, LV_PART_MAIN);
  if (!AddPressCancelOnLeave(button)) {
    return nullptr;
  }
  lv_obj_add_event_cb(button, callback, LV_EVENT_CLICKED, state);
  return button;
}

/**
 * @brief 创建取消图标
 * @param parent 父对象
 * @return 创建成功返回 true，否则返回 false
 */
bool CreateCloseIcon(lv_obj_t* parent) {
  lv_obj_t* icon = CreateLabel(parent, icon::kClose,
      lv_color_hex(kDetailBackColor), MaterialIconFont32());
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
      lv_color_hex(kDetailBackColor), MaterialIconFont32());
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
  lv_obj_remove_flag(back_button, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(back_button, LV_OBJ_FLAG_PRESS_LOCK);
  lv_obj_add_flag(back_button, LV_OBJ_FLAG_GESTURE_BUBBLE);
  lv_obj_set_size(back_button, kDetailBackButtonSize, kDetailBackButtonSize);
  lv_obj_set_pos(back_button, kDetailBackButtonLeft, kDetailBackButtonTop);
  lv_obj_set_style_bg_opa(back_button, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_set_style_bg_color(
      back_button, lv_color_hex(kPressedColor), LV_STATE_PRESSED);
  lv_obj_set_style_bg_opa(back_button, kPressedOpacity, LV_STATE_PRESSED);
  lv_obj_set_style_border_width(back_button, 0, LV_PART_MAIN);
  lv_obj_set_style_radius(
      back_button, kDetailBackButtonSize / 2, LV_PART_MAIN);
  lv_obj_set_style_pad_all(back_button, 0, LV_PART_MAIN);
  if (!AddPressCancelOnLeave(back_button)) {
    return false;
  }
  lv_obj_add_event_cb(
      back_button, MyDeviceBackClickedEventCallback, LV_EVENT_CLICKED, state);

  lv_obj_t* back_icon = CreateLabel(
      back_button, icon::kArrowBack, lv_color_hex(kDetailBackColor),
      MaterialIconFont32());
  if (back_icon == nullptr) {
    return false;
  }
  lv_obj_center(back_icon);
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

  lv_obj_t* update_button = CreateBox(parent, kDetailUpdateWidth,
      kDetailUpdateHeight, kDetailBlueColor, LV_OPA_COVER,
      kDetailUpdateHeight / 3);
  if (update_button == nullptr) {
    return false;
  }
  lv_obj_add_flag(update_button, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_flag(update_button, LV_OBJ_FLAG_GESTURE_BUBBLE);
  lv_obj_align(update_button, LV_ALIGN_TOP_MID, 0, kDetailUpdateTop);
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
 * @return 创建成功返回 true，否则返回 false
 */
bool CreateDeviceOptionRow(
    lv_obj_t* parent, const char* text, int y, int width) {
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
 * @return 创建成功返回 true，否则返回 false
 */
bool CreateDeviceOptions(lv_obj_t* parent, int width) {
  const app::SettingsDeviceOptionCatalog& catalog =
      app::GetSettingsDeviceOptionCatalog();
  int y = kDetailSecondCardTop + kDetailSecondCardHeight + kDetailOptionTopGap;
  for (size_t i = 0; i < catalog.option_count; ++i) {
    if (!CreateDeviceOptionRow(parent, catalog.options[i].title, y, width)) {
      return false;
    }
    y += kDetailOptionRowHeight;
  }
  return true;
}

/**
 * @brief 判断 WLAN 页面开关是否应显示为打开
 * @param status 当前 WiFi 连接状态
 * @param scan_status 当前 WiFi 扫描状态
 * @return WiFi 正在启动、运行、连接或扫描时返回 true
 */
bool IsWifiPageEnabled(
    const hal::WifiStatus& status, const hal::WifiScanStatus& scan_status) {
  return status.running || status.connected || status.got_ip ||
         status.init_task_running || scan_status.scan_running;
}

/**
 * @brief 读取连接卡片上显示的 SSID 文本
 * @param status 当前 WiFi 连接状态
 * @param buffer 文本输出缓冲区
 * @param buffer_size 文本输出缓冲区大小
 */
void ReadWifiPageSsid(const hal::WifiStatus& status, char* buffer,
    size_t buffer_size) {
  if (buffer == nullptr || buffer_size == 0) {
    return;
  }
  std::snprintf(buffer, buffer_size, "%s", kWifiSavedSsid5G);

  if (status.ssid[0] == '\0') {
    return;
  }
  std::snprintf(buffer, buffer_size, "%s", status.ssid);
}

/**
 * @brief 判断 SSID 是否为固化的已保存测试热点
 * @param ssid 待判断的热点名称
 * @return 是已保存测试热点返回 true，否则返回 false
 */
bool IsSavedWifiSsid(const char* ssid) {
  if (ssid == nullptr) {
    return false;
  }
  return std::strcmp(ssid, kWifiSavedSsid) == 0 ||
         std::strcmp(ssid, kWifiSavedSsid5G) == 0;
}

/**
 * @brief 根据 RSSI 计算 WiFi 信号等级
 * @param rssi 信号强度，单位为 dBm
 * @return 1 到 4 的信号等级
 */
int WifiSignalLevelForRssi(int rssi) {
  if (rssi >= -55) {
    return 4;
  }
  if (rssi >= -67) {
    return 3;
  }
  if (rssi >= -75) {
    return 2;
  }
  return 1;
}

/**
 * @brief 根据 RSSI 获取 WiFi 信号图标文本
 * @param rssi 信号强度，单位为 dBm
 * @return Material Symbols 图标文本
 */
const char* WifiSignalIconForRssi(int rssi) {
  switch (WifiSignalLevelForRssi(rssi)) {
    case 4:
      return icon::kAndroidWifi4Bar;
    case 3:
      return icon::kAndroidWifi3Bar;
    case 2:
      return icon::kWifi2Bar;
    default:
      return icon::kWifi1Bar;
  }
}

/**
 * @brief 生成用于判断 WLAN 页面可见状态是否变化的摘要
 * @param status 当前 WiFi 连接状态
 * @param scan_status 当前 WiFi 扫描状态
 * @return 当前可见状态摘要
 */
uint32_t MakeWifiRefreshKey(
    const hal::WifiStatus& status, const hal::WifiScanStatus& scan_status) {
  uint32_t key = scan_status.generation * 131U;
  key ^= static_cast<uint32_t>(scan_status.network_count) << 16;
  key ^= scan_status.scan_failed ? 0x0002U : 0U;
  key ^= status.init_task_running ? 0x0004U : 0U;
  key ^= status.running ? 0x0008U : 0U;
  key ^= status.connected ? 0x0010U : 0U;
  key ^= status.got_ip ? 0x0020U : 0U;
  key ^= status.start_failed ? 0x0040U : 0U;
  key ^= static_cast<uint32_t>(status.disconnect_reason & 0xFF) << 8;
  return key;
}

/**
 * @brief 读取 WiFi 连接和扫描快照，读取失败时填充默认值
 * @param config app 页面配置
 * @param status WiFi 状态输出地址，可为 nullptr
 * @param scan_status WiFi 扫描状态输出地址，可为 nullptr
 */
void ReadWifiSnapshots(const AppViewConfig& config, hal::WifiStatus* status,
    hal::WifiScanStatus* scan_status) {
  if (status != nullptr) {
    *status = hal::WifiStatus();
  }
  if (scan_status != nullptr) {
    *scan_status = hal::WifiScanStatus();
  }
  if (config.wifi == nullptr) {
    return;
  }
  if (status != nullptr) {
    config.wifi->ReadWifiStatus(status);
  }
  if (scan_status != nullptr) {
    config.wifi->ReadWifiScanStatus(scan_status);
  }
}

/**
 * @brief 判断 WLAN 刷新动画是否应该保持运行
 * @param state 设置页状态
 * @param status WiFi 连接状态
 * @param scan_status WiFi 扫描状态
 * @return 需要显示刷新中返回 true，否则返回 false
 */
bool IsWifiRefreshActive(const SettingsViewState* state,
    const hal::WifiStatus& status, const hal::WifiScanStatus& scan_status) {
  return scan_status.scan_running || status.init_task_running ||
         (state != nullptr && state->wifi_scan_on_ready);
}

/**
 * @brief 启动 WLAN 刷新图标旋转动画
 * @param state 设置页状态
 */
void StartWifiRefreshIconSpin(SettingsViewState* state) {
  if (state == nullptr || state->wifi_refresh_icon == nullptr ||
      lv_anim_get(state->wifi_refresh_icon, SetWifiRefreshIconRotation) !=
          nullptr) {
    return;
  }

  lv_anim_t animation;
  lv_anim_init(&animation);
  lv_anim_set_var(&animation, state->wifi_refresh_icon);
  lv_anim_set_values(&animation, 0, 3600);
  lv_anim_set_duration(&animation, kWifiRefreshSpinMs);
  lv_anim_set_repeat_count(&animation, LV_ANIM_REPEAT_INFINITE);
  lv_anim_set_path_cb(&animation, lv_anim_path_linear);
  lv_anim_set_exec_cb(&animation, SetWifiRefreshIconRotation);
  lv_anim_start(&animation);
}

/**
 * @brief 停止 WLAN 刷新图标旋转动画并复位角度
 * @param state 设置页状态
 */
void StopWifiRefreshIconSpin(SettingsViewState* state) {
  if (state == nullptr || state->wifi_refresh_icon == nullptr) {
    return;
  }

  lv_anim_delete(state->wifi_refresh_icon, SetWifiRefreshIconRotation);
  SetWifiRefreshIconRotation(state->wifi_refresh_icon, 0);
}

/**
 * @brief 根据 WiFi 初始化和扫描状态刷新旋转动画
 * @param state 设置页状态
 */
void UpdateWifiRefreshAnimation(SettingsViewState* state) {
  if (state == nullptr || state->wifi_refresh_icon == nullptr) {
    return;
  }

  hal::WifiStatus status;
  hal::WifiScanStatus scan_status;
  ReadWifiSnapshots(state->config, &status, &scan_status);
  if (IsWifiRefreshActive(state, status, scan_status)) {
    StartWifiRefreshIconSpin(state);
  } else {
    StopWifiRefreshIconSpin(state);
  }
}

/**
 * @brief 刷新已连接 WiFi 卡片上的信号图标
 * @param state 设置页状态
 */
void UpdateWifiConnectedSignalIcon(SettingsViewState* state) {
  if (state == nullptr || state->wifi_connected_signal_icon == nullptr ||
      state->config.wifi == nullptr) {
    return;
  }

  hal::WifiStatus status;
  if (!state->config.wifi->ReadWifiStatus(&status) ||
      (!status.connected && !status.got_ip)) {
    return;
  }

  lv_label_set_text(
      state->wifi_connected_signal_icon, WifiSignalIconForRssi(status.rssi));
}

/**
 * @brief 请求 HAL 开始扫描并更新 UI 期望状态
 * @param state 设置页状态
 */
void RequestWifiScan(SettingsViewState* state) {
  if (state == nullptr || state->config.wifi == nullptr) {
    return;
  }

  state->wifi_enabled_requested = true;
  hal::WifiStatus status;
  hal::WifiScanStatus scan_status;
  ReadWifiSnapshots(state->config, &status, &scan_status);
  state->wifi_scan_on_ready = true;
  state->wifi_scan_request_generation = scan_status.generation;
  if (scan_status.scan_running) {
    state->wifi_scan_on_ready = false;
    return;
  }
  state->config.wifi->StartWifiScan();
}

/**
 * @brief 为 WiFi 热点行分配稳定的 LVGL 回调参数
 * @param state 设置页状态
 * @param ssid 热点 SSID
 * @param password 热点密码，开放热点可为空
 * @return 分配到的参数地址，参数池已满时返回 nullptr
 */
WifiNetworkAction* ReserveWifiNetworkAction(SettingsViewState* state,
    const char* ssid, const char* password) {
  if (state == nullptr || ssid == nullptr || ssid[0] == '\0' ||
      state->wifi_action_count >= kWifiActionCapacity) {
    return nullptr;
  }

  WifiNetworkAction* action = &state->wifi_actions[state->wifi_action_count++];
  *action = WifiNetworkAction();
  action->state = state;
  std::snprintf(action->ssid, sizeof(action->ssid), "%s", ssid);
  if (password != nullptr) {
    std::snprintf(action->password, sizeof(action->password), "%s", password);
  }
  return action;
}

/**
 * @brief 创建 WLAN 页面返回按钮和标题
 * @param parent 父对象
 * @param state 设置页状态
 * @param width 页面宽度
 * @return 创建成功返回 true，否则返回 false
 */
bool CreateWifiHeader(lv_obj_t* parent, SettingsViewState* state, int) {
  lv_obj_t* back_button = CreateToolbarButton(parent, kDetailBackButtonLeft,
      kDetailBackButtonTop, WifiBackClickedEventCallback, state);
  if (back_button == nullptr) {
    return false;
  }
  lv_obj_t* back_icon = CreateLabel(back_button, icon::kArrowBack,
      lv_color_hex(kDetailBackColor), MaterialIconFont32());
  if (back_icon == nullptr) {
    return false;
  }
  lv_obj_center(back_icon);

  lv_obj_t* title =
      CreateLabel(parent, "WLAN", lv_color_hex(kTitleColor), Font48());
  if (title == nullptr) {
    return false;
  }
  lv_obj_align(title, LV_ALIGN_TOP_LEFT, kWifiSidePadding, kWifiTitleTop);
  return true;
}

/**
 * @brief 绘制加密 WLAN 行右侧的小锁图标
 * @param parent 父对象
 * @param x 左侧坐标
 * @param y 顶部坐标
 * @return 创建成功返回 true，否则返回 false
 */
bool CreateWifiSmallLock(lv_obj_t* parent, int x, int y) {
  lv_obj_t* shackle = lv_obj_create(parent);
  if (shackle == nullptr) {
    return false;
  }
  lv_obj_remove_flag(shackle, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(shackle, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_flag(shackle, LV_OBJ_FLAG_GESTURE_BUBBLE);
  lv_obj_set_size(shackle, 18, 16);
  lv_obj_set_pos(shackle, x + 2, y);
  lv_obj_set_style_bg_opa(shackle, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_set_style_border_color(
      shackle, lv_color_hex(kWifiMutedColor), LV_PART_MAIN);
  lv_obj_set_style_border_width(shackle, 3, LV_PART_MAIN);
  lv_obj_set_style_radius(shackle, 9, LV_PART_MAIN);
  lv_obj_set_style_pad_all(shackle, 0, LV_PART_MAIN);

  lv_obj_t* body = CreateBox(
      parent, 22, 16, kWifiMutedColor, LV_OPA_COVER, 3);
  if (body == nullptr) {
    return false;
  }
  lv_obj_remove_flag(body, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_pos(body, x, y + 10);
  return true;
}

/**
 * @brief 创建 WLAN 行末尾的圆形箭头按钮
 * @param parent 父对象
 * @return 创建成功返回对象指针，否则返回 nullptr
 */
lv_obj_t* CreateWifiCircleArrow(lv_obj_t* parent) {
  lv_obj_t* circle = CreateBox(parent, kWifiCircleButtonSize,
      kWifiCircleButtonSize, kWifiControlColor, LV_OPA_COVER,
      kWifiCircleButtonSize / 2);
  if (circle == nullptr) {
    return nullptr;
  }
  lv_obj_t* arrow = CreateLabel(circle, icon::kChevronRight,
      lv_color_hex(kWifiMutedColor), MaterialIconFont32());
  if (arrow == nullptr) {
    lv_obj_delete(circle);
    return nullptr;
  }
  lv_obj_center(arrow);
  return circle;
}

/**
 * @brief 创建显示在 5 GHz WLAN 名称旁边的 5G 标签
 * @param parent 父对象
 * @return 创建成功返回对象指针，否则返回 nullptr
 */
lv_obj_t* CreateWifi5GTag(lv_obj_t* parent) {
  lv_obj_t* tag = lv_obj_create(parent);
  if (tag == nullptr) {
    return nullptr;
  }
  lv_obj_remove_flag(tag, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(tag, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_flag(tag, LV_OBJ_FLAG_GESTURE_BUBBLE);
  lv_obj_set_size(tag, 42, 28);
  lv_obj_set_style_bg_opa(tag, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_set_style_border_color(
      tag, lv_color_hex(kWifiMutedColor), LV_PART_MAIN);
  lv_obj_set_style_border_width(tag, 2, LV_PART_MAIN);
  lv_obj_set_style_radius(tag, 7, LV_PART_MAIN);
  lv_obj_set_style_pad_all(tag, 0, LV_PART_MAIN);

  lv_obj_t* label =
      CreateLabel(tag, "5G", lv_color_hex(kWifiMutedColor), Font22());
  if (label == nullptr) {
    lv_obj_delete(tag);
    return nullptr;
  }
  lv_obj_center(label);
  return tag;
}

/**
 * @brief 创建 WLAN 顶部开关行
 * @param parent 父对象
 * @param state 设置页状态
 * @param y 行顶部坐标
 * @param width 页面宽度
 * @param enabled 开关初始是否打开
 * @return 创建成功返回 true，否则返回 false
 */
bool CreateWifiSwitchRow(lv_obj_t* parent, SettingsViewState* state, int y,
    int width, bool enabled) {
  lv_obj_t* row = lv_obj_create(parent);
  if (row == nullptr) {
    return false;
  }
  lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(row, LV_OBJ_FLAG_GESTURE_BUBBLE);
  lv_obj_set_size(row, width, kWifiRowHeight);
  lv_obj_set_pos(row, 0, y);
  lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_set_style_border_width(row, 0, LV_PART_MAIN);
  lv_obj_set_style_radius(row, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(row, 0, LV_PART_MAIN);

  lv_obj_t* title =
      CreateLabel(row, "WLAN", lv_color_hex(kPrimaryTextColor), Font32());
  if (title == nullptr) {
    return false;
  }
  lv_obj_align(title, LV_ALIGN_LEFT_MID, kWifiSidePadding, 0);

  lv_obj_t* switch_object = lv_switch_create(row);
  if (switch_object == nullptr) {
    return false;
  }
  lv_obj_add_flag(switch_object, LV_OBJ_FLAG_GESTURE_BUBBLE);
  lv_obj_set_size(switch_object, kWifiSwitchWidth, kWifiSwitchHeight);
  lv_obj_align(switch_object, LV_ALIGN_RIGHT_MID, -kWifiSidePadding, 0);
  lv_obj_set_style_anim_duration(
      switch_object, kWifiSwitchAnimationMs, LV_PART_MAIN);
  lv_obj_set_style_bg_color(switch_object, lv_color_hex(kWifiBlueColor),
                            kWifiSwitchCheckedIndicatorSelector);
  lv_obj_set_style_bg_opa(
      switch_object, LV_OPA_COVER, kWifiSwitchCheckedIndicatorSelector);
  if (enabled) {
    lv_obj_add_state(switch_object, LV_STATE_CHECKED);
  }
  lv_obj_add_event_cb(switch_object, WifiSwitchValueChangedEventCallback,
      LV_EVENT_VALUE_CHANGED, state);
  return true;
}

/**
 * @brief 在指定位置创建 WLAN 分割线
 * @param parent 父对象
 * @param y 分割线顶部坐标
 * @param width 页面宽度
 * @return 创建成功返回 true，否则返回 false
 */
bool CreateWifiDividerAt(lv_obj_t* parent, int y, int width) {
  lv_obj_t* divider =
      CreateDivider(parent, width - 2 * kWifiSidePadding);
  if (divider == nullptr) {
    return false;
  }
  lv_obj_set_pos(divider, kWifiSidePadding, y);
  return true;
}

/**
 * @brief 创建 WLAN 分组标题
 * @param parent 父对象
 * @param text 标题文本
 * @param y 标题顶部坐标
 * @param width 页面宽度
 * @return 创建成功返回 true，否则返回 false
 */
bool CreateWifiSectionLabel(
    lv_obj_t* parent, const char* text, int y, int width) {
  lv_obj_t* label =
      CreateLabel(parent, text, lv_color_hex(0x8F8EA2), Font24());
  if (label == nullptr) {
    return false;
  }
  lv_obj_set_width(label, width - 2 * kWifiSidePadding);
  lv_obj_align(label, LV_ALIGN_TOP_LEFT, kWifiSidePadding, y + 12);
  return true;
}

/**
 * @brief 创建带右箭头的 WLAN 选项行
 * @param parent 父对象
 * @param state 设置页状态
 * @param text 行文本
 * @param y 行顶部坐标
 * @param width 页面宽度
 * @return 创建成功返回 true，否则返回 false
 */
bool CreateWifiOptionRow(lv_obj_t* parent, SettingsViewState* state,
    const char* text, int y, int width) {
  lv_obj_t* row = lv_obj_create(parent);
  if (row == nullptr) {
    return false;
  }
  lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_flag(row, LV_OBJ_FLAG_GESTURE_BUBBLE);
  lv_obj_set_size(row, width, kWifiRowHeight);
  lv_obj_set_pos(row, 0, y);
  lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_set_style_bg_color(
      row, lv_color_hex(kPressedColor), LV_STATE_PRESSED);
  lv_obj_set_style_bg_opa(row, kPressedOpacity, LV_STATE_PRESSED);
  lv_obj_set_style_border_width(row, 0, LV_PART_MAIN);
  lv_obj_set_style_radius(row, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(row, 0, LV_PART_MAIN);
  if (!AddPressCancelOnLeave(row)) {
    return false;
  }
  AddEdgeBackSwipeEvents(row, WifiEdgeBackEventCallback, state);

  lv_obj_t* label =
      CreateLabel(row, text, lv_color_hex(kPrimaryTextColor), Font32());
  if (label == nullptr) {
    return false;
  }
  lv_obj_set_width(label, width - 2 * kWifiSidePadding - 70);
  lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
  lv_obj_align(label, LV_ALIGN_LEFT_MID, kWifiSidePadding, 0);

  lv_obj_t* arrow = CreateLabel(row, icon::kChevronRight,
      lv_color_hex(kWifiMutedColor), MaterialIconFont32());
  if (arrow == nullptr) {
    return false;
  }
  lv_obj_align(arrow, LV_ALIGN_RIGHT_MID, -kWifiSidePadding, 0);
  return true;
}

/**
 * @brief 创建当前已连接 WLAN 的信息卡片
 * @param parent 父对象
 * @param state 设置页状态
 * @param ssid 已连接热点 SSID
 * @param y 卡片顶部坐标
 * @param width 页面宽度
 * @return 创建成功返回 true，否则返回 false
 */
bool CreateWifiConnectedCard(lv_obj_t* parent, SettingsViewState* state,
    const char* ssid, int rssi, int y, int width) {
  const int card_width = width - 2 * kWifiSidePadding;
  lv_obj_t* card = CreateBox(parent, card_width, kWifiConnectedCardHeight,
      kWifiCardColor, LV_OPA_COVER, kWifiConnectedCardRadius);
  if (card == nullptr) {
    return false;
  }
  lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_pos(card, kWifiSidePadding, y);
  lv_obj_set_style_bg_color(
      card, lv_color_hex(0xEEF3FF), LV_STATE_PRESSED);
  lv_obj_set_style_bg_opa(card, LV_OPA_COVER, LV_STATE_PRESSED);
  if (!AddPressCancelOnLeave(card)) {
    return false;
  }
  AddEdgeBackSwipeEvents(card, WifiEdgeBackEventCallback, state);

  lv_obj_t* wifi_icon = CreateLabel(card, WifiSignalIconForRssi(rssi),
      lv_color_hex(kPrimaryTextColor), MaterialIconFont32());
  if (wifi_icon == nullptr) {
    return false;
  }
  if (state != nullptr) {
    state->wifi_connected_signal_icon = wifi_icon;
  }
  lv_obj_align(wifi_icon, LV_ALIGN_LEFT_MID, 28, -4);

  lv_obj_t* title =
      CreateLabel(card, ssid, lv_color_hex(kPrimaryTextColor), Font28());
  if (title == nullptr) {
    return false;
  }
  lv_obj_set_width(title, card_width - 240);
  lv_label_set_long_mode(title, LV_LABEL_LONG_DOT);
  lv_obj_align(title, LV_ALIGN_LEFT_MID, 82, 0);

  lv_obj_t* tag = CreateWifi5GTag(card);
  if (tag == nullptr) {
    return false;
  }
  lv_obj_align_to(tag, title, LV_ALIGN_OUT_RIGHT_MID, 8, 0);

  if (!CreateWifiSmallLock(card, card_width - 100,
          (kWifiConnectedCardHeight - 26) / 2)) {
    return false;
  }

  lv_obj_t* arrow_circle = CreateWifiCircleArrow(card);
  if (arrow_circle == nullptr) {
    return false;
  }
  lv_obj_align(arrow_circle, LV_ALIGN_RIGHT_MID, -22, 0);
  return true;
}

/**
 * @brief 创建已保存或扫描到的 WLAN 热点行
 * @param parent 父对象
 * @param state 设置页状态
 * @param ssid 热点 SSID
 * @param show_tag 是否显示 5G 标签
 * @param secure 是否显示加密锁
 * @param password 点击连接时使用的密码
 * @param y 行顶部坐标
 * @param width 页面宽度
 * @return 创建成功返回 true，否则返回 false
 */
bool CreateWifiNetworkRow(lv_obj_t* parent, SettingsViewState* state,
    const char* ssid, bool show_tag, bool secure, int rssi,
    const char* password, int y, int width) {
  const int row_width = width - 2 * kWifiSidePadding;
  lv_obj_t* row = lv_obj_create(parent);
  if (row == nullptr) {
    return false;
  }
  lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_flag(row, LV_OBJ_FLAG_GESTURE_BUBBLE);
  lv_obj_set_size(row, row_width, kWifiNetworkRowHeight);
  lv_obj_set_pos(row, kWifiSidePadding, y);
  lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_set_style_bg_color(row, lv_color_hex(0xEDEDED), LV_STATE_PRESSED);
  lv_obj_set_style_bg_opa(row, LV_OPA_COVER, LV_STATE_PRESSED);
  lv_obj_set_style_border_width(row, 0, LV_PART_MAIN);
  lv_obj_set_style_radius(row, kWifiConnectedCardRadius, LV_PART_MAIN);
  lv_obj_set_style_pad_all(row, 0, LV_PART_MAIN);
  if (!AddPressCancelOnLeave(row)) {
    return false;
  }
  AddEdgeBackSwipeEvents(row, WifiEdgeBackEventCallback, state);
  WifiNetworkAction* action =
      ReserveWifiNetworkAction(state, ssid, password);
  if (action != nullptr) {
    lv_obj_add_event_cb(
        row, WifiNetworkClickedEventCallback, LV_EVENT_CLICKED, action);
  }

  lv_obj_t* wifi_icon = CreateLabel(row, WifiSignalIconForRssi(rssi),
      lv_color_hex(kPrimaryTextColor), MaterialIconFont32());
  if (wifi_icon == nullptr) {
    return false;
  }
  lv_obj_align(
      wifi_icon, LV_ALIGN_LEFT_MID, kWifiNetworkIconLeft, -1);

  const int tag_reserve = show_tag ? 58 : 0;
  lv_obj_t* title =
      CreateLabel(row, ssid, lv_color_hex(kPrimaryTextColor), Font28());
  if (title == nullptr) {
    return false;
  }
  lv_obj_set_width(title, row_width - kWifiNetworkTextLeft -
      kWifiNetworkRightControlWidth - tag_reserve);
  lv_label_set_long_mode(title, LV_LABEL_LONG_DOT);
  lv_obj_align(title, LV_ALIGN_LEFT_MID, kWifiNetworkTextLeft, 0);

  if (show_tag) {
    lv_obj_t* tag = CreateWifi5GTag(row);
    if (tag == nullptr) {
      return false;
    }
    lv_obj_align(tag, LV_ALIGN_RIGHT_MID,
        -kWifiNetworkRightControlWidth, 0);
  }

  if (secure && !CreateWifiSmallLock(row, row_width - 108,
                    (kWifiNetworkRowHeight - 26) / 2)) {
    return false;
  }

  lv_obj_t* arrow_circle = CreateWifiCircleArrow(row);
  if (arrow_circle == nullptr) {
    return false;
  }
  lv_obj_align(arrow_circle, LV_ALIGN_RIGHT_MID, -kWifiNetworkArrowRight, 0);
  return true;
}

/**
 * @brief 创建 WLAN 扫描刷新按钮
 * @param parent 父对象
 * @param state 设置页状态
 * @param y 按钮顶部坐标
 * @param width 页面宽度
 * @return 创建成功返回 true，否则返回 false
 */
bool CreateWifiRefreshButton(
    lv_obj_t* parent, SettingsViewState* state, int y, int width) {
  lv_obj_t* button = CreateBox(parent, 54, 54, kWifiControlColor,
      LV_OPA_COVER, 27);
  if (button == nullptr) {
    return false;
  }
  lv_obj_add_flag(button, LV_OBJ_FLAG_CLICKABLE);
  AddEdgeBackSwipeEvents(button, WifiEdgeBackEventCallback, state);
  lv_obj_add_event_cb(button, WifiRefreshButtonClickedEventCallback,
      LV_EVENT_CLICKED, state);
  lv_obj_set_pos(button, width - kWifiSidePadding - 54, y);
  lv_obj_set_style_bg_color(
      button, lv_color_hex(0xE6E7EA), LV_STATE_PRESSED);
  lv_obj_set_style_bg_opa(button, LV_OPA_COVER, LV_STATE_PRESSED);
  if (!AddPressCancelOnLeave(button)) {
    return false;
  }

  lv_obj_t* refresh_icon = CreateLabel(button, icon::kRefresh,
      lv_color_hex(kPrimaryTextColor), MaterialIconFont32());
  if (refresh_icon == nullptr) {
    return false;
  }
  lv_obj_update_layout(refresh_icon);
  lv_obj_set_style_transform_pivot_x(
      refresh_icon, lv_obj_get_width(refresh_icon) / 2, LV_PART_MAIN);
  lv_obj_set_style_transform_pivot_y(
      refresh_icon, lv_obj_get_height(refresh_icon) / 2, LV_PART_MAIN);
  lv_obj_center(refresh_icon);
  if (state != nullptr) {
    state->wifi_refresh_icon = refresh_icon;
    UpdateWifiRefreshAnimation(state);
  }
  return true;
}

/**
 * @brief 创建附近 WLAN 分组标题和刷新按钮
 * @param parent 父对象
 * @param state 设置页状态
 * @param y 标题顶部坐标
 * @param width 页面宽度
 * @return 创建成功返回 true，否则返回 false
 */
bool CreateWifiNearbyHeader(
    lv_obj_t* parent, SettingsViewState* state, int y, int width) {
  if (!CreateWifiSectionLabel(parent, "Select nearby WLAN", y, width) ||
      !CreateWifiRefreshButton(parent, state, y, width)) {
    return false;
  }
  return true;
}

/**
 * @brief 创建 WLAN 状态提示文本
 * @param parent 父对象
 * @param text 提示文本
 * @param y 文本顶部坐标
 * @param width 页面宽度
 * @return 创建成功返回 true，否则返回 false
 */
bool CreateWifiStatusText(
    lv_obj_t* parent, const char* text, int y, int width) {
  lv_obj_t* label =
      CreateLabel(parent, text, lv_color_hex(kSecondaryTextColor), Font24());
  if (label == nullptr) {
    return false;
  }
  lv_obj_set_width(label, width - 2 * kWifiSidePadding);
  lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
  lv_obj_align(label, LV_ALIGN_TOP_LEFT, kWifiSidePadding, y + 12);
  return true;
}

/**
 * @brief 根据 HAL 连接和扫描状态创建当前 WLAN 页面内容
 * @param parent WLAN 页面滚动内容对象
 * @param state 设置页状态
 * @param config app 页面配置
 * @return 创建成功返回 true，否则返回 false
 */
bool CreateWifiPageContent(lv_obj_t* parent, SettingsViewState* state,
    const AppViewConfig& config) {
  hal::WifiStatus status;
  hal::WifiScanStatus scan_status;
  ReadWifiSnapshots(config, &status, &scan_status);

  const bool wifi_enabled = state != nullptr ? state->wifi_enabled_requested
                                             : IsWifiPageEnabled(status,
                                                   scan_status);
  const bool scan_pending = state != nullptr && state->wifi_scan_on_ready;
  const bool refreshing = IsWifiRefreshActive(state, status, scan_status);
  const bool show_scan_results =
      scan_status.network_count > 0 && !status.start_failed &&
      (!scan_status.scan_failed || refreshing);
  int y = 0;
  if (!CreateWifiSwitchRow(parent, state, y, config.width, wifi_enabled)) {
    return false;
  }
  y += kWifiRowHeight;

  if (!wifi_enabled) {
    y += 8;
    if (!CreateWifiDividerAt(parent, y, config.width)) {
      return false;
    }
    y += 18;
    if (!CreateWifiSectionLabel(parent, "More settings", y, config.width)) {
      return false;
    }
    y += kWifiSectionHeight;
    return CreateWifiOptionRow(
        parent, state, "Advanced settings", y, config.width);
  }

  char ssid[33] = {};
  ReadWifiPageSsid(status, ssid, sizeof(ssid));
  if (status.connected || status.got_ip) {
    y += 10;
    if (!CreateWifiConnectedCard(
            parent, state, ssid, status.rssi, y, config.width)) {
      return false;
    }
    y += kWifiConnectedCardHeight + 18;
  }

  if (!CreateWifiDividerAt(parent, y, config.width)) {
    return false;
  }
  y += 14;
  if (!CreateWifiSectionLabel(parent, "Saved WLAN", y, config.width)) {
    return false;
  }
  y += kWifiSectionHeight;

  if (show_scan_results) {
    for (size_t i = 0; i < scan_status.network_count; ++i) {
      const hal::WifiNetworkInfo& network = scan_status.networks[i];
      if (!IsSavedWifiSsid(network.ssid)) {
        continue;
      }
      if (!CreateWifiNetworkRow(parent, state, network.ssid, network.is_5g,
              network.secure, network.rssi, kWifiDefaultPassword, y,
              config.width)) {
        return false;
      }
      y += kWifiNetworkRowHeight;
    }
  }
  y += 10;

  if (!CreateWifiDividerAt(parent, y, config.width)) {
    return false;
  }
  y += 16;
  if (!CreateWifiNearbyHeader(parent, state, y, config.width)) {
    return false;
  }
  y += kWifiSectionHeight + 6;

  if (show_scan_results) {
    size_t nearby_count = 0;
    for (size_t i = 0; i < scan_status.network_count; ++i) {
      const hal::WifiNetworkInfo& network = scan_status.networks[i];
      if (IsSavedWifiSsid(network.ssid)) {
        continue;
      }
      const char* password = network.secure ? kWifiDefaultPassword : "";
      if (!CreateWifiNetworkRow(parent, state, network.ssid, network.is_5g,
              network.secure, network.rssi, password, y, config.width)) {
        return false;
      }
      ++nearby_count;
      y += kWifiNetworkRowHeight;
    }
    if (nearby_count == 0 && !refreshing) {
      if (!CreateWifiStatusText(parent, "No nearby WLAN.", y, config.width)) {
        return false;
      }
      y += kWifiSectionHeight;
    }
  } else if (scan_pending || scan_status.scan_running ||
             status.init_task_running) {
    if (!CreateWifiStatusText(parent, "Scanning...", y, config.width)) {
      return false;
    }
    y += kWifiSectionHeight;
  } else if (scan_status.scan_failed || status.start_failed) {
    if (!CreateWifiStatusText(parent, "WLAN scan failed", y, config.width)) {
      return false;
    }
    y += kWifiSectionHeight;
  } else if (scan_status.network_count == 0) {
    if (!CreateWifiStatusText(
            parent, "Tap refresh to scan nearby WLAN.", y, config.width)) {
      return false;
    }
    y += kWifiSectionHeight;
  }

  y += 10;
  if (!CreateWifiDividerAt(parent, y, config.width)) {
    return false;
  }
  y += 18;
  if (!CreateWifiSectionLabel(parent, "More settings", y, config.width)) {
    return false;
  }
  y += kWifiSectionHeight;
  return CreateWifiOptionRow(
      parent, state, "Advanced settings", y, config.width);
}

/**
 * @brief WiFi 状态或扫描结果变化时重建 WLAN 内容区域
 * @param state 设置页状态
 * @param force 是否忽略已缓存的状态摘要
 */
void RefreshWifiPage(SettingsViewState* state, bool force) {
  if (state == nullptr || state->wifi_body == nullptr) {
    return;
  }

  hal::WifiStatus status;
  hal::WifiScanStatus scan_status;
  ReadWifiSnapshots(state->config, &status, &scan_status);
  const uint32_t refresh_key = MakeWifiRefreshKey(status, scan_status);
  if (!force && !state->wifi_refresh_force &&
      state->wifi_refresh_key == refresh_key) {
    return;
  }

  state->wifi_refresh_key = refresh_key;
  state->wifi_refresh_force = false;
  state->wifi_action_count = 0;
  state->wifi_connected_signal_icon = nullptr;
  const int scroll_y = lv_obj_get_scroll_y(state->wifi_body);
  StopWifiRefreshIconSpin(state);
  state->wifi_refresh_icon = nullptr;
  lv_obj_clean(state->wifi_body);
  CreateWifiPageContent(state->wifi_body, state, state->config);
  lv_obj_update_layout(state->wifi_body);
  lv_obj_scroll_to_y(state->wifi_body, scroll_y, LV_ANIM_OFF);
}

/**
 * @brief 显示 WLAN 详情页并启动 HAL 状态轮询
 * @param state 设置页状态
 * @return 页面创建成功或已经显示返回 true
 */
bool ShowWifiPage(SettingsViewState* state) {
  if (state == nullptr || state->root == nullptr) {
    return false;
  }
  if (state->wifi_closing) {
    return true;
  }
  if (state->wifi_page != nullptr) {
    lv_obj_add_flag(state->root, kBlockLauncherGestureFlag);
    lv_obj_remove_flag(state->root, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_move_to_index(state->wifi_page, -1);
    return true;
  }

  const AppViewConfig& config = state->config;
  lv_obj_t* page = lv_obj_create(state->root);
  if (page == nullptr) {
    return false;
  }
  state->wifi_page = page;
  state->wifi_body = nullptr;
  state->wifi_connected_signal_icon = nullptr;
  state->wifi_refresh_icon = nullptr;
  state->wifi_closing = false;
  state->wifi_swipe = EdgeBackSwipeState();
  state->wifi_action_count = 0;
  state->wifi_refresh_key = 0;
  state->wifi_refresh_force = true;
  state->wifi_scan_on_ready = false;
  state->wifi_scan_request_generation = 0;
  hal::WifiStatus initial_status;
  hal::WifiScanStatus initial_scan_status;
  ReadWifiSnapshots(config, &initial_status, &initial_scan_status);
  state->wifi_enabled_requested =
      IsWifiPageEnabled(initial_status, initial_scan_status);
  if (state->wifi_enabled_requested) {
    RequestWifiScan(state);
  }
  lv_obj_add_flag(state->root, kBlockLauncherGestureFlag);
  lv_obj_remove_flag(state->root, LV_OBJ_FLAG_GESTURE_BUBBLE);

  lv_obj_remove_flag(page, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(page, LV_OBJ_FLAG_GESTURE_BUBBLE);
  AddEdgeBackSwipeEvents(page, WifiEdgeBackEventCallback, state);
  lv_obj_set_size(page, config.width, config.height);
  lv_obj_set_pos(page, 0, 0);
  lv_obj_set_style_bg_color(page, lv_color_hex(kBackgroundColor),
      LV_PART_MAIN);
  lv_obj_set_style_bg_opa(page, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_border_width(page, 0, LV_PART_MAIN);
  lv_obj_set_style_radius(page, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(page, 0, LV_PART_MAIN);

  if (!CreateWifiHeader(page, state, config.width)) {
    CloseWifiPage(state, false);
    return false;
  }

  lv_obj_t* body = lv_obj_create(page);
  if (body == nullptr) {
    CloseWifiPage(state, false);
    return false;
  }
  MakeTransparent(body);
  lv_obj_set_size(body, config.width, config.height - kWifiBodyTop);
  lv_obj_align(body, LV_ALIGN_TOP_LEFT, 0, kWifiBodyTop);
  lv_obj_set_scroll_dir(body, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(body, LV_SCROLLBAR_MODE_OFF);
  lv_obj_add_flag(body, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(body, LV_OBJ_FLAG_GESTURE_BUBBLE);
  lv_obj_remove_flag(body, LV_OBJ_FLAG_SCROLL_ELASTIC);
  AddEdgeBackSwipeEvents(body, WifiEdgeBackEventCallback, state);
  state->wifi_body = body;

  if (!CreateWifiPageContent(body, state, config)) {
    CloseWifiPage(state, false);
    return false;
  }
  state->wifi_refresh_timer =
      lv_timer_create(WifiRefreshTimerCallback, kWifiRefreshPeriodMs, state);
  if (state->wifi_refresh_timer == nullptr) {
    CloseWifiPage(state, false);
    return false;
  }

  EnableEdgeBackSwipeEventBubble(page);
  if (!StartSlideLeftWindowTransition(
          page, config.width, kDetailSlideAnimationMs, state, nullptr)) {
    CloseWifiPage(state, false);
    return false;
  }
  return true;
}

/**
 * @brief 打开我的设备详情页
 * @param state 设置页状态
 * @return 打开成功返回 true，否则返回 false
 */
bool ShowMyDevicePage(SettingsViewState* state) {
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
                       CreateDeviceOptions(body, config.width);
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
  lv_obj_set_style_text_font(text_area, Font32(), LV_PART_MAIN);
  lv_obj_set_style_text_color(
      text_area, lv_color_hex(kPrimaryTextColor), LV_PART_MAIN);
  lv_obj_set_style_bg_color(
      text_area, lv_color_hex(kNameEditInputColor), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(text_area, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_border_color(
      text_area, lv_color_hex(kNameEditInputBorderColor), LV_PART_MAIN);
  lv_obj_set_style_border_width(text_area, 3, LV_PART_MAIN);
  lv_obj_set_style_radius(text_area, kNameEditTextAreaRadius, LV_PART_MAIN);
  lv_obj_set_style_pad_left(text_area, 26, LV_PART_MAIN);
  lv_obj_set_style_pad_right(text_area, 26, LV_PART_MAIN);
  lv_obj_set_style_pad_top(text_area, 18, LV_PART_MAIN);
  lv_obj_set_style_pad_bottom(text_area, 18, LV_PART_MAIN);

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
 * @brief 从我的设备详情页打开设备名称编辑页
 * @param event LVGL 事件对象
 */
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

/**
 * @brief 创建单个设置项
 * @param parent 父对象
 * @param item 设置项
 * @param width 设置项宽度
 * @return 创建成功返回对象指针，否则返回 nullptr
 */
lv_obj_t* CreateSettingsRow(
    lv_obj_t* parent, const app::SettingsEntry& item, int width) {
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
  }

  return row;
}

/**
 * @brief 根据 WiFi 状态更新 WiFi 设置项显示值
 * @param config app 页面配置
 * @param buffer 文本缓冲区
 * @param size 文本缓冲区大小
 * @return 文本指针
 */
const char* WifiValueText(
    const AppViewConfig& config, char* buffer, size_t size) {
  if (buffer == nullptr || size == 0) {
    return "";
  }
  buffer[0] = '\0';

  hal::WifiStatus status;
  if (config.wifi == nullptr || !config.wifi->ReadWifiStatus(&status)) {
    return "";
  }
  if (!status.running) {
    return "Off";
  }
  if (status.got_ip || status.connected) {
    return kWifiSavedSsid5G;
  }
  if (status.init_task_running) {
    return "Starting";
  }
  return "On";
}

/**
 * @brief 创建设置列表
 * @param parent 父对象
 * @param config app 页面配置
 * @param width 列表宽度
 * @param state 设置页面状态
 * @return 创建成功返回 true，否则返回 false
 */
bool CreateSettingsList(lv_obj_t* parent, const AppViewConfig& config,
    int width, SettingsViewState* state) {
  const app::SettingsCatalog& catalog = app::GetSettingsCatalog();
  char wifi_value[32] = {};
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
      item.value = WifiValueText(config, wifi_value, sizeof(wifi_value));
    }

    lv_obj_t* row = CreateSettingsRow(parent, item, width);
    if (row == nullptr) {
      return false;
    }
    if (IsId(item.id, "my_device")) {
      lv_obj_add_event_cb(
          row, MyDeviceRowClickedEventCallback, LV_EVENT_CLICKED, state);
    } else if (IsId(item.id, "wlan")) {
      lv_obj_add_event_cb(
          row, WifiRowClickedEventCallback, LV_EVENT_CLICKED, state);
    }
    lv_obj_set_pos(row, 0, y);
    y += kRowHeight;
  }
  return true;
}

}  // namespace

lv_obj_t* CreateSettingsView(lv_obj_t* parent, const app::AppEntry&,
    const AppViewConfig& config) {
  if (parent == nullptr || config.width <= 0 || config.height <= 0) {
    return nullptr;
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

  if (!CreateSettingsList(list, config, config.width, state)) {
    lv_obj_delete(root);
    return nullptr;
  }

  return root;
}

}  // namespace lilygo_box::ui
