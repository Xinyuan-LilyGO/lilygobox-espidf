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
   * @brief 读取可用 RAM 触觉反馈波形数量
   * @param waveform_count 波形数量输出地址
   * @return 读取成功返回 true，否则返回 false
   */
  virtual bool ReadHapticWaveformCount(uint8_t* waveform_count) = 0;

  /**
   * @brief 播放指定触觉反馈波形
   * @param waveform_sequence_number RAM 波形 sequence 编号
   * @param loop_count 播放循环次数，范围 1~16
   * @param gain 振动增益，范围 0~255
   * @param auto_brake true 表示启用自动制动，false 表示关闭自动制动
   * @return 播放任务启动成功返回 true，否则返回 false
   */
  virtual bool PlayHapticWaveform(uint8_t waveform_sequence_number,
      uint8_t loop_count, uint8_t gain, bool auto_brake) = 0;
};

}  // namespace lilygo_box::hal
