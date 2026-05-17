/*
 * @Description: None
 * @Author: LILYGO_L
 * @Date: 2026-05-14 00:20:00
 * @LastEditTime: 2026-05-14 00:20:00
 * @License: GPL 3.0
 */
#pragma once

#include <cstddef>

namespace lilygo_box::hal {

struct SpeakerStatus {
  bool running = false;
  bool completed = false;
  bool success = false;
  size_t bytes_written = 0;
  size_t total_bytes = 0;
};

struct MicrophoneStatus {
  bool running = false;
  bool adc_to_dac_enabled = false;
  int level_percent = 0;
  int peak_sample = 0;
  size_t bytes_read = 0;
};

class AudioProvider {
 public:
  virtual ~AudioProvider() = default;

  /**
   * @brief 播放扬声器音频提示
   * @param bytes_written 实际写入 I2S 的字节数输出地址
   * @return 播放成功返回 true，否则返回 false
   */
  virtual bool PlaySpeakerTone(size_t* bytes_written) = 0;

  /**
   * @brief 创建后台任务播放扬声器音频
   * @return 任务创建成功返回 true，否则返回 false
   */
  virtual bool StartSpeakerTone() = 0;

  /**
   * @brief 读取扬声器音频播放状态
   * @param status 播放状态输出地址
   * @return 读取成功返回 true，否则返回 false
   */
  virtual bool ReadSpeakerToneStatus(
      SpeakerStatus* status) = 0;

  /**
   * @brief 创建后台任务读取麦克风采样数据
   * @return 任务创建成功返回 true，否则返回 false
   */
  virtual bool StartMicrophone() = 0;

  /**
   * @brief 停止麦克风采样并关闭 ADC 到 DAC 直通
   * @return 停止命令发送成功返回 true，否则返回 false
   */
  virtual bool StopMicrophone() = 0;

  /**
   * @brief 设置麦克风 ADC 数据是否直通到 DAC
   * @param enable true 表示打开直通，false 表示关闭直通
   * @return 设置成功返回 true，否则返回 false
   */
  virtual bool SetAudioAdcToDac(bool enable) = 0;

  /**
   * @brief 读取麦克风状态
   * @param status 麦克风状态输出地址
   * @return 读取成功返回 true，否则返回 false
   */
  virtual bool ReadMicrophoneStatus(MicrophoneStatus* status) = 0;
};

}  // namespace lilygo_box::hal
