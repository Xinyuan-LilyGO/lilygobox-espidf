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

constexpr size_t kMaxCitTestEntryCount = 16;

enum class CitTestStatus {
  kReady,
  kFailed,
  kPending,
};

struct CitTestEntry {
  const char* id = nullptr;
  const char* name = nullptr;
  CitTestStatus status = CitTestStatus::kPending;
};

struct CitTestCatalog {
  const CitTestEntry* entries = nullptr;
  size_t entry_count = 0;
};

// Returns the current hardware self-test entries shown by the CIT page.
const CitTestCatalog& GetCitTestCatalog();
const char* GetCitTestStatusText(CitTestStatus status);

}  // namespace lilygo_box::app
