/*
 * @Description: None
 * @Author: LILYGO_L
 * @Date: 2026-05-14 00:20:00
 * @LastEditTime: 2026-05-14 00:20:00
 * @License: GPL 3.0
 */
#pragma once

#include <cstdint>

namespace lilygo_box::hal {

class HapticProvider {
 public:
  virtual ~HapticProvider() = default;

  /**
   * @brief 播放触觉反馈波形
   * @param waveform_count 波形数量输出地址
   * @return 播放成功返回 true，否则返回 false
   */
  virtual bool PlayHapticWaveform(uint8_t* waveform_count) = 0;
};

}  // namespace lilygo_box::hal
