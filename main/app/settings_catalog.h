/*
 * @Description: Settings page catalog
 * @Author: LILYGO_L
 * @Date: 2026-05-19 13:30:00
 * @LastEditTime: 2026-05-19 13:30:00
 * @License: GPL 3.0
 */
#pragma once

#include <cstddef>

namespace lilygo_box::app {

constexpr size_t kMaxSettingsEntryCount = 16;
constexpr size_t kMaxSettingsDeviceOptionCount = 8;

// 设置入口图标类型，由 UI 层映射成实际 Material 图标和颜色。
enum class SettingsIcon {
  kInfo,
  kWifi,
  kBluetooth,
  kCellTower,
  kAppList,
  kAntenna,
  kHome,
  kFile,
  kImage,
  kSunny,
  kFolder,
  kLock,
  kWarning,
  kVolumeUp,
  kBattery,
  kSettings,
};

// 设置首页入口。
struct SettingsEntry {
  const char* id = nullptr;
  const char* title = nullptr;
  const char* value = nullptr;
  SettingsIcon icon = SettingsIcon::kInfo;
  bool divider_before = false;
};

// 设置首页入口目录。
struct SettingsCatalog {
  const SettingsEntry* entries = nullptr;
  size_t entry_count = 0;
};

// 我的设备详情页下方选项。
struct SettingsDeviceOption {
  const char* id = nullptr;
  const char* title = nullptr;
};

// 我的设备详情页下方选项目录。
struct SettingsDeviceOptionCatalog {
  const SettingsDeviceOption* options = nullptr;
  size_t option_count = 0;
};

/**
 * @brief 获取设置首页入口目录
 * @return 设置首页入口目录引用
 */
const SettingsCatalog& GetSettingsCatalog();

/**
 * @brief 获取我的设备详情页下方选项目录
 * @return 我的设备详情页下方选项目录引用
 */
const SettingsDeviceOptionCatalog& GetSettingsDeviceOptionCatalog();

}  // namespace lilygo_box::app
