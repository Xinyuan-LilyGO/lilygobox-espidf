/*
 * @Description: None
 * @Author: LILYGO_L
 * @Date: 2026-05-10 13:27:05
 * @LastEditTime: 2026-05-10 23:28:15
 * @License: GPL 3.0
 */
#include "app/app_catalog.h"

namespace lilygo_box::app {
namespace {

constexpr AppEntry kHomeAppEntries[] = {
    {.id = "cit", .title = "CIT", .subtitle = "Hardware self-test entry"},
    {.id = "rf", .title = "RF", .subtitle = "RF tools placeholder"},
    {.id = "music", .title = "Music", .subtitle = "Audio UI placeholder"},
    {.id = "files", .title = "Files", .subtitle = "File manager"},
};

constexpr AppEntry kDockAppEntries[] = {
    {.id = "camera", .title = "Camera", .subtitle = "Camera shortcut"},
    {.id = "settings", .title = "Settings", .subtitle = "System settings"},
};

constexpr size_t kHomeAppEntryCount =
    sizeof(kHomeAppEntries) / sizeof(kHomeAppEntries[0]);
constexpr size_t kDockAppEntryCount =
    sizeof(kDockAppEntries) / sizeof(kDockAppEntries[0]);
static_assert(kHomeAppEntryCount <= kMaxAppEntryCount);
static_assert(kDockAppEntryCount <= kMaxAppEntryCount);

const AppCatalog kHomeAppCatalog = {
    .entries = kHomeAppEntries,
    .entry_count = kHomeAppEntryCount,
};

const AppCatalog kDockAppCatalog = {
    .entries = kDockAppEntries,
    .entry_count = kDockAppEntryCount,
};

}  // namespace

/**
 * @brief 获取 launcher 主屏显示的本地 app 入口目录
 * @return app 入口目录引用
 */
const AppCatalog& GetHomeAppCatalog() { return kHomeAppCatalog; }

/**
 * @brief 获取 launcher 底部 dock 显示的本地 app 入口目录
 * @return app 入口目录引用
 */
const AppCatalog& GetDockAppCatalog() { return kDockAppCatalog; }

/**
 * @brief 获取默认 launcher app 入口目录
 * @return app 入口目录引用
 */
const AppCatalog& GetAppCatalog() { return GetHomeAppCatalog(); }

}  // namespace lilygo_box::app
