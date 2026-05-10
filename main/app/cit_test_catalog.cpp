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
    {.id = "screen", .name = "Screen", .status = CitTestStatus::kReady},
    {.id = "touch", .name = "Touch", .status = CitTestStatus::kWaiting},
    {.id = "power", .name = "Power", .status = CitTestStatus::kPending},
    {.id = "imu", .name = "IMU", .status = CitTestStatus::kPending},
    {.id = "host_link", .name = "Host Link", .status = CitTestStatus::kPending},
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
    case CitTestStatus::kWaiting:
      return "Waiting";
    case CitTestStatus::kPending:
      return "Pending";
  }
  return "Pending";
}

}  // namespace lilygo_box::app
