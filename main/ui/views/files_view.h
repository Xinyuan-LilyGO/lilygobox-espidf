/*
 * @Description: Files app view
 * @Author: LILYGO_L
 * @Date: 2026-07-09 00:00:00
 * @LastEditTime: 2026-07-09 00:00:00
 * @License: GPL 3.0
 */
#pragma once

#include <cstdint>
#include <functional>

#include "app/app_catalog.h"
#include "lvgl.h"
#include "ui/views/app_view_config.h"

namespace lilygo_box::ui {

// 临时文件夹选择页面的显示和回调配置。
struct FolderPickerViewConfig {
  AppViewConfig view_config;
  const char* title = "Select folder";
  const char* action_text = "Use this folder";
  int animation_ms = 240;
  // 0 表示使用当前主题的操作色。
  uint32_t action_color = 0;
  uint32_t action_text_color = 0;
  std::function<void(const char* path)> selected_callback;
  std::function<void()> closed_callback;
};

/**
 * @brief 创建文件管理应用界面
 * @param parent 父对象
 * @param app_entry 应用条目
 * @param config 应用视图配置
 * @return 创建成功返回文件管理应用根对象，否则返回 nullptr
 */
lv_obj_t* CreateFilesView(lv_obj_t* parent, const app::AppEntry& app_entry,
    const AppViewConfig& config);

/**
 * @brief 创建用于选择目录的临时文件管理页面
 * @param parent 父对象
 * @param config 文件夹选择页面配置
 * @return 创建成功返回页面根对象，否则返回 nullptr
 */
lv_obj_t* CreateFolderPickerView(
    lv_obj_t* parent, const FolderPickerViewConfig& config);

}  // namespace lilygo_box::ui
