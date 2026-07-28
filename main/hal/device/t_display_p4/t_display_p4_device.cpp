/*
 * @Description: T-Display-P4 设备初始化与硬件 Provider 适配实现
 * @Author: LILYGO_L
 * @Date: 2026-05-10 13:27:05
 * @LastEditTime: 2026-07-24 14:54:36
 * @License: GPL 3.0
 */
#include "hal/device/t_display_p4/t_display_p4_device.h"

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
#include <memory>
#include <new>
#include <string>

#include "app/storage/display_storage.h"
#include "audio/new_notification_010_c2_b16_s44100.h"
#include "base/logger.h"
#include "esp_err.h"
#include "esp_eth.h"
#include "esp_eth_mac.h"
#include "esp_eth_phy_802_3.h"
#include "esp_event.h"
#include "esp_heap_caps.h"
#include "esp_hosted.h"
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
#include "linux/videodev2.h"

namespace lilygo_box::hal {
namespace device = lilygo_device_driver::t_display_p4::device;
namespace gpio = lilygo_device_driver::t_display_p4::gpio;
namespace {

constexpr int kScreenBrightnessMinPercent = 0;
constexpr int kScreenBrightnessMaxPercent = 100;
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
constexpr uint32_t kCameraOutputClearFrameCount = 3;
constexpr uint32_t kCameraWarmupFrameCount = 5;
// Radio 发送硬件超时的最小值和额外保护时间。
constexpr uint32_t kMinimumRadioTransmitTimeoutMs = 1000;
constexpr uint32_t kRadioTransmitTimeoutMarginMs = 500;
constexpr uint32_t kRadioTransmitWatchdogGraceMs = 1000;
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

// 当前接收 SNTP 同步回调的设备实例
std::atomic<TDisplayP4Device*> g_wifi_time_sync_owner{nullptr};

void SetEdgeTouchPoint(TouchPoint* point) {
  point->id = 0;
  point->x = -1;
  point->y = -1;
  point->pressure = 0;
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

uint8_t ScreenBrightnessPercentToRm69a10Value(int clamped_percent) {
  return static_cast<uint8_t>(
      clamped_percent * kRm69a10BrightnessMax / kScreenBrightnessMaxPercent);
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
  if (target == nullptr || source.frequency_hz < 150000000 ||
      source.frequency_hz > 960000000 || source.spreading_factor < 5 ||
      source.spreading_factor > 12 || source.coding_rate_denominator < 5 ||
      source.coding_rate_denominator > 8 || source.preamble_length == 0 ||
      source.output_power_dbm < -9 || source.output_power_dbm > 22 ||
      !SelectLoraBandwidth(source.bandwidth_hz, &target->bandwidth)) {
    return false;
  }
  target->frequency_hz = source.frequency_hz;
  target->spreading_factor =
      static_cast<sx126x_lora_sf_t>(source.spreading_factor);
  target->coding_rate =
      static_cast<sx126x_lora_cr_t>(source.coding_rate_denominator - 4);
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
      config.spreading_factor < 5 || config.spreading_factor > 12 ||
      config.coding_rate_denominator < 5 ||
      config.coding_rate_denominator > 8 || config.preamble_length == 0) {
    return false;
  }
  sx126x_lora_bw_t bandwidth;
  if (!SelectLoraBandwidth(config.bandwidth_hz, &bandwidth)) {
    return false;
  }
  const sx126x_mod_params_lora_t modulation_params = {
      .sf = static_cast<sx126x_lora_sf_t>(config.spreading_factor),
      .bw = bandwidth,
      .cr = static_cast<sx126x_lora_cr_t>(config.coding_rate_denominator - 4),
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

}  // namespace

TDisplayP4Device::TDisplayP4Device()
    : driver_(lilygo_device_driver::TDisplayP4Driver::GetInstance()),
      tool_(std::make_unique<cpp_bus_driver::Tool>()),
      usb_storage_manager_([this]() {
        driver_.SetUsbHostPowerEnabled(false);
      }) {
  wifi_.scan_results_mutex = xSemaphoreCreateMutex();
  radio_.mutex = xSemaphoreCreateMutex();
}

bool TDisplayP4Device::InitDevice() {
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
  return true;
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
  g_wifi_time_sync_owner.compare_exchange_strong(owner, nullptr);
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
  if (usb_storage_manager_.Start()) {
    return true;
  }
  driver_.SetUsbHostPowerEnabled(false);
  return false;
}

bool TDisplayP4Device::StopUsbStorage() {
  const bool stopped = usb_storage_manager_.Stop();
  const bool powered_off = driver_.SetUsbHostPowerEnabled(false);
  return stopped && powered_off;
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

  if (!driver_.IsTouchReady()) {
    return false;
  }

  switch (driver_.screen_type()) {
    case device::ScreenType::kHi8561: {
      cpp_bus_driver::Hi8561Touch::TouchPoint touch_point;
      const bool result =
          driver_.chip().hi8561_touch->GetSingleTouchPoint(touch_point);
      if (!result || touch_point.info.empty()) {
        if (driver_.chip().hi8561_touch->GetEdgeTouch()) {
          point->id = 0;
          point->x = -1;
          point->y = -1;
          point->pressure = 0;
          point->edge_touch_flag = true;
          return true;
        }
        return false;
      }
      point->id = 1;
      point->x = touch_point.info[0].x;
      point->y = touch_point.info[0].y;
      point->pressure = touch_point.info[0].pressure_value;
      point->edge_touch_flag = touch_point.edge_touch_flag;
      return true;
    }
    case device::ScreenType::kRm69a10: {
      cpp_bus_driver::Gt9895::TouchPoint touch_point;
      const bool result =
          driver_.chip().gt9895->GetSingleTouchPoint(touch_point);
      if (!result || touch_point.info.empty()) {
        return false;
      }
      point->id = touch_point.info[0].finger_id;
      point->x = touch_point.info[0].x;
      point->y = touch_point.info[0].y;
      point->pressure = touch_point.info[0].pressure_value;
      point->edge_touch_flag = touch_point.edge_touch_flag;
      return true;
    }
    default:
      break;
  }
  return false;
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

  switch (driver_.screen_type()) {
    case device::ScreenType::kHi8561: {
      cpp_bus_driver::Hi8561Touch::TouchPoint touch_point;
      const bool result =
          driver_.chip().hi8561_touch->GetMultipleTouchPoint(touch_point);
      if (!result || touch_point.info.empty()) {
        return false;
      }

      const size_t count = std::min(max_points, touch_point.info.size());
      for (size_t i = 0; i < count; ++i) {
        if (touch_point.info[i].x == UINT16_MAX &&
            touch_point.info[i].y == UINT16_MAX) {
          continue;
        }
        points[*point_count].id = static_cast<uint8_t>(i + 1);
        points[*point_count].x = touch_point.info[i].x;
        points[*point_count].y = touch_point.info[i].y;
        points[*point_count].pressure = touch_point.info[i].pressure_value;
        points[*point_count].edge_touch_flag = touch_point.edge_touch_flag;
        ++(*point_count);
      }
      if (*point_count == 0 && touch_point.edge_touch_flag) {
        SetEdgeTouchPoint(&points[0]);
        *point_count = 1;
      }
      return *point_count > 0;
    }
    case device::ScreenType::kRm69a10: {
      cpp_bus_driver::Gt9895::TouchPoint touch_point;
      const bool result =
          driver_.chip().gt9895->GetMultipleTouchPoint(touch_point);
      if (!result || touch_point.info.empty()) {
        return false;
      }

      const size_t count = std::min(max_points, touch_point.info.size());
      for (size_t i = 0; i < count; ++i) {
        if (touch_point.info[i].x == UINT16_MAX &&
            touch_point.info[i].y == UINT16_MAX) {
          continue;
        }
        if (touch_point.edge_touch_flag && touch_point.info[i].finger_id == 0) {
          continue;
        }
        points[*point_count].id = touch_point.info[i].finger_id;
        points[*point_count].x = touch_point.info[i].x;
        points[*point_count].y = touch_point.info[i].y;
        points[*point_count].pressure = touch_point.info[i].pressure_value;
        points[*point_count].edge_touch_flag = touch_point.edge_touch_flag;
        ++(*point_count);
      }
      if (*point_count == 0 && touch_point.edge_touch_flag) {
        SetEdgeTouchPoint(&points[0]);
        *point_count = 1;
      }
      return *point_count > 0;
    }
    default:
      break;
  }
  return false;
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
  bool expected = false;
  if (!speaker_.running.compare_exchange_strong(expected, true)) {
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
    return false;
  }

  return true;
}

bool TDisplayP4Device::StartSpeakerToneLoop() {
  if (speaker_.running.load()) {
    return speaker_.playback_kind.load() ==
           SpeakerState::PlaybackKind::kToneLoop;
  }
  speaker_.loop_enabled.store(true);
  speaker_.stop_requested.store(false);

  bool expected = false;
  if (!speaker_.running.compare_exchange_strong(expected, true)) {
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
    return false;
  }

  return true;
}

bool TDisplayP4Device::StopSpeakerToneLoop() {
  if (speaker_.playback_kind.load() !=
      SpeakerState::PlaybackKind::kToneLoop) {
    return false;
  }
  speaker_.stop_requested.store(true);
  speaker_.loop_enabled.store(false);
  return true;
}

bool TDisplayP4Device::SetSpeakerVolumePercent(int percent) {
  if (!driver_.IsEs8311Ready() &&
      (!driver_.InitEs8311() || !driver_.ConfigEs8311())) {
    LogMessage(
        LogLevel::kWarning, __FILE__, __LINE__, "Es8311 init failed\n");
    return false;
  }

  const uint8_t volume = PercentToUint8Value(percent, kAudioVolumeMax);
  const bool result = driver_.chip().es8311->SetDacVolume(volume);
  UpdateAudioCodecPowerState();
  return result;
}

bool TDisplayP4Device::ReadSpeakerToneStatus(SpeakerStatus* status) {
  if (status == nullptr) {
    return false;
  }

  const SpeakerState::PlaybackKind playback_kind =
      speaker_.playback_kind.load();
  status->running = speaker_.running.load() &&
                    playback_kind != SpeakerState::PlaybackKind::kAudioFile;
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

  bool expected = false;
  if (!speaker_.running.compare_exchange_strong(expected, true)) {
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
  speaker_.paused.store(true);
  speaker_.file_state.store(AudioFilePlaybackState::kPaused);
  return true;
}

bool TDisplayP4Device::ResumeAudioFile() {
  if (!speaker_.running.load() ||
      speaker_.playback_kind.load() !=
          SpeakerState::PlaybackKind::kAudioFile ||
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
  speaker_.paused.store(false);
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
    UpdateAudioCodecPowerState();
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
  speaker_.running.store(false);
  UpdateAudioCodecPowerState();
}

bool TDisplayP4Device::UpdateAudioCodecPowerState() {
  using AudioPowerState =
      lilygo_device_driver::TDisplayP4Driver::Es8311PowerState;
  const bool playback_active = speaker_.running.load();
  const bool capture_active = microphone_.running.load();
  AudioPowerState state = AudioPowerState::kSleep;
  if (playback_active && capture_active) {
    state = AudioPowerState::kDuplex;
  } else if (playback_active) {
    state = AudioPowerState::kPlayback;
  } else if (capture_active) {
    state = microphone_.adc_to_dac_enabled.load()
                ? AudioPowerState::kDuplex
                : AudioPowerState::kCapture;
  }
  return driver_.SetEs8311PowerState(state);
}

bool TDisplayP4Device::Configure(uint32_t sample_rate_hz,
    uint8_t channel_count, uint8_t bits_per_sample) {
  if ((channel_count != 1 && channel_count != 2) ||
      bits_per_sample != kSpeakerPlaybackBitsPerSample) {
    return false;
  }
  const bool codec_was_ready = driver_.IsEs8311Ready();
  if (!UpdateAudioCodecPowerState()) {
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
  while (speaker_.paused.load() && !speaker_.stop_requested.load()) {
    vTaskDelay(pdMS_TO_TICKS(20));
  }
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
      driver_.chip().aw86224->StopRamPlaybackWaveform();
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
    driver_.chip().aw86224->StopRamPlaybackWaveform();
    haptic_.running.store(false);
    return;
  }

  vTaskDelay(pdMS_TO_TICKS(kVibrationPreviewPlayMs));

  if (!driver_.chip().aw86224->StopRamPlaybackWaveform()) {
    LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
        "Aw86224 StopRamPlaybackWaveform failed, sequence=%u\n",
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
        "Failed to power the ES8311 microphone capture path\n");
    microphone_.running.store(false);
    UpdateAudioCodecPowerState();
    return false;
  }

  const BaseType_t result = xTaskCreate(MicrophoneCaptureTaskEntry,
      "mic_capture", kMicrophoneCaptureTaskStackBytes, this,
      kMicrophoneCaptureTaskPriority, nullptr);
  if (result != pdPASS) {
    microphone_.running.store(false);
    microphone_.stop_requested.store(true);
    UpdateAudioCodecPowerState();
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
    return true;
  }
  return SetAudioAdcToDac(false);
}

bool TDisplayP4Device::SetAudioAdcToDac(bool enable) {
  if (enable &&
      (!microphone_.running.load() || speaker_.running.load())) {
    return false;
  }

  const bool previous_enabled = microphone_.adc_to_dac_enabled.exchange(enable);
  if (!UpdateAudioCodecPowerState() || !driver_.IsEs8311Ready()) {
    microphone_.adc_to_dac_enabled.store(previous_enabled);
    UpdateAudioCodecPowerState();
    return false;
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

  camera_preview_.stop_requested.store(false);
  if (!InitializeCameraPreview()) {
    DeinitializeCameraPreview();
    camera_preview_.stop_requested.store(true);
    return false;
  }

  camera_preview_.task_active.store(true);
  BaseType_t result = xTaskCreate(CameraPreviewTaskEntry,
      "camera_preview", kCameraPreviewTaskStackBytes, this,
      kCameraPreviewTaskPriority, nullptr);
  if (result != pdPASS) {
    camera_preview_.task_active.store(false);
    camera_preview_.stop_requested.store(true);
    DeinitializeCameraPreview();
    return false;
  }
  return true;
}

bool TDisplayP4Device::StopCameraPreview() {
  camera_preview_.stop_requested.store(true);
  // 不在这里发 VIDIOC_STREAMOFF — 让 RunCameraPreviewTask 退出时由
  // DeinitializeCameraPreview 统一处理，避免与正在运行的 DQBUF/PPA 产生 I2C 竞态
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

bool TDisplayP4Device::InitializeCameraPreview() {
  if (!driver_.IsScreenReady()) {
    LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
        "Camera preview start failed: screen is not ready\n");
    return false;
  }
  if (!driver_.SetCameraPowerEnabled(true)) {
    LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
        "Camera preview start failed: power enable failed\n");
    return false;
  }

  const bool restore_sensor =
      camera_preview_.video_system_initialized.load();
  if (!camera_preview_.video_system_initialized.load()) {
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
    const uint32_t init_flags =
        ESP_VIDEO_INIT_FLAGS_MIPI_CSI | ESP_VIDEO_INIT_FLAGS_ISP;
    esp_err_t result =
        esp_video_init_with_flags(&camera_config, init_flags);
    if (result != ESP_OK) {
      // 组件初始化失败时会清理当前已经创建的全部视频设备。
      camera_preview_.video_system_initialized.store(false);
      LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
          "esp_video_init_with_flags failed: %s (%#X)\n",
          esp_err_to_name(result), static_cast<unsigned>(result));
      driver_.SetCameraPowerEnabled(false);
      return false;
    }
    camera_preview_.video_system_initialized.store(true);
  }

  camera_preview_.video_fd = open(kCameraDeviceName, O_RDONLY | O_NONBLOCK);
  if (camera_preview_.video_fd < 0) {
    LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
        "Open camera video device failed\n");
    return false;
  }

  if (restore_sensor) {
    // 摄像头重新上电后，通过组件公开接口重写传感器的完整寄存器配置。
    auto& sensor_format = camera_preview_.sensor_format;
    if (ioctl(camera_preview_.video_fd, VIDIOC_G_SENSOR_FMT,
            &sensor_format) != 0 ||
        ioctl(camera_preview_.video_fd, VIDIOC_S_SENSOR_FMT,
            &sensor_format) != 0) {
      LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
          "Restore camera sensor format failed\n");
      return false;
    }
  }

  v4l2_format format = {};
  format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  if (ioctl(camera_preview_.video_fd, VIDIOC_G_FMT, &format) != 0) {
    LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
        "VIDIOC_G_FMT failed\n");
    return false;
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
    LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
        "VIDIOC_S_FMT failed\n");
    return false;
  }
  camera_preview_.frame_width = format.fmt.pix.width;
  camera_preview_.frame_height = format.fmt.pix.height;

  v4l2_requestbuffers request = {};
  request.count = kCameraBufferCount;
  request.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  request.memory = V4L2_MEMORY_MMAP;
  if (ioctl(camera_preview_.video_fd, VIDIOC_REQBUFS, &request) != 0 ||
      request.count < kCameraBufferCount) {
    LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
        "VIDIOC_REQBUFS failed or returned too few buffers\n");
    return false;
  }

  for (uint32_t index = 0; index < kCameraBufferCount; ++index) {
    v4l2_buffer buffer = {};
    buffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buffer.memory = V4L2_MEMORY_MMAP;
    buffer.index = index;
    if (ioctl(camera_preview_.video_fd, VIDIOC_QUERYBUF, &buffer) != 0) {
      LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
          "VIDIOC_QUERYBUF failed\n");
      return false;
    }
    camera_preview_.frame_buffer_sizes[index] = buffer.length;
    camera_preview_.frame_buffers[index] = mmap(nullptr, buffer.length,
        PROT_READ | PROT_WRITE, MAP_SHARED, camera_preview_.video_fd,
        buffer.m.offset);
    if (camera_preview_.frame_buffers[index] == MAP_FAILED) {
      camera_preview_.frame_buffers[index] = nullptr;
      LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
          "Camera buffer mmap failed\n");
      return false;
    }
    if (ioctl(camera_preview_.video_fd, VIDIOC_QBUF, &buffer) != 0) {
      LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
          "VIDIOC_QBUF failed\n");
      return false;
    }
  }

  if (camera_preview_.output_mutex == nullptr) {
    camera_preview_.output_mutex = xSemaphoreCreateMutex();
    if (camera_preview_.output_mutex == nullptr) {
      LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
          "Camera output mutex allocation failed\n");
      return false;
    }
  }

  if (!camera_preview_.ppa.Init()) {
    LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
        "PPA SRM init failed\n");
    return false;
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
    LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
        "Camera output buffer allocation failed\n");
    return false;
  }
  camera_preview_.output_buffer.reset(static_cast<uint8_t*>(output_buffer));
  camera_preview_.clear_output_frames_remaining = kCameraOutputClearFrameCount;
  camera_preview_.warmup_frames_remaining = kCameraWarmupFrameCount;

  int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  if (ioctl(camera_preview_.video_fd, VIDIOC_STREAMON, &type) != 0) {
    LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
        "VIDIOC_STREAMON failed\n");
    return false;
  }

  camera_preview_.initialized.store(true);
  LogMessage(LogLevel::kInfo, __FILE__, __LINE__,
      "Camera preview started (%lux%lu)\n", camera_preview_.frame_width,
      camera_preview_.frame_height);
  return true;
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
            UpdateAudioCodecPowerState();
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
  microphone_.level_percent.store(0);
  microphone_.peak_sample.store(0);
  microphone_.running.store(false);
  UpdateAudioCodecPowerState();
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
  g_wifi_time_sync_owner.store(this);
  esp_sntp_set_time_sync_notification_cb([](struct timeval* time_value) {
    auto* owner = g_wifi_time_sync_owner.load();
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
      status->gauge_temperature_c =
          driver_.chip().bq27220->GetChipTemperatureCelsius();
      status->pack_present =
          battery_management_status_ok && battery_management_status_flags.flag.battery_present;
      status->charging = current_ma > 0 ||
                         (battery_management_status_ok && !battery_management_status_flags.flag.discharging);
      status->full_charged =
          battery_management_status_ok && battery_management_status_flags.flag.full_charged;
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
  capabilities->entries[0] = {
      .chip = radio::ChipType::kSx1262,
      .protocol = radio::ProtocolType::kLora,
      .maximum_payload_size = kRadioPayloadCapacity,
  };
  capabilities->count = 1;
  return true;
}

bool TDisplayP4Device::ActivateRadio(const RadioConfig& config) {
  if (radio_.mutex == nullptr ||
      xSemaphoreTake(radio_.mutex, pdMS_TO_TICKS(50)) != pdTRUE) {
    LogMessage(LogLevel::kError, __FILE__, __LINE__,
        "Radio activate failed: mutex unavailable, profile=%lu\n",
        static_cast<unsigned long>(config.client_token));
    return false;
  }
  usp_cpp_bus_driver::Sx126x::LoraConfig driver_config;
  const bool antenna_supported =
      config.antenna == radio::AntennaType::kInternal ||
      config.antenna == radio::AntennaType::kExternal;
  bool result = config.chip == radio::ChipType::kSx1262 &&
                config.protocol == radio::ProtocolType::kLora &&
                antenna_supported &&
                BuildSx1262Config(config.lora, &driver_config);
  if (result) {
    auto* antenna_switch = driver_.chip().xl9535.get();
    const uint8_t antenna_level =
        config.antenna == radio::AntennaType::kExternal ? 0 : 1;
    result = driver_.IsXl9535Ready() && antenna_switch != nullptr &&
             antenna_switch->GpioWrite(
                 gpio::xl9535::kSky13453Vctl, antenna_level);
  }
  if (result && driver_.SetSx1262PowerState(
                    lilygo_device_driver::TDisplayP4Driver::
                        Sx1262PowerState::kStandby)) {
    auto* radio = driver_.chip().sx1262.get();
    result = radio != nullptr && radio->Configure(driver_config) &&
             radio->StartReceive();
  } else {
    result = false;
  }
  if (!result) {
    driver_.SetSx1262PowerState(
        lilygo_device_driver::TDisplayP4Driver::Sx1262PowerState::kSleep);
  }
  radio_.active = result;
  radio_.transmitting = false;
  radio_.chip_error = !result;
  radio_.active_client_token = config.client_token;
  radio_.transmit_request_token = 0;
  radio_.transmit_deadline_us = 0;
  radio_.lora_config = config.lora;
  xSemaphoreGive(radio_.mutex);
  LogMessage(result ? LogLevel::kDebug : LogLevel::kError, __FILE__, __LINE__,
      "Radio activate %s: profile=%lu, frequency=%lu Hz, SF=%u, "
      "bandwidth=%lu Hz, antenna=%s\n",
      result ? "succeeded" : "failed",
      static_cast<unsigned long>(config.client_token),
      static_cast<unsigned long>(config.lora.frequency_hz),
      static_cast<unsigned>(config.lora.spreading_factor),
      static_cast<unsigned long>(config.lora.bandwidth_hz),
      config.antenna == radio::AntennaType::kExternal ? "RF2" : "RF1");
  return result;
}

bool TDisplayP4Device::DeactivateRadio() {
  if (radio_.mutex == nullptr ||
      xSemaphoreTake(radio_.mutex, pdMS_TO_TICKS(50)) != pdTRUE) {
    LogMessage(LogLevel::kError, __FILE__, __LINE__,
        "Radio deactivate failed: mutex unavailable\n");
    return false;
  }
  bool result = true;
  if (driver_.IsSx1262Ready()) {
    auto* radio = driver_.chip().sx1262.get();
    if (radio_.active) {
      result = radio != nullptr &&
               radio->Invoke(sx126x_set_standby, SX126X_STANDBY_CFG_RC) ==
                   SX126X_STATUS_OK &&
               radio->ClearIrqStatus(SX126X_IRQ_ALL);
    }
    result &= driver_.SetSx1262PowerState(
        lilygo_device_driver::TDisplayP4Driver::
            Sx1262PowerState::kSleep);
  }
  radio_.active = false;
  radio_.transmitting = false;
  radio_.chip_error = !result;
  radio_.active_client_token = 0;
  radio_.transmit_request_token = 0;
  radio_.transmit_deadline_us = 0;
  xSemaphoreGive(radio_.mutex);
  LogMessage(result ? LogLevel::kInfo : LogLevel::kError, __FILE__, __LINE__,
      "Radio deactivate %s\n", result ? "succeeded" : "failed");
  return result;
}

bool TDisplayP4Device::SendRadio(
    const uint8_t* data, size_t size, uint64_t request_token) {
  if (data == nullptr || size == 0 || size > kRadioPayloadCapacity ||
      request_token == 0) {
    LogMessage(LogLevel::kError, __FILE__, __LINE__,
        "Radio send rejected: invalid request, message=%lu, size=%u bytes\n",
        static_cast<unsigned long>(static_cast<uint32_t>(request_token)),
        static_cast<unsigned>(size));
    return false;
  }
  if (radio_.mutex == nullptr ||
      xSemaphoreTake(radio_.mutex, pdMS_TO_TICKS(50)) != pdTRUE) {
    LogMessage(LogLevel::kError, __FILE__, __LINE__,
        "Radio send rejected: radio is busy, message=%lu\n",
        static_cast<unsigned long>(static_cast<uint32_t>(request_token)));
    return false;
  }
  if (!radio_.active) {
    LogMessage(LogLevel::kError, __FILE__, __LINE__,
        "Radio send rejected: profile %lu is inactive, message=%lu\n",
        static_cast<unsigned long>(radio_.active_client_token),
        static_cast<unsigned long>(static_cast<uint32_t>(request_token)));
    xSemaphoreGive(radio_.mutex);
    return false;
  }
  if (radio_.transmitting) {
    LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
        "Radio send rejected: message %lu is still transmitting, "
        "new message=%lu\n",
        static_cast<unsigned long>(
            static_cast<uint32_t>(radio_.transmit_request_token)),
        static_cast<unsigned long>(static_cast<uint32_t>(request_token)));
    xSemaphoreGive(radio_.mutex);
    return false;
  }
  if (!driver_.IsSx1262Ready()) {
    radio_.chip_error = true;
    LogMessage(LogLevel::kError, __FILE__, __LINE__,
        "Radio send rejected: SX1262 is unavailable, message=%lu\n",
        static_cast<unsigned long>(static_cast<uint32_t>(request_token)));
    xSemaphoreGive(radio_.mutex);
    return false;
  }
  LoraTransmitTiming timing;
  if (!CalculateLoraTransmitTiming(radio_.lora_config, size, &timing)) {
    LogMessage(LogLevel::kError, __FILE__, __LINE__,
        "Radio send rejected: invalid LoRa timing, message=%lu, "
        "size=%u bytes\n",
        static_cast<unsigned long>(static_cast<uint32_t>(request_token)),
        static_cast<unsigned>(size));
    xSemaphoreGive(radio_.mutex);
    return false;
  }
  auto* radio = driver_.chip().sx1262.get();
  const bool result = radio != nullptr && radio->StartTransmit(data, size,
                                              timing.hardware_timeout_ms);
  radio_.transmitting = result;
  radio_.chip_error = !result;
  radio_.transmit_request_token = result ? request_token : 0;
  radio_.transmit_deadline_us =
      result ? esp_timer_get_time() +
                   static_cast<int64_t>(timing.watchdog_timeout_ms) * 1000
             : 0;
  const uint32_t profile_id = radio_.active_client_token;
  xSemaphoreGive(radio_.mutex);
  if (result) {
    LogMessage(LogLevel::kDebug, __FILE__, __LINE__,
        "Radio send started: profile %lu, %u bytes, estimated %lu ms\n",
        static_cast<unsigned long>(profile_id), static_cast<unsigned>(size),
        static_cast<unsigned long>(timing.time_on_air_ms));
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
  if (radio_.mutex == nullptr ||
      xSemaphoreTake(radio_.mutex, pdMS_TO_TICKS(20)) != pdTRUE) {
    LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
        "Radio event poll failed: mutex unavailable\n");
    return false;
  }
  event->client_token = radio_.active_client_token;
  event->request_token = radio_.transmit_request_token;
  if (!radio_.active) {
    xSemaphoreGive(radio_.mutex);
    return true;
  }
  if (!driver_.IsSx1262Ready()) {
    radio_.active = false;
    radio_.transmitting = false;
    radio_.chip_error = true;
    event->type = RadioEventType::kChipError;
    event->failure_reason = RadioFailureReason::kHardwareUnavailable;
    radio_.transmit_request_token = 0;
    radio_.transmit_deadline_us = 0;
    xSemaphoreGive(radio_.mutex);
    LogMessage(LogLevel::kError, __FILE__, __LINE__,
        "Radio event failed: SX1262 is unavailable, profile=%lu, message=%lu\n",
        static_cast<unsigned long>(event->client_token),
        static_cast<unsigned long>(
            static_cast<uint32_t>(event->request_token)));
    return false;
  }

  auto* radio = driver_.chip().sx1262.get();
  sx126x_irq_mask_t irq_mask = 0;
  if (radio == nullptr || !radio->GetIrqStatus(irq_mask)) {
    radio_.active = false;
    radio_.transmitting = false;
    radio_.chip_error = true;
    event->type = RadioEventType::kChipError;
    event->failure_reason = RadioFailureReason::kIrqReadFailed;
    radio_.transmit_request_token = 0;
    radio_.transmit_deadline_us = 0;
    xSemaphoreGive(radio_.mutex);
    LogMessage(LogLevel::kError, __FILE__, __LINE__,
        "Radio event failed: cannot read IRQ, profile=%lu, message=%lu\n",
        static_cast<unsigned long>(event->client_token),
        static_cast<unsigned long>(
            static_cast<uint32_t>(event->request_token)));
    return false;
  }
  if (irq_mask == 0) {
    if (radio_.transmitting && radio_.transmit_deadline_us > 0 &&
        esp_timer_get_time() >= radio_.transmit_deadline_us) {
      const bool recovered = radio->StartReceive();
      radio_.transmitting = false;
      radio_.active = recovered;
      radio_.chip_error = !recovered;
      radio_.transmit_request_token = 0;
      radio_.transmit_deadline_us = 0;
      event->type = RadioEventType::kTransmitFailed;
      event->failure_reason = RadioFailureReason::kSoftwareTimeout;
      xSemaphoreGive(radio_.mutex);
      LogMessage(LogLevel::kError, __FILE__, __LINE__,
          "Radio send failed: software timeout, profile=%lu, message=%lu, "
          "receive recovery=%s\n",
          static_cast<unsigned long>(event->client_token),
          static_cast<unsigned long>(
              static_cast<uint32_t>(event->request_token)),
          recovered ? "succeeded" : "failed");
      return recovered;
    }
    xSemaphoreGive(radio_.mutex);
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
    radio_.active = false;
    radio_.transmitting = false;
    radio_.chip_error = true;
    radio_.transmit_request_token = 0;
    radio_.transmit_deadline_us = 0;
    event->type = RadioEventType::kChipError;
    event->failure_reason = RadioFailureReason::kIrqClearFailed;
    xSemaphoreGive(radio_.mutex);
    LogMessage(LogLevel::kError, __FILE__, __LINE__,
        "Radio event failed: cannot clear IRQ %s, message=%lu\n",
        irq_text_for_log(),
        static_cast<unsigned long>(
            static_cast<uint32_t>(event->request_token)));
    return false;
  }
  if (radio_.transmitting && (tx_done || timed_out)) {
    radio_.transmitting = false;
    radio_.transmit_request_token = 0;
    radio_.transmit_deadline_us = 0;
    const bool receive_restarted = radio->StartReceive();
    radio_.active = receive_restarted;
    radio_.chip_error = !receive_restarted;
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
  } else if (radio_.transmitting) {
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
    radio_.active = false;
    radio_.transmitting = false;
    radio_.chip_error = true;
    if (event->type != RadioEventType::kTransmitComplete) {
      event->type = RadioEventType::kChipError;
    }
    if (event->failure_reason == RadioFailureReason::kNone) {
      event->failure_reason = RadioFailureReason::kReceiveRestartFailed;
    }
    radio_.transmit_request_token = 0;
    radio_.transmit_deadline_us = 0;
    LogMessage(LogLevel::kError, __FILE__, __LINE__,
        "Radio event processing failed: profile=%lu, message=%lu, IRQ=%s\n",
        static_cast<unsigned long>(event->client_token),
        static_cast<unsigned long>(
            static_cast<uint32_t>(event->request_token)),
        irq_text_for_log());
  }
  xSemaphoreGive(radio_.mutex);
  return result;
}

bool TDisplayP4Device::ReadRadioStatus(RadioStatus* status) {
  if (status == nullptr || radio_.mutex == nullptr ||
      xSemaphoreTake(radio_.mutex, pdMS_TO_TICKS(20)) != pdTRUE) {
    return false;
  }
  status->hardware_ready = driver_.IsSx1262Ready();
  status->transmitting = radio_.transmitting;
  status->active_client_token = radio_.active_client_token;
  if (radio_.chip_error || (radio_.active && !status->hardware_ready)) {
    status->state = RadioLinkState::kChipError;
  } else if (radio_.active) {
    status->state = RadioLinkState::kActive;
  } else {
    status->state = RadioLinkState::kInactive;
  }
  xSemaphoreGive(radio_.mutex);
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

  if (imu_enabled_.load() && driver_.IsIcm20948Ready()) {
    xyzFloat acceleration;
    xyzFloat angle;
    xyzFloat magnetic;
    driver_.chip().icm20948->readSensor();
    driver_.chip().icm20948->getGValues(&acceleration);
    driver_.chip().icm20948->getAngles(&angle);
    const float pitch = driver_.chip().icm20948->getPitch();
    const float roll = driver_.chip().icm20948->getRoll();
    driver_.chip().icm20948->getMagValues(&magnetic);
    const float yaw =
        std::atan2(magnetic.y, magnetic.x) * kRadiansToDegrees;

    status->ready = true;
    status->pitch_deg = pitch;
    status->yaw_deg = yaw;
    status->roll_deg = roll;
    return true;
  }

  return false;
}

void TDisplayP4Device::StartScreenBacklight(int initial_percent) {
  if (!WaitForScreenReady()) {
    return;
  }

  const int clamped_percent = ClampScreenBrightnessPercent(initial_percent);
  switch (driver_.screen_type()) {
    case device::ScreenType::kHi8561:
      if (driver_.IsHi8561BacklightReady()) {
        driver_.chip().hi8561_backlight->StartGradientTime(
            static_cast<uint8_t>(clamped_percent), 500);
      }
      break;
    case device::ScreenType::kRm69a10:
      if (driver_.IsRm69a10Ready()) {
        const uint8_t target_brightness =
            ScreenBrightnessPercentToRm69a10Value(clamped_percent);
        for (uint16_t brightness = 0; brightness < target_brightness;
             brightness += 5) {
          driver_.chip().rm69a10->SetBrightness(brightness);
          vTaskDelay(pdMS_TO_TICKS(10));
        }
        driver_.chip().rm69a10->SetBrightness(target_brightness);
      }
      break;
    default:
      break;
  }
}

bool TDisplayP4Device::SetScreenBrightnessPercent(int percent) {
  if (!WaitForScreenReady()) {
    return false;
  }

  const int clamped_percent = ClampScreenBrightnessPercent(percent);
  switch (driver_.screen_type()) {
    case device::ScreenType::kHi8561:
      if (driver_.IsHi8561BacklightReady()) {
        return driver_.chip().hi8561_backlight->SetDuty(
            static_cast<uint8_t>(clamped_percent));
      }
      break;
    case device::ScreenType::kRm69a10:
      if (driver_.IsRm69a10Ready()) {
        const uint8_t brightness =
            ScreenBrightnessPercentToRm69a10Value(clamped_percent);
        return driver_.chip().rm69a10->SetBrightness(brightness);
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
  bool prepared = true;
  if (deep_sleep) {
    prepared = PrepareForPowerOff();
  }
  if (!prepared) {
    LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
        "Prepare device for power off failed\n");
  }
  const auto state = deep_sleep
                         ? lilygo_device_driver::TDisplayP4Driver::
                               PowerState::kOff
                         : lilygo_device_driver::TDisplayP4Driver::
                               PowerState::kSleep;
  return driver_.SetPowerState(state) && prepared;
}

bool TDisplayP4Device::ExitDeviceSleep(bool deep_sleep) {
  if (deep_sleep) {
    return false;
  }
  const bool result = driver_.SetPowerState(
      lilygo_device_driver::TDisplayP4Driver::PowerState::kActive);
  if (!result) {
    LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
        "Wake device from chip sleep failed\n");
    return false;
  }
  return WaitForScreenReady();
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
  if (radio_.active || radio_.transmitting) {
    result &= DeactivateRadio();
  }
  result &= SetGpsEnabled(false);
  result &= SetImuEnabled(false);
  result &= SetEthernetEnabled(false);
  result &= SetWifiEnabled(false);
  result &= StopUsbStorage();
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

}  // namespace lilygo_box::hal
