/*
 * @Description: 系统设置应用页面创建接口
 * @Author: LILYGO_L
 * @Date: 2026-05-18 09:20:00
 * @LastEditTime: 2026-05-18 09:20:00
 * @License: GPL 3.0
 */
#pragma once

#include "app/app_catalog.h"
#include "lvgl.h"
#include "ui/views/app_view_config.h"

namespace lilygo_box::ui {

/**
 * @brief 创建设置页面
 * @param parent 父对象
 * @param app_entry launcher app 入口
 * @param config app 页面配置
 * @return 创建成功返回页面对象指针，否则返回 nullptr
 */
lv_obj_t* CreateSettingsView(lv_obj_t* parent, const app::AppEntry& app_entry,
    const AppViewConfig& config);

/**
 * @brief 同步设置页面保存的音量值和当前可见的音量滑动条
 * @param settings_view 设置页面根对象
 * @param volume_percent 目标音量百分比
 */
void UpdateSettingsViewVolume(
    lv_obj_t* settings_view, int volume_percent);

}  // namespace lilygo_box::ui
