/*
 * @Description: Settings WLAN detail page
 * @Author: LILYGO_L
 * @Date: 2026-05-23 00:00:00
 * @LastEditTime: 2026-08-10 10:30:09
 * @License: GPL 3.0
 */
#include "ui/views/settings/settings_view_internal.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "app/network_monitor.h"
#include "app/storage/wifi_storage.h"
#include "app/wifi_manager.h"
#include "esp_wifi.h"
#include "hal/providers/screen_provider.h"
#include "ui/animation/transition_animation.h"
#include "ui/resources/fonts/icon_assets.h"
#include "ui/input/press_cancel.h"
#include "ui/views/settings/settings_basic_view_common.h"
#include "ui/widgets/prompt/prompt_sheet.h"
#include "ui/widgets/shared_keyboard.h"

namespace lilygo_box::ui {

namespace {

// P4 侧运行期保存的 WLAN 凭据，不写入 ESP32-C6。
app::WifiSavedNetwork g_wifi_saved_networks[
    app::kWifiSavedNetworkCapacity] = {};
size_t g_wifi_saved_network_count = 0;
bool g_wifi_saved_networks_loaded = false;

/**
 * @brief 关闭 WLAN 详情页并释放页面资源
 * @param state 设置页状态
 * @param animated 是否播放关闭动画
 */
void CloseWifiPage(SettingsViewState* state, bool animated);

/**
 * @brief 按当前 WLAN 状态重新绘制网络列表
 * @param state 设置页状态
 * @param force 是否强制重建列表内容
 */
void RefreshWifiPage(SettingsViewState* state, bool force);

/**
 * @brief 停止 WLAN 刷新图标旋转动画
 * @param state 设置页状态
 */
void StopWifiRefreshIconSpin(SettingsViewState* state);

/**
 * @brief 根据扫描状态更新 WLAN 刷新动画
 * @param state 设置页状态
 */
void UpdateWifiRefreshAnimation(SettingsViewState* state);

/**
 * @brief 更新已连接网络右侧的信号图标
 * @param state 设置页状态
 */
void UpdateWifiConnectedSignalIcon(SettingsViewState* state);

/**
 * @brief 根据当前密码长度更新连接按钮状态
 * @param state 设置页状态
 */
void UpdateWifiConnectButtonState(SettingsViewState* state);

/**
 * @brief 根据键盘显示状态移动 WLAN 连接弹窗
 * @param state 设置页状态
 * @param keyboard_visible 键盘是否显示
 */
void MoveWifiConnectSheetForKeyboard(
    SettingsViewState* state, bool keyboard_visible);

/**
 * @brief 检查 WLAN 连接是否超过等待时间
 * @param state 设置页状态
 */
void UpdateWifiConnectTimeout(SettingsViewState* state);

/**
 * @brief 清理 WLAN 连接等待和失败重试状态
 * @param state 设置页状态
 */
void ResetWifiConnectionState(SettingsViewState* state);

/**
 * @brief 请求驱动层执行一次 WLAN 扫描
 * @param state 设置页状态
 * @param force 是否强制发起扫描，手动刷新时允许已连接状态下扫描
 */
void RequestWifiScan(SettingsViewState* state, bool force = false);

/**
 * @brief 从扫描结果里查找指定 SSID 的最新网络信息
 * @param scan_status 扫描状态
 * @param ssid 待查找 SSID
 * @param output 找到时写入网络信息，可为空
 * @return 找到返回 true，否则返回 false
 */
bool FindScannedWifiNetwork(const hal::WifiScanStatus& scan_status,
                            const char* ssid, hal::WifiNetworkInfo* output);

/**
 * @brief 保存或更新用户确认使用的 WLAN 凭据
 * @param action 当前连接动作
 * @param password 用户确认使用的密码，开放网络可为空
 */
void SaveWifiNetworkCredential(
    const WifiNetworkAction& action, const char* password);

/**
 * @brief 将运行期已保存 WLAN 列表写入长期缓存
 */
void SaveWifiNetworks();

/**
 * @brief 保存 WLAN 开关和自动连接偏好
 * @param state 设置页状态
 */
void SaveWifiPreferences(const SettingsViewState* state);

/**
 * @brief 判断 SSID 是否已经在运行期保存过
 * @param ssid 待判断的热点名称
 * @return 已保存返回 true，否则返回 false
 */
bool IsSavedWifiSsid(const char* ssid);

/**
 * @brief 在运行期已保存 WLAN 表里查找指定 SSID
 * @param ssid 待查找的热点名称
 * @return 找到返回保存项地址，否则返回 nullptr
 */
app::WifiSavedNetwork* FindSavedWifiNetwork(const char* ssid);

/**
 * @brief 在只读运行期已保存 WLAN 表里查找指定 SSID
 * @param ssid 待查找的热点名称
 * @return 找到返回保存项地址，否则返回 nullptr
 */
const app::WifiSavedNetwork* FindSavedWifiNetworkConst(const char* ssid);

/**
 * @brief 从运行期已保存 WLAN 表里删除指定 SSID
 * @param ssid 待删除的热点名称
 */
void RemoveSavedWifiNetwork(const char* ssid);

/**
 * @brief 删除保存凭据并按需断开当前 WLAN
 * @param state 设置页状态
 * @param ssid 待删除的热点名称
 */
void ForgetSavedWifiNetwork(SettingsViewState* state, const char* ssid);

/**
 * @brief 判断当前待连接 WLAN 是否可以直接重试
 * @param state 设置页状态
 * @return 有保存密码或开放网络返回 true，否则返回 false
 */
bool CanRetryPendingWifiConnection(const SettingsViewState* state);

/**
 * @brief 读取 WLAN 连接状态和扫描状态快照
 * @param config 应用视图配置
 * @param status 用于写入连接状态的输出参数
 * @param scan_status 用于写入扫描状态的输出参数
 */
void ReadWifiSnapshots(
    const AppViewConfig& config, hal::WifiStatus* status,
    hal::WifiScanStatus* scan_status);

/**
 * @brief 关闭 WLAN 二级详情页
 * @param state 设置页状态
 * @param animated 是否播放关闭动画
 */
void CloseWifiSubPage(SettingsViewState* state, bool animated);

/**
 * @brief 关闭 WLAN 的全部子页面
 * @param state 设置页状态
 */
void CloseAllWifiSubPages(SettingsViewState* state);

/**
 * @brief 关闭 WLAN 底部弹窗
 * @param state 设置页状态
 */
void CloseWifiModal(SettingsViewState* state);

/**
 * @brief 立即关闭 WLAN 底部弹窗
 * @param state 设置页状态
 */
void CloseWifiModalImmediately(SettingsViewState* state);

/**
 * @brief 打开单个 WLAN 网络详情页
 * @param state 设置页状态
 * @param action 被点击网络的操作信息
 * @return 打开成功返回 true，否则返回 false
 */
bool ShowWifiNetworkDetailPage(
    SettingsViewState* state, const WifiNetworkAction& action);

/**
 * @brief 打开 WLAN 高级设置页
 * @param state 设置页状态
 * @return 打开成功返回 true，否则返回 false
 */
bool ShowWifiAdvancedPage(SettingsViewState* state);

/**
 * @brief 打开已保存 WLAN 网络管理页
 * @param state 设置页状态
 * @return 打开成功返回 true，否则返回 false
 */
bool ShowWifiSavedNetworksPage(SettingsViewState* state);

/**
 * @brief 打开 WLAN 连接确认或密码输入底部弹窗
 * @param state 设置页状态
 * @param action 被点击网络的操作信息
 * @return 打开成功返回 true，否则返回 false
 */
bool ShowWifiConnectSheet(
    SettingsViewState* state, const WifiNetworkAction& action,
    const char* error_text = nullptr, bool edit_mode = false);

/**
 * @brief 打开 WLAN 删除网络确认底部弹窗
 * @param state 设置页状态
 * @param ssid 待删除的热点名称
 * @param close_sub_page 确认删除后是否关闭当前 WLAN 子页面
 * @param saved_delete_row 管理已保存网络页中待删除的行对象
 * @return 打开成功返回 true，否则返回 false
 */
bool ShowWifiDeleteNetworkSheet(SettingsViewState* state, const char* ssid,
    bool close_sub_page, lv_obj_t* saved_delete_row);

/**
 * @brief 创建 WLAN 状态提示文本
 * @param parent 父对象
 * @param text 提示文本
 * @param y 顶部坐标
 * @param width 页面宽度
 * @return 创建成功返回 true，否则返回 false
 */
bool CreateWifiStatusText(
    lv_obj_t* parent, const char* text, int y, int width);

/**
 * @brief 创建已保存 WLAN 为空时的提示文本
 * @param parent 父对象
 * @param width 页面宽度
 * @return 创建成功返回 true，否则返回 false
 */
bool CreateWifiSavedEmptyText(lv_obj_t* parent, int width);

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
  CloseAllWifiSubPages(state);
  state->wifi_page = nullptr;
  state->wifi_body = nullptr;
  state->wifi_sub_page = nullptr;
  state->wifi_sub_page_count = 0;
  state->wifi_modal_overlay = nullptr;
  state->wifi_modal_sheet = nullptr;
  state->wifi_password_text_area = nullptr;
  state->wifi_password_error_label = nullptr;
  state->wifi_password_keyboard = nullptr;
  state->wifi_connect_button = nullptr;
  state->wifi_connect_button_label = nullptr;
  state->wifi_connected_signal_icon = nullptr;
  state->wifi_refresh_icon = nullptr;
  state->wifi_closing = false;
  if (state->wifi_refresh_timer != nullptr) {
    lv_timer_delete(state->wifi_refresh_timer);
    state->wifi_refresh_timer = nullptr;
  }
  lv_obj_delete(page);
  UpdateSettingsWifiValue(state);
}

void CloseWifiPage(SettingsViewState* state, bool animated) {
  if (state == nullptr || state->wifi_page == nullptr ||
      state->wifi_closing) {
    return;
  }

  if (state->wifi_refresh_timer != nullptr) {
    lv_timer_delete(state->wifi_refresh_timer);
    state->wifi_refresh_timer = nullptr;
  }
  CloseWifiModalImmediately(state);
  app::SetWifiAutoConnectPaused(false);
  CloseAllWifiSubPages(state);

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
  state->wifi_sub_page = nullptr;
  state->wifi_sub_page_count = 0;
  state->wifi_modal_overlay = nullptr;
  state->wifi_modal_sheet = nullptr;
  state->wifi_password_text_area = nullptr;
  state->wifi_password_error_label = nullptr;
  state->wifi_password_keyboard = nullptr;
  state->wifi_connect_button = nullptr;
  state->wifi_connect_button_label = nullptr;
  state->wifi_connected_signal_icon = nullptr;
  state->wifi_refresh_icon = nullptr;
  state->wifi_closing = false;
  if (state->wifi_refresh_timer != nullptr) {
    lv_timer_delete(state->wifi_refresh_timer);
    state->wifi_refresh_timer = nullptr;
  }
  lv_obj_delete(page);
  UpdateSettingsWifiValue(state);
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
  lv_event_stop_bubbling(event);
  lv_event_stop_processing(event);
}

/**
 * @brief 同步 WLAN 子页面栈顶指针和返回手势状态
 * @param state 设置页状态
 */
void SyncWifiSubPageTop(SettingsViewState* state) {
  if (state == nullptr) {
    return;
  }

  if (state->wifi_sub_page_count == 0) {
    state->wifi_sub_page = nullptr;
  } else {
    state->wifi_sub_page =
        state->wifi_sub_pages[state->wifi_sub_page_count - 1];
    if (state->wifi_sub_page != nullptr) {
      lv_obj_move_to_index(state->wifi_sub_page, -1);
    }
  }
  state->wifi_sub_closing = false;
}

/**
 * @brief 弹出并删除当前 WLAN 子页面
 * @param state 设置页状态
 */
void DeleteWifiSubPageTop(SettingsViewState* state) {
  if (state == nullptr || state->wifi_sub_page_count == 0) {
    return;
  }

  const size_t top_index = state->wifi_sub_page_count - 1;
  lv_obj_t* page = state->wifi_sub_pages[top_index];
  state->wifi_sub_pages[top_index] = nullptr;
  --state->wifi_sub_page_count;
  if (page != nullptr) {
    lv_obj_delete(page);
  }
  SyncWifiSubPageTop(state);
}

void CloseAllWifiSubPages(SettingsViewState* state) {
  if (state == nullptr) {
    return;
  }

  for (size_t i = state->wifi_sub_page_count; i > 0; --i) {
    lv_obj_t* page = state->wifi_sub_pages[i - 1];
    state->wifi_sub_pages[i - 1] = nullptr;
    if (page != nullptr) {
      lv_obj_delete(page);
    }
  }
  state->wifi_sub_page_count = 0;
  SyncWifiSubPageTop(state);
}

/**
 * @brief WLAN 子页面关闭动画完成后清理页面对象
 * @param animation LVGL 动画对象
 */
void WifiSubCloseCompletedCallback(lv_anim_t* animation) {
  auto* state =
      static_cast<SettingsViewState*>(lv_anim_get_user_data(animation));
  if (state == nullptr || state->wifi_sub_page == nullptr) {
    return;
  }

  DeleteWifiSubPageTop(state);
}

void CloseWifiSubPage(SettingsViewState* state, bool animated) {
  if (state == nullptr || state->wifi_sub_page == nullptr ||
      state->wifi_sub_closing) {
    return;
  }

  if (animated &&
      StartSlideRightWindowTransition(state->wifi_sub_page,
          state->config.width, kDetailSlideAnimationMs, state,
          WifiSubCloseCompletedCallback)) {
    state->wifi_sub_closing = true;
    return;
  }

  DeleteWifiSubPageTop(state);
}

/**
 * @brief 清理 WLAN 底部弹窗状态字段
 * @param state 设置页状态
 */
void ResetWifiModalState(SettingsViewState* state) {
  if (state == nullptr) {
    return;
  }

  state->wifi_modal_overlay = nullptr;
  state->wifi_modal_sheet = nullptr;
  state->wifi_password_text_area = nullptr;
  state->wifi_password_error_label = nullptr;
  state->wifi_password_keyboard = nullptr;
  state->wifi_connect_button = nullptr;
  state->wifi_connect_button_label = nullptr;
  state->wifi_saved_delete_row = nullptr;
  state->wifi_delete_close_sub_page = false;
}

void CloseWifiModalImmediately(SettingsViewState* state) {
  if (state == nullptr || state->wifi_modal_overlay == nullptr) {
    return;
  }

  lv_obj_t* overlay = state->wifi_modal_overlay;
  ResetWifiModalState(state);
  lv_obj_delete(overlay);
  if (!state->wifi_connect_waiting) {
    app::SetWifiAutoConnectPaused(false);
  }
}

void CloseWifiModal(SettingsViewState* state) {
  if (state == nullptr || state->wifi_modal_overlay == nullptr) {
    return;
  }

  lv_obj_t* overlay = state->wifi_modal_overlay;
  lv_obj_t* sheet = state->wifi_modal_sheet;
  if (state->wifi_password_keyboard != nullptr) {
    HideSharedKeyboard(state->wifi_password_keyboard);
  }
  ResetWifiModalState(state);
  if (!AnimatePromptSheetOut(overlay, sheet, kDetailSlideAnimationMs)) {
    lv_obj_delete(overlay);
  }
  if (!state->wifi_connect_waiting) {
    app::SetWifiAutoConnectPaused(false);
  }
}

void ResetWifiConnectionState(SettingsViewState* state) {
  if (state == nullptr) {
    return;
  }

  state->wifi_connect_waiting = false;
  state->wifi_connection_retry_ready = false;
  state->wifi_connect_started_ms = 0;
}

/**
 * @brief 处理 WLAN 子页面顶部返回按钮
 * @param event LVGL 事件对象
 */
void WifiSubBackClickedEventCallback(lv_event_t* event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
    return;
  }
  CloseWifiSubPage(
      static_cast<SettingsViewState*>(lv_event_get_user_data(event)), true);
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
    SaveWifiPreferences(state);
    RequestWifiScan(state);
  } else {
    state->wifi_enabled_requested = false;
    // 先保存关闭状态，避免恢复自动连接后再次拉高 WiFi 协处理器 EN。
    SaveWifiPreferences(state);
    app::SetWifiAutoConnectPaused(false);
    ResetWifiConnectionState(state);
    state->wifi_scan_on_ready = false;
    state->wifi_scan_request_generation = 0;
    state->config.wifi->SetWifiEnabled(false);
  }
  UpdateSettingsWifiValue(state);
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
  RequestWifiScan(state, true);
  if (state != nullptr) {
    UpdateWifiRefreshAnimation(state);
  }
}

/**
 * @brief 保存用户确认的凭据并发起 WLAN 连接
 * @param state 设置页状态
 * @param password 本次连接密码，开放网络可为空字符串
 * @return 连接命令发送成功返回 true，否则返回 false
 */
bool StartWifiConnection(SettingsViewState* state, const char* password) {
  if (state == nullptr || state->config.wifi == nullptr ||
      state->wifi_pending_action.ssid[0] == '\0') {
    return false;
  }
  if (state->wifi_connect_waiting) {
    return false;
  }

  const char* connect_password = password == nullptr ? "" : password;
  if (connect_password != state->wifi_pending_action.password) {
    std::snprintf(state->wifi_pending_action.password,
        sizeof(state->wifi_pending_action.password), "%s",
        connect_password);
  }

  state->wifi_enabled_requested = true;
  if (state->wifi_pending_action.saved) {
    SaveWifiNetworkCredential(
        state->wifi_pending_action, connect_password);
  }
  SaveWifiPreferences(state);
  UpdateSettingsWifiValue(state);
  state->wifi_scan_on_ready = false;
  app::SetWifiAutoConnectPaused(true);
  if (state->config.wifi->ConnectWifi(
          state->wifi_pending_action.ssid, connect_password)) {
    state->wifi_connect_waiting = true;
    state->wifi_connection_retry_ready = true;
    state->wifi_connect_started_ms = lv_tick_get();
  } else {
    state->wifi_connect_waiting = false;
    state->wifi_connection_retry_ready = CanRetryPendingWifiConnection(state);
    state->wifi_connect_started_ms = 0;
    app::SetWifiAutoConnectPaused(false);
  }
  state->wifi_refresh_force = true;
  if (state->wifi_refresh_timer != nullptr) {
    lv_timer_ready(state->wifi_refresh_timer);
  }
  return state->wifi_connect_waiting;
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
  if (action->state->wifi_closing || action->state->wifi_page == nullptr) {
    lv_event_stop_bubbling(event);
    lv_event_stop_processing(event);
    return;
  }
  if (action->state->wifi_connect_waiting) {
    lv_event_stop_bubbling(event);
    lv_event_stop_processing(event);
    return;
  }

  if (action->saved) {
    action->state->wifi_pending_action = *action;
    const app::WifiSavedNetwork* saved =
        FindSavedWifiNetworkConst(action->ssid);
    if (saved != nullptr) {
      std::snprintf(action->state->wifi_pending_action.password,
          sizeof(action->state->wifi_pending_action.password), "%s",
          saved->password);
      if (saved->secure && saved->password[0] == '\0') {
        ShowWifiConnectSheet(action->state,
            action->state->wifi_pending_action, nullptr, true);
        lv_event_stop_bubbling(event);
        lv_event_stop_processing(event);
        return;
      }
    }
    StartWifiConnection(
        action->state, action->state->wifi_pending_action.password);
    lv_event_stop_bubbling(event);
    lv_event_stop_processing(event);
    return;
  }

  hal::WifiStatus status;
  ReadWifiSnapshots(action->state->config, &status, nullptr);
  const bool retry_failed_ssid =
      std::strcmp(action->ssid, action->state->wifi_pending_action.ssid) == 0 &&
      action->state->wifi_connection_retry_ready &&
      (status.start_failed || status.disconnect_reason != 0);
  if (retry_failed_ssid && CanRetryPendingWifiConnection(action->state)) {
    StartWifiConnection(
        action->state, action->state->wifi_pending_action.password);
    lv_event_stop_bubbling(event);
    lv_event_stop_processing(event);
    return;
  }

  ShowWifiConnectSheet(action->state, *action);
}

/**
 * @brief 点击失败连接卡片后重新连接上一次 WLAN
 * @param event LVGL 事件对象
 */
void WifiConnectionCardRetryClickedEventCallback(lv_event_t* event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
    return;
  }

  auto* state = static_cast<SettingsViewState*>(lv_event_get_user_data(event));
  if (state != nullptr && state->wifi_pending_action.ssid[0] != '\0') {
    const app::WifiSavedNetwork* saved =
        FindSavedWifiNetworkConst(state->wifi_pending_action.ssid);
    if (saved != nullptr) {
      std::snprintf(state->wifi_pending_action.password,
          sizeof(state->wifi_pending_action.password), "%s",
          saved->password);
      state->wifi_pending_action.saved = true;
    }
  }
  if (state != nullptr && CanRetryPendingWifiConnection(state)) {
    StartWifiConnection(state, state->wifi_pending_action.password);
  } else if (state != nullptr && state->wifi_pending_action.ssid[0] != '\0') {
    ShowWifiConnectSheet(state, state->wifi_pending_action);
  }
  lv_event_stop_bubbling(event);
  lv_event_stop_processing(event);
}

/**
 * @brief 打开被点击 WLAN 的网络详情页
 * @param event 携带 WifiNetworkAction 的 LVGL 事件对象
 */
void WifiNetworkDetailClickedEventCallback(lv_event_t* event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
    return;
  }

  auto* action =
      static_cast<WifiNetworkAction*>(lv_event_get_user_data(event));
  if (action == nullptr || action->state == nullptr ||
      action->ssid[0] == '\0') {
    return;
  }

  ShowWifiNetworkDetailPage(action->state, *action);
  lv_event_stop_bubbling(event);
  lv_event_stop_processing(event);
}

/**
 * @brief 打开 WLAN 高级设置页面
 * @param event LVGL 事件对象
 */
void WifiAdvancedClickedEventCallback(lv_event_t* event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
    return;
  }

  ShowWifiAdvancedPage(
      static_cast<SettingsViewState*>(lv_event_get_user_data(event)));
  lv_event_stop_bubbling(event);
  lv_event_stop_processing(event);
}

/**
 * @brief 打开已保存 WLAN 管理页面
 * @param event LVGL 事件对象
 */
void WifiSavedNetworksClickedEventCallback(lv_event_t* event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
    return;
  }

  ShowWifiSavedNetworksPage(
      static_cast<SettingsViewState*>(lv_event_get_user_data(event)));
  lv_event_stop_bubbling(event);
  lv_event_stop_processing(event);
}

/**
 * @brief 关闭 WLAN 连接弹窗
 * @param event LVGL 事件对象
 */
void WifiModalCancelClickedEventCallback(lv_event_t* event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
    return;
  }

  auto* state = static_cast<SettingsViewState*>(lv_event_get_user_data(event));
  if (state != nullptr) {
    state->wifi_connection_retry_ready = false;
  }
  lv_event_stop_bubbling(event);
  lv_event_stop_processing(event);
  CloseWifiModal(state);
}

/**
 * @brief 阻止底部弹窗内容点击冒泡到遮罩层
 * @param event LVGL 事件对象
 */
void WifiModalContentClickedEventCallback(lv_event_t* event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
    return;
  }
  auto* state = static_cast<SettingsViewState*>(lv_event_get_user_data(event));
  lv_obj_t* target = lv_event_get_target_obj(event);
  if (state != nullptr && state->wifi_password_keyboard != nullptr &&
      state->wifi_password_text_area != nullptr &&
      !IsObjectOrChildOf(target, state->wifi_password_keyboard) &&
      !IsObjectOrChildOf(target, state->wifi_password_text_area)) {
    HideSharedKeyboard(state->wifi_password_keyboard);
    MoveWifiConnectSheetForKeyboard(state, false);
    lv_obj_remove_state(state->wifi_password_text_area, LV_STATE_FOCUSED);
  }
  lv_event_stop_bubbling(event);
}

/**
 * @brief 用弹窗中的密码发起 WLAN 连接
 * @param event LVGL 事件对象
 */
void WifiModalConnectClickedEventCallback(lv_event_t* event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
    return;
  }

  auto* state = static_cast<SettingsViewState*>(lv_event_get_user_data(event));
  if (state == nullptr || state->config.wifi == nullptr ||
      state->wifi_pending_action.ssid[0] == '\0') {
    return;
  }

  const char* password = state->wifi_pending_action.password;
  if (state->wifi_password_text_area != nullptr) {
    password = lv_textarea_get_text(state->wifi_password_text_area);
    if (std::strlen(password) < kWifiPasswordMinLength) {
      UpdateWifiConnectButtonState(state);
      lv_event_stop_bubbling(event);
      lv_event_stop_processing(event);
      return;
    }
  }
  state->wifi_pending_action.saved = true;
  StartWifiConnection(state, password);
  lv_event_stop_bubbling(event);
  lv_event_stop_processing(event);
  CloseWifiModal(state);
}

/**
 * @brief 切换指定 WLAN 的自动连接状态
 * @param event LVGL 事件对象
 */
void WifiAutoConnectChangedEventCallback(lv_event_t* event) {
  if (lv_event_get_code(event) != LV_EVENT_VALUE_CHANGED) {
    return;
  }

  auto* state = static_cast<SettingsViewState*>(lv_event_get_user_data(event));
  lv_obj_t* target = lv_event_get_target_obj(event);
  if (state == nullptr || target == nullptr ||
      state->wifi_pending_action.ssid[0] == '\0') {
    return;
  }

  app::WifiSavedNetwork* saved =
      FindSavedWifiNetwork(state->wifi_pending_action.ssid);
  if (saved != nullptr) {
    saved->auto_connect = lv_obj_has_state(target, LV_STATE_CHECKED);
    SaveWifiNetworks();
  }
  lv_event_stop_bubbling(event);
  lv_event_stop_processing(event);
}

/**
 * @brief 删除当前 WLAN 保存信息
 * @param event LVGL 事件对象
 */
void WifiDeleteNetworkClickedEventCallback(lv_event_t* event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
    return;
  }

  auto* state = static_cast<SettingsViewState*>(lv_event_get_user_data(event));
  if (state == nullptr) {
    return;
  }

  ShowWifiDeleteNetworkSheet(
      state, state->wifi_pending_action.ssid, true, nullptr);
  lv_event_stop_bubbling(event);
  lv_event_stop_processing(event);
}

/**
 * @brief 打开已保存 WLAN 的密码修改弹窗
 * @param event LVGL 事件对象
 */
void WifiModifyNetworkClickedEventCallback(lv_event_t* event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
    return;
  }

  auto* state = static_cast<SettingsViewState*>(lv_event_get_user_data(event));
  if (state == nullptr || state->wifi_pending_action.ssid[0] == '\0') {
    return;
  }
  const app::WifiSavedNetwork* saved =
      FindSavedWifiNetworkConst(state->wifi_pending_action.ssid);
  if (saved == nullptr || !saved->secure) {
    return;
  }

  WifiNetworkAction action = state->wifi_pending_action;
  action.saved = true;
  action.password[0] = '\0';
  ShowWifiConnectSheet(state, action, nullptr, true);
  lv_event_stop_bubbling(event);
  lv_event_stop_processing(event);
}

/**
 * @brief 确认删除当前 WLAN 保存信息
 * @param event LVGL 事件对象
 */
void WifiDeleteConfirmClickedEventCallback(lv_event_t* event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
    return;
  }

  auto* state = static_cast<SettingsViewState*>(lv_event_get_user_data(event));
  if (state == nullptr) {
    return;
  }

  char deleted_ssid[hal::kWifiSsidMaxLength + 1] = {};
  std::snprintf(deleted_ssid, sizeof(deleted_ssid), "%s",
      state->wifi_pending_action.ssid);
  const bool close_sub_page = state->wifi_delete_close_sub_page;
  lv_obj_t* saved_delete_row = state->wifi_saved_delete_row;
  lv_obj_t* saved_delete_body =
      saved_delete_row == nullptr ? nullptr : lv_obj_get_parent(
                                             saved_delete_row);
  if (deleted_ssid[0] != '\0') {
    ForgetSavedWifiNetwork(state, deleted_ssid);
    RefreshWifiPage(state, true);
    if (close_sub_page) {
      CloseWifiSubPage(state, true);
    } else {
      if (saved_delete_row != nullptr) {
        lv_obj_delete(saved_delete_row);
      }
      if (saved_delete_body != nullptr && g_wifi_saved_network_count == 0) {
        CreateWifiSavedEmptyText(saved_delete_body, state->config.width);
      }
    }
  }
  state->wifi_saved_delete_row = nullptr;
  state->wifi_delete_close_sub_page = false;
  CloseWifiModal(state);
  lv_event_stop_bubbling(event);
  lv_event_stop_processing(event);
}

/**
 * @brief 处理管理已保存网络页的单项删除按钮
 * @param event LVGL 事件对象
 */
void WifiSavedNetworkDeleteClickedEventCallback(lv_event_t* event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
    return;
  }

  auto* action =
      static_cast<WifiNetworkAction*>(lv_event_get_user_data(event));
  lv_obj_t* button = lv_event_get_target_obj(event);
  if (action == nullptr || action->state == nullptr ||
      action->ssid[0] == '\0' || button == nullptr) {
    return;
  }

  lv_obj_t* row = lv_obj_get_parent(button);
  ShowWifiDeleteNetworkSheet(action->state, action->ssid, false, row);
  lv_event_stop_bubbling(event);
  lv_event_stop_processing(event);
}

/**
 * @brief WLAN 页面打开期间轮询 WiFi 状态并刷新内容
 * @param timer LVGL 定时器对象
 */
void WifiRefreshTimerCallback(lv_timer_t* timer) {
  auto* state = static_cast<SettingsViewState*>(lv_timer_get_user_data(timer));
  UpdateWifiConnectTimeout(state);
  if (state != nullptr && state->wifi_enabled_requested) {
    hal::WifiStatus status;
    hal::WifiScanStatus scan_status;
    ReadWifiSnapshots(state->config, &status, &scan_status);

    if ((status.connected || status.got_ip) && state->wifi_scan_on_ready) {
      state->wifi_scan_on_ready = false;
    } else if (state->wifi_connect_waiting) {
      state->wifi_scan_on_ready = false;
    } else if (scan_status.scan_running ||
        scan_status.generation != state->wifi_scan_request_generation) {
      state->wifi_scan_on_ready = false;
    } else if (state->wifi_scan_on_ready && !status.init_task_running) {
      if (state->config.wifi != nullptr) {
        state->config.wifi->StartWifiScan();
        ReadWifiSnapshots(state->config, nullptr, &scan_status);
        if (scan_status.scan_running || scan_status.scan_failed ||
            scan_status.generation != state->wifi_scan_request_generation) {
          state->wifi_scan_on_ready = false;
        }
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
  buffer[0] = '\0';

  if (status.ssid[0] == '\0') {
    return;
  }
  std::snprintf(buffer, buffer_size, "%s", status.ssid);
}

/**
 * @brief 保存运行期 WLAN 凭据
 */
void SaveWifiNetworks() {
  app::UpdateWifiSavedNetworks(
      g_wifi_saved_networks, g_wifi_saved_network_count);
}

void SaveWifiPreferences(const SettingsViewState* state) {
  if (state == nullptr) {
    return;
  }

  app::WifiPreferences preferences;
  preferences.enabled_requested = state->wifi_enabled_requested;
  app::UpdateWifiPreferences(preferences);
}

/**
 * @brief 从长期 RAM 缓存加载运行期 WLAN 凭据
 */
void LoadSavedWifiNetworksFromCache() {
  if (g_wifi_saved_networks_loaded) {
    return;
  }
  if (app::GetWifiSavedNetworks(g_wifi_saved_networks,
          app::kWifiSavedNetworkCapacity, &g_wifi_saved_network_count)) {
    g_wifi_saved_networks_loaded = true;
  }
}

/**
 * @brief 从长期 RAM 缓存加载 WLAN 开关和自动连接偏好
 * @param state 设置页状态
 * @param fallback_enabled 未保存开关状态时使用的默认 WLAN 开关状态
 */
void LoadWifiPreferencesFromCache(
    SettingsViewState* state, bool fallback_enabled) {
  if (state == nullptr) {
    return;
  }

  const app::WifiPreferences preferences = app::GetWifiPreferences();
  state->wifi_enabled_requested = app::HasWifiPreferences()
                                      ? preferences.enabled_requested
                                      : fallback_enabled;
}

app::WifiSavedNetwork* FindSavedWifiNetwork(const char* ssid) {
  if (ssid == nullptr || ssid[0] == '\0') {
    return nullptr;
  }
  for (size_t i = 0; i < g_wifi_saved_network_count; ++i) {
    if (std::strcmp(g_wifi_saved_networks[i].ssid, ssid) == 0) {
      return &g_wifi_saved_networks[i];
    }
  }
  return nullptr;
}

const app::WifiSavedNetwork* FindSavedWifiNetworkConst(const char* ssid) {
  return FindSavedWifiNetwork(ssid);
}

bool IsSavedWifiSsid(const char* ssid) {
  return FindSavedWifiNetworkConst(ssid) != nullptr;
}

void SaveWifiNetworkCredential(
    const WifiNetworkAction& action, const char* password) {
  if (action.ssid[0] == '\0') {
    return;
  }

  app::WifiSavedNetwork* saved = FindSavedWifiNetwork(action.ssid);
  if (saved == nullptr) {
    if (g_wifi_saved_network_count >= app::kWifiSavedNetworkCapacity) {
      return;
    }
    saved = &g_wifi_saved_networks[g_wifi_saved_network_count++];
    *saved = app::WifiSavedNetwork();
  }

  std::snprintf(saved->ssid, sizeof(saved->ssid), "%s", action.ssid);
  std::snprintf(saved->password, sizeof(saved->password), "%s",
      password == nullptr ? "" : password);
  saved->secure = action.secure;
  saved->is_5g = action.is_5g;
  saved->rssi = action.rssi;
  SaveWifiNetworks();
}

void RemoveSavedWifiNetwork(const char* ssid) {
  if (ssid == nullptr || ssid[0] == '\0') {
    return;
  }
  for (size_t i = 0; i < g_wifi_saved_network_count; ++i) {
    if (std::strcmp(g_wifi_saved_networks[i].ssid, ssid) != 0) {
      continue;
    }
    for (size_t j = i + 1; j < g_wifi_saved_network_count; ++j) {
      g_wifi_saved_networks[j - 1] = g_wifi_saved_networks[j];
    }
    --g_wifi_saved_network_count;
    g_wifi_saved_networks[g_wifi_saved_network_count] =
        app::WifiSavedNetwork();
    SaveWifiNetworks();
    return;
  }
}

void ForgetSavedWifiNetwork(SettingsViewState* state, const char* ssid) {
  if (state == nullptr || ssid == nullptr || ssid[0] == '\0') {
    return;
  }

  hal::WifiStatus status;
  ReadWifiSnapshots(state->config, &status, nullptr);
  const bool deleted_current_connection =
      (status.connected || status.got_ip) &&
      std::strcmp(status.ssid, ssid) == 0;
  RemoveSavedWifiNetwork(ssid);

  if (std::strcmp(state->wifi_pending_action.ssid, ssid) == 0) {
    state->wifi_pending_action = WifiNetworkAction();
  }
  ResetWifiConnectionState(state);
  state->wifi_scan_on_ready = false;
  if (deleted_current_connection && state->config.wifi != nullptr) {
    state->config.wifi->CancelWifiConnection();
  }
  state->wifi_refresh_force = true;
}

bool FindScannedWifiNetwork(const hal::WifiScanStatus& scan_status,
    const char* ssid, hal::WifiNetworkInfo* output) {
  if (ssid == nullptr || ssid[0] == '\0') {
    return false;
  }
  for (size_t i = 0; i < scan_status.network_count; ++i) {
    const hal::WifiNetworkInfo& network = scan_status.networks[i];
    if (std::strcmp(network.ssid, ssid) != 0) {
      continue;
    }
    if (output != nullptr) {
      *output = network;
    }
    return true;
  }
  return false;
}

/**
 * @brief 用扫描结果刷新已保存 WLAN 的信号、安全性和频段信息
 * @param scan_status 扫描状态
 */
void SyncSavedWifiNetworksWithScan(
    const hal::WifiScanStatus& scan_status) {
  for (size_t i = 0; i < g_wifi_saved_network_count; ++i) {
    hal::WifiNetworkInfo network = {};
    if (!FindScannedWifiNetwork(
            scan_status, g_wifi_saved_networks[i].ssid, &network)) {
      continue;
    }
    g_wifi_saved_networks[i].secure = network.secure;
    g_wifi_saved_networks[i].is_5g = network.is_5g;
    g_wifi_saved_networks[i].rssi = network.rssi;
  }
}

/**
 * @brief 记录本页已经显示过的 SSID，避免不同分组重复展示
 */
void MarkShownWifiSsid(char shown_ssids[][hal::kWifiSsidMaxLength + 1],
    size_t* shown_count, const char* ssid) {
  if (shown_ssids == nullptr || shown_count == nullptr ||
      ssid == nullptr || ssid[0] == '\0' ||
      *shown_count >= kWifiActionCapacity) {
    return;
  }
  std::snprintf(shown_ssids[*shown_count],
      hal::kWifiSsidMaxLength + 1, "%.*s",
      static_cast<int>(hal::kWifiSsidMaxLength), ssid);
  ++(*shown_count);
}

/**
 * @brief 判断 SSID 是否已经在本页显示过
 */
bool IsShownWifiSsid(
    char shown_ssids[][hal::kWifiSsidMaxLength + 1],
    size_t shown_count, const char* ssid) {
  if (ssid == nullptr || ssid[0] == '\0') {
    return false;
  }
  for (size_t i = 0; i < shown_count; ++i) {
    if (std::strcmp(shown_ssids[i], ssid) == 0) {
      return true;
    }
  }
  return false;
}

bool CanRetryPendingWifiConnection(const SettingsViewState* state) {
  if (state == nullptr || state->wifi_pending_action.ssid[0] == '\0') {
    return false;
  }
  if (!state->wifi_pending_action.secure) {
    return true;
  }
  const app::WifiSavedNetwork* saved =
      FindSavedWifiNetworkConst(state->wifi_pending_action.ssid);
  return saved != nullptr && saved->password[0] != '\0';
}

/**
 * @brief 判断断开原因是否表示密码或认证失败
 * @param reason ESP WiFi 断开原因
 * @return 密码或认证失败返回 true
 */
bool IsWifiAuthenticationFailure(int reason) {
  return reason == WIFI_REASON_AUTH_FAIL ||
      reason == WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT ||
      reason == WIFI_REASON_HANDSHAKE_TIMEOUT;
}

/**
 * @brief 根据 RSSI 计算 WiFi 信号等级
 * @param rssi 信号强度，单位为 dBm
 * @return 1 到 5 的信号等级
 */
int WifiSignalLevelForRssi(int rssi) {
  if (rssi >= -50) {
    return 5;
  }
  if (rssi >= -60) {
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
    case 5:
      return icon::kSignalWifi4Bar;
    case 4:
      return icon::kNetworkWifi;
    case 3:
      return icon::kNetworkWifi3Bar;
    case 2:
      return icon::kNetworkWifi2Bar;
    default:
      return icon::kNetworkWifi1Bar;
  }
}

/**
 * @brief 生成用于判断 WLAN 页面可见状态是否变化的摘要
 * @param status 当前 WiFi 连接状态
 * @param scan_status 当前 WiFi 扫描状态
 * @param internet_state 当前互联网可用性状态
 * @return 当前可见状态摘要
 */
uint32_t MakeWifiRefreshKey(
    const hal::WifiStatus& status, const hal::WifiScanStatus& scan_status,
    app::InternetAccessState internet_state) {
  uint32_t key = scan_status.generation * 131U;
  key ^= static_cast<uint32_t>(scan_status.network_count) << 16;
  key ^= scan_status.scan_failed ? 0x0002U : 0U;
  key ^= status.init_task_running ? 0x0004U : 0U;
  key ^= status.running ? 0x0008U : 0U;
  key ^= status.connected ? 0x0010U : 0U;
  key ^= status.got_ip ? 0x0020U : 0U;
  key ^= status.start_failed ? 0x0040U : 0U;
  key ^= static_cast<uint32_t>(status.disconnect_reason & 0xFF) << 8;
  key ^= static_cast<uint32_t>(internet_state) << 24;
  return key;
}

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

void UpdateWifiConnectTimeout(SettingsViewState* state) {
  if (state == nullptr || !state->wifi_connect_waiting) {
    return;
  }
  if (state->config.wifi == nullptr) {
    state->wifi_connect_waiting = false;
    state->wifi_connect_started_ms = 0;
    app::SetWifiAutoConnectPaused(false);
    return;
  }

  hal::WifiStatus status;
  ReadWifiSnapshots(state->config, &status, nullptr);
  const bool pending_network_connected =
      status.got_ip &&
      std::strcmp(status.ssid, state->wifi_pending_action.ssid) == 0;
  if (pending_network_connected) {
    state->wifi_connect_waiting = false;
    state->wifi_connection_retry_ready = false;
    state->wifi_connect_started_ms = 0;
    state->wifi_refresh_force = true;
    app::SetWifiAutoConnectPaused(false);
    return;
  }

  if (IsWifiAuthenticationFailure(status.disconnect_reason)) {
    WifiNetworkAction retry_action = state->wifi_pending_action;
    state->config.wifi->CancelWifiConnection();
    state->wifi_connect_waiting = false;
    state->wifi_connection_retry_ready = false;
    state->wifi_connect_started_ms = 0;
    state->wifi_refresh_force = true;
    retry_action.password[0] = '\0';
    if (!ShowWifiConnectSheet(
            state, retry_action, "Incorrect password. Try again.", true)) {
      app::SetWifiAutoConnectPaused(false);
    }
    return;
  }

  const uint32_t elapsed_ms =
      lv_tick_get() - state->wifi_connect_started_ms;
  if (elapsed_ms < kWifiConnectTimeoutMs) {
    return;
  }

  state->config.wifi->CancelWifiConnection();
  state->wifi_connect_waiting = false;
  state->wifi_connection_retry_ready = CanRetryPendingWifiConnection(state);
  state->wifi_connect_started_ms = 0;
  state->wifi_refresh_force = true;
  app::SetWifiAutoConnectPaused(false);
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

void StopWifiRefreshIconSpin(SettingsViewState* state) {
  if (state == nullptr || state->wifi_refresh_icon == nullptr) {
    return;
  }

  lv_anim_delete(state->wifi_refresh_icon, SetWifiRefreshIconRotation);
  SetWifiRefreshIconRotation(state->wifi_refresh_icon, 0);
}

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

void RequestWifiScan(SettingsViewState* state, bool force) {
  if (state == nullptr || state->config.wifi == nullptr) {
    return;
  }

  state->wifi_enabled_requested = true;
  hal::WifiStatus status;
  hal::WifiScanStatus scan_status;
  ReadWifiSnapshots(state->config, &status, &scan_status);
  if (force && state->wifi_connect_waiting &&
      (status.connected || status.got_ip)) {
    state->wifi_connect_waiting = false;
    state->wifi_connection_retry_ready = false;
    state->wifi_connect_started_ms = 0;
  }
  if (state->wifi_connect_waiting ||
      (!force && (status.connected || status.got_ip))) {
    state->wifi_scan_on_ready = false;
    return;
  }
  state->wifi_scan_on_ready = true;
  state->wifi_scan_request_generation = scan_status.generation;
  if (scan_status.scan_running) {
    state->wifi_scan_on_ready = false;
    return;
  }
  if (!state->config.wifi->StartWifiScan()) {
    state->wifi_scan_on_ready = false;
    return;
  }
  ReadWifiSnapshots(state->config, nullptr, &scan_status);
  state->wifi_scan_on_ready =
      !scan_status.scan_running && !scan_status.scan_failed &&
      scan_status.generation == state->wifi_scan_request_generation;
}

/**
 * @brief 为 WiFi 热点行分配稳定的 LVGL 回调参数
 * @param state 设置页状态
 * @param ssid 热点 SSID
 * @param password 热点密码，开放热点可为空
 * @return 分配到的参数地址，参数池已满时返回 nullptr
 */
WifiNetworkAction* ReserveWifiNetworkAction(SettingsViewState* state,
    const char* ssid, bool secure, bool is_5g, int rssi,
    const char* password, bool saved) {
  if (state == nullptr || ssid == nullptr || ssid[0] == '\0' ||
      state->wifi_action_count >= kWifiActionCapacity) {
    return nullptr;
  }

  WifiNetworkAction* action = &state->wifi_actions[state->wifi_action_count++];
  *action = WifiNetworkAction();
  action->state = state;
  std::snprintf(action->ssid, sizeof(action->ssid), "%.*s",
      static_cast<int>(hal::kWifiSsidMaxLength), ssid);
  if (password != nullptr) {
    std::snprintf(action->password, sizeof(action->password), "%s", password);
  }
  action->secure = secure;
  action->is_5g = is_5g;
  action->rssi = rssi;
  action->saved = saved;
  return action;
}

/**
 * @brief 为管理已保存网络页分配删除按钮参数
 * @param state 设置页状态
 * @param ssid 待删除的热点名称
 * @return 分配到的参数地址，参数池已满时返回 nullptr
 */
WifiNetworkAction* ReserveWifiSavedDeleteAction(
    SettingsViewState* state, const char* ssid) {
  if (state == nullptr || ssid == nullptr || ssid[0] == '\0' ||
      state->wifi_saved_delete_action_count >=
          app::kWifiSavedNetworkCapacity) {
    return nullptr;
  }

  WifiNetworkAction* action =
      &state->wifi_saved_delete_actions[
          state->wifi_saved_delete_action_count++];
  *action = WifiNetworkAction();
  action->state = state;
  std::snprintf(action->ssid, sizeof(action->ssid), "%.*s",
      static_cast<int>(hal::kWifiSsidMaxLength), ssid);
  action->saved = true;
  return action;
}

/**
 * @brief 创建 WLAN 页面返回按钮和标题
 * @param parent 父对象
 * @param state 设置页状态
 * @return 创建成功返回 true，否则返回 false
 */
bool CreateWifiHeader(lv_obj_t* parent, SettingsViewState* state) {
  lv_obj_t* back_button = CreateToolbarButton(parent, kDetailBackButtonLeft,
      kDetailBackButtonTop, WifiBackClickedEventCallback, state);
  if (back_button == nullptr) {
    return false;
  }
  lv_obj_t* back_icon = CreateLabel(back_button, icon::kArrowBack,
      lv_color_hex(kDetailBackColor), MaterialIconFont44());
  if (back_icon == nullptr) {
    return false;
  }
  lv_obj_align(back_icon, LV_ALIGN_CENTER, kDetailBackIconOffsetX, 0);

  lv_obj_t* title =
      CreateLabel(parent, "WLAN", lv_color_hex(kTitleColor), Font32());
  if (title == nullptr) {
    return false;
  }
  lv_obj_set_width(title, state->config.width);
  lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
  lv_obj_align(title, LV_ALIGN_TOP_MID, 0, kDetailTitleTop);
  return true;
}

/**
 * @brief 绘制加密 WLAN 行右侧的小锁图标
 * @param parent 父对象
 * @param x 左侧坐标
 * @param y 顶部坐标
 * @param color 锁图标颜色
 * @return 创建成功返回 true，否则返回 false
 */
bool CreateWifiSmallLock(lv_obj_t* parent, int x, int y, uint32_t color) {
  lv_obj_t* lock = CreateLabel(
      parent, icon::kLock, lv_color_hex(color), MaterialIconFont32());
  if (lock == nullptr) {
    return false;
  }
  lv_obj_remove_flag(lock, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_flag(lock, LV_OBJ_FLAG_GESTURE_BUBBLE);
  lv_obj_set_size(lock, 28, 28);
  lv_obj_set_pos(lock, x - 3, y - 2);
  return true;
}

/**
 * @brief 创建 WLAN 行末尾的圆形箭头按钮
 * @param parent 父对象
 * @return 创建成功返回对象指针，否则返回 nullptr
 */
lv_obj_t* CreateWifiCircleArrow(lv_obj_t* parent) {
  lv_obj_t* circle = lv_button_create(parent);
  if (circle == nullptr) {
    return nullptr;
  }
  lv_obj_remove_flag(circle, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(circle, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
  lv_obj_remove_flag(circle, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_flag(circle, LV_OBJ_FLAG_GESTURE_BUBBLE);
  lv_obj_set_size(circle, kWifiCircleButtonSize, kWifiCircleButtonSize);
  lv_obj_set_style_bg_color(circle, lv_color_hex(kWifiControlColor),
      LV_PART_MAIN);
  lv_obj_set_style_bg_opa(circle, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_border_width(circle, 0, LV_PART_MAIN);
  lv_obj_set_style_radius(circle, kWifiCircleButtonSize / 2, LV_PART_MAIN);
  lv_obj_set_style_shadow_width(circle, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(circle, 0, LV_PART_MAIN);
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
  if (std::strcmp(text, "Advanced settings") == 0) {
    lv_obj_add_event_cb(row, WifiAdvancedClickedEventCallback,
        LV_EVENT_CLICKED, state);
  } else if (std::strcmp(text, "Manage saved networks") == 0) {
    lv_obj_add_event_cb(row, WifiSavedNetworksClickedEventCallback,
        LV_EVENT_CLICKED, state);
  } else if (std::strcmp(text, "Modify network") == 0) {
    lv_obj_add_event_cb(row, WifiModifyNetworkClickedEventCallback,
        LV_EVENT_CLICKED, state);
  } else if (std::strcmp(text, "Delete network") == 0) {
    lv_obj_add_event_cb(row, WifiDeleteNetworkClickedEventCallback,
        LV_EVENT_CLICKED, state);
  }

  const bool delete_network = std::strcmp(text, "Delete network") == 0;
  const uint32_t label_color =
      delete_network ? 0xE53935 : kPrimaryTextColor;
  lv_obj_t* label = CreateLabel(row, text, lv_color_hex(label_color),
      delete_network ? Font28() : Font32());
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
 * @param ssid 热点 SSID
 * @param state_text 连接状态文本
 * @param card_color 卡片背景颜色
 * @param rssi 热点信号强度
 * @param y 卡片顶部坐标
 * @param width 页面宽度
 * @param retry_on_click 点击卡片是否重新连接
 * @return 创建成功返回 true，否则返回 false
 */
bool CreateWifiConnectedCard(lv_obj_t* parent, SettingsViewState* state,
    const char* ssid, const char* state_text, uint32_t card_color, int rssi,
    bool is_5g, bool secure, const char* password, int y, int width,
    bool retry_on_click) {
  const int card_width = width - 2 * kWifiSidePadding;
  const bool blue_card = card_color == kWifiBlueColor;
  const bool colored_card =
      blue_card || card_color == kWifiConnectingColor;
  const uint32_t text_color =
      colored_card ? 0xFFFFFF : kPrimaryTextColor;
  const uint32_t subtitle_color =
      colored_card ? (blue_card ? 0xEAF1FF : 0xFFF4E2)
                   : kSecondaryTextColor;
  const uint32_t pressed_color = colored_card
                                     ? (blue_card ? 0x2F70E4 : 0xD88D16)
                                     : 0xEEF3FF;
  lv_obj_t* card = CreateBox(parent, card_width, kWifiConnectedCardHeight,
      card_color, LV_OPA_COVER, kWifiConnectedCardRadius);
  if (card == nullptr) {
    return false;
  }
  lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_pos(card, kWifiSidePadding, y);
  lv_obj_set_style_bg_color(
      card, lv_color_hex(pressed_color), LV_STATE_PRESSED);
  lv_obj_set_style_bg_opa(card, LV_OPA_COVER, LV_STATE_PRESSED);
  if (!AddPressCancelOnLeave(card)) {
    return false;
  }
  if (retry_on_click) {
    lv_obj_add_event_cb(card, WifiConnectionCardRetryClickedEventCallback,
        LV_EVENT_CLICKED, state);
  }

  lv_obj_t* wifi_icon = CreateLabel(card, WifiSignalIconForRssi(rssi),
      lv_color_hex(text_color), MaterialIconFont32());
  if (wifi_icon == nullptr) {
    return false;
  }
  if (state != nullptr) {
    state->wifi_connected_signal_icon = wifi_icon;
  }
  lv_obj_align(wifi_icon, LV_ALIGN_LEFT_MID, 28, -4);

  lv_obj_t* title =
      CreateLabel(card, ssid, lv_color_hex(text_color), Font28());
  if (title == nullptr) {
    return false;
  }
  lv_obj_set_size(title, card_width - 240,
      static_cast<int>(lv_font_get_line_height(Font28())));
  lv_label_set_long_mode(title, LV_LABEL_LONG_SCROLL_CIRCULAR);
  lv_obj_align(title, LV_ALIGN_LEFT_MID, 82, -16);

  lv_obj_t* subtitle = CreateLabel(card, state_text,
      lv_color_hex(subtitle_color), Font22());
  if (subtitle == nullptr) {
    return false;
  }
  lv_obj_set_width(subtitle, card_width - 210);
  lv_obj_align(subtitle, LV_ALIGN_LEFT_MID, 82, 22);

  if (is_5g) {
    lv_obj_t* tag = CreateWifi5GTag(card);
    if (tag == nullptr) {
      return false;
    }
    if (colored_card) {
      lv_obj_set_style_border_color(
          tag, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
      lv_obj_t* label = lv_obj_get_child(tag, 0);
      if (label != nullptr) {
        lv_obj_set_style_text_color(label, lv_color_hex(0xFFFFFF),
            LV_PART_MAIN);
      }
    }
    lv_obj_align_to(tag, title, LV_ALIGN_OUT_RIGHT_MID, 0, 0);
  }

  if (secure && !CreateWifiSmallLock(card, card_width - 112,
                    (kWifiConnectedCardHeight - 26) / 2,
                    colored_card ? 0xFFFFFF : kWifiMutedColor)) {
    return false;
  }

  WifiNetworkAction* action = ReserveWifiNetworkAction(state, ssid, secure,
      is_5g, rssi, password == nullptr ? "" : password,
      IsSavedWifiSsid(ssid));
  lv_obj_t* arrow_circle = CreateWifiCircleArrow(card);
  if (arrow_circle == nullptr) {
    return false;
  }
  if (colored_card) {
    lv_obj_set_style_bg_color(
        arrow_circle, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(arrow_circle, LV_OPA_30, LV_PART_MAIN);
    lv_obj_t* arrow = lv_obj_get_child(arrow_circle, 0);
    if (arrow != nullptr) {
      lv_obj_set_style_text_color(arrow, lv_color_hex(0xFFFFFF),
          LV_PART_MAIN);
    }
  }
  if (action != nullptr) {
    lv_obj_add_flag(arrow_circle, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(arrow_circle, WifiNetworkDetailClickedEventCallback,
        LV_EVENT_CLICKED, action);
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
    const char* password, bool saved, int y, int width) {
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
  WifiNetworkAction* action =
      ReserveWifiNetworkAction(state, ssid, secure, show_tag, rssi, password,
          saved);
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
  lv_obj_set_size(title,
      row_width - kWifiNetworkTextLeft - kWifiNetworkRightControlWidth -
          tag_reserve,
      static_cast<int>(lv_font_get_line_height(Font28())));
  lv_label_set_long_mode(title, LV_LABEL_LONG_SCROLL_CIRCULAR);
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
                    (kWifiNetworkRowHeight - 26) / 2, kWifiMutedColor)) {
    return false;
  }

  lv_obj_t* arrow_circle = CreateWifiCircleArrow(row);
  if (arrow_circle == nullptr) {
    return false;
  }
  if (action != nullptr) {
    lv_obj_add_flag(arrow_circle, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(arrow_circle, WifiNetworkDetailClickedEventCallback,
        LV_EVENT_CLICKED, action);
  }
  lv_obj_align(arrow_circle, LV_ALIGN_RIGHT_MID, -kWifiNetworkArrowRight, 0);
  return true;
}

/**
 * @brief 创建管理已保存网络页的简化删除行
 * @param parent 父对象
 * @param state 设置页状态
 * @param ssid 已保存热点 SSID
 * @param width 页面宽度
 * @return 创建成功返回 true，否则返回 false
 */
bool CreateWifiSavedManageRow(
    lv_obj_t* parent, SettingsViewState* state, const char* ssid, int width) {
  const int button_width = 142;
  const int button_height = 62;
  lv_obj_t* row = lv_obj_create(parent);
  if (row == nullptr) {
    return false;
  }
  lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(row, LV_OBJ_FLAG_GESTURE_BUBBLE);
  lv_obj_set_size(row, width, kWifiNetworkRowHeight + 12);
  lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_set_style_border_width(row, 0, LV_PART_MAIN);
  lv_obj_set_style_radius(row, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(row, 0, LV_PART_MAIN);

  lv_obj_t* name =
      CreateLabel(row, ssid, lv_color_hex(kPrimaryTextColor), Font32());
  if (name == nullptr) {
    return false;
  }
  lv_obj_set_size(name,
      width - 2 * kWifiSidePadding - button_width - 28,
      static_cast<int>(lv_font_get_line_height(Font32())));
  lv_label_set_long_mode(name, LV_LABEL_LONG_SCROLL_CIRCULAR);
  lv_obj_align(name, LV_ALIGN_LEFT_MID, kWifiSidePadding, 0);

  lv_obj_t* button = lv_button_create(row);
  if (button == nullptr) {
    return false;
  }
  lv_obj_remove_flag(button, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(button, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
  lv_obj_add_flag(button, LV_OBJ_FLAG_GESTURE_BUBBLE);
  lv_obj_set_size(button, button_width, button_height);
  lv_obj_align(button, LV_ALIGN_RIGHT_MID, -kWifiSidePadding, 0);
  lv_obj_set_style_bg_color(button, lv_color_hex(0xFFECEE), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(button, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_bg_color(
      button, lv_color_hex(0xFFD7DC), LV_STATE_PRESSED);
  lv_obj_set_style_bg_opa(button, LV_OPA_COVER, LV_STATE_PRESSED);
  lv_obj_set_style_border_width(button, 0, LV_PART_MAIN);
  lv_obj_set_style_radius(button, button_height / 2, LV_PART_MAIN);
  lv_obj_set_style_shadow_width(button, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(button, 0, LV_PART_MAIN);
  if (!AddPressCancelOnLeave(button)) {
    return false;
  }
  WifiNetworkAction* action = ReserveWifiSavedDeleteAction(state, ssid);
  if (action != nullptr) {
    lv_obj_add_event_cb(button, WifiSavedNetworkDeleteClickedEventCallback,
        LV_EVENT_CLICKED, action);
  }

  lv_obj_t* label =
      CreateLabel(button, "Delete", lv_color_hex(0xE53935), Font28());
  if (label == nullptr) {
    return false;
  }
  lv_obj_center(label);
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
  lv_obj_t* button = lv_button_create(parent);
  if (button == nullptr) {
    return false;
  }
  lv_obj_remove_flag(button, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(button, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
  lv_obj_add_flag(button, LV_OBJ_FLAG_GESTURE_BUBBLE);
  lv_obj_set_size(button, 54, 54);
  lv_obj_add_event_cb(button, WifiRefreshButtonClickedEventCallback,
      LV_EVENT_CLICKED, state);
  lv_obj_set_pos(button, width - kWifiSidePadding - 54, y);
  lv_obj_set_style_bg_color(button, lv_color_hex(kWifiControlColor),
      LV_PART_MAIN);
  lv_obj_set_style_bg_opa(button, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_bg_color(
      button, lv_color_hex(0xE6E7EA), LV_STATE_PRESSED);
  lv_obj_set_style_bg_opa(button, LV_OPA_COVER, LV_STATE_PRESSED);
  lv_obj_set_style_border_width(button, 0, LV_PART_MAIN);
  lv_obj_set_style_radius(button, 27, LV_PART_MAIN);
  lv_obj_set_style_shadow_width(button, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(button, 0, LV_PART_MAIN);
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

bool CreateWifiSavedEmptyText(lv_obj_t* parent, int width) {
  if (parent == nullptr) {
    return false;
  }
  lv_obj_set_layout(parent, LV_LAYOUT_NONE);
  lv_obj_set_style_pad_top(parent, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_bottom(parent, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_row(parent, 0, LV_PART_MAIN);
  return CreateWifiSectionLabel(parent, "No saved WLAN.", 0, width);
}

/**
 * @brief 设置对象的垂直坐标，用于底部弹窗动画
 * @param object LVGL 对象
 * @param value Y 坐标
 */
void SetObjectY(void* object, int32_t value) {
  if (object == nullptr) {
    return;
  }
  lv_obj_set_y(static_cast<lv_obj_t*>(object), value);
}

/**
 * @brief 创建 WLAN 子页面顶部栏
 * @param page 页面对象
 * @param state 设置页状态
 * @param title 页面标题
 * @return 创建成功返回 true，否则返回 false
 */
bool CreateWifiSubHeader(
    lv_obj_t* page, SettingsViewState* state, const char* title) {
  lv_obj_t* back_button = CreateToolbarButton(page, kDetailBackButtonLeft,
      kDetailBackButtonTop, WifiSubBackClickedEventCallback, state);
  if (back_button == nullptr) {
    return false;
  }
  lv_obj_t* back_icon = CreateLabel(back_button, icon::kArrowBack,
      lv_color_hex(kDetailBackColor), MaterialIconFont44());
  if (back_icon == nullptr) {
    return false;
  }
  lv_obj_align(back_icon, LV_ALIGN_CENTER, kDetailBackIconOffsetX, 0);

  lv_obj_t* title_label =
      CreateLabel(page, title, lv_color_hex(kTitleColor), Font32());
  if (title_label == nullptr) {
    return false;
  }
  lv_obj_set_size(title_label, state->config.width,
      static_cast<int>(lv_font_get_line_height(Font32())));
  lv_label_set_long_mode(title_label, LV_LABEL_LONG_SCROLL_CIRCULAR);
  lv_obj_set_style_text_align(
      title_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
  lv_obj_align(title_label, LV_ALIGN_TOP_MID, 0, kDetailTitleTop);
  return true;
}

/**
 * @brief 创建 WLAN 子页面内容容器
 * @param page 页面对象
 * @param state 设置页状态
 * @return 创建成功返回内容容器，否则返回 nullptr
 */
lv_obj_t* CreateWifiSubBody(lv_obj_t* page, SettingsViewState* state) {
  lv_obj_t* body = lv_obj_create(page);
  if (body == nullptr) {
    return nullptr;
  }
  MakeTransparent(body);
  lv_obj_set_size(body, state->config.width,
      state->config.height - kWifiBodyTop);
  lv_obj_align(body, LV_ALIGN_TOP_LEFT, 0, kWifiBodyTop);
  lv_obj_set_scroll_dir(body, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(body, LV_SCROLLBAR_MODE_OFF);
  lv_obj_add_flag(body, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(body, LV_OBJ_FLAG_GESTURE_BUBBLE);
  lv_obj_remove_flag(body, LV_OBJ_FLAG_SCROLL_ELASTIC);
  return body;
}

/**
 * @brief 显示 WLAN 子页面
 * @param state 设置页状态
 * @param title 页面标题
 * @param builder 内容构建函数
 * @return 创建成功返回 true，否则返回 false
 */
bool ShowWifiSubPage(SettingsViewState* state, const char* title,
    bool (*builder)(lv_obj_t*, SettingsViewState*)) {
  if (state == nullptr || state->root == nullptr || title == nullptr ||
      builder == nullptr) {
    return false;
  }
  if (state->wifi_sub_closing ||
      state->wifi_sub_page_count >= kWifiSubPageStackCapacity) {
    return false;
  }

  lv_obj_t* page = lv_obj_create(state->root);
  if (page == nullptr) {
    return false;
  }
  state->wifi_sub_pages[state->wifi_sub_page_count] = page;
  ++state->wifi_sub_page_count;
  state->wifi_sub_page = page;
  state->wifi_sub_closing = false;
  lv_obj_remove_flag(page, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(page, LV_OBJ_FLAG_GESTURE_BUBBLE);
  lv_obj_set_size(page, state->config.width, state->config.height);
  lv_obj_set_pos(page, 0, 0);
  lv_obj_set_style_bg_color(page, lv_color_hex(kBackgroundColor),
      LV_PART_MAIN);
  lv_obj_set_style_bg_opa(page, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_border_width(page, 0, LV_PART_MAIN);
  lv_obj_set_style_radius(page, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(page, 0, LV_PART_MAIN);

  if (!CreateWifiSubHeader(page, state, title)) {
    CloseWifiSubPage(state, false);
    return false;
  }
  lv_obj_t* body = CreateWifiSubBody(page, state);
  if (body == nullptr || !builder(body, state)) {
    CloseWifiSubPage(state, false);
    return false;
  }

  if (!StartSlideLeftWindowTransition(page, state->config.width,
          kDetailSlideAnimationMs, state, nullptr)) {
    CloseWifiSubPage(state, false);
    return false;
  }
  if (!RegisterBackNavigationHandler(page, [state]() {
        CloseWifiSubPage(state, true);
      })) {
    CloseWifiSubPage(state, false);
    return false;
  }
  return true;
}

/**
 * @brief 根据信号强度返回展示文本
 * @param rssi 信号强度
 * @return 信号强度文本
 */
const char* WifiSignalText(int rssi) {
  switch (WifiSignalLevelForRssi(rssi)) {
    case 5:
      return "Excellent";
    case 4:
      return "Very good";
    case 3:
      return "Good";
    case 2:
      return "Fair";
    default:
      return "Weak";
  }
}

/**
 * @brief 返回 WLAN 安全性展示文本
 * @param secure 是否需要密码
 * @return 安全性文本
 */
const char* WifiSecurityText(bool secure) {
  return secure ? "WPA/WPA2-Personal" : "Open";
}

/**
 * @brief 创建 WLAN 详情中的信息行
 * @param parent 父对象
 * @param title 标题文本
 * @param value 右侧文本
 * @param y 顶部坐标
 * @param width 页面宽度
 * @return 创建成功返回 true，否则返回 false
 */
bool CreateWifiInfoRow(lv_obj_t* parent, const char* title,
    const char* value, int y, int width) {
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

  lv_obj_t* title_label =
      CreateLabel(row, title, lv_color_hex(kPrimaryTextColor), Font28());
  if (title_label == nullptr) {
    return false;
  }
  lv_obj_align(title_label, LV_ALIGN_LEFT_MID, kWifiSidePadding, 0);

  lv_obj_t* value_label =
      CreateLabel(row, value, lv_color_hex(kSecondaryTextColor), Font24());
  if (value_label == nullptr) {
    return false;
  }
  lv_obj_set_width(value_label, width / 2);
  lv_obj_set_style_text_align(value_label, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);
  lv_obj_align(value_label, LV_ALIGN_RIGHT_MID, -kWifiSidePadding, 0);
  return true;
}

/**
 * @brief 创建 WLAN 自动连接开关行
 * @param parent 父对象
 * @param state 设置页状态
 * @param y 顶部坐标
 * @param width 页面宽度
 * @return 创建成功返回 true，否则返回 false
 */
bool CreateWifiAutoConnectRow(
    lv_obj_t* parent, SettingsViewState* state, int y, int width) {
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

  lv_obj_t* label =
      CreateLabel(row, "Auto connect", lv_color_hex(kPrimaryTextColor),
          Font28());
  if (label == nullptr) {
    return false;
  }
  lv_obj_align(label, LV_ALIGN_LEFT_MID, kWifiSidePadding, 0);

  lv_obj_t* switch_object = lv_switch_create(row);
  if (switch_object == nullptr) {
    return false;
  }
  lv_obj_set_size(switch_object, kWifiSwitchWidth, kWifiSwitchHeight);
  lv_obj_align(switch_object, LV_ALIGN_RIGHT_MID, -kWifiSidePadding, 0);
  lv_obj_set_style_bg_color(switch_object, lv_color_hex(kWifiBlueColor),
      kWifiSwitchCheckedIndicatorSelector);
  lv_obj_set_style_bg_opa(switch_object, LV_OPA_COVER,
      kWifiSwitchCheckedIndicatorSelector);
  const app::WifiSavedNetwork* saved =
      FindSavedWifiNetworkConst(state->wifi_pending_action.ssid);
  if (saved != nullptr && saved->auto_connect) {
    lv_obj_add_state(switch_object, LV_STATE_CHECKED);
  }
  lv_obj_add_event_cb(switch_object, WifiAutoConnectChangedEventCallback,
      LV_EVENT_VALUE_CHANGED, state);
  return true;
}

/**
 * @brief 构建单个 WLAN 网络详情内容
 * @param parent 内容容器
 * @param state 设置页状态
 * @return 创建成功返回 true，否则返回 false
 */
bool BuildWifiNetworkDetailContent(
    lv_obj_t* parent, SettingsViewState* state) {
  if (state == nullptr) {
    return false;
  }

  const bool saved_network =
      FindSavedWifiNetworkConst(state->wifi_pending_action.ssid) != nullptr;
  int y = 0;
  if (!CreateWifiSectionLabel(parent, "Network details", y,
          state->config.width)) {
    return false;
  }
  y += kWifiSectionHeight;
  if (saved_network) {
    if (!CreateWifiAutoConnectRow(parent, state, y, state->config.width)) {
      return false;
    }
    y += kWifiRowHeight + 8;
    if (!CreateWifiDividerAt(parent, y, state->config.width)) {
      return false;
    }
    y += 18;
  }
  if (!CreateWifiInfoRow(parent, "Signal strength",
          WifiSignalText(state->wifi_pending_action.rssi), y,
          state->config.width)) {
    return false;
  }
  y += kWifiRowHeight;
  if (!CreateWifiInfoRow(parent, "Security",
          WifiSecurityText(state->wifi_pending_action.secure), y,
          state->config.width)) {
    return false;
  }
  if (!saved_network) {
    return true;
  }
  y += kWifiRowHeight + 8;
  if (!CreateWifiDividerAt(parent, y, state->config.width)) {
    return false;
  }
  y += 18;
  if (state->wifi_pending_action.secure) {
    if (!CreateWifiOptionRow(parent, state, "Modify network", y,
            state->config.width)) {
      return false;
    }
    y += kWifiRowHeight;
  }
  return CreateWifiOptionRow(parent, state, "Delete network", y,
      state->config.width);
}

/**
 * @brief 构建 WLAN 高级设置内容
 * @param parent 内容容器
 * @param state 设置页状态
 * @return 创建成功返回 true，否则返回 false
 */
bool BuildWifiAdvancedContent(lv_obj_t* parent, SettingsViewState* state) {
  int y = 0;
  if (!CreateWifiSectionLabel(parent, "WLAN connection management", y,
          state->config.width)) {
    return false;
  }
  y += kWifiSectionHeight;
  return CreateWifiOptionRow(parent, state, "Manage saved networks", y,
      state->config.width);
}

/**
 * @brief 构建管理已保存 WLAN 内容
 * @param parent 内容容器
 * @param state 设置页状态
 * @return 创建成功返回 true，否则返回 false
 */
bool BuildWifiSavedNetworksContent(
    lv_obj_t* parent, SettingsViewState* state) {
  hal::WifiScanStatus scan_status;
  ReadWifiSnapshots(state->config, nullptr, &scan_status);
  SyncSavedWifiNetworksWithScan(scan_status);
  state->wifi_saved_delete_action_count = 0;

  if (g_wifi_saved_network_count == 0) {
    return CreateWifiSavedEmptyText(parent, state->config.width);
  }

  lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(parent, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START,
      LV_FLEX_ALIGN_START);
  lv_obj_set_style_pad_top(parent, 10, LV_PART_MAIN);
  lv_obj_set_style_pad_bottom(parent, 24, LV_PART_MAIN);
  lv_obj_set_style_pad_row(parent, 10, LV_PART_MAIN);

  for (size_t i = 0; i < g_wifi_saved_network_count; ++i) {
    const app::WifiSavedNetwork& saved = g_wifi_saved_networks[i];
    if (!CreateWifiSavedManageRow(
            parent, state, saved.ssid, state->config.width)) {
      return false;
    }
  }

  return true;
}

bool ShowWifiNetworkDetailPage(
    SettingsViewState* state, const WifiNetworkAction& action) {
  if (state == nullptr || action.ssid[0] == '\0') {
    return false;
  }
  state->wifi_pending_action = action;
  return ShowWifiSubPage(state, action.ssid, BuildWifiNetworkDetailContent);
}

bool ShowWifiAdvancedPage(SettingsViewState* state) {
  return ShowWifiSubPage(state, "Advanced settings",
      BuildWifiAdvancedContent);
}

bool ShowWifiSavedNetworksPage(SettingsViewState* state) {
  return ShowWifiSubPage(state, "Manage saved networks",
      BuildWifiSavedNetworksContent);
}

/**
 * @brief 创建底部弹窗按钮
 * @param parent 父对象
 * @param text 按钮文本
 * @param x 左侧坐标
 * @param y 顶部坐标
 * @param width 按钮宽度
 * @param callback 点击回调
 * @param state 设置页状态
 * @return 创建成功返回 true，否则返回 false
 */
bool CreateWifiSheetButton(lv_obj_t* parent, const char* text, int x, int y,
    int width, lv_event_cb_t callback, SettingsViewState* state,
    bool primary, bool enabled) {
  const uint32_t background_color =
      primary ? kWifiBlueColor : kWifiConnectSecondaryColor;
  PromptSheetButtonConfig button_config;
  button_config.text = text;
  button_config.x = x;
  button_config.y = y;
  button_config.width = width;
  button_config.height = kWifiConnectButtonHeight;
  button_config.radius = 24;
  button_config.background_color = background_color;
  button_config.disabled_background_color = kWifiConnectDisabledColor;
  button_config.pressed_background_color =
      primary ? kWifiActionPressedColor : kWifiConnectSecondaryPressedColor;
  button_config.pressed_opacity = LV_OPA_COVER;
  button_config.text_color = primary ? 0xFFFFFF : kPrimaryTextColor;
  button_config.font = Font28();
  button_config.callback = callback;
  button_config.user_data = state;
  button_config.enabled = enabled;
  lv_obj_t* button = CreatePromptSheetButton(parent, button_config);
  if (button == nullptr) {
    return false;
  }
  if (primary && state != nullptr) {
    state->wifi_connect_button = button;
    state->wifi_connect_button_label = lv_obj_get_child(button, 0);
  }
  return true;
}

/**
 * @brief 设置 WLAN 连接按钮是否可点击
 * @param state 设置页状态
 * @param enabled 是否启用
 */
void SetWifiConnectButtonEnabled(SettingsViewState* state, bool enabled) {
  if (state == nullptr || state->wifi_connect_button == nullptr) {
    return;
  }
  if (enabled) {
    lv_obj_remove_state(state->wifi_connect_button, LV_STATE_DISABLED);
    lv_obj_set_style_bg_color(state->wifi_connect_button,
        lv_color_hex(kWifiBlueColor), LV_PART_MAIN);
  } else {
    lv_obj_add_state(state->wifi_connect_button, LV_STATE_DISABLED);
    lv_obj_set_style_bg_color(state->wifi_connect_button,
        lv_color_hex(kWifiConnectDisabledColor), LV_PART_MAIN);
  }
  if (state->wifi_connect_button_label != nullptr) {
    lv_obj_set_style_text_color(state->wifi_connect_button_label,
        lv_color_hex(0xFFFFFF), LV_PART_MAIN);
  }
}

void UpdateWifiConnectButtonState(SettingsViewState* state) {
  if (state == nullptr) {
    return;
  }
  if (state->wifi_password_text_area == nullptr) {
    SetWifiConnectButtonEnabled(state, true);
    return;
  }
  const char* password = lv_textarea_get_text(state->wifi_password_text_area);
  const bool enabled =
      password != nullptr && std::strlen(password) >= kWifiPasswordMinLength;
  SetWifiConnectButtonEnabled(state, enabled);
}

void MoveWifiConnectSheetForKeyboard(
    SettingsViewState* state, bool keyboard_visible) {
  if (state == nullptr || state->wifi_modal_sheet == nullptr) {
    return;
  }
  keyboard_visible = keyboard_visible && ShouldShowSharedKeyboard();

  const int sheet_height = lv_obj_get_height(state->wifi_modal_sheet);
  int target_y =
      state->config.height - sheet_height - kWifiConnectSheetBottomMargin;
  if (keyboard_visible) {
    const int keyboard_height =
        state->config.height * kWifiPasswordKeyboardHeightPercent / 100;
    target_y = state->config.height - keyboard_height - sheet_height - 14;
    if (target_y < 16) {
      target_y = 16;
    }
  }

  lv_anim_delete(state->wifi_modal_sheet, SetObjectY);
  lv_anim_t animation;
  lv_anim_init(&animation);
  lv_anim_set_var(&animation, state->wifi_modal_sheet);
  lv_anim_set_values(&animation, lv_obj_get_y(state->wifi_modal_sheet),
      target_y);
  lv_anim_set_duration(&animation, kDetailSlideAnimationMs);
  lv_anim_set_path_cb(&animation, lv_anim_path_ease_out);
  lv_anim_set_exec_cb(&animation, SetObjectY);
  lv_anim_start(&animation);
}

/**
 * @brief 处理 WLAN 密码输入框焦点变化并同步连接按钮状态
 * @param event LVGL 事件对象
 */
void WifiPasswordTextAreaEventCallback(lv_event_t* event) {
  const lv_event_code_t code = lv_event_get_code(event);
  // 按钮按下时密码框会先失焦。此时保持弹窗位置不变，避免按钮在释放前
  // 发生位移而丢失本次点击。
  if (code != LV_EVENT_VALUE_CHANGED && code != LV_EVENT_FOCUSED &&
      code != LV_EVENT_CLICKED && code != LV_EVENT_READY &&
      code != LV_EVENT_CANCEL) {
    return;
  }

  auto* state = static_cast<SettingsViewState*>(lv_event_get_user_data(event));
  if (code == LV_EVENT_VALUE_CHANGED) {
    if (state != nullptr && state->wifi_password_error_label != nullptr) {
      lv_obj_add_flag(
          state->wifi_password_error_label, LV_OBJ_FLAG_HIDDEN);
    }
    UpdateWifiConnectButtonState(state);
    return;
  }
  if (code == LV_EVENT_FOCUSED || code == LV_EVENT_CLICKED) {
    MoveWifiConnectSheetForKeyboard(state, true);
  } else if (code == LV_EVENT_READY || code == LV_EVENT_CANCEL) {
    if (state != nullptr && state->wifi_password_keyboard != nullptr) {
      HideSharedKeyboard(state->wifi_password_keyboard);
    }
    MoveWifiConnectSheetForKeyboard(state, false);
  }
}

bool ShowWifiConnectSheet(SettingsViewState* state,
    const WifiNetworkAction& action, const char* error_text,
    bool edit_mode) {
  if (state == nullptr || state->root == nullptr ||
      state->wifi_page == nullptr || state->wifi_closing ||
      action.ssid[0] == '\0') {
    return false;
  }
  CloseWifiModalImmediately(state);
  app::SetWifiAutoConnectPaused(true);
  state->wifi_pending_action = action;
  state->wifi_connection_retry_ready = false;

  const bool has_error = error_text != nullptr && error_text[0] != '\0';
  const int sheet_height = action.secure ? 350 : 292;
  const int sheet_width =
      state->config.width - 2 * kWifiConnectSheetSideMargin;
  PromptSheetConfig sheet_config;
  sheet_config.screen_width = state->config.width;
  sheet_config.screen_height = state->config.height;
  sheet_config.sheet_width = sheet_width;
  sheet_config.sheet_height = sheet_height;
  sheet_config.side_margin = kWifiConnectSheetSideMargin;
  sheet_config.bottom_margin = kWifiConnectSheetBottomMargin;
  sheet_config.sheet_radius = kWifiConnectSheetRadius;

  lv_obj_t* overlay = CreatePromptSheetOverlay(state->root, sheet_config);
  if (overlay == nullptr) {
    app::SetWifiAutoConnectPaused(false);
    return false;
  }
  state->wifi_modal_overlay = overlay;
  lv_obj_add_event_cb(overlay, WifiModalCancelClickedEventCallback,
      LV_EVENT_CLICKED, state);
  if (!RegisterBackNavigationHandler(overlay, [state]() {
        if (state != nullptr) {
          state->wifi_connection_retry_ready = false;
        }
        CloseWifiModal(state);
      })) {
    CloseWifiModalImmediately(state);
    return false;
  }

  lv_obj_t* sheet = CreatePromptSheet(overlay, sheet_config);
  if (sheet == nullptr) {
    CloseWifiModalImmediately(state);
    return false;
  }
  state->wifi_modal_sheet = sheet;
  lv_obj_add_event_cb(sheet, WifiModalContentClickedEventCallback,
      LV_EVENT_CLICKED, state);

  lv_obj_t* title = CreateLabel(sheet, action.ssid,
      lv_color_hex(kPrimaryTextColor), Font32());
  if (title == nullptr) {
    CloseWifiModalImmediately(state);
    return false;
  }
  lv_obj_set_size(title,
      sheet_width - 2 * kWifiConnectSheetInnerPadding,
      static_cast<int>(lv_font_get_line_height(Font32())));
  lv_label_set_long_mode(title, LV_LABEL_LONG_SCROLL_CIRCULAR);
  lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
  lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 34);

  const char* subtitle = action.secure
      ? (has_error ? error_text : (edit_mode ? "" : "Password required"))
      : "Connect to this open network?";
  if (subtitle[0] != '\0') {
    lv_obj_t* subtitle_label = CreateLabel(sheet, subtitle,
        lv_color_hex(has_error ? 0xE53935 : kSecondaryTextColor), Font24());
    if (subtitle_label == nullptr) {
      CloseWifiModalImmediately(state);
      return false;
    }
    state->wifi_password_error_label =
        has_error ? subtitle_label : nullptr;
    AlignPromptSheetSubtitle(subtitle_label, title, 8);
  }

  const int button_y =
      sheet_height - kWifiConnectSheetInnerPadding - kWifiConnectButtonHeight;
  if (action.secure) {
    lv_obj_t* text_area = lv_textarea_create(sheet);
    if (text_area == nullptr) {
      CloseWifiModalImmediately(state);
      return false;
    }
    state->wifi_password_text_area = text_area;
    lv_obj_add_flag(text_area, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_set_size(text_area,
        sheet_width - 2 * kWifiConnectSheetInnerPadding,
        kWifiPasswordInputHeight);
    lv_obj_align(
        text_area, LV_ALIGN_TOP_MID, 0, kWifiPasswordInputTop);
    lv_textarea_set_one_line(text_area, true);
    lv_textarea_set_password_mode(text_area, true);
    lv_textarea_set_password_bullet(text_area, "*");
    lv_textarea_set_max_length(text_area, hal::kWifiPasswordMaxLength);
    lv_textarea_set_placeholder_text(text_area, "");
    lv_textarea_set_text(text_area, "");
    ApplySettingsTextAreaStyle(
        text_area, Font28(), kWifiPasswordInputHeight);

    SharedKeyboardConfig keyboard_config;
    keyboard_config.width = state->config.width;
    keyboard_config.height =
        state->config.height * kWifiPasswordKeyboardHeightPercent / 100;
    lv_obj_t* keyboard = CreateSharedKeyboard(overlay, keyboard_config);
    if (keyboard == nullptr) {
      CloseWifiModalImmediately(state);
      return false;
    }
    state->wifi_password_keyboard = keyboard;
    lv_obj_add_flag(keyboard, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_event_cb(keyboard, WifiModalContentClickedEventCallback,
        LV_EVENT_CLICKED, state);
    lv_obj_add_event_cb(text_area, WifiPasswordTextAreaEventCallback,
        LV_EVENT_ALL, state);
    if (!AttachSharedKeyboardToTextArea(
            keyboard, text_area, kWifiPasswordAcceptedChars)) {
      CloseWifiModalImmediately(state);
      return false;
    }
  } else {
    lv_obj_t* hint = CreateLabel(sheet, "No password required.",
        lv_color_hex(kSecondaryTextColor), Font24());
    if (hint == nullptr) {
      CloseWifiModalImmediately(state);
      return false;
    }
    lv_obj_align(hint, LV_ALIGN_TOP_MID, 0, 136);
  }

  const int button_width =
      (sheet_width - 2 * kWifiConnectSheetInnerPadding -
          kWifiConnectButtonGap) /
      2;
  const int left_button_x = kWifiConnectSheetInnerPadding;
  const int right_button_x =
      left_button_x + button_width + kWifiConnectButtonGap;
  const bool connect_enabled = !action.secure;
  if (!CreateWifiSheetButton(sheet, "Cancel", left_button_x, button_y,
          button_width, WifiModalCancelClickedEventCallback, state, false,
          true) ||
      !CreateWifiSheetButton(sheet, edit_mode ? "Save" : "Connect",
          right_button_x, button_y,
          button_width, WifiModalConnectClickedEventCallback, state, true,
          connect_enabled)) {
    CloseWifiModalImmediately(state);
    return false;
  }
  UpdateWifiConnectButtonState(state);

  AnimatePromptSheetIn(sheet, sheet_config, kDetailSlideAnimationMs);
  return true;
}

bool ShowWifiDeleteNetworkSheet(SettingsViewState* state, const char* ssid,
    bool close_sub_page, lv_obj_t* saved_delete_row) {
  if (state == nullptr || state->root == nullptr ||
      ssid == nullptr || ssid[0] == '\0' || !IsSavedWifiSsid(ssid)) {
    return false;
  }

  CloseWifiModalImmediately(state);
  std::strncpy(state->wifi_pending_action.ssid, ssid,
      sizeof(state->wifi_pending_action.ssid) - 1);
  state->wifi_pending_action.ssid[
      sizeof(state->wifi_pending_action.ssid) - 1] = '\0';
  state->wifi_delete_close_sub_page = close_sub_page;
  state->wifi_saved_delete_row = saved_delete_row;

  const int sheet_height = 332;
  const int sheet_width =
      state->config.width - 2 * kWifiConnectSheetSideMargin;
  PromptSheetConfig sheet_config;
  sheet_config.screen_width = state->config.width;
  sheet_config.screen_height = state->config.height;
  sheet_config.sheet_width = sheet_width;
  sheet_config.sheet_height = sheet_height;
  sheet_config.side_margin = kWifiConnectSheetSideMargin;
  sheet_config.bottom_margin = kWifiConnectSheetBottomMargin;
  sheet_config.sheet_radius = kWifiConnectSheetRadius;

  lv_obj_t* overlay = CreatePromptSheetOverlay(state->root, sheet_config);
  if (overlay == nullptr) {
    return false;
  }
  state->wifi_modal_overlay = overlay;
  lv_obj_add_event_cb(overlay, WifiModalCancelClickedEventCallback,
      LV_EVENT_CLICKED, state);
  if (!RegisterBackNavigationHandler(overlay, [state]() {
        CloseWifiModal(state);
      })) {
    CloseWifiModalImmediately(state);
    return false;
  }

  lv_obj_t* sheet = CreatePromptSheet(overlay, sheet_config);
  if (sheet == nullptr) {
    CloseWifiModalImmediately(state);
    return false;
  }
  state->wifi_modal_sheet = sheet;
  lv_obj_add_event_cb(sheet, WifiModalContentClickedEventCallback,
      LV_EVENT_CLICKED, state);

  lv_obj_t* title = CreatePromptSheetLabel(sheet, "Delete network",
      kPrimaryTextColor, Font32());
  if (title == nullptr) {
    CloseWifiModalImmediately(state);
    return false;
  }
  lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 34);

  lv_obj_t* message = CreatePromptSheetLabel(sheet,
      "Stop automatically connecting to this network. You may need to enter "
      "the password again.",
      kSecondaryTextColor, Font24());
  if (message == nullptr) {
    CloseWifiModalImmediately(state);
    return false;
  }
  lv_obj_set_width(message, sheet_width - 2 * kWifiConnectSheetInnerPadding);
  lv_label_set_long_mode(message, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_text_align(message, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
  AlignPromptSheetSubtitle(message, title, 8);

  const int button_width =
      (sheet_width - 2 * kWifiConnectSheetInnerPadding -
          kWifiConnectButtonGap) /
      2;
  const int button_y =
      sheet_height - kWifiConnectSheetInnerPadding - kWifiConnectButtonHeight;
  const int left_button_x = kWifiConnectSheetInnerPadding;
  const int right_button_x =
      left_button_x + button_width + kWifiConnectButtonGap;
  if (!CreateWifiSheetButton(sheet, "Cancel", left_button_x, button_y,
          button_width, WifiModalCancelClickedEventCallback, state, false,
          true) ||
      !CreateWifiSheetButton(sheet, "OK", right_button_x, button_y,
          button_width, WifiDeleteConfirmClickedEventCallback, state, true,
          true)) {
    CloseWifiModalImmediately(state);
    return false;
  }

  AnimatePromptSheetIn(sheet, sheet_config, kDetailSlideAnimationMs);
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
  SyncSavedWifiNetworksWithScan(scan_status);

  const bool wifi_enabled = state != nullptr ? state->wifi_enabled_requested
                                             : IsWifiPageEnabled(status,
                                                   scan_status);
  const bool scan_pending = state != nullptr && state->wifi_scan_on_ready;
  const bool connection_waiting =
      state != nullptr && state->wifi_connect_waiting;
  const bool connected = status.connected || status.got_ip;
  const app::InternetAccessState internet_state =
      app::NetworkMonitor::Instance().GetStatus().internet_state;
  const bool refreshing = IsWifiRefreshActive(state, status, scan_status);
  const bool show_scan_results =
      scan_status.network_count > 0 && (!scan_status.scan_failed ||
          refreshing || status.start_failed || status.disconnect_reason != 0);
  // 当前页面已展示过的 SSID，避免当前、已保存、附近列表重复。
  char shown_ssids[kWifiActionCapacity][hal::kWifiSsidMaxLength + 1] = {};
  size_t shown_ssid_count = 0;
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
  if (!connected && state != nullptr &&
      state->wifi_pending_action.ssid[0] != '\0' &&
      (connection_waiting || status.start_failed ||
          status.disconnect_reason != 0)) {
    std::snprintf(ssid, sizeof(ssid), "%s",
        state->wifi_pending_action.ssid);
  }

  const bool failed_saved_network =
      ssid[0] != '\0' && !connection_waiting &&
      (status.start_failed || status.disconnect_reason != 0) &&
      IsSavedWifiSsid(ssid);
  hal::WifiNetworkInfo card_network = {};
  const bool card_scan_found =
      FindScannedWifiNetwork(scan_status, ssid, &card_network);
  int card_rssi = status.rssi;
  bool card_is_5g = status.channel > 14;
  bool card_secure = true;
  const char* card_password = "";
  const app::WifiSavedNetwork* card_saved = FindSavedWifiNetworkConst(ssid);
  if (card_saved != nullptr) {
    card_rssi = card_saved->rssi;
    card_is_5g = card_saved->is_5g;
    card_secure = card_saved->secure;
    card_password = card_saved->password;
  }
  if (state != nullptr &&
      std::strcmp(ssid, state->wifi_pending_action.ssid) == 0) {
    card_rssi = state->wifi_pending_action.rssi;
    card_is_5g = state->wifi_pending_action.is_5g;
    card_secure = state->wifi_pending_action.secure;
    if (state->wifi_pending_action.password[0] != '\0' ||
        card_saved == nullptr) {
      card_password = state->wifi_pending_action.password;
    }
  }
  if (card_scan_found) {
    card_rssi = card_network.rssi;
    card_is_5g = card_network.is_5g;
    card_secure = card_network.secure;
  }
  if (connected) {
    card_rssi = status.rssi;
    card_is_5g = status.channel > 14;
  }

  if (ssid[0] != '\0' && connected) {
    y += 10;
    const char* connection_text = "Connected";
    if (status.got_ip &&
        internet_state != app::InternetAccessState::kAvailable &&
        internet_state != app::InternetAccessState::kLocalOnly) {
      connection_text = "Checking internet access...";
    } else if (status.got_ip &&
               internet_state == app::InternetAccessState::kLocalOnly) {
      connection_text = "Connected, no internet";
    }
    if (!CreateWifiConnectedCard(parent, state, ssid, connection_text,
            kWifiBlueColor, card_rssi, card_is_5g, card_secure,
            card_password, y, config.width, false)) {
      return false;
    }
    MarkShownWifiSsid(shown_ssids, &shown_ssid_count, ssid);
    y += kWifiConnectedCardHeight + 18;
  } else if (ssid[0] != '\0' &&
             (connection_waiting || status.init_task_running ||
                 (state != nullptr && state->wifi_scan_on_ready))) {
    y += 10;
    if (!CreateWifiConnectedCard(parent, state, ssid, "Connecting...",
            kWifiConnectingColor, card_rssi, card_is_5g, card_secure,
            card_password, y, config.width, false)) {
      return false;
    }
    MarkShownWifiSsid(shown_ssids, &shown_ssid_count, ssid);
    y += kWifiConnectedCardHeight + 18;
  } else if (ssid[0] != '\0' && !failed_saved_network &&
             (status.start_failed || status.disconnect_reason != 0)) {
    y += 10;
    if (!CreateWifiConnectedCard(parent, state, ssid, "Connection failed",
            0xFFECEC, card_rssi, card_is_5g, card_secure, card_password, y,
            config.width, true)) {
      return false;
    }
    MarkShownWifiSsid(shown_ssids, &shown_ssid_count, ssid);
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
    for (size_t i = 0; i < g_wifi_saved_network_count; ++i) {
      const app::WifiSavedNetwork& saved = g_wifi_saved_networks[i];
      hal::WifiNetworkInfo network = {};
      if (IsShownWifiSsid(
              shown_ssids, shown_ssid_count, saved.ssid) ||
          !FindScannedWifiNetwork(scan_status, saved.ssid, &network)) {
        continue;
      }
      if (!CreateWifiNetworkRow(parent, state, saved.ssid, network.is_5g,
              network.secure, network.rssi, saved.password, true, y,
              config.width)) {
        return false;
      }
      MarkShownWifiSsid(shown_ssids, &shown_ssid_count, saved.ssid);
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
      if (IsSavedWifiSsid(network.ssid) ||
          IsShownWifiSsid(shown_ssids, shown_ssid_count, network.ssid)) {
        continue;
      }
      if (!CreateWifiNetworkRow(parent, state, network.ssid, network.is_5g,
              network.secure, network.rssi, "", false, y, config.width)) {
        return false;
      }
      MarkShownWifiSsid(shown_ssids, &shown_ssid_count, network.ssid);
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

void RefreshWifiPage(SettingsViewState* state, bool force) {
  if (state == nullptr || state->wifi_body == nullptr) {
    return;
  }

  hal::WifiStatus status;
  hal::WifiScanStatus scan_status;
  ReadWifiSnapshots(state->config, &status, &scan_status);
  const app::InternetAccessState internet_state =
      app::NetworkMonitor::Instance().GetStatus().internet_state;
  const uint32_t refresh_key =
      MakeWifiRefreshKey(status, scan_status, internet_state);
  if (!force && !state->wifi_refresh_force &&
      state->wifi_refresh_key == refresh_key) {
    return;
  }

  state->wifi_refresh_key = refresh_key;
  state->wifi_refresh_force = false;
  state->wifi_action_count = 0;
  state->wifi_saved_delete_action_count = 0;
  state->wifi_connected_signal_icon = nullptr;
  const int scroll_y = lv_obj_get_scroll_y(state->wifi_body);
  StopWifiRefreshIconSpin(state);
  state->wifi_refresh_icon = nullptr;
  lv_obj_clean(state->wifi_body);
  if (!CreateWifiPageContent(state->wifi_body, state, state->config)) {
    state->wifi_action_count = 0;
    state->wifi_saved_delete_action_count = 0;
    state->wifi_connected_signal_icon = nullptr;
    state->wifi_refresh_icon = nullptr;
    state->wifi_refresh_force = true;
    lv_obj_clean(state->wifi_body);
    lv_obj_t* error_label = CreateLabel(state->wifi_body,
        "Unable to load WLAN settings", lv_color_hex(kSecondaryTextColor),
        Font24());
    if (error_label != nullptr) {
      lv_obj_center(error_label);
    }
    return;
  }
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
  LoadSavedWifiNetworksFromCache();
  if (state->wifi_closing) {
    return true;
  }
  if (state->wifi_page != nullptr) {
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
  state->wifi_sub_page = nullptr;
  state->wifi_sub_page_count = 0;
  for (lv_obj_t*& sub_page : state->wifi_sub_pages) {
    sub_page = nullptr;
  }
  state->wifi_connected_signal_icon = nullptr;
  state->wifi_refresh_icon = nullptr;
  state->wifi_closing = false;
  state->wifi_action_count = 0;
  state->wifi_saved_delete_action_count = 0;
  state->wifi_refresh_key = 0;
  state->wifi_refresh_force = true;
  state->wifi_scan_on_ready = false;
  state->wifi_scan_request_generation = 0;
  UpdateSettingsWifiValue(state);
  if (state->wifi_enabled_requested) {
    RequestWifiScan(state, true);
  }

  lv_obj_remove_flag(page, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(page, LV_OBJ_FLAG_GESTURE_BUBBLE);
  lv_obj_set_size(page, config.width, config.height);
  lv_obj_set_pos(page, 0, 0);
  lv_obj_set_style_bg_color(page, lv_color_hex(kBackgroundColor),
      LV_PART_MAIN);
  lv_obj_set_style_bg_opa(page, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_border_width(page, 0, LV_PART_MAIN);
  lv_obj_set_style_radius(page, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(page, 0, LV_PART_MAIN);

  if (!CreateWifiHeader(page, state)) {
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

  if (!StartSlideLeftWindowTransition(
          page, config.width, kDetailSlideAnimationMs, state, nullptr)) {
    CloseWifiPage(state, false);
    return false;
  }
  if (!RegisterBackNavigationHandler(page, [state]() {
        CloseWifiPage(state, true);
      })) {
    CloseWifiPage(state, false);
    return false;
  }
  return true;
}

}  // namespace

bool ShowWifiPage(SettingsViewState* state) {
  return ShowWifiPageInternal(state);
}

void LoadWifiSettingsFromCache(
    SettingsViewState* state, bool fallback_enabled) {
  LoadSavedWifiNetworksFromCache();
  LoadWifiPreferencesFromCache(state, fallback_enabled);
}

}  // namespace lilygo_box::ui
