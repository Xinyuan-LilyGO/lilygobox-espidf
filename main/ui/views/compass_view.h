/*
 * @Description: 指南针应用页面接口
 * @Author: LILYGO_L
 * @Date: 2026-09-28 00:00:00
 * @LastEditTime: 2026-09-28 00:00:00
 * @License: GPL 3.0
 */
#pragma once

#include "app/app_catalog.h"
#include "ui/views/app_view_config.h"

namespace lilygo_box::ui {

/**
 * @brief 创建包含磁北罗盘、GPS 信息和校准操作的指南针页面
 * @param parent 父对象
 * @param app_entry 应用条目
 * @param config 页面尺寸、传感器及导航配置
 * @return 成功返回页面根对象，否则返回 nullptr
 */
lv_obj_t* CreateCompassView(lv_obj_t* parent, const app::AppEntry& app_entry,
    const AppViewConfig& config);

}  // namespace lilygo_box::ui
