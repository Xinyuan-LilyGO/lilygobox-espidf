/*
 * @Description: Settings page catalog
 * @Author: LILYGO_L
 * @Date: 2026-05-19 13:30:00
 * @LastEditTime: 2026-05-19 13:30:00
 * @License: GPL 3.0
 */
#include "app/settings_catalog.h"

namespace lilygo_box::app {
namespace {

constexpr SettingsEntry kSettingsEntries[] = {
    {.id = "my_device",
        .title = "My Device",
        .value = "",
        .icon = SettingsIcon::kInfo,
        .divider_before = false},
    {.id = "wlan",
        .title = "WLAN",
        .value = "LilyGo-AABB-5G",
        .icon = SettingsIcon::kWifi,
        .divider_before = true},
    {.id = "bluetooth",
        .title = "Bluetooth",
        .value = "Off",
        .icon = SettingsIcon::kBluetooth,
        .divider_before = false},
    {.id = "personal_hotspot",
        .title = "Personal Hotspot",
        .value = "Off",
        .icon = SettingsIcon::kAntenna,
        .divider_before = false},
    {.id = "lock_screen",
        .title = "Lock Screen",
        .value = "",
        .icon = SettingsIcon::kLock,
        .divider_before = true},
    {.id = "display_brightness",
        .title = "Display & Brightness",
        .value = "",
        .icon = SettingsIcon::kSunny,
        .divider_before = false},
    {.id = "sound",
        .title = "Sound & Haptics",
        .value = "",
        .icon = SettingsIcon::kVolumeUp,
        .divider_before = false},
    {.id = "power_battery",
        .title = "Power Saving & Battery",
        .value = "",
        .icon = SettingsIcon::kBattery,
        .divider_before = true},
    {.id = "more_settings",
        .title = "More Settings",
        .value = "",
        .icon = SettingsIcon::kSettings,
        .divider_before = true},
};

constexpr SettingsDeviceOption kSettingsDeviceOptions[] = {
    {.id = "factory_reset", .title = "Factory reset"},
    {.id = "authentication_info", .title = "Authentication info"},
};

constexpr size_t kSettingsEntryCount =
    sizeof(kSettingsEntries) / sizeof(kSettingsEntries[0]);
constexpr size_t kSettingsDeviceOptionCount =
    sizeof(kSettingsDeviceOptions) / sizeof(kSettingsDeviceOptions[0]);

static_assert(kSettingsEntryCount <= kMaxSettingsEntryCount);
static_assert(kSettingsDeviceOptionCount <= kMaxSettingsDeviceOptionCount);

const SettingsCatalog kSettingsCatalog = {
    .entries = kSettingsEntries,
    .entry_count = kSettingsEntryCount,
};

const SettingsDeviceOptionCatalog kSettingsDeviceOptionCatalog = {
    .options = kSettingsDeviceOptions,
    .option_count = kSettingsDeviceOptionCount,
};

}  // namespace

const SettingsCatalog& GetSettingsCatalog() { return kSettingsCatalog; }

const SettingsDeviceOptionCatalog& GetSettingsDeviceOptionCatalog() {
  return kSettingsDeviceOptionCatalog;
}

}  // namespace lilygo_box::app
