/*
 * @Description: None
 * @Author: LILYGO_L
 * @Date: 2026-05-10 13:27:05
 * @LastEditTime: 2026-05-12 20:58:06
 * @License: GPL 3.0
 */
#include "app/cit_test_catalog.h"

namespace lilygo_box::app {
namespace {

constexpr CitTestEntry kCitTestEntries[] = {
    {.id = "version",
        .name = "Version Info Test",
        .status = CitTestStatus::kPending},
    {.id = "touch", .name = "Touch Test", .status = CitTestStatus::kPending},
    {.id = "screen",
        .name = "Screen Color Test",
        .status = CitTestStatus::kPending},
    {.id = "vibration",
        .name = "Vibration Test",
        .status = CitTestStatus::kPending},
    {.id = "speaker",
        .name = "Speaker Test",
        .status = CitTestStatus::kPending},
    {.id = "microphone",
        .name = "Microphone Test",
        .status = CitTestStatus::kPending},
    {.id = "imu", .name = "IMU Test", .status = CitTestStatus::kPending},
    {.id = "battery",
        .name = "Battery Health Test",
        .status = CitTestStatus::kPending},
    {.id = "gps", .name = "GPS Test", .status = CitTestStatus::kPending},
    {.id = "ethernet",
        .name = "Ethernet Test",
        .status = CitTestStatus::kPending},
    {.id = "rtc", .name = "RTC Test", .status = CitTestStatus::kPending},
    {.id = "wifi",
        .name = "WIFI Get Time Test",
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
