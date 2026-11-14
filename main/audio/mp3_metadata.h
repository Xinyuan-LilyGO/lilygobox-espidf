/*
 * @Description: MP3 文件元数据与音频流参数读取接口
 */
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace lilygo_box::audio {

struct Mp3Metadata {
  std::string title;
  std::string artist;
  uint32_t duration_ms = 0;
  uint32_t sample_rate_hz = 0;
  uint32_t bitrate_kbps = 0;
  uint8_t channel_count = 2;
  size_t audio_data_offset = 0;
};

/**
 * @brief 读取 MP3 文件的 ID3 文本、音频流参数和总时长
 * @param path MP3 文件绝对路径
 * @param metadata 元数据输出地址
 * @return 读取成功返回 true，否则返回 false
 */
bool ReadMp3Metadata(const char* path, Mp3Metadata* metadata);

}  // namespace lilygo_box::audio
