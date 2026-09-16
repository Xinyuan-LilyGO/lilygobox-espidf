/*
 * @Description: T-Display-P4 V2 ES8389 音频硬件实现
 * @Author: LILYGO_L
 * @Date: 2026-08-28 00:00:00
 * @LastEditTime: 2026-09-16 14:38:21
 * @License: GPL 3.0
 */
#include <unistd.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <mutex>

#include "audio/new_notification_010_c2_b16_s44100.h"
#include "base/logger.h"
#include "esp_codec_dev.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "hal/device/common/device_utils.h"
#include "hal/device/t_display_p4/device.h"

namespace lilygo_box::hal {
namespace device = lilygo_device_driver::t_display_p4::device;
namespace {

constexpr uint8_t kAudioVolumeMax = 100;
constexpr size_t kSpeakerPlaybackChunkBytes = 4096;
constexpr uint32_t kSpeakerPlaybackTaskStackBytes = 4 * 1024;
constexpr uint32_t kAudioFilePlaybackTaskStackBytes = 8 * 1024;
constexpr UBaseType_t kSpeakerPlaybackTaskPriority = 3;
constexpr uint32_t kPausedAudioReadyTimeoutMs = 1000;
constexpr uint32_t kPausedAudioReadyPollMs = 10;
constexpr uint32_t kSpeakerPlaybackSampleRateHz = 44100;
constexpr uint8_t kSpeakerPlaybackChannelCount = 2;
constexpr uint8_t kSpeakerPlaybackBitsPerSample = 16;
constexpr uint32_t kMicrophoneCaptureTaskStackBytes = 4 * 1024;
constexpr UBaseType_t kMicrophoneCaptureTaskPriority = 3;
constexpr size_t kMicrophoneReadSampleCount = 128;
constexpr uint32_t kMicrophoneReadRetryDelayMs = 10;
constexpr int kMicrophoneAverageFullScale = 1000;
constexpr int kMicrophonePeakFullScale = 4000;
constexpr int kMicrophoneLevelRiseDivisor = 4;
constexpr int kMicrophoneLevelFallDivisor = 8;
std::atomic<int> g_speaker_volume_percent{100};
std::recursive_mutex g_codec_mutex;
/**
 * @brief 将 PCM 数据写入 ES8389，驱动返回成功时表示整块数据已提交
 * @param data PCM 数据地址
 * @param size 数据字节数
 * @return 音频句柄有效且写入成功时返回 true
 */
bool CodecWrite(const void* data, size_t size) {
  std::lock_guard<std::recursive_mutex> lock(g_codec_mutex);
  auto& driver = lilygo_device_driver::TDisplayP4Driver::GetInstance();
  return driver.es8389_output_codec_dev() != nullptr &&
         esp_codec_dev_write(driver.es8389_output_codec_dev(),
             const_cast<void*>(data), static_cast<int>(size)) == ESP_CODEC_DEV_OK;
}

/**
 * @brief 从 ES8389 读取一块 PCM 数据并转换驱动的状态返回值
 * @param data PCM 接收缓冲区
 * @param size 请求读取的字节数
 * @return 成功时返回完整数据长度，失败时返回 0
 */
size_t CodecRead(void* data, size_t size) {
  std::lock_guard<std::recursive_mutex> lock(g_codec_mutex);
  auto& driver = lilygo_device_driver::TDisplayP4Driver::GetInstance();
  if (driver.es8389_input_codec_dev() == nullptr ||
      esp_codec_dev_read(driver.es8389_input_codec_dev(), data,
          static_cast<int>(size)) != ESP_CODEC_DEV_OK) {
    return 0;
  }
  return size;
}

}  // namespace

bool TDisplayP4Device::PlaySpeakerTone(size_t* bytes_written) {
  if (bytes_written != nullptr) {
    *bytes_written = 0;
  }

  if (!Configure(kSpeakerPlaybackSampleRateHz, kSpeakerPlaybackChannelCount,
          kSpeakerPlaybackBitsPerSample)) {
    LogMessage(
        LogLevel::kWarning, __FILE__, __LINE__, "Es8389 init retry failed\n");
    return false;
  }

  const auto* audio_data = reinterpret_cast<const uint8_t*>(c2_b16_s44100);
  const size_t audio_size = sizeof(c2_b16_s44100);
  speaker_.total_bytes.store(audio_size);
  const size_t frame_size =
      (kSpeakerPlaybackBitsPerSample / 8) * kSpeakerPlaybackChannelCount;
  const size_t duration_ms =
      ((audio_size / frame_size) * 1000U) / kSpeakerPlaybackSampleRateHz;

  LogMessage(LogLevel::kDebug, __FILE__, __LINE__,
      "ES8389 speaker playback: bytes=%u, sample_rate=%u, channels=%u, "
      "duration=%u ms\n",
      static_cast<unsigned int>(audio_size),
      static_cast<unsigned int>(kSpeakerPlaybackSampleRateHz),
      static_cast<unsigned int>(kSpeakerPlaybackChannelCount),
      static_cast<unsigned int>(duration_ms));

  size_t total_written = 0;
  while (total_written < audio_size) {
    const size_t write_size =
        std::min(kSpeakerPlaybackChunkBytes, audio_size - total_written);
    const size_t written =
        CodecWrite(audio_data + total_written, write_size) ? write_size : 0;
    if (written == 0) {
      LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
          "ES8389 WriteI2s failed, written=%u/%u\n",
          static_cast<unsigned int>(total_written),
          static_cast<unsigned int>(audio_size));
      return false;
    }
    total_written += written;
    if (bytes_written != nullptr) {
      *bytes_written = total_written;
    }
    speaker_.bytes_written.store(total_written);
  }

  return true;
}

bool TDisplayP4Device::StartSpeakerTone() {
  if (speaker_.running.load()) {
    return StartPausedAudioSpeakerTone(false);
  }

  if (!TryAcquireAuxiliaryAudioOutput(AuxiliaryAudioOutput::kSpeakerTone)) {
    return false;
  }

  bool expected = false;
  if (!speaker_.running.compare_exchange_strong(expected, true)) {
    ReleaseAuxiliaryAudioOutput(AuxiliaryAudioOutput::kSpeakerTone);
    return false;
  }

  speaker_.completed.store(false);
  speaker_.success.store(false);
  speaker_.bytes_written.store(0);
  speaker_.total_bytes.store(sizeof(c2_b16_s44100));
  speaker_.loop_enabled.store(false);
  speaker_.stop_requested.store(false);
  speaker_.paused.store(false);
  speaker_.playback_kind.store(SpeakerState::PlaybackKind::kTone);

  const BaseType_t result = xTaskCreate(SpeakerPlaybackTaskEntry,
      "speaker_play", kSpeakerPlaybackTaskStackBytes, this,
      kSpeakerPlaybackTaskPriority, nullptr);
  if (result != pdPASS) {
    speaker_.running.store(false);
    speaker_.completed.store(true);
    speaker_.playback_kind.store(SpeakerState::PlaybackKind::kNone);
    ReleaseAuxiliaryAudioOutput(AuxiliaryAudioOutput::kSpeakerTone);
    return false;
  }

  return true;
}

bool TDisplayP4Device::StartSpeakerToneLoop() {
  if (speaker_.tone_overlay_running.load()) {
    return speaker_.tone_overlay_loop_enabled.load();
  }
  if (speaker_.running.load()) {
    if (speaker_.playback_kind.load() ==
        SpeakerState::PlaybackKind::kToneLoop) {
      return true;
    }
    return StartPausedAudioSpeakerTone(true);
  }
  if (!TryAcquireAuxiliaryAudioOutput(AuxiliaryAudioOutput::kSpeakerTone)) {
    return false;
  }
  speaker_.loop_enabled.store(true);
  speaker_.stop_requested.store(false);

  bool expected = false;
  if (!speaker_.running.compare_exchange_strong(expected, true)) {
    ReleaseAuxiliaryAudioOutput(AuxiliaryAudioOutput::kSpeakerTone);
    return speaker_.playback_kind.load() ==
           SpeakerState::PlaybackKind::kToneLoop;
  }

  speaker_.completed.store(false);
  speaker_.success.store(false);
  speaker_.bytes_written.store(0);
  speaker_.total_bytes.store(sizeof(c2_b16_s44100));
  speaker_.paused.store(false);
  speaker_.playback_kind.store(SpeakerState::PlaybackKind::kToneLoop);

  const BaseType_t result = xTaskCreate(SpeakerPlaybackTaskEntry,
      "speaker_loop", kSpeakerPlaybackTaskStackBytes, this,
      kSpeakerPlaybackTaskPriority, nullptr);
  if (result != pdPASS) {
    speaker_.running.store(false);
    speaker_.completed.store(true);
    speaker_.loop_enabled.store(false);
    speaker_.playback_kind.store(SpeakerState::PlaybackKind::kNone);
    ReleaseAuxiliaryAudioOutput(AuxiliaryAudioOutput::kSpeakerTone);
    return false;
  }

  return true;
}

bool TDisplayP4Device::StopSpeakerToneLoop() {
  if (speaker_.tone_overlay_running.load() &&
      speaker_.tone_overlay_loop_enabled.load()) {
    speaker_.tone_overlay_stop_requested.store(true);
    speaker_.tone_overlay_loop_enabled.store(false);
    return true;
  }
  if (speaker_.playback_kind.load() != SpeakerState::PlaybackKind::kToneLoop) {
    return false;
  }
  speaker_.stop_requested.store(true);
  speaker_.loop_enabled.store(false);
  return true;
}

bool TDisplayP4Device::SetSpeakerVolumePercent(int percent) {
  std::lock_guard<std::recursive_mutex> lock(g_codec_mutex);
  if (!driver_.IsEs8389Ready() && !driver_.InitEs8389()) {
    LogMessage(LogLevel::kWarning, __FILE__, __LINE__, "Es8389 init failed\n");
    return false;
  }

  const uint8_t volume =
      device_utils::PercentToUint8Value(percent, kAudioVolumeMax);
  const bool result =
      esp_codec_dev_set_out_vol(driver_.es8389_output_codec_dev(), volume) ==
      ESP_CODEC_DEV_OK;
  if (result) {
    g_speaker_volume_percent.store(volume);
  }
  return result;
}

bool TDisplayP4Device::ReadSpeakerToneStatus(SpeakerStatus* status) {
  if (status == nullptr) {
    return false;
  }

  const SpeakerState::PlaybackKind playback_kind =
      speaker_.playback_kind.load();
  const bool audio_file_running =
      playback_kind == SpeakerState::PlaybackKind::kAudioFile;
  status->running = speaker_.tone_overlay_running.load() ||
                    (speaker_.running.load() && !audio_file_running);
  status->completed = speaker_.completed.load();
  status->success = speaker_.success.load();
  status->bytes_written = speaker_.bytes_written.load();
  status->total_bytes = speaker_.total_bytes.load();
  return true;
}

bool TDisplayP4Device::StartAudioFile(const char* path, uint32_t duration_ms) {
  if (path == nullptr || path[0] == '\0') {
    return false;
  }
  const AuxiliaryAudioOutput auxiliary_output =
      speaker_.auxiliary_output.load();
  if (auxiliary_output == AuxiliaryAudioOutput::kMicrophoneLoopback ||
      speaker_.tone_overlay_running.load()) {
    return false;
  }
  if (speaker_.running.load()) {
    speaker_.stop_requested.store(true);
    speaker_.paused.store(false);
    for (int retry = 0; retry < 100 && speaker_.running.load(); ++retry) {
      vTaskDelay(pdMS_TO_TICKS(10));
    }
    if (speaker_.running.load()) {
      return false;
    }
  }
  if (speaker_.auxiliary_output.load() != AuxiliaryAudioOutput::kNone) {
    return false;
  }

  bool expected = false;
  if (!speaker_.running.compare_exchange_strong(expected, true)) {
    return false;
  }
  if (speaker_.auxiliary_output.load() != AuxiliaryAudioOutput::kNone) {
    speaker_.running.store(false);
    return false;
  }
  std::snprintf(
      speaker_.audio_file_path, sizeof(speaker_.audio_file_path), "%s", path);
  speaker_.loop_enabled.store(false);
  speaker_.stop_requested.store(false);
  speaker_.paused.store(false);
  speaker_.elapsed_ms.store(0);
  speaker_.duration_ms.store(duration_ms);
  speaker_.seek_requested.store(false);
  speaker_.seek_position_ms.store(0);
  speaker_.file_state.store(AudioFilePlaybackState::kPlaying);
  speaker_.playback_kind.store(SpeakerState::PlaybackKind::kAudioFile);

  const BaseType_t result = xTaskCreate(SpeakerPlaybackTaskEntry, "audio_file",
      kAudioFilePlaybackTaskStackBytes, this, kSpeakerPlaybackTaskPriority,
      nullptr);
  if (result != pdPASS) {
    speaker_.running.store(false);
    speaker_.file_state.store(AudioFilePlaybackState::kError);
    speaker_.playback_kind.store(SpeakerState::PlaybackKind::kNone);
    return false;
  }
  return true;
}

bool TDisplayP4Device::PauseAudioFile() {
  if (!speaker_.running.load() ||
      speaker_.playback_kind.load() != SpeakerState::PlaybackKind::kAudioFile ||
      speaker_.file_state.load() != AudioFilePlaybackState::kPlaying) {
    return false;
  }
  speaker_.pause_acknowledged.store(false);
  speaker_.paused.store(true);
  speaker_.file_state.store(AudioFilePlaybackState::kPaused);
  return true;
}

bool TDisplayP4Device::ResumeAudioFile() {
  if (!speaker_.running.load() ||
      speaker_.playback_kind.load() != SpeakerState::PlaybackKind::kAudioFile ||
      speaker_.auxiliary_output.load() != AuxiliaryAudioOutput::kNone ||
      speaker_.file_state.load() != AudioFilePlaybackState::kPaused) {
    return false;
  }
  speaker_.paused.store(false);
  speaker_.file_state.store(AudioFilePlaybackState::kPlaying);
  return true;
}

bool TDisplayP4Device::SeekAudioFile(uint32_t position_ms) {
  if (!speaker_.running.load() ||
      speaker_.playback_kind.load() != SpeakerState::PlaybackKind::kAudioFile) {
    return false;
  }
  const uint32_t duration_ms = speaker_.duration_ms.load();
  if (duration_ms == 0) {
    return false;
  }
  const uint32_t clamped_position_ms = std::min(position_ms, duration_ms);
  speaker_.seek_position_ms.store(clamped_position_ms);
  speaker_.seek_requested.store(true);
  speaker_.elapsed_ms.store(clamped_position_ms);
  return true;
}

bool TDisplayP4Device::StopAudioFile() {
  if (speaker_.playback_kind.load() != SpeakerState::PlaybackKind::kAudioFile) {
    return false;
  }
  speaker_.stop_requested.store(true);
  if (!speaker_.tone_overlay_running.load()) {
    speaker_.paused.store(false);
  }
  speaker_.file_state.store(AudioFilePlaybackState::kStopped);
  return true;
}

bool TDisplayP4Device::ReadAudioFileStatus(AudioFilePlaybackStatus* status) {
  if (status == nullptr) {
    return false;
  }
  status->state = speaker_.file_state.load();
  status->elapsed_ms = speaker_.elapsed_ms.load();
  status->duration_ms = speaker_.duration_ms.load();
  return true;
}

void TDisplayP4Device::SpeakerPlaybackTaskEntry(void* context) {
  auto* self = static_cast<TDisplayP4Device*>(context);
  if (self != nullptr) {
    self->RunSpeakerPlaybackTask();
  }
  vTaskDelete(nullptr);
}

void TDisplayP4Device::PausedAudioSpeakerToneTaskEntry(void* context) {
  auto* self = static_cast<TDisplayP4Device*>(context);
  if (self != nullptr) {
    self->RunPausedAudioSpeakerToneTask();
  }
  vTaskDelete(nullptr);
}

void TDisplayP4Device::RunSpeakerPlaybackTask() {
  if (speaker_.playback_kind.load() == SpeakerState::PlaybackKind::kAudioFile) {
    const audio::Mp3PlaybackResult result =
        audio::PlayMp3File(speaker_.audio_file_path, this);
    const bool completed = result == audio::Mp3PlaybackResult::kCompleted;
    const bool stopped = result == audio::Mp3PlaybackResult::kStopped;
    speaker_.paused.store(false);
    speaker_.stop_requested.store(false);
    speaker_.seek_requested.store(false);
    speaker_.file_state.store(completed
                                  ? AudioFilePlaybackState::kCompleted
                                  : (stopped ? AudioFilePlaybackState::kStopped
                                             : AudioFilePlaybackState::kError));
    speaker_.playback_kind.store(SpeakerState::PlaybackKind::kNone);
    speaker_.running.store(false);
    UpdateAudioCodecOperatingMode();
    return;
  }

  size_t bytes_written = 0;
  bool played = false;
  do {
    size_t current_written = 0;
    played = PlaySpeakerTone(&current_written) || played;
    bytes_written += current_written;
    speaker_.bytes_written.store(bytes_written);
  } while (speaker_.loop_enabled.load() && !speaker_.stop_requested.load());
  speaker_.success.store(played);
  speaker_.completed.store(true);
  speaker_.loop_enabled.store(false);
  speaker_.stop_requested.store(false);
  speaker_.playback_kind.store(SpeakerState::PlaybackKind::kNone);
  ReleaseAuxiliaryAudioOutput(AuxiliaryAudioOutput::kSpeakerTone);
  speaker_.running.store(false);
  UpdateAudioCodecOperatingMode();
}

bool TDisplayP4Device::StartPausedAudioSpeakerTone(bool loop_enabled) {
  if (speaker_.playback_kind.load() != SpeakerState::PlaybackKind::kAudioFile ||
      speaker_.file_state.load() != AudioFilePlaybackState::kPaused) {
    return false;
  }

  if (!TryAcquireAuxiliaryAudioOutput(AuxiliaryAudioOutput::kSpeakerTone)) {
    return false;
  }

  bool expected = false;
  if (!speaker_.tone_overlay_running.compare_exchange_strong(expected, true)) {
    ReleaseAuxiliaryAudioOutput(AuxiliaryAudioOutput::kSpeakerTone);
    return false;
  }
  speaker_.tone_overlay_loop_enabled.store(loop_enabled);
  speaker_.tone_overlay_stop_requested.store(false);
  speaker_.completed.store(false);
  speaker_.success.store(false);
  speaker_.bytes_written.store(0);
  speaker_.total_bytes.store(sizeof(c2_b16_s44100));

  const BaseType_t result = xTaskCreate(PausedAudioSpeakerToneTaskEntry,
      "speaker_overlay", kSpeakerPlaybackTaskStackBytes, this,
      kSpeakerPlaybackTaskPriority, nullptr);
  if (result != pdPASS) {
    speaker_.tone_overlay_loop_enabled.store(false);
    speaker_.tone_overlay_running.store(false);
    speaker_.completed.store(true);
    ReleaseAuxiliaryAudioOutput(AuxiliaryAudioOutput::kSpeakerTone);
    return false;
  }
  return true;
}

void TDisplayP4Device::RunPausedAudioSpeakerToneTask() {
  const bool pause_ready = WaitForPausedAudioFile();

  const uint32_t paused_sample_rate_hz = speaker_.sample_rate_hz.load();
  size_t total_bytes_written = 0;
  bool played = false;
  if (pause_ready && !speaker_.tone_overlay_stop_requested.load() &&
      !speaker_.stop_requested.load()) {
    do {
      size_t bytes_written = 0;
      played = PlaySpeakerTone(&bytes_written) || played;
      total_bytes_written += bytes_written;
      speaker_.bytes_written.store(total_bytes_written);
    } while (speaker_.tone_overlay_loop_enabled.load() &&
             !speaker_.tone_overlay_stop_requested.load() &&
             !speaker_.stop_requested.load());
  }
  const bool output_restored =
      !pause_ready ||
      Configure(paused_sample_rate_hz, kSpeakerPlaybackChannelCount,
          kSpeakerPlaybackBitsPerSample);
  speaker_.bytes_written.store(total_bytes_written);
  speaker_.success.store(played && output_restored);
  speaker_.completed.store(true);
  speaker_.tone_overlay_loop_enabled.store(false);
  speaker_.tone_overlay_stop_requested.store(false);
  speaker_.tone_overlay_running.store(false);
  ReleaseAuxiliaryAudioOutput(AuxiliaryAudioOutput::kSpeakerTone);
  if (speaker_.stop_requested.load()) {
    speaker_.paused.store(false);
  }
}

bool TDisplayP4Device::TryAcquireAuxiliaryAudioOutput(
    AuxiliaryAudioOutput output) {
  AuxiliaryAudioOutput expected = AuxiliaryAudioOutput::kNone;
  return output != AuxiliaryAudioOutput::kNone &&
         speaker_.auxiliary_output.compare_exchange_strong(expected, output);
}

void TDisplayP4Device::ReleaseAuxiliaryAudioOutput(
    AuxiliaryAudioOutput output) {
  speaker_.auxiliary_output.compare_exchange_strong(
      output, AuxiliaryAudioOutput::kNone);
}

bool TDisplayP4Device::WaitForPausedAudioFile() {
  for (uint32_t elapsed_ms = 0; elapsed_ms < kPausedAudioReadyTimeoutMs;
      elapsed_ms += kPausedAudioReadyPollMs) {
    if (!speaker_.running.load() || !speaker_.paused.load() ||
        speaker_.stop_requested.load() ||
        speaker_.playback_kind.load() !=
            SpeakerState::PlaybackKind::kAudioFile ||
        speaker_.file_state.load() != AudioFilePlaybackState::kPaused) {
      return false;
    }
    if (speaker_.pause_acknowledged.load()) {
      return true;
    }
    vTaskDelay(pdMS_TO_TICKS(kPausedAudioReadyPollMs));
  }
  return speaker_.running.load() && speaker_.paused.load() &&
         !speaker_.stop_requested.load() &&
         speaker_.playback_kind.load() ==
             SpeakerState::PlaybackKind::kAudioFile &&
         speaker_.file_state.load() == AudioFilePlaybackState::kPaused &&
         speaker_.pause_acknowledged.load();
}

bool TDisplayP4Device::UpdateAudioCodecOperatingMode() {
  std::lock_guard<std::recursive_mutex> lock(g_codec_mutex);
  using OperatingMode =
      lilygo_device_driver::TDisplayP4Driver::Es8389OperatingMode;
  if (!driver_.IsEs8389Ready() && !driver_.InitEs8389()) {
    return false;
  }
  const bool active = speaker_.running.load() || microphone_.running.load();
  const bool result = driver_.SetEs8389OperatingMode(
      active ? OperatingMode::kActive : OperatingMode::kSleep);
  if (!active) {
    // 休眠会关闭 codec 句柄，唤醒后必须重新应用当前采样率。
    speaker_.sample_rate_hz.store(0);
  } else if (result && driver_.es8389_output_codec_dev() != nullptr) {
    if (esp_codec_dev_set_out_vol(driver_.es8389_output_codec_dev(),
            g_speaker_volume_percent.load()) != ESP_CODEC_DEV_OK) {
      return false;
    }
  }
  return result;
}

bool TDisplayP4Device::Configure(
    uint32_t sample_rate_hz, uint8_t channel_count, uint8_t bits_per_sample) {
  std::lock_guard<std::recursive_mutex> lock(g_codec_mutex);
  if ((channel_count != 1 && channel_count != 2) ||
      bits_per_sample != kSpeakerPlaybackBitsPerSample || sample_rate_hz == 0) {
    return false;
  }
  if (!driver_.IsEs8389Ready() && !driver_.InitEs8389()) {
    return false;
  }
  if (!UpdateAudioCodecOperatingMode()) {
    return false;
  }
  if (speaker_.sample_rate_hz.load() == sample_rate_hz) {
    return true;
  }
  esp_codec_dev_sample_info_t sample_info = {
      .bits_per_sample = bits_per_sample,
      // MP3 解码器在写入 PcmOutput 前会把单声道展开为双声道。
      .channel = kSpeakerPlaybackChannelCount,
      .channel_mask = ESP_CODEC_DEV_MAKE_CHANNEL_MASK(0) |
                      ESP_CODEC_DEV_MAKE_CHANNEL_MASK(1),
      .sample_rate = sample_rate_hz,
      .mclk_multiple = device::es8389::kMclkMultiple,
  };
  auto output = driver_.es8389_output_codec_dev();
  auto input = driver_.es8389_input_codec_dev();
  const auto fail_configuration = [this]() {
    speaker_.sample_rate_hz.store(0);
    driver_.DeinitEs8389();
    return false;
  };
  if (output == nullptr || input == nullptr) {
    return fail_configuration();
  }
  if (esp_codec_dev_close(output) != ESP_CODEC_DEV_OK) {
    return fail_configuration();
  }
  if (esp_codec_dev_close(input) != ESP_CODEC_DEV_OK) {
    // 输出句柄已经关闭，尽力恢复驱动初始化时的默认采样率。
    esp_codec_dev_sample_info_t default_info = {
        .bits_per_sample = kSpeakerPlaybackBitsPerSample,
        .channel = kSpeakerPlaybackChannelCount,
        .channel_mask = ESP_CODEC_DEV_MAKE_CHANNEL_MASK(0) |
                        ESP_CODEC_DEV_MAKE_CHANNEL_MASK(1),
        .sample_rate = device::es8389::kSampleRate,
        .mclk_multiple = device::es8389::kMclkMultiple,
    };
    esp_codec_dev_open(output, &default_info);
    return fail_configuration();
  }
  if (esp_codec_dev_open(output, &sample_info) != ESP_CODEC_DEV_OK) {
    return fail_configuration();
  }
  if (esp_codec_dev_open(input, &sample_info) != ESP_CODEC_DEV_OK) {
    return fail_configuration();
  }
  speaker_.sample_rate_hz.store(sample_rate_hz);
  return true;
}

bool TDisplayP4Device::WaitUntilReady() {
  if (speaker_.paused.load() && !speaker_.stop_requested.load()) {
    speaker_.pause_acknowledged.store(true);
  }
  while (speaker_.paused.load() && !speaker_.stop_requested.load()) {
    vTaskDelay(pdMS_TO_TICKS(20));
  }
  speaker_.pause_acknowledged.store(false);
  return !speaker_.stop_requested.load();
}

bool TDisplayP4Device::TakeSeekRequest(uint32_t* position_ms) {
  if (position_ms == nullptr || !speaker_.seek_requested.exchange(false)) {
    return false;
  }
  *position_ms = speaker_.seek_position_ms.load();
  return true;
}

bool TDisplayP4Device::Write(const uint8_t* data, size_t size) {
  if (data == nullptr || size == 0 || !driver_.IsEs8389Ready()) {
    return false;
  }
  size_t total_written = 0;
  while (total_written < size) {
    if (!WaitUntilReady()) {
      return false;
    }
    const size_t write_size =
        std::min(kSpeakerPlaybackChunkBytes, size - total_written);
    const size_t written =
        CodecWrite(data + total_written, write_size) ? write_size : 0;
    if (written == 0) {
      return false;
    }
    total_written += written;
  }
  return true;
}

void TDisplayP4Device::UpdateProgress(uint32_t elapsed_ms) {
  const uint32_t duration_ms = speaker_.duration_ms.load();
  speaker_.elapsed_ms.store(
      duration_ms == 0 ? elapsed_ms : std::min(elapsed_ms, duration_ms));
}

bool TDisplayP4Device::StartMicrophone() {
  bool expected = false;
  if (!microphone_.running.compare_exchange_strong(expected, true)) {
    return !microphone_.stop_requested.load();
  }

  microphone_.stop_requested.store(false);
  microphone_.level_percent.store(0);
  microphone_.peak_sample.store(0);
  microphone_.bytes_read.store(0);
  if (!SetAudioAdcToDac(false)) {
    LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
        "Failed to activate the ES8389 microphone capture path\n");
    microphone_.running.store(false);
    UpdateAudioCodecOperatingMode();
    return false;
  }

  const BaseType_t result = xTaskCreate(MicrophoneCaptureTaskEntry,
      "mic_capture", kMicrophoneCaptureTaskStackBytes, this,
      kMicrophoneCaptureTaskPriority, nullptr);
  if (result != pdPASS) {
    microphone_.running.store(false);
    microphone_.stop_requested.store(true);
    UpdateAudioCodecOperatingMode();
    return false;
  }

  return true;
}

bool TDisplayP4Device::StopMicrophone() {
  microphone_.stop_requested.store(true);
  microphone_.level_percent.store(0);
  microphone_.peak_sample.store(0);
  if (!driver_.IsEs8389Ready()) {
    microphone_.adc_to_dac_enabled.store(false);
    ReleaseAuxiliaryAudioOutput(AuxiliaryAudioOutput::kMicrophoneLoopback);
    return true;
  }
  return SetAudioAdcToDac(false);
}

bool TDisplayP4Device::SetAudioAdcToDac(bool enable) {
  if (enable && microphone_.adc_to_dac_enabled.load()) {
    return true;
  }
  if (enable && !microphone_.running.load()) {
    return false;
  }

  if (enable) {
    if (!TryAcquireAuxiliaryAudioOutput(
            AuxiliaryAudioOutput::kMicrophoneLoopback)) {
      return false;
    }
    if (speaker_.running.load() && !WaitForPausedAudioFile()) {
      ReleaseAuxiliaryAudioOutput(AuxiliaryAudioOutput::kMicrophoneLoopback);
      return false;
    }
  }

  const bool previous_enabled = microphone_.adc_to_dac_enabled.exchange(enable);
  if (!UpdateAudioCodecOperatingMode() || !driver_.IsEs8389Ready()) {
    microphone_.adc_to_dac_enabled.store(previous_enabled);
    if (enable) {
      ReleaseAuxiliaryAudioOutput(AuxiliaryAudioOutput::kMicrophoneLoopback);
    }
    UpdateAudioCodecOperatingMode();
    return false;
  }

  if (!enable) {
    ReleaseAuxiliaryAudioOutput(AuxiliaryAudioOutput::kMicrophoneLoopback);
  }

  return true;
}

bool TDisplayP4Device::ReadMicrophoneStatus(MicrophoneStatus* status) {
  if (status == nullptr) {
    return false;
  }

  status->running = microphone_.running.load();
  status->adc_to_dac_enabled = microphone_.adc_to_dac_enabled.load();
  status->level_percent = microphone_.level_percent.load();
  status->peak_sample = microphone_.peak_sample.load();
  status->bytes_read = microphone_.bytes_read.load();
  return true;
}

void TDisplayP4Device::MicrophoneCaptureTaskEntry(void* context) {
  auto* self = static_cast<TDisplayP4Device*>(context);
  if (self != nullptr) {
    self->RunMicrophoneCaptureTask();
  }
  vTaskDelete(nullptr);
}

void TDisplayP4Device::RunMicrophoneCaptureTask() {
  std::array<int16_t, kMicrophoneReadSampleCount> samples = {};
  while (!microphone_.stop_requested.load()) {
    const size_t read_bytes =
        CodecRead(samples.data(), samples.size() * sizeof(samples[0]));
    if (read_bytes > 0) {
      microphone_.bytes_read.fetch_add(read_bytes);

      int peak_sample = 0;
      int64_t absolute_sum = 0;
      const size_t sample_count = read_bytes / sizeof(samples[0]);
      for (size_t i = 0; i < sample_count && i < samples.size(); ++i) {
        const int sample = samples[i];
        const int absolute_sample = sample < 0 ? -sample : sample;
        absolute_sum += absolute_sample;
        peak_sample = std::max(peak_sample, absolute_sample);
      }

      const int average_sample =
          sample_count == 0 ? 0 : absolute_sum / static_cast<int>(sample_count);
      const int average_level_percent =
          std::min(100, (average_sample * 100) / kMicrophoneAverageFullScale);
      const int peak_level_percent =
          std::min(100, (peak_sample * 100) / kMicrophonePeakFullScale);
      const int target_level_percent =
          std::max(average_level_percent, peak_level_percent);
      const int current_level_percent = microphone_.level_percent.load();
      const int difference = target_level_percent - current_level_percent;
      const int divisor = difference > 0 ? kMicrophoneLevelRiseDivisor
                                         : kMicrophoneLevelFallDivisor;
      int level_percent = current_level_percent + difference / divisor;
      if (level_percent == current_level_percent && difference != 0) {
        level_percent += difference > 0 ? 1 : -1;
      }
      microphone_.peak_sample.store(peak_sample);
      microphone_.level_percent.store(level_percent);

      if (microphone_.adc_to_dac_enabled.load()) {
        const auto* pcm_data = reinterpret_cast<const uint8_t*>(samples.data());
        size_t written_bytes = 0;
        while (written_bytes < read_bytes &&
               microphone_.adc_to_dac_enabled.load() &&
               !microphone_.stop_requested.load()) {
          const size_t written = CodecWrite(
              pcm_data + written_bytes, read_bytes - written_bytes)
                                    ? read_bytes - written_bytes
                                    : 0;
          if (written == 0) {
            LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
                "ES8389 microphone PCM loopback write failed\n");
            microphone_.adc_to_dac_enabled.store(false);
            ReleaseAuxiliaryAudioOutput(
                AuxiliaryAudioOutput::kMicrophoneLoopback);
            UpdateAudioCodecOperatingMode();
            break;
          }
          written_bytes += written;
        }
      }
    } else {
      vTaskDelay(pdMS_TO_TICKS(kMicrophoneReadRetryDelayMs));
    }
  }

  microphone_.adc_to_dac_enabled.store(false);
  ReleaseAuxiliaryAudioOutput(AuxiliaryAudioOutput::kMicrophoneLoopback);
  microphone_.level_percent.store(0);
  microphone_.peak_sample.store(0);
  microphone_.running.store(false);
  UpdateAudioCodecOperatingMode();
}

}  // namespace lilygo_box::hal
