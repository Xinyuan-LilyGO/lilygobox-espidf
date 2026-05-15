/*
 * @Description: None
 * @Author: LILYGO_L
 * @Date: 2026-05-10 13:27:05
 * @LastEditTime: 2026-05-12 22:55:00
 * @License: GPL 3.0
 */
#pragma once

#include "app/app_catalog.h"
#include "lvgl.h"
#include "ui/views/app_view_config.h"

namespace lilygo_box::ui {

/**
 * @brief 创建本地硬件自检入口页面
 * @param parent 父对象
 * @param app_entry launcher app 入口
 * @param config app 页面配置
 * @return 创建成功返回页面对象指针，否则返回 nullptr
 * @Date 2026-05-12 22:55:00
 */
lv_obj_t* CreateCitView(lv_obj_t* parent, const app::AppEntry& app_entry,
    const AppViewConfig& config);

}  // namespace lilygo_box::ui
