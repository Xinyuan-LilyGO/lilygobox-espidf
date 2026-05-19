/*
 * @Description: None
 * @Author: LILYGO_L
 * @Date: 2026-05-18 12:08:00
 * @LastEditTime: 2026-05-18 12:08:00
 * @License: GPL 3.0
 */
#include "app/device_identity.h"

#include <cctype>
#include <cstring>

namespace lilygo_box::app {
namespace {

// 用户设置的本机设备名称，空字符串表示使用硬件型号名。
char g_device_name[kMaxDeviceNameLength + 1] = "";

/**
 * @brief 判断字符是否为空白字符
 * @param character 待判断字符
 * @return 是空白字符返回 true，否则返回 false
 */
bool IsSpace(char character) {
  return std::isspace(static_cast<unsigned char>(character)) != 0;
}

}  // namespace

const char* ConfiguredDeviceName() { return g_device_name; }

bool SetConfiguredDeviceName(const char* name) {
  if (name == nullptr) {
    return false;
  }

  const char* begin = name;
  while (*begin != '\0' && IsSpace(*begin)) {
    ++begin;
  }

  const char* end = begin + std::strlen(begin);
  while (end > begin && IsSpace(*(end - 1))) {
    --end;
  }

  if (begin == end) {
    return false;
  }

  size_t length = static_cast<size_t>(end - begin);
  if (length > kMaxDeviceNameLength) {
    length = kMaxDeviceNameLength;
  }

  std::memcpy(g_device_name, begin, length);
  g_device_name[length] = '\0';
  return true;
}

}  // namespace lilygo_box::app
