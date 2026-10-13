/*
 * @Description: 扬声器与麦克风状态及控制接口
 * @Author: LILYGO_L
 * @Date: 2026-05-14 00:20:00
 * @LastEditTime: 2026-10-13 12:00:00
 * @License: GPL 3.0
 */
#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>

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
   * @brief 向后台提交音量请求，连续拖动只保留最新值，UI 不等待硬件
   * @param percent 音量百分比，范围 0~100
   * @param preview 是否循环试听；松开滑动条时传入 false
   * @param enabled 可选声音开关；未指定时保留已应用的静音状态
   * @return 请求提交成功返回 true，不代表硬件已经应用成功
   */
  bool RequestSpeakerVolumePercent(int percent, bool preview = false,
                                   std::optional<bool> enabled = std::nullopt);

  /**
   * @brief 串行应用音量与静音状态，静音不会改写保存的音量
   * @param percent 保留的音量百分比
   * @param enabled 是否允许声音输出
   * @return 硬件设置成功返回 true
   */
  bool ApplySpeakerVolumeSetting(int percent, bool enabled);

  /**
   * @brief 后台应用新音量，保留静音状态并丢弃已被替代的请求
   * @param percent 目标音量百分比
   * @param generation 后台请求序号
   * @param enabled 可选声音开关，与音量在同一后台请求内应用
   * @return 硬件设置成功返回 true
   */
  bool ApplyRequestedSpeakerVolume(int percent, uint32_t generation,
                                   std::optional<bool> enabled = std::nullopt);

  /**
   * @brief 读取已应用的声音开关，该状态仅在本次运行期间保留
   * @return 允许输出声音返回 true
   */
  bool SpeakerEnabled() const { return speaker_enabled_.load(); }

  /**
   * @brief 读取扬声器音频播放状态
   * @param status 播放状态输出地址
   * @return 读取成功返回 true，否则返回 false
   */
  virtual bool ReadSpeakerToneStatus(SpeakerStatus* status) = 0;

  /**
   * @brief 创建后台任务解码并播放音频文件
   * @param path 音频文件绝对路径
   * @param duration_ms 音频总时长，单位毫秒
   * @return 任务创建成功返回 true，否则返回 false
   */
  virtual bool StartAudioFile(const char* path, uint32_t duration_ms) = 0;

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
  virtual bool ReadAudioFileStatus(AudioFilePlaybackStatus* status) = 0;

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

 private:
  std::mutex speaker_setting_mutex_;
  std::atomic<bool> speaker_enabled_{true};
};

}  // namespace lilygo_box::hal
