/*
 * @Description: 整机测试项目目录与测试状态文本实现
 * @Author: LILYGO_L
 * @Date: 2026-05-10 13:27:05
 * @LastEditTime: 2026-07-30 18:00:00
 * @License: GPL 3.0
 */
#include "app/cit_catalog.h"

#include "sdkconfig.h"

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
    {.id = "battery_management",
        .name = "Battery Management Test",
        .status = CitTestStatus::kPending},
    {.id = "gps", .name = "GPS Test", .status = CitTestStatus::kPending},
#if defined(CONFIG_LILYGO_DEVICE_DRIVER_T_DISPLAY_P4)
    {.id = "ethernet",
        .name = "Ethernet Test",
        .status = CitTestStatus::kPending},
    {.id = "rtc", .name = "RTC Test", .status = CitTestStatus::kPending},
#endif
    {.id = "wifi",
        .name = "WIFI Get Time Test",
        .status = CitTestStatus::kPending},
#if defined(CONFIG_LILYGO_DEVICE_DRIVER_T_DISPLAY_P4_AIR)
    {.id = "radio",
        .name = "Radio Test",
        .status = CitTestStatus::kPending},
    {.id = "storage",
        .name = "SD Card Test",
        .status = CitTestStatus::kPending},
    {.id = "camera", .name = "Camera Test", .status = CitTestStatus::kPending},
    {.id = "nfc", .name = "NFC Test", .status = CitTestStatus::kPending},
    {.id = "infrared",
        .name = "Infrared Test",
        .status = CitTestStatus::kPending},
    {.id = "cellular",
        .name = "Cellular Test",
        .status = CitTestStatus::kPending},
#endif
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
