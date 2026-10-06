/*
 * @Description: 展示应用连接状态、自动连接、授权管理和拖动优先级排序。
 * @Author: LILYGO_L
 * @Date: 2026-10-04 18:37:01
 * @LastEditTime: 2026-10-06 13:51:00
 * @License: GPL 3.0
 */
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <new>

#include "app/app_connection.h"
#include "hal/providers/wifi_provider.h"
#include "ui/input/press_cancel.h"
#include "ui/resources/fonts/icon_assets.h"
#include "ui/views/settings/settings_basic_view_common.h"
#include "ui/widgets/prompt/prompt_dialog.h"

namespace lilygo_box::ui {
namespace {

constexpr char kAppConnectionTitle[] = "LilygoBox App";
constexpr char kAppConnectionSubtitle[] =
    "Connect to the LilygoBox app on your computer or phone.";
constexpr char kAutoConnectSubtitle[] =
    "Reconnect to authorized apps in priority order.";

struct ConnectionView {
  SettingsViewState* settings = nullptr;
  lv_obj_t* toggle = nullptr;
  lv_obj_t* connection_name = nullptr;
  lv_obj_t* disconnect_button = nullptr;
  char connected_id[65] = {};
  char disconnect_id[65] = {};
  lv_timer_t* timer = nullptr;
  PromptDialogState prompt;
};

/**
 * @brief 同步自动连接、当前应用名称和断开入口，不展示诊断信息
 * @param view 连接设置页状态
 */
void Refresh(ConnectionView* view) {
  const auto status = app::ReadAppConnectionStatus();
  if (status.auto_connect)
    lv_obj_add_state(view->toggle, LV_STATE_CHECKED);
  else
    lv_obj_remove_state(view->toggle, LV_STATE_CHECKED);
  const auto apps = app::ReadAuthorizedApps();
  const app::AuthorizedApp* connected = nullptr;
  for (size_t i = 0; i < apps.count; ++i) {
    if (apps.clients[i].connected) {
      connected = &apps.clients[i];
      break;
    }
  }
  std::snprintf(view->connected_id, sizeof(view->connected_id), "%s",
      connected == nullptr ? "" : connected->id);
  const char* name = connected == nullptr ? "Not connected" : connected->name;
  if (std::strcmp(lv_label_get_text(view->connection_name), name) != 0)
    lv_label_set_text(view->connection_name, name);
  if (connected == nullptr)
    lv_obj_add_flag(view->disconnect_button, LV_OBJ_FLAG_HIDDEN);
  else
    lv_obj_remove_flag(view->disconnect_button, LV_OBJ_FLAG_HIDDEN);
  lv_obj_update_layout(view->disconnect_button);
  const int trailing =
      connected == nullptr ? 0 : lv_obj_get_width(view->disconnect_button) + 28;
  lv_obj_set_width(view->connection_name,
      view->settings->config.width - 2 * kBasicSidePadding - trailing);
}

/**
 * @brief 在应用连接主页确认断开指定应用，保留授权与排序
 * @param event 断开按钮点击事件
 */
void DisconnectClicked(lv_event_t* event) {
  auto* view = static_cast<ConnectionView*>(lv_event_get_user_data(event));
  Refresh(view);
  if (view->connected_id[0] == '\0') return;
  std::snprintf(view->disconnect_id, sizeof(view->disconnect_id), "%s",
      view->connected_id);
  auto config = MakeSettingsTextPromptConfig(view->settings);
  config.title = "Disconnect app?";
  config.subtitle =
      "Authorization will be kept. Reconnect from the app when needed.";
  config.confirm_text = "Disconnect";
  config.callback_context = view;
  config.confirm_callback = [](void* context) {
    auto* current = static_cast<ConnectionView*>(context);
    // 确认期间连接可能变化，只断开点击时选中的应用。
    app::DisconnectApp(current->disconnect_id);
  };
  ShowPromptDialog(view->settings->settings_nested_page, &view->prompt, config);
}

/**
 * @brief 按设置页双行布局创建当前连接信息和蓝色断开按钮
 * @param body 设置页内容容器
 * @param view 连接页状态
 * @param y 行顶部位置
 * @return 创建成功返回 true
 */
bool CreateConnectionStatusRow(lv_obj_t* body, ConnectionView* view, int y) {
  const int width = view->settings->config.width;
  auto* row = lv_obj_create(body);
  if (row == nullptr) return false;
  MakeTransparent(row);
  lv_obj_set_style_radius(row, 0, LV_PART_MAIN);
  lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(row, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_flag(row, LV_OBJ_FLAG_GESTURE_BUBBLE);
  lv_obj_set_size(row, width, kBasicSwitchRowWithSubtitleHeight);
  lv_obj_set_pos(row, 0, y);
  auto* title = CreateLabel(row, "Current connection",
      lv_color_hex(SettingsThemeColors().on_surface), Font28());
  view->connection_name = CreateLabel(row, "Not connected",
      lv_color_hex(SettingsThemeColors().on_surface_variant), Font22());
  view->disconnect_button = CreateCredentialActionButton(row, true);
  if (title == nullptr || view->connection_name == nullptr ||
      view->disconnect_button == nullptr)
    return false;
  lv_obj_update_layout(view->disconnect_button);
  const int text_width = width - 2 * kBasicSidePadding -
                         lv_obj_get_width(view->disconnect_button) - 28;
  lv_obj_set_size(title, text_width, lv_font_get_line_height(Font28()));
  lv_label_set_long_mode(title, LV_LABEL_LONG_SCROLL_CIRCULAR);
  lv_obj_set_pos(title, kBasicSidePadding, 12);
  lv_obj_set_size(
      view->connection_name, text_width, lv_font_get_line_height(Font22()));
  lv_label_set_long_mode(view->connection_name, LV_LABEL_LONG_SCROLL_CIRCULAR);
  lv_obj_set_pos(view->connection_name, kBasicSidePadding,
      12 + lv_font_get_line_height(Font28()) + 8);
  lv_obj_add_event_cb(
      view->disconnect_button, DisconnectClicked, LV_EVENT_CLICKED, view);
  return true;
}

struct AppsView;
struct AppRow {
  AppsView* view = nullptr;
  app::AuthorizedApp app;
  lv_obj_t* object = nullptr;
  lv_obj_t* button = nullptr;
  lv_obj_t* name = nullptr;
  size_t position = 0;
};
struct AppsView {
  SettingsViewState* settings = nullptr;
  lv_obj_t* body = nullptr;
  lv_obj_t* rows = nullptr;
  lv_timer_t* timer = nullptr;
  PromptDialogState prompt;
  app::AuthorizedApps snapshot;
  AppRow items[app::kAppCredentialCapacity];
  AppRow* dragging = nullptr;
  int drag_offset = 0;
  char action_id[65] = {};
  bool save_failed = false;
};
constexpr int kAppRowHeight = kWifiNetworkRowHeight + 12;

/**
 * @brief 使用公共提示模板显示保存失败，保留当前授权与原顺序
 * @param view 授权管理页面
 */
void ShowAppSaveError(AppsView* view) {
  auto config = MakeSettingsTextPromptConfig(view->settings);
  config.title = "Unable to save changes";
  config.subtitle = "Please try again.";
  config.cancel_text = "OK";
  config.confirm_text = nullptr;
  ShowPromptDialog(lv_obj_get_parent(view->body), &view->prompt, config);
}

/**
 * @brief 确认删除指定授权，正在连接时一并断开
 * @param event 删除按钮点击事件
 */
void DeleteAppClicked(lv_event_t* event) {
  auto* row = static_cast<AppRow*>(lv_event_get_user_data(event));
  auto* view = row->view;
  std::snprintf(view->action_id, sizeof(view->action_id), "%s", row->app.id);
  auto config = MakeSettingsTextPromptConfig(view->settings);
  config.title = "Remove authorized app?";
  config.subtitle =
      "This will disconnect the app if connected. Approval will be required to "
      "connect again.";
  config.confirm_text = "Delete";
  config.callback_context = view;
  config.confirm_callback = [](void* context) {
    auto* current = static_cast<AppsView*>(context);
    current->save_failed = !app::RemoveAppCredential(current->action_id);
  };
  ShowPromptDialog(lv_obj_get_parent(view->body), &view->prompt, config);
}

/**
 * @brief 更新排序行位置，拖动中的行跟随触点
 * @param view 管理页面
 */
void LayoutAppRows(AppsView* view) {
  for (size_t i = 0; i < view->snapshot.count; ++i) {
    auto& row = view->items[i];
    if (&row != view->dragging)
      lv_obj_set_y(row.object, row.position * kAppRowHeight);
  }
}

/**
 * @brief 仅通过左侧图标手柄自由拖动排序；松手后一次持久保存
 * @param event 拖动手柄的触摸事件
 */
void DragAppEvent(lv_event_t* event) {
  const auto code = lv_event_get_code(event);
  if (code != LV_EVENT_PRESSED && code != LV_EVENT_PRESSING &&
      code != LV_EVENT_RELEASED && code != LV_EVENT_PRESS_LOST)
    return;
  auto* row = static_cast<AppRow*>(lv_event_get_user_data(event));
  auto* view = row->view;
  auto* input = lv_indev_active();
  if (code == LV_EVENT_PRESSED && input != nullptr) {
    lv_point_t point;
    lv_indev_get_point(input, &point);
    lv_area_t area;
    lv_obj_get_coords(row->object, &area);
    view->drag_offset = point.y - area.y1;
    view->dragging = row;
    lv_obj_stop_scroll_anim(view->body);
    lv_obj_move_to_index(row->object, -1);
    lv_obj_set_style_bg_color(row->object,
        lv_color_hex(SettingsThemeColors().surface_container_high),
        LV_PART_MAIN);
    lv_obj_set_style_bg_opa(row->object, LV_OPA_COVER, LV_PART_MAIN);
  } else if (code == LV_EVENT_PRESSING && view->dragging == row &&
             input != nullptr) {
    lv_point_t point;
    lv_indev_get_point(input, &point);
    lv_area_t body_area;
    lv_obj_get_coords(view->body, &body_area);
    // 仅在列表可滚动范围内移动，禁止将内容拉到顶部/底部之外。
    const int scroll_y = lv_obj_get_scroll_y(view->body);
    const int max_scroll = std::max(
        0, scroll_y + static_cast<int>(lv_obj_get_scroll_bottom(view->body)));
    int next_scroll = scroll_y;
    if (point.y < body_area.y1 + 40) next_scroll -= 12;
    if (point.y > body_area.y2 - 40) next_scroll += 12;
    lv_obj_scroll_to_y(
        view->body, std::clamp(next_scroll, 0, max_scroll), LV_ANIM_OFF);
    lv_obj_update_layout(view->body);
    lv_area_t list_area;
    lv_obj_get_coords(view->rows, &list_area);
    const int y =
        std::clamp(static_cast<int>(point.y - list_area.y1 - view->drag_offset),
            0, static_cast<int>((view->snapshot.count - 1) * kAppRowHeight));
    const size_t target = std::min(view->snapshot.count - 1,
        static_cast<size_t>((y + kAppRowHeight / 2) / kAppRowHeight));
    const size_t previous = row->position;
    for (size_t i = 0; i < view->snapshot.count; ++i) {
      auto& other = view->items[i];
      if (&other == row) continue;
      if (target < previous && other.position >= target &&
          other.position < previous)
        ++other.position;
      if (target > previous && other.position <= target &&
          other.position > previous)
        --other.position;
    }
    row->position = target;
    lv_obj_set_y(row->object, y);
    LayoutAppRows(view);
  } else if ((code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) &&
             view->dragging == row) {
    view->dragging = nullptr;
    lv_obj_set_style_bg_opa(row->object, LV_OPA_TRANSP, LV_PART_MAIN);
    if (code == LV_EVENT_RELEASED) {
      view->save_failed = !app::MoveAppCredential(row->app.id, row->position);
    }
    // 触摸丢失或写入失败才回退；成功时保持松手位置，下一次刷新读取新顺序。
    if (code == LV_EVENT_PRESS_LOST || view->save_failed)
      for (size_t i = 0; i < view->snapshot.count; ++i)
        view->items[i].position = i;
    LayoutAppRows(view);
    if (input != nullptr) lv_indev_wait_release(input);
  }
}

/**
 * @brief 创建左侧排序图标、循环滚动名称和公共凭证操作按钮
 * @param view 管理页面
 * @param index 授权优先级
 * @return 创建成功返回 true
 */
bool CreateAppManageRow(AppsView* view, size_t index) {
  auto& row = view->items[index];
  row = {};
  row.view = view;
  row.app = view->snapshot.clients[index];
  row.position = index;
  const int width = view->settings->config.width;
  row.object = lv_obj_create(view->rows);
  if (row.object == nullptr) return false;
  MakeTransparent(row.object);
  lv_obj_set_style_radius(row.object, 0, LV_PART_MAIN);
  lv_obj_remove_flag(row.object, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(row.object, width, kAppRowHeight);
  lv_obj_set_y(row.object, index * kAppRowHeight);
  row.name = CreateLabel(row.object, row.app.name,
      lv_color_hex(SettingsThemeColors().on_surface), Font32());
  if (row.name == nullptr) return false;
  lv_obj_set_height(row.name, lv_font_get_line_height(Font32()));
  lv_label_set_long_mode(row.name, LV_LABEL_LONG_SCROLL_CIRCULAR);
  lv_obj_align(row.name, LV_ALIGN_LEFT_MID, kBasicSidePadding + 56, 0);
  row.button = CreateCredentialActionButton(row.object, false);
  if (row.button == nullptr) return false;
  lv_obj_update_layout(row.button);
  lv_obj_set_width(row.name,
      width - 2 * kBasicSidePadding - 56 - lv_obj_get_width(row.button) - 28);
  lv_obj_add_event_cb(row.button, DeleteAppClicked, LV_EVENT_CLICKED, &row);
  auto* handle = lv_obj_create(row.object);
  if (handle == nullptr) return false;
  lv_obj_remove_style_all(handle);
  lv_obj_set_size(handle, 56, 72);
  lv_obj_align(handle, LV_ALIGN_LEFT_MID, kBasicSidePadding - 8, 0);
  lv_obj_remove_flag(handle, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(handle, LV_OBJ_FLAG_SCROLL_CHAIN);
  lv_obj_remove_flag(handle, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
  lv_obj_remove_flag(handle, LV_OBJ_FLAG_GESTURE_BUBBLE);
  lv_obj_add_flag(handle, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_flag(handle, LV_OBJ_FLAG_PRESS_LOCK);
  lv_obj_add_event_cb(handle, DragAppEvent, LV_EVENT_ALL, &row);
  auto* icon = CreateLabel(handle, icon::kMenu,
      lv_color_hex(SettingsThemeColors().outline), MaterialIconFont32());
  if (icon == nullptr) return false;
  lv_obj_center(icon);
  return true;
}

/**
 * @brief 刷新授权名称和排序，拖动和弹窗期间避免重建触摸对象
 * @param view 授权管理页面
 */
void RefreshApps(AppsView* view) {
  if (view->dragging != nullptr || IsPromptDialogVisible(&view->prompt)) return;
  if (view->save_failed) {
    view->save_failed = false;
    ShowAppSaveError(view);
    return;
  }
  const auto current = app::ReadAuthorizedApps();
  bool changed = current.count != view->snapshot.count || view->rows == nullptr;
  for (size_t i = 0; i < current.count && !changed; ++i)
    changed =
        std::strcmp(current.clients[i].id, view->snapshot.clients[i].id) != 0 ||
        std::strcmp(current.clients[i].name, view->snapshot.clients[i].name) !=
            0;
  if (changed) {
    auto* input = lv_indev_active();
    if (input != nullptr && lv_indev_get_state(input) == LV_INDEV_STATE_PRESSED)
      return;
    if (view->rows != nullptr) lv_obj_delete(view->rows);
    view->snapshot = current;
    view->rows = lv_obj_create(view->body);
    if (view->rows == nullptr) return;
    MakeTransparent(view->rows);
    lv_obj_remove_flag(view->rows, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(view->rows, 0, kBasicSectionHeight);
    lv_obj_set_size(view->rows, view->settings->config.width,
        std::max(
            kAppRowHeight, static_cast<int>(current.count) * kAppRowHeight));
    if (current.count == 0) {
      CreateSectionLabel(
          view->rows, "No authorized apps.", 0, view->settings->config.width);
    } else {
      for (size_t i = 0; i < current.count; ++i) {
        if (!CreateAppManageRow(view, i)) {
          lv_obj_delete(view->rows);
          view->rows = nullptr;
          return;
        }
      }
      LayoutAppRows(view);
    }
  }
}

/**
 * @brief 构建授权管理子页，复用设置滚动布局和公共提示框
 * @param body 页面内容容器
 * @param settings 设置页状态
 * @return 页面创建成功返回 true
 */
bool BuildAuthorizedApps(lv_obj_t* body, SettingsViewState* settings) {
  auto* view = new (std::nothrow) AppsView();
  if (view == nullptr) return false;
  view->settings = settings;
  view->body = body;
  lv_obj_add_event_cb(
      body,
      [](lv_event_t* event) {
        auto* current = static_cast<AppsView*>(lv_event_get_user_data(event));
        if (current->timer != nullptr) lv_timer_delete(current->timer);
        if (current->prompt.overlay != nullptr)
          lv_obj_delete(current->prompt.overlay);
        delete current;
      },
      LV_EVENT_DELETE, view);
  CreateSectionLabel(body, "Drag to reorder. Top apps connect first.", 0,
      settings->config.width);
  RefreshApps(view);
  view->timer = lv_timer_create(
      [](lv_timer_t* timer) {
        RefreshApps(static_cast<AppsView*>(lv_timer_get_user_data(timer)));
      },
      250, view);
  return view->timer != nullptr;
}

/**
 * @brief 保存自动连接开关，网络未就绪时保持开启并使用公共提示框提醒
 * @param event 自动连接开关的值变化事件
 */
void AutoConnectChanged(lv_event_t* event) {
  auto* view = static_cast<ConnectionView*>(lv_event_get_user_data(event));
  const bool enabled = lv_obj_has_state(view->toggle, LV_STATE_CHECKED);
  hal::WifiStatus wifi;
  const bool has_status = view->settings->config.wifi != nullptr &&
                          view->settings->config.wifi->ReadWifiStatus(&wifi);
  const bool network_ready =
      has_status && wifi.running && wifi.connected && wifi.got_ip;
  const char* title = nullptr;
  const char* subtitle = nullptr;
  if (!app::SetAppAutoConnect(enabled)) {
    title = "Unable to update connection";
    subtitle = "Please try again.";
  } else if (enabled && !network_ready) {
    title = "WLAN connection required";
    subtitle =
        !has_status || !wifi.running
            ? "Turn on WLAN and connect to a network. Auto-connect remains on."
            : "Connect to a WLAN network. Auto-connect remains on.";
  }
  Refresh(view);
  if (title == nullptr) {
    return;
  }
  auto config = MakeSettingsTextPromptConfig(view->settings);
  config.title = title;
  config.subtitle = subtitle;
  config.title_font = Font32();
  config.subtitle_font = Font24();
  config.action_font = Font28();
  config.title_text_align = LV_TEXT_ALIGN_CENTER;
  config.subtitle_text_align = LV_TEXT_ALIGN_CENTER;
  config.cancel_text = "OK";
  config.cancel_background_color = theme::FixedColors().action;
  config.cancel_pressed_color = theme::FixedColors().action_pressed;
  config.cancel_text_color = theme::FixedColors().on_action;
  config.confirm_text = nullptr;
  ShowPromptDialog(view->settings->settings_nested_page, &view->prompt, config);
}

/**
 * @brief 释放页面计时器和弹窗，保留后台连接服务
 * @param event 页面销毁事件
 */
void PageDeleted(lv_event_t* event) {
  auto* view = static_cast<ConnectionView*>(lv_event_get_user_data(event));
  if (view->timer != nullptr) lv_timer_delete(view->timer);
  if (view->prompt.overlay != nullptr) {
    lv_obj_delete(view->prompt.overlay);
    view->prompt = {};
  }
  // 页面退出只释放 UI，后台连接与自动发现由应用服务管理。
  delete view;
}

/**
 * @brief 构建自动连接、当前连接控制和授权管理入口
 * @param body 页面内容容器
 * @param settings 设置页状态
 * @return 页面创建成功返回 true
 */
bool BuildAppConnection(lv_obj_t* body, SettingsViewState* settings) {
  auto* view = new (std::nothrow) ConnectionView();
  if (view == nullptr) return false;
  view->settings = settings;
  lv_obj_add_event_cb(body, PageDeleted, LV_EVENT_DELETE, view);
  const int width = settings->config.width;
  int y = 0;
  if (!CreateSectionLabel(body, "Connection", y, width)) {
    return false;
  }
  y += kBasicSectionHeight;
  if (!CreateSwitchRow(body, "Auto-connect", y, width,
          app::ReadAppConnectionStatus().auto_connect, nullptr, settings, false,
          &view->toggle, kAutoConnectSubtitle)) {
    return false;
  }
  lv_obj_add_event_cb(
      view->toggle, AutoConnectChanged, LV_EVENT_VALUE_CHANGED, view);
  y += kBasicSwitchRowWithSubtitleHeight;
  if (!CreateConnectionStatusRow(body, view, y)) return false;
  y += kBasicSwitchRowWithSubtitleHeight;

  if (!CreateArrowRow(
          body, "Authorized apps", "", y, width,
          [](lv_event_t* event) {
            auto* state =
                static_cast<SettingsViewState*>(lv_event_get_user_data(event));
            ShowSettingsChildPage(state->settings_nested_page, state,
                "Authorized apps", BuildAuthorizedApps);
          },
          settings))
    return false;
  view->timer = lv_timer_create(
      [](lv_timer_t* timer) {
        Refresh(static_cast<ConnectionView*>(lv_timer_get_user_data(timer)));
      },
      250, view);
  if (view->timer == nullptr) return false;
  Refresh(view);
  return true;
}

/**
 * @brief 创建更多连接页面中的 LilygoBox 入口
 * @param body 页面内容容器
 * @param state 设置页状态
 * @return 入口创建成功返回 true
 */
bool BuildMoreConnections(lv_obj_t* body, SettingsViewState* state) {
  if (!CreateSectionLabel(body, "App connections", 0, state->config.width)) {
    return false;
  }
  return CreateArrowRow(
      body, kAppConnectionTitle, "", kBasicSectionHeight, state->config.width,
      [](lv_event_t* event) {
        ShowNestedPage(
            static_cast<SettingsViewState*>(lv_event_get_user_data(event)),
            kAppConnectionTitle, BuildAppConnection);
      },
      state, kAppConnectionSubtitle);
}

}  // namespace

/**
 * @brief 使用设置页公共布局打开更多连接页面
 * @param state 设置页状态
 * @return 页面打开成功返回 true
 */
bool ShowMoreConnectionsPage(SettingsViewState* state) {
  return ShowBasicPage(state, "More Connections", BuildMoreConnections);
}

}  // namespace lilygo_box::ui
