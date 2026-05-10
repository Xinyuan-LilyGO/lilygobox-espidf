/*
 * @Description: None
 * @Author: LILYGO_L
 * @Date: 2026-05-10 13:27:05
 * @LastEditTime: 2026-05-10 23:51:34
 * @License: GPL 3.0
 */
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "app/app_catalog.h"
#include "hal/screen_device.h"
#include "lvgl.h"

namespace lilygo_box::ui {

// Creates the first local UI surface shown after device startup.
class UiManager final {
 public:
  UiManager() = default;

  /**
   * @brief 初始化 launcher 和根屏幕 UI
   * @param screen 屏幕设备对象
   * @return 初始化成功返回 true，否则返回 false
   * @Date 2026-05-10 13:01:03
   */
  bool Init(hal::ScreenDevice* screen);

 private:
  struct AppButtonContext {
    UiManager* manager = nullptr;
    const app::AppEntry* app_entry = nullptr;
  };

  /**
   * @brief 处理 app 图标点击事件
   * @param event LVGL 事件对象
   * @return
   * @Date 2026-05-10 13:01:03
   */
  static void AppButtonEventCallback(lv_event_t* event);

  /**
   * @brief 处理返回按钮点击事件
   * @param event LVGL 事件对象
   * @return
   * @Date 2026-05-10 13:01:03
   */
  static void BackButtonEventCallback(lv_event_t* event);

  /**
   * @brief 处理 launcher 和 app 页面手势事件
   * @param event LVGL 事件对象
   * @return
   * @Date 2026-05-10 13:01:03
   */
  static void GestureEventCallback(lv_event_t* event);

  /**
   * @brief 处理 launcher 页面滚动事件
   * @param event LVGL 事件对象
   * @return
   * @Date 2026-05-10 13:01:03
   */
  static void PageScrollEventCallback(lv_event_t* event);

  /**
   * @brief 创建 launcher 根容器
   * @param parent 父对象
   * @return 创建成功返回对象指针，否则返回 nullptr
   * @Date 2026-05-10 13:01:03
   */
  lv_obj_t* CreateLauncher(lv_obj_t* parent);

  /**
   * @brief 创建可拖动的 launcher 页面容器
   * @param parent 父对象
   * @return 创建成功返回对象指针，否则返回 nullptr
   * @Date 2026-05-10 13:01:03
   */
  lv_obj_t* CreatePageScroller(lv_obj_t* parent);

  /**
   * @brief 创建状态栏
   * @param parent 父对象
   * @return 创建成功返回对象指针，否则返回 nullptr
   * @Date 2026-05-10 13:01:03
   */
  lv_obj_t* CreateStatusBar(lv_obj_t* parent);

  /**
   * @brief 创建主屏时间日期区域
   * @param parent 父对象
   * @return 创建成功返回对象指针，否则返回 nullptr
   * @Date 2026-05-10 13:01:03
   */
  lv_obj_t* CreateClockGroup(lv_obj_t* parent);

  /**
   * @brief 创建主屏 app 图标网格
   * @param parent 父对象
   * @return 创建成功返回对象指针，否则返回 nullptr
   * @Date 2026-05-10 13:01:03
   */
  lv_obj_t* CreateAppGrid(lv_obj_t* parent);

  /**
   * @brief 创建一个 app 图标单元
   * @param parent 父对象
   * @param context app 按钮上下文
   * @param cell_width 图标单元宽度
   * @return 创建成功返回对象指针，否则返回 nullptr
   * @Date 2026-05-10 13:01:03
   */
  lv_obj_t* CreateAppIcon(
      lv_obj_t* parent, AppButtonContext* context, int cell_width);

  /**
   * @brief 创建底部常驻 dock
   * @param parent 父对象
   * @return 创建成功返回对象指针，否则返回 nullptr
   * @Date 2026-05-10 13:01:03
   */
  lv_obj_t* CreateDock(lv_obj_t* parent);

  /**
   * @brief 创建一个 dock 图标单元
   * @param parent 父对象
   * @param entry_index dock 图标入口索引
   * @param cell_width 图标单元宽度
   * @return 创建成功返回对象指针，否则返回 nullptr
   * @Date 2026-05-10 22:24:18
   */
  lv_obj_t* CreateDockIcon(
      lv_obj_t* parent, size_t entry_index, int cell_width);

  /**
   * @brief 创建页面指示器
   * @param parent 父对象
   * @return 创建成功返回对象指针，否则返回 nullptr
   * @Date 2026-05-10 13:01:03
   */
  lv_obj_t* CreatePageIndicator(lv_obj_t* parent);

  /**
   * @brief 更新页面指示器状态
   * @param page_index 页面索引
   * @return
   * @Date 2026-05-10 13:01:03
   */
  void UpdatePageIndicator(size_t page_index);

  /**
   * @brief 显示指定 launcher app 页面
   * @param app_entry launcher app 入口
   * @return 显示成功返回 true，否则返回 false
   * @Date 2026-05-10 13:01:03
   */
  bool ShowAppView(const app::AppEntry& app_entry);

  /**
   * @brief 返回 launcher 页面
   * @return
   * @Date 2026-05-10 13:01:03
   */
  void ShowLauncher();

  hal::ScreenDevice* screen_ = nullptr;
  lv_obj_t* root_screen_ = nullptr;
  lv_obj_t* launcher_container_ = nullptr;
  lv_obj_t* page_scroller_ = nullptr;
  lv_obj_t* home_page_ = nullptr;
  lv_obj_t* reserved_page_ = nullptr;
  lv_obj_t* active_view_container_ = nullptr;
  lv_obj_t* page_indicator_ = nullptr;
  lv_obj_t* first_page_dot_ = nullptr;
  lv_obj_t* second_page_dot_ = nullptr;
  std::array<AppButtonContext, app::kMaxAppEntryCount> button_contexts_;
  size_t button_context_count_ = 0;
  size_t page_index_ = 0;
};

}  // namespace lilygo_box::ui
