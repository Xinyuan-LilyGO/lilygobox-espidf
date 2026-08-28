/*
 * @Description: WiFi Provider 公共数据转换辅助实现
 * @Author: LILYGO_L
 * @Date: 2026-08-28 00:00:00
 * @LastEditTime: 2026-08-28 00:00:00
 * @License: GPL 3.0
 */
#include "hal/device/common/wifi_utils.h"

#include <cstddef>

namespace lilygo_box::hal::wifi_utils {

uint64_t PackMacAddress(const uint8_t* mac_address) {
  if (mac_address == nullptr) {
    return 0;
  }

  uint64_t packed = 0;
  for (size_t index = 0; index < 6; ++index) {
    packed = (packed << 8) | mac_address[index];
  }
  return packed;
}

bool IsSecureAuthMode(wifi_auth_mode_t auth_mode) {
  return auth_mode != WIFI_AUTH_OPEN;
}

bool IsFiveGChannel(int channel) { return channel > 14; }

}  // namespace lilygo_box::hal::wifi_utils
