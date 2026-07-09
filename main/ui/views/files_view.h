/*
 * @Description: Files app view
 * @Author: LILYGO_L
 * @Date: 2026-07-09 00:00:00
 * @LastEditTime: 2026-07-09 00:00:00
 * @License: GPL 3.0
 */
#pragma once

#include "app/app_catalog.h"
#include "lvgl.h"
#include "ui/views/app_view_config.h"

namespace lilygo_box::ui {

/**
 * @brief 创建文件管理应用界面
 * @param parent 父对象
 * @param app_entry 应用条目
 * @param config 应用视图配置
 * @return 创建成功返回文件管理应用根对象，否则返回 nullptr
 */
lv_obj_t* CreateFilesView(lv_obj_t* parent, const app::AppEntry& app_entry,
    const AppViewConfig& config);

}  // namespace lilygo_box::ui
