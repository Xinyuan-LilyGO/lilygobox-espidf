/*
 * @Description: Camera app view
 * @Author: LILYGO_L
 * @Date: 2026-07-02 00:00:00
 * @LastEditTime: 2026-07-02 00:00:00
 * @License: GPL 3.0
 */
#pragma once

#include "app/app_catalog.h"
#include "lvgl.h"
#include "ui/views/app_view_config.h"

namespace lilygo_box::ui {

/**
 * @brief 创建摄像头预览页面
 * @param parent 父对象
 * @param app_entry 应用条目
 * @param config 应用视图配置
 * @return 创建成功返回页面对象指针，否则返回 nullptr
 */
lv_obj_t* CreateCameraView(lv_obj_t* parent, const app::AppEntry& app_entry,
    const AppViewConfig& config);

/**
 * @brief 准备立即关闭摄像头页面并异步停止后台预览
 * @param camera_view 摄像头页面对象
 */
void PrepareCameraViewForDismiss(lv_obj_t* camera_view);

}  // namespace lilygo_box::ui
