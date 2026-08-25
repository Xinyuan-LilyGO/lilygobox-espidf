/*
 * @Description: T-Display-P4 设备初始化与硬件 Provider 适配实现
 * @Author: LILYGO_L
 * @Date: 2026-05-10 13:27:05
 * @LastEditTime: 2026-08-22 16:00:46
 * @License: GPL 3.0
 */
#include "hal/device/t_display_p4/t_display_p4_device.h"

#include <cerrno>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <iterator>
#include <memory>
#include <new>
#include <string>

#include "app/diagnostics/camera_error.h"
#include "app/storage/display_storage.h"
#include "audio/new_notification_010_c2_b16_s44100.h"
#include "base/logger.h"
#include "driver/i2c_master.h"
#include "esp_err.h"
#include "esp_eth.h"
#include "esp_eth_mac.h"
#include "esp_eth_phy_802_3.h"
#include "esp_event.h"
#include "esp_heap_caps.h"
#include "esp_hosted.h"
#include "esp_hosted_transport_config.h"
#include "esp_lcd_mipi_dsi.h"
#include "esp_netif.h"
#include "esp_sntp.h"
#include "esp_timer.h"
#include "esp_video_device.h"
#include "esp_video_init.h"
#include "esp_video_ioctl.h"
#include "esp_wifi.h"
#include "esp_wifi_default.h"
#include "esp_wifi_remote.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "examples/radio_hal/lr20xx_pa_pwr_cfg.h"
#include "linux/videodev2.h"

namespace lilygo_box::hal {
namespace device = lilygo_device_driver::t_display_p4::device;
namespace gpio = lilygo_device_driver::t_display_p4::gpio;
namespace keyboard_device =
    lilygo_device_driver::t_display_p4::keyboard_expansion::device;
namespace keyboard_gpio =
    lilygo_device_driver::t_display_p4::keyboard_expansion::gpio;
namespace {

using DriverKeyboardExpansionLed =
    lilygo_device_driver::TDisplayP4Driver::KeyboardExpansionLed;

constexpr int kScreenBrightnessMinPercent = 0;
constexpr int kScreenBrightnessMaxPercent = 100;
constexpr int kKeyboardBacklightBrightnessMinPercent = 0;
constexpr int kKeyboardBacklightBrightnessMaxPercent = 100;
// 键盘背光 PWM 只使用 0~500/1000，避免负载过大影响系统稳定性。
constexpr uint32_t kKeyboardBacklightDutyMax = 500;
constexpr int kHi8561BrightnessInputMinPercent = 10;
constexpr uint32_t kPt4103DutyScale = 1000;
constexpr uint32_t kSy7200aDutyScale = 1000;
constexpr uint32_t kScreenBrightnessFadeUpdateMs = 10;
constexpr uint8_t kRm69a10BrightnessMax = UINT8_MAX;
constexpr uint8_t kVibrationTestGain = 255;
constexpr uint8_t kVibrationTestLoopCount = 1;
constexpr uint8_t kAudioVolumeMax = 192;
constexpr uint8_t kHapticStrengthMax = UINT8_MAX;
constexpr uint32_t kVibrationTestPlayMs = 220;
constexpr uint32_t kVibrationPreviewPlayMs = 10;
constexpr uint32_t kVibrationPreviewMinIntervalMs = 45;
constexpr uint32_t kVibrationTestStopMs = 180;
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
constexpr uint32_t kCameraPreviewTaskStackBytes = 6 * 1024;
constexpr UBaseType_t kCameraPreviewTaskPriority = 5;
constexpr uint32_t kCameraBufferCount = 2;
constexpr uint32_t kCameraFrameIntervalMs = 10;
constexpr uint32_t kCameraStopWaitTimeoutMs = 5000;
constexpr uint32_t kCameraSensorReadyPollIntervalMs = 100;
constexpr uint32_t kCameraStartupTimeoutMs = 3000;
constexpr uint32_t kCameraPowerCycleOffDelayMs = 20;
constexpr uint32_t kCameraOutputClearFrameCount = 3;
constexpr uint32_t kCameraWarmupFrameCount = 5;
constexpr uint32_t kCameraVideoInitFlags =
    ESP_VIDEO_INIT_FLAGS_MIPI_CSI | ESP_VIDEO_INIT_FLAGS_ISP;
static_assert(kCameraStartupTimeoutMs >= kCameraSensorReadyPollIntervalMs);
#if defined(CONFIG_LILYGO_DEVICE_DRIVER_CAMERA_TYPE_SC2336)
constexpr uint16_t kCameraSensorI2cAddress = 0x30;
#elif defined(CONFIG_LILYGO_DEVICE_DRIVER_CAMERA_TYPE_OV2710)
constexpr uint16_t kCameraSensorI2cAddress = 0x36;
#elif defined(CONFIG_LILYGO_DEVICE_DRIVER_CAMERA_TYPE_OV5645)
constexpr uint16_t kCameraSensorI2cAddress = 0x3C;
#else
#error "Unsupported camera sensor type"
#endif

/**
 * @brief 将键盘扩展硬件键值转换为通用键盘键值
 * @param key_code 键盘扩展硬件键值
 * @param shift_pressed Shift 当前是否按下
 * @return 通用键盘键值
 */
KeyboardKey ToKeyboardKey(
    keyboard_device::tca8418::KeyCode key_code, bool shift_pressed) {
  using KeyCode = keyboard_device::tca8418::KeyCode;
  switch (key_code) {
    case KeyCode::kCharacter:
      return KeyboardKey::kCharacter;
    case KeyCode::kEscape:
      return KeyboardKey::kEscape;
    case KeyCode::kBackspace:
      return KeyboardKey::kBackspace;
    case KeyCode::kEnter:
      return shift_pressed ? KeyboardKey::kLineBreak : KeyboardKey::kEnter;
    case KeyCode::kTab:
      return shift_pressed ? KeyboardKey::kPrevious : KeyboardKey::kNext;
    case KeyCode::kUp:
      return KeyboardKey::kUp;
    case KeyCode::kDown:
      return KeyboardKey::kDown;
    case KeyCode::kLeft:
      return KeyboardKey::kLeft;
    case KeyCode::kRight:
      return KeyboardKey::kRight;
    case KeyCode::kCapsLock:
      return KeyboardKey::kCapsLock;
    case KeyCode::kShift:
      return KeyboardKey::kShift;
    case KeyCode::kControl:
      return KeyboardKey::kControl;
    case KeyCode::kAlt:
      return KeyboardKey::kAlt;
    case KeyCode::kMeta:
      return KeyboardKey::kMeta;
    case KeyCode::kFunction:
      return KeyboardKey::kFunction;
    case KeyCode::kRecord:
      return KeyboardKey::kRecord;
    case KeyCode::kF1:
      return KeyboardKey::kF1;
    case KeyCode::kF2:
      return KeyboardKey::kF2;
    case KeyCode::kF3:
      return KeyboardKey::kF3;
    case KeyCode::kF4:
      return KeyboardKey::kF4;
    case KeyCode::kF5:
      return KeyboardKey::kF5;
    case KeyCode::kF6:
      return KeyboardKey::kF6;
    case KeyCode::kF7:
      return KeyboardKey::kF7;
    case KeyCode::kF8:
      return KeyboardKey::kF8;
    case KeyCode::kF9:
      return KeyboardKey::kF9;
    case KeyCode::kF10:
      return KeyboardKey::kF10;
    case KeyCode::kF11:
      return KeyboardKey::kF11;
    case KeyCode::kUnknown:
    default:
      return KeyboardKey::kUnknown;
  }
}

/**
 * @brief 根据 Fn、Shift 和 Caps Lock 状态解析实体键盘字符
 * @param mapping 实体键盘硬件映射
 * @param function_pressed Fn 当前是否按下
 * @param shift_pressed Shift 当前是否按下
 * @param caps_lock_enabled Caps Lock 当前是否启用
 * @return ASCII 字符值，无有效字符返回 0
 */
uint32_t ResolveKeyboardCharacter(
    const keyboard_device::tca8418::KeyMapping& mapping,
    bool function_pressed, bool shift_pressed, bool caps_lock_enabled) {
  const bool use_function_character =
      function_pressed && mapping.function_character != '\0';
  char character = use_function_character
      ? mapping.function_character
      : mapping.character;
  if (!use_function_character && shift_pressed != caps_lock_enabled &&
      character >= 'a' && character <= 'z') {
    character = static_cast<char>(character - 'a' + 'A');
  }
  return static_cast<uint8_t>(character);
}

/**
 * @brief 获取摄像头启动流程已经消耗的时间
 * @param start_tick 启动流程开始时的系统节拍
 * @return 已消耗时间，单位为毫秒
 */
uint32_t CameraStartupElapsedMs(TickType_t start_tick) {
  return static_cast<uint32_t>(
      (xTaskGetTickCount() - start_tick) * portTICK_PERIOD_MS);
}

/**
 * @brief 判断摄像头启动流程是否已经达到总超时
 * @param start_tick 启动流程开始时的系统节拍
 * @return 达到总超时返回 true，否则返回 false
 */
bool CameraStartupTimedOut(TickType_t start_tick) {
  return CameraStartupElapsedMs(start_tick) >= kCameraStartupTimeoutMs;
}

/**
 * @brief 获取摄像头启动流程剩余时间
 * @param start_tick 启动流程开始时的系统节拍
 * @return 剩余时间，单位为毫秒
 */
uint32_t CameraStartupRemainingMs(TickType_t start_tick) {
  const uint32_t elapsed_ms = CameraStartupElapsedMs(start_tick);
  return elapsed_ms < kCameraStartupTimeoutMs
             ? kCameraStartupTimeoutMs - elapsed_ms
             : 0;
}

/**
 * @brief 判断系统错误是否可能由摄像头瞬时硬件状态引起
 * @param error errno 错误值
 * @return 适合通过完整摄像头电源重启恢复返回 true
 */
bool IsRetryableCameraIoError(int error) {
  switch (error) {
    case EAGAIN:
    case EBUSY:
    case EIO:
    case ENODEV:
    case ENOENT:
    case ENXIO:
    case ETIMEDOUT:
      return true;
    default:
      return false;
  }
}

/**
 * @brief 判断 ESP Video 初始化错误是否适合重新上电重试
 * @param error ESP-IDF 错误值
 * @return 适合通过完整摄像头电源重启恢复返回 true
 */
bool IsRetryableCameraVideoError(esp_err_t error) {
  return error == ESP_FAIL || error == ESP_ERR_NOT_FOUND ||
         error == ESP_ERR_TIMEOUT;
}
// Radio 发送硬件超时的最小值和额外保护时间。
constexpr uint32_t kMinimumRadioTransmitTimeoutMs = 1000;
constexpr uint32_t kRadioTransmitTimeoutMarginMs = 500;
constexpr uint32_t kRadioTransmitWatchdogGraceMs = 1000;
constexpr float kDegreesToRadians = 0.0174532925F;
constexpr float kRadiansToDegrees = 57.2957795F;
constexpr const char* kCameraDeviceName = ESP_VIDEO_MIPI_CSI_DEVICE_NAME;
constexpr size_t kGpsMaxReadBufferBytes = 4096;
constexpr uint32_t kEthernetInitTaskStackBytes = 6 * 1024;
constexpr UBaseType_t kEthernetInitTaskPriority = 3;
constexpr uint32_t kWifiInitTaskStackBytes = 6 * 1024;
constexpr UBaseType_t kWifiInitTaskPriority = 3;
constexpr uint32_t kWifiScanTaskStackBytes = 6 * 1024;
constexpr UBaseType_t kWifiScanTaskPriority = 3;
constexpr uint32_t kWifiConnectTaskStackBytes = 6 * 1024;
constexpr UBaseType_t kWifiConnectTaskPriority = 3;
constexpr uint32_t kWifiHardwareReadyTimeoutMs = 8000;
constexpr uint32_t kWifiHardwareReadyPollMs = 50;
constexpr uint32_t kWifiEsp32c6BootDelayMs = 500;
constexpr uint32_t kWifiScanTimeoutMs = 8000;
constexpr uint32_t kWifiScanStateRetryIntervalMs = 500;
constexpr const char* kFactoryWifiSsid = "LilyGo-AABB";
constexpr const char* kFactoryWifiPassword = "xinyuandianzi";
constexpr const char* kWifiSntpServer = "pool.ntp.org";
constexpr int kWifiSntpMaxAttemptCount = 3;
constexpr uint32_t kWifiSntpAttemptIntervalMs =
    kWifiInternetCheckTimeoutMs / kWifiSntpMaxAttemptCount;
static_assert(kWifiSntpAttemptIntervalMs * kWifiSntpMaxAttemptCount ==
    kWifiInternetCheckTimeoutMs);
constexpr int kWifiMaxReconnectCount = 8;
constexpr int64_t kWifiValidUnixTimeThreshold = 1700000000LL;
constexpr uint32_t kRtcSyncTaskStackBytes = 4 * 1024;
constexpr UBaseType_t kRtcSyncTaskPriority = 3;
constexpr size_t kRadioIrqTextCapacity = 160;

// SX1262 IRQ 位与日志名称映射。
struct RadioIrqDescription {
  uint16_t mask;
  const char* name;
};

constexpr std::array<RadioIrqDescription, 10> kRadioIrqDescriptions = {{
    {static_cast<uint16_t>(SX126X_IRQ_TX_DONE), "TX_DONE"},
    {static_cast<uint16_t>(SX126X_IRQ_RX_DONE), "RX_DONE"},
    {static_cast<uint16_t>(SX126X_IRQ_PREAMBLE_DETECTED),
        "PREAMBLE_DETECTED"},
    {static_cast<uint16_t>(SX126X_IRQ_SYNC_WORD_VALID), "SYNC_WORD_VALID"},
    {static_cast<uint16_t>(SX126X_IRQ_HEADER_VALID), "HEADER_VALID"},
    {static_cast<uint16_t>(SX126X_IRQ_HEADER_ERROR), "HEADER_ERROR"},
    {static_cast<uint16_t>(SX126X_IRQ_CRC_ERROR), "CRC_ERROR"},
    {static_cast<uint16_t>(SX126X_IRQ_CAD_DONE), "CAD_DONE"},
    {static_cast<uint16_t>(SX126X_IRQ_CAD_DETECTED), "CAD_DETECTED"},
    {static_cast<uint16_t>(SX126X_IRQ_TIMEOUT), "TIMEOUT"},
}};

/**
 * @brief 将 SX1262 IRQ 位掩码格式化为可读名称和十六进制数值
 * @param irq_mask SX1262 IRQ 位掩码
 * @param output 输出文本缓冲区
 * @param output_size 输出文本缓冲区大小
 */
void FormatRadioIrqMask(uint16_t irq_mask, char* output, size_t output_size) {
  if (output == nullptr || output_size == 0) {
    return;
  }
  output[0] = '\0';
  size_t used = 0;
  bool has_name = false;

  const auto append_name = [&](const char* name) {
    const int result = std::snprintf(output + used, output_size - used,
        "%s%s", has_name ? " | " : "", name);
    if (result < 0 || static_cast<size_t>(result) >= output_size - used) {
      output[output_size - 1] = '\0';
      return false;
    }
    used += static_cast<size_t>(result);
    has_name = true;
    return true;
  };

  uint16_t unknown_mask = irq_mask;
  for (const RadioIrqDescription& description : kRadioIrqDescriptions) {
    if ((irq_mask & description.mask) == 0) {
      continue;
    }
    if (!append_name(description.name)) {
      return;
    }
    unknown_mask &= static_cast<uint16_t>(~description.mask);
  }
  if (unknown_mask != 0 && !append_name("UNKNOWN")) {
    return;
  }
  if (!has_name && !append_name("NONE")) {
    return;
  }

  std::snprintf(output + used, output_size - used, " (0x%04X)",
      static_cast<unsigned>(irq_mask));
}

/**
 * @brief 获取当前接收 SNTP 时间同步回调的设备实例
 * @return 保存回调目标设备的原子指针
 */
std::atomic<TDisplayP4Device*>& WifiTimeSyncOwner() {
  static std::atomic<TDisplayP4Device*> owner{nullptr};
  return owner;
}

/**
 * @brief 将触摸点设置为仅包含硬件边缘提示的无坐标样本
 * @param point 待设置的触摸点
 */
void SetHardwareEdgeTouchPoint(TouchPoint* point) {
  if (point == nullptr) {
    return;
  }
  *point = TouchPoint();
  point->x = -1;
  point->y = -1;
  point->edge_touch_flag = true;
}

/**
 * @brief 将屏幕旋转角度规整到摄像头预览支持的范围
 * @param angle 屏幕旋转角度
 * @return 规整后的角度
 */
int NormalizeCameraPreviewRotationAngle(int angle) {
  angle %= 360;
  if (angle < 0) {
    angle += 360;
  }
  switch (angle) {
    case 90:
    case 180:
    case 270:
      return angle;
    default:
      return 0;
  }
}

/**
 * @brief 将屏幕旋转角度转换为 PPA 旋转角度
 * @param angle 屏幕旋转角度
 * @return PPA 旋转角度
 */
ppa_srm_rotation_angle_t ToCameraPreviewPpaRotation(int angle) {
  switch (NormalizeCameraPreviewRotationAngle(angle)) {
    case 90:
      return PPA_SRM_ROTATION_ANGLE_270;
    case 180:
      return PPA_SRM_ROTATION_ANGLE_180;
    case 270:
      return PPA_SRM_ROTATION_ANGLE_90;
    default:
      return PPA_SRM_ROTATION_ANGLE_0;
  }
}

int ClampScreenBrightnessPercent(int percent) {
  return std::clamp(
      percent, kScreenBrightnessMinPercent, kScreenBrightnessMaxPercent);
}

/**
 * @brief 将用户亮度映射为经过感知校正的 HI8561 背光 PWM 占空比
 * @param clamped_percent 已限制到 0～100 的用户亮度
 * @return 0 表示关闭背光，非零亮度按平方曲线映射到比例值 10～1000
 */
cpp_bus_driver::Pwm::DutyCycle ScreenBrightnessPercentToHi8561DutyCycle(
    int clamped_percent) {
  if (clamped_percent <= kScreenBrightnessMinPercent) {
    return {.value = 0, .scale = kPt4103DutyScale};
  }

  const int input_percent =
      std::max(clamped_percent, kHi8561BrightnessInputMinPercent);
  constexpr int kInputRangeSquared =
      kScreenBrightnessMaxPercent * kScreenBrightnessMaxPercent;
  const uint32_t scaled_duty = static_cast<uint32_t>(
      input_percent * input_percent * kPt4103DutyScale);
  return {
      .value = (scaled_duty + kInputRangeSquared / 2) / kInputRangeSquared,
      .scale = kPt4103DutyScale,
  };
}

uint8_t ScreenBrightnessPercentToRm69a10Value(int clamped_percent) {
  return static_cast<uint8_t>(
      clamped_percent * kRm69a10BrightnessMax / kScreenBrightnessMaxPercent);
}

cpp_bus_driver::Pwm::DutyCycle
KeyboardBacklightBrightnessPercentToSy7200aDutyCycle(int percent) {
  return {
      .value = static_cast<uint32_t>(percent) *
          kKeyboardBacklightDutyMax /
          kKeyboardBacklightBrightnessMaxPercent,
      .scale = kSy7200aDutyScale,
  };
}

uint8_t PercentToUint8Value(int percent, uint8_t max_value) {
  const int clamped_percent = std::clamp(percent, 0, 100);
  return static_cast<uint8_t>(clamped_percent * max_value / 100);
}

/**
 * @brief 判断 GNSS 浮点字段是否已经被解析更新
 * @param value GNSS 浮点字段
 * @return 已更新返回 true，否则返回 false
 */
bool IsGnssFloatReady(float value) { return value >= 0.0F; }

/**
 * @brief 将字符串安全复制到固定长度 C 字符数组
 * @param destination 目标字符数组
 * @param destination_size 目标字符数组长度
 * @param source 源字符串
 */
void CopyString(
    char* destination, size_t destination_size, const std::string& source) {
  if (destination == nullptr || destination_size == 0) {
    return;
  }

  std::snprintf(destination, destination_size, "%s", source.c_str());
}

/**
 * @brief 将 6 字节 MAC 地址打包为整数
 * @param mac_address MAC 地址数组
 * @return 打包后的 MAC 地址
 */
uint64_t PackMacAddress(const uint8_t* mac_address) {
  if (mac_address == nullptr) {
    return 0;
  }

  uint64_t packed = 0;
  for (size_t i = 0; i < 6; ++i) {
    packed = (packed << 8) | mac_address[i];
  }
  return packed;
}

/**
 * @brief 判断 ESP-IDF 认证模式是否表示加密热点
 * @param auth_mode ESP-IDF WiFi 认证模式
 * @return 需要密码返回 true，开放热点返回 false
 */
bool IsSecureWifiAuthMode(wifi_auth_mode_t auth_mode) {
  return auth_mode != WIFI_AUTH_OPEN;
}

/**
 * @brief 根据 WiFi 信道判断是否属于 5 GHz 频段
 * @param channel WiFi 主信道
 * @return 大于 2.4 GHz 信道范围返回 true
 */
bool IsFiveGWifiChannel(int channel) {
  return channel > 14;
}

esp_err_t SetWifiCoprocessorResetLevel(void* user_data, bool level) {
  auto* driver =
      static_cast<lilygo_device_driver::TDisplayP4Driver*>(user_data);
  if (driver == nullptr) {
    return ESP_ERR_INVALID_ARG;
  }
  return driver->SetEsp32c6PowerEnabled(level) ? ESP_OK : ESP_FAIL;
}

bool SelectLoraBandwidth(uint32_t bandwidth_hz,
    sx126x_lora_bw_t* bandwidth) {
  if (bandwidth == nullptr) {
    return false;
  }
  switch (bandwidth_hz) {
    case 62500:
      *bandwidth = SX126X_LORA_BW_062;
      return true;
    case 125000:
      *bandwidth = SX126X_LORA_BW_125;
      return true;
    case 250000:
      *bandwidth = SX126X_LORA_BW_250;
      return true;
    case 500000:
      *bandwidth = SX126X_LORA_BW_500;
      return true;
    default:
      return false;
  }
}

/**
 * @brief 将应用层扩频因子转换为 SX1262 LoRa 枚举
 * @param value 应用层扩频因子
 * @param spreading_factor SX1262 扩频因子输出地址
 * @return 扩频因子受支持返回 true
 */
bool SelectLoraSpreadingFactor(
    uint8_t value, sx126x_lora_sf_t* spreading_factor) {
  if (spreading_factor == nullptr) {
    return false;
  }
  switch (value) {
    case 5:
      *spreading_factor = SX126X_LORA_SF5;
      return true;
    case 6:
      *spreading_factor = SX126X_LORA_SF6;
      return true;
    case 7:
      *spreading_factor = SX126X_LORA_SF7;
      return true;
    case 8:
      *spreading_factor = SX126X_LORA_SF8;
      return true;
    case 9:
      *spreading_factor = SX126X_LORA_SF9;
      return true;
    case 10:
      *spreading_factor = SX126X_LORA_SF10;
      return true;
    case 11:
      *spreading_factor = SX126X_LORA_SF11;
      return true;
    case 12:
      *spreading_factor = SX126X_LORA_SF12;
      return true;
    default:
      return false;
  }
}

/**
 * @brief 将应用层编码率分母转换为 SX1262 LoRa 枚举
 * @param denominator 应用层编码率分母
 * @param coding_rate SX1262 编码率输出地址
 * @return 编码率受支持返回 true
 */
bool SelectLoraCodingRate(
    uint8_t denominator, sx126x_lora_cr_t* coding_rate) {
  if (coding_rate == nullptr) {
    return false;
  }
  switch (denominator) {
    case 5:
      *coding_rate = SX126X_LORA_CR_4_5;
      return true;
    case 6:
      *coding_rate = SX126X_LORA_CR_4_6;
      return true;
    case 7:
      *coding_rate = SX126X_LORA_CR_4_7;
      return true;
    case 8:
      *coding_rate = SX126X_LORA_CR_4_8;
      return true;
    default:
      return false;
  }
}

void SelectImageCalibration(uint32_t frequency_hz,
    uint16_t* minimum_mhz, uint16_t* maximum_mhz) {
  const uint32_t frequency_mhz = frequency_hz / 1000000;
  if (frequency_mhz >= 902) {
    *minimum_mhz = 902;
    *maximum_mhz = 928;
  } else if (frequency_mhz >= 863) {
    *minimum_mhz = 863;
    *maximum_mhz = 870;
  } else if (frequency_mhz >= 779) {
    *minimum_mhz = 779;
    *maximum_mhz = 787;
  } else if (frequency_mhz >= 470) {
    *minimum_mhz = 470;
    *maximum_mhz = 510;
  } else {
    *minimum_mhz = 430;
    *maximum_mhz = 440;
  }
}

/**
 * @brief 校验应用层 LoRa 参数并转换为 SX1262 驱动配置
 * @param source 应用层 LoRa 配置
 * @param target SX1262 驱动配置输出地址
 * @return 参数有效且转换成功时返回 true
 */
bool BuildSx1262Config(const LoraRadioConfig& source,
    usp_cpp_bus_driver::Sx126x::LoraConfig* target) {
  sx126x_lora_sf_t spreading_factor;
  sx126x_lora_bw_t bandwidth;
  sx126x_lora_cr_t coding_rate;
  if (target == nullptr || source.frequency_hz < 150000000 ||
      source.frequency_hz > 960000000 || source.preamble_length == 0 ||
      source.output_power_dbm < -9 || source.output_power_dbm > 22 ||
      !SelectLoraSpreadingFactor(
          source.spreading_factor, &spreading_factor) ||
      !SelectLoraBandwidth(source.bandwidth_hz, &bandwidth) ||
      !SelectLoraCodingRate(source.coding_rate_denominator, &coding_rate)) {
    return false;
  }
  target->frequency_hz = source.frequency_hz;
  target->spreading_factor = spreading_factor;
  target->bandwidth = bandwidth;
  target->coding_rate = coding_rate;
  target->preamble_length = source.preamble_length;
  target->sync_word = source.sync_word;
  target->output_power_dbm = source.output_power_dbm;
  target->crc_enabled = source.crc_enabled;
  target->invert_iq = source.invert_iq;
  target->rx_boosted = source.rx_boosted;
  SelectImageCalibration(source.frequency_hz,
      &target->image_calibration_min_mhz, &target->image_calibration_max_mhz);
  return true;
}

struct LoraTransmitTiming {
  // 根据当前调制参数计算的理论空中时间。
  uint32_t time_on_air_ms = 0;
  // 写入 SX1262 SetTx 命令的硬件超时，0 表示禁用硬件超时。
  uint32_t hardware_timeout_ms = 0;
  // MCU 等待 TX_DONE 或 TIMEOUT 事件的最长时间。
  uint32_t watchdog_timeout_ms = 0;
};

/**
 * @brief 根据 LoRa 符号时间判断是否启用低数据率优化
 * @param config 当前 LoRa 配置
 * @return 单个符号时间不小于 16 ms 时返回 true
 */
bool ShouldEnableLoraLdro(const LoraRadioConfig& config) {
  const uint64_t symbol_time_numerator = uint64_t{1} << config.spreading_factor;
  return symbol_time_numerator * 1000U >=
         static_cast<uint64_t>(config.bandwidth_hz) * 16U;
}

/**
 * @brief 计算指定 LoRa 数据包的空中时间和安全发送超时
 * @param config 当前 LoRa 配置
 * @param payload_size 待发送负载长度
 * @param timing 发送时间参数输出
 * @return 配置和负载有效且时间计算成功时返回 true
 */
bool CalculateLoraTransmitTiming(const LoraRadioConfig& config,
    size_t payload_size, LoraTransmitTiming* timing) {
  if (timing == nullptr || payload_size == 0 || payload_size > UINT8_MAX ||
      config.preamble_length == 0) {
    return false;
  }
  sx126x_lora_sf_t spreading_factor;
  sx126x_lora_bw_t bandwidth;
  sx126x_lora_cr_t coding_rate;
  if (!SelectLoraSpreadingFactor(
          config.spreading_factor, &spreading_factor) ||
      !SelectLoraBandwidth(config.bandwidth_hz, &bandwidth) ||
      !SelectLoraCodingRate(config.coding_rate_denominator, &coding_rate)) {
    return false;
  }
  const sx126x_mod_params_lora_t modulation_params = {
      .sf = spreading_factor,
      .bw = bandwidth,
      .cr = coding_rate,
      .ldro = static_cast<uint8_t>(ShouldEnableLoraLdro(config)),
  };
  const sx126x_pkt_params_lora_t packet_params = {
      .preamble_len_in_symb = config.preamble_length,
      .header_type = SX126X_LORA_PKT_EXPLICIT,
      .pld_len_in_bytes = static_cast<uint8_t>(payload_size),
      .crc_is_on = config.crc_enabled,
      .invert_iq_is_on = config.invert_iq,
  };
  const uint32_t time_on_air_ms =
      sx126x_get_lora_time_on_air_in_ms(&packet_params, &modulation_params);
  if (time_on_air_ms == 0) {
    return false;
  }
  const uint32_t margin_ms =
      std::max(kRadioTransmitTimeoutMarginMs, time_on_air_ms / 4);
  const uint64_t requested_timeout_ms =
      std::max<uint64_t>(kMinimumRadioTransmitTimeoutMs,
          static_cast<uint64_t>(time_on_air_ms) + margin_ms);
  *timing = LoraTransmitTiming{};
  timing->time_on_air_ms = time_on_air_ms;
  timing->hardware_timeout_ms =
      requested_timeout_ms <= SX126X_MAX_TIMEOUT_IN_MS
          ? static_cast<uint32_t>(requested_timeout_ms)
          : 0;
  timing->watchdog_timeout_ms = static_cast<uint32_t>(std::min<uint64_t>(
      requested_timeout_ms + kRadioTransmitWatchdogGraceMs, UINT32_MAX));
  return true;
}

/**
 * @brief 将公共 GFSK 参数转换为 CC1101 驱动配置
 * @param source 公共 GFSK 参数
 * @param target CC1101 驱动配置输出
 * @return 参数有效时返回 true
 */
bool BuildCc1101Config(const GfskRadioConfig& source,
    cpp_bus_driver::Cc1101::Config* target) {
  if (target == nullptr || source.frequency_hz == 0 ||
      source.data_rate_bps == 0 || source.frequency_deviation_hz == 0 ||
      source.receive_bandwidth_hz == 0 ||
      source.preamble_length_bits == 0) {
    return false;
  }
  *target = cpp_bus_driver::Cc1101::Config{};
  target->frequency_mhz = static_cast<double>(source.frequency_hz) / 1000000.0;
  target->data_rate_kbaud =
      static_cast<double>(source.data_rate_bps) / 1000.0;
  target->frequency_deviation_khz =
      static_cast<double>(source.frequency_deviation_hz) / 1000.0;
  target->receive_bandwidth_khz =
      static_cast<double>(source.receive_bandwidth_hz) / 1000.0;
  target->output_power_dbm = source.output_power_dbm;
  target->preamble_length_bits = source.preamble_length_bits;
  target->sync_word_high = static_cast<uint8_t>(source.sync_word >> 8);
  target->sync_word_low = static_cast<uint8_t>(source.sync_word);
  target->modulation = cpp_bus_driver::Cc1101::Modulation::kGfsk;
  target->encoding = source.whitening_enabled
      ? cpp_bus_driver::Cc1101::Encoding::kWhitening
      : cpp_bus_driver::Cc1101::Encoding::kNrz;
  target->maximum_packet_length = 60;
  target->packet_length_mode = source.fec_enabled
      ? cpp_bus_driver::Cc1101::PacketLengthMode::kFixed
      : cpp_bus_driver::Cc1101::PacketLengthMode::kVariable;
  target->crc_enabled = source.crc_enabled;
  target->crc_autoflush = source.crc_enabled;
  target->append_status = true;
  target->fec_enabled = source.fec_enabled;
  return true;
}

/**
 * @brief 根据中心频率选择键盘扩展板上的 CC1101 射频通路
 * @param frequency_hz 中心频率，单位为 Hz
 * @param rf_switch 射频开关输出
 * @return 频率属于板级支持频段时返回 true
 */
bool SelectCc1101RfSwitch(uint32_t frequency_hz,
    lilygo_device_driver::TDisplayP4Driver::Cc1101RfSwitch* rf_switch) {
  if (rf_switch == nullptr) {
    return false;
  }
  if (frequency_hz >= 300000000U && frequency_hz <= 348000000U) {
    *rf_switch = lilygo_device_driver::TDisplayP4Driver::
        Cc1101RfSwitch::k315Mhz;
    return true;
  }
  if (frequency_hz >= 387000000U && frequency_hz <= 464000000U) {
    *rf_switch = lilygo_device_driver::TDisplayP4Driver::
        Cc1101RfSwitch::k434Mhz;
    return true;
  }
  if (frequency_hz >= 779000000U && frequency_hz <= 928000000U) {
    *rf_switch = lilygo_device_driver::TDisplayP4Driver::
        Cc1101RfSwitch::k868_915Mhz;
    return true;
  }
  return false;
}

/**
 * @brief 将空中数据速率转换为 nRF24L01 驱动枚举
 * @param data_rate_bps 空中数据速率，单位为 bit/s
 * @param data_rate 驱动数据速率输出
 * @return 速率受芯片支持时返回 true
 */
bool SelectNrf24l01DataRate(uint32_t data_rate_bps,
    cpp_bus_driver::Nrf24l01x::DataRate* data_rate) {
  if (data_rate == nullptr) {
    return false;
  }
  switch (data_rate_bps) {
    case 250000:
      *data_rate = cpp_bus_driver::Nrf24l01x::DataRate::k250Kbps;
      return true;
    case 1000000:
      *data_rate = cpp_bus_driver::Nrf24l01x::DataRate::k1Mbps;
      return true;
    case 2000000:
      *data_rate = cpp_bus_driver::Nrf24l01x::DataRate::k2Mbps;
      return true;
    default:
      return false;
  }
}

/**
 * @brief 将发射功率转换为 nRF24L01 驱动枚举
 * @param output_power_dbm 发射功率，单位为 dBm
 * @param output_power 驱动发射功率输出
 * @return 功率受芯片支持时返回 true
 */
bool SelectNrf24l01OutputPower(int8_t output_power_dbm,
    cpp_bus_driver::Nrf24l01x::OutputPower* output_power) {
  if (output_power == nullptr) {
    return false;
  }
  switch (output_power_dbm) {
    case -18:
      *output_power = cpp_bus_driver::Nrf24l01x::OutputPower::kMinus18Dbm;
      return true;
    case -12:
      *output_power = cpp_bus_driver::Nrf24l01x::OutputPower::kMinus12Dbm;
      return true;
    case -6:
      *output_power = cpp_bus_driver::Nrf24l01x::OutputPower::kMinus6Dbm;
      return true;
    case 0:
      *output_power = cpp_bus_driver::Nrf24l01x::OutputPower::kZeroDbm;
      return true;
    default:
      return false;
  }
}

/**
 * @brief 获取 nRF24L01 发射结果的日志说明
 * @param result nRF24L01 发射结果
 * @return 静态结果说明
 */
const char* Nrf24l01TransmitResultName(
    cpp_bus_driver::Nrf24l01x::TransmitResult result) {
  using TransmitResult = cpp_bus_driver::Nrf24l01x::TransmitResult;
  switch (result) {
    case TransmitResult::kSuccess:
      return "success";
    case TransmitResult::kMaximumRetransmit:
      return "maximum retransmit";
    case TransmitResult::kTimeout:
      return "timeout";
    case TransmitResult::kInvalidArgument:
      return "invalid argument";
    case TransmitResult::kBusError:
      return "bus or GPIO error";
  }
  return "unknown";
}

/**
 * @brief 将公共 Enhanced ShockBurst 参数转换为 nRF24L01 驱动配置
 * @param source 公共 Enhanced ShockBurst 参数
 * @param target nRF24L01 驱动配置输出
 * @return 参数有效时返回 true
 */
bool BuildNrf24l01Config(const EnhancedShockBurstRadioConfig& source,
    cpp_bus_driver::Nrf24l01x::Config* target) {
  cpp_bus_driver::Nrf24l01x::DataRate data_rate;
  cpp_bus_driver::Nrf24l01x::OutputPower output_power;
  if (target == nullptr || source.channel > 125 ||
      source.address_width < 3 || source.address_width > 5 ||
      (source.crc_length_bits != 8 && source.crc_length_bits != 16) ||
      (source.dynamic_payload_enabled && !source.auto_ack_enabled) ||
      source.retransmit_count > 15 || source.retransmit_delay_us < 250 ||
      source.retransmit_delay_us > 4000 ||
      source.retransmit_delay_us % 250 != 0 ||
      !SelectNrf24l01DataRate(source.data_rate_bps, &data_rate) ||
      !SelectNrf24l01OutputPower(source.output_power_dbm, &output_power)) {
    return false;
  }
  *target = cpp_bus_driver::Nrf24l01x::Config{};
  target->operation_mode =
      cpp_bus_driver::Nrf24l01x::OperationMode::kPrimaryReceiver;
  target->power_mode = cpp_bus_driver::Nrf24l01x::PowerMode::kPowerUp;
  target->crc_mode = source.crc_length_bits == 16
      ? cpp_bus_driver::Nrf24l01x::CrcMode::k16Bit
      : cpp_bus_driver::Nrf24l01x::CrcMode::k8Bit;
  target->output_power = output_power;
  target->data_rate = data_rate;
  target->address_width = static_cast<cpp_bus_driver::Nrf24l01x::AddressWidth>(
      source.address_width);
  target->rf_channel = source.channel;
  target->retransmit_count = source.retransmit_count;
  target->retransmit_delay_us = source.retransmit_delay_us;
  target->enabled_pipe_mask = 0x01;
  target->auto_ack_pipe_mask = source.auto_ack_enabled ? 0x01 : 0;
  target->dynamic_payload_enabled = source.dynamic_payload_enabled;
  target->dynamic_payload_pipe_mask =
      source.dynamic_payload_enabled && source.auto_ack_enabled ? 0x01 : 0;
  target->rx_payload_width[0] = source.dynamic_payload_enabled ? 0 : 32;
  return true;
}

/**
 * @brief 按 nRF24L01 寄存器写入顺序编码五字节地址
 * @param address 数值形式的空中地址
 * @param output 五字节地址输出
 */
void EncodeNrf24l01Address(uint64_t address, uint8_t* output) {
  for (size_t index = 0; index < 5; ++index) {
    output[index] = static_cast<uint8_t>(address >> (index * 8));
  }
}

}  // namespace

TDisplayP4Device::TDisplayP4Device()
    : driver_(lilygo_device_driver::TDisplayP4Driver::GetInstance()),
      tool_(std::make_unique<cpp_bus_driver::Tool>()) {
  wifi_.scan_results_mutex = xSemaphoreCreateMutex();
  radio_.mutex = xSemaphoreCreateMutex();
  cc1101_radio_.mutex = xSemaphoreCreateMutex();
  nrf24l01_radio_.mutex = xSemaphoreCreateMutex();
  nfc_.mutex = xSemaphoreCreateMutex();
}

bool TDisplayP4Device::InitializeTouchInterrupt() {
  if (touch_interrupt_initialized_) {
    return true;
  }
  if (tool_ == nullptr || !driver_.IsTouchReady() ||
      !driver_.IsXl9535Ready() || driver_.chip().xl9535 == nullptr) {
    return false;
  }

  if (!driver_.chip().xl9535->ClearIrqFlag()) {
    LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
        "Clear XL9535 interrupt failed during initialization\n");
    return false;
  }
  touch_interrupt_pending_.store(false, std::memory_order_relaxed);
  if (!tool_->InitGpioInterrupt(gpio::xl9535::kInt,
          cpp_bus_driver::Tool::InterruptMode::kFalling,
          TouchInterruptHandler, this,
          cpp_bus_driver::Tool::GpioStatus::kPullup)) {
    return false;
  }

  touch_interrupt_initialized_ = true;
  if (!tool_->GpioRead(gpio::xl9535::kInt)) {
    touch_interrupt_pending_.store(true, std::memory_order_relaxed);
  }
  return true;
}

void TDisplayP4Device::TouchInterruptHandler(void* context) {
  if (context == nullptr) {
    return;
  }
  auto* device = static_cast<TDisplayP4Device*>(context);
  device->touch_interrupt_pending_.store(true, std::memory_order_relaxed);
}

bool TDisplayP4Device::InitializeKeyboardExpansionConnectionInterrupt(
    bool detect_current_level) {
  if (tool_ == nullptr) {
    return false;
  }

  bool expected = false;
  if (!keyboard_expansion_.interrupt_initialized
           .compare_exchange_strong(expected, true)) {
    return true;
  }

  keyboard_expansion_.connection_interrupt_pending.store(
      false, std::memory_order_relaxed);
  if (!tool_->SetGpioMode(keyboard_gpio::tca8418::kInt,
          cpp_bus_driver::Tool::GpioMode::kInput,
          cpp_bus_driver::Tool::GpioStatus::kPulldown)) {
    keyboard_expansion_.interrupt_initialized.store(false);
    return false;
  }
  const bool connected_before_interrupt =
      tool_->GpioRead(keyboard_gpio::tca8418::kInt);
  if (!tool_->InitGpioInterrupt(keyboard_gpio::tca8418::kInt,
          cpp_bus_driver::Tool::InterruptMode::kRising,
          KeyboardExpansionConnectionInterruptHandler, this,
          cpp_bus_driver::Tool::GpioStatus::kPulldown)) {
    keyboard_expansion_.interrupt_initialized.store(false);
    return false;
  }

  // 初始化失败且连接线始终为高时等待下一次实际插拔，避免循环扫描；
  // 监听注册期间出现的低到高变化仍需立即补记。
  if ((detect_current_level || !connected_before_interrupt) &&
      tool_->GpioRead(keyboard_gpio::tca8418::kInt)) {
    keyboard_expansion_.connection_interrupt_tick.store(
        xTaskGetTickCount(), std::memory_order_relaxed);
    keyboard_expansion_.connection_interrupt_pending.store(
        true, std::memory_order_release);
  }
  return true;
}

bool TDisplayP4Device::InitializeKeyboardInputInterrupt() {
  if (tool_ == nullptr) {
    return false;
  }

  bool expected = false;
  if (!keyboard_expansion_.interrupt_initialized.compare_exchange_strong(
          expected, true)) {
    return true;
  }

  keyboard_expansion_.input_interrupt_pending.store(
      false, std::memory_order_relaxed);
  keyboard_expansion_.disconnection_check_pending.store(
      false, std::memory_order_relaxed);
  if (!tool_->InitGpioInterrupt(keyboard_gpio::tca8418::kInt,
          cpp_bus_driver::Tool::InterruptMode::kFalling,
          KeyboardInputInterruptHandler, this,
          cpp_bus_driver::Tool::GpioStatus::kPulldown)) {
    keyboard_expansion_.interrupt_initialized.store(false);
    return false;
  }

  // 注册中断前 INT 可能已经拉低，需要补记已存在的按键事件。
  if (!tool_->GpioRead(keyboard_gpio::tca8418::kInt)) {
    keyboard_expansion_.input_interrupt_pending.store(
        true, std::memory_order_release);
    keyboard_expansion_.disconnection_check_pending.store(
        true, std::memory_order_release);
  }
  return true;
}

bool TDisplayP4Device::DeinitializeKeyboardExpansionInterrupt() {
  if (!keyboard_expansion_.interrupt_initialized.exchange(false)) {
    keyboard_expansion_.connection_interrupt_pending.store(
        false, std::memory_order_relaxed);
    keyboard_expansion_.input_interrupt_pending.store(
        false, std::memory_order_relaxed);
    keyboard_expansion_.disconnection_check_pending.store(
        false, std::memory_order_relaxed);
    return true;
  }

  const bool result = tool_ != nullptr &&
      tool_->DeinitGpioInterrupt(keyboard_gpio::tca8418::kInt);
  keyboard_expansion_.connection_interrupt_pending.store(
      false, std::memory_order_relaxed);
  keyboard_expansion_.input_interrupt_pending.store(
      false, std::memory_order_relaxed);
  keyboard_expansion_.disconnection_check_pending.store(
      false, std::memory_order_relaxed);
  return result;
}

void TDisplayP4Device::KeyboardExpansionConnectionInterruptHandler(
    void* context) {
  if (context == nullptr) {
    return;
  }
  auto* device = static_cast<TDisplayP4Device*>(context);
  device->keyboard_expansion_.connection_interrupt_tick.store(
      xTaskGetTickCountFromISR(), std::memory_order_relaxed);
  device->keyboard_expansion_.connection_interrupt_pending.store(
      true, std::memory_order_release);
}

void TDisplayP4Device::KeyboardInputInterruptHandler(void* context) {
  if (context == nullptr) {
    return;
  }
  auto* device = static_cast<TDisplayP4Device*>(context);
  device->keyboard_expansion_.input_interrupt_pending.store(
      true, std::memory_order_release);
  device->keyboard_expansion_.disconnection_check_pending.store(
      true, std::memory_order_release);
}

void TDisplayP4Device::RecordKeyboardInputReadFailure() {
  const uint8_t failure_count =
      keyboard_expansion_.consecutive_read_failures.fetch_add(1) + 1;
  if (failure_count < kKeyboardExpansionDisconnectFailureThreshold) {
    if (keyboard_expansion_.interrupt_initialized.load(
            std::memory_order_acquire)) {
      keyboard_expansion_.input_interrupt_pending.store(
          true, std::memory_order_release);
    }
    return;
  }

  KeyboardExpansionState expected = KeyboardExpansionState::kReady;
  if (keyboard_expansion_.state.compare_exchange_strong(
          expected, KeyboardExpansionState::kDisconnected)) {
    keyboard_expansion_.tca8418.store(
        KeyboardExpansionComponentState::kFailed);
    keyboard_expansion_.shift_pressed.store(false);
    keyboard_expansion_.function_pressed.store(false);
    keyboard_expansion_.caps_lock_enabled.store(false);
    keyboard_expansion_.scan_generation.fetch_add(1);
    LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
        "Keyboard expansion disconnected\n");
  }
}

bool TDisplayP4Device::InitDevice() {
  if (nfc_.mutex == nullptr) {
    LogMessage(LogLevel::kError, __FILE__, __LINE__,
        "Create T-Display-P4 NFC synchronization resource failed\n");
    return false;
  }
  const bool result =
      driver_.Init(lilygo_device_driver::TDisplayP4Driver::InitMode::kAsync);
  if (!result) {
    LogMessage(LogLevel::kWarning, __FILE__, __LINE__, "Init failed\n");
  }

  if (!WaitForScreenReady()) {
    LogMessage(
        LogLevel::kError, __FILE__, __LINE__, "WaitForScreenReady failed\n");
    return false;
  }
  if (!WaitForTouchReady()) {
    LogMessage(
        LogLevel::kError, __FILE__, __LINE__, "WaitForTouchReady failed\n");
    return false;
  }
  if (!driver_.SetScreenSleep(false)) {
    LogMessage(LogLevel::kError, __FILE__, __LINE__,
        "Activate screen failed\n");
    return false;
  }
  if (!InitializeTouchInterrupt()) {
    LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
        "Initialize touch interrupt failed; using polling fallback\n");
  }
  return true;
}

bool TDisplayP4Device::StartKeyboardExpansionScan() {
  if (keyboard_expansion_.state.load() == KeyboardExpansionState::kReady) {
    return true;
  }

  if (!DeinitializeKeyboardExpansionInterrupt()) {
    return false;
  }

  bool expected = false;
  if (!keyboard_expansion_.task_running.compare_exchange_strong(
          expected, true)) {
    return keyboard_expansion_.state.load() ==
           KeyboardExpansionState::kScanning;
  }

  keyboard_expansion_.xl9555.store(
      KeyboardExpansionComponentState::kNotChecked);
  keyboard_expansion_.tca8418.store(
      KeyboardExpansionComponentState::kNotChecked);
  keyboard_expansion_.sy7200a.store(
      KeyboardExpansionComponentState::kNotChecked);
  keyboard_expansion_.cc1101.store(
      KeyboardExpansionComponentState::kNotChecked);
  keyboard_expansion_.nrf24l01.store(
      KeyboardExpansionComponentState::kNotChecked);
  keyboard_expansion_.st25r3916.store(
      KeyboardExpansionComponentState::kNotChecked);
  keyboard_expansion_.shift_pressed.store(false);
  keyboard_expansion_.function_pressed.store(false);
  keyboard_expansion_.caps_lock_enabled.store(false);
  keyboard_expansion_.consecutive_read_failures.store(0);
  keyboard_expansion_.state.store(KeyboardExpansionState::kScanning);

  if (xTaskCreate(KeyboardExpansionScanTaskEntry,
          "KeyboardExpScan", kKeyboardExpansionTaskStackBytes, this,
          kKeyboardExpansionTaskPriority, nullptr) != pdPASS) {
    keyboard_expansion_.task_running.store(false);
    keyboard_expansion_.state.store(
        KeyboardExpansionState::kComponentFailure);
    keyboard_expansion_.scan_generation.fetch_add(1);
    if (!InitializeKeyboardExpansionConnectionInterrupt(false)) {
      LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
          "Initialize keyboard expansion connection interrupt failed\n");
    }
    return false;
  }
  return true;
}

bool TDisplayP4Device::DeinitializeKeyboardExpansionHardware(
    KeyboardExpansionState final_state) {
  const KeyboardExpansionState previous_state =
      keyboard_expansion_.state.load();
  RadioState* extension_states[] = {&cc1101_radio_, &nrf24l01_radio_};
  for (RadioState* state : extension_states) {
    if (state->mutex == nullptr ||
        xSemaphoreTake(state->mutex, pdMS_TO_TICKS(2000)) != pdTRUE) {
      LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
          "Wait for keyboard expansion Radio session failed\n");
      return false;
    }
    const bool active = state->active;
    const uint32_t client_token = state->active_client_token;
    const radio::ChipType chip = state->chip;
    const radio::ProtocolType protocol = state->protocol;
    xSemaphoreGive(state->mutex);
    if (!active) {
      continue;
    }
    if (!DeactivateRadio(client_token)) {
      LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
          "Stop keyboard expansion Radio session failed\n");
      return false;
    }
    if (xSemaphoreTake(state->mutex, pdMS_TO_TICKS(2000)) != pdTRUE) {
      return false;
    }
    // 保留逻辑激活项，让扩展板重新连接后由 Radio 页面自动恢复会话。
    state->active_client_token = client_token;
    state->chip = chip;
    state->protocol = protocol;
    state->chip_error = true;
    xSemaphoreGive(state->mutex);
  }
  // 先停止发布键盘和扩展射频能力，避免清理过程中重新排队硬件操作。
  keyboard_expansion_.state.store(final_state);
  if (!SetNfcPollingEnabled(false)) {
    LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
        "Stop keyboard expansion NFC polling failed\n");
    keyboard_expansion_.state.store(
        KeyboardExpansionState::kComponentFailure);
    return false;
  }
  if (!WaitForKeyboardExpansionTask()) {
    keyboard_expansion_.state.store(
        KeyboardExpansionState::kComponentFailure);
    return false;
  }

  const bool interrupt_deinitialized =
      DeinitializeKeyboardExpansionInterrupt();
  const auto deinit_mode =
      previous_state == KeyboardExpansionState::kDisconnected
          ? lilygo_device_driver::TDisplayP4Driver::
                KeyboardExpansionDeinitMode::kForced
          : lilygo_device_driver::TDisplayP4Driver::
                KeyboardExpansionDeinitMode::kNormal;
  const bool result =
      driver_.DeinitKeyboardExpansion(deinit_mode) && interrupt_deinitialized;
  keyboard_expansion_.xl9555.store(
      KeyboardExpansionComponentState::kNotChecked);
  keyboard_expansion_.tca8418.store(
      KeyboardExpansionComponentState::kNotChecked);
  keyboard_expansion_.sy7200a.store(
      KeyboardExpansionComponentState::kNotChecked);
  keyboard_expansion_.cc1101.store(
      KeyboardExpansionComponentState::kNotChecked);
  keyboard_expansion_.nrf24l01.store(
      KeyboardExpansionComponentState::kNotChecked);
  keyboard_expansion_.st25r3916.store(
      KeyboardExpansionComponentState::kNotChecked);
  keyboard_expansion_.shift_pressed.store(false);
  keyboard_expansion_.function_pressed.store(false);
  keyboard_expansion_.caps_lock_enabled.store(false);
  keyboard_expansion_.consecutive_read_failures.store(0);
  keyboard_expansion_.state.store(result
          ? final_state
          : KeyboardExpansionState::kComponentFailure);
  keyboard_expansion_.scan_generation.fetch_add(1);
  return result;
}

bool TDisplayP4Device::DisableKeyboardExpansion() {
  return DeinitializeKeyboardExpansionHardware(
      KeyboardExpansionState::kDisabled);
}

bool TDisplayP4Device::SuspendKeyboardExpansionForScreenLock() {
  keyboard_expansion_.screen_lock_suspended.store(true);
  if (keyboard_expansion_.task_running.load() &&
      !WaitForKeyboardExpansionTask()) {
    return false;
  }
  if (keyboard_expansion_.state.load() != KeyboardExpansionState::kReady) {
    return true;
  }

  return ApplyKeyboardExpansionScreenLockSleep();
}

bool TDisplayP4Device::ApplyKeyboardExpansionScreenLockSleep() {
  bool result = driver_.IsTca8418Ready() &&
      driver_.chip().tca8418 != nullptr;
  if (result) {
    auto* keyboard = driver_.chip().tca8418.get();
    result &= keyboard->SetInterruptEnable(0);
    result &= keyboard->SetKeypadPins(0);
    result &= keyboard->ClearEventFifo();
  }
  keyboard_expansion_.input_interrupt_pending.store(
      false, std::memory_order_relaxed);
  keyboard_expansion_.disconnection_check_pending.store(
      false, std::memory_order_relaxed);
  keyboard_expansion_.shift_pressed.store(false);
  keyboard_expansion_.function_pressed.store(false);
  keyboard_expansion_.consecutive_read_failures.store(0);
  result &= driver_.SetKeyboardExpansionOperatingMode(
      lilygo_device_driver::TDisplayP4Driver::
          KeyboardExpansionOperatingMode::kSleep);
  return result;
}

bool TDisplayP4Device::ResumeKeyboardExpansionAfterScreenUnlock() {
  if (!keyboard_expansion_.screen_lock_suspended.load()) {
    return true;
  }
  if (keyboard_expansion_.task_running.load() &&
      !WaitForKeyboardExpansionTask()) {
    return false;
  }
  if (keyboard_expansion_.state.load() != KeyboardExpansionState::kReady) {
    keyboard_expansion_.screen_lock_suspended.store(false);
    return true;
  }

  bool input_restored = driver_.IsTca8418Ready() &&
      driver_.chip().tca8418 != nullptr;
  if (input_restored) {
    auto* keyboard = driver_.chip().tca8418.get();
    input_restored &= keyboard->ClearEventFifo();
    input_restored &= keyboard->SetKeypadScanWindow(0, 0,
        keyboard_device::tca8418::kKeypadScanWidth,
        keyboard_device::tca8418::kKeypadScanHeight);
    input_restored &= keyboard->ClearEventFifo();
    input_restored &= keyboard->SetIrqGpioMode(
        cpp_bus_driver::Tca8418::IrqMask::kKeyEvents);
  }
  keyboard_expansion_.input_interrupt_pending.store(
      false, std::memory_order_relaxed);
  keyboard_expansion_.disconnection_check_pending.store(
      false, std::memory_order_relaxed);
  keyboard_expansion_.shift_pressed.store(false);
  keyboard_expansion_.function_pressed.store(false);
  keyboard_expansion_.consecutive_read_failures.store(0);
  if (!input_restored) {
    return false;
  }
  keyboard_expansion_.screen_lock_suspended.store(false);
  return RestoreKeyboardExpansionOperatingState();
}

bool TDisplayP4Device::HasKeyboardExpansionDisconnectionCheckPending() const {
  return keyboard_expansion_.disconnection_check_pending.load(
      std::memory_order_acquire);
}

bool TDisplayP4Device::UpdateKeyboardExpansionDisconnectionState() {
  if (keyboard_expansion_.state.load() != KeyboardExpansionState::kReady ||
      !keyboard_expansion_.interrupt_initialized.load(
          std::memory_order_acquire) ||
      !keyboard_expansion_.disconnection_check_pending.exchange(
          false, std::memory_order_acq_rel)) {
    return true;
  }
  if (tool_ == nullptr || !driver_.IsTca8418Ready() ||
      driver_.chip().tca8418 == nullptr) {
    return false;
  }

  // 扩展板上的 INT 空闲时由外部上拉保持高电平，拔出后由主板内部
  // 下拉保持低电平。下降沿也可能来自正常按键，因此需要通过一次
  // TCA8418 通信确认，不能仅根据 GPIO 电平判定扩展已断开。
  if (tool_->GpioRead(keyboard_gpio::tca8418::kInt)) {
    keyboard_expansion_.consecutive_read_failures.store(0);
    return true;
  }
  if (driver_.chip().tca8418->GetFingerCount() != UINT8_MAX) {
    keyboard_expansion_.consecutive_read_failures.store(0);
    // 锁屏时按键事件可能暂时不被 LVGL 消费，INT 会持续为低。保留低频
    // 复查，确保此后直接拔出扩展板时仍能发现通信已经中断。
    if (!tool_->GpioRead(keyboard_gpio::tca8418::kInt)) {
      keyboard_expansion_.disconnection_check_pending.store(
          true, std::memory_order_release);
    }
    return true;
  }

  RecordKeyboardInputReadFailure();
  if (keyboard_expansion_.state.load() == KeyboardExpansionState::kReady) {
    keyboard_expansion_.disconnection_check_pending.store(
        true, std::memory_order_release);
  }
  return true;
}

namespace {

struct Lr2021LfPaTableEntry {
  int8_t half_power;
  uint8_t pa_duty_cycle;
  uint8_t pa_lf_slices;
};

struct Lr2021HfPaTableEntry {
  int8_t half_power;
  uint8_t pa_hf_duty_cycle;
};

constexpr Lr2021LfPaTableEntry kLr2021Pa915MhzTable[] = {
    {44, 7, 6}, {42, 7, 7}, {41, 6, 6}, {39, 6, 6}, {38, 5, 6},
    {36, 5, 6}, {36, 4, 4}, {33, 5, 4}, {34, 4, 2}, {31, 4, 3},
    {30, 5, 1}, {32, 2, 2}, {32, 2, 1},
};

constexpr Lr2021LfPaTableEntry kLr2021Pa490MhzTable[] = {
    {40, 7, 7}, {38, 7, 7}, {36, 7, 6}, {34, 7, 6}, {32, 7, 6},
    {31, 7, 4}, {31, 6, 4}, {29, 7, 2}, {30, 5, 3}, {29, 5, 2},
    {31, 4, 2},
};

constexpr Lr2021HfPaTableEntry kLr2021Pa2445MhzTable[] = {
    {24, 16}, {24, 26}, {24, 30}, {22, 30}, {21, 31},
    {18, 30}, {16, 30}, {15, 31}, {10, 25}, {8, 25},
    {7, 28}, {6, 30}, {4, 30},
};

static_assert(std::size(kLr2021Pa915MhzTable) == 13);
static_assert(std::size(kLr2021Pa490MhzTable) == 11);
static_assert(std::size(kLr2021Pa2445MhzTable) == 13);

bool SelectLr2021Bandwidth(
    uint32_t bandwidth_hz, lr20xx_radio_lora_bw_t* bandwidth) {
  if (bandwidth == nullptr) {
    return false;
  }
  switch (bandwidth_hz) {
    case 31250:
      *bandwidth = LR20XX_RADIO_LORA_BW_31;
      return true;
    case 41670:
      *bandwidth = LR20XX_RADIO_LORA_BW_41;
      return true;
    case 62500:
      *bandwidth = LR20XX_RADIO_LORA_BW_62;
      return true;
    case 83340:
      *bandwidth = LR20XX_RADIO_LORA_BW_83;
      return true;
    case 101563:
      *bandwidth = LR20XX_RADIO_LORA_BW_101;
      return true;
    case 125000:
      *bandwidth = LR20XX_RADIO_LORA_BW_125;
      return true;
    case 203000:
      *bandwidth = LR20XX_RADIO_LORA_BW_203;
      return true;
    case 250000:
      *bandwidth = LR20XX_RADIO_LORA_BW_250;
      return true;
    case 406000:
      *bandwidth = LR20XX_RADIO_LORA_BW_406;
      return true;
    case 500000:
      *bandwidth = LR20XX_RADIO_LORA_BW_500;
      return true;
    case 812000:
      *bandwidth = LR20XX_RADIO_LORA_BW_812;
      return true;
    case 1000000:
      *bandwidth = LR20XX_RADIO_LORA_BW_1000;
      return true;
    default:
      return false;
  }
}

bool SelectLr2021Power(const LoraRadioConfig& source,
    lr20xx_radio_common_pa_cfg_t* pa, int8_t* output_power_half_dbm) {
  if (pa == nullptr || output_power_half_dbm == nullptr) {
    return false;
  }
  const bool high_frequency = source.frequency_hz >= 1600000000U;
  if (high_frequency) {
    if (source.output_power_dbm < -19 || source.output_power_dbm > 5) {
      return false;
    }
    const int8_t table_power = std::max<int8_t>(source.output_power_dbm, 0);
    const Lr2021HfPaTableEntry& power =
        kLr2021Pa2445MhzTable[12 - table_power];
    *pa = {
        .pa_sel = LR20XX_RADIO_COMMON_PA_SEL_HF,
        .pa_lf_mode = LR20XX_RADIO_COMMON_PA_LF_MODE_FSM,
        .pa_lf_duty_cycle = 7,
        .pa_lf_slices = 6,
        .pa_hf_duty_cycle = power.pa_hf_duty_cycle,
    };
    *output_power_half_dbm = source.output_power_dbm < 0
        ? static_cast<int8_t>(source.output_power_dbm * 2)
        : power.half_power;
    return true;
  }
  if (source.output_power_dbm < -9 || source.output_power_dbm > 22) {
    return false;
  }
  const bool low_band = source.frequency_hz < 700000000U;
  const int8_t minimum_table_power = 10;
  const int8_t maximum_table_power = low_band ? 20 : 22;
  const int8_t table_power = std::clamp<int8_t>(
      source.output_power_dbm, minimum_table_power, maximum_table_power);
  const Lr2021LfPaTableEntry& power = low_band
      ? kLr2021Pa490MhzTable[20 - table_power]
      : kLr2021Pa915MhzTable[22 - table_power];
  *pa = {
      .pa_sel = LR20XX_RADIO_COMMON_PA_SEL_LF,
      .pa_lf_mode = LR20XX_RADIO_COMMON_PA_LF_MODE_FSM,
      .pa_lf_duty_cycle = power.pa_duty_cycle,
      .pa_lf_slices = power.pa_lf_slices,
      .pa_hf_duty_cycle = 16,
  };
  *output_power_half_dbm =
      source.output_power_dbm < minimum_table_power ||
          source.output_power_dbm > maximum_table_power
      ? static_cast<int8_t>(source.output_power_dbm * 2)
      : power.half_power;
  return true;
}

bool BuildLr2021Config(const LoraRadioConfig& source, uint8_t payload_size,
    usp_cpp_bus_driver::Lr20xx::LoraConfig* target) {
  if (target == nullptr ||
      !radio::IsLr2021BandwidthSupported(
          source.frequency_hz, source.bandwidth_hz) ||
      source.preamble_length == 0 || source.lr2021_rx_boost_mode > 7) {
    return false;
  }
  lr20xx_radio_lora_bw_t bandwidth;
  lr20xx_radio_common_pa_cfg_t pa = {};
  int8_t output_power_half_dbm = 0;
  const uint8_t coding_rate =
      static_cast<uint8_t>(source.lr2021_coding_rate);
  if (!SelectLr2021Bandwidth(source.bandwidth_hz, &bandwidth) ||
      !SelectLr2021Power(source, &pa, &output_power_half_dbm) ||
      source.spreading_factor < 5 || source.spreading_factor > 12 ||
      !radio::IsLr2021CodingRate(source.lr2021_coding_rate)) {
    return false;
  }
  *target = usp_cpp_bus_driver::Lr20xx::LoraConfig{};
  target->frequency_hz = source.frequency_hz;
  target->modulation.sf = static_cast<lr20xx_radio_lora_sf_t>(
      source.spreading_factor);
  target->modulation.bw = bandwidth;
  target->modulation.cr =
      static_cast<lr20xx_radio_lora_cr_t>(coding_rate);
  target->modulation.ppm = ShouldEnableLoraLdro(source)
      ? LR20XX_RADIO_LORA_PPM_1_4
      : LR20XX_RADIO_LORA_NO_PPM;
  target->packet.preamble_len_in_symb = source.preamble_length;
  target->packet.pkt_mode = LR20XX_RADIO_LORA_PKT_EXPLICIT;
  target->packet.pld_len_in_bytes = payload_size;
  target->packet.crc = source.crc_enabled
      ? LR20XX_RADIO_LORA_CRC_ENABLED
      : LR20XX_RADIO_LORA_CRC_DISABLED;
  target->packet.iq = source.invert_iq
      ? LR20XX_RADIO_LORA_IQ_INVERTED
      : LR20XX_RADIO_LORA_IQ_STANDARD;
  target->sync_word = source.sync_word;
  target->rx_path = source.frequency_hz >= 1600000000U
      ? LR20XX_RADIO_COMMON_RX_PATH_HF
      : LR20XX_RADIO_COMMON_RX_PATH_LF;
  target->rx_boost_mode =
      static_cast<lr20xx_radio_common_rx_path_boost_mode_t>(
          source.lr2021_rx_boost_mode);
  target->pa = pa;
  target->output_power_half_dbm = output_power_half_dbm;
  target->ramp_time = LR20XX_RADIO_COMMON_RAMP_48_US;
  return true;
}

constexpr lr20xx_system_irq_mask_t kLr2021RadioIrqMask =
    LR20XX_SYSTEM_IRQ_TX_DONE | LR20XX_SYSTEM_IRQ_RX_DONE |
    LR20XX_SYSTEM_IRQ_TIMEOUT | LR20XX_SYSTEM_IRQ_CRC_ERROR |
    LR20XX_SYSTEM_IRQ_LEN_ERROR | LR20XX_SYSTEM_IRQ_LORA_HEADER_ERROR |
    LR20XX_SYSTEM_IRQ_ERROR | LR20XX_SYSTEM_IRQ_CMD_ERROR;

bool StartLr2021Receive(usp_cpp_bus_driver::Lr20xx* radio,
    const LoraRadioConfig& config) {
  usp_cpp_bus_driver::Lr20xx::LoraConfig driver_config;
  return radio != nullptr &&
      BuildLr2021Config(config, UINT8_MAX, &driver_config) &&
      radio->Configure(driver_config) &&
      radio->Invoke(lr20xx_system_clear_irq_status,
          LR20XX_SYSTEM_IRQ_ALL_MASK) == LR20XX_STATUS_OK &&
      radio->Invoke(lr20xx_system_set_dio_irq_cfg, LR20XX_SYSTEM_DIO_11,
          kLr2021RadioIrqMask) == LR20XX_STATUS_OK &&
      radio->StartReceive(0);
}

}  // namespace

bool TDisplayP4Device::UpdateKeyboardExpansionConnection(
    bool* scan_started) {
  if (scan_started != nullptr) {
    *scan_started = false;
  }

  KeyboardExpansionState state = keyboard_expansion_.state.load();
  if (state == KeyboardExpansionState::kReady ||
      state == KeyboardExpansionState::kScanning) {
    return true;
  }
  if (state == KeyboardExpansionState::kDisabled) {
    return true;
  }

  if (state == KeyboardExpansionState::kDisconnected) {
    if (!DeinitializeKeyboardExpansionHardware(
            KeyboardExpansionState::kNotFound)) {
      return false;
    }
    if (!InitializeKeyboardExpansionConnectionInterrupt(true)) {
      return false;
    }
  } else if (!InitializeKeyboardExpansionConnectionInterrupt(false)) {
    return false;
  }

  if (!keyboard_expansion_.connection_interrupt_pending.load(
          std::memory_order_acquire)) {
    return true;
  }
  const TickType_t interrupt_tick =
      keyboard_expansion_.connection_interrupt_tick.load(
          std::memory_order_relaxed);
  if (xTaskGetTickCount() - interrupt_tick <
      pdMS_TO_TICKS(kKeyboardExpansionConnectionDebounceMs)) {
    return true;
  }
  if (!keyboard_expansion_.connection_interrupt_pending.exchange(
          false, std::memory_order_acq_rel)) {
    return true;
  }
  if (tool_ == nullptr ||
      !tool_->GpioRead(keyboard_gpio::tca8418::kInt)) {
    return true;
  }

  const bool started = StartKeyboardExpansionScan();
  if (scan_started != nullptr) {
    *scan_started = started;
  }
  return started;
}

bool TDisplayP4Device::HasKeyboardExpansionConnectionChangePending() const {
  return keyboard_expansion_.connection_interrupt_pending.load(
      std::memory_order_acquire);
}

bool TDisplayP4Device::SetKeyboardBacklightBrightnessPercent(int percent) {
  if (percent < kKeyboardBacklightBrightnessMinPercent ||
      percent > kKeyboardBacklightBrightnessMaxPercent) {
    return false;
  }

  if (driver_.IsSy7200aReady()) {
    bool applied = false;
    if (percent == kKeyboardBacklightBrightnessMinPercent) {
      applied = driver_.chip().sy7200a->DisableOutput(
          cpp_bus_driver::Pwm::IdleLevel::kLow);
    } else {
      applied = driver_.chip().sy7200a->SetDuty(
          KeyboardBacklightBrightnessPercentToSy7200aDutyCycle(percent));
    }
    if (!applied) {
      return false;
    }
  } else if (keyboard_expansion_.state.load() ==
             KeyboardExpansionState::kReady) {
    return false;
  }

  keyboard_expansion_.backlight_brightness_percent.store(percent);
  return true;
}

bool TDisplayP4Device::SetKeyboardExpansionLed(
    KeyboardExpansionLed led, bool enabled) {
  if (keyboard_expansion_.state.load() != KeyboardExpansionState::kReady ||
      !driver_.IsXl9555Ready()) {
    return false;
  }

  DriverKeyboardExpansionLed driver_led;
  switch (led) {
    case KeyboardExpansionLed::kLed1:
      driver_led = DriverKeyboardExpansionLed::kLed1;
      break;
    case KeyboardExpansionLed::kLed2:
      driver_led = DriverKeyboardExpansionLed::kLed2;
      break;
    case KeyboardExpansionLed::kLed3:
      driver_led = DriverKeyboardExpansionLed::kLed3;
      break;
    default:
      return false;
  }
  return driver_.SetKeyboardExpansionLed(driver_led, enabled);
}

bool TDisplayP4Device::ReadKeyboardInputEvent(KeyboardInputEvent* event) {
  if (event == nullptr || tool_ == nullptr ||
      keyboard_expansion_.screen_lock_suspended.load() ||
      keyboard_expansion_.state.load() != KeyboardExpansionState::kReady ||
      !driver_.IsTca8418Ready() || driver_.chip().tca8418 == nullptr) {
    return false;
  }

  const bool interrupt_initialized =
      keyboard_expansion_.interrupt_initialized.load(
          std::memory_order_acquire);
  if (interrupt_initialized &&
      !keyboard_expansion_.input_interrupt_pending.exchange(
          false, std::memory_order_acq_rel)) {
    return false;
  }
  // 中断初始化失败时保留 GPIO 轮询回退，避免键盘完全失去输入。
  if (tool_->GpioRead(keyboard_gpio::tca8418::kInt)) {
    return false;
  }

  const uint8_t event_count = driver_.chip().tca8418->GetFingerCount();
  if (event_count == UINT8_MAX) {
    RecordKeyboardInputReadFailure();
    return false;
  }
  if (event_count == 0 || event_count > 10) {
    if (event_count == 0) {
      if (!driver_.chip().tca8418->ClearIrqFlag(
              cpp_bus_driver::Tca8418::IrqFlag::kKeyEvents)) {
        RecordKeyboardInputReadFailure();
      } else {
        keyboard_expansion_.consecutive_read_failures.store(0);
      }
    } else {
      keyboard_expansion_.consecutive_read_failures.store(0);
    }
    return false;
  }

  cpp_bus_driver::Tca8418::TouchInfo input;
  if (!driver_.chip().tca8418->ReadKeyEvent(&input)) {
    RecordKeyboardInputReadFailure();
    return false;
  }
  if (event_count > 1 && interrupt_initialized) {
    keyboard_expansion_.input_interrupt_pending.store(
        true, std::memory_order_release);
  }
  if (event_count == 1) {
    if (!driver_.chip().tca8418->ClearIrqFlag(
            cpp_bus_driver::Tca8418::IrqFlag::kKeyEvents)) {
      RecordKeyboardInputReadFailure();
    } else {
      keyboard_expansion_.consecutive_read_failures.store(0);
    }
  } else {
    keyboard_expansion_.consecutive_read_failures.store(0);
  }
  if (input.num == 0 ||
      input.num > keyboard_device::tca8418::kMap.size()) {
    return false;
  }

  const keyboard_device::tca8418::KeyMapping& mapping =
      keyboard_device::tca8418::kMap[input.num - 1];
  const bool shift_pressed = keyboard_expansion_.shift_pressed.load();
  const bool function_pressed = keyboard_expansion_.function_pressed.load();
  if (mapping.key == keyboard_device::tca8418::KeyCode::kShift) {
    keyboard_expansion_.shift_pressed.store(input.press_flag);
  } else if (mapping.key ==
             keyboard_device::tca8418::KeyCode::kFunction) {
    keyboard_expansion_.function_pressed.store(input.press_flag);
  } else if (mapping.key ==
                 keyboard_device::tca8418::KeyCode::kCapsLock &&
             input.press_flag) {
    const bool caps_lock_enabled =
        !keyboard_expansion_.caps_lock_enabled.load();
    keyboard_expansion_.caps_lock_enabled.store(caps_lock_enabled);
    if (!SetKeyboardExpansionLed(
            KeyboardExpansionLed::kLed1, caps_lock_enabled)) {
      LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
          "Set keyboard Caps Lock indicator failed\n");
    }
  }

  event->key = ToKeyboardKey(mapping.key, shift_pressed);
  event->character = mapping.key ==
          keyboard_device::tca8418::KeyCode::kCharacter
      ? ResolveKeyboardCharacter(mapping, function_pressed, shift_pressed,
            keyboard_expansion_.caps_lock_enabled.load())
      : 0;
  event->key_id = input.num;
  event->pressed = input.press_flag;
  return event->key != KeyboardKey::kUnknown;
}

bool TDisplayP4Device::ReadKeyboardExpansionStatus(
    KeyboardExpansionStatus* status) const {
  if (status == nullptr) {
    return false;
  }
  status->state = keyboard_expansion_.state.load();
  status->xl9555 = keyboard_expansion_.xl9555.load();
  status->tca8418 = keyboard_expansion_.tca8418.load();
  status->sy7200a = keyboard_expansion_.sy7200a.load();
  status->cc1101 = keyboard_expansion_.cc1101.load();
  status->nrf24l01 = keyboard_expansion_.nrf24l01.load();
  status->st25r3916 = keyboard_expansion_.st25r3916.load();
  status->backlight_brightness_percent =
      keyboard_expansion_.backlight_brightness_percent.load();
  status->scan_generation = keyboard_expansion_.scan_generation.load();
  return true;
}

void TDisplayP4Device::KeyboardExpansionScanTaskEntry(void* context) {
  auto* device = static_cast<TDisplayP4Device*>(context);
  if (device != nullptr) {
    device->RunKeyboardExpansionScanTask();
  }
  vTaskDelete(nullptr);
}

void TDisplayP4Device::RunKeyboardExpansionScanTask() {
  const bool initialized = driver_.InitKeyboardExpansion();
  const bool keep_screen_lock_suspended =
      keyboard_expansion_.screen_lock_suspended.load();
  const int backlight_brightness_percent =
      keyboard_expansion_.backlight_brightness_percent.load();
  const bool backlight_applied = !initialized || keep_screen_lock_suspended ||
      SetKeyboardBacklightBrightnessPercent(backlight_brightness_percent);

  const auto component_state = [](bool ready) {
    return ready ? KeyboardExpansionComponentState::kReady
                 : KeyboardExpansionComponentState::kFailed;
  };
  const bool xl9555_ready = driver_.IsXl9555Ready();
  keyboard_expansion_.xl9555.store(component_state(xl9555_ready));
  keyboard_expansion_.tca8418.store(
      component_state(driver_.IsTca8418Ready()));
  keyboard_expansion_.sy7200a.store(
      component_state(driver_.IsSy7200aReady() && backlight_applied));
  keyboard_expansion_.cc1101.store(
      component_state(driver_.IsCc1101Ready()));
  keyboard_expansion_.nrf24l01.store(
      component_state(driver_.IsNrf24l01Ready()));
  keyboard_expansion_.st25r3916.store(
      component_state(driver_.IsSt25r3916Ready()));

  KeyboardExpansionState state;
  if (initialized && backlight_applied) {
    state = KeyboardExpansionState::kReady;
  } else if (!xl9555_ready) {
    state = KeyboardExpansionState::kNotFound;
  } else {
    state = KeyboardExpansionState::kComponentFailure;
  }

  if (state != KeyboardExpansionState::kReady &&
      !driver_.DeinitKeyboardExpansion()) {
    LogMessage(LogLevel::kError, __FILE__, __LINE__,
        "Keyboard expansion cleanup failed\n");
  }
  if (state == KeyboardExpansionState::kReady &&
      !InitializeKeyboardInputInterrupt()) {
    LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
        "Initialize keyboard input interrupt failed; using polling fallback\n");
  }
  if (state == KeyboardExpansionState::kReady &&
      keep_screen_lock_suspended &&
      !ApplyKeyboardExpansionScreenLockSleep()) {
    LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
        "Keep keyboard expansion asleep after locked scan failed\n");
  }
  keyboard_expansion_.state.store(state);
  keyboard_expansion_.scan_generation.fetch_add(1);
  if (state != KeyboardExpansionState::kReady &&
      !InitializeKeyboardExpansionConnectionInterrupt(false)) {
    LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
        "Initialize keyboard expansion connection interrupt failed\n");
  }
  keyboard_expansion_.task_running.store(false);
}

bool TDisplayP4Device::WaitForKeyboardExpansionTask() {
  for (int elapsed_ms = 0; elapsed_ms < kPowerOffTaskTimeoutMs;
      elapsed_ms += kPowerOffTaskPollMs) {
    if (!keyboard_expansion_.task_running.load()) {
      return true;
    }
    vTaskDelay(pdMS_TO_TICKS(kPowerOffTaskPollMs));
  }
  return !keyboard_expansion_.task_running.load();
}

PowerOffAction TDisplayP4Device::RequestPowerOff() {
  if (!PrepareForPowerOff()) {
    LogMessage(LogLevel::kError, __FILE__, __LINE__,
        "Prepare device for power off failed\n");
    return PowerOffAction::kFailed;
  }
  if (!driver_.PrepareDriversForPowerOff()) {
    LogMessage(LogLevel::kError, __FILE__, __LINE__,
        "Prepare device hardware for power off failed\n");
    return PowerOffAction::kFailed;
  }
  return PowerOffAction::kEnterDeepSleep;
}

int TDisplayP4Device::ScreenWidth() const {
  return driver_.screen_info().width;
}

int TDisplayP4Device::ScreenHeight() const {
  return driver_.screen_info().height;
}

int TDisplayP4Device::ScreenBitsPerPixel() const {
  return driver_.screen_info().bits_per_pixel;
}

bool TDisplayP4Device::ReadDeviceInfo(DeviceInfo* info) {
  if (info == nullptr) {
    return false;
  }

  const auto device_info = driver_.device_info();
  info->device_model_name = device_info.model.name;
  info->device_model_version = device_info.model.version;
  info->screen_type = device_info.screen.name;
  info->screen_width = device_info.screen.width;
  info->screen_height = device_info.screen.height;
  info->screen_bits_per_pixel = device_info.screen.bits_per_pixel;
  info->screen_pixel_format = device_info.screen.pixel_format;
  info->camera_name = device_info.camera.name;
  info->camera_pixel_format = device_info.camera.pixel_format;
  info->camera_bits_per_pixel = device_info.camera.bits_per_pixel;
  info->camera_buffer_count = device_info.camera.buffer_count;
  info->battery_fuel_gauge_name = device_info.battery.fuel_gauge_name;
  info->battery_capacity_mah = device_info.battery.capacity_mah;
  return true;
}

bool TDisplayP4Device::SetEthernetEnabled(bool enabled) {
  ethernet_.stop_requested.store(!enabled);
  if (!enabled) {
    if (ethernet_.init_task_running.load()) {
      return true;
    }
    if (ethernet_.handle != nullptr && ethernet_.running.load()) {
      const esp_err_t result =
          esp_eth_stop(reinterpret_cast<esp_eth_handle_t>(ethernet_.handle));
      if (result != ESP_OK && result != ESP_ERR_INVALID_STATE) {
        SetEthernetFailure(result);
        driver_.SetEthernetPowerEnabled(false);
        return false;
      }
    }
    ethernet_.running.store(false);
    ethernet_.link_up.store(false);
    ethernet_.got_ip.store(false);
    ethernet_.start_failed.store(false);
    ethernet_.last_error.store(ESP_OK);
    ethernet_.ip_address.store(0);
    ethernet_.netmask.store(0);
    ethernet_.gateway.store(0);
    return driver_.SetEthernetPowerEnabled(false);
  }

  if (ethernet_.driver_initialized.load() && ethernet_.running.load()) {
    return true;
  }

  bool expected = false;
  if (!ethernet_.init_task_running.compare_exchange_strong(expected, true)) {
    return true;
  }

  ethernet_.start_failed.store(false);
  ethernet_.last_error.store(ESP_OK);
  const BaseType_t result = xTaskCreate(EthernetInitTaskEntry, "ethernet",
      kEthernetInitTaskStackBytes, this, kEthernetInitTaskPriority, nullptr);
  if (result != pdPASS) {
    SetEthernetFailure(ESP_ERR_NO_MEM);
    driver_.SetEthernetPowerEnabled(false);
    return false;
  }
  return true;
}

bool TDisplayP4Device::ReadEthernetStatus(EthernetStatus* status) {
  if (status == nullptr) {
    return false;
  }

  status->init_task_running = ethernet_.init_task_running.load();
  status->driver_initialized = ethernet_.driver_initialized.load();
  status->running = ethernet_.running.load();
  status->link_up = ethernet_.link_up.load();
  status->got_ip = ethernet_.got_ip.load();
  status->start_failed = ethernet_.start_failed.load();
  status->port_count = ethernet_.port_count.load();
  status->last_error = ethernet_.last_error.load();
  status->mac_address = ethernet_.mac_address.load();
  status->ip_address = ethernet_.ip_address.load();
  status->netmask = ethernet_.netmask.load();
  status->gateway = ethernet_.gateway.load();
  return true;
}

bool TDisplayP4Device::SetWifiEnabled(bool enabled) {
  if (!enabled) {
    wifi_time_test_.requested.store(false);
    wifi_.connect_cancel_requested.store(true);
    wifi_.stop_requested.store(true);
    if (wifi_time_test_.active.load()) {
      StopWifiTimeTest();
    } else {
      StopWifiInternetCheck();
    }

    if (!wifi_.driver_initialized.load()) {
      if (wifi_.init_task_running.load()) {
        return true;
      }
      esp_event_handler_unregister(
          WIFI_EVENT, ESP_EVENT_ANY_ID, WifiEventHandler);
      esp_event_handler_unregister(
          IP_EVENT, IP_EVENT_STA_GOT_IP, WifiGotIpEventHandler);
      esp_wifi_deinit();
      if (wifi_.netif != nullptr) {
        esp_netif_destroy_default_wifi(wifi_.netif);
        wifi_.netif = nullptr;
      }
      if (wifi_.hosted_bridge_initialized.exchange(false)) {
        esp_hosted_deinit();
      }
      wifi_.scan_running.store(false);
      wifi_.scan_task_running.store(false);
      wifi_.connect_task_running.store(false);
      wifi_.running.store(false);
      wifi_.connected.store(false);
      wifi_.got_ip.store(false);
      return driver_.SetEsp32c6PowerEnabled(false);
    }

    if (wifi_.scan_running.load() || wifi_.scan_task_running.load()) {
      const esp_err_t scan_result = esp_wifi_scan_stop();
      if (scan_result != ESP_OK && scan_result != ESP_ERR_WIFI_NOT_STARTED &&
          scan_result != ESP_ERR_INVALID_STATE &&
          scan_result != ESP_ERR_WIFI_STATE) {
        SetWifiFailure(scan_result);
        driver_.SetEsp32c6PowerEnabled(false);
        return false;
      }
    }
    esp_wifi_disconnect();
    wifi_config_t empty_config = {};
    esp_wifi_set_config(WIFI_IF_STA, &empty_config);
    esp_err_t result = esp_wifi_stop();
    if (result != ESP_OK && result != ESP_ERR_WIFI_NOT_STARTED) {
      SetWifiFailure(result);
      driver_.SetEsp32c6PowerEnabled(false);
      return false;
    }

    result = esp_wifi_set_mode(WIFI_MODE_NULL);
    if (result != ESP_OK) {
      SetWifiFailure(result);
      driver_.SetEsp32c6PowerEnabled(false);
      return false;
    }

    esp_event_handler_unregister(
        WIFI_EVENT, ESP_EVENT_ANY_ID, WifiEventHandler);
    esp_event_handler_unregister(
        IP_EVENT, IP_EVENT_STA_GOT_IP, WifiGotIpEventHandler);
    result = esp_wifi_deinit();
    if (result != ESP_OK && result != ESP_ERR_WIFI_NOT_INIT) {
      SetWifiFailure(result);
      driver_.SetEsp32c6PowerEnabled(false);
      return false;
    }
    wifi_.driver_initialized.store(false);
    if (wifi_.netif != nullptr) {
      esp_netif_destroy_default_wifi(wifi_.netif);
      wifi_.netif = nullptr;
    }
    if (wifi_.hosted_bridge_initialized.exchange(false)) {
      result = static_cast<esp_err_t>(esp_hosted_deinit());
      if (result != ESP_OK) {
        SetWifiFailure(result);
        driver_.SetEsp32c6PowerEnabled(false);
        return false;
      }
    }

    wifi_.running.store(false);
    wifi_.connect_task_running.store(false);
    wifi_.connected.store(false);
    wifi_.got_ip.store(false);
    wifi_.start_failed.store(false);
    wifi_.last_error.store(ESP_OK);
    wifi_.disconnect_reason.store(0);
    wifi_.retry_count.store(0);
    wifi_.scan_running.store(false);
    wifi_.scan_task_running.store(false);
    wifi_.scan_failed.store(false);
    wifi_.scan_network_count.store(0);
    wifi_.scan_generation.fetch_add(1);
    wifi_.ip_address.store(0);
    wifi_.netmask.store(0);
    wifi_.gateway.store(0);
    return driver_.SetEsp32c6PowerEnabled(false);
  }

  wifi_.stop_requested.store(false);
  if (wifi_.driver_initialized.load() && wifi_.running.load()) {
    return true;
  }

  if (!driver_.SetEsp32c6PowerEnabled(true)) {
    SetWifiFailure(ESP_FAIL);
    return false;
  }

  bool expected = false;
  if (!wifi_.init_task_running.compare_exchange_strong(expected, true)) {
    return true;
  }

  wifi_.start_failed.store(false);
  wifi_.last_error.store(ESP_OK);
  const BaseType_t result = xTaskCreate(WifiInitTaskEntry, "wifi_init",
      kWifiInitTaskStackBytes, this, kWifiInitTaskPriority, nullptr);
  if (result != pdPASS) {
    SetWifiFailure(ESP_ERR_NO_MEM);
    driver_.SetEsp32c6PowerEnabled(false);
    return false;
  }
  return true;
}

bool TDisplayP4Device::StartWifiScan() {
  wifi_.stop_requested.store(false);
  if (!wifi_.driver_initialized.load()) {
    return SetWifiEnabled(true);
  }

  bool expected = false;
  if (!wifi_.scan_task_running.compare_exchange_strong(expected, true)) {
    return true;
  }

  wifi_.scan_failed.store(false);
  wifi_.last_error.store(ESP_OK);
  wifi_.scan_running.store(true);
  const BaseType_t result = xTaskCreate(WifiScanTaskEntry, "wifi_scan",
      kWifiScanTaskStackBytes, this, kWifiScanTaskPriority, nullptr);
  if (result != pdPASS) {
    wifi_.scan_task_running.store(false);
    wifi_.scan_running.store(false);
    wifi_.scan_failed.store(true);
    wifi_.last_error.store(ESP_ERR_NO_MEM);
    wifi_.scan_generation.fetch_add(1);
    return false;
  }
  return true;
}

bool TDisplayP4Device::ReadWifiScanStatus(WifiScanStatus* status) {
  if (status == nullptr) {
    return false;
  }

  *status = WifiScanStatus();
  status->scan_running = wifi_.scan_running.load();
  if (wifi_.scan_results_mutex != nullptr) {
    xSemaphoreTake(wifi_.scan_results_mutex, portMAX_DELAY);
  }
  status->scan_failed = wifi_.scan_failed.load();
  status->last_error = wifi_.last_error.load();
  status->generation = wifi_.scan_generation.load();
  status->network_count =
      std::min(wifi_.scan_network_count.load(), kMaxWifiScanNetworkCount);
  for (size_t i = 0; i < status->network_count; ++i) {
    status->networks[i] = wifi_.scan_networks[i];
  }
  if (wifi_.scan_results_mutex != nullptr) {
    xSemaphoreGive(wifi_.scan_results_mutex);
  }
  return true;
}

bool TDisplayP4Device::ConnectWifi(
    const char* ssid, const char* password) {
  if (ssid == nullptr || ssid[0] == '\0' || wifi_.stop_requested.load()) {
    return false;
  }

  if (!wifi_.driver_initialized.load()) {
    if (!SetWifiEnabled(true)) {
      return false;
    }
    return false;
  }

  bool expected = false;
  if (!wifi_.connect_task_running.compare_exchange_strong(expected, true)) {
    return false;
  }

  std::snprintf(wifi_.connect_ssid, sizeof(wifi_.connect_ssid), "%s", ssid);
  std::snprintf(wifi_.connect_password, sizeof(wifi_.connect_password), "%s",
      password == nullptr ? "" : password);
  wifi_.connect_cancel_requested.store(false);
  const BaseType_t result = xTaskCreate(WifiConnectTaskEntry, "wifi_connect",
      kWifiConnectTaskStackBytes, this, kWifiConnectTaskPriority, nullptr);
  if (result != pdPASS) {
    wifi_.connect_task_running.store(false);
    SetWifiFailure(ESP_ERR_NO_MEM);
    return false;
  }
  return true;
}

bool TDisplayP4Device::CancelWifiConnection() {
  wifi_.connect_cancel_requested.store(true);
  wifi_.connect_task_running.store(false);
  StopWifiInternetCheck();
  if (!wifi_.driver_initialized.load()) {
    wifi_.connected.store(false);
    wifi_.got_ip.store(false);
    wifi_.start_failed.store(false);
    wifi_.last_error.store(ESP_OK);
    return true;
  }

  esp_wifi_disconnect();
  wifi_config_t empty_config = {};
  esp_wifi_set_config(WIFI_IF_STA, &empty_config);

  wifi_.connected.store(false);
  wifi_.got_ip.store(false);
  wifi_.start_failed.store(false);
  wifi_.last_error.store(ESP_OK);
  wifi_.disconnect_reason.store(0);
  wifi_.retry_count.store(0);
  wifi_.ip_address.store(0);
  wifi_.netmask.store(0);
  wifi_.gateway.store(0);
  wifi_time_test_.synced.store(false);
  wifi_time_test_.sntp_unix_time.store(0);
  wifi_time_test_.sntp_sync_monotonic_ms.store(0);
  return true;
}

bool TDisplayP4Device::RequestWifiInternetCheck() {
  if (!wifi_.driver_initialized.load() || !wifi_.got_ip.load() ||
      wifi_time_test_.active.load()) {
    return false;
  }

  wifi_time_test_.synced.store(false);
  wifi_time_test_.sntp_unix_time.store(0);
  wifi_time_test_.sntp_sync_monotonic_ms.store(0);
  if (!wifi_time_test_.sync_started.load() || !esp_sntp_enabled()) {
    return StartWifiSntp() == ESP_OK;
  }
  if (StartWifiSntpAttemptTimer() != ESP_OK || !esp_sntp_restart()) {
    StopWifiInternetCheck();
    return false;
  }
  return true;
}

void TDisplayP4Device::StopWifiInternetCheck() {
  wifi_time_test_.sync_started.store(false);
  wifi_time_test_.sntp_attempt_count.store(0);
  if (wifi_time_test_.sntp_attempt_timer != nullptr &&
      esp_timer_is_active(wifi_time_test_.sntp_attempt_timer)) {
    esp_timer_stop(wifi_time_test_.sntp_attempt_timer);
  }
  esp_sntp_set_time_sync_notification_cb(nullptr);
  TDisplayP4Device* owner = this;
  WifiTimeSyncOwner().compare_exchange_strong(owner, nullptr);
  if (esp_sntp_enabled()) {
    esp_sntp_stop();
  }
}

bool TDisplayP4Device::StartWifiTimeTest() {
  wifi_.stop_requested.store(false);
  wifi_time_test_.requested.store(true);
  if (!wifi_.driver_initialized.load()) {
    return SetWifiEnabled(true);
  }

  const int result = StartWifiTimeTestInternal();
  if (result != ESP_OK) {
    SetWifiFailure(result);
    LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
        "WiFi time test start failed: %s (%#X)\n",
        esp_err_to_name(static_cast<esp_err_t>(result)),
        static_cast<unsigned>(result));
    return false;
  }
  return true;
}

bool TDisplayP4Device::StopWifiTimeTest() {
  wifi_time_test_.requested.store(false);
  const bool was_active = wifi_time_test_.active.exchange(false);
  if (!wifi_.driver_initialized.load()) {
    return true;
  }
  if (!was_active && !wifi_time_test_.sync_started.load()) {
    return true;
  }

  StopWifiInternetCheck();
  wifi_time_test_.synced.store(false);
  wifi_time_test_.sntp_unix_time.store(0);
  wifi_time_test_.sntp_sync_monotonic_ms.store(0);
  wifi_.start_failed.store(false);
  wifi_.last_error.store(ESP_OK);
  wifi_.disconnect_reason.store(0);
  wifi_.retry_count.store(0);

  esp_wifi_disconnect();
  wifi_.connected.store(false);
  wifi_.got_ip.store(false);
  wifi_.ip_address.store(0);
  wifi_.netmask.store(0);
  wifi_.gateway.store(0);

  wifi_config_t empty_config = {};
  esp_wifi_set_storage(WIFI_STORAGE_RAM);
  esp_wifi_set_config(WIFI_IF_STA, &empty_config);

  if (wifi_time_test_.previous_sta_config_valid) {
    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_set_storage(WIFI_STORAGE_RAM);
    esp_wifi_set_config(WIFI_IF_STA, &wifi_time_test_.previous_sta_config);
  }

  if (wifi_time_test_.previous_mode_valid) {
    esp_wifi_set_mode(wifi_time_test_.previous_mode);
  } else {
    esp_wifi_set_mode(WIFI_MODE_NULL);
  }

  if (wifi_time_test_.previous_running) {
    const esp_err_t start_result = esp_wifi_start();
    if (start_result != ESP_OK) {
      SetWifiFailure(start_result);
      return false;
    }
    wifi_.running.store(true);
    if (wifi_time_test_.previous_connected) {
      wifi_.connect_task_running.store(true);
      const esp_err_t connect_result = esp_wifi_connect();
      if (connect_result != ESP_OK) {
        wifi_.connect_task_running.store(false);
        SetWifiFailure(connect_result);
        return false;
      }
    }
  } else {
    const esp_err_t stop_result = esp_wifi_stop();
    if (stop_result != ESP_OK && stop_result != ESP_ERR_WIFI_NOT_STARTED) {
      SetWifiFailure(stop_result);
      return false;
    }
    wifi_.running.store(false);
    wifi_.connected.store(false);
    wifi_.got_ip.store(false);
    wifi_.ip_address.store(0);
    wifi_.netmask.store(0);
    wifi_.gateway.store(0);
  }

  wifi_time_test_.previous_running = false;
  wifi_time_test_.previous_connected = false;
  wifi_time_test_.previous_mode_valid = false;
  wifi_time_test_.previous_sta_config_valid = false;
  wifi_time_test_.previous_mode = WIFI_MODE_NULL;
  wifi_time_test_.previous_sta_config = {};
  return true;
}

bool TDisplayP4Device::ReadWifiStatus(WifiStatus* status) {
  if (status == nullptr) {
    return false;
  }

  *status = WifiStatus();
  status->init_task_running = wifi_.init_task_running.load();
  status->connect_task_running = wifi_.connect_task_running.load();
  status->driver_initialized = wifi_.driver_initialized.load();
  status->running = wifi_.running.load();
  status->connected = wifi_.connected.load();
  status->got_ip = wifi_.got_ip.load();
  status->start_failed = wifi_.start_failed.load();
  status->time_test_running = wifi_time_test_.active.load();
  status->time_sync_started = wifi_time_test_.sync_started.load();
  status->retry_count = wifi_.retry_count.load();
  status->last_error = wifi_.last_error.load();
  status->disconnect_reason = wifi_.disconnect_reason.load();
  status->rssi = wifi_.rssi.load();
  status->channel = wifi_.channel.load();
  status->mac_address = wifi_.mac_address.load();
  status->ip_address = wifi_.ip_address.load();
  status->netmask = wifi_.netmask.load();
  status->gateway = wifi_.gateway.load();
  status->connection_generation = wifi_.connection_generation.load();

  if (status->time_test_running) {
    std::strncpy(status->ssid, kFactoryWifiSsid, sizeof(status->ssid) - 1);
  }

  if (status->connected) {
    wifi_ap_record_t ap_info = {};
    if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
      std::memcpy(status->ssid, ap_info.ssid,
          std::min(sizeof(status->ssid) - 1, sizeof(ap_info.ssid)));
      status->rssi = ap_info.rssi;
      status->channel = ap_info.primary;
      wifi_.rssi.store(status->rssi);
      wifi_.channel.store(status->channel);
    }
  }

  const int64_t synced_unix_time = wifi_time_test_.sntp_unix_time.load();
  status->time_synced = wifi_time_test_.synced.load() &&
                        synced_unix_time > kWifiValidUnixTimeThreshold;
  status->unix_time = status->time_synced ? synced_unix_time : 0;
  const int64_t sync_monotonic_ms =
      wifi_time_test_.sntp_sync_monotonic_ms.load();
  if (status->time_synced && sync_monotonic_ms > 0) {
    const int64_t elapsed_ms = esp_timer_get_time() / 1000 - sync_monotonic_ms;
    if (elapsed_ms > 0) {
      status->time_sync_age_s = static_cast<uint32_t>(elapsed_ms / 1000);
    }
  }
  return true;
}

bool TDisplayP4Device::EnsureSdCardMounted() {
  if (IsSdCardMounted()) {
    return true;
  }

  const bool result = driver_.InitSdmmc(device::sd::kBasePath, SDMMC_FREQ_52M);
  if (!result) {
    LogMessage(LogLevel::kWarning, __FILE__, __LINE__, "InitSdmmc failed\n");
    return false;
  }
  return IsSdCardMounted();
}

bool TDisplayP4Device::UnmountSdCard() { return driver_.DeinitSdmmc(); }

bool TDisplayP4Device::IsSdCardMounted() const {
  if (!driver_.IsSdmmcReady()) {
    return false;
  }
  struct stat info = {};
  return stat(device::sd::kBasePath, &info) == 0 && S_ISDIR(info.st_mode);
}

const char* TDisplayP4Device::SdCardBasePath() const {
  return device::sd::kBasePath;
}

bool TDisplayP4Device::StartUsbStorage() {
  if (!driver_.SetUsbHostPowerEnabled(true)) {
    return false;
  }
  return usb_storage_manager_.Start();
}

bool TDisplayP4Device::StopUsbStorage() {
  // USB PHY 供电保持开启可避免 ESP32-P4 产生约 20 mA 的额外功耗。
  return usb_storage_manager_.Stop();
}

bool TDisplayP4Device::ReadUsbStorageSnapshot(
    UsbStorageSnapshot* snapshot) const {
  return usb_storage_manager_.ReadSnapshot(snapshot);
}

bool TDisplayP4Device::RegisterScreenDisplayCallbacks(
    const ScreenProviderDisplayCallbacks& callbacks) {
  if (!driver_.IsScreenReady()) {
    LogMessage(LogLevel::kError, __FILE__, __LINE__,
        "Screen is not ready for display callback registration\n");
    return false;
  }

  display_callbacks_ = callbacks;

  esp_lcd_dpi_panel_event_callbacks_t panel_callbacks = {
      .on_color_trans_done = [](esp_lcd_panel_handle_t,
                                 esp_lcd_dpi_panel_event_data_t*,
                                 void* user_context) -> bool {
        const auto* display_callbacks =
            static_cast<const ScreenProviderDisplayCallbacks*>(user_context);
        if (display_callbacks != nullptr &&
            display_callbacks->flush_ready_callback != nullptr) {
          display_callbacks->flush_ready_callback(
              display_callbacks->callback_context);
        }
        return false;
      },
      .on_refresh_done = [](esp_lcd_panel_handle_t,
                             esp_lcd_dpi_panel_event_data_t*,
                             void* user_context) -> bool {
        const auto* display_callbacks =
            static_cast<const ScreenProviderDisplayCallbacks*>(user_context);
        if (display_callbacks != nullptr &&
            display_callbacks->refresh_done_callback != nullptr) {
          display_callbacks->refresh_done_callback(
              display_callbacks->callback_context);
        }
        return false;
      },
  };

  const auto screen_bus = driver_.bus().screen_mipi_bus;
  if (screen_bus == nullptr) {
    LogMessage(
        LogLevel::kError, __FILE__, __LINE__, "Screen MIPI bus is empty\n");
    return false;
  }

  esp_lcd_panel_handle_t panel = screen_bus->device_handle();
  if (panel == nullptr) {
    LogMessage(
        LogLevel::kError, __FILE__, __LINE__, "Screen panel handle is empty\n");
    return false;
  }

  const int result = esp_lcd_dpi_panel_register_event_callbacks(
      panel, &panel_callbacks, &display_callbacks_);
  if (result != 0) {
    LogMessage(LogLevel::kError, __FILE__, __LINE__,
        "esp_lcd_dpi_panel_register_event_callbacks failed: %s (%#X)\n",
        esp_err_to_name(static_cast<esp_err_t>(result)),
        static_cast<unsigned>(result));
    return false;
  }
  return true;
}

bool TDisplayP4Device::WriteScreenPixels(
    int x_start, int y_start, int x_end, int y_end, const void* pixels) {
  if (!driver_.IsScreenReady()) {
    return false;
  }

  switch (driver_.screen_type()) {
    case device::ScreenType::kHi8561:
      return driver_.chip().hi8561->SendColorStreamCoordinate(
          x_start, y_start, x_end, y_end, pixels);
    case device::ScreenType::kRm69a10:
      return driver_.chip().rm69a10->SendColorStreamCoordinate(
          x_start, y_start, x_end, y_end, pixels);
    default:
      break;
  }
  return false;
}

bool TDisplayP4Device::ReadScreenTouch(TouchPoint* point) {
  if (point == nullptr) {
    return false;
  }
  *point = TouchPoint();

  if (!driver_.IsTouchReady()) {
    return false;
  }

  // 亮屏轮询也需要清除 XL9535 的汇总中断锁存，确保后续边沿可继续上报。
  const bool touch_interrupt_received = ConsumeTouchInterrupt();

  cpp_bus_driver::TouchFrame frame;
  cpp_bus_driver::TouchReadStatus read_status =
      cpp_bus_driver::TouchReadStatus::kInvalidData;
  switch (driver_.screen_type()) {
    case device::ScreenType::kHi8561:
      read_status = driver_.chip().hi8561_touch->ReadPrimaryTouch(&frame);
      break;
    case device::ScreenType::kRm69a10:
      read_status = driver_.chip().gt9895->ReadPrimaryTouch(&frame);
      break;
    default:
      return false;
  }

  if (driver_.screen_type() == device::ScreenType::kHi8561 &&
      frame.gesture == static_cast<uint8_t>(
          cpp_bus_driver::Hi8561Touch::Gesture::kDoubleTap)) {
    point->x = -1;
    point->y = -1;
    point->gesture = TouchGesture::kDoubleTap;
    return true;
  }
  // GT9895 在物理左右边缘可能先产生硬件中断而暂不提供有效坐标。
  // 该中断只作为应用层边缘手势候选提示，不能单独触发返回操作。
  // GT9895 的 INT 是通用触摸通知，不能把每次中断都解释为边缘触摸。
  // 只有收到中断但固件暂时没有提供坐标时，才将其作为边缘候选提示；
  // 普通有效坐标必须继续交给 LVGL，否则屏幕边缘控件会全部失效。
  const bool hardware_edge_hint = frame.edge_touch ||
      (driver_.screen_type() == device::ScreenType::kRm69a10 &&
          touch_interrupt_received &&
          (read_status == cpp_bus_driver::TouchReadStatus::kNoData ||
              (read_status == cpp_bus_driver::TouchReadStatus::kSuccess &&
                  frame.contact_count == 0)));
  if (read_status != cpp_bus_driver::TouchReadStatus::kSuccess) {
    if (!hardware_edge_hint ||
        read_status != cpp_bus_driver::TouchReadStatus::kNoData) {
      return false;
    }
    SetHardwareEdgeTouchPoint(point);
    return true;
  }
  if (frame.contact_count == 0) {
    if (!hardware_edge_hint) {
      return false;
    }
    SetHardwareEdgeTouchPoint(point);
    return true;
  }

  const cpp_bus_driver::TouchContact& contact = frame.contacts[0];
  point->id = contact.id;
  point->x = contact.x;
  point->y = contact.y;
  point->pressure =
      static_cast<uint8_t>(std::min<uint16_t>(contact.pressure, UINT8_MAX));
  point->edge_touch_flag = frame.edge_touch;
  return true;
}

bool TDisplayP4Device::ReadScreenTouchPoints(
    TouchPoint* points, size_t max_points, size_t* point_count) {
  if (point_count != nullptr) {
    *point_count = 0;
  }
  if (points == nullptr || max_points == 0 || point_count == nullptr) {
    return false;
  }

  if (!driver_.IsTouchReady()) {
    return false;
  }

  // 亮屏轮询也需要清除 XL9535 的汇总中断锁存，确保后续边沿可继续上报。
  const bool touch_interrupt_received = ConsumeTouchInterrupt();

  cpp_bus_driver::TouchFrame frame;
  cpp_bus_driver::TouchReadStatus read_status =
      cpp_bus_driver::TouchReadStatus::kInvalidData;
  switch (driver_.screen_type()) {
    case device::ScreenType::kHi8561:
      read_status = driver_.chip().hi8561_touch->ReadTouchFrame(&frame);
      break;
    case device::ScreenType::kRm69a10:
      read_status = driver_.chip().gt9895->ReadTouchFrame(&frame);
      break;
    default:
      return false;
  }

  // GT9895 的通用触摸中断只有在缺少有效坐标时才可作为边缘候选提示。
  // 有效触摸不能携带这个提示，避免全局手势抢占边缘页面控件。
  const bool hardware_edge_hint = frame.edge_touch ||
      (driver_.screen_type() == device::ScreenType::kRm69a10 &&
          touch_interrupt_received &&
          (read_status == cpp_bus_driver::TouchReadStatus::kNoData ||
              (read_status == cpp_bus_driver::TouchReadStatus::kSuccess &&
                  frame.contact_count == 0)));
  if (read_status != cpp_bus_driver::TouchReadStatus::kSuccess) {
    if (!hardware_edge_hint ||
        read_status != cpp_bus_driver::TouchReadStatus::kNoData) {
      return false;
    }
    SetHardwareEdgeTouchPoint(&points[0]);
    *point_count = 1;
    return true;
  }
  const size_t count = std::min<size_t>(max_points, frame.contact_count);
  for (size_t i = 0; i < count; ++i) {
    const cpp_bus_driver::TouchContact& contact = frame.contacts[i];
    points[i].id = contact.id;
    points[i].x = contact.x;
    points[i].y = contact.y;
    points[i].pressure =
        static_cast<uint8_t>(std::min<uint16_t>(contact.pressure, UINT8_MAX));
    points[i].edge_touch_flag = frame.edge_touch;
  }
  *point_count = count;
  if (*point_count == 0 && hardware_edge_hint) {
    SetHardwareEdgeTouchPoint(&points[0]);
    *point_count = 1;
  }
  return *point_count > 0;
}

/**
 * @brief 判断当前屏幕及显示方向是否支持硬件边缘触摸提示
 * @param display_rotation_angle 显示旋转角度
 * @return 当前屏幕及方向支持硬件提示时返回 true，否则返回 false
 */
bool TDisplayP4Device::SupportsHardwareEdgeTouchHint(
    int display_rotation_angle) const {
  switch (driver_.screen_type()) {
    case device::ScreenType::kHi8561:
      // HI8561 会在物理上下左右四边上报专用边缘触摸标记，
      // 因此软件旋转到任意方向时都可将其作为手势候选提示。
      return true;
    case device::ScreenType::kRm69a10:
      // GT9895 当前固件只在物理左右边缘存在坐标抑制。0°/180°
      // 竖屏时应用的返回边缘与其重合；90°/270° 横屏时应用左右边
      // 对应物理上下边，不得使用这个提示，否则普通中断可能被误判。
      return display_rotation_angle == 0 || display_rotation_angle == 180;
    default:
      return false;
  }
}

bool TDisplayP4Device::SupportsTouchInterrupt() const {
  return touch_interrupt_initialized_;
}

bool TDisplayP4Device::ConsumeTouchInterrupt() {
  if (!touch_interrupt_initialized_ ||
      !touch_interrupt_pending_.exchange(false, std::memory_order_relaxed)) {
    return false;
  }
  if (!driver_.IsXl9535Ready() || driver_.chip().xl9535 == nullptr) {
    return false;
  }

  // GPIO5 是 XL9535 的汇总中断，芯片没有独立的中断源状态寄存器。任务先
  // 清除两个输入端口的锁存，再由触摸报告内容确认这次通知是否属于触摸。
  if (!driver_.chip().xl9535->ClearIrqFlag()) {
    LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
        "Clear XL9535 interrupt failed\n");
    return false;
  }
  return true;
}

bool TDisplayP4Device::ReadHapticWaveformCount(uint8_t* waveform_count) {
  if (waveform_count != nullptr) {
    *waveform_count = 0;
  }
  if (!driver_.IsAw86224Ready() && !driver_.InitAw86224()) {
    LogMessage(
        LogLevel::kWarning, __FILE__, __LINE__, "Aw86224 init retry failed\n");
    return false;
  }
  const auto info = cpp_bus_driver::Aw862xx::GetRamWaveformInfo(
      cpp_bus_driver::Aw862xx::RamWaveformLibrary::kRam12k041230_235);
  if (waveform_count != nullptr) {
    *waveform_count = info.waveform_count;
  }
  return info.waveform_count > 0;
}

bool TDisplayP4Device::PlayHapticWaveform(uint8_t waveform_sequence_number,
    uint8_t loop_count, uint8_t gain, bool auto_brake) {
  haptic_.waveform_sequence_number.store(waveform_sequence_number);
  haptic_.loop_count.store(std::clamp<uint8_t>(loop_count, 1, 16));
  haptic_.gain.store(gain);
  haptic_.auto_brake.store(auto_brake);

  const uint32_t now_ms = static_cast<uint32_t>(xTaskGetTickCount() *
      portTICK_PERIOD_MS);
  const uint32_t last_preview_ms = haptic_.last_preview_ms.load();
  if (haptic_.running.load() ||
      now_ms - last_preview_ms < kVibrationPreviewMinIntervalMs) {
    return true;
  }
  haptic_.last_preview_ms.store(now_ms);

  bool expected = false;
  if (!haptic_.running.compare_exchange_strong(expected, true)) {
    return true;
  }

  const BaseType_t result = xTaskCreate(HapticPlaybackTaskEntry,
      "haptic_play", kSpeakerPlaybackTaskStackBytes, this,
      kSpeakerPlaybackTaskPriority, nullptr);
  if (result != pdPASS) {
    haptic_.running.store(false);
    return false;
  }
  return true;
}

bool TDisplayP4Device::PlaySpeakerTone(size_t* bytes_written) {
  if (bytes_written != nullptr) {
    *bytes_written = 0;
  }

  if (!Configure(kSpeakerPlaybackSampleRateHz,
          kSpeakerPlaybackChannelCount, kSpeakerPlaybackBitsPerSample)) {
    LogMessage(
        LogLevel::kWarning, __FILE__, __LINE__, "Es8311 init retry failed\n");
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
      "ES8311 speaker playback: bytes=%u, sample_rate=%u, channels=%u, "
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
        driver_.chip().es8311->WriteI2s(audio_data + total_written, write_size);
    if (written == 0) {
      LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
          "ES8311 WriteI2s failed, written=%u/%u\n",
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

  if (!TryAcquireAuxiliaryAudioOutput(
          AuxiliaryAudioOutput::kSpeakerTone)) {
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
  if (!TryAcquireAuxiliaryAudioOutput(
          AuxiliaryAudioOutput::kSpeakerTone)) {
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
  if (speaker_.playback_kind.load() !=
      SpeakerState::PlaybackKind::kToneLoop) {
    return false;
  }
  speaker_.stop_requested.store(true);
  speaker_.loop_enabled.store(false);
  return true;
}

bool TDisplayP4Device::SetSpeakerVolumePercent(int percent) {
  if (!driver_.IsEs8311Ready() && !driver_.InitEs8311()) {
    LogMessage(
        LogLevel::kWarning, __FILE__, __LINE__, "Es8311 init failed\n");
    return false;
  }

  const uint8_t volume = PercentToUint8Value(percent, kAudioVolumeMax);
  const bool result = driver_.chip().es8311->SetDacVolume(volume);
  UpdateAudioCodecOperatingMode();
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

bool TDisplayP4Device::StartAudioFile(
    const char* path, uint32_t duration_ms) {
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
  std::snprintf(speaker_.audio_file_path,
      sizeof(speaker_.audio_file_path), "%s", path);
  speaker_.loop_enabled.store(false);
  speaker_.stop_requested.store(false);
  speaker_.paused.store(false);
  speaker_.elapsed_ms.store(0);
  speaker_.duration_ms.store(duration_ms);
  speaker_.seek_requested.store(false);
  speaker_.seek_position_ms.store(0);
  speaker_.file_state.store(AudioFilePlaybackState::kPlaying);
  speaker_.playback_kind.store(SpeakerState::PlaybackKind::kAudioFile);

  const BaseType_t result = xTaskCreate(SpeakerPlaybackTaskEntry,
      "audio_file", kAudioFilePlaybackTaskStackBytes, this,
      kSpeakerPlaybackTaskPriority, nullptr);
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
      speaker_.playback_kind.load() !=
          SpeakerState::PlaybackKind::kAudioFile ||
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
      speaker_.playback_kind.load() !=
          SpeakerState::PlaybackKind::kAudioFile ||
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
      speaker_.playback_kind.load() !=
          SpeakerState::PlaybackKind::kAudioFile) {
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
  if (speaker_.playback_kind.load() !=
      SpeakerState::PlaybackKind::kAudioFile) {
    return false;
  }
  speaker_.stop_requested.store(true);
  if (!speaker_.tone_overlay_running.load()) {
    speaker_.paused.store(false);
  }
  speaker_.file_state.store(AudioFilePlaybackState::kStopped);
  return true;
}

bool TDisplayP4Device::ReadAudioFileStatus(
    AudioFilePlaybackStatus* status) {
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
  if (speaker_.playback_kind.load() ==
      SpeakerState::PlaybackKind::kAudioFile) {
    const audio::Mp3PlaybackResult result =
        audio::PlayMp3File(speaker_.audio_file_path, this);
    const bool completed = result == audio::Mp3PlaybackResult::kCompleted;
    const bool stopped = result == audio::Mp3PlaybackResult::kStopped;
    speaker_.paused.store(false);
    speaker_.stop_requested.store(false);
    speaker_.seek_requested.store(false);
    speaker_.file_state.store(completed
                                  ? AudioFilePlaybackState::kCompleted
                                  : (stopped
                                            ? AudioFilePlaybackState::kStopped
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
  } while (speaker_.loop_enabled.load() &&
           !speaker_.stop_requested.load());
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
  if (speaker_.playback_kind.load() !=
          SpeakerState::PlaybackKind::kAudioFile ||
      speaker_.file_state.load() != AudioFilePlaybackState::kPaused) {
    return false;
  }

  if (!TryAcquireAuxiliaryAudioOutput(
          AuxiliaryAudioOutput::kSpeakerTone)) {
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
      !pause_ready || Configure(paused_sample_rate_hz,
                          kSpeakerPlaybackChannelCount,
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
  using OperatingMode =
      lilygo_device_driver::TDisplayP4Driver::Es8311OperatingMode;
  const bool playback_active = speaker_.running.load();
  const bool capture_active = microphone_.running.load();
  OperatingMode mode = OperatingMode::kSleep;
  if (playback_active && capture_active) {
    mode = OperatingMode::kDuplex;
  } else if (playback_active) {
    mode = OperatingMode::kPlayback;
  } else if (capture_active) {
    mode = microphone_.adc_to_dac_enabled.load()
               ? OperatingMode::kDuplex
               : OperatingMode::kCapture;
  }
  return driver_.SetEs8311OperatingMode(mode);
}

bool TDisplayP4Device::Configure(uint32_t sample_rate_hz,
    uint8_t channel_count, uint8_t bits_per_sample) {
  if ((channel_count != 1 && channel_count != 2) ||
      bits_per_sample != kSpeakerPlaybackBitsPerSample) {
    return false;
  }
  const bool codec_was_ready = driver_.IsEs8311Ready();
  if (!UpdateAudioCodecOperatingMode()) {
    return false;
  }
  if (codec_was_ready && speaker_.sample_rate_hz.load() == sample_rate_hz) {
    return true;
  }

  // ESP-IDF 只允许在 I2S 通道禁用时重配标准模式时钟。
  if (!driver_.chip().es8311->SetI2sChannelEnable(false)) {
    return false;
  }
  const bool clock_reconfigured =
      driver_.chip().es8311->SetClockReconfig(
          device::es8311::kMclkMultiple, sample_rate_hz);
  const bool channels_restored =
      driver_.chip().es8311->SetI2sChannelEnable(true);
  if (!clock_reconfigured || !channels_restored) {
    return false;
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
  if (data == nullptr || size == 0 || !driver_.IsEs8311Ready()) {
    return false;
  }
  size_t total_written = 0;
  while (total_written < size) {
    if (!WaitUntilReady()) {
      return false;
    }
    const size_t write_size =
        std::min(kSpeakerPlaybackChunkBytes, size - total_written);
    const size_t written = driver_.chip().es8311->WriteI2s(
        data + total_written, write_size);
    if (written == 0) {
      return false;
    }
    total_written += written;
  }
  return true;
}

void TDisplayP4Device::UpdateProgress(uint32_t elapsed_ms) {
  const uint32_t duration_ms = speaker_.duration_ms.load();
  speaker_.elapsed_ms.store(duration_ms == 0
                                ? elapsed_ms
                                : std::min(elapsed_ms, duration_ms));
}

void TDisplayP4Device::HapticPlaybackTaskEntry(void* context) {
  auto* self = static_cast<TDisplayP4Device*>(context);
  if (self != nullptr) {
    self->RunHapticPlaybackTask();
  }
  vTaskDelete(nullptr);
}

void TDisplayP4Device::RunHapticPlaybackTask() {
  if (!driver_.IsAw86224Ready() && !driver_.InitAw86224()) {
    LogMessage(
        LogLevel::kWarning, __FILE__, __LINE__, "Aw86224 init retry failed\n");
    haptic_.running.store(false);
    return;
  }

  const uint8_t sequence = haptic_.waveform_sequence_number.load();
  const uint8_t loop_count = haptic_.loop_count.load();
  const uint8_t gain = haptic_.gain.load();
  const bool auto_brake = haptic_.auto_brake.load();
  LogMessage(LogLevel::kDebug, __FILE__, __LINE__,
      "Aw86224 vibration playback: sequence=%u loop=%u gain=%u auto_brake=%u\n",
      static_cast<unsigned int>(sequence),
      static_cast<unsigned int>(loop_count), static_cast<unsigned int>(gain),
      static_cast<unsigned int>(auto_brake ? 1 : 0));

  const bool needs_configure = !haptic_.ram_playback_configured ||
                               haptic_.configured_sequence_number != sequence ||
                               haptic_.configured_loop_count != loop_count ||
                               haptic_.configured_auto_brake != auto_brake;
  if (needs_configure) {
    if (!driver_.chip().aw86224->ConfigureRamPlaybackWaveform(
            sequence, loop_count - 1, gain, auto_brake)) {
      LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
          "Aw86224 ConfigureRamPlaybackWaveform failed, sequence=%u\n",
          static_cast<unsigned int>(sequence));
      driver_.SetAw86224Standby();
      haptic_.running.store(false);
      return;
    }
    haptic_.ram_playback_configured = true;
    haptic_.configured_sequence_number = sequence;
    haptic_.configured_loop_count = loop_count;
    haptic_.configured_auto_brake = auto_brake;
    haptic_.configured_gain = gain;
  } else if (haptic_.configured_gain != gain) {
    if (!driver_.chip().aw86224->SetRrtModeGain(gain)) {
      LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
          "Aw86224 SetRrtModeGain failed, gain=%u\n",
          static_cast<unsigned int>(gain));
      haptic_.running.store(false);
      return;
    }
    haptic_.configured_gain = gain;
  }

  if (!driver_.chip().aw86224->StartRamPlaybackWaveform()) {
    LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
        "Aw86224 StartRamPlaybackWaveform failed, sequence=%u\n",
        static_cast<unsigned int>(sequence));
    driver_.SetAw86224Standby();
    haptic_.running.store(false);
    return;
  }

  vTaskDelay(pdMS_TO_TICKS(kVibrationPreviewPlayMs));

  if (!driver_.SetAw86224Standby()) {
    LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
        "Aw86224 standby failed, sequence=%u\n",
        static_cast<unsigned int>(sequence));
  }

  haptic_.running.store(false);
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
        "Failed to activate the ES8311 microphone capture path\n");
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
  if (!driver_.IsEs8311Ready()) {
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

  const bool previous_enabled =
      microphone_.adc_to_dac_enabled.exchange(enable);
  if (!UpdateAudioCodecOperatingMode() || !driver_.IsEs8311Ready()) {
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

void TDisplayP4Device::HeapCapsBufferDeleter::operator()(uint8_t* pointer)
    const {
  if (pointer != nullptr) {
    heap_caps_free(pointer);
  }
}

bool TDisplayP4Device::StartCameraPreview() {
  if (camera_preview_.task_active.load() ||
      camera_preview_.running.load() || camera_preview_.initialized.load()) {
    return !camera_preview_.stop_requested.load();
  }

  camera_preview_.error.store(CameraError::kNone);
  camera_preview_.startup_in_progress.store(false);
  camera_preview_.stop_requested.store(false);
  camera_preview_.task_active.store(true);
  const BaseType_t result = xTaskCreate(CameraPreviewTaskEntry,
      "camera_preview", kCameraPreviewTaskStackBytes, this,
      kCameraPreviewTaskPriority, nullptr);
  if (result != pdPASS) {
    camera_preview_.error.store(CameraError::kPreviewTaskCreateFailed);
    LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
        "Create camera preview task failed: %ld\n",
        static_cast<long>(result));
    camera_preview_.task_active.store(false);
    camera_preview_.stop_requested.store(true);
    DeinitializeCameraPreview();
    return false;
  }
  return true;
}

CameraError TDisplayP4Device::GetCameraPreviewError() const {
  if (camera_preview_.startup_in_progress.load()) {
    return CameraError::kNone;
  }
  return camera_preview_.error.load();
}

void TDisplayP4Device::RequestCameraPreviewStop() {
  camera_preview_.stop_requested.store(true);
}

bool TDisplayP4Device::StopCameraPreview() {
  RequestCameraPreviewStop();
  // 不在这里发 VIDIOC_STREAMOFF — 让 RunCameraPreviewTask 退出时由
  // DeinitializeCameraPreview 统一处理，避免与正在运行的 DQBUF/PPA 产生 I2C
  // 竞态
  const uint32_t start_ms = static_cast<uint32_t>(
      xTaskGetTickCount() * portTICK_PERIOD_MS);
  while (camera_preview_.task_active.load()) {
    if (static_cast<uint32_t>(xTaskGetTickCount() * portTICK_PERIOD_MS) -
            start_ms >=
        kCameraStopWaitTimeoutMs) {
      LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
          "StopCameraPreview timed out\n");
      return false;
    }
    vTaskDelay(pdMS_TO_TICKS(20));
  }
  if (camera_preview_.initialized.load()) {
    DeinitializeCameraPreview();
  }
  return true;
}

bool TDisplayP4Device::GetCameraPreviewFrameInfo(
    CameraPreviewFrameInfo* info) {
  if (info == nullptr || camera_preview_.output_buffer == nullptr ||
      camera_preview_.frame_sequence.load() == 0) {
    return false;
  }

  info->data_size = camera_preview_.output_buffer_size;
  info->width = camera_preview_.output_width;
  info->height = camera_preview_.output_height;
  info->stride = camera_preview_.output_stride;
  info->bits_per_pixel = ScreenBitsPerPixel();
  info->sequence = camera_preview_.frame_sequence.load();
  return true;
}

bool TDisplayP4Device::CopyCameraPreviewFrame(uint8_t* buffer,
    size_t buffer_size, CameraPreviewFrameInfo* info) {
  if (buffer == nullptr || info == nullptr ||
      !GetCameraPreviewFrameInfo(info) || buffer_size < info->data_size ||
      camera_preview_.output_mutex == nullptr) {
    return false;
  }

  if (xSemaphoreTake(camera_preview_.output_mutex, pdMS_TO_TICKS(20)) != pdTRUE) {
    return false;
  }
  std::memcpy(buffer, camera_preview_.output_buffer.get(), info->data_size);
  info->sequence = camera_preview_.frame_sequence.load();
  xSemaphoreGive(camera_preview_.output_mutex);
  return true;
}

void TDisplayP4Device::CameraPreviewTaskEntry(void* context) {
  static_cast<TDisplayP4Device*>(context)->RunCameraPreviewTask();
}

void TDisplayP4Device::RunCameraPreviewTask() {
  if (camera_preview_.stop_requested.load() || !InitializeCameraPreview() ||
      camera_preview_.stop_requested.load()) {
    DeinitializeCameraPreview();
    camera_preview_.stop_requested.store(true);
    camera_preview_.running.store(false);
    camera_preview_.task_active.store(false);
    vTaskDelete(nullptr);
    return;
  }

  camera_preview_.running.store(true);
  while (!camera_preview_.stop_requested.load()) {
    v4l2_buffer buffer = {};
    buffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buffer.memory = V4L2_MEMORY_MMAP;
    if (ioctl(camera_preview_.video_fd, VIDIOC_DQBUF, &buffer) != 0) {
      vTaskDelay(pdMS_TO_TICKS(10));
      continue;
    }

    const bool frame_valid =
        buffer.index < kCameraBufferCount && buffer.bytesused > 0 &&
        (buffer.flags & V4L2_BUF_FLAG_DONE) != 0 &&
        (buffer.flags & V4L2_BUF_FLAG_ERROR) == 0;
    if (frame_valid) {
      if (camera_preview_.warmup_frames_remaining > 0) {
        // 传感器刚上电时丢弃少量预热帧，避免未稳定像素短暂显示。
        --camera_preview_.warmup_frames_remaining;
      } else {
        RenderCameraFrame(
            static_cast<uint8_t*>(camera_preview_.frame_buffers[buffer.index]),
            camera_preview_.frame_width, camera_preview_.frame_height);
      }
    }
    ioctl(camera_preview_.video_fd, VIDIOC_QBUF, &buffer);
    vTaskDelay(pdMS_TO_TICKS(kCameraFrameIntervalMs));
  }

  DeinitializeCameraPreview();
  camera_preview_.running.store(false);
  camera_preview_.task_active.store(false);
  vTaskDelete(nullptr);
}

bool TDisplayP4Device::WaitForCameraSensorReady(TickType_t startup_tick) {
  const auto& i2c_bus = driver_.bus().sgm38121_i2c_bus;
  if (i2c_bus == nullptr || i2c_bus->bus_handle() == nullptr) {
    camera_preview_.error.store(CameraError::kSensorNotDetected);
    LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
        "Camera sensor readiness check failed (I2C bus unavailable)\n");
    return false;
  }

  uint32_t attempt = 0;
  while (!CameraStartupTimedOut(startup_tick)) {
    if (camera_preview_.stop_requested.load()) {
      return false;
    }

    ++attempt;
    const TickType_t probe_tick = xTaskGetTickCount();
    const uint32_t remaining_ms = CameraStartupRemainingMs(startup_tick);
    const uint32_t probe_timeout_ms =
        std::min(kCameraSensorReadyPollIntervalMs, remaining_ms);
    const esp_err_t result = i2c_master_probe(i2c_bus->bus_handle(),
        kCameraSensorI2cAddress, probe_timeout_ms);
    if (result == ESP_OK) {
      LogMessage(LogLevel::kInfo, __FILE__, __LINE__,
          "Camera sensor ready (address: %#X, attempts: %lu, elapsed: %lu ms)\n",
          kCameraSensorI2cAddress, static_cast<unsigned long>(attempt),
          static_cast<unsigned long>(CameraStartupElapsedMs(startup_tick)));
      return true;
    }
    if (result != ESP_ERR_NOT_FOUND && result != ESP_ERR_TIMEOUT) {
      camera_preview_.error.store(CameraError::kSensorNotDetected);
      LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
          "Camera sensor readiness check failed (address: %#X, reason: %s, "
          "error: %#X)\n",
          kCameraSensorI2cAddress, esp_err_to_name(result),
          static_cast<unsigned>(result));
      return false;
    }

    const uint32_t probe_elapsed_ms = static_cast<uint32_t>(
        (xTaskGetTickCount() - probe_tick) * portTICK_PERIOD_MS);
    const uint32_t poll_delay_ms =
        probe_elapsed_ms < kCameraSensorReadyPollIntervalMs
            ? kCameraSensorReadyPollIntervalMs - probe_elapsed_ms
            : 0;
    const uint32_t delay_ms = std::min(
        poll_delay_ms, CameraStartupRemainingMs(startup_tick));
    if (delay_ms > 0) {
      vTaskDelay(pdMS_TO_TICKS(delay_ms));
    }
  }

  if (camera_preview_.stop_requested.load()) {
    return false;
  }
  camera_preview_.error.store(CameraError::kSensorNotDetected);
  LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
      "Camera sensor readiness check timed out (address: %#X, attempts: %lu, "
      "timeout: %lu ms)\n",
      kCameraSensorI2cAddress, static_cast<unsigned long>(attempt),
      static_cast<unsigned long>(kCameraStartupTimeoutMs));
  return false;
}

bool TDisplayP4Device::InitializeCameraPreview() {
  if (!driver_.IsScreenReady()) {
    camera_preview_.error.store(CameraError::kScreenNotReady);
    LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
        "Camera preview start failed: screen is not ready\n");
    return false;
  }

  const TickType_t startup_tick = xTaskGetTickCount();
  uint32_t attempt = 0;
  CameraError last_error = CameraError::kNone;
  camera_preview_.startup_in_progress.store(true);
  while (!camera_preview_.stop_requested.load() &&
         !CameraStartupTimedOut(startup_tick)) {
    ++attempt;
    camera_preview_.error.store(CameraError::kNone);
    const CameraStartupAttemptResult result =
        InitializeCameraPreviewAttempt(startup_tick);
    if (result == CameraStartupAttemptResult::kSuccess) {
      camera_preview_.startup_in_progress.store(false);
      if (attempt > 1) {
        LogMessage(LogLevel::kInfo, __FILE__, __LINE__,
            "Camera startup recovered (attempts: %lu, elapsed: %lu ms)\n",
            static_cast<unsigned long>(attempt),
            static_cast<unsigned long>(CameraStartupElapsedMs(startup_tick)));
      }
      return true;
    }

    last_error = camera_preview_.error.load();
    if (camera_preview_.stop_requested.load()) {
      camera_preview_.startup_in_progress.store(false);
      return false;
    }
    if (result == CameraStartupAttemptResult::kStop) {
      camera_preview_.startup_in_progress.store(false);
      return false;
    }
    if (CameraStartupTimedOut(startup_tick)) {
      break;
    }

    const DiagnosticError diagnostic_error =
        GetCameraDiagnosticError(last_error);
    LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
        "Camera startup attempt failed; retrying from power-on (attempt: %lu, "
        "error: %s, reason: %s, elapsed: %lu ms)\n",
        static_cast<unsigned long>(attempt),
        diagnostic_error.code, diagnostic_error.text,
        static_cast<unsigned long>(CameraStartupElapsedMs(startup_tick)));
    DeinitializeCameraPreview();

    const uint32_t delay_ms = std::min(
        kCameraPowerCycleOffDelayMs,
        CameraStartupRemainingMs(startup_tick));
    if (delay_ms > 0) {
      vTaskDelay(pdMS_TO_TICKS(delay_ms));
    }
  }

  if (last_error == CameraError::kNone) {
    last_error = CameraError::kSensorNotDetected;
  }
  camera_preview_.error.store(last_error);
  camera_preview_.startup_in_progress.store(false);
  const DiagnosticError diagnostic_error = GetCameraDiagnosticError(last_error);
  LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
      "Camera startup timed out (attempts: %lu, elapsed: %lu ms, error: %s, "
      "reason: %s)\n",
      static_cast<unsigned long>(attempt),
      static_cast<unsigned long>(CameraStartupElapsedMs(startup_tick)),
      diagnostic_error.code, diagnostic_error.text);
  return false;
}

TDisplayP4Device::CameraStartupAttemptResult
TDisplayP4Device::InitializeCameraPreviewAttempt(TickType_t startup_tick) {
  if (!driver_.SetCameraPowerEnabled(true)) {
    camera_preview_.error.store(CameraError::kPowerEnableFailed);
    LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
        "Camera preview start failed: power enable failed\n");
    return CameraStartupAttemptResult::kPowerCycle;
  }
  if (!WaitForCameraSensorReady(startup_tick)) {
    return CameraStartupAttemptResult::kStop;
  }

  const bool video_system_was_initialized =
      camera_preview_.video_system_initialized.load();
  bool initialized_video_system = false;
  if (!video_system_was_initialized) {
    esp_video_init_csi_config_t csi_config = {};
    csi_config.sccb_config.init_sccb = false;
    csi_config.sccb_config.i2c_handle =
        driver_.bus().sgm38121_i2c_bus->bus_handle();
    csi_config.sccb_config.freq = static_cast<uint32_t>(100000);
    csi_config.reset_pin = GPIO_NUM_NC;
    csi_config.pwdn_pin = GPIO_NUM_NC;
    csi_config.dont_init_ldo = true;

    esp_video_init_config_t camera_config = {};
    camera_config.csi = &csi_config;
    // video0 和 video20 只注册一次，避免组件反初始化后无法重复注册 VFS。
    esp_err_t result =
        esp_video_init_with_flags(&camera_config, kCameraVideoInitFlags);
    if (result != ESP_OK) {
      // 组件初始化失败时会清理当前已经创建的全部视频设备。
      camera_preview_.video_system_initialized.store(false);
      camera_preview_.error.store(CameraError::kVideoInitFailed);
      LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
          "esp_video_init_with_flags failed: %s (%#X)\n",
          esp_err_to_name(result), static_cast<unsigned>(result));
      return IsRetryableCameraVideoError(result)
                 ? CameraStartupAttemptResult::kPowerCycle
                 : CameraStartupAttemptResult::kStop;
    }
    initialized_video_system = true;
  }

  camera_preview_.video_fd = open(kCameraDeviceName, O_RDONLY | O_NONBLOCK);
  if (camera_preview_.video_fd < 0) {
    const int open_error = errno;
    if (open_error == ENOENT) {
      LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
          "Open camera video device failed (device: %s, reason: video device "
          "node is unavailable, errno: %d)\n",
          kCameraDeviceName, open_error);
    } else {
      LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
          "Open camera video device failed (device: %s, reason: %s, errno: "
          "%d)\n",
          kCameraDeviceName, std::strerror(open_error), open_error);
    }
    esp_err_t deinit_result = ESP_OK;
    if (!video_system_was_initialized) {
      deinit_result = esp_video_deinit_with_flags(kCameraVideoInitFlags);
      camera_preview_.video_system_initialized.store(false);
    }
    camera_preview_.error.store(CameraError::kVideoDeviceOpenFailed);
    if (deinit_result != ESP_OK) {
      camera_preview_.error.store(CameraError::kVideoInitFailed);
      LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
          "esp_video_deinit_with_flags failed: %s (%#X)\n",
          esp_err_to_name(deinit_result),
          static_cast<unsigned>(deinit_result));
      return CameraStartupAttemptResult::kStop;
    }
    return IsRetryableCameraIoError(open_error)
               ? CameraStartupAttemptResult::kPowerCycle
               : CameraStartupAttemptResult::kStop;
  }
  if (initialized_video_system) {
    camera_preview_.video_system_initialized.store(true);
  }

  if (video_system_was_initialized) {
    // 摄像头重新上电后，通过组件公开接口重写传感器的完整寄存器配置。
    auto& sensor_format = camera_preview_.sensor_format;
    if (ioctl(camera_preview_.video_fd, VIDIOC_G_SENSOR_FMT,
            &sensor_format) != 0) {
      const int ioctl_error = errno;
      camera_preview_.error.store(CameraError::kSensorRestoreFailed);
      LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
          "VIDIOC_G_SENSOR_FMT failed (reason: %s, errno: %d)\n",
          std::strerror(ioctl_error), ioctl_error);
      return IsRetryableCameraIoError(ioctl_error)
                 ? CameraStartupAttemptResult::kPowerCycle
                 : CameraStartupAttemptResult::kStop;
    }
    if (ioctl(camera_preview_.video_fd, VIDIOC_S_SENSOR_FMT,
            &sensor_format) != 0) {
      const int ioctl_error = errno;
      camera_preview_.error.store(CameraError::kSensorRestoreFailed);
      LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
          "VIDIOC_S_SENSOR_FMT failed (reason: %s, errno: %d)\n",
          std::strerror(ioctl_error), ioctl_error);
      return IsRetryableCameraIoError(ioctl_error)
                 ? CameraStartupAttemptResult::kPowerCycle
                 : CameraStartupAttemptResult::kStop;
    }
  }

  v4l2_format format = {};
  format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  if (ioctl(camera_preview_.video_fd, VIDIOC_G_FMT, &format) != 0) {
    const int ioctl_error = errno;
    camera_preview_.error.store(CameraError::kFormatConfigurationFailed);
    LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
        "VIDIOC_G_FMT failed (reason: %s, errno: %d)\n",
        std::strerror(ioctl_error), ioctl_error);
    return IsRetryableCameraIoError(ioctl_error)
               ? CameraStartupAttemptResult::kPowerCycle
               : CameraStartupAttemptResult::kStop;
  }
  camera_preview_.frame_width = format.fmt.pix.width;
  camera_preview_.frame_height = format.fmt.pix.height;
#if defined(CONFIG_LILYGO_DEVICE_DRIVER_CAMERA_TYPE_OV5645)
  format.fmt.pix.pixelformat = V4L2_PIX_FMT_RGB565;
#elif defined(CONFIG_LILYGO_DEVICE_DRIVER_SCREEN_PIXEL_FORMAT_RGB888)
  format.fmt.pix.pixelformat = V4L2_PIX_FMT_RGB24;
#else
  format.fmt.pix.pixelformat = V4L2_PIX_FMT_RGB565;
#endif
  if (ioctl(camera_preview_.video_fd, VIDIOC_S_FMT, &format) != 0) {
    const int ioctl_error = errno;
    camera_preview_.error.store(CameraError::kFormatConfigurationFailed);
    LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
        "VIDIOC_S_FMT failed (reason: %s, errno: %d)\n",
        std::strerror(ioctl_error), ioctl_error);
    return IsRetryableCameraIoError(ioctl_error)
               ? CameraStartupAttemptResult::kPowerCycle
               : CameraStartupAttemptResult::kStop;
  }
  camera_preview_.frame_width = format.fmt.pix.width;
  camera_preview_.frame_height = format.fmt.pix.height;

  v4l2_requestbuffers request = {};
  request.count = kCameraBufferCount;
  request.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  request.memory = V4L2_MEMORY_MMAP;
  if (ioctl(camera_preview_.video_fd, VIDIOC_REQBUFS, &request) != 0) {
    camera_preview_.error.store(CameraError::kBufferAllocationFailed);
    LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
        "VIDIOC_REQBUFS failed\n");
    return CameraStartupAttemptResult::kStop;
  }
  if (request.count < kCameraBufferCount) {
    camera_preview_.error.store(CameraError::kBufferAllocationFailed);
    LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
        "VIDIOC_REQBUFS returned too few buffers: %lu\n",
        static_cast<unsigned long>(request.count));
    return CameraStartupAttemptResult::kStop;
  }

  for (uint32_t index = 0; index < kCameraBufferCount; ++index) {
    v4l2_buffer buffer = {};
    buffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buffer.memory = V4L2_MEMORY_MMAP;
    buffer.index = index;
    if (ioctl(camera_preview_.video_fd, VIDIOC_QUERYBUF, &buffer) != 0) {
      camera_preview_.error.store(CameraError::kBufferAllocationFailed);
      LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
          "VIDIOC_QUERYBUF failed\n");
      return CameraStartupAttemptResult::kStop;
    }
    camera_preview_.frame_buffer_sizes[index] = buffer.length;
    camera_preview_.frame_buffers[index] = mmap(nullptr, buffer.length,
        PROT_READ | PROT_WRITE, MAP_SHARED, camera_preview_.video_fd,
        buffer.m.offset);
    if (camera_preview_.frame_buffers[index] == MAP_FAILED) {
      camera_preview_.frame_buffers[index] = nullptr;
      camera_preview_.error.store(CameraError::kBufferMappingFailed);
      LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
          "Camera buffer mmap failed\n");
      return CameraStartupAttemptResult::kStop;
    }
    if (ioctl(camera_preview_.video_fd, VIDIOC_QBUF, &buffer) != 0) {
      camera_preview_.error.store(CameraError::kBufferAllocationFailed);
      LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
          "VIDIOC_QBUF failed\n");
      return CameraStartupAttemptResult::kStop;
    }
  }

  if (camera_preview_.output_mutex == nullptr) {
    camera_preview_.output_mutex = xSemaphoreCreateMutex();
    if (camera_preview_.output_mutex == nullptr) {
      camera_preview_.error.store(
          CameraError::kOutputBufferAllocationFailed);
      LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
          "Camera output mutex allocation failed\n");
      return CameraStartupAttemptResult::kStop;
    }
  }

  if (!camera_preview_.ppa.Init()) {
    camera_preview_.error.store(CameraError::kProcessingInitFailed);
    LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
        "PPA SRM init failed\n");
    return CameraStartupAttemptResult::kStop;
  }
  const size_t bytes_per_pixel = ScreenBitsPerPixel() / 8;
  camera_preview_.output_rotation_angle = NormalizeCameraPreviewRotationAngle(
      app::GetDisplayPreferences().screen_rotation_angle);
  const bool output_rotated =
      camera_preview_.output_rotation_angle == 90 ||
      camera_preview_.output_rotation_angle == 270;
  const uint32_t output_screen_width =
      output_rotated ? ScreenHeight() : ScreenWidth();
  const uint32_t output_screen_height =
      output_rotated ? ScreenWidth() : ScreenHeight();
  camera_preview_.output_width = output_screen_width;
  camera_preview_.output_height = output_screen_height;
  camera_preview_.output_width = std::max<uint32_t>(1, camera_preview_.output_width);
  camera_preview_.output_height = std::max<uint32_t>(1, camera_preview_.output_height);
  camera_preview_.output_stride = camera_preview_.output_width * bytes_per_pixel;
  camera_preview_.output_buffer_size = AlignUp(
      camera_preview_.output_stride * camera_preview_.output_height,
      camera_preview_.ppa.CacheLineSize());
  void* output_buffer = heap_caps_aligned_calloc(
      camera_preview_.ppa.CacheLineSize(), 1,
      camera_preview_.output_buffer_size, MALLOC_CAP_SPIRAM);
  if (output_buffer == nullptr) {
    camera_preview_.error.store(CameraError::kOutputBufferAllocationFailed);
    LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
        "Camera output buffer allocation failed\n");
    return CameraStartupAttemptResult::kStop;
  }
  camera_preview_.output_buffer.reset(static_cast<uint8_t*>(output_buffer));
  camera_preview_.clear_output_frames_remaining = kCameraOutputClearFrameCount;
  camera_preview_.warmup_frames_remaining = kCameraWarmupFrameCount;

  int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  if (ioctl(camera_preview_.video_fd, VIDIOC_STREAMON, &type) != 0) {
    const int ioctl_error = errno;
    camera_preview_.error.store(CameraError::kStreamStartFailed);
    LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
        "VIDIOC_STREAMON failed (reason: %s, errno: %d)\n",
        std::strerror(ioctl_error), ioctl_error);
    return IsRetryableCameraIoError(ioctl_error)
               ? CameraStartupAttemptResult::kPowerCycle
               : CameraStartupAttemptResult::kStop;
  }

  camera_preview_.initialized.store(true);
  LogMessage(LogLevel::kInfo, __FILE__, __LINE__,
      "Camera preview started (%lux%lu)\n", camera_preview_.frame_width,
      camera_preview_.frame_height);
  return CameraStartupAttemptResult::kSuccess;
}

void TDisplayP4Device::DeinitializeCameraPreview() {
  if (camera_preview_.video_fd >= 0) {
    int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    ioctl(camera_preview_.video_fd, VIDIOC_STREAMOFF, &type);
  }
  for (uint32_t index = 0; index < kCameraBufferCount; ++index) {
    if (camera_preview_.frame_buffers[index] != nullptr) {
      munmap(camera_preview_.frame_buffers[index],
          camera_preview_.frame_buffer_sizes[index]);
      camera_preview_.frame_buffers[index] = nullptr;
      camera_preview_.frame_buffer_sizes[index] = 0;
    }
  }
  if (camera_preview_.video_fd >= 0) {
    // 显式归还驱动缓冲区，避免下次打开预览时残留旧缓冲状态
    v4l2_requestbuffers request = {};
    request.count = 0;
    request.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    request.memory = V4L2_MEMORY_MMAP;
    if (ioctl(camera_preview_.video_fd, VIDIOC_REQBUFS, &request) != 0) {
      LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
          "VIDIOC_REQBUFS release failed\n");
    }
    close(camera_preview_.video_fd);
    camera_preview_.video_fd = -1;
  }
  camera_preview_.output_buffer.reset();
  if (camera_preview_.output_mutex != nullptr) {
    vSemaphoreDelete(camera_preview_.output_mutex);
    camera_preview_.output_mutex = nullptr;
  }
  camera_preview_.output_buffer_size = 0;
  camera_preview_.output_width = 0;
  camera_preview_.output_height = 0;
  camera_preview_.output_stride = 0;
  camera_preview_.output_rotation_angle = 0;
  camera_preview_.clear_output_frames_remaining = 0;
  camera_preview_.warmup_frames_remaining = 0;
  camera_preview_.frame_sequence.store(0);
  camera_preview_.initialized.store(false);
  camera_preview_.ppa.Deinit();

  // 保留 video0/video20 的 VFS 节点，只关闭摄像头供电以降低页面退出后的功耗。
  if (!driver_.SetCameraPowerEnabled(false)) {
    LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
        "Camera power disable failed\n");
  }
}

bool TDisplayP4Device::RenderCameraFrame(
    uint8_t* buffer, uint32_t width, uint32_t height) {
  if (buffer == nullptr || camera_preview_.output_buffer == nullptr ||
      camera_preview_.output_mutex == nullptr) {
    return false;
  }

  const uint32_t output_width = camera_preview_.output_width;
  const uint32_t output_height = camera_preview_.output_height;
  const int output_rotation_angle = camera_preview_.output_rotation_angle;
  const bool output_rotated =
      output_rotation_angle == 90 || output_rotation_angle == 270;
  const uint32_t rotated_source_width = output_rotated ? height : width;
  const uint32_t rotated_source_height = output_rotated ? width : height;
  const float scale = std::min(
      static_cast<float>(output_width) / static_cast<float>(rotated_source_width),
      static_cast<float>(output_height) /
          static_cast<float>(rotated_source_height));
  const uint32_t scaled_width = std::max<uint32_t>(
      1, static_cast<uint32_t>(std::round(rotated_source_width * scale)));
  const uint32_t scaled_height = std::max<uint32_t>(
      1, static_cast<uint32_t>(std::round(rotated_source_height * scale)));
  const uint32_t output_offset_x =
      output_width > scaled_width ? (output_width - scaled_width) / 2 : 0;
  const uint32_t output_offset_y =
      output_height > scaled_height ? (output_height - scaled_height) / 2 : 0;
  const size_t aligned_output_size = camera_preview_.output_buffer_size;
#if defined(CONFIG_LILYGO_DEVICE_DRIVER_CAMERA_TYPE_OV5645)
  const ppa_srm_color_mode_t input_color_mode = PPA_SRM_COLOR_MODE_RGB565;
#elif defined(CONFIG_LILYGO_DEVICE_DRIVER_SCREEN_PIXEL_FORMAT_RGB888)
  const ppa_srm_color_mode_t input_color_mode = PPA_SRM_COLOR_MODE_RGB888;
#else
  const ppa_srm_color_mode_t input_color_mode = PPA_SRM_COLOR_MODE_RGB565;
#endif
#if defined(CONFIG_LILYGO_DEVICE_DRIVER_SCREEN_PIXEL_FORMAT_RGB888)
  const ppa_srm_color_mode_t output_color_mode = PPA_SRM_COLOR_MODE_RGB888;
#else
  const ppa_srm_color_mode_t output_color_mode = PPA_SRM_COLOR_MODE_RGB565;
#endif
  PpaSrmImageConfig input = {
      .buffer = buffer,
      .pic_width = width,
      .pic_height = height,
      .block_width = width,
      .block_height = height,
      .block_offset_x = 0,
      .block_offset_y = 0,
      .color_mode = input_color_mode,
  };
  PpaSrmImageConfig output = {
      .buffer = camera_preview_.output_buffer.get(),
      .buffer_size = aligned_output_size,
      .pic_width = output_width,
      .pic_height = output_height,
      .block_width = output_width,
      .block_height = output_height,
      .block_offset_x = output_offset_x,
      .block_offset_y = output_offset_y,
      .color_mode = output_color_mode,
  };
  PpaSrmTransformConfig transform = {
      .rotation_angle = ToCameraPreviewPpaRotation(output_rotation_angle),
      .scale_x = scale,
      .scale_y = scale,
      .mirror_y = driver_.screen_type() == device::ScreenType::kHi8561,
  };
  if (xSemaphoreTake(camera_preview_.output_mutex, pdMS_TO_TICKS(20)) != pdTRUE) {
    return false;
  }
  if (camera_preview_.clear_output_frames_remaining > 0 ||
      output_offset_x > 0 || output_offset_y > 0) {
    std::memset(camera_preview_.output_buffer.get(), 0,
        camera_preview_.output_buffer_size);
    if (camera_preview_.clear_output_frames_remaining > 0) {
      --camera_preview_.clear_output_frames_remaining;
    }
  }
  const bool transformed = camera_preview_.ppa.Transform(input, output, transform);
  if (transformed) {
    camera_preview_.frame_sequence.fetch_add(1);
  }
  xSemaphoreGive(camera_preview_.output_mutex);
  return transformed;
}

bool TDisplayP4Device::SetGpsEnabled(bool enabled) {
  if (!enabled) {
    gps_running_ = false;
    gps_status_.running = false;
    const bool result = driver_.SetL76kSleep(true);
    if (!result) {
      LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
          "Disable GPS failed\n");
    }
    return result;
  }

  if (!driver_.SetL76kSleep(false) || !driver_.IsL76kReady()) {
    driver_.SetL76kSleep(true);
    LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
        "Enable GPS failed\n");
    return false;
  }

  gps_status_ = GpsStatus();
  gps_running_ = true;
  gps_status_.running = true;
  gps_status_.update_interval_ms = driver_.chip().l76k->update_interval_ms();
  if (!driver_.chip().l76k->ClearRxBufferData()) {
    gps_running_ = false;
    gps_status_.running = false;
    driver_.SetL76kSleep(true);
    return false;
  }
  return true;
}

bool TDisplayP4Device::ReadGpsStatus(GpsStatus* status) {
  if (status == nullptr) {
    return false;
  }

  gps_status_.running = gps_running_;
  if (driver_.IsL76kReady()) {
    gps_status_.update_interval_ms = driver_.chip().l76k->update_interval_ms();
  }
  *status = gps_status_;
  if (!gps_running_) {
    return true;
  }
  if (!driver_.IsL76kReady()) {
    return false;
  }

  const size_t rx_buffer_length = driver_.chip().l76k->GetRxBufferLength();
  if (rx_buffer_length == 0) {
    return true;
  }

  const size_t buffer_length =
      std::min(rx_buffer_length, kGpsMaxReadBufferBytes);
  std::unique_ptr<uint8_t[]> buffer(
      new (std::nothrow) uint8_t[buffer_length + 1]);
  if (buffer == nullptr) {
    return false;
  }

  const uint32_t read_length = driver_.chip().l76k->ReadData(
      buffer.get(), static_cast<uint32_t>(buffer_length));
  if (read_length == 0) {
    return true;
  }

  const size_t data_length =
      std::min(static_cast<size_t>(read_length), buffer_length);
  buffer[data_length] = '\0';

  GpsStatus next_status = gps_status_;
  next_status.running = true;
  next_status.data_ready = true;
  next_status.bytes_read = data_length;
  next_status.update_interval_ms = driver_.chip().l76k->update_interval_ms();

  cpp_bus_driver::L76k::Info info;
  const bool parse_success =
      driver_.chip().l76k->ParseInfo(buffer.get(), data_length, info);
  next_status.parse_success = next_status.parse_success || parse_success;
  if (parse_success) {
    const auto& rmc = info.rmc;
    const auto& gga = info.gga;
    const auto& gsv = info.gsv;
    const auto& gsa = info.gsa;
    const auto& vtg = info.vtg;
    const auto& zda = info.zda;

    if (rmc.location_status_update_flag) {
      CopyString(next_status.location_status,
          sizeof(next_status.location_status), rmc.location_status);
    }
    if (!rmc.mode_indicator.empty()) {
      CopyString(next_status.mode_indicator, sizeof(next_status.mode_indicator),
          rmc.mode_indicator);
    } else if (!vtg.mode_indicator.empty()) {
      CopyString(next_status.mode_indicator, sizeof(next_status.mode_indicator),
          vtg.mode_indicator);
    }
    if (!rmc.navigational_status.empty()) {
      CopyString(next_status.navigational_status,
          sizeof(next_status.navigational_status), rmc.navigational_status);
    }

    if (rmc.utc.update_flag) {
      next_status.utc.ready = true;
      next_status.utc.hour = rmc.utc.hour;
      next_status.utc.minute = rmc.utc.minute;
      next_status.utc.second = rmc.utc.second;
    } else if (zda.utc.update_flag) {
      next_status.utc.ready = true;
      next_status.utc.hour = zda.utc.hour;
      next_status.utc.minute = zda.utc.minute;
      next_status.utc.second = zda.utc.second;
    }

    if (rmc.data.update_flag) {
      next_status.date.ready = true;
      next_status.date.day = rmc.data.day;
      next_status.date.month = rmc.data.month;
      next_status.date.year = 2000 + rmc.data.year;
    } else if (zda.date.update_flag) {
      next_status.date.ready = true;
      next_status.date.day = zda.date.day;
      next_status.date.month = zda.date.month;
      next_status.date.year = zda.date.year;
    }

    if (rmc.location.lat.update_flag &&
        rmc.location.lat.direction_update_flag) {
      next_status.latitude.ready = true;
      next_status.latitude.degrees = rmc.location.lat.degrees;
      next_status.latitude.minutes = rmc.location.lat.minutes;
      next_status.latitude.degrees_minutes = rmc.location.lat.degrees_minutes;
      std::snprintf(next_status.latitude.direction,
          sizeof(next_status.latitude.direction), "%s",
          rmc.location.lat.direction.c_str());
    }

    if (rmc.location.lon.update_flag &&
        rmc.location.lon.direction_update_flag) {
      next_status.longitude.ready = true;
      next_status.longitude.degrees = rmc.location.lon.degrees;
      next_status.longitude.minutes = rmc.location.lon.minutes;
      next_status.longitude.degrees_minutes = rmc.location.lon.degrees_minutes;
      std::snprintf(next_status.longitude.direction,
          sizeof(next_status.longitude.direction), "%s",
          rmc.location.lon.direction.c_str());
    }

    next_status.positioned =
        next_status.positioned ||
        (next_status.latitude.ready && next_status.longitude.ready);

    if (IsGnssFloatReady(rmc.speed_over_ground_knots)) {
      next_status.speed_ready = true;
      next_status.speed_knots = rmc.speed_over_ground_knots;
      next_status.speed_kmh = rmc.speed_over_ground_knots * 1.852F;
    } else if (vtg.update_flag && IsGnssFloatReady(vtg.speed_kmh)) {
      next_status.speed_ready = true;
      next_status.speed_knots = vtg.speed_knots;
      next_status.speed_kmh = vtg.speed_kmh;
    }
    if (IsGnssFloatReady(rmc.course_over_ground_degree)) {
      next_status.course_ready = true;
      next_status.course_degree = rmc.course_over_ground_degree;
    } else if (vtg.update_flag && IsGnssFloatReady(vtg.course_true_degree)) {
      next_status.course_ready = true;
      next_status.course_degree = vtg.course_true_degree;
    }
    if (gga.gps_mode_status != 0xFF) {
      next_status.fix_quality_ready = true;
      next_status.fix_quality = gga.gps_mode_status;
    }
    if (gga.online_satellite_count != 0xFF) {
      next_status.satellites_used_ready = true;
      next_status.satellites_used = gga.online_satellite_count;
    }
    if (gsv.update_flag && gsv.total_satellite_count != 0xFF) {
      next_status.satellites_in_view_ready = true;
      next_status.satellites_in_view = gsv.total_satellite_count;
    }
    if (gsv.update_flag) {
      next_status.satellite_info_count = gsv.satellites.size();
      int16_t strongest_cn0 = -1;
      uint16_t strongest_id = 0;
      for (const auto& satellite : gsv.satellites) {
        if (satellite.cn0 > strongest_cn0) {
          strongest_cn0 = satellite.cn0;
          strongest_id = satellite.id;
        }
      }
      if (strongest_cn0 >= 0) {
        next_status.strongest_satellite_ready = true;
        next_status.strongest_satellite_id = strongest_id;
        next_status.strongest_satellite_cn0 = strongest_cn0;
      }
    }
    if (IsGnssFloatReady(gga.hdop)) {
      next_status.hdop_ready = true;
      next_status.hdop = gga.hdop;
    }
    if (IsGnssFloatReady(gga.altitude)) {
      next_status.altitude_ready = true;
      next_status.altitude = gga.altitude;
      CopyString(next_status.altitude_unit, sizeof(next_status.altitude_unit),
          gga.altitude_unit);
    }
    if (gsa.update_flag && !gsa.sentences.empty()) {
      const auto& sentence = gsa.sentences.front();
      if (sentence.fix_mode != 0xFF) {
        next_status.fix_mode_ready = true;
        next_status.fix_mode = sentence.fix_mode;
      }
      if (IsGnssFloatReady(sentence.pdop)) {
        next_status.pdop_ready = true;
        next_status.pdop = sentence.pdop;
      }
      if (IsGnssFloatReady(sentence.hdop)) {
        next_status.hdop_ready = true;
        next_status.hdop = sentence.hdop;
      }
      if (IsGnssFloatReady(sentence.vdop)) {
        next_status.vdop_ready = true;
        next_status.vdop = sentence.vdop;
      }
    }
  }

  gps_status_ = next_status;
  *status = gps_status_;
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
    const size_t read_bytes = driver_.chip().es8311->ReadI2s(
        samples.data(), samples.size() * sizeof(samples[0]));
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
      const int average_level_percent = std::min(
          100, (average_sample * 100) / kMicrophoneAverageFullScale);
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
        const auto* pcm_data =
            reinterpret_cast<const uint8_t*>(samples.data());
        size_t written_bytes = 0;
        while (written_bytes < read_bytes &&
               microphone_.adc_to_dac_enabled.load() &&
               !microphone_.stop_requested.load()) {
          const size_t written = driver_.chip().es8311->WriteI2s(
              pcm_data + written_bytes, read_bytes - written_bytes);
          if (written == 0) {
            LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
                "ES8311 microphone PCM loopback write failed\n");
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

void TDisplayP4Device::EthernetInitTaskEntry(void* context) {
  auto* self = static_cast<TDisplayP4Device*>(context);
  if (self != nullptr) {
    self->RunEthernetInitTask();
  }
  vTaskDelete(nullptr);
}

void TDisplayP4Device::RunEthernetInitTask() {
  if (ethernet_.stop_requested.load()) {
    ethernet_.init_task_running.store(false);
    SetEthernetEnabled(false);
    return;
  }

  int result = ESP_OK;
  if (!driver_.SetEthernetPowerEnabled(true)) {
    result = ESP_FAIL;
  } else if (ethernet_.stop_requested.load()) {
    driver_.SetEthernetPowerEnabled(false);
    ethernet_.init_task_running.store(false);
    SetEthernetEnabled(false);
    return;
  } else {
    result = InitializeEthernetStack();
  }
  if (result != ESP_OK) {
    SetEthernetFailure(result);
    driver_.SetEthernetPowerEnabled(false);
    LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
        "Ethernet init failed: %s (%#X)\n",
        esp_err_to_name(static_cast<esp_err_t>(result)),
        static_cast<unsigned>(result));
  }
  ethernet_.init_task_running.store(false);
  if (ethernet_.stop_requested.load()) {
    SetEthernetEnabled(false);
  }
}

int TDisplayP4Device::InitializeEthernetStack() {
  if (ethernet_.handle != nullptr) {
    const esp_err_t start_result =
        esp_eth_start(reinterpret_cast<esp_eth_handle_t>(ethernet_.handle));
    if (start_result != ESP_OK && start_result != ESP_ERR_INVALID_STATE) {
      return start_result;
    }
    ethernet_.driver_initialized.store(true);
    ethernet_.running.store(true);
    ethernet_.start_failed.store(false);
    ethernet_.last_error.store(ESP_OK);
    return ESP_OK;
  }

  esp_err_t result = esp_netif_init();
  if (result != ESP_OK && result != ESP_ERR_INVALID_STATE) {
    return result;
  }

  result = esp_event_loop_create_default();
  if (result != ESP_OK && result != ESP_ERR_INVALID_STATE) {
    return result;
  }

  eth_mac_config_t mac_config = ETH_MAC_DEFAULT_CONFIG();
  eth_phy_config_t phy_config = ETH_PHY_DEFAULT_CONFIG();
  phy_config.phy_addr = device::ip101::kPhyAddress;
  phy_config.reset_gpio_num = gpio::ip101::kPhyRst;

  eth_esp32_emac_config_t emac_config = {};
  emac_config.smi_gpio.mdc_num = gpio::ip101::kRmiiMdc;
  emac_config.smi_gpio.mdio_num = gpio::ip101::kRmiiMdio;
  emac_config.interface = EMAC_DATA_INTERFACE_RMII;
  emac_config.clock_config.rmii.clock_mode = EMAC_CLK_EXT_IN;
  emac_config.clock_config.rmii.clock_gpio =
      static_cast<emac_rmii_clock_gpio_t>(gpio::ip101::kRmiiRefClk);
  emac_config.dma_burst_len = ETH_DMA_BURST_LEN_32;
  emac_config.intr_priority = 0;
#if SOC_EMAC_USE_MULTI_IO_MUX || SOC_EMAC_MII_USE_GPIO_MATRIX
  emac_config.emac_dataif_gpio.rmii.tx_en_num = gpio::ip101::kRmiiTxEn;
  emac_config.emac_dataif_gpio.rmii.txd0_num = gpio::ip101::kRmiiTxd0;
  emac_config.emac_dataif_gpio.rmii.txd1_num = gpio::ip101::kRmiiTxd1;
  emac_config.emac_dataif_gpio.rmii.crs_dv_num = gpio::ip101::kRmiiCrsDv;
  emac_config.emac_dataif_gpio.rmii.rxd0_num = gpio::ip101::kRmiiRxd0;
  emac_config.emac_dataif_gpio.rmii.rxd1_num = gpio::ip101::kRmiiRxd1;
#endif
#if !SOC_EMAC_RMII_CLK_OUT_INTERNAL_LOOPBACK
  emac_config.clock_config_out_in.rmii.clock_mode = EMAC_CLK_EXT_IN;
  emac_config.clock_config_out_in.rmii.clock_gpio =
      static_cast<emac_rmii_clock_gpio_t>(gpio::ip101::kRmiiClkOut);
#endif
  emac_config.mdc_freq_hz = 0;

  esp_eth_mac_t* mac = esp_eth_mac_new_esp32(&emac_config, &mac_config);
  if (mac == nullptr) {
    return ESP_ERR_NO_MEM;
  }

  esp_eth_phy_t* phy = esp_eth_phy_new_ip101(&phy_config);
  if (phy == nullptr) {
    mac->del(mac);
    return ESP_ERR_NO_MEM;
  }

  esp_eth_handle_t handle = nullptr;
  esp_eth_config_t config = ETH_DEFAULT_CONFIG(mac, phy);
  result = esp_eth_driver_install(&config, &handle);
  if (result != ESP_OK) {
    mac->del(mac);
    phy->del(phy);
    return result;
  }

  esp_netif_inherent_config_t inherent_config = *ESP_NETIF_BASE_DEFAULT_ETH;
  esp_netif_config_t netif_config = {
      .base = &inherent_config,
      .driver = nullptr,
      .stack = ESP_NETIF_NETSTACK_DEFAULT_ETH,
  };
  esp_netif_t* netif = esp_netif_new(&netif_config);
  if (netif == nullptr) {
    return ESP_ERR_NO_MEM;
  }

  auto glue = esp_eth_new_netif_glue(handle);
  if (glue == nullptr) {
    return ESP_ERR_NO_MEM;
  }

  result = esp_netif_attach(netif, glue);
  if (result != ESP_OK) {
    return result;
  }

  result = esp_event_handler_register(
      ETH_EVENT, ESP_EVENT_ANY_ID, EthernetEventHandler, this);
  if (result != ESP_OK) {
    return result;
  }

  result = esp_event_handler_register(
      IP_EVENT, IP_EVENT_ETH_GOT_IP, EthernetGotIpEventHandler, this);
  if (result != ESP_OK) {
    return result;
  }

  ethernet_.handle = handle;
  ethernet_.port_count.store(1);

  result = esp_eth_start(handle);
  if (result != ESP_OK && result != ESP_ERR_INVALID_STATE) {
    return result;
  }

  ethernet_.driver_initialized.store(true);
  ethernet_.running.store(true);
  ethernet_.start_failed.store(false);
  ethernet_.last_error.store(ESP_OK);
  return ESP_OK;
}

void TDisplayP4Device::SetEthernetFailure(int error) {
  ethernet_.init_task_running.store(false);
  ethernet_.driver_initialized.store(ethernet_.handle != nullptr);
  ethernet_.running.store(false);
  ethernet_.link_up.store(false);
  ethernet_.got_ip.store(false);
  ethernet_.start_failed.store(true);
  ethernet_.last_error.store(error);
  ethernet_.ip_address.store(0);
  ethernet_.netmask.store(0);
  ethernet_.gateway.store(0);
}

void TDisplayP4Device::EthernetEventHandler(
    void* arg, const char* event_base, int32_t event_id, void* event_data) {
  (void)event_base;
  auto* self = static_cast<TDisplayP4Device*>(arg);
  if (self == nullptr) {
    return;
  }

  switch (event_id) {
    case ETHERNET_EVENT_CONNECTED: {
      self->ethernet_.running.store(true);
      self->ethernet_.link_up.store(true);
      self->ethernet_.got_ip.store(false);
      self->ethernet_.ip_address.store(0);
      self->ethernet_.netmask.store(0);
      self->ethernet_.gateway.store(0);

      if (event_data != nullptr) {
        esp_eth_handle_t handle = *static_cast<esp_eth_handle_t*>(event_data);
        uint8_t mac_address[6] = {};
        if (esp_eth_ioctl(handle, ETH_CMD_G_MAC_ADDR, mac_address) == ESP_OK) {
          self->ethernet_.mac_address.store(PackMacAddress(mac_address));
        }
      }
      break;
    }
    case ETHERNET_EVENT_DISCONNECTED:
      self->ethernet_.link_up.store(false);
      self->ethernet_.got_ip.store(false);
      self->ethernet_.ip_address.store(0);
      self->ethernet_.netmask.store(0);
      self->ethernet_.gateway.store(0);
      break;
    case ETHERNET_EVENT_START:
      self->ethernet_.running.store(true);
      self->ethernet_.start_failed.store(false);
      self->ethernet_.last_error.store(ESP_OK);
      break;
    case ETHERNET_EVENT_STOP:
      self->ethernet_.running.store(false);
      self->ethernet_.link_up.store(false);
      self->ethernet_.got_ip.store(false);
      self->ethernet_.ip_address.store(0);
      self->ethernet_.netmask.store(0);
      self->ethernet_.gateway.store(0);
      break;
    default:
      break;
  }
}

void TDisplayP4Device::EthernetGotIpEventHandler(
    void* arg, const char* event_base, int32_t event_id, void* event_data) {
  (void)event_base;
  (void)event_id;
  auto* self = static_cast<TDisplayP4Device*>(arg);
  auto* event = static_cast<ip_event_got_ip_t*>(event_data);
  if (self == nullptr || event == nullptr) {
    return;
  }

  self->ethernet_.link_up.store(true);
  self->ethernet_.got_ip.store(true);
  self->ethernet_.ip_address.store(event->ip_info.ip.addr);
  self->ethernet_.netmask.store(event->ip_info.netmask.addr);
  self->ethernet_.gateway.store(event->ip_info.gw.addr);
}

void TDisplayP4Device::WifiInitTaskEntry(void* context) {
  auto* self = static_cast<TDisplayP4Device*>(context);
  if (self != nullptr) {
    self->RunWifiInitTask();
  }
  vTaskDelete(nullptr);
}

void TDisplayP4Device::RunWifiInitTask() {
  if (!WaitForWifiHardwareReady()) {
    LogMessage(
        LogLevel::kWarning, __FILE__, __LINE__, "WiFi hardware is not ready\n");
    wifi_.init_task_running.store(false);
    SetWifiEnabled(false);
    SetWifiFailure(ESP_ERR_TIMEOUT);
    return;
  }

  const int result = InitializeWifiStack();
  if (result != ESP_OK) {
    LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
        "WiFi init failed: %s (%#X)\n",
        esp_err_to_name(static_cast<esp_err_t>(result)),
        static_cast<unsigned>(result));
    wifi_.init_task_running.store(false);
    SetWifiEnabled(false);
    SetWifiFailure(result);
    return;
  }

  wifi_.init_task_running.store(false);
  if (wifi_.stop_requested.load()) {
    SetWifiEnabled(false);
    return;
  }
  if (wifi_time_test_.requested.load()) {
    const int test_result = StartWifiTimeTestInternal();
    if (test_result != ESP_OK) {
      SetWifiFailure(test_result);
      LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
          "WiFi time test start failed: %s (%#X)\n",
          esp_err_to_name(static_cast<esp_err_t>(test_result)),
          static_cast<unsigned>(test_result));
    }
  }
}

void TDisplayP4Device::WifiScanTaskEntry(void* context) {
  auto* self = static_cast<TDisplayP4Device*>(context);
  if (self != nullptr) {
    self->RunWifiScanTask();
  }
  vTaskDelete(nullptr);
}

void TDisplayP4Device::WifiConnectTaskEntry(void* context) {
  auto* self = static_cast<TDisplayP4Device*>(context);
  if (self != nullptr) {
    self->RunWifiConnectTask();
  }
  vTaskDelete(nullptr);
}

void TDisplayP4Device::RunWifiScanTask() {
  if (!wifi_.running.load()) {
    const int prepare_result = PrepareWifiStation();
    if (prepare_result != ESP_OK) {
      wifi_.scan_failed.store(true);
      wifi_.last_error.store(prepare_result);
      wifi_.scan_network_count.store(0);
      wifi_.scan_generation.fetch_add(1);
      wifi_.scan_running.store(false);
      wifi_.scan_task_running.store(false);
      return;
    }
  }

  if (wifi_.stop_requested.load()) {
    wifi_.scan_running.store(false);
    wifi_.scan_task_running.store(false);
    wifi_.scan_network_count.store(0);
    wifi_.scan_generation.fetch_add(1);
    return;
  }

  // hosted STA 在关联热点期间可能暂时拒绝扫描，等待连接状态稳定后重试。
  esp_err_t scan_result = ESP_ERR_WIFI_STATE;
  uint32_t retry_elapsed_ms = 0;
  while (!wifi_.stop_requested.load()) {
    scan_result = esp_wifi_scan_start(nullptr, false);
    if (scan_result == ESP_OK) {
      return;
    }
    if (scan_result != ESP_ERR_WIFI_STATE ||
        retry_elapsed_ms >= kWifiScanTimeoutMs) {
      break;
    }
    vTaskDelay(pdMS_TO_TICKS(kWifiScanStateRetryIntervalMs));
    retry_elapsed_ms += kWifiScanStateRetryIntervalMs;
  }

  if (wifi_.stop_requested.load()) {
    wifi_.scan_running.store(false);
    wifi_.scan_task_running.store(false);
    wifi_.scan_network_count.store(0);
    wifi_.scan_generation.fetch_add(1);
    return;
  }

  wifi_.scan_failed.store(true);
  wifi_.last_error.store(scan_result);
  wifi_.scan_network_count.store(0);
  wifi_.scan_generation.fetch_add(1);
  wifi_.scan_running.store(false);
  wifi_.scan_task_running.store(false);
}

void TDisplayP4Device::RunWifiConnectTask() {
  char ssid[kWifiSsidMaxLength + 1] = {};
  char password[kWifiPasswordMaxLength + 1] = {};
  std::snprintf(ssid, sizeof(ssid), "%s", wifi_.connect_ssid);
  std::snprintf(password, sizeof(password), "%s", wifi_.connect_password);

  const auto finish = [this](esp_err_t error) {
    if (error != ESP_OK) {
      SetWifiFailure(error);
    }
    wifi_.connect_task_running.store(false);
  };

  if (ssid[0] == '\0') {
    finish(ESP_ERR_INVALID_ARG);
    return;
  }

  uint32_t wait_scan_ms = 0;
  while (wifi_.scan_running.load() || wifi_.scan_task_running.load()) {
    if (wifi_.connect_cancel_requested.load()) {
      wifi_.connect_task_running.store(false);
      return;
    }
    if (wait_scan_ms >= kWifiScanTimeoutMs) {
      wifi_.scan_running.store(false);
      wifi_.scan_task_running.store(false);
      esp_wifi_scan_stop();
      wifi_.scan_failed.store(true);
      wifi_.last_error.store(ESP_ERR_TIMEOUT);
      wifi_.scan_generation.fetch_add(1);
      finish(ESP_ERR_TIMEOUT);
      return;
    }
    vTaskDelay(pdMS_TO_TICKS(kWifiHardwareReadyPollMs));
    wait_scan_ms += kWifiHardwareReadyPollMs;
  }

  const int prepare_result = PrepareWifiStation();
  if (prepare_result != ESP_OK) {
    finish(static_cast<esp_err_t>(prepare_result));
    return;
  }

  if (wifi_.connect_cancel_requested.load()) {
    wifi_.connect_task_running.store(false);
    return;
  }

  if (wifi_.connected.load() || wifi_.got_ip.load()) {
    // 切换热点前先断开当前连接。
    esp_wifi_disconnect();
  }

  if (wifi_.connect_cancel_requested.load()) {
    wifi_.connect_task_running.store(false);
    return;
  }

  wifi_config_t wifi_config = {};
  const size_t ssid_length =
      std::min(std::strlen(ssid), sizeof(wifi_config.sta.ssid));
  std::memcpy(wifi_config.sta.ssid, ssid, ssid_length);
  if (password[0] != '\0') {
    const size_t password_length =
        std::min(std::strlen(password), sizeof(wifi_config.sta.password));
    std::memcpy(wifi_config.sta.password, password, password_length);
  }

  wifi_.start_failed.store(false);
  wifi_.last_error.store(ESP_OK);
  wifi_.disconnect_reason.store(0);
  wifi_.retry_count.store(0);
  wifi_.connected.store(false);
  wifi_.got_ip.store(false);
  wifi_.ip_address.store(0);
  wifi_.netmask.store(0);
  wifi_.gateway.store(0);

  const esp_err_t config_result = esp_wifi_set_config(WIFI_IF_STA,
      &wifi_config);
  if (config_result != ESP_OK) {
    finish(config_result);
    return;
  }

  if (wifi_.connect_cancel_requested.load()) {
    wifi_.connect_task_running.store(false);
    return;
  }

  const esp_err_t connect_result = esp_wifi_connect();
  if (connect_result != ESP_OK) {
    finish(connect_result);
    return;
  }
  // 保持连接进行中状态，直到取得 DHCP 地址或收到断开事件。
}

bool TDisplayP4Device::WaitForWifiHardwareReady() {
  uint32_t elapsed_ms = 0;
  while (!driver_.IsXl9535Ready() &&
         elapsed_ms < kWifiHardwareReadyTimeoutMs) {
    vTaskDelay(pdMS_TO_TICKS(kWifiHardwareReadyPollMs));
    elapsed_ms += kWifiHardwareReadyPollMs;
  }

  if (!driver_.IsXl9535Ready()) {
    return false;
  }

  vTaskDelay(pdMS_TO_TICKS(kWifiEsp32c6BootDelayMs));
  return true;
}

int TDisplayP4Device::InitializeWifiStack() {
  if (wifi_.driver_initialized.load()) {
    return PrepareWifiStation();
  }

  if (!wifi_.hosted_bridge_initialized.load()) {
    const esp_hosted_transport_err_t reset_callback_result =
        esp_hosted_sdio_set_reset_callback(
            SetWifiCoprocessorResetLevel, &driver_);
    if (reset_callback_result != ESP_TRANSPORT_OK) {
      return static_cast<int>(reset_callback_result);
    }
    const esp_err_t hosted_result = esp_hosted_init();
    if (hosted_result != ESP_OK && hosted_result != ESP_ERR_INVALID_STATE) {
      return hosted_result;
    }
    wifi_.hosted_bridge_initialized.store(true);
  }

  esp_err_t result = esp_netif_init();
  if (result != ESP_OK && result != ESP_ERR_INVALID_STATE) {
    return result;
  }

  result = esp_event_loop_create_default();
  if (result != ESP_OK && result != ESP_ERR_INVALID_STATE) {
    return result;
  }

  if (wifi_.netif == nullptr) {
    wifi_.netif = esp_netif_create_default_wifi_sta();
    if (wifi_.netif == nullptr) {
      return ESP_ERR_NO_MEM;
    }
  }

  wifi_init_config_t config = WIFI_INIT_CONFIG_DEFAULT();
  // 账号密码由 ESP32-P4 侧管理，C6 只接收 RAM 中的临时 WiFi 配置。
  config.nvs_enable = false;
  result = esp_wifi_init(&config);
  if (result != ESP_OK && result != ESP_ERR_WIFI_INIT_STATE) {
    return result;
  }

  result = esp_wifi_set_storage(WIFI_STORAGE_RAM);
  if (result != ESP_OK) {
    return result;
  }

  result = esp_event_handler_register(
      WIFI_EVENT, ESP_EVENT_ANY_ID, WifiEventHandler, this);
  if (result != ESP_OK) {
    return result;
  }

  result = esp_event_handler_register(
      IP_EVENT, IP_EVENT_STA_GOT_IP, WifiGotIpEventHandler, this);
  if (result != ESP_OK) {
    return result;
  }

  result = esp_wifi_set_mode(WIFI_MODE_STA);
  if (result != ESP_OK) {
    return result;
  }

  wifi_config_t empty_config = {};
  result = esp_wifi_set_config(WIFI_IF_STA, &empty_config);
  if (result != ESP_OK) {
    return result;
  }

  result = esp_wifi_start();
  if (result != ESP_OK) {
    return result;
  }
  wifi_.driver_initialized.store(true);
  wifi_.running.store(true);
  wifi_.connected.store(false);
  wifi_.got_ip.store(false);
  wifi_.start_failed.store(false);
  wifi_.last_error.store(ESP_OK);
  return ESP_OK;
}

int TDisplayP4Device::PrepareWifiStation() {
  if (!wifi_.driver_initialized.load()) {
    return ESP_ERR_WIFI_NOT_INIT;
  }

  if (wifi_.running.load()) {
    wifi_.start_failed.store(false);
    wifi_.last_error.store(ESP_OK);
    return ESP_OK;
  }

  esp_err_t result = esp_wifi_set_storage(WIFI_STORAGE_RAM);
  if (result != ESP_OK) {
    return result;
  }

  result = esp_wifi_set_mode(WIFI_MODE_STA);
  if (result != ESP_OK) {
    return result;
  }

  result = esp_wifi_start();
  if (result != ESP_OK) {
    return result;
  }

  wifi_.running.store(true);
  wifi_.start_failed.store(false);
  wifi_.last_error.store(ESP_OK);
  return ESP_OK;
}

void TDisplayP4Device::CopyWifiScanResultsFromDriver() {
  uint16_t available_count = 0;
  esp_err_t result = esp_wifi_scan_get_ap_num(&available_count);
  if (result != ESP_OK) {
    wifi_.scan_failed.store(true);
    wifi_.last_error.store(result);
    wifi_.scan_network_count.store(0);
    wifi_.scan_generation.fetch_add(1);
    return;
  }

  uint16_t record_count = static_cast<uint16_t>(
      std::min<size_t>(available_count, kMaxWifiScanNetworkCount));
  std::unique_ptr<wifi_ap_record_t[]> records(
      new (std::nothrow) wifi_ap_record_t[kMaxWifiScanNetworkCount]());
  std::unique_ptr<WifiNetworkInfo[]> networks(
      new (std::nothrow) WifiNetworkInfo[kMaxWifiScanNetworkCount]());
  if (records == nullptr || networks == nullptr) {
    wifi_.scan_failed.store(true);
    wifi_.last_error.store(ESP_ERR_NO_MEM);
    wifi_.scan_network_count.store(0);
    wifi_.scan_generation.fetch_add(1);
    return;
  }

  if (record_count > 0) {
    result = esp_wifi_scan_get_ap_records(&record_count, records.get());
    if (result != ESP_OK) {
      wifi_.scan_failed.store(true);
      wifi_.last_error.store(result);
      wifi_.scan_network_count.store(0);
      wifi_.scan_generation.fetch_add(1);
      return;
    }
  } else {
    wifi_.scan_network_count.store(0);
    wifi_.scan_generation.fetch_add(1);
    return;
  }

  size_t network_count = 0;
  for (uint16_t i = 0; i < record_count &&
       network_count < kMaxWifiScanNetworkCount; ++i) {
    const auto* ssid =
        reinterpret_cast<const char*>(records[i].ssid);
    if (ssid == nullptr || ssid[0] == '\0') {
      continue;
    }

    bool duplicate = false;
    for (size_t existing = 0; existing < network_count; ++existing) {
      if (std::strncmp(networks[existing].ssid, ssid,
              sizeof(networks[existing].ssid)) == 0) {
        duplicate = true;
        break;
      }
    }
    if (duplicate) {
      continue;
    }

    WifiNetworkInfo info;
    std::snprintf(info.ssid, sizeof(info.ssid), "%s", ssid);
    info.rssi = records[i].rssi;
    info.channel = records[i].primary;
    info.secure = IsSecureWifiAuthMode(records[i].authmode);
    info.is_5g = IsFiveGWifiChannel(records[i].primary);
    networks[network_count++] = info;
  }

  if (wifi_.scan_results_mutex != nullptr) {
    xSemaphoreTake(wifi_.scan_results_mutex, portMAX_DELAY);
  }
  for (size_t i = 0; i < kMaxWifiScanNetworkCount; ++i) {
    wifi_.scan_networks[i] = networks[i];
  }
  wifi_.scan_network_count.store(network_count);
  wifi_.scan_failed.store(false);
  wifi_.last_error.store(ESP_OK);
  wifi_.scan_generation.fetch_add(1);
  if (wifi_.scan_results_mutex != nullptr) {
    xSemaphoreGive(wifi_.scan_results_mutex);
  }
}

int TDisplayP4Device::StartWifiTimeTestInternal() {
  if (!wifi_.driver_initialized.load()) {
    return ESP_ERR_WIFI_NOT_INIT;
  }

  if (wifi_time_test_.active.load()) {
    return ESP_OK;
  }

  wifi_.connect_cancel_requested.store(true);
  wifi_.connect_task_running.store(false);

  wifi_time_test_.previous_running = wifi_.running.load();
  wifi_time_test_.previous_connected = wifi_.connected.load();
  wifi_time_test_.previous_mode_valid =
      esp_wifi_get_mode(&wifi_time_test_.previous_mode) == ESP_OK;
  wifi_time_test_.previous_sta_config_valid =
      esp_wifi_get_config(WIFI_IF_STA, &wifi_time_test_.previous_sta_config) ==
      ESP_OK;

  // 进入 CIT WiFi 时间测试前先停止设置页当前 WiFi，避免沿用旧热点。
  if (wifi_time_test_.previous_running) {
    esp_wifi_disconnect();
    const esp_err_t stop_result = esp_wifi_stop();
    if (stop_result != ESP_OK && stop_result != ESP_ERR_WIFI_NOT_STARTED) {
      return stop_result;
    }
  }
  StopWifiInternetCheck();

  wifi_.start_failed.store(false);
  wifi_.last_error.store(ESP_OK);
  wifi_.disconnect_reason.store(0);
  wifi_.retry_count.store(0);
  wifi_.running.store(false);
  wifi_time_test_.synced.store(false);
  wifi_time_test_.sync_started.store(false);
  wifi_time_test_.sntp_unix_time.store(0);
  wifi_time_test_.sntp_sync_monotonic_ms.store(0);
  wifi_.connected.store(false);
  wifi_.got_ip.store(false);
  wifi_.ip_address.store(0);
  wifi_.netmask.store(0);
  wifi_.gateway.store(0);
  // 后续任何失败都走 StopWifiTimeTest，确保原 WiFi 配置能恢复。
  wifi_time_test_.active.store(true);

  esp_err_t result = esp_wifi_set_storage(WIFI_STORAGE_RAM);
  if (result != ESP_OK) {
    StopWifiTimeTest();
    return result;
  }

  result = esp_wifi_set_mode(WIFI_MODE_STA);
  if (result != ESP_OK) {
    StopWifiTimeTest();
    return result;
  }

  wifi_config_t wifi_config = {};
  std::strncpy(reinterpret_cast<char*>(wifi_config.sta.ssid), kFactoryWifiSsid,
      sizeof(wifi_config.sta.ssid));
  std::strncpy(reinterpret_cast<char*>(wifi_config.sta.password),
      kFactoryWifiPassword, sizeof(wifi_config.sta.password));
  result = esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
  if (result != ESP_OK) {
    StopWifiTimeTest();
    return result;
  }

  result = esp_wifi_start();
  if (result != ESP_OK) {
    StopWifiTimeTest();
    return result;
  }
  wifi_.running.store(true);

  wifi_.connect_task_running.store(true);
  result = esp_wifi_connect();
  if (result != ESP_OK) {
    wifi_.connect_task_running.store(false);
    StopWifiTimeTest();
    return result;
  }
  return ESP_OK;
}

int TDisplayP4Device::StartWifiSntp() {
  if (wifi_time_test_.sync_started.load()) {
    return ESP_OK;
  }

  StopWifiInternetCheck();
  wifi_time_test_.sntp_unix_time.store(0);
  wifi_time_test_.sntp_sync_monotonic_ms.store(0);
  wifi_time_test_.synced.store(false);
  WifiTimeSyncOwner().store(this);
  esp_sntp_set_time_sync_notification_cb([](struct timeval* time_value) {
    auto* owner = WifiTimeSyncOwner().load();
    if (owner == nullptr || time_value == nullptr) {
      return;
    }

    const int64_t unix_time = static_cast<int64_t>(time_value->tv_sec);
    if (unix_time <= kWifiValidUnixTimeThreshold) {
      return;
    }

    owner->wifi_time_test_.sntp_unix_time.store(unix_time);
    owner->wifi_time_test_.sntp_sync_monotonic_ms.store(
        esp_timer_get_time() / 1000);
    owner->wifi_time_test_.synced.store(true);
    owner->StopWifiInternetCheck();
    owner->ScheduleRtcSync(unix_time);
  });
  // 客户端取时使用轮询模式；成功回调或第三次检测结束后停止客户端。
  esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);
  esp_sntp_set_sync_mode(SNTP_SYNC_MODE_IMMED);
  esp_sntp_setservername(0, kWifiSntpServer);
  const int timer_result = StartWifiSntpAttemptTimer();
  if (timer_result != ESP_OK) {
    StopWifiInternetCheck();
    return timer_result;
  }
  wifi_time_test_.sync_started.store(true);
  esp_sntp_init();
  return ESP_OK;
}

int TDisplayP4Device::StartWifiSntpAttemptTimer() {
  if (wifi_time_test_.sntp_attempt_timer == nullptr) {
    esp_timer_create_args_t timer_config = {};
    timer_config.callback = WifiSntpAttemptTimerCallback;
    timer_config.arg = this;
    timer_config.dispatch_method = ESP_TIMER_TASK;
    timer_config.name = "sntp_attempt";
    const esp_err_t create_result = esp_timer_create(
        &timer_config, &wifi_time_test_.sntp_attempt_timer);
    if (create_result != ESP_OK) {
      return create_result;
    }
  }

  constexpr uint64_t kMicrosecondsPerMillisecond = 1000;
  const uint64_t interval_us =
      static_cast<uint64_t>(kWifiSntpAttemptIntervalMs) *
      kMicrosecondsPerMillisecond;
  wifi_time_test_.sntp_attempt_count.store(1);
  return esp_timer_is_active(wifi_time_test_.sntp_attempt_timer)
      ? esp_timer_restart(wifi_time_test_.sntp_attempt_timer, interval_us)
      : esp_timer_start_periodic(
            wifi_time_test_.sntp_attempt_timer, interval_us);
}

void TDisplayP4Device::WifiSntpAttemptTimerCallback(void* argument) {
  auto* self = static_cast<TDisplayP4Device*>(argument);
  if (self == nullptr) {
    return;
  }

  const int attempt_count =
      self->wifi_time_test_.sntp_attempt_count.load();
  if (self->wifi_time_test_.synced.load() ||
      !self->wifi_.got_ip.load() ||
      attempt_count >= kWifiSntpMaxAttemptCount) {
    self->StopWifiInternetCheck();
    return;
  }

  self->wifi_time_test_.sntp_attempt_count.store(attempt_count + 1);
  if (!esp_sntp_enabled() || !esp_sntp_restart()) {
    self->StopWifiInternetCheck();
  }
}

void TDisplayP4Device::ScheduleRtcSync(int64_t unix_time) {
  const int64_t previous_sync = wifi_time_test_.rtc_sync_unix_time.load();
  bool expected = false;
  if (!wifi_time_test_.rtc_sync_task_running.compare_exchange_strong(
          expected, true)) {
    return;
  }
  wifi_time_test_.rtc_sync_unix_time.store(unix_time);
  const BaseType_t result = xTaskCreate(RtcSyncTaskEntry, "rtc_sync",
      kRtcSyncTaskStackBytes, this, kRtcSyncTaskPriority, nullptr);
  if (result != pdPASS) {
    wifi_time_test_.rtc_sync_task_running.store(false);
    wifi_time_test_.rtc_sync_unix_time.store(previous_sync);
  }
}

void TDisplayP4Device::RtcSyncTaskEntry(void* argument) {
  auto* self = static_cast<TDisplayP4Device*>(argument);
  if (self == nullptr) {
    vTaskDelete(nullptr);
    return;
  }

  const int64_t unix_time = self->wifi_time_test_.rtc_sync_unix_time.load();
  if (!self->WriteRtcUnixTime(unix_time)) {
    self->wifi_time_test_.rtc_sync_unix_time.store(0);
    LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
        "Write network time to PCF8563 failed\n");
  }
  self->wifi_time_test_.rtc_sync_task_running.store(false);
  vTaskDelete(nullptr);
}

void TDisplayP4Device::SetWifiFailure(int error) {
  StopWifiInternetCheck();
  wifi_.init_task_running.store(false);
  wifi_.connect_task_running.store(false);
  wifi_.start_failed.store(true);
  wifi_.last_error.store(error);
  wifi_.connected.store(false);
  wifi_.got_ip.store(false);
  wifi_time_test_.synced.store(false);
  wifi_time_test_.sntp_unix_time.store(0);
  wifi_time_test_.sntp_sync_monotonic_ms.store(0);
  wifi_.ip_address.store(0);
  wifi_.netmask.store(0);
  wifi_.gateway.store(0);
}

void TDisplayP4Device::WifiEventHandler(
    void* arg, const char* event_base, int32_t event_id, void* event_data) {
  (void)event_base;
  auto* self = static_cast<TDisplayP4Device*>(arg);
  if (self == nullptr) {
    return;
  }

  switch (event_id) {
    case WIFI_EVENT_SCAN_DONE:
      if (self->wifi_.scan_running.load() ||
          self->wifi_.scan_task_running.load()) {
        if (self->wifi_.running.load()) {
          self->CopyWifiScanResultsFromDriver();
        } else {
          self->wifi_.scan_network_count.store(0);
          self->wifi_.scan_failed.store(false);
          self->wifi_.last_error.store(ESP_OK);
          self->wifi_.scan_generation.fetch_add(1);
        }
      }
      self->wifi_.scan_running.store(false);
      self->wifi_.scan_task_running.store(false);
      break;
    case WIFI_EVENT_STA_START:
      self->wifi_.running.store(true);
      self->wifi_.start_failed.store(false);
      self->wifi_.last_error.store(ESP_OK);
      break;
    case WIFI_EVENT_STA_CONNECTED: {
      self->wifi_.connected.store(true);
      self->wifi_.got_ip.store(false);
      self->wifi_.retry_count.store(0);
      wifi_ap_record_t ap_info = {};
      if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
        self->wifi_.rssi.store(ap_info.rssi);
        self->wifi_.channel.store(ap_info.primary);
      }
      uint8_t mac_address[6] = {};
      if (esp_wifi_get_mac(WIFI_IF_STA, mac_address) == ESP_OK) {
        self->wifi_.mac_address.store(PackMacAddress(mac_address));
      }
      break;
    }
    case WIFI_EVENT_STA_DISCONNECTED: {
      self->wifi_.connect_task_running.store(false);
      self->wifi_.connected.store(false);
      self->wifi_.got_ip.store(false);
      self->wifi_time_test_.synced.store(false);
      self->wifi_time_test_.sntp_unix_time.store(0);
      self->wifi_time_test_.sntp_sync_monotonic_ms.store(0);
      self->wifi_.ip_address.store(0);
      self->wifi_.netmask.store(0);
      self->wifi_.gateway.store(0);
      self->StopWifiInternetCheck();
      if (event_data != nullptr) {
        const auto* disconnected =
            static_cast<wifi_event_sta_disconnected_t*>(event_data);
        self->wifi_.disconnect_reason.store(disconnected->reason);
      }

      if (self->wifi_time_test_.active.load()) {
        const int retry_count = self->wifi_.retry_count.fetch_add(1) + 1;
        if (retry_count <= kWifiMaxReconnectCount) {
          self->wifi_.connect_task_running.store(true);
          const esp_err_t connect_result = esp_wifi_connect();
          if (connect_result != ESP_OK) {
            self->SetWifiFailure(connect_result);
          }
        } else {
          self->wifi_.start_failed.store(true);
          self->wifi_.last_error.store(ESP_ERR_WIFI_CONN);
        }
      }
      break;
    }
    case WIFI_EVENT_STA_STOP:
      self->wifi_.connect_task_running.store(false);
      self->wifi_.running.store(false);
      self->wifi_.connected.store(false);
      self->wifi_.got_ip.store(false);
      self->wifi_.scan_running.store(false);
      self->wifi_time_test_.synced.store(false);
      self->wifi_time_test_.sntp_unix_time.store(0);
      self->wifi_time_test_.sntp_sync_monotonic_ms.store(0);
      self->wifi_.ip_address.store(0);
      self->wifi_.netmask.store(0);
      self->wifi_.gateway.store(0);
      self->StopWifiInternetCheck();
      break;
    default:
      break;
  }
}

void TDisplayP4Device::WifiGotIpEventHandler(
    void* arg, const char* event_base, int32_t event_id, void* event_data) {
  (void)event_base;
  (void)event_id;
  auto* self = static_cast<TDisplayP4Device*>(arg);
  auto* event = static_cast<ip_event_got_ip_t*>(event_data);
  if (self == nullptr || event == nullptr) {
    return;
  }

  self->wifi_.connected.store(true);
  self->wifi_.connect_task_running.store(false);
  self->wifi_.ip_address.store(event->ip_info.ip.addr);
  self->wifi_.netmask.store(event->ip_info.netmask.addr);
  self->wifi_.gateway.store(event->ip_info.gw.addr);
  self->wifi_.connection_generation.fetch_add(1);
  self->wifi_.got_ip.store(true);
  const int result = self->StartWifiSntp();
  if (result != ESP_OK) {
    self->SetWifiFailure(result);
  }
}

bool TDisplayP4Device::ReadDeviceDiagnostics(DeviceDiagnostics* diagnostics) {
  if (diagnostics == nullptr) {
    return false;
  }

  *diagnostics = DeviceDiagnostics();
  const bool battery_management_result = ReadBatteryManagementStatus(&diagnostics->battery_management);
  const bool imu_result = ReadImuStatus(&diagnostics->imu);
  return battery_management_result || imu_result;
}

bool TDisplayP4Device::ReadBatteryManagementStatus(BatteryManagementStatus* status) {
  if (status == nullptr) {
    return false;
  }

  *status = BatteryManagementStatus();
  status->capabilities.average_measurements = true;
  status->capabilities.capacity = true;
  status->capabilities.remaining_time = true;
  status->capabilities.cycle_count = true;

  if (driver_.IsBq27220Ready()) {
    cpp_bus_driver::Bq27220::BatteryStatus battery_management_status_flags;
    const bool battery_management_status_ok =
        driver_.chip().bq27220->GetBatteryStatus(battery_management_status_flags);
    const uint16_t voltage_mv = driver_.chip().bq27220->GetVoltage();
    const int16_t current_ma = driver_.chip().bq27220->GetCurrent();
    const uint16_t charge_percent = driver_.chip().bq27220->GetStatusOfCharge();

    if (voltage_mv > 0 && voltage_mv != UINT16_MAX) {
      status->ready = true;
      status->voltage_mv = voltage_mv;
      status->current_ma = current_ma;
      status->average_current_ma = driver_.chip().bq27220->GetAverageCurrent();
      status->average_power_mw = driver_.chip().bq27220->GetAveragePower();
      status->charge_percent =
          charge_percent == UINT16_MAX ? 0 : charge_percent;
      status->health_percent = driver_.chip().bq27220->GetStatusOfHealth();
      status->design_capacity_mah = driver_.chip().bq27220->GetDesignCapacity();
      status->remaining_capacity_mah =
          driver_.chip().bq27220->GetRemainingCapacity();
      status->full_charge_capacity_mah =
          driver_.chip().bq27220->GetFullChargeCapacity();
      status->time_to_empty_min = driver_.chip().bq27220->GetTimeToEmpty();
      status->time_to_full_min = driver_.chip().bq27220->GetTimeToFull();
      status->cycle_count = driver_.chip().bq27220->GetCycleCount();
      status->pack_temperature_c =
          driver_.chip().bq27220->GetTemperatureCelsius();
      status->chip_temperature_c =
          driver_.chip().bq27220->GetChipTemperatureCelsius();
      const bool full_charged = battery_management_status_ok &&
          battery_management_status_flags.flag.full_charged;
      const bool idle_or_charging = battery_management_status_ok &&
          !battery_management_status_flags.flag.discharging;
      status->pack_present =
          battery_management_status_ok && battery_management_status_flags.flag.battery_present;
      status->charging = current_ma > 0 ||
          (current_ma == 0 && (full_charged || idle_or_charging));
      status->full_charged = full_charged;
      status->full_discharged =
          battery_management_status_ok && battery_management_status_flags.flag.full_discharged;
      return true;
    }
  }

  return false;
}

bool TDisplayP4Device::ReadBatteryLevel(int* percent) {
  if (percent == nullptr || !driver_.IsBq27220Ready()) {
    return false;
  }

  cpp_bus_driver::Bq27220::BatteryStatus battery_status;
  if (!driver_.chip().bq27220->GetBatteryStatus(battery_status) ||
      !battery_status.flag.battery_present) {
    return false;
  }

  const uint16_t charge_percent =
      driver_.chip().bq27220->GetStatusOfCharge();
  if (charge_percent > 100) {
    return false;
  }

  *percent = charge_percent;
  return true;
}

bool TDisplayP4Device::ReadRtcStatus(RtcStatus* status) {
  if (status == nullptr) {
    return false;
  }

  *status = RtcStatus();

  if (!driver_.IsPcf8563Ready() && !driver_.InitPcf8563()) {
    LogMessage(
        LogLevel::kWarning, __FILE__, __LINE__, "Pcf8563 init retry failed\n");
    return false;
  }

  cpp_bus_driver::Pcf8563x::Time time;
  if (!driver_.chip().pcf8563->GetTime(time)) {
    return false;
  }

  status->ready = true;
  status->clock_integrity = driver_.chip().pcf8563->CheckClockIntegrityFlag();
  status->year = static_cast<uint16_t>(time.year) + 2000;
  status->month = time.month;
  status->day = time.day;
  status->week = static_cast<uint8_t>(time.week);
  status->hour = time.hour;
  status->minute = time.minute;
  status->second = time.second;
  return true;
}

bool TDisplayP4Device::WriteRtcUnixTime(int64_t unix_time) {
  if (unix_time <= kWifiValidUnixTimeThreshold ||
      (!driver_.IsPcf8563Ready() && !driver_.InitPcf8563())) {
    return false;
  }

  const std::time_t time_value = static_cast<std::time_t>(unix_time);
  std::tm local_time = {};
  if (localtime_r(&time_value, &local_time) == nullptr ||
      local_time.tm_year + 1900 < 2000 ||
      local_time.tm_year + 1900 > 2099) {
    return false;
  }

  cpp_bus_driver::Pcf8563x::Time rtc_time;
  rtc_time.year = static_cast<uint8_t>(local_time.tm_year + 1900 - 2000);
  rtc_time.month = static_cast<uint8_t>(local_time.tm_mon + 1);
  rtc_time.day = static_cast<uint8_t>(local_time.tm_mday);
  rtc_time.week = static_cast<cpp_bus_driver::Pcf8563x::Week>(
      local_time.tm_wday);
  rtc_time.hour = static_cast<uint8_t>(local_time.tm_hour);
  rtc_time.minute = static_cast<uint8_t>(local_time.tm_min);
  rtc_time.second = static_cast<uint8_t>(local_time.tm_sec);
  return driver_.chip().pcf8563->SetTime(rtc_time) &&
      driver_.chip().pcf8563->ClearClockIntegrityFlag();
}

bool TDisplayP4Device::ReadRadioCapabilities(RadioCapabilities* capabilities) {
  if (capabilities == nullptr) {
    return false;
  }
  *capabilities = RadioCapabilities();
  radio::ChipType primary_chip = radio::ChipType::kUnknown;
  switch (driver_.radio_type()) {
    case device::RadioType::kSx1262:
      primary_chip = radio::ChipType::kSx1262;
      break;
    case device::RadioType::kLr2021:
      primary_chip = radio::ChipType::kLr2021;
      break;
    case device::RadioType::kUnknown:
      break;
  }
  if (primary_chip != radio::ChipType::kUnknown) {
    RadioCapability& capability =
        capabilities->entries[capabilities->count++];
    capability.chip = primary_chip;
    capability.protocol = radio::ProtocolType::kLora;
    capability.maximum_payload_size = kRadioPayloadCapacity;
    capability.frequency_bands[0] = {
        .minimum_hz = 150000000U,
        .maximum_hz = 960000000U,
    };
    capability.frequency_band_count = 1;
    if (primary_chip == radio::ChipType::kLr2021) {
      capability.frequency_bands[1] = {
          .minimum_hz = 2400000000U,
          .maximum_hz = 2500000000U,
      };
      capability.frequency_band_count = 2;
    }
  }
  if (keyboard_expansion_.state.load() == KeyboardExpansionState::kReady &&
      driver_.IsCc1101Ready()) {
    RadioCapability& cc1101 = capabilities->entries[capabilities->count++];
    cc1101.chip = radio::ChipType::kCc1101;
    cc1101.protocol = radio::ProtocolType::kGfsk;
    cc1101.maximum_payload_size = 60;
    cc1101.frequency_bands[0] = {
        .minimum_hz = 300000000U,
        .maximum_hz = 348000000U,
    };
    cc1101.frequency_bands[1] = {
        .minimum_hz = 387000000U,
        .maximum_hz = 464000000U,
    };
    cc1101.frequency_bands[2] = {
        .minimum_hz = 779000000U,
        .maximum_hz = 928000000U,
    };
    cc1101.frequency_band_count = 3;
  }
  if (keyboard_expansion_.state.load() == KeyboardExpansionState::kReady &&
      driver_.IsNrf24l01Ready()) {
    RadioCapability& nrf24l01 =
        capabilities->entries[capabilities->count++];
    nrf24l01.chip = radio::ChipType::kNrf24l01;
    nrf24l01.protocol = radio::ProtocolType::kEnhancedShockBurst;
    nrf24l01.maximum_payload_size =
        cpp_bus_driver::Nrf24l01x::kMaximumPayloadLength;
    nrf24l01.frequency_bands[0] = {
        .minimum_hz = 2400000000U,
        .maximum_hz = 2525000000U,
    };
    nrf24l01.frequency_band_count = 1;
  }
  capabilities->supports_external_antenna = true;
  return true;
}

bool TDisplayP4Device::InitializeCc1101ReceiveInterrupt() {
  if (cc1101_radio_.receive_interrupt_initialized) {
    return true;
  }
  if (tool_ == nullptr || !driver_.IsCc1101Ready()) {
    return false;
  }

  cc1101_radio_.receive_interrupt_pending.store(
      false, std::memory_order_relaxed);
  if (!tool_->InitGpioInterrupt(keyboard_gpio::t_mix_rf::cc1101::kGdo0,
          cpp_bus_driver::Tool::InterruptMode::kFalling,
          Cc1101ReceiveInterruptHandler, this,
          cpp_bus_driver::Tool::GpioStatus::kDisable)) {
    return false;
  }
  cc1101_radio_.receive_interrupt_initialized = true;
  return true;
}

bool TDisplayP4Device::DeinitializeCc1101ReceiveInterrupt() {
  cc1101_radio_.receive_interrupt_pending.store(
      false, std::memory_order_relaxed);
  if (!cc1101_radio_.receive_interrupt_initialized) {
    return true;
  }

  const bool result = tool_ != nullptr && tool_->DeinitGpioInterrupt(
      keyboard_gpio::t_mix_rf::cc1101::kGdo0);
  cc1101_radio_.receive_interrupt_initialized = false;
  cc1101_radio_.receive_interrupt_pending.store(
      false, std::memory_order_relaxed);
  return result;
}

void TDisplayP4Device::Cc1101ReceiveInterruptHandler(void* context) {
  if (context == nullptr) {
    return;
  }
  auto* device = static_cast<TDisplayP4Device*>(context);
  device->cc1101_radio_.receive_interrupt_pending.store(
      true, std::memory_order_release);
}

TDisplayP4Device::RadioState* TDisplayP4Device::RadioStateForChip(
    radio::ChipType chip) {
  switch (chip) {
    case radio::ChipType::kSx1262:
    case radio::ChipType::kLr2021:
      return &radio_;
    case radio::ChipType::kCc1101:
      return &cc1101_radio_;
    case radio::ChipType::kNrf24l01:
      return &nrf24l01_radio_;
    default:
      return nullptr;
  }
}

TDisplayP4Device::RadioState* TDisplayP4Device::FindRadioState(
    uint32_t client_token) {
  if (client_token == 0) {
    return nullptr;
  }
  RadioState* states[] = {&radio_, &cc1101_radio_, &nrf24l01_radio_};
  for (RadioState* state : states) {
    if (state->active_client_token == client_token) {
      return state;
    }
  }
  return nullptr;
}

bool TDisplayP4Device::ActivateRadio(const RadioConfig& config) {
  RadioState* state = RadioStateForChip(config.chip);
  const bool primary_chip_matches =
      (config.chip == radio::ChipType::kSx1262 &&
           driver_.radio_type() ==
               device::RadioType::kSx1262) ||
          (config.chip == radio::ChipType::kLr2021 &&
              driver_.radio_type() ==
                  device::RadioType::kLr2021);
  const bool expansion_chip_matches =
      config.chip == radio::ChipType::kCc1101 ||
      config.chip == radio::ChipType::kNrf24l01;
  if (state == nullptr || (!primary_chip_matches && !expansion_chip_matches)) {
    return false;
  }
  if (state->mutex == nullptr ||
      xSemaphoreTake(state->mutex, pdMS_TO_TICKS(50)) != pdTRUE) {
    LogMessage(LogLevel::kError, __FILE__, __LINE__,
        "Radio activate failed: mutex unavailable, profile=%lu\n",
        static_cast<unsigned long>(config.client_token));
    return false;
  }
  bool previous_session_stopped = true;
  if (state->active && state->chip == radio::ChipType::kCc1101) {
    previous_session_stopped &= DeinitializeCc1101ReceiveInterrupt();
  }
  if (state->active) {
    if (state->chip == radio::ChipType::kSx1262 &&
        driver_.IsSx1262Ready()) {
      auto* radio = driver_.chip().sx1262.get();
      previous_session_stopped = radio != nullptr &&
          radio->Invoke(sx126x_set_standby, SX126X_STANDBY_CFG_RC) ==
              SX126X_STATUS_OK;
    } else if (state->chip == radio::ChipType::kLr2021 &&
               driver_.IsLr2021Ready()) {
      auto* radio = driver_.chip().lr2021.get();
      previous_session_stopped = radio != nullptr &&
          radio->Invoke(lr20xx_system_set_dio_irq_cfg,
              LR20XX_SYSTEM_DIO_11, LR20XX_SYSTEM_IRQ_NONE) ==
              LR20XX_STATUS_OK &&
          driver_.SetLr2021OperatingMode(
              lilygo_device_driver::TDisplayP4Driver::
                  Lr2021OperatingMode::kStandby);
    } else if (state->chip == radio::ChipType::kCc1101 &&
               driver_.IsCc1101Ready()) {
      auto* radio = driver_.chip().cc1101.get();
      previous_session_stopped &= radio != nullptr && radio->Standby() &&
          driver_.SetCc1101OperatingMode(
              lilygo_device_driver::TDisplayP4Driver::
                  Cc1101OperatingMode::kSleep);
    } else if (state->chip == radio::ChipType::kNrf24l01 &&
               driver_.IsNrf24l01Ready()) {
      auto* radio = driver_.chip().nrf24l01.get();
      previous_session_stopped = radio != nullptr && radio->StopReceive() &&
          driver_.SetNrf24l01OperatingMode(
              lilygo_device_driver::TDisplayP4Driver::
                  Nrf24l01OperatingMode::kSleep);
    }
  }
  if (!previous_session_stopped) {
    state->active = false;
    state->transmitting = false;
    state->chip_error = true;
    xSemaphoreGive(state->mutex);
    LogMessage(LogLevel::kError, __FILE__, __LINE__,
        "Radio activate failed: previous session could not stop\n");
    return false;
  }
  bool result = false;
  if (config.chip == radio::ChipType::kSx1262 &&
      config.protocol == radio::ProtocolType::kLora) {
    usp_cpp_bus_driver::Sx126x::LoraConfig driver_config;
    const bool antenna_supported =
        config.antenna == radio::AntennaType::kInternal ||
        config.antenna == radio::AntennaType::kExternal;
    result = antenna_supported && BuildSx1262Config(
        config.lora, &driver_config);
    if (result) {
      auto* antenna_switch = driver_.chip().xl9535.get();
      const uint8_t antenna_level =
          config.antenna == radio::AntennaType::kExternal ? 0 : 1;
      result = driver_.IsXl9535Ready() && antenna_switch != nullptr &&
               antenna_switch->GpioWrite(
                   gpio::xl9535::kSky13453Vctl, antenna_level);
    }
    if (result) {
      result = driver_.SetSx1262OperatingMode(
          lilygo_device_driver::TDisplayP4Driver::
              Sx1262OperatingMode::kStandby);
    }
    if (result) {
      auto* radio = driver_.chip().sx1262.get();
      result = radio != nullptr && radio->Configure(driver_config) &&
               radio->StartReceive();
    }
    if (!result) {
      driver_.SetSx1262OperatingMode(
          lilygo_device_driver::TDisplayP4Driver::
              Sx1262OperatingMode::kSleep);
    }
  } else if (config.chip == radio::ChipType::kLr2021 &&
             config.protocol == radio::ProtocolType::kLora) {
    const bool antenna_supported =
        config.antenna == radio::AntennaType::kInternal ||
        config.antenna == radio::AntennaType::kExternal;
    result = antenna_supported && driver_.IsLr2021Ready();
    if (result) {
      auto* antenna_switch = driver_.chip().xl9535.get();
      const uint8_t antenna_level =
          config.antenna == radio::AntennaType::kExternal ? 0 : 1;
      result = driver_.IsXl9535Ready() && antenna_switch != nullptr &&
          antenna_switch->GpioWrite(
              gpio::xl9535::kSky13453Vctl, antenna_level);
    }
    if (result) {
      result = driver_.SetLr2021OperatingMode(
          lilygo_device_driver::TDisplayP4Driver::
              Lr2021OperatingMode::kStandby);
    }
    if (result) {
      result = StartLr2021Receive(
          driver_.chip().lr2021.get(), config.lora);
    }
    if (!result) {
      driver_.SetLr2021OperatingMode(
          lilygo_device_driver::TDisplayP4Driver::
              Lr2021OperatingMode::kSleep);
    }
  } else if (config.chip == radio::ChipType::kCc1101 &&
             config.protocol == radio::ProtocolType::kGfsk &&
             config.antenna == radio::AntennaType::kInternal &&
             keyboard_expansion_.state.load() ==
                 KeyboardExpansionState::kReady) {
    cpp_bus_driver::Cc1101::Config driver_config;
    lilygo_device_driver::TDisplayP4Driver::Cc1101RfSwitch rf_switch;
    result = driver_.IsCc1101Ready() &&
             BuildCc1101Config(config.gfsk, &driver_config) &&
             SelectCc1101RfSwitch(config.gfsk.frequency_hz, &rf_switch) &&
             driver_.SetCc1101RfSwitch(rf_switch) &&
             driver_.SetCc1101OperatingMode(
                 lilygo_device_driver::TDisplayP4Driver::
                     Cc1101OperatingMode::kStandby);
    if (result) {
      auto* radio = driver_.chip().cc1101.get();
      result = radio != nullptr && radio->Configure(driver_config);
      if (result) {
        result = InitializeCc1101ReceiveInterrupt();
      }
      if (result) {
        cc1101_radio_.receive_interrupt_pending.store(
            false, std::memory_order_relaxed);
        result = radio->StartReceive();
      }
    }
    if (!result) {
      DeinitializeCc1101ReceiveInterrupt();
      driver_.SetCc1101OperatingMode(
          lilygo_device_driver::TDisplayP4Driver::
              Cc1101OperatingMode::kSleep);
    }
  } else if (config.chip == radio::ChipType::kNrf24l01 &&
             config.protocol ==
                 radio::ProtocolType::kEnhancedShockBurst &&
             config.antenna == radio::AntennaType::kInternal &&
             keyboard_expansion_.state.load() ==
                 KeyboardExpansionState::kReady) {
    cpp_bus_driver::Nrf24l01x::Config driver_config;
    result = driver_.IsNrf24l01Ready() && BuildNrf24l01Config(
        config.enhanced_shock_burst, &driver_config) &&
        driver_.SetNrf24l01OperatingMode(
            lilygo_device_driver::TDisplayP4Driver::
                Nrf24l01OperatingMode::kStandby);
    if (result) {
      uint8_t address[5] = {};
      EncodeNrf24l01Address(
          config.enhanced_shock_burst.address, address);
      auto* radio = driver_.chip().nrf24l01.get();
      const size_t address_width =
          config.enhanced_shock_burst.address_width;
      result = radio != nullptr && radio->Configure(driver_config) &&
               radio->SetAddress(cpp_bus_driver::Nrf24l01x::Address::kPipe0,
                   address, address_width) &&
               radio->SetAddress(
                   cpp_bus_driver::Nrf24l01x::Address::kTransmit,
                   address, address_width) &&
               radio->StartReceive();
    }
    if (!result) {
      driver_.SetNrf24l01OperatingMode(
          lilygo_device_driver::TDisplayP4Driver::
              Nrf24l01OperatingMode::kSleep);
    }
  }
  state->active = result;
  state->transmitting = false;
  state->chip_error = !result;
  state->active_client_token = config.client_token;
  state->transmit_request_token = 0;
  state->transmit_deadline_us = 0;
  state->lora_config = config.lora;
  state->gfsk_config = config.gfsk;
  state->enhanced_shock_burst_config = config.enhanced_shock_burst;
  state->chip = config.chip;
  state->protocol = config.protocol;
  state->pending_event = RadioEvent();
  xSemaphoreGive(state->mutex);
  LogMessage(result ? LogLevel::kDebug : LogLevel::kError, __FILE__, __LINE__,
      "Radio activate %s: profile=%lu, chip=%u, protocol=%u\n",
      result ? "succeeded" : "failed",
      static_cast<unsigned long>(config.client_token),
      static_cast<unsigned>(config.chip),
      static_cast<unsigned>(config.protocol));
  return result;
}

bool TDisplayP4Device::DeactivateRadio() {
  bool result = true;
  RadioState* states[] = {&radio_, &cc1101_radio_, &nrf24l01_radio_};
  for (RadioState* state : states) {
    if (state->active || state->active_client_token != 0) {
      result &= DeactivateRadioState(state);
    }
  }
  return result;
}

bool TDisplayP4Device::DeactivateRadio(uint32_t client_token) {
  if (client_token == 0) {
    return DeactivateRadio();
  }
  RadioState* state = FindRadioState(client_token);
  return state != nullptr && DeactivateRadioState(state);
}

bool TDisplayP4Device::DeactivateRadioState(RadioState* state) {
  if (state->mutex == nullptr ||
      xSemaphoreTake(state->mutex, pdMS_TO_TICKS(2000)) != pdTRUE) {
    LogMessage(LogLevel::kError, __FILE__, __LINE__,
        "Radio deactivate failed: mutex unavailable\n");
    return false;
  }
  bool result = true;
  if (state->chip == radio::ChipType::kCc1101) {
    result &= DeinitializeCc1101ReceiveInterrupt();
  }
  if (state->chip == radio::ChipType::kSx1262 && driver_.IsSx1262Ready()) {
    auto* radio = driver_.chip().sx1262.get();
    if (state->active) {
      result = radio != nullptr &&
               radio->Invoke(sx126x_set_standby, SX126X_STANDBY_CFG_RC) ==
                   SX126X_STATUS_OK &&
               radio->ClearIrqStatus(SX126X_IRQ_ALL);
    }
    result &= driver_.SetSx1262OperatingMode(
        lilygo_device_driver::TDisplayP4Driver::
            Sx1262OperatingMode::kStandby);
  } else if (state->chip == radio::ChipType::kLr2021 &&
             driver_.IsLr2021Ready()) {
    auto* radio = driver_.chip().lr2021.get();
    if (state->active) {
      result = radio != nullptr &&
          radio->Invoke(lr20xx_system_set_dio_irq_cfg,
              LR20XX_SYSTEM_DIO_11, LR20XX_SYSTEM_IRQ_NONE) ==
              LR20XX_STATUS_OK &&
          radio->Invoke(lr20xx_system_clear_irq_status,
              LR20XX_SYSTEM_IRQ_ALL_MASK) == LR20XX_STATUS_OK;
    }
    result &= driver_.SetLr2021OperatingMode(
        lilygo_device_driver::TDisplayP4Driver::
            Lr2021OperatingMode::kStandby);
  } else if (state->chip == radio::ChipType::kCc1101 &&
             keyboard_expansion_.state.load() ==
                 KeyboardExpansionState::kReady &&
             driver_.IsCc1101Ready()) {
    auto* radio = driver_.chip().cc1101.get();
    result &= radio != nullptr && radio->Standby() && radio->FlushRx() &&
              radio->FlushTx();
    result &= driver_.SetCc1101OperatingMode(
        lilygo_device_driver::TDisplayP4Driver::
            Cc1101OperatingMode::kSleep);
  } else if (state->chip == radio::ChipType::kNrf24l01 &&
             keyboard_expansion_.state.load() ==
                 KeyboardExpansionState::kReady &&
             driver_.IsNrf24l01Ready()) {
    auto* radio = driver_.chip().nrf24l01.get();
    result = radio != nullptr && radio->StopReceive() && radio->FlushRx() &&
             radio->FlushTx();
    result &= driver_.SetNrf24l01OperatingMode(
        lilygo_device_driver::TDisplayP4Driver::
            Nrf24l01OperatingMode::kSleep);
  }
  state->active = false;
  state->transmitting = false;
  state->chip_error = !result;
  state->active_client_token = 0;
  state->transmit_request_token = 0;
  state->transmit_deadline_us = 0;
  state->chip = radio::ChipType::kUnknown;
  state->protocol = radio::ProtocolType::kUnknown;
  state->pending_event = RadioEvent();
  xSemaphoreGive(state->mutex);
  LogMessage(result ? LogLevel::kInfo : LogLevel::kError, __FILE__, __LINE__,
      "Radio deactivate %s\n", result ? "succeeded" : "failed");
  return result;
}

bool TDisplayP4Device::SendRadio(
    const uint8_t* data, size_t size, uint64_t request_token) {
  RadioState* selected = nullptr;
  RadioState* states[] = {&radio_, &cc1101_radio_, &nrf24l01_radio_};
  for (RadioState* state : states) {
    if (!state->active) {
      continue;
    }
    if (selected != nullptr) {
      return false;
    }
    selected = state;
  }
  return selected != nullptr && SendRadio(
      selected->active_client_token, data, size, request_token);
}

bool TDisplayP4Device::SendRadio(uint32_t client_token,
    const uint8_t* data, size_t size, uint64_t request_token) {
  RadioState* state = FindRadioState(client_token);
  if (state == nullptr) {
    return false;
  }
  if (data == nullptr || size == 0 || size > kRadioPayloadCapacity ||
      request_token == 0) {
    LogMessage(LogLevel::kError, __FILE__, __LINE__,
        "Radio send rejected: invalid request, message=%lu, size=%u bytes\n",
        static_cast<unsigned long>(static_cast<uint32_t>(request_token)),
        static_cast<unsigned>(size));
    return false;
  }
  if (state->mutex == nullptr ||
      xSemaphoreTake(state->mutex, pdMS_TO_TICKS(50)) != pdTRUE) {
    LogMessage(LogLevel::kError, __FILE__, __LINE__,
        "Radio send rejected: radio is busy, message=%lu\n",
        static_cast<unsigned long>(static_cast<uint32_t>(request_token)));
    return false;
  }
  if (!state->active) {
    LogMessage(LogLevel::kError, __FILE__, __LINE__,
        "Radio send rejected: profile %lu is inactive, message=%lu\n",
        static_cast<unsigned long>(state->active_client_token),
        static_cast<unsigned long>(static_cast<uint32_t>(request_token)));
    xSemaphoreGive(state->mutex);
    return false;
  }
  if (state->transmitting) {
    LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
        "Radio send rejected: message %lu is still transmitting, "
        "new message=%lu\n",
        static_cast<unsigned long>(
            static_cast<uint32_t>(state->transmit_request_token)),
        static_cast<unsigned long>(static_cast<uint32_t>(request_token)));
    xSemaphoreGive(state->mutex);
    return false;
  }
  bool result = false;
  bool send_failure_is_chip_error = true;
  uint32_t estimated_time_ms = 0;
  if (state->chip == radio::ChipType::kSx1262 &&
      state->protocol == radio::ProtocolType::kLora &&
      driver_.IsSx1262Ready()) {
    LoraTransmitTiming timing;
    if (CalculateLoraTransmitTiming(state->lora_config, size, &timing)) {
      auto* radio = driver_.chip().sx1262.get();
      result = radio != nullptr && radio->StartTransmit(
          data, size, timing.hardware_timeout_ms);
      if (result) {
        state->transmit_deadline_us = esp_timer_get_time() +
            static_cast<int64_t>(timing.watchdog_timeout_ms) * 1000;
        estimated_time_ms = timing.time_on_air_ms;
      }
    }
  } else if (state->chip == radio::ChipType::kLr2021 &&
             state->protocol == radio::ProtocolType::kLora &&
             driver_.IsLr2021Ready()) {
    LoraTransmitTiming timing;
    usp_cpp_bus_driver::Lr20xx::LoraConfig driver_config;
    if (CalculateLoraTransmitTiming(state->lora_config, size, &timing) &&
        BuildLr2021Config(state->lora_config, static_cast<uint8_t>(size),
            &driver_config)) {
      auto* radio = driver_.chip().lr2021.get();
      result = radio != nullptr && radio->Configure(driver_config) &&
          radio->Invoke(lr20xx_system_clear_irq_status,
              LR20XX_SYSTEM_IRQ_ALL_MASK) == LR20XX_STATUS_OK &&
          radio->WriteBuffer(data, size) &&
          radio->StartTransmit(timing.hardware_timeout_ms);
      if (result) {
        state->transmit_deadline_us = esp_timer_get_time() +
            static_cast<int64_t>(timing.watchdog_timeout_ms) * 1000;
        estimated_time_ms = timing.time_on_air_ms;
      }
    }
  } else if (state->chip == radio::ChipType::kCc1101 &&
             state->protocol == radio::ProtocolType::kGfsk &&
             driver_.IsCc1101Ready() && size <= 60) {
    auto* radio = driver_.chip().cc1101.get();
    std::array<uint8_t, 60> fixed_payload = {};
    const uint8_t* transmit_data = data;
    size_t transmit_size = size;
    if (state->gfsk_config.fec_enabled) {
      std::copy_n(data, size, fixed_payload.begin());
      transmit_data = fixed_payload.data();
      transmit_size = fixed_payload.size();
    }
    state->receive_interrupt_pending.store(
        false, std::memory_order_relaxed);
    const bool transmitted = radio != nullptr &&
        radio->Transmit(transmit_data, transmit_size);
    // GDO0 在发送结束时也会产生下降沿，重新进入 RX 前丢弃该通知。
    state->receive_interrupt_pending.store(
        false, std::memory_order_relaxed);
    const bool receive_restarted = radio != nullptr && radio->StartReceive();
    result = transmitted && receive_restarted;
    send_failure_is_chip_error = !receive_restarted;
  } else if (state->chip == radio::ChipType::kNrf24l01 &&
             state->protocol ==
                 radio::ProtocolType::kEnhancedShockBurst &&
             driver_.IsNrf24l01Ready() &&
             size <= cpp_bus_driver::Nrf24l01x::kMaximumPayloadLength) {
    auto* radio = driver_.chip().nrf24l01.get();
    if (radio != nullptr) {
      std::array<uint8_t,
          cpp_bus_driver::Nrf24l01x::kMaximumPayloadLength> fixed_payload = {};
      const uint8_t* transmit_data = data;
      size_t transmit_size = size;
      if (!state->enhanced_shock_burst_config.dynamic_payload_enabled) {
        std::copy_n(data, size, fixed_payload.begin());
        transmit_data = fixed_payload.data();
        transmit_size = fixed_payload.size();
      }
      const cpp_bus_driver::Nrf24l01x::TransmitResult transmit_result =
          radio->Transmit(transmit_data, transmit_size, false, 250);
      result = transmit_result ==
          cpp_bus_driver::Nrf24l01x::TransmitResult::kSuccess;
      if (result) {
        result = radio->StartReceive();
      } else {
        const bool receive_restarted = radio->StartReceive();
        send_failure_is_chip_error =
            transmit_result ==
                cpp_bus_driver::Nrf24l01x::TransmitResult::kBusError ||
            transmit_result ==
                cpp_bus_driver::Nrf24l01x::TransmitResult::kInvalidArgument ||
            !receive_restarted;
        LogMessage(LogLevel::kError, __FILE__, __LINE__,
            "nRF24L01 transmit failed: result=%s, auto_ack=%s, "
            "channel=%u, data_rate=%lu, receive_recovery=%s\n",
            Nrf24l01TransmitResultName(transmit_result),
            state->enhanced_shock_burst_config.auto_ack_enabled
                ? "enabled"
                : "disabled",
            static_cast<unsigned>(
                state->enhanced_shock_burst_config.channel),
            static_cast<unsigned long>(
                state->enhanced_shock_burst_config.data_rate_bps),
            receive_restarted ? "succeeded" : "failed");
      }
    }
  }
  state->transmitting = result;
  state->chip_error = !result && send_failure_is_chip_error;
  state->transmit_request_token = result ? request_token : 0;
  if (result && state->chip != radio::ChipType::kSx1262 &&
      state->chip != radio::ChipType::kLr2021) {
    state->pending_event = RadioEvent();
    state->pending_event.type = RadioEventType::kTransmitComplete;
    state->pending_event.client_token = state->active_client_token;
    state->pending_event.request_token = request_token;
  }
  if (!result) {
    state->transmit_deadline_us = 0;
  }
  const uint32_t profile_id = state->active_client_token;
  xSemaphoreGive(state->mutex);
  if (result) {
    LogMessage(LogLevel::kDebug, __FILE__, __LINE__,
        "Radio send started: profile %lu, %u bytes, estimated %lu ms\n",
        static_cast<unsigned long>(profile_id), static_cast<unsigned>(size),
        static_cast<unsigned long>(estimated_time_ms));
  } else {
    LogMessage(LogLevel::kError, __FILE__, __LINE__,
        "Radio send start failed: profile=%lu, message=%lu, size=%u bytes\n",
        static_cast<unsigned long>(profile_id),
        static_cast<unsigned long>(static_cast<uint32_t>(request_token)),
        static_cast<unsigned>(size));
  }
  return result;
}

bool TDisplayP4Device::PollRadioEvent(RadioEvent* event) {
  if (event == nullptr) {
    return false;
  }
  *event = RadioEvent();
  RadioState* states[] = {&radio_, &cc1101_radio_, &nrf24l01_radio_};
  bool result = true;
  for (size_t offset = 0; offset < std::size(states); ++offset) {
    const size_t index = (radio_poll_index_ + offset) % std::size(states);
    RadioEvent candidate;
    const bool poll_result = PollRadioState(states[index], &candidate);
    result &= poll_result;
    if (candidate.type != RadioEventType::kNone) {
      *event = candidate;
      radio_poll_index_ = static_cast<uint8_t>(
          (index + 1) % std::size(states));
      return poll_result;
    }
  }
  radio_poll_index_ = static_cast<uint8_t>(
      (radio_poll_index_ + 1) % std::size(states));
  return result;
}

bool TDisplayP4Device::PollRadioState(
    RadioState* state, RadioEvent* event) {
  if (event == nullptr) {
    return false;
  }
  *event = RadioEvent();
  if (state->mutex == nullptr ||
      xSemaphoreTake(state->mutex, pdMS_TO_TICKS(20)) != pdTRUE) {
    LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
        "Radio event poll failed: mutex unavailable\n");
    return false;
  }
  event->client_token = state->active_client_token;
  event->request_token = state->transmit_request_token;
  if (!state->active) {
    xSemaphoreGive(state->mutex);
    return true;
  }
  if (state->pending_event.type != RadioEventType::kNone) {
    *event = state->pending_event;
    state->pending_event = RadioEvent();
    state->transmitting = false;
    state->transmit_request_token = 0;
    xSemaphoreGive(state->mutex);
    return true;
  }
  const bool hardware_ready =
      (state->chip == radio::ChipType::kSx1262 &&
          driver_.IsSx1262Ready()) ||
      (state->chip == radio::ChipType::kLr2021 &&
          driver_.IsLr2021Ready()) ||
      (state->chip == radio::ChipType::kCc1101 &&
          keyboard_expansion_.state.load() ==
              KeyboardExpansionState::kReady &&
          driver_.IsCc1101Ready()) ||
      (state->chip == radio::ChipType::kNrf24l01 &&
          keyboard_expansion_.state.load() ==
              KeyboardExpansionState::kReady &&
          driver_.IsNrf24l01Ready());
  if (!hardware_ready) {
    if (state->chip == radio::ChipType::kCc1101) {
      DeinitializeCc1101ReceiveInterrupt();
    }
    state->active = false;
    state->transmitting = false;
    state->chip_error = true;
    event->type = RadioEventType::kChipError;
    event->failure_reason = RadioFailureReason::kHardwareUnavailable;
    state->transmit_request_token = 0;
    state->transmit_deadline_us = 0;
    xSemaphoreGive(state->mutex);
    LogMessage(LogLevel::kError, __FILE__, __LINE__,
        "Radio event failed: chip is unavailable, profile=%lu, message=%lu\n",
        static_cast<unsigned long>(event->client_token),
        static_cast<unsigned long>(
            static_cast<uint32_t>(event->request_token)));
    return false;
  }

  if (state->chip == radio::ChipType::kCc1101) {
    if (!state->receive_interrupt_pending.exchange(
            false, std::memory_order_acq_rel)) {
      xSemaphoreGive(state->mutex);
      return true;
    }
    auto* radio = driver_.chip().cc1101.get();
    cpp_bus_driver::Cc1101::PacketMetrics metrics;
    size_t received = 0;
    const bool packet_received = radio != nullptr &&
        radio->ReadReceivedPacket(event->payload, 60, &received, &metrics);
    const bool receive_restarted = radio != nullptr && radio->StartReceive();
    state->active = receive_restarted;
    state->chip_error = !receive_restarted;
    if (!receive_restarted) {
      event->type = RadioEventType::kChipError;
      event->failure_reason = RadioFailureReason::kReceiveRestartFailed;
    } else if (packet_received) {
      if (state->gfsk_config.fec_enabled) {
        while (received > 0 && event->payload[received - 1] == 0) {
          --received;
        }
      }
      event->type = RadioEventType::kPacketReceived;
      event->payload_size = received;
      event->rssi_dbm = static_cast<int8_t>(std::clamp(
          metrics.rssi_dbm, -128.0F, 127.0F));
      event->snr_db = 0;
      event->rssi_valid = true;
      event->snr_valid = false;
    }
    xSemaphoreGive(state->mutex);
    return receive_restarted;
  }

  if (state->chip == radio::ChipType::kNrf24l01) {
    auto* radio = driver_.chip().nrf24l01.get();
    bool fifo_empty = true;
    if (radio == nullptr || !radio->RxFifoEmpty(&fifo_empty)) {
      state->active = false;
      state->chip_error = true;
      event->type = RadioEventType::kChipError;
      event->failure_reason = RadioFailureReason::kIrqReadFailed;
      xSemaphoreGive(state->mutex);
      return false;
    }
    if (!fifo_empty) {
      size_t received = 0;
      const bool received_ok = radio->ReadRxPayload(
          event->payload, cpp_bus_driver::Nrf24l01x::kMaximumPayloadLength,
          &received);
      bool fifo_empty_after_read = true;
      const bool status_ok = received_ok &&
          radio->RxFifoEmpty(&fifo_empty_after_read) &&
          (!fifo_empty_after_read || radio->ClearIrqFlag(
              cpp_bus_driver::Nrf24l01x::IrqSource::kRxDataReady));
      if (status_ok) {
        if (!state->enhanced_shock_burst_config.dynamic_payload_enabled) {
          while (received > 0 && event->payload[received - 1] == 0) {
            --received;
          }
        }
        event->type = RadioEventType::kPacketReceived;
        event->payload_size = received;
        event->rssi_valid = false;
        event->snr_valid = false;
      } else {
        state->active = false;
        state->chip_error = true;
        event->type = RadioEventType::kChipError;
        event->failure_reason = RadioFailureReason::kIrqClearFailed;
      }
      xSemaphoreGive(state->mutex);
      return status_ok;
    }
    xSemaphoreGive(state->mutex);
    return true;
  }

  if (state->chip == radio::ChipType::kLr2021) {
    auto* radio = driver_.chip().lr2021.get();
    lr20xx_system_irq_mask_t irq_mask = LR20XX_SYSTEM_IRQ_NONE;
    if (radio == nullptr ||
        radio->Invoke(lr20xx_system_get_and_clear_irq_status, &irq_mask) !=
            LR20XX_STATUS_OK) {
      state->active = false;
      state->transmitting = false;
      state->chip_error = true;
      event->type = RadioEventType::kChipError;
      event->failure_reason = RadioFailureReason::kIrqReadFailed;
      state->transmit_request_token = 0;
      state->transmit_deadline_us = 0;
      xSemaphoreGive(state->mutex);
      return false;
    }
    if (irq_mask == LR20XX_SYSTEM_IRQ_NONE) {
      if (state->transmitting && state->transmit_deadline_us > 0 &&
          esp_timer_get_time() >= state->transmit_deadline_us) {
        const bool recovered = StartLr2021Receive(radio, state->lora_config);
        state->transmitting = false;
        state->active = recovered;
        state->chip_error = !recovered;
        state->transmit_request_token = 0;
        state->transmit_deadline_us = 0;
        event->type = RadioEventType::kTransmitFailed;
        event->failure_reason = RadioFailureReason::kSoftwareTimeout;
        xSemaphoreGive(state->mutex);
        return recovered;
      }
      xSemaphoreGive(state->mutex);
      return true;
    }

    const bool timed_out =
        (irq_mask & LR20XX_SYSTEM_IRQ_TIMEOUT) != 0;
    const bool tx_done =
        (irq_mask & LR20XX_SYSTEM_IRQ_TX_DONE) != 0;
    const bool rx_done =
        (irq_mask & LR20XX_SYSTEM_IRQ_RX_DONE) != 0;
    const bool receive_error =
        (irq_mask & (LR20XX_SYSTEM_IRQ_CRC_ERROR |
            LR20XX_SYSTEM_IRQ_LEN_ERROR |
            LR20XX_SYSTEM_IRQ_LORA_HEADER_ERROR)) != 0;
    const bool chip_error =
        (irq_mask &
            (LR20XX_SYSTEM_IRQ_ERROR | LR20XX_SYSTEM_IRQ_CMD_ERROR)) != 0;
    bool result = !chip_error;
    if (state->transmitting && (tx_done || timed_out)) {
      state->transmitting = false;
      state->transmit_request_token = 0;
      state->transmit_deadline_us = 0;
      const bool receive_restarted =
          StartLr2021Receive(radio, state->lora_config);
      state->active = receive_restarted;
      state->chip_error = !receive_restarted;
      event->type = timed_out ? RadioEventType::kTransmitFailed
                              : RadioEventType::kTransmitComplete;
      event->failure_reason = timed_out
          ? RadioFailureReason::kHardwareTimeout
          : (receive_restarted ? RadioFailureReason::kNone
                               : RadioFailureReason::kReceiveRestartFailed);
      result = receive_restarted;
    } else if (state->transmitting) {
      result = true;
    } else if (rx_done && !receive_error && !chip_error) {
      uint16_t received_size = 0;
      lr20xx_radio_lora_packet_status_t metrics = {};
      result = radio->Invoke(lr20xx_radio_common_get_rx_packet_length,
                   &received_size) == LR20XX_STATUS_OK &&
          received_size > 0 && received_size <= kRadioPayloadCapacity &&
          radio->ReadBuffer(event->payload, received_size) &&
          radio->Invoke(lr20xx_radio_lora_get_packet_status, &metrics) ==
              LR20XX_STATUS_OK &&
          StartLr2021Receive(radio, state->lora_config);
      if (result) {
        event->type = RadioEventType::kPacketReceived;
        event->payload_size = received_size;
        const int16_t rssi_dbm = metrics.rssi_pkt_in_dbm -
            static_cast<int16_t>(metrics.rssi_pkt_half_dbm_count) / 2;
        event->rssi_dbm = static_cast<int8_t>(
            std::clamp<int16_t>(rssi_dbm, INT8_MIN, INT8_MAX));
        event->snr_db = static_cast<int8_t>(metrics.snr_pkt_raw / 4);
        event->rssi_valid = true;
        event->snr_valid = true;
      }
    } else {
      result = !chip_error &&
          StartLr2021Receive(radio, state->lora_config);
    }
    if (!result) {
      state->active = false;
      state->transmitting = false;
      state->chip_error = true;
      state->transmit_request_token = 0;
      state->transmit_deadline_us = 0;
      if (event->type == RadioEventType::kNone) {
        event->type = RadioEventType::kChipError;
      }
      if (event->failure_reason == RadioFailureReason::kNone) {
        event->failure_reason = chip_error
            ? RadioFailureReason::kHardwareUnavailable
            : RadioFailureReason::kReceiveRestartFailed;
      }
    }
    xSemaphoreGive(state->mutex);
    return result;
  }

  auto* radio = driver_.chip().sx1262.get();
  sx126x_irq_mask_t irq_mask = SX126X_IRQ_NONE;
  if (radio == nullptr || !radio->GetIrqStatus(irq_mask)) {
    state->active = false;
    state->transmitting = false;
    state->chip_error = true;
    event->type = RadioEventType::kChipError;
    event->failure_reason = RadioFailureReason::kIrqReadFailed;
    state->transmit_request_token = 0;
    state->transmit_deadline_us = 0;
    xSemaphoreGive(state->mutex);
    LogMessage(LogLevel::kError, __FILE__, __LINE__,
        "Radio event failed: cannot read IRQ, profile=%lu, message=%lu\n",
        static_cast<unsigned long>(event->client_token),
        static_cast<unsigned long>(
            static_cast<uint32_t>(event->request_token)));
    return false;
  }
  if (irq_mask == SX126X_IRQ_NONE) {
    if (state->transmitting && state->transmit_deadline_us > 0 &&
        esp_timer_get_time() >= state->transmit_deadline_us) {
      const bool recovered = radio->StartReceive();
      state->transmitting = false;
      state->active = recovered;
      state->chip_error = !recovered;
      state->transmit_request_token = 0;
      state->transmit_deadline_us = 0;
      event->type = RadioEventType::kTransmitFailed;
      event->failure_reason = RadioFailureReason::kSoftwareTimeout;
      xSemaphoreGive(state->mutex);
      LogMessage(LogLevel::kError, __FILE__, __LINE__,
          "Radio send failed: software timeout, profile=%lu, message=%lu, "
          "receive recovery=%s\n",
          static_cast<unsigned long>(event->client_token),
          static_cast<unsigned long>(
              static_cast<uint32_t>(event->request_token)),
          recovered ? "succeeded" : "failed");
      return recovered;
    }
    xSemaphoreGive(state->mutex);
    return true;
  }

  const bool timed_out = (irq_mask & SX126X_IRQ_TIMEOUT) != 0;
  const bool tx_done = (irq_mask & SX126X_IRQ_TX_DONE) != 0;
  const bool rx_done = (irq_mask & SX126X_IRQ_RX_DONE) != 0;
  const bool receive_error =
      (irq_mask & (SX126X_IRQ_HEADER_ERROR | SX126X_IRQ_CRC_ERROR)) != 0;
  char irq_text[kRadioIrqTextCapacity] = {};
  bool irq_text_ready = false;
  const auto irq_text_for_log = [&]() -> const char* {
    if (!irq_text_ready) {
      FormatRadioIrqMask(irq_mask, irq_text, sizeof(irq_text));
      irq_text_ready = true;
    }
    return irq_text;
  };
  bool result = radio->ClearIrqStatus(irq_mask);
  if (!result) {
    state->active = false;
    state->transmitting = false;
    state->chip_error = true;
    state->transmit_request_token = 0;
    state->transmit_deadline_us = 0;
    event->type = RadioEventType::kChipError;
    event->failure_reason = RadioFailureReason::kIrqClearFailed;
    xSemaphoreGive(state->mutex);
    LogMessage(LogLevel::kError, __FILE__, __LINE__,
        "Radio event failed: cannot clear IRQ %s, message=%lu\n",
        irq_text_for_log(),
        static_cast<unsigned long>(
            static_cast<uint32_t>(event->request_token)));
    return false;
  }
  if (state->transmitting && (tx_done || timed_out)) {
    state->transmitting = false;
    state->transmit_request_token = 0;
    state->transmit_deadline_us = 0;
    const bool receive_restarted = radio->StartReceive();
    state->active = receive_restarted;
    state->chip_error = !receive_restarted;
    if (timed_out) {
      event->type = RadioEventType::kTransmitFailed;
      event->failure_reason = RadioFailureReason::kHardwareTimeout;
    } else {
      event->type = RadioEventType::kTransmitComplete;
      if (!receive_restarted) {
        event->failure_reason = RadioFailureReason::kReceiveRestartFailed;
      }
    }
    result = receive_restarted;
    if (event->type == RadioEventType::kTransmitComplete && receive_restarted) {
      LogMessage(LogLevel::kDebug, __FILE__, __LINE__,
          "Radio send completed: profile %lu\n",
          static_cast<unsigned long>(event->client_token));
    } else if (event->type == RadioEventType::kTransmitFailed) {
      LogMessage(LogLevel::kError, __FILE__, __LINE__,
          "Radio send failed: hardware timeout, profile=%lu, message=%lu, "
          "receive recovery=%s\n",
          static_cast<unsigned long>(event->client_token),
          static_cast<unsigned long>(
              static_cast<uint32_t>(event->request_token)),
          receive_restarted ? "succeeded" : "failed");
    } else {
      LogMessage(LogLevel::kError, __FILE__, __LINE__,
          "Radio send completed, but receive restart failed: profile=%lu, "
          "message=%lu\n",
          static_cast<unsigned long>(event->client_token),
          static_cast<unsigned long>(
              static_cast<uint32_t>(event->request_token)));
    }
  } else if (state->transmitting) {
    LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
        "Radio send ignored unrelated IRQ %s, message=%lu\n",
        irq_text_for_log(),
        static_cast<unsigned long>(
            static_cast<uint32_t>(event->request_token)));
  } else if (rx_done && !receive_error) {
    uint8_t received_size = 0;
    usp_cpp_bus_driver::Sx126x::PacketMetrics metrics;
    result = radio->ReadPacket(
        event->payload, kRadioPayloadCapacity, received_size, &metrics);
    result = result && radio->StartReceive();
    if (result) {
      event->type = RadioEventType::kPacketReceived;
      event->payload_size = received_size;
      event->rssi_dbm = metrics.rssi_dbm;
      event->snr_db = metrics.snr_db;
    }
  } else {
    result = radio->StartReceive();
    if (receive_error) {
      LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
          "Radio RX packet rejected: IRQ=%s\n", irq_text_for_log());
    }
  }

  if (!result) {
    state->active = false;
    state->transmitting = false;
    state->chip_error = true;
    if (event->type != RadioEventType::kTransmitComplete) {
      event->type = RadioEventType::kChipError;
    }
    if (event->failure_reason == RadioFailureReason::kNone) {
      event->failure_reason = RadioFailureReason::kReceiveRestartFailed;
    }
    state->transmit_request_token = 0;
    state->transmit_deadline_us = 0;
    LogMessage(LogLevel::kError, __FILE__, __LINE__,
        "Radio event processing failed: profile=%lu, message=%lu, IRQ=%s\n",
        static_cast<unsigned long>(event->client_token),
        static_cast<unsigned long>(
            static_cast<uint32_t>(event->request_token)),
        irq_text_for_log());
  }
  xSemaphoreGive(state->mutex);
  return result;
}

bool TDisplayP4Device::ReadRadioStatus(RadioStatus* status) {
  RadioState* selected = nullptr;
  RadioState* states[] = {&radio_, &cc1101_radio_, &nrf24l01_radio_};
  for (RadioState* state : states) {
    if (!state->active && state->active_client_token == 0) {
      continue;
    }
    if (selected != nullptr) {
      return false;
    }
    selected = state;
  }
  return selected != nullptr && ReadRadioStateStatus(selected, status);
}

bool TDisplayP4Device::ReadRadioStatus(
    uint32_t client_token, RadioStatus* status) {
  return ReadRadioStateStatus(FindRadioState(client_token), status);
}

bool TDisplayP4Device::ReadRadioStateStatus(
    RadioState* state, RadioStatus* status) {
  if (state == nullptr || status == nullptr || state->mutex == nullptr ||
      xSemaphoreTake(state->mutex, pdMS_TO_TICKS(20)) != pdTRUE) {
    return false;
  }
  *status = RadioStatus();
  switch (state->chip) {
    case radio::ChipType::kSx1262:
      status->hardware_ready = driver_.IsSx1262Ready();
      break;
    case radio::ChipType::kLr2021:
      status->hardware_ready = driver_.IsLr2021Ready();
      break;
    case radio::ChipType::kCc1101:
      status->hardware_ready =
          keyboard_expansion_.state.load() ==
              KeyboardExpansionState::kReady &&
          driver_.IsCc1101Ready();
      break;
    case radio::ChipType::kNrf24l01:
      status->hardware_ready =
          keyboard_expansion_.state.load() ==
              KeyboardExpansionState::kReady &&
          driver_.IsNrf24l01Ready();
      break;
    default:
      status->hardware_ready = false;
      break;
  }
  status->transmitting = state->transmitting;
  status->active_client_token = state->active_client_token;
  if (state->chip_error || (state->active && !status->hardware_ready)) {
    status->state = RadioLinkState::kChipError;
  } else if (state->active) {
    status->state = RadioLinkState::kActive;
  } else {
    status->state = RadioLinkState::kInactive;
  }
  xSemaphoreGive(state->mutex);
  return true;
}

bool TDisplayP4Device::SetImuEnabled(bool enabled) {
  const bool result = driver_.SetIcm20948Sleep(!enabled);
  imu_enabled_.store(enabled && result);
  return result;
}

bool TDisplayP4Device::ReadImuStatus(ImuStatus* status) {
  if (status == nullptr) {
    return false;
  }

  *status = ImuStatus();

  auto&icm20948 = driver_.chip().icm20948;
  if (!imu_enabled_.load() || !driver_.IsIcm20948Ready() ||
      icm20948 == nullptr) {
    return false;
  }

  cpp_bus_driver::Icm20948::SensorData data;
  if (!icm20948->ReadData(data)) {
    return false;
  }

  const auto& acceleration = data.acceleration_g;
  const float acceleration_magnitude_squared = acceleration.x * acceleration.x +
                                               acceleration.y * acceleration.y +
                                               acceleration.z * acceleration.z;
  const auto& magnetic = data.magnetic_field_ut;
  const float magnetic_magnitude_squared = magnetic.x * magnetic.x +
                                           magnetic.y * magnetic.y +
                                           magnetic.z * magnetic.z;
  if (acceleration_magnitude_squared < 0.0001F ||
      magnetic_magnitude_squared < 0.0001F || data.magnetometer_overflow) {
    return false;
  }

  const float pitch =
      std::atan2(-acceleration.x, std::sqrt(acceleration.y * acceleration.y +
                                            acceleration.z * acceleration.z)) *
      kRadiansToDegrees;
    const float roll =
      std::atan2(acceleration.y, acceleration.z) * kRadiansToDegrees;
  const float pitch_radians = pitch * kDegreesToRadians;
  const float roll_radians = roll * kDegreesToRadians;
  const float magnetic_x_horizontal = magnetic.x * std::cos(pitch_radians) +
                                      magnetic.z * std::sin(pitch_radians);
    const float magnetic_y_horizontal =
      magnetic.x * std::sin(roll_radians) * std::sin(pitch_radians) +
      magnetic.y * std::cos(roll_radians) -
      magnetic.z * std::sin(roll_radians) * std::cos(pitch_radians); float yaw =
        std::atan2(magnetic_y_horizontal, magnetic_x_horizontal) * kRadiansToDegrees;
  if (yaw < 0.0F) {
    yaw += 360.0F;
  }

    status->ready = true;
    status->pitch_deg = pitch;
    status->yaw_deg = yaw;
    status->roll_deg = roll;
    return true;
}

bool TDisplayP4Device::SetScreenBrightnessPercent(int percent) {
  if (!WaitForScreenReady()) {
    return false;
  }

  const int clamped_percent = ClampScreenBrightnessPercent(percent);
  switch (driver_.screen_type()) {
    case device::ScreenType::kHi8561:
      if (driver_.IsPt4103Ready()) {
        const cpp_bus_driver::Pwm::DutyCycle duty =
            ScreenBrightnessPercentToHi8561DutyCycle(clamped_percent);
        return driver_.chip().pt4103->SetDuty(duty);
      }
      break;
    case device::ScreenType::kRm69a10:
      if (driver_.IsRm69a10Ready()) {
        const uint8_t brightness =
            ScreenBrightnessPercentToRm69a10Value(clamped_percent);
        const bool result = driver_.chip().rm69a10->SetBrightness(brightness);
        if (result) {
          rm69a10_brightness_percent_ = clamped_percent;
        }
        return result;
      }
      break;
    default:
      break;
  }
  return false;
}

bool TDisplayP4Device::FadeScreenBrightnessPercent(
    int target_percent, uint32_t duration_ms) {
  if (!WaitForScreenReady()) {
    return false;
  }

  const int clamped_percent = ClampScreenBrightnessPercent(target_percent);
  if (duration_ms == 0) {
    return SetScreenBrightnessPercent(clamped_percent);
  }

  switch (driver_.screen_type()) {
    case device::ScreenType::kHi8561:
      if (driver_.IsPt4103Ready()) {
        const cpp_bus_driver::Pwm::DutyCycle target_duty =
            ScreenBrightnessPercentToHi8561DutyCycle(clamped_percent);
        if (driver_.chip().pt4103->FadeTo(target_duty, duration_ms,
                cpp_bus_driver::Pwm::FadeMode::kWaitForCompletion)) {
          return true;
        }
      }
      break;
    case device::ScreenType::kRm69a10:
      if (driver_.IsRm69a10Ready()) {
        const int start_percent = rm69a10_brightness_percent_;
        const int brightness_delta = std::abs(clamped_percent - start_percent);
        if (brightness_delta == 0) {
          return true;
        }
        const int duration_step_count =
            static_cast<int>(duration_ms / kScreenBrightnessFadeUpdateMs);
        const int step_count =
            std::max(1, std::min(brightness_delta, duration_step_count));
        for (int step = 1; step <= step_count; ++step) {
          const int brightness_percent = start_percent +
              (clamped_percent - start_percent) * step / step_count;
          const uint8_t brightness =
              ScreenBrightnessPercentToRm69a10Value(brightness_percent);
          if (!driver_.chip().rm69a10->SetBrightness(brightness)) {
            return false;
          }
          rm69a10_brightness_percent_ = brightness_percent;
          vTaskDelay(pdMS_TO_TICKS(
              std::max<uint32_t>(1, duration_ms / step_count)));
        }
        return true;
      }
      break;
    default:
      break;
  }
  return false;
}

bool TDisplayP4Device::EnterDeviceSleep(bool deep_sleep) {
  if (!deep_sleep && !WaitForScreenReady()) {
    return false;
  }
  if (!deep_sleep) {
    if (keyboard_expansion_.task_running.load()) {
      if (!WaitForKeyboardExpansionTask()) {
        return false;
      }
    }
    const bool keyboard_expansion_slept =
        keyboard_expansion_.state.load() != KeyboardExpansionState::kReady ||
        driver_.SetKeyboardExpansionOperatingMode(
            lilygo_device_driver::TDisplayP4Driver::
                KeyboardExpansionOperatingMode::kSleep);
    touch_gesture_wake_enabled_ =
        driver_.screen_type() == device::ScreenType::kHi8561 &&
        driver_.IsHi8561TouchReady() &&
        driver_.chip().hi8561_touch->SetGestureWakeEnabled(true);
    const bool screen_slept = driver_.SetScreenSleep(true);
    if (!screen_slept && touch_gesture_wake_enabled_) {
      driver_.chip().hi8561_touch->SetGestureWakeEnabled(false);
      touch_gesture_wake_enabled_ = false;
    }
    if (!keyboard_expansion_slept) {
      LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
          "Sleep keyboard expansion failed; continue sleeping the screen\n");
    }
    return screen_slept;
  }

  const bool
    prepared = PrepareForPowerOff();
  if (!prepared) {
    LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
        "Prepare device for power off failed\n");
    return false;
  }
  return driver_.PrepareDriversForPowerOff();
}

bool TDisplayP4Device::RestoreKeyboardExpansionOperatingState() {
  if (keyboard_expansion_.state.load() != KeyboardExpansionState::kReady) {
    return true;
  }

  bool keyboard_state_restored = SetKeyboardBacklightBrightnessPercent(
      keyboard_expansion_.backlight_brightness_percent.load());
  keyboard_state_restored &= SetKeyboardExpansionLed(
      KeyboardExpansionLed::kLed1,
      keyboard_expansion_.caps_lock_enabled.load());
  RadioState* extension_states[] = {&cc1101_radio_, &nrf24l01_radio_};
  for (RadioState* state : extension_states) {
    if (!state->active || state->mutex == nullptr ||
        xSemaphoreTake(state->mutex, pdMS_TO_TICKS(50)) != pdTRUE) {
      continue;
    }
    if (state->chip == radio::ChipType::kCc1101 &&
        driver_.IsCc1101Ready()) {
      auto* radio = driver_.chip().cc1101.get();
      bool restored = driver_.SetCc1101OperatingMode(
          lilygo_device_driver::TDisplayP4Driver::
              Cc1101OperatingMode::kStandby) &&
          radio != nullptr && InitializeCc1101ReceiveInterrupt();
      if (restored) {
        state->receive_interrupt_pending.store(
            false, std::memory_order_relaxed);
        restored = radio->StartReceive();
      }
      state->chip_error = !restored;
      state->active = restored;
      keyboard_state_restored &= restored;
    } else if (state->chip == radio::ChipType::kNrf24l01 &&
               driver_.IsNrf24l01Ready()) {
      auto* radio = driver_.chip().nrf24l01.get();
      const bool restored = driver_.SetNrf24l01OperatingMode(
          lilygo_device_driver::TDisplayP4Driver::
              Nrf24l01OperatingMode::kStandby) &&
          radio != nullptr && radio->StartReceive();
      state->chip_error = !restored;
      state->active = restored;
      keyboard_state_restored &= restored;
    }
    xSemaphoreGive(state->mutex);
  }
  return keyboard_state_restored;
}

bool TDisplayP4Device::ExitDeviceSleep(bool deep_sleep) {
  if (deep_sleep) {
    return false;
  }
  const bool result = driver_.SetScreenSleep(false);
  if (!result) {
    LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
        "Wake device from chip sleep failed\n");
    return false;
  }
  if (touch_gesture_wake_enabled_) {
    if (!driver_.chip().hi8561_touch->SetGestureWakeEnabled(false)) {
      LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
          "Disable HI8561 touch gesture wake failed\n");
    }
    touch_gesture_wake_enabled_ = false;
  }
  if (!WaitForScreenReady()) {
    return false;
  }
  if (!keyboard_expansion_.screen_lock_suspended.load() &&
      !RestoreKeyboardExpansionOperatingState()) {
    LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
        "Restore keyboard expansion state failed; "
        "continue waking the screen\n");
  }
  return true;
}

bool TDisplayP4Device::PrepareForPowerOff() {
  bool result = true;

  if (speaker_.running.load()) {
    if (speaker_.playback_kind.load() ==
        SpeakerState::PlaybackKind::kAudioFile) {
      result &= StopAudioFile();
    } else {
      speaker_.stop_requested.store(true);
      speaker_.loop_enabled.store(false);
    }
  }
  if (microphone_.running.load() ||
      microphone_.adc_to_dac_enabled.load()) {
    result &= StopMicrophone();
  }
  if (camera_preview_.task_active.load() ||
      camera_preview_.initialized.load()) {
    result &= StopCameraPreview();
  }
  if (radio_.active || radio_.transmitting || cc1101_radio_.active ||
      cc1101_radio_.transmitting || nrf24l01_radio_.active ||
      nrf24l01_radio_.transmitting) {
    result &= DeactivateRadio();
  }
  result &= SetNfcPollingEnabled(false);
  result &= SetGpsEnabled(false);
  result &= SetImuEnabled(false);
  result &= SetEthernetEnabled(false);
  result &= SetWifiEnabled(false);
  result &= StopUsbStorage();
  result &= DisableKeyboardExpansion();
  result &= WaitForPowerOffTasks();
  return result;
}

bool TDisplayP4Device::WaitForPowerOffTasks() {
  for (int elapsed_ms = 0; elapsed_ms < kPowerOffTaskTimeoutMs;
      elapsed_ms += kPowerOffTaskPollMs) {
    const bool tasks_running = speaker_.running.load() ||
                               haptic_.running.load() ||
                               microphone_.running.load() ||
                               camera_preview_.task_active.load() ||
                               ethernet_.init_task_running.load() ||
                               keyboard_expansion_.task_running.load() ||
                               nfc_.task_active.load() ||
                               wifi_.init_task_running.load() ||
                               wifi_.scan_task_running.load() ||
                               wifi_.connect_task_running.load();
    if (!tasks_running) {
      return true;
    }
    vTaskDelay(pdMS_TO_TICKS(kPowerOffTaskPollMs));
  }
  return false;
}

bool TDisplayP4Device::WaitForScreenReady() {
  for (int elapsed_ms = 0; elapsed_ms < kScreenReadyTimeoutMs;
      elapsed_ms += kScreenReadyPollMs) {
    if (driver_.IsScreenReady()) {
      return true;
    }
    vTaskDelay(pdMS_TO_TICKS(kScreenReadyPollMs));
  }
  return driver_.IsScreenReady();
}

bool TDisplayP4Device::WaitForTouchReady() {
  for (int elapsed_ms = 0; elapsed_ms < kScreenReadyTimeoutMs;
      elapsed_ms += kScreenReadyPollMs) {
    if (driver_.IsTouchReady()) {
      return true;
    }
    vTaskDelay(pdMS_TO_TICKS(kScreenReadyPollMs));
  }
  return driver_.IsTouchReady();
}

}  // namespace lilygo_box::hal
