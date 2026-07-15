/*
 * @Description: MP3 流式解码与 PCM 输出接口
 * @Author: LILYGO_L
 * @Date: 2026-07-14 22:50:00
 * @LastEditTime: 2026-07-15 11:16:11
 * @License: GPL 3.0
 */
#pragma once

#include <cstddef>
#include <cstdint>

namespace lilygo_box::audio {

enum class Mp3PlaybackResult {
  kCompleted,
  kStopped,
  kOpenFailed,
  kInvalidStream,
  kDecoderFailed,
  kOutputFailed,
};

class PcmOutput {
 public:
  virtual ~PcmOutput() = default;

  /**
   * @brief 根据 MP3 流参数配置 PCM 输出设备
   * @param sample_rate_hz 采样率
   * @param channel_count 声道数
   * @param bits_per_sample 采样位宽
   * @return 配置成功返回 true，否则返回 false
   */
  virtual bool Configure(uint32_t sample_rate_hz, uint8_t channel_count,
      uint8_t bits_per_sample) = 0;

  /**
   * @brief 等待输出设备可以继续接收数据
   * @return 可以继续播放返回 true，请求停止返回 false
   */
  virtual bool WaitUntilReady() = 0;

  /**
   * @brief 读取并清除一个待处理的播放定位请求
   * @param position_ms 目标播放时间输出地址，单位毫秒
   * @return 存在待处理请求返回 true，否则返回 false
   */
  virtual bool TakeSeekRequest(uint32_t* position_ms) = 0;

  /**
   * @brief 向音频设备写入一段 PCM 数据
   * @param data PCM 数据地址
   * @param size PCM 数据字节数
   * @return 完整写入返回 true，否则返回 false
   */
  virtual bool Write(const uint8_t* data, size_t size) = 0;

  /**
   * @brief 更新已解码的播放时间
   * @param elapsed_ms 已播放时间，单位毫秒
   */
  virtual void UpdateProgress(uint32_t elapsed_ms) = 0;
};

/**
 * @brief 流式解码并播放一个 MP3 文件
 * @param path MP3 文件绝对路径
 * @param output PCM 输出接口
 * @return 播放结束原因
 */
Mp3PlaybackResult PlayMp3File(const char* path, PcmOutput* output);

}  // namespace lilygo_box::audio
