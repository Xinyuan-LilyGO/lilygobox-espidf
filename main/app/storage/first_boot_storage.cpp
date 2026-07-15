/*
 * @Description: 首次开机欢迎页完成标志存储实现
 * @Author: LILYGO_L
 * @Date: 2026-07-15 00:00:00
 * @LastEditTime: 2026-07-15 00:00:00
 * @License: GPL 3.0
 */
#include "app/storage/first_boot_storage.h"

#include <atomic>
#include <cstdint>

#include "nvs.h"

namespace lilygo_box::app {
namespace {

constexpr const char* kNvsNamespace = "settings";
constexpr const char* kNvsKey = "first_boot";
std::atomic<bool> g_first_boot_completed{false};

}  // namespace

void InitFirstBootCache() {
  g_first_boot_completed.store(false);

  nvs_handle_t handle = 0;
  if (nvs_open(kNvsNamespace, NVS_READONLY, &handle) != ESP_OK) {
    return;
  }

  uint8_t completed = 0;
  if (nvs_get_u8(handle, kNvsKey, &completed) == ESP_OK) {
    g_first_boot_completed.store(completed != 0);
  }
  nvs_close(handle);
}

bool IsFirstBootCompleted() {
  return g_first_boot_completed.load();
}

bool MarkFirstBootCompleted() {
  nvs_handle_t handle = 0;
  if (nvs_open(kNvsNamespace, NVS_READWRITE, &handle) != ESP_OK) {
    return false;
  }

  const bool saved = nvs_set_u8(handle, kNvsKey, 1) == ESP_OK &&
                     nvs_commit(handle) == ESP_OK;
  nvs_close(handle);
  if (saved) {
    g_first_boot_completed.store(true);
  }
  return saved;
}

}  // namespace lilygo_box::app
