/*
 * @Description: MP3 流式解码与 PCM 输出实现
 * @Author: LILYGO_L
 * @Date: 2026-07-14 22:50:00
 * @LastEditTime: 2026-09-02 17:52:03
 * @License: GPL 3.0
 */
#include "audio/mp3_decoder.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>

#include "audio/mp3_metadata.h"
#include "base/logger.h"
#include "esp_audio_dec.h"
#include "esp_audio_dec_default.h"

namespace lilygo_box::audio {
namespace {

constexpr size_t kReadBufferSize = 8 * 1024;
constexpr size_t kPcmBufferSize = 16 * 1024;
constexpr size_t kMaxMp3FrameSize = 1441;
constexpr size_t kSeekFrameSearchRadius = 8 * 1024;
constexpr uint8_t kPcmBitsPerSample = 16;

/**
 * @brief 解析 MP3 帧头并计算完整帧长度
 * @param header 四字节 MP3 帧头
 * @param frame_size 帧长度输出地址
 * @return 帧头有效返回 true，否则返回 false
 */
bool ParseMp3FrameSize(const uint8_t* header, size_t* frame_size) {
  if (header == nullptr || frame_size == nullptr || header[0] != 0xFF ||
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
  const uint32_t sample_rate_hz = kSampleRates[version][sample_rate_index];
  const uint32_t bitrate_kbps = version == 3 ? kMpeg1Bitrates[bitrate_index]
                                             : kMpeg2Bitrates[bitrate_index];
  if (sample_rate_hz == 0 || bitrate_kbps == 0) {
    return false;
  }
  const uint32_t coefficient = version == 3 ? 144000 : 72000;
  *frame_size = coefficient * bitrate_kbps / sample_rate_hz + padding;
  return *frame_size >= 4 && *frame_size <= kMaxMp3FrameSize;
}

/**
 * @brief 在估算文件位置附近查找有效 MP3 帧
 * @param file 已打开的 MP3 文件
 * @param file_size 文件总长度
 * @param audio_offset 音频数据起始偏移
 * @param estimated_offset 按播放时间估算的文件偏移
 * @param frame_offset 有效帧偏移输出地址
 * @return 找到有效帧返回 true，否则返回 false
 */
bool FindSeekFrame(FILE* file, size_t file_size, size_t audio_offset,
    size_t estimated_offset, size_t* frame_offset) {
  if (file == nullptr || frame_offset == nullptr || audio_offset >= file_size) {
    return false;
  }
  const long original_offset = ftell(file);
  const size_t search_start = estimated_offset > kSeekFrameSearchRadius
                                  ? estimated_offset - kSeekFrameSearchRadius
                                  : audio_offset;
  const size_t clamped_search_start = std::max(search_start, audio_offset);
  const size_t candidate_end =
      estimated_offset +
      std::min(file_size - estimated_offset, kSeekFrameSearchRadius);
  const size_t search_end =
      candidate_end + std::min(file_size - candidate_end, kMaxMp3FrameSize);
  if (search_end <= clamped_search_start ||
      fseek(file, static_cast<long>(clamped_search_start), SEEK_SET) != 0) {
    return false;
  }

  const size_t search_size = search_end - clamped_search_start;
  auto search_buffer = std::make_unique<uint8_t[]>(search_size);
  const size_t bytes_read = fread(search_buffer.get(), 1, search_size, file);
  bool found_before_estimate = false;
  size_t frame_before_estimate = 0;
  bool found = false;
  for (size_t index = 0; index + 4 <= bytes_read; ++index) {
    const size_t absolute_offset = clamped_search_start + index;
    if (absolute_offset > candidate_end) {
      break;
    }
    size_t frame_size = 0;
    if (!ParseMp3FrameSize(search_buffer.get() + index, &frame_size)) {
      continue;
    }
    const size_t next_frame_index = index + frame_size;
    size_t next_frame_size = 0;
    if (next_frame_index + 4 > bytes_read ||
        !ParseMp3FrameSize(
            search_buffer.get() + next_frame_index, &next_frame_size)) {
      continue;
    }
    if (absolute_offset >= estimated_offset) {
      *frame_offset = absolute_offset;
      found = true;
      break;
    }
    frame_before_estimate = absolute_offset;
    found_before_estimate = true;
  }
  if (!found && found_before_estimate) {
    *frame_offset = frame_before_estimate;
    found = true;
  }
  if (original_offset >= 0) {
    fseek(file, original_offset, SEEK_SET);
  }
  return found;
}

/**
 * @brief 根据目标播放时间定位文件并重置 MP3 解码器
 * @param file 已打开的 MP3 文件
 * @param file_size 文件总长度
 * @param decoder MP3 解码器句柄
 * @param metadata MP3 元数据
 * @param position_ms 目标播放时间，单位毫秒
 * @param frame_offset 实际定位帧偏移输出地址
 * @return 定位成功返回 true，否则返回 false
 */
bool SeekMp3Stream(FILE* file, size_t file_size, esp_audio_dec_handle_t decoder,
    const Mp3Metadata& metadata, uint32_t position_ms, size_t* frame_offset) {
  if (file == nullptr || decoder == nullptr || frame_offset == nullptr ||
      metadata.duration_ms == 0 || metadata.audio_data_offset >= file_size) {
    return false;
  }
  const uint32_t clamped_position_ms =
      std::min(position_ms, metadata.duration_ms);
  const uint64_t audio_size = file_size - metadata.audio_data_offset;
  const size_t estimated_offset =
      metadata.audio_data_offset +
      static_cast<size_t>(
          audio_size * clamped_position_ms / metadata.duration_ms);
  size_t seek_offset = metadata.audio_data_offset;
  if (clamped_position_ms > 0 &&
      !FindSeekFrame(file, file_size, metadata.audio_data_offset,
          std::min(estimated_offset, file_size - 1), &seek_offset)) {
    return false;
  }
  const long original_offset = ftell(file);
  if (fseek(file, static_cast<long>(seek_offset), SEEK_SET) != 0 ||
      esp_audio_dec_reset(decoder) != ESP_AUDIO_ERR_OK) {
    if (original_offset >= 0) {
      fseek(file, original_offset, SEEK_SET);
    }
    return false;
  }
  *frame_offset = seek_offset;
  return true;
}

/**
 * @brief 根据累计 PCM 字节数计算播放时间
 * @param pcm_bytes 累计 PCM 字节数
 * @param metadata MP3 流参数
 * @return 播放时间，单位毫秒
 */
uint32_t CalculateElapsedMs(uint64_t pcm_bytes, const Mp3Metadata& metadata) {
  const uint64_t bytes_per_second =
      static_cast<uint64_t>(metadata.sample_rate_hz) * metadata.channel_count *
      (kPcmBitsPerSample / 8U);
  if (bytes_per_second == 0) {
    return 0;
  }
  return static_cast<uint32_t>(pcm_bytes * 1000ULL / bytes_per_second);
}

/**
 * @brief 根据定位起点和已解码 PCM 字节计算当前播放时间
 * @param base_position_ms 最近一次定位的目标时间
 * @param pcm_bytes 定位后累计解码的 PCM 字节数
 * @param metadata MP3 流参数
 * @return 当前播放时间，单位毫秒
 */
uint32_t CalculatePositionMs(uint32_t base_position_ms, uint64_t pcm_bytes,
    const Mp3Metadata& metadata) {
  const uint64_t position_ms = static_cast<uint64_t>(base_position_ms) +
                               CalculateElapsedMs(pcm_bytes, metadata);
  return static_cast<uint32_t>(
      std::min<uint64_t>(position_ms, metadata.duration_ms));
}

const char* PlaybackResultName(Mp3PlaybackResult result) {
  switch (result) {
    case Mp3PlaybackResult::kCompleted:
      return "completed";
    case Mp3PlaybackResult::kStopped:
      return "stopped";
    case Mp3PlaybackResult::kOpenFailed:
      return "open failed";
    case Mp3PlaybackResult::kInvalidStream:
      return "invalid stream";
    case Mp3PlaybackResult::kDecoderFailed:
      return "decoder failed";
    case Mp3PlaybackResult::kOutputFailed:
      return "output failed";
    default:
      return "unknown";
  }
}

/**
 * @brief 以 FFmpeg 风格输出 MP3 输入、元数据和音频流参数
 * @param path MP3 文件绝对路径
 * @param metadata MP3 元数据和音频流参数
 */
void LogMp3Input(const char* path, const Mp3Metadata& metadata) {
  const uint32_t duration_seconds = metadata.duration_ms / 1000U;
  const uint32_t hours = duration_seconds / 3600U;
  const uint32_t minutes = (duration_seconds % 3600U) / 60U;
  const uint32_t seconds = duration_seconds % 60U;
  const uint32_t milliseconds = metadata.duration_ms % 1000U;
  const char* channel_layout = metadata.channel_count == 1 ? "mono" : "stereo";

  LogMessage(
      LogLevel::kInfo, __FILE__, __LINE__, "Input #0, mp3, from '%s':\n", path);
  if (!metadata.title.empty() || !metadata.artist.empty()) {
    LogMessage(LogLevel::kInfo, __FILE__, __LINE__, "  Metadata:\n");
    if (!metadata.title.empty()) {
      LogMessage(LogLevel::kInfo, __FILE__, __LINE__,
          "    title           : %s\n", metadata.title.c_str());
    }
    if (!metadata.artist.empty()) {
      LogMessage(LogLevel::kInfo, __FILE__, __LINE__,
          "    artist          : %s\n", metadata.artist.c_str());
    }
  }
  LogMessage(LogLevel::kInfo, __FILE__, __LINE__,
      "  Duration: %02u:%02u:%02u.%03u, start: 0.000000, "
      "bitrate: %u kb/s\n",
      static_cast<unsigned int>(hours), static_cast<unsigned int>(minutes),
      static_cast<unsigned int>(seconds),
      static_cast<unsigned int>(milliseconds),
      static_cast<unsigned int>(metadata.bitrate_kbps));
  LogMessage(LogLevel::kInfo, __FILE__, __LINE__,
      "  Stream #0:0: Audio: mp3, %u Hz, %s, s16, %u kb/s\n",
      static_cast<unsigned int>(metadata.sample_rate_hz), channel_layout,
      static_cast<unsigned int>(metadata.bitrate_kbps));
}

}  // namespace

Mp3PlaybackResult PlayMp3File(const char* path, PcmOutput* output) {
  if (path == nullptr || path[0] == '\0' || output == nullptr) {
    LogMessage(LogLevel::kError, __FILE__, __LINE__,
        "MP3 playback rejected: invalid path or PCM output\n");
    return Mp3PlaybackResult::kOpenFailed;
  }
  Mp3Metadata metadata;
  if (!ReadMp3Metadata(path, &metadata)) {
    LogMessage(LogLevel::kError, __FILE__, __LINE__,
        "Unable to read MP3 stream metadata: %s\n", path);
    return Mp3PlaybackResult::kInvalidStream;
  }
  LogMp3Input(path, metadata);
  if (!output->Configure(
          metadata.sample_rate_hz, metadata.channel_count, kPcmBitsPerSample)) {
    LogMessage(LogLevel::kError, __FILE__, __LINE__,
        "Unable to configure PCM output for %u Hz, %u channel MP3\n",
        static_cast<unsigned int>(metadata.sample_rate_hz),
        static_cast<unsigned int>(metadata.channel_count));
    return Mp3PlaybackResult::kOutputFailed;
  }

  FILE* file = fopen(path, "rb");
  if (file == nullptr) {
    LogMessage(LogLevel::kError, __FILE__, __LINE__,
        "Unable to open MP3 input: %s\n", path);
    return Mp3PlaybackResult::kOpenFailed;
  }
  if (fseek(file, 0, SEEK_END) != 0) {
    fclose(file);
    return Mp3PlaybackResult::kInvalidStream;
  }
  const long file_size_value = ftell(file);
  if (file_size_value <= static_cast<long>(metadata.audio_data_offset) ||
      fseek(file, static_cast<long>(metadata.audio_data_offset), SEEK_SET) !=
          0) {
    LogMessage(LogLevel::kError, __FILE__, __LINE__,
        "Unable to seek to MP3 audio stream offset: %u\n",
        static_cast<unsigned int>(metadata.audio_data_offset));
    fclose(file);
    return Mp3PlaybackResult::kInvalidStream;
  }
  const size_t file_size = static_cast<size_t>(file_size_value);

  esp_audio_dec_register_default();
  esp_audio_dec_cfg_t decoder_config = {
      .type = ESP_AUDIO_TYPE_MP3,
      .cfg = nullptr,
      .cfg_sz = 0,
  };
  esp_audio_dec_handle_t decoder = nullptr;
  if (esp_audio_dec_open(&decoder_config, &decoder) != ESP_AUDIO_ERR_OK) {
    LogMessage(LogLevel::kError, __FILE__, __LINE__,
        "Unable to open the ESP MP3 decoder\n");
    esp_audio_dec_unregister_default();
    fclose(file);
    return Mp3PlaybackResult::kDecoderFailed;
  }

  auto read_buffer = std::make_unique<uint8_t[]>(kReadBufferSize);
  auto pcm_buffer = std::make_unique<uint8_t[]>(kPcmBufferSize);
  auto stereo_buffer = metadata.channel_count == 1
                           ? std::make_unique<int16_t[]>(kPcmBufferSize)
                           : nullptr;
  size_t remaining_bytes = 0;
  uint64_t decoded_pcm_bytes = 0;
  uint64_t position_pcm_bytes = 0;
  uint32_t base_position_ms = 0;
  Mp3PlaybackResult playback_result = Mp3PlaybackResult::kCompleted;
  LogMessage(LogLevel::kInfo, __FILE__, __LINE__, "MP3 playback started\n");

  while (true) {
    if (!output->WaitUntilReady()) {
      playback_result = Mp3PlaybackResult::kStopped;
      break;
    }
    const size_t read_bytes = fread(read_buffer.get() + remaining_bytes, 1,
        kReadBufferSize - remaining_bytes, file);
    const size_t total_bytes = remaining_bytes + read_bytes;
    if (total_bytes == 0) {
      break;
    }

    esp_audio_dec_in_raw_t input = {
        .buffer = read_buffer.get(),
        .len = total_bytes,
        .consumed = 0,
        .frame_recover = ESP_AUDIO_DEC_RECOVERY_NONE,
    };
    bool seek_applied = false;
    while (input.len > 0) {
      if (!output->WaitUntilReady()) {
        playback_result = Mp3PlaybackResult::kStopped;
        break;
      }
      uint32_t seek_position_ms = 0;
      if (output->TakeSeekRequest(&seek_position_ms)) {
        size_t seek_offset = 0;
        if (SeekMp3Stream(file, file_size, decoder, metadata, seek_position_ms,
                &seek_offset)) {
          base_position_ms = std::min(seek_position_ms, metadata.duration_ms);
          position_pcm_bytes = 0;
          remaining_bytes = 0;
          output->UpdateProgress(base_position_ms);
          seek_applied = true;
          LogMessage(LogLevel::kInfo, __FILE__, __LINE__,
              "MP3 seek completed: position=%u ms, offset=%u\n",
              static_cast<unsigned int>(base_position_ms),
              static_cast<unsigned int>(seek_offset));
          break;
        }
        LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
            "Unable to seek MP3 stream to %u ms\n",
            static_cast<unsigned int>(seek_position_ms));
      }
      esp_audio_dec_out_frame_t output_frame = {
          .buffer = pcm_buffer.get(),
          .len = kPcmBufferSize,
          .needed_size = 0,
          .decoded_size = 0,
      };
      const esp_audio_err_t result =
          esp_audio_dec_process(decoder, &input, &output_frame);
      if (result != ESP_AUDIO_ERR_OK) {
        if (result == ESP_AUDIO_ERR_FAIL) {
          LogMessage(LogLevel::kError, __FILE__, __LINE__,
              "MP3 decoder process failed, remaining=%u bytes\n",
              static_cast<unsigned int>(input.len));
        }
        break;
      }
      const uint8_t* output_data = output_frame.buffer;
      size_t output_size = output_frame.decoded_size;
      if (output_size > 0 && metadata.channel_count == 1) {
        const auto* mono_samples =
            reinterpret_cast<const int16_t*>(output_frame.buffer);
        const size_t sample_count = output_size / sizeof(int16_t);
        for (size_t index = 0; index < sample_count; ++index) {
          stereo_buffer[index * 2] = mono_samples[index];
          stereo_buffer[index * 2 + 1] = mono_samples[index];
        }
        output_data = reinterpret_cast<const uint8_t*>(stereo_buffer.get());
        output_size = sample_count * 2 * sizeof(int16_t);
      }
      if (output_size > 0 && !output->Write(output_data, output_size)) {
        playback_result = output->WaitUntilReady()
                              ? Mp3PlaybackResult::kOutputFailed
                              : Mp3PlaybackResult::kStopped;
        break;
      }
      decoded_pcm_bytes += output_frame.decoded_size;
      position_pcm_bytes += output_frame.decoded_size;
      const uint32_t elapsed_ms =
          CalculatePositionMs(base_position_ms, position_pcm_bytes, metadata);
      output->UpdateProgress(elapsed_ms);
      if (input.consumed == 0) {
        break;
      }
      input.buffer += input.consumed;
      input.len -= input.consumed;
    }
    if (playback_result != Mp3PlaybackResult::kCompleted) {
      break;
    }
    if (seek_applied) {
      continue;
    }

    remaining_bytes = input.len;
    if (remaining_bytes > 0) {
      std::memmove(read_buffer.get(), input.buffer, remaining_bytes);
    }
    if (read_bytes == 0 && remaining_bytes == total_bytes) {
      break;
    }
  }

  fclose(file);
  esp_audio_dec_close(decoder);
  esp_audio_dec_unregister_default();
  const uint32_t elapsed_ms =
      CalculatePositionMs(base_position_ms, position_pcm_bytes, metadata);
  const LogLevel result_log_level =
      playback_result == Mp3PlaybackResult::kCompleted ||
              playback_result == Mp3PlaybackResult::kStopped
          ? LogLevel::kInfo
          : LogLevel::kError;
  LogMessage(result_log_level, __FILE__, __LINE__,
      "MP3 playback finished: result=%s, elapsed=%u.%03us, "
      "decoded_pcm=%llu bytes\n",
      PlaybackResultName(playback_result),
      static_cast<unsigned int>(elapsed_ms / 1000U),
      static_cast<unsigned int>(elapsed_ms % 1000U),
      static_cast<unsigned long long>(decoded_pcm_bytes));
  return playback_result;
}

}  // namespace lilygo_box::audio
