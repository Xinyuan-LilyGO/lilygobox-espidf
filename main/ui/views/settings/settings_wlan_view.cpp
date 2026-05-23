/*
 * @Description: Settings WLAN detail page
 * @Author: LILYGO_L
 * @Date: 2026-05-23 00:00:00
 * @LastEditTime: 2026-05-23 00:00:00
 * @License: GPL 3.0
 */
#include "ui/views/settings/settings_view_internal.h"

#include <cstdio>
#include <cstring>

#include "hal/providers/screen_provider.h"
#include "ui/animation/transition_animation.h"
#include "ui/font/material_symbols_assets.h"
#include "ui/input/app_view_gesture_flags.h"
#include "ui/input/edge_back_gesture.h"
#include "ui/input/press_cancel.h"

namespace lilygo_box::ui {
namespace {

void CloseWifiPage(SettingsViewState* state, bool animated);
void RefreshWifiPage(SettingsViewState* state, bool force);
void StopWifiRefreshIconSpin(SettingsViewState* state);
void UpdateWifiRefreshAnimation(SettingsViewState* state);
void UpdateWifiConnectedSignalIcon(SettingsViewState* state);
void RequestWifiScan(SettingsViewState* state);
void ReadWifiSnapshots(
    const AppViewConfig& config, hal::WifiStatus* status,
    hal::WifiScanStatus* scan_status);

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
bool ShowWifiPageInternal(SettingsViewState* state) {
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

}  // namespace

/**
 * @brief 从设置主页打开 WLAN 详情页
 * @param state 设置页状态
 * @return 页面创建成功或已经显示返回 true
 */
bool ShowWifiPage(SettingsViewState* state) {
  return ShowWifiPageInternal(state);
}

}  // namespace lilygo_box::ui
