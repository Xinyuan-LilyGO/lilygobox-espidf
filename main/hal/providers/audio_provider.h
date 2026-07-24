/*
 * @Description: 扬声器与麦克风状态及控制接口
 * @Author: LILYGO_L
 * @Date: 2026-05-14 00:20:00
 * @LastEditTime: 2026-07-15 11:16:11
 * @License: GPL 3.0
 */
#pragma once

#include <cstddef>
#include <cstdint>

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

enum class AudioFilePlaybackState {
  kStopped,
  kPlaying,
  kPaused,
  kCompleted,
  kError,
};

struct AudioFilePlaybackStatus {
  AudioFilePlaybackState state = AudioFilePlaybackState::kStopped;
  uint32_t elapsed_ms = 0;
  uint32_t duration_ms = 0;
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
   * @brief 创建后台任务循环播放扬声器音频预览
   * @return 任务创建成功或已经在播放返回 true，否则返回 false
   */
  virtual bool StartSpeakerToneLoop() = 0;

  /**
   * @brief 停止后台循环播放扬声器音频预览
   * @return 停止命令发送成功返回 true，否则返回 false
   */
  virtual bool StopSpeakerToneLoop() = 0;

  /**
   * @brief 设置扬声器播放音量百分比
   * @param percent 音量百分比，范围 0~100
   * @return 设置成功返回 true，否则返回 false
   */
  virtual bool SetSpeakerVolumePercent(int percent) = 0;

  /**
   * @brief 读取扬声器音频播放状态
   * @param status 播放状态输出地址
   * @return 读取成功返回 true，否则返回 false
   */
  virtual bool ReadSpeakerToneStatus(
      SpeakerStatus* status) = 0;

  /**
   * @brief 创建后台任务解码并播放音频文件
   * @param path 音频文件绝对路径
   * @param duration_ms 音频总时长，单位毫秒
   * @return 任务创建成功返回 true，否则返回 false
   */
  virtual bool StartAudioFile(
      const char* path, uint32_t duration_ms) = 0;

  /**
   * @brief 暂停当前音频文件播放
   * @return 暂停成功返回 true，否则返回 false
   */
  virtual bool PauseAudioFile() = 0;

  /**
   * @brief 恢复当前音频文件播放
   * @return 恢复成功返回 true，否则返回 false
   */
  virtual bool ResumeAudioFile() = 0;

  /**
   * @brief 请求将当前音频文件定位到指定播放时间
   * @param position_ms 目标播放时间，单位毫秒
   * @return 定位请求发送成功返回 true，否则返回 false
   */
  virtual bool SeekAudioFile(uint32_t position_ms) = 0;

  /**
   * @brief 请求停止当前音频文件播放
   * @return 停止请求发送成功返回 true，否则返回 false
   */
  virtual bool StopAudioFile() = 0;

  /**
   * @brief 读取当前音频文件播放状态
   * @param status 播放状态输出地址
   * @return 读取成功返回 true，否则返回 false
   */
  virtual bool ReadAudioFileStatus(
      AudioFilePlaybackStatus* status) = 0;

  /**
   * @brief 创建后台任务读取麦克风采样数据
   * @return 任务创建成功返回 true，否则返回 false
   */
  virtual bool StartMicrophone() = 0;

  /**
   * @brief 停止麦克风采样并关闭 ADC PCM 到 DAC 的实时转送
   * @return 停止命令发送成功返回 true，否则返回 false
   */
  virtual bool StopMicrophone() = 0;

  /**
   * @brief 设置是否将麦克风 ADC PCM 数据实时转送到 DAC
   * @param enable true 表示打开实时转送，false 表示关闭实时转送
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
