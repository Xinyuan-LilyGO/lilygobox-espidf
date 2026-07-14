/*
 * @Description: 主屏幕与底部停靠栏应用目录数据接口
 * @Author: LILYGO_L
 * @Date: 2026-05-10 13:27:05
 * @LastEditTime: 2026-05-12 22:55:00
 * @License: GPL 3.0
 */
#pragma once

#include <cstddef>

namespace lilygo_box::app {

static constexpr size_t kMaxAppEntryCount = 8;

struct AppEntry {
  const char* id = nullptr;
  const char* title = nullptr;
  const char* subtitle = nullptr;
};

struct AppCatalog {
  const AppEntry* entries = nullptr;
  size_t entry_count = 0;
};

/**
 * @brief 获取 launcher 页面显示的本地 app 入口目录
 * @return app 入口目录引用
 */
const AppCatalog& GetHomeAppCatalog();

/**
 * @brief 获取 launcher 固定栏显示的本地 app 入口目录
 * @return app 入口目录引用
 */
const AppCatalog& GetDockAppCatalog();

/**
 * @brief 获取默认 launcher app 入口目录
 * @return app 入口目录引用
 */
const AppCatalog& GetAppCatalog();

}  // namespace lilygo_box::app
