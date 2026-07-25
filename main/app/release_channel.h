/*
 * @Description: LilygoBox 发布频道公共定义
 * @Author: LILYGO_L
 * @Date: 2026-07-25 00:00:00
 * @LastEditTime: 2026-07-25 00:00:00
 * @License: GPL 3.0
 */
#pragma once

#include "sdkconfig.h"

namespace lilygo_box::app {

// 应用构建使用的固定发布频道。
enum class ReleaseChannel {
  kAlpha,
  kBeta,
  kStable,
};

// Kconfig 宏只在这里转换为应用层使用的 C++ 枚举。
#if defined(CONFIG_LILYGO_BOX_RELEASE_CHANNEL_ALPHA)
inline constexpr ReleaseChannel kReleaseChannel = ReleaseChannel::kAlpha;
#elif defined(CONFIG_LILYGO_BOX_RELEASE_CHANNEL_BETA)
inline constexpr ReleaseChannel kReleaseChannel = ReleaseChannel::kBeta;
#elif defined(CONFIG_LILYGO_BOX_RELEASE_CHANNEL_STABLE)
inline constexpr ReleaseChannel kReleaseChannel = ReleaseChannel::kStable;
#else
#error "A LilygoBox release channel must be selected"
#endif

/**
 * @brief 获取发布频道的稳定协议名称
 * @param channel 发布频道
 * @return alpha、beta 或 stable
 */
constexpr const char* ReleaseChannelName(ReleaseChannel channel) {
  switch (channel) {
    case ReleaseChannel::kAlpha:
      return "alpha";
    case ReleaseChannel::kBeta:
      return "beta";
    case ReleaseChannel::kStable:
      return "stable";
  }
  return "stable";
}

}  // namespace lilygo_box::app
