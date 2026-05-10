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

constexpr AppEntry kAppEntries[] = {
    {.id = "cit", .title = "CIT", .subtitle = "Hardware self-test entry"},
    {.id = "rf", .title = "RF", .subtitle = "RF tools placeholder"},
    {.id = "music", .title = "Music", .subtitle = "Audio UI placeholder"},
};

constexpr size_t kAppEntryCount = sizeof(kAppEntries) / sizeof(kAppEntries[0]);
static_assert(kAppEntryCount <= kMaxAppEntryCount);

const AppCatalog kAppCatalog = {
    .entries = kAppEntries,
    .entry_count = kAppEntryCount,
};

}  // namespace

const AppCatalog& GetAppCatalog() { return kAppCatalog; }

}  // namespace lilygo_box::app
