/*
 * @Description: 共用后台音量调节，避免 UI 等待音频锁、I2C 和 I2S。
 * @Author: LILYGO_L
 * @Date: 2026-10-12 00:00:00
 * @LastEditTime: 2026-10-13 12:00:00
 * @License: GPL 3.0
 */
#include "hal/providers/audio_provider.h"

#include <algorithm>
#include <mutex>

#include "base/logger.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

namespace lilygo_box::hal {
namespace {

struct VolumeRequest {
  AudioProvider* audio = nullptr;
  int percent = 0;
  bool preview = false;
  uint32_t generation = 0;
  std::optional<bool> enabled;
};

QueueHandle_t g_volume_requests = nullptr;
std::mutex g_volume_request_mutex;
uint32_t g_volume_request_generation = 0;

/**
 * @brief 合并连续音量请求并在后台执行硬件调节和试听控制
 * @param context 单元素请求队列；设备 Provider 在应用运行期间保持有效
 */
void VolumeControlTask(void* context) {
  auto queue = static_cast<QueueHandle_t>(context);
  VolumeRequest request;
  while (true) {
    if (xQueueReceive(queue, &request, portMAX_DELAY) != pdTRUE) continue;
    // 小窗口合并高频滑动事件，停止试听请求也覆盖尚未执行的开始请求。
    vTaskDelay(pdMS_TO_TICKS(20));
    VolumeRequest latest;
    while (xQueueReceive(queue, &latest, 0) == pdTRUE) request = latest;
    if (!request.preview) request.audio->StopSpeakerToneLoop();
    if (!request.audio->ApplyRequestedSpeakerVolume(
            request.percent, request.generation, request.enabled)) {
      LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
          "Apply queued speaker volume failed: percent=%d\n", request.percent);
    }
    // 硬件调用期间可能已松手或提交新值；旧请求不得再次启动试听。
    // 启停接口只更新标志/创建播放任务，不在此锁内执行 codec 或 PCM 操作。
    std::lock_guard<std::mutex> lock(g_volume_request_mutex);
    if (request.preview && request.audio->SpeakerEnabled() &&
        request.generation == g_volume_request_generation) {
      request.audio->StartSpeakerToneLoop();
    }
  }
}

}  // namespace

bool AudioProvider::ApplySpeakerVolumeSetting(int percent, bool enabled) {
  std::lock_guard<std::mutex> lock(speaker_setting_mutex_);
  {
    // 远程设置替代尚未执行的本地请求，避免旧滑动值在确认后覆盖新目标。
    std::lock_guard<std::mutex> request_lock(g_volume_request_mutex);
    ++g_volume_request_generation;
    if (g_volume_requests != nullptr) xQueueReset(g_volume_requests);
    StopSpeakerToneLoop();
  }
  if (!SetSpeakerVolumePercent(enabled ? std::clamp(percent, 0, 100) : 0)) {
    return false;
  }
  speaker_enabled_.store(enabled);
  return true;
}

bool AudioProvider::ApplyRequestedSpeakerVolume(int percent,
                                                uint32_t generation,
                                                std::optional<bool> enabled) {
  std::lock_guard<std::mutex> lock(speaker_setting_mutex_);
  {
    std::lock_guard<std::mutex> request_lock(g_volume_request_mutex);
    if (generation != g_volume_request_generation) return true;
  }
  const bool output_enabled = enabled.value_or(speaker_enabled_.load());
  if (!SetSpeakerVolumePercent(output_enabled ? std::clamp(percent, 0, 100)
                                              : 0)) {
    return false;
  }
  speaker_enabled_.store(output_enabled);
  return true;
}

bool AudioProvider::RequestSpeakerVolumePercent(int percent, bool preview,
                                                std::optional<bool> enabled) {
  // 该锁仅保护队列创建与投递；后台硬件处理不会持有它。
  std::lock_guard<std::mutex> lock(g_volume_request_mutex);
  if (g_volume_requests == nullptr) {
    auto queue = xQueueCreate(1, sizeof(VolumeRequest));
    if (queue == nullptr) return false;
    if (xTaskCreate(VolumeControlTask, "volume_control", 4096, queue, 3,
            nullptr) != pdPASS) {
      vQueueDelete(queue);
      return false;
    }
    g_volume_requests = queue;
  }
  const VolumeRequest request{this, std::clamp(percent, 0, 100), preview,
                              g_volume_request_generation + 1, enabled};
  if (xQueueOverwrite(g_volume_requests, &request) != pdTRUE) return false;
  g_volume_request_generation = request.generation;
  // 与后台开始试听使用同一短锁，保证松手后的停止不会被旧请求覆盖。
  if (!preview) StopSpeakerToneLoop();
  return true;
}

}  // namespace lilygo_box::hal
