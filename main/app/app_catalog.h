/*
 * @Description: None
 * @Author: LILYGO_L
 * @Date: 2026-05-10 13:27:05
 * @LastEditTime: 2026-05-10 13:27:05
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

// Returns the local app entries exposed on the launcher screen.
const AppCatalog& GetAppCatalog();

}  // namespace lilygo_box::app
