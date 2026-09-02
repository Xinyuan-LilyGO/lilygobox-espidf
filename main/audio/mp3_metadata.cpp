/*
 * @Description: MP3 文件元数据与音频流参数读取实现
 * @Author: LILYGO_L
 * @Date: 2026-07-14 22:45:00
 * @LastEditTime: 2026-09-02 17:52:05
 * @License: GPL 3.0
 */
#include "audio/mp3_metadata.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <limits>
#include <memory>
#include <new>

namespace lilygo_box::audio {
namespace {

constexpr size_t kId3HeaderSize = 10;
constexpr size_t kId3FrameHeaderSize = 10;
constexpr size_t kId3v1TagSize = 128;
constexpr size_t kFrameSearchLimit = 64 * 1024;
constexpr size_t kVbrHeaderSearchSize = 2 * 1024;

/**
 * @brief 将 ID3 synchsafe 四字节整数转换为主机整数
 * @param bytes 四字节数据
 * @return 转换后的整数
 */
uint32_t DecodeSynchsafe(const uint8_t* bytes) {
  return (static_cast<uint32_t>(bytes[0] & 0x7F) << 21) |
         (static_cast<uint32_t>(bytes[1] & 0x7F) << 14) |
         (static_cast<uint32_t>(bytes[2] & 0x7F) << 7) |
         static_cast<uint32_t>(bytes[3] & 0x7F);
}

/**
 * @brief 将大端序四字节整数转换为主机整数
 * @param bytes 四字节数据
 * @return 转换后的整数
 */
uint32_t DecodeBigEndian(const uint8_t* bytes) {
  return (static_cast<uint32_t>(bytes[0]) << 24) |
         (static_cast<uint32_t>(bytes[1]) << 16) |
         (static_cast<uint32_t>(bytes[2]) << 8) |
         static_cast<uint32_t>(bytes[3]);
}

/**
 * @brief 清理 ID3 文本末尾的空字符和空白字符
 * @param text 待清理文本
 */
void TrimTagText(std::string* text) {
  if (text == nullptr) {
    return;
  }
  while (!text->empty()) {
    const char value = text->back();
    if (value != '\0' && value != ' ' && value != '\r' && value != '\n') {
      break;
    }
    text->pop_back();
  }
}

/**
 * @brief 读取当前 ID3 文本帧中的 UTF-8 或单字节文本
 * @param file 已打开文件
 * @param frame_size 帧数据长度
 * @param text 文本输出地址
 * @return 读取到可用文本返回 true，否则返回 false
 */
bool ReadTextFrame(FILE* file, uint32_t frame_size, std::string* text) {
  if (file == nullptr || text == nullptr || frame_size <= 1 ||
      frame_size > 64 * 1024) {
    return false;
  }
  std::string buffer(frame_size, '\0');
  if (fread(buffer.data(), 1, frame_size, file) != frame_size) {
    return false;
  }
  const uint8_t encoding = static_cast<uint8_t>(buffer[0]);
  if (encoding != 0 && encoding != 3) {
    return false;
  }
  *text = buffer.substr(1);
  TrimTagText(text);
  return !text->empty();
}

/**
 * @brief 解析文件开头的 ID3v2 标签
 * @param file 已打开文件
 * @param metadata 元数据输出地址
 * @return MP3 音频数据起始偏移
 */
size_t ReadId3v2(FILE* file, Mp3Metadata* metadata) {
  std::array<uint8_t, kId3HeaderSize> header{};
  if (fread(header.data(), 1, header.size(), file) != header.size() ||
      std::memcmp(header.data(), "ID3", 3) != 0) {
    rewind(file);
    return 0;
  }

  const uint8_t major_version = header[3];
  const uint32_t tag_size = DecodeSynchsafe(header.data() + 6);
  const size_t tag_end = kId3HeaderSize + tag_size;
  size_t position = kId3HeaderSize;
  while (position + kId3FrameHeaderSize <= tag_end) {
    std::array<uint8_t, kId3FrameHeaderSize> frame_header{};
    if (fread(frame_header.data(), 1, frame_header.size(), file) !=
        frame_header.size()) {
      break;
    }
    position += frame_header.size();
    if (frame_header[0] == 0) {
      break;
    }

    const uint32_t frame_size = major_version == 4
                                    ? DecodeSynchsafe(frame_header.data() + 4)
                                    : DecodeBigEndian(frame_header.data() + 4);
    if (frame_size == 0 || position + frame_size > tag_end) {
      break;
    }
    const bool is_title = std::memcmp(frame_header.data(), "TIT2", 4) == 0;
    const bool is_artist = std::memcmp(frame_header.data(), "TPE1", 4) == 0;
    std::string* output =
        is_title ? &metadata->title : (is_artist ? &metadata->artist : nullptr);
    if (output == nullptr || !ReadTextFrame(file, frame_size, output)) {
      if (fseek(file, static_cast<long>(position + frame_size), SEEK_SET) !=
          0) {
        break;
      }
    }
    position += frame_size;
  }

  const bool footer_present = (header[5] & 0x10U) != 0;
  return tag_end + (footer_present ? kId3HeaderSize : 0);
}

/**
 * @brief 从 ID3v1 定长字段读取文本
 * @param data 字段数据地址
 * @param size 字段长度
 * @return 清理尾部空白后的文本
 */
std::string ReadFixedTagText(const uint8_t* data, size_t size) {
  std::string text(reinterpret_cast<const char*>(data), size);
  TrimTagText(&text);
  return text;
}

/**
 * @brief 在缺少 ID3v2 文本时读取文件尾部的 ID3v1 标题和艺术家
 * @param file 已打开文件
 * @param file_size 文件总长度
 * @param metadata 元数据输出地址
 */
bool ReadId3v1(FILE* file, long file_size, Mp3Metadata* metadata) {
  if (file_size < static_cast<long>(kId3v1TagSize) ||
      fseek(file, -static_cast<long>(kId3v1TagSize), SEEK_END) != 0) {
    return false;
  }
  std::array<uint8_t, kId3v1TagSize> tag{};
  if (fread(tag.data(), 1, tag.size(), file) != tag.size() ||
      std::memcmp(tag.data(), "TAG", 3) != 0) {
    return false;
  }
  if (metadata->title.empty()) {
    metadata->title = ReadFixedTagText(tag.data() + 3, 30);
  }
  if (metadata->artist.empty()) {
    metadata->artist = ReadFixedTagText(tag.data() + 33, 30);
  }
  return true;
}

struct FrameInfo {
  uint32_t sample_rate_hz = 0;
  uint32_t bitrate_kbps = 0;
  uint32_t frame_size_bytes = 0;
  uint16_t samples_per_frame = 0;
  uint8_t channel_count = 2;
};

/**
 * @brief 解析 MPEG Layer III 帧头
 * @param header 四字节帧头
 * @param info 帧参数输出地址
 * @return 帧头有效返回 true，否则返回 false
 */
bool ParseFrameHeader(const uint8_t* header, FrameInfo* info) {
  if (header == nullptr || info == nullptr || header[0] != 0xFF ||
      (header[1] & 0xE0U) != 0xE0U) {
    return false;
  }
  const uint8_t version = (header[1] >> 3) & 0x03U;
  const uint8_t layer = (header[1] >> 1) & 0x03U;
  const uint8_t bitrate_index = (header[2] >> 4) & 0x0FU;
  const uint8_t sample_rate_index = (header[2] >> 2) & 0x03U;
  const uint8_t padding = (header[2] >> 1) & 0x01U;
  if (version == 1 || layer != 1 || bitrate_index == 0 || bitrate_index == 15 ||
      sample_rate_index == 3) {
    return false;
  }

  constexpr uint32_t kSampleRates[4][3] = {
      {11025, 12000, 8000},
      {0, 0, 0},
      {22050, 24000, 16000},
      {44100, 48000, 32000},
  };
  constexpr uint16_t kMpeg2Bitrates[16] = {
      0,
      8,
      16,
      24,
      32,
      40,
      48,
      56,
      64,
      80,
      96,
      112,
      128,
      144,
      160,
      0,
  };
  constexpr uint16_t kMpeg1Bitrates[16] = {
      0,
      32,
      40,
      48,
      56,
      64,
      80,
      96,
      112,
      128,
      160,
      192,
      224,
      256,
      320,
      0,
  };
  info->sample_rate_hz = kSampleRates[version][sample_rate_index];
  info->bitrate_kbps = version == 3 ? kMpeg1Bitrates[bitrate_index]
                                    : kMpeg2Bitrates[bitrate_index];
  info->channel_count = ((header[3] >> 6) & 0x03U) == 3 ? 1 : 2;
  info->samples_per_frame = version == 3 ? 1152 : 576;
  const uint32_t frame_size_coefficient = version == 3 ? 144000 : 72000;
  if (info->sample_rate_hz == 0 || info->bitrate_kbps == 0) {
    return false;
  }
  info->frame_size_bytes =
      frame_size_coefficient * info->bitrate_kbps / info->sample_rate_hz +
      padding;
  return info->frame_size_bytes > 0;
}

/**
 * @brief 根据 VBR 头记录的帧数计算 MP3 总时长
 * @param frame_count 音频帧总数
 * @param info 首个有效音频帧参数
 * @return 总时长，单位为毫秒；参数无效时返回 0
 */
uint32_t CalculateFrameDurationMs(uint32_t frame_count, const FrameInfo& info) {
  if (frame_count == 0 || info.samples_per_frame == 0 ||
      info.sample_rate_hz == 0) {
    return 0;
  }
  const uint64_t duration_ms = static_cast<uint64_t>(frame_count) *
                               info.samples_per_frame * 1000ULL /
                               info.sample_rate_hz;
  return static_cast<uint32_t>(
      std::min<uint64_t>(duration_ms, std::numeric_limits<uint32_t>::max()));
}

/**
 * @brief 从首个音频帧读取 Xing、Info 或 VBRI 总帧数
 * @param file 已打开文件
 * @param frame_offset 首个有效音频帧偏移
 * @param info 首个有效音频帧参数
 * @return VBR 头计算出的总时长，未找到有效 VBR 头时返回 0
 */
uint32_t ReadVbrDurationMs(
    FILE* file, size_t frame_offset, const FrameInfo& info) {
  if (file == nullptr || info.frame_size_bytes == 0 ||
      fseek(file, static_cast<long>(frame_offset), SEEK_SET) != 0) {
    return 0;
  }

  std::unique_ptr<uint8_t[]> frame(
      new (std::nothrow) uint8_t[kVbrHeaderSearchSize]);
  if (frame == nullptr) {
    return 0;
  }
  const size_t bytes_to_read =
      std::min<size_t>(info.frame_size_bytes, kVbrHeaderSearchSize);
  const size_t bytes_read = fread(frame.get(), 1, bytes_to_read, file);
  for (size_t position = 0; position + 4 <= bytes_read; ++position) {
    const bool has_xing = std::memcmp(frame.get() + position, "Xing", 4) == 0 ||
                          std::memcmp(frame.get() + position, "Info", 4) == 0;
    if (has_xing && position + 12 <= bytes_read) {
      const uint32_t flags = DecodeBigEndian(frame.get() + position + 4);
      if ((flags & 0x00000001U) != 0) {
        return CalculateFrameDurationMs(
            DecodeBigEndian(frame.get() + position + 8), info);
      }
    }
    if (std::memcmp(frame.get() + position, "VBRI", 4) == 0 &&
        position + 18 <= bytes_read) {
      return CalculateFrameDurationMs(
          DecodeBigEndian(frame.get() + position + 14), info);
    }
  }
  return 0;
}

/**
 * @brief 从音频数据起点查找首个有效 MP3 帧
 * @param file 已打开文件
 * @param start_offset 搜索起始偏移
 * @param frame_offset 帧偏移输出地址
 * @param info 帧参数输出地址
 * @return 找到有效帧返回 true，否则返回 false
 */
bool FindFirstFrame(
    FILE* file, size_t start_offset, size_t* frame_offset, FrameInfo* info) {
  if (fseek(file, static_cast<long>(start_offset), SEEK_SET) != 0) {
    return false;
  }
  std::array<uint8_t, 4> header{};
  for (size_t index = 0; index < kFrameSearchLimit; ++index) {
    if (fread(header.data(), 1, header.size(), file) != header.size()) {
      return false;
    }
    if (ParseFrameHeader(header.data(), info)) {
      *frame_offset = start_offset + index;
      return true;
    }
    if (fseek(file, -3, SEEK_CUR) != 0) {
      return false;
    }
  }
  return false;
}

}  // namespace

bool ReadMp3Metadata(const char* path, Mp3Metadata* metadata) {
  if (path == nullptr || path[0] == '\0' || metadata == nullptr) {
    return false;
  }
  *metadata = Mp3Metadata{};
  FILE* file = fopen(path, "rb");
  if (file == nullptr) {
    return false;
  }
  if (fseek(file, 0, SEEK_END) != 0) {
    fclose(file);
    return false;
  }
  const long file_size = ftell(file);
  rewind(file);
  if (file_size <= 0) {
    fclose(file);
    return false;
  }

  const size_t id3_end = ReadId3v2(file, metadata);
  const bool has_id3v1 = ReadId3v1(file, file_size, metadata);
  size_t frame_offset = id3_end;
  FrameInfo frame_info;
  const bool frame_found =
      FindFirstFrame(file, id3_end, &frame_offset, &frame_info);
  if (!frame_found) {
    fclose(file);
    return false;
  }

  metadata->audio_data_offset = frame_offset;
  metadata->sample_rate_hz = frame_info.sample_rate_hz;
  metadata->bitrate_kbps = frame_info.bitrate_kbps;
  metadata->channel_count = frame_info.channel_count;
  metadata->duration_ms = ReadVbrDurationMs(file, frame_offset, frame_info);
  if (metadata->duration_ms == 0) {
    const uint64_t audio_end =
        static_cast<uint64_t>(file_size) - (has_id3v1 ? kId3v1TagSize : 0);
    const uint64_t audio_bytes =
        audio_end > frame_offset ? audio_end - frame_offset : 0;
    metadata->duration_ms =
        static_cast<uint32_t>(audio_bytes * 8ULL / frame_info.bitrate_kbps);
  }
  fclose(file);
  return true;
}

}  // namespace lilygo_box::audio
