/*
 * @Description: None
 * @Author: LILYGO_L
 * @Date: 2026-05-10 13:27:05
 * @LastEditTime: 2026-05-10 23:28:36
 * @License: GPL 3.0
 */
#include "app/cit_test_catalog.h"

namespace lilygo_box::app {
namespace {

constexpr CitTestEntry kCitTestEntries[] = {
    {.id = "version",
        .name = "version information test",
        .status = CitTestStatus::kPending},
    {.id = "touch", .name = "touch test", .status = CitTestStatus::kPending},
    {.id = "screen",
        .name = "screen color test",
        .status = CitTestStatus::kPending},
    {.id = "vibration",
        .name = "vibration test",
        .status = CitTestStatus::kPending},
    {.id = "speaker",
        .name = "speaker test",
        .status = CitTestStatus::kPending},
    {.id = "microphone",
        .name = "microphone test",
        .status = CitTestStatus::kPending},
    {.id = "imu", .name = "imu test", .status = CitTestStatus::kPending},
    {.id = "power",
        .name = "battery health test",
        .status = CitTestStatus::kPending},
    {.id = "gps", .name = "gps test", .status = CitTestStatus::kPending},
    {.id = "ethernet",
        .name = "ethernet test",
        .status = CitTestStatus::kPending},
    {.id = "rtc", .name = "rtc test", .status = CitTestStatus::kPending},
    {.id = "esp32c6",
        .name = "esp32c6 at test",
        .status = CitTestStatus::kPending},
};

constexpr size_t kCitTestEntryCount =
    sizeof(kCitTestEntries) / sizeof(kCitTestEntries[0]);
static_assert(
    kCitTestEntryCount <= kMaxCitTestEntryCount, "too many CIT test entries");

const CitTestCatalog kCitTestCatalog = {
    .entries = kCitTestEntries,
    .entry_count = kCitTestEntryCount,
};

}  // namespace

const CitTestCatalog& GetCitTestCatalog() { return kCitTestCatalog; }

const char* GetCitTestStatusText(CitTestStatus status) {
  switch (status) {
    case CitTestStatus::kReady:
      return "Ready";
    case CitTestStatus::kFailed:
      return "Failed";
    case CitTestStatus::kPending:
      return "Pending";
  }
  return "Pending";
}

}  // namespace lilygo_box::app
