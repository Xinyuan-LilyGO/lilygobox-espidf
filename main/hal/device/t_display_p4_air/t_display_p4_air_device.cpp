/*
 * @Description: T-Display-P4-Air 设备初始化与硬件 Provider 适配实现
 * @Author: LILYGO_L
 * @Date: 2026-05-10 13:27:05
 * @LastEditTime: 2026-08-17 09:16:56
 * @License: GPL 3.0
 */
#include "hal/device/t_display_p4_air/t_display_p4_air_device.h"

#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <iterator>
#include <limits>
#include <memory>
#include <new>
#include <string>

#include "app/storage/display_storage.h"
#include "audio/new_notification_010_c2_b16_s44100.h"
#include "base/logger.h"
#include "bhy2_parse.h"
#include "esp_attr.h"
#include "esp_err.h"
#include "esp_event.h"
#include "esp_heap_caps.h"
#include "esp_hosted.h"
#include "esp_hosted_transport_config.h"
#include "esp_lcd_mipi_dsi.h"
#include "esp_netif.h"
#include "esp_sleep.h"
#include "esp_sntp.h"
#include "esp_system.h"
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

extern "C" {
#include "rfal_chip.h"
#include "rfal_nfca.h"
#include "rfal_t2t.h"
#include "st25r3916_com.h"
}

namespace lilygo_box::hal {
namespace device = lilygo_device_driver::t_display_p4_air::device;
namespace gpio = lilygo_device_driver::t_display_p4_air::gpio;
namespace {

constexpr int kScreenBrightnessMinPercent = 0;
constexpr int kScreenBrightnessMaxPercent = 100;
constexpr int kHi8561BrightnessInputMinPercent = 10;
constexpr uint32_t kSy7200aDutyScale = 1000;
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
constexpr uint32_t kCameraOutputClearFrameCount = 3;
constexpr uint32_t kCameraWarmupFrameCount = 5;
// Radio 发送硬件超时的最小值和额外保护时间。
constexpr uint32_t kMinimumRadioTransmitTimeoutMs = 1000;
constexpr uint32_t kRadioTransmitTimeoutMarginMs = 500;
constexpr uint32_t kRadioTransmitWatchdogGraceMs = 1000;
constexpr float kRadiansToDegrees = 57.2957795F;
constexpr float kDegreesToRadians = 0.0174532925F;
constexpr float kBhi260apAccelerometerScale = 1.0F / 4096.0F;
constexpr float kBhi260apSampleRateHz = 100.0F;
constexpr uint32_t kBhi260apReportLatencyMs = 0;
constexpr uint32_t kImuHardwareReadyTimeoutMs = 5000;
constexpr uint32_t kImuHardwareReadyPollMs = 20;
constexpr const char* kCameraDeviceName = ESP_VIDEO_MIPI_CSI_DEVICE_NAME;
constexpr size_t kGpsMaxReadBufferBytes = 4096;
constexpr uint32_t kNrf9151CommandTimeoutMs = 5000;
constexpr uint32_t kNrf9151StartupDelayMs = 1000;
constexpr size_t kNrf9151PendingDataLimit = 8192;
constexpr uint32_t kNrf9151GnssUpdateIntervalMs = 1000;
constexpr uint32_t kNfcPollingTaskStackBytes = 6 * 1024;
constexpr UBaseType_t kNfcPollingTaskPriority = 3;
// 移除超时需要大于一次发现周期与标签保持时间之和，避免状态闪烁。
constexpr uint32_t kNfcDiscoveryDurationMs = 500;
constexpr uint32_t kNfcCardRemovalTimeoutMs = 800;
constexpr uint32_t kNfcActiveDeviceHoldMs = 100;
constexpr uint32_t kNfcDiscoveryRestartDelayMs = 50;
constexpr uint32_t kNfcTaskStopTimeoutMs = 2000;
constexpr int kNfcPlatformErrorBase = 1000;
constexpr uint8_t kNfcType2NdefMagic = 0xE1;
constexpr uint8_t kNfcType2NullTlv = 0x00;
constexpr uint8_t kNfcType2NdefMessageTlv = 0x03;
constexpr uint8_t kNfcType2TerminatorTlv = 0xFE;
constexpr size_t kNfcType2HeaderBytes = 16;
constexpr size_t kNfcType2MaximumReadBytes = 256;
// 以下参数只用于 Debug 日志和射频诊断。
constexpr uint32_t kNfcDebugStatusLogIntervalMs = 5000;
constexpr uint32_t kNfcDebugDiagnosticGuardTimeoutMs = 20;
constexpr uint32_t kInfraredRmtResolutionHz = 1000000;
constexpr int kNecDecodeMarginUs = 200;
constexpr uint32_t kInfraredReceiveMinimumNs = 1000;
// 关机充电状态每 5 秒短暂唤醒一次，仅用于检查 USB 是否已经拔出。
constexpr uint64_t kPowerOffMonitorWakeIntervalUs = 5ULL * 1000 * 1000;
constexpr uint32_t kPowerOffStartupHoldMs = 2 * 1000;
constexpr uint32_t kPowerOffButtonPollMs = 20;
constexpr uint32_t kPowerOffButtonReleaseTimeoutMs = 3000;
constexpr uint32_t kPowerOffRtcMagic = 0x504F4646;  // "POFF"

// RTC 内存只跨越深度睡眠保留，用于避免每次 5 秒巡检都点亮充电界面。
RTC_DATA_ATTR uint32_t g_power_off_rtc_magic = 0;
RTC_DATA_ATTR bool g_power_off_charging_screen_pending = false;
constexpr uint32_t kInfraredReceiveMaximumNs = 12 * 1000 * 1000;
constexpr uint32_t kInfraredTransmitTimeoutMs = 1000;
constexpr uint16_t kNecLeaderMarkUs = 9000;
constexpr uint16_t kNecLeaderSpaceUs = 4500;
constexpr uint16_t kNecRepeatSpaceUs = 2250;
constexpr uint16_t kNecBitMarkUs = 560;
constexpr uint16_t kNecZeroSpaceUs = 560;
constexpr uint16_t kNecOneSpaceUs = 1690;
constexpr uint16_t kNecFrameEndSpaceUs = 10000;
constexpr size_t kNecDataBitCount = 32;
constexpr size_t kNecFrameSymbolCount = kNecDataBitCount + 2;
constexpr uint32_t kCellularTaskStackBytes = 6 * 1024;
constexpr UBaseType_t kCellularTaskPriority = 3;
constexpr uint32_t kCellularCommandTimeoutMs = 2000;
constexpr uint32_t kCellularStatusPollMs = 2000;
constexpr uint32_t kCellularNetworkTimePollMs = 10000;
constexpr uint32_t kCellularSimStartupDelayMs = 3000;
constexpr uint32_t kCellularTaskStopTimeoutMs = 12000;
constexpr uint32_t kWifiInitTaskStackBytes = 6 * 1024;
constexpr UBaseType_t kWifiInitTaskPriority = 3;
constexpr uint32_t kWifiScanTaskStackBytes = 6 * 1024;
constexpr UBaseType_t kWifiScanTaskPriority = 3;
constexpr uint32_t kWifiConnectTaskStackBytes = 6 * 1024;
constexpr UBaseType_t kWifiConnectTaskPriority = 3;
constexpr uint32_t kWifiHardwareReadyTimeoutMs = 8000;
constexpr uint32_t kWifiHardwareReadyPollMs = 50;
constexpr uint32_t kWifiCoprocessorBootDelayMs = 500;
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
constexpr size_t kRadioIrqTextCapacity = 160;

enum class NecDecodeResult {
  kInvalid,
  kFrame,
  kRepeat,
};

/**
 * @brief 判断 RMT symbol 持续时间是否处于 NEC 允许误差内
 * @param actual_us RMT 读取到的持续时间
 * @param expected_us NEC 协议期望持续时间
 * @return 持续时间匹配返回 true
 */
bool IsNecDuration(uint16_t actual_us, uint16_t expected_us) {
  const int difference =
      std::abs(static_cast<int>(actual_us) - static_cast<int>(expected_us));
  return difference <= kNecDecodeMarginUs;
}

/**
 * @brief 将一组 RMT symbol 解码为标准 NEC 地址和命令
 * @param symbols RMT symbol 数组
 * @param symbol_count symbol 有效数量
 * @param address NEC 地址输出地址
 * @param command NEC 命令输出地址
 * @return 普通帧、重复帧或无效帧
 */
NecDecodeResult DecodeNecSymbols(const rmt_symbol_word_t* symbols,
    size_t symbol_count, uint8_t* address, uint8_t* command) {
  if (symbols == nullptr || address == nullptr || command == nullptr ||
      symbol_count < 2 ||
      !IsNecDuration(symbols[0].duration0, kNecLeaderMarkUs)) {
    return NecDecodeResult::kInvalid;
  }
  if (IsNecDuration(symbols[0].duration1, kNecRepeatSpaceUs) &&
      IsNecDuration(symbols[1].duration0, kNecBitMarkUs)) {
    return NecDecodeResult::kRepeat;
  }
  if (symbol_count < kNecFrameSymbolCount ||
      !IsNecDuration(symbols[0].duration1, kNecLeaderSpaceUs)) {
    return NecDecodeResult::kInvalid;
  }

  uint32_t raw_data = 0;
  for (size_t bit = 0; bit < kNecDataBitCount; ++bit) {
    const rmt_symbol_word_t& symbol = symbols[bit + 1];
    if (!IsNecDuration(symbol.duration0, kNecBitMarkUs)) {
      return NecDecodeResult::kInvalid;
    }
    if (IsNecDuration(symbol.duration1, kNecOneSpaceUs)) {
      raw_data |= 1UL << bit;
    } else if (!IsNecDuration(symbol.duration1, kNecZeroSpaceUs)) {
      return NecDecodeResult::kInvalid;
    }
  }

  const uint8_t decoded_address = raw_data & 0xFFU;
  const uint8_t inverted_address = (raw_data >> 8) & 0xFFU;
  const uint8_t decoded_command = (raw_data >> 16) & 0xFFU;
  const uint8_t inverted_command = (raw_data >> 24) & 0xFFU;
  if (static_cast<uint8_t>(~decoded_address) != inverted_address ||
      static_cast<uint8_t>(~decoded_command) != inverted_command) {
    return NecDecodeResult::kInvalid;
  }
  *address = decoded_address;
  *command = decoded_command;
  return NecDecodeResult::kFrame;
}

/**
 * @brief 将 RFAL 卡片类型转换为应用层 NFC 技术
 * @param type RFAL 卡片类型
 * @return 应用层 NFC 技术
 */
NfcTechnology ToNfcTechnology(rfalNfcDevType type) {
  switch (type) {
    case RFAL_NFC_LISTEN_TYPE_NFCA:
      return NfcTechnology::kTypeA;
    case RFAL_NFC_LISTEN_TYPE_NFCB:
      return NfcTechnology::kTypeB;
    case RFAL_NFC_LISTEN_TYPE_NFCF:
      return NfcTechnology::kTypeF;
    case RFAL_NFC_LISTEN_TYPE_NFCV:
      return NfcTechnology::kTypeV;
    case RFAL_NFC_LISTEN_TYPE_ST25TB:
      return NfcTechnology::kSt25Tb;
    default:
      return NfcTechnology::kUnknown;
  }
}

/**
 * @brief 将 RFAL 接口转换为应用层 NFC 接口
 * @param rf_interface RFAL 接口
 * @return 应用层 NFC 接口
 */
NfcRfInterface ToNfcRfInterface(rfalNfcRfInterface rf_interface) {
  switch (rf_interface) {
    case RFAL_NFC_INTERFACE_RF:
      return NfcRfInterface::kRf;
    case RFAL_NFC_INTERFACE_ISODEP:
      return NfcRfInterface::kIsoDep;
    case RFAL_NFC_INTERFACE_NFCDEP:
      return NfcRfInterface::kNfcDep;
    default:
      return NfcRfInterface::kUnknown;
  }
}

/**
 * @brief 判断字节序列是否等于指定 ASCII 文本
 * @param data 字节序列
 * @param length 字节数量
 * @param text ASCII 文本
 * @return 完全相同返回 true，否则返回 false
 */
bool NfcBytesEqualText(
    const uint8_t* data, size_t length, const char* text) {
  return data != nullptr && text != nullptr && std::strlen(text) == length &&
         std::memcmp(data, text, length) == 0;
}

/**
 * @brief 将标签字节追加为适合单行显示的文本
 * @param data 标签字节
 * @param length 字节数量
 * @param output 输出缓冲区
 * @param output_size 输出缓冲区容量
 * @param used 已使用长度
 * @param truncated 是否发生截断
 */
void AppendNfcDisplayText(const uint8_t* data, size_t length, char* output,
    size_t output_size, size_t* used, bool* truncated) {
  if (data == nullptr || output == nullptr || output_size == 0 ||
      used == nullptr || truncated == nullptr) {
    return;
  }
  for (size_t index = 0; index < length; ++index) {
    if (*used + 1 >= output_size) {
      *truncated = true;
      break;
    }
    const uint8_t value = data[index];
    const bool whitespace = value == '\r' || value == '\n' || value == '\t';
    const bool control_character = value < 0x20 || value == 0x7F;
    output[(*used)++] = whitespace
                            ? ' '
                            : (control_character ? '.'
                                                 : static_cast<char>(value));
  }
  output[*used] = '\0';
}

/**
 * @brief 将字符串追加到 NFC 内容摘要
 * @param text 待追加字符串
 * @param status NFC 状态
 * @param used 已使用长度
 */
void AppendNfcContentText(
    const char* text, NfcStatus* status, size_t* used) {
  if (text == nullptr || status == nullptr || used == nullptr) {
    return;
  }
  AppendNfcDisplayText(reinterpret_cast<const uint8_t*>(text),
      std::strlen(text), status->content, sizeof(status->content), used,
      &status->content_truncated);
}

/**
 * @brief 将字节序列复制为 NFC 显示文本
 * @param data 字节序列
 * @param length 字节数量
 * @param output 输出缓冲区
 * @param output_size 输出缓冲区容量
 * @param truncated 是否发生截断
 */
void CopyNfcDisplayText(const uint8_t* data, size_t length, char* output,
    size_t output_size, bool* truncated) {
  if (output == nullptr || output_size == 0 || truncated == nullptr) {
    return;
  }
  output[0] = '\0';
  size_t used = 0;
  AppendNfcDisplayText(
      data, length, output, output_size, &used, truncated);
}

/**
 * @brief 获取常用 NDEF URI 标识码对应的前缀
 * @param code URI 标识码
 * @return URI 前缀
 */
const char* NfcNdefUriPrefix(uint8_t code) {
  switch (code) {
    case 1:
      return "http://www.";
    case 2:
      return "https://www.";
    case 3:
      return "http://";
    case 4:
      return "https://";
    case 5:
      return "tel:";
    case 6:
      return "mailto:";
    default:
      return "";
  }
}

/**
 * @brief 记录 NDEF 数据不完整或格式错误
 * @param complete 输入是否包含完整 NDEF 消息
 * @param status NFC 状态
 */
void SetNfcNdefParseFailure(bool complete, NfcStatus* status) {
  if (status == nullptr) {
    return;
  }
  if (complete) {
    status->content_error = RFAL_ERR_PROTO;
  } else {
    status->content_truncated = true;
  }
}

/**
 * @brief 解析首条 NDEF 记录中的文本或 URI
 * @param message NDEF 消息
 * @param message_length 已读取消息长度
 * @param complete 是否已经读取完整消息
 * @param status NFC 状态
 */
void ParseFirstNfcNdefRecord(const uint8_t* message, size_t message_length,
    bool complete, NfcStatus* status) {
  if (message == nullptr || status == nullptr || message_length < 3) {
    SetNfcNdefParseFailure(complete, status);
    return;
  }

  size_t offset = 0;
  const uint8_t header = message[offset++];
  const bool chunked = (header & 0x20) != 0;
  const bool short_record = (header & 0x10) != 0;
  const bool id_present = (header & 0x08) != 0;
  const uint8_t tnf = header & 0x07;
  const size_t type_length = message[offset++];

  size_t payload_length = 0;
  if (short_record) {
    payload_length = message[offset++];
  } else {
    if (message_length - offset < 4) {
      SetNfcNdefParseFailure(complete, status);
      return;
    }
    payload_length = static_cast<uint32_t>(message[offset]) << 24U |
                     static_cast<uint32_t>(message[offset + 1]) << 16U |
                     static_cast<uint32_t>(message[offset + 2]) << 8U |
                     message[offset + 3];
    offset += 4;
  }

  size_t id_length = 0;
  if (id_present) {
    if (offset >= message_length) {
      SetNfcNdefParseFailure(complete, status);
      return;
    }
    id_length = message[offset++];
  }
  const size_t remaining = message_length - offset;
  if (type_length > remaining || id_length > remaining - type_length ||
      payload_length > remaining - type_length - id_length) {
    SetNfcNdefParseFailure(complete, status);
    return;
  }

  const uint8_t* type = message + offset;
  offset += type_length + id_length;
  const uint8_t* payload = message + offset;
  if (chunked) {
    status->ndef_record_type = NfcNdefRecordType::kUnsupported;
    return;
  }

  if (tnf == 0x01 && NfcBytesEqualText(type, type_length, "T")) {
    status->ndef_record_type = NfcNdefRecordType::kText;
    if (payload_length == 0) {
      return;
    }
    const uint8_t text_status = payload[0];
    const size_t language_length = text_status & 0x3F;
    if (language_length + 1 > payload_length) {
      SetNfcNdefParseFailure(complete, status);
      return;
    }
    bool language_truncated = false;
    CopyNfcDisplayText(payload + 1, language_length, status->ndef_language,
        sizeof(status->ndef_language), &language_truncated);
    status->content_truncated |= language_truncated;
    const uint8_t* text = payload + language_length + 1;
    const size_t text_length = payload_length - language_length - 1;
    if ((text_status & 0x80) != 0) {
      std::snprintf(status->content, sizeof(status->content),
          "UTF-16 text (%u bytes)", static_cast<unsigned>(text_length));
      return;
    }
    CopyNfcDisplayText(text, text_length, status->content,
        sizeof(status->content), &status->content_truncated);
    return;
  }

  if (tnf == 0x01 && NfcBytesEqualText(type, type_length, "U")) {
    status->ndef_record_type = NfcNdefRecordType::kUri;
    if (payload_length == 0) {
      return;
    }
    size_t used = 0;
    AppendNfcContentText(NfcNdefUriPrefix(payload[0]), status, &used);
    AppendNfcDisplayText(payload + 1, payload_length - 1, status->content,
        sizeof(status->content), &used, &status->content_truncated);
    return;
  }

  if (tnf == 0x03) {
    status->ndef_record_type = NfcNdefRecordType::kUri;
    CopyNfcDisplayText(type, type_length, status->content,
        sizeof(status->content), &status->content_truncated);
    return;
  }
  status->ndef_record_type = NfcNdefRecordType::kUnsupported;
}

/**
 * @brief 解析 Type 2 标签数据区中的 NDEF TLV
 * @param data Type 2 数据区
 * @param data_length 已读取长度
 * @param status NFC 状态
 */
void ParseNfcType2Tlvs(
    const uint8_t* data, size_t data_length, NfcStatus* status) {
  if (data == nullptr || status == nullptr) {
    return;
  }
  size_t offset = 0;
  while (offset < data_length) {
    const uint8_t type = data[offset++];
    if (type == kNfcType2NullTlv) {
      continue;
    }
    if (type == kNfcType2TerminatorTlv) {
      return;
    }
    if (offset >= data_length) {
      status->content_truncated = true;
      return;
    }

    size_t value_length = data[offset++];
    if (value_length == 0xFF) {
      if (data_length - offset < 2) {
        status->content_truncated = true;
        return;
      }
      value_length =
          static_cast<size_t>(data[offset]) << 8U | data[offset + 1];
      offset += 2;
    }
    const size_t available_length =
        std::min(value_length, data_length - offset);
    if (type == kNfcType2NdefMessageTlv) {
      status->ndef_present = true;
      status->ndef_message_length = value_length;
      status->content_truncated |= available_length < value_length;
      ParseFirstNfcNdefRecord(data + offset, available_length,
          available_length == value_length, status);
      return;
    }
    if (available_length < value_length) {
      status->content_truncated = true;
      return;
    }
    offset += value_length;
  }
}

/**
 * @brief 读取 Type 2 标签容量和有限长度的 NDEF 内容
 * @param status NFC 状态
 */
void ReadNfcType2Content(NfcStatus* status) {
  if (status == nullptr) {
    return;
  }
  std::array<uint8_t, kNfcType2MaximumReadBytes> memory = {};
  uint16_t received_length = 0;
  ReturnCode result = rfalT2TPollerRead(
      0, memory.data(), RFAL_T2T_READ_DATA_LEN, &received_length);
  if (result != RFAL_ERR_NONE || received_length < kNfcType2HeaderBytes) {
    status->content_error = result == RFAL_ERR_NONE ? RFAL_ERR_PROTO : result;
    return;
  }

  const uint8_t* capability = memory.data() + 12;
  status->ndef_formatted = capability[0] == kNfcType2NdefMagic;
  if (!status->ndef_formatted) {
    return;
  }
  status->memory_capacity_bytes = static_cast<size_t>(capability[2]) * 8;
  status->read_only = (capability[3] & 0x0F) == 0x0F;
  const size_t total_memory_bytes =
      kNfcType2HeaderBytes + status->memory_capacity_bytes;
  const size_t read_limit =
      std::min(total_memory_bytes, memory.size());
  status->content_truncated = total_memory_bytes > read_limit;

  size_t bytes_read = kNfcType2HeaderBytes;
  while (bytes_read < read_limit) {
    const size_t page = bytes_read / RFAL_T2T_BLOCK_LEN;
    std::array<uint8_t, RFAL_T2T_READ_DATA_LEN> block = {};
    received_length = 0;
    result = rfalT2TPollerRead(static_cast<uint8_t>(page), block.data(),
        static_cast<uint16_t>(block.size()), &received_length);
    if (result != RFAL_ERR_NONE || received_length < block.size()) {
      status->content_error =
          result == RFAL_ERR_NONE ? RFAL_ERR_PROTO : result;
      status->content_truncated = true;
      break;
    }
    const size_t copy_length =
        std::min(block.size(), read_limit - bytes_read);
    std::memcpy(memory.data() + bytes_read, block.data(), copy_length);
    bytes_read += copy_length;
  }

  if (bytes_read > kNfcType2HeaderBytes) {
    ParseNfcType2Tlvs(memory.data() + kNfcType2HeaderBytes,
        bytes_read - kNfcType2HeaderBytes, status);
  }
}

/**
 * @brief 提取已激活 NFC 标签的关键协议字段和内容
 * @param device RFAL 已激活设备
 * @param status NFC 状态
 */
void PopulateNfcTagDetails(const rfalNfcDevice& device, NfcStatus* status) {
  if (status == nullptr) {
    return;
  }
  status->technology = ToNfcTechnology(device.type);
  status->rf_interface = ToNfcRfInterface(device.rfInterface);

  switch (device.type) {
    case RFAL_NFC_LISTEN_TYPE_NFCA:
      status->atqa =
          static_cast<uint16_t>(device.dev.nfca.sensRes.platformInfo) << 8U |
          device.dev.nfca.sensRes.anticollisionInfo;
      status->sak = device.dev.nfca.selRes.sak;
      switch (device.dev.nfca.type) {
        case RFAL_NFCA_T1T:
          status->tag_type = NfcTagType::kType1;
          break;
        case RFAL_NFCA_T2T:
          status->tag_type = NfcTagType::kType2;
          ReadNfcType2Content(status);
          break;
        case RFAL_NFCA_T4T:
        case RFAL_NFCA_T4T_NFCDEP:
          status->tag_type = NfcTagType::kType4;
          break;
        case RFAL_NFCA_NFCDEP:
          status->tag_type = NfcTagType::kPeerToPeer;
          break;
        default:
          break;
      }
      break;
    case RFAL_NFC_LISTEN_TYPE_NFCB:
      status->tag_type = rfalNfcbIsIsoDepSupported(&device.dev.nfcb)
                             ? NfcTagType::kType4
                             : NfcTagType::kUnknown;
      status->afi = device.dev.nfcb.sensbRes.appData.AFI;
      break;
    case RFAL_NFC_LISTEN_TYPE_NFCF:
      status->tag_type = rfalNfcfIsNfcDepSupported(&device.dev.nfcf)
                             ? NfcTagType::kPeerToPeer
                             : NfcTagType::kType3;
      status->system_code =
          static_cast<uint16_t>(device.dev.nfcf.sensfRes.RD[0]) << 8U |
          device.dev.nfcf.sensfRes.RD[1];
      break;
    case RFAL_NFC_LISTEN_TYPE_NFCV:
      status->tag_type = NfcTagType::kType5;
      status->dsfid = device.dev.nfcv.InvRes.DSFID;
      if (device.dev.nfcv.InvRes.UID[RFAL_NFCV_UID_LEN - 1U] == 0xE0) {
        status->manufacturer_code =
            device.dev.nfcv.InvRes.UID[RFAL_NFCV_UID_LEN - 2U];
      }
      break;
    case RFAL_NFC_LISTEN_TYPE_ST25TB:
      status->tag_type = NfcTagType::kProprietary;
      status->chip_id = device.dev.st25tb.chipID;
      break;
    default:
      status->tag_type = NfcTagType::kUnknown;
      break;
  }
}

/**
 * @brief 创建 NFC 轮询发现参数
 * @return 支持 NFC-A、B、F、V 和 ST25TB 的发现参数
 */
rfalNfcDiscoverParam CreateNfcDiscoveryParameters() {
  rfalNfcDiscoverParam parameters = {};
  parameters.compMode = RFAL_COMPLIANCE_MODE_NFC;
  parameters.techs2Find = RFAL_NFC_POLL_TECH_A | RFAL_NFC_POLL_TECH_B |
                          RFAL_NFC_POLL_TECH_F | RFAL_NFC_POLL_TECH_V |
                          RFAL_NFC_POLL_TECH_ST25TB;
  parameters.totalDuration = kNfcDiscoveryDurationMs;
  parameters.devLimit = 1;
  parameters.maxBR = RFAL_BR_848;
  parameters.nfcfBR = RFAL_BR_212;
  parameters.ap2pBR = RFAL_BR_424;
  parameters.notifyCb = nullptr;
  parameters.wakeupEnabled = false;
  parameters.wakeupConfigDefault = true;
  return parameters;
}

/**
 * @brief 在 Debug 日志启用时执行 NFC 射频诊断
 * @param nfc_driver ST25R3916 驱动
 */
void RunNfcDebugDiagnostics(
    stsw_st25rfal002_cpp_bus_driver::St25r3916x& nfc_driver) {
  if (!ShouldLog(LogLevel::kDebug)) {
    return;
  }

  LogMessage(LogLevel::kDebug, __FILE__, __LINE__,
      "NFC reader detected (identity: 0x%02X, revision: %u, chip: %s)\n",
      static_cast<unsigned>(nfc_driver.chip_identity()),
      static_cast<unsigned>(nfc_driver.chip_revision()),
      nfc_driver.is_st25r3916b() ? "ST25R3916B" : "ST25R3916");

  const ReturnCode init_result = rfalNfcaPollerInitialize();
  ReturnCode field_result = RFAL_ERR_INVALID_HANDLE;
  ReturnCode amplitude_result = RFAL_ERR_INVALID_HANDLE;
  ReturnCode probe_result = RFAL_ERR_INVALID_HANDLE;
  uint8_t amplitude = 0;
  uint8_t op_control = 0;
  uint8_t aux_display = 0;
  uint8_t tx_driver = 0;
  uint8_t field_threshold = 0;
  rfalNfcaSensRes sens_res = {};

  if (init_result == RFAL_ERR_NONE) {
    field_result = rfalFieldOnAndStartGT();
  }
  if (field_result == RFAL_ERR_NONE) {
    const TickType_t guard_start_tick = xTaskGetTickCount();
    while (!rfalIsGTExpired() &&
           xTaskGetTickCount() - guard_start_tick <
               pdMS_TO_TICKS(kNfcDebugDiagnosticGuardTimeoutMs)) {
      nfc_driver.Worker();
      vTaskDelay(pdMS_TO_TICKS(1));
    }
    amplitude_result = rfalChipMeasureAmplitude(&amplitude);
    probe_result = rfalNfcaPollerTechnologyDetection(
        RFAL_COMPLIANCE_MODE_ISO, &sens_res);
  }

  st25r3916ReadRegister(ST25R3916_REG_OP_CONTROL, &op_control);
  st25r3916ReadRegister(ST25R3916_REG_AUX_DISPLAY, &aux_display);
  st25r3916ReadRegister(ST25R3916_REG_TX_DRIVER, &tx_driver);
  st25r3916ReadRegister(
      ST25R3916_REG_FIELD_THRESHOLD_ACTV, &field_threshold);
  rfalFieldOff();

  LogMessage(LogLevel::kDebug, __FILE__, __LINE__,
      "NFC RF diagnostic (init: %u, field: %u, amplitude result: %u, "
      "amplitude: %u, NFC-A probe: %u, ATQA: %02X%02X, op: 0x%02X, "
      "aux: 0x%02X, tx driver: 0x%02X, field threshold: 0x%02X)\n",
      static_cast<unsigned>(init_result),
      static_cast<unsigned>(field_result),
      static_cast<unsigned>(amplitude_result),
      static_cast<unsigned>(amplitude), static_cast<unsigned>(probe_result),
      static_cast<unsigned>(sens_res.anticollisionInfo),
      static_cast<unsigned>(sens_res.platformInfo),
      static_cast<unsigned>(op_control), static_cast<unsigned>(aux_display),
      static_cast<unsigned>(tx_driver),
      static_cast<unsigned>(field_threshold));
}

/**
 * @brief 删除字符串首尾的 ASCII 空白字符
 * @param text 待处理字符串
 * @return 删除空白后的字符串副本
 */
std::string TrimAsciiWhitespace(const std::string& text) {
  size_t first = 0;
  while (first < text.size() &&
         std::isspace(static_cast<unsigned char>(text[first])) != 0) {
    ++first;
  }
  size_t last = text.size();
  while (last > first &&
         std::isspace(static_cast<unsigned char>(text[last - 1])) != 0) {
    --last;
  }
  return text.substr(first, last - first);
}

/**
 * @brief 从 AT 完整响应中提取指定前缀所在的数据行
 * @param response AT 完整响应
 * @param prefix 数据行前缀
 * @param value 数据行去除前缀后的输出地址
 * @return 找到有效数据行返回 true
 */
bool ExtractAtPrefixedValue(
    const std::string& response, const char* prefix, std::string* value) {
  if (prefix == nullptr || value == nullptr) {
    return false;
  }
  const size_t prefix_position = response.find(prefix);
  if (prefix_position == std::string::npos) {
    return false;
  }
  const size_t value_start = prefix_position + std::strlen(prefix);
  const size_t line_end = response.find_first_of("\r\n", value_start);
  *value = TrimAsciiWhitespace(response.substr(
      value_start, line_end == std::string::npos ? std::string::npos
                                                 : line_end - value_start));
  return !value->empty();
}

/**
 * @brief 从 AT 响应中提取首个纯数字数据行
 * @param response AT 完整响应
 * @param value 数字字符串输出地址
 * @return 找到纯数字数据行返回 true
 */
bool ExtractAtNumericLine(const std::string& response, std::string* value) {
  if (value == nullptr) {
    return false;
  }
  size_t line_start = 0;
  while (line_start < response.size()) {
    const size_t line_end = response.find_first_of("\r\n", line_start);
    const std::string line = TrimAsciiWhitespace(response.substr(
        line_start, line_end == std::string::npos ? std::string::npos
                                                  : line_end - line_start));
    if (!line.empty() &&
        std::all_of(line.begin(), line.end(), [](unsigned char character) {
          return std::isdigit(character) != 0;
        })) {
      *value = line;
      return true;
    }
    if (line_end == std::string::npos) {
      break;
    }
    line_start = line_end + 1;
  }
  return false;
}

/**
 * @brief 解析 +CPIN 响应中的 SIM 卡访问状态
 * @param response AT+CPIN? 完整响应
 * @param state SIM 卡状态输出地址
 * @return 解析成功返回 true
 */
bool ParseCellularSimState(
    const std::string& response, CellularSimState* state) {
  std::string value;
  if (state == nullptr ||
      !ExtractAtPrefixedValue(response, "+CPIN:", &value)) {
    return false;
  }
  if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
    value = value.substr(1, value.size() - 2);
  }

  if (value == "READY") {
    *state = CellularSimState::kReady;
  } else if (value == "SIM PIN" || value == "SIM PIN2") {
    *state = CellularSimState::kPinRequired;
  } else if (value == "SIM PUK" || value == "SIM PUK2") {
    *state = CellularSimState::kPukRequired;
  } else if (value.rfind("PH-", 0) == 0) {
    *state = CellularSimState::kBlocked;
  } else {
    *state = CellularSimState::kFailure;
    return false;
  }
  return true;
}

/**
 * @brief 获取 SIM 卡状态的日志文本
 * @param state SIM 卡状态
 * @return 静态文本
 */
const char* CellularSimStateLogText(CellularSimState state) {
  switch (state) {
    case CellularSimState::kReady:
      return "ready";
    case CellularSimState::kPinRequired:
      return "PIN required";
    case CellularSimState::kPukRequired:
      return "PUK required";
    case CellularSimState::kBlocked:
      return "blocked";
    case CellularSimState::kFailure:
      return "invalid status";
    case CellularSimState::kUnavailable:
      return "unavailable";
    case CellularSimState::kUnknown:
      return "unknown";
  }
  return "unknown";
}

/**
 * @brief 将 CEREG 数值转换为应用层注册状态
 * @param registration CEREG 注册数值
 * @return 应用层注册状态
 */
CellularRegistrationState ToCellularRegistrationState(int registration) {
  switch (registration) {
    case 0:
      return CellularRegistrationState::kNotRegistered;
    case 1:
      return CellularRegistrationState::kRegisteredHome;
    case 2:
      return CellularRegistrationState::kSearching;
    case 3:
      return CellularRegistrationState::kDenied;
    case 5:
      return CellularRegistrationState::kRegisteredRoaming;
    default:
      return CellularRegistrationState::kUnknown;
  }
}

/**
 * @brief 判断蜂窝网络是否已经完成注册
 * @param state 当前网络注册状态
 * @return 已注册到本地或漫游网络时返回 true
 */
bool IsCellularRegistered(CellularRegistrationState state) {
  return state == CellularRegistrationState::kRegisteredHome ||
         state == CellularRegistrationState::kRegisteredRoaming;
}

/**
 * @brief 解析 +CEREG 响应中的网络注册状态
 * @param response AT+CEREG? 完整响应
 * @param state 注册状态输出地址
 * @return 解析成功返回 true
 */
bool ParseCellularRegistration(
    const std::string& response, CellularRegistrationState* state) {
  std::string value;
  if (state == nullptr ||
      !ExtractAtPrefixedValue(response, "+CEREG:", &value)) {
    return false;
  }
  int reporting_mode = 0;
  int registration = 0;
  const int parsed =
      std::sscanf(value.c_str(), "%d,%d", &reporting_mode, &registration);
  if (parsed == 1) {
    registration = reporting_mode;
  } else if (parsed != 2) {
    return false;
  }
  *state = ToCellularRegistrationState(registration);
  return true;
}

/**
 * @brief 解析 +CSQ 响应并换算 RSSI
 * @param response AT+CSQ 完整响应
 * @param signal_quality CSQ 输出地址
 * @param rssi_dbm RSSI 输出地址
 * @return 解析成功返回 true
 */
bool ParseCellularSignal(
    const std::string& response, int* signal_quality, int* rssi_dbm) {
  std::string value;
  if (signal_quality == nullptr || rssi_dbm == nullptr ||
      !ExtractAtPrefixedValue(response, "+CSQ:", &value)) {
    return false;
  }
  int quality = 99;
  int bit_error_rate = 99;
  if (std::sscanf(value.c_str(), "%d,%d", &quality, &bit_error_rate) != 2) {
    return false;
  }
  *signal_quality = quality;
  *rssi_dbm = quality >= 0 && quality <= 31 ? -113 + quality * 2 : 0;
  return true;
}

/**
 * @brief 解析 +COPS 响应中的运营商字段
 * @param response AT+COPS? 完整响应
 * @param operator_name 运营商字符串输出地址
 * @return 解析成功返回 true
 */
bool ParseCellularOperator(
    const std::string& response, std::string* operator_name) {
  std::string value;
  if (operator_name == nullptr ||
      !ExtractAtPrefixedValue(response, "+COPS:", &value)) {
    return false;
  }
  const size_t first_quote = value.find('"');
  if (first_quote != std::string::npos) {
    const size_t second_quote = value.find('"', first_quote + 1);
    if (second_quote != std::string::npos) {
      *operator_name =
          value.substr(first_quote + 1, second_quote - first_quote - 1);
      return !operator_name->empty();
    }
  }
  const size_t last_comma = value.rfind(',');
  *operator_name = TrimAsciiWhitespace(
      last_comma == std::string::npos ? value : value.substr(last_comma + 1));
  return !operator_name->empty();
}

/**
 * @brief 解析 +CCLK 响应中的网络时间
 * @param response AT+CCLK? 完整响应
 * @param network_time 网络时间字符串输出地址
 * @return 解析并校验成功返回 true
 */
bool ParseCellularNetworkTime(
    const std::string& response, std::string* network_time) {
  std::string value;
  if (network_time == nullptr ||
      !ExtractAtPrefixedValue(response, "+CCLK:", &value)) {
    return false;
  }

  int year = 0;
  int month = 0;
  int day = 0;
  int hour = 0;
  int minute = 0;
  int second = 0;
  int timezone_quarters = 0;
  char timezone_sign = '\0';
  if (std::sscanf(value.c_str(), "\"%d/%d/%d,%d:%d:%d%c%d\"", &year,
          &month, &day, &hour, &minute, &second, &timezone_sign,
          &timezone_quarters) != 8 ||
      year < 0 || year > 99 || month < 1 || month > 12 || day < 1 ||
      hour < 0 || hour > 23 || minute < 0 || minute > 59 || second < 0 ||
      second > 59 || (timezone_sign != '+' && timezone_sign != '-') ||
      timezone_quarters < 0 || timezone_quarters > 48) {
    return false;
  }

  constexpr std::array<int, 12> kDaysInMonth = {
      31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  int maximum_day = kDaysInMonth[static_cast<size_t>(month - 1)];
  const int full_year = 2000 + year;
  if (month == 2 &&
      ((full_year % 4 == 0 && full_year % 100 != 0) || full_year % 400 == 0)) {
    maximum_day = 29;
  }
  if (day > maximum_day) {
    return false;
  }

  const size_t first_quote = value.find('"');
  const size_t second_quote = value.find('"', first_quote + 1);
  if (first_quote == std::string::npos || second_quote == std::string::npos ||
      second_quote <= first_quote + 1) {
    return false;
  }
  *network_time =
      value.substr(first_quote + 1, second_quote - first_quote - 1);
  return true;
}

// LR1121 IRQ 位与日志名称映射。
struct RadioIrqDescription {
  lr11xx_system_irq_mask_t mask;
  const char* name;
};

constexpr std::array<RadioIrqDescription, 5> kRadioIrqDescriptions = {{
    {LR11XX_SYSTEM_IRQ_TX_DONE, "TX_DONE"},
    {LR11XX_SYSTEM_IRQ_RX_DONE, "RX_DONE"},
    {LR11XX_SYSTEM_IRQ_HEADER_ERROR, "HEADER_ERROR"},
    {LR11XX_SYSTEM_IRQ_CRC_ERROR, "CRC_ERROR"},
    {LR11XX_SYSTEM_IRQ_TIMEOUT, "TIMEOUT"},
}};
constexpr lr11xx_system_irq_mask_t kRadioEventIrqMask =
    LR11XX_SYSTEM_IRQ_TX_DONE | LR11XX_SYSTEM_IRQ_RX_DONE |
    LR11XX_SYSTEM_IRQ_HEADER_ERROR | LR11XX_SYSTEM_IRQ_CRC_ERROR |
    LR11XX_SYSTEM_IRQ_TIMEOUT;

/**
 * @brief 将 LR1121 IRQ 位掩码格式化为可读名称和十六进制数值
 * @param irq_mask LR1121 IRQ 位掩码
 * @param output 输出文本缓冲区
 * @param output_size 输出文本缓冲区大小
 */
void FormatRadioIrqMask(
    lr11xx_system_irq_mask_t irq_mask, char* output, size_t output_size) {
  if (output == nullptr || output_size == 0) {
    return;
  }
  output[0] = '\0';
  size_t used = 0;
  bool has_name = false;

  const auto append_name = [&](const char* name) {
    const int result = std::snprintf(
        output + used, output_size - used, "%s%s", has_name ? " | " : "", name);
    if (result < 0 || static_cast<size_t>(result) >= output_size - used) {
      output[output_size - 1] = '\0';
      return false;
    }
    used += static_cast<size_t>(result);
    has_name = true;
    return true;
  };

  lr11xx_system_irq_mask_t unknown_mask = irq_mask;
  for (const RadioIrqDescription& description : kRadioIrqDescriptions) {
    if ((irq_mask & description.mask) == 0) {
      continue;
    }
    if (!append_name(description.name)) {
      return;
    }
    unknown_mask &= ~description.mask;
  }
  if (unknown_mask != 0 && !append_name("UNKNOWN")) {
    return;
  }
  if (!has_name && !append_name("NONE")) {
    return;
  }

  std::snprintf(output + used, output_size - used, " (0x%08lX)",
      static_cast<unsigned long>(irq_mask));
}

// 当前接收 SNTP 同步回调的设备实例
std::atomic<TDisplayP4AirDevice*> g_wifi_time_sync_owner{nullptr};

/**
 * @brief 将触摸点标记为屏幕边缘手势
 * @param point 待更新的触摸点
 */
void SetEdgeTouchPoint(TouchPoint* point) {
  if (point == nullptr) {
    return;
  }
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

/**
 * @brief 将屏幕亮度限制在驱动支持的百分比范围
 * @param percent 原始亮度百分比
 * @return 限制后的亮度百分比
 */
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
    return {.value = 0, .scale = kSy7200aDutyScale};
  }

  const int input_percent =
      std::max(clamped_percent, kHi8561BrightnessInputMinPercent);
  constexpr int kInputRangeSquared =
      kScreenBrightnessMaxPercent * kScreenBrightnessMaxPercent;
  const uint32_t scaled_duty = static_cast<uint32_t>(
      input_percent * input_percent * kSy7200aDutyScale);
  return {
      .value = (scaled_duty + kInputRangeSquared / 2) / kInputRangeSquared,
      .scale = kSy7200aDutyScale,
  };
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
bool IsFiveGWifiChannel(int channel) { return channel > 14; }

/**
 * @brief 控制板载 WiFi 协处理器电源
 * @param driver 当前板级驱动
 * @param enabled true 上电，false 断电
 * @return GPIO 状态切换成功返回 true，否则返回 false
 */
bool SetWifiCoprocessorPowerEnabled(
    TDisplayP4AirBoardDriver& driver, bool enabled) {
  return driver.SetEsp32c5PowerEnabled(enabled);
}

/**
 * @brief 判断触摸控制器是否可用
 * @param driver 当前板级驱动
 * @return 触摸控制器可用返回 true
 */
bool IsTouchReady(const TDisplayP4AirBoardDriver& driver) {
  return driver.IsHi8561TouchReady();
}

/**
 * @brief 将应用层扩频因子转换为 LR1121 LoRa 枚举
 * @param value 应用层扩频因子
 * @param spreading_factor LR1121 扩频因子输出地址
 * @return 扩频因子受支持返回 true
 */
bool SelectLoraSpreadingFactor(
    uint8_t value, lr11xx_radio_lora_sf_t* spreading_factor) {
  if (spreading_factor == nullptr) {
    return false;
  }
  switch (value) {
    case 5:
      *spreading_factor = LR11XX_RADIO_LORA_SF5;
      return true;
    case 6:
      *spreading_factor = LR11XX_RADIO_LORA_SF6;
      return true;
    case 7:
      *spreading_factor = LR11XX_RADIO_LORA_SF7;
      return true;
    case 8:
      *spreading_factor = LR11XX_RADIO_LORA_SF8;
      return true;
    case 9:
      *spreading_factor = LR11XX_RADIO_LORA_SF9;
      return true;
    case 10:
      *spreading_factor = LR11XX_RADIO_LORA_SF10;
      return true;
    case 11:
      *spreading_factor = LR11XX_RADIO_LORA_SF11;
      return true;
    case 12:
      *spreading_factor = LR11XX_RADIO_LORA_SF12;
      return true;
    default:
      return false;
  }
}

/**
 * @brief 将应用层带宽转换为 LR1121 LoRa 带宽枚举
 * @param bandwidth_hz 应用层带宽，单位 Hz
 * @param bandwidth LR1121 带宽输出地址
 * @return 带宽受支持返回 true
 */
bool SelectLoraBandwidth(
    uint32_t bandwidth_hz, lr11xx_radio_lora_bw_t* bandwidth) {
  if (bandwidth == nullptr) {
    return false;
  }
  switch (bandwidth_hz) {
    case 62500:
      *bandwidth = LR11XX_RADIO_LORA_BW_62;
      return true;
    case 125000:
      *bandwidth = LR11XX_RADIO_LORA_BW_125;
      return true;
    case 200000:
      *bandwidth = LR11XX_RADIO_LORA_BW_200;
      return true;
    case 250000:
      *bandwidth = LR11XX_RADIO_LORA_BW_250;
      return true;
    case 400000:
      *bandwidth = LR11XX_RADIO_LORA_BW_400;
      return true;
    case 500000:
      *bandwidth = LR11XX_RADIO_LORA_BW_500;
      return true;
    case 800000:
      *bandwidth = LR11XX_RADIO_LORA_BW_800;
      return true;
    default:
      return false;
  }
}

/**
 * @brief 将应用层编码率分母转换为 LR1121 LoRa 枚举
 * @param denominator 应用层编码率分母
 * @param coding_rate LR1121 编码率输出地址
 * @return 编码率受支持返回 true
 */
bool SelectLoraCodingRate(
    uint8_t denominator, lr11xx_radio_lora_cr_t* coding_rate) {
  if (coding_rate == nullptr) {
    return false;
  }
  switch (denominator) {
    case 5:
      *coding_rate = LR11XX_RADIO_LORA_CR_4_5;
      return true;
    case 6:
      *coding_rate = LR11XX_RADIO_LORA_CR_4_6;
      return true;
    case 7:
      *coding_rate = LR11XX_RADIO_LORA_CR_4_7;
      return true;
    case 8:
      *coding_rate = LR11XX_RADIO_LORA_CR_4_8;
      return true;
    default:
      return false;
  }
}

bool ShouldEnableLoraLdro(const LoraRadioConfig& config);

/**
 * @brief 创建 LR1121 LoRa 数据包参数
 * @param source 应用层 LoRa 配置
 * @param payload_length 当前收发负载长度
 * @return LR1121 数据包参数
 */
lr11xx_radio_pkt_params_lora_t MakeLr1121PacketConfig(
    const LoraRadioConfig& source, uint8_t payload_length) {
  return {
      .preamble_len_in_symb = source.preamble_length,
      .header_type = LR11XX_RADIO_LORA_PKT_EXPLICIT,
      .pld_len_in_bytes = payload_length,
      .crc = source.crc_enabled ? LR11XX_RADIO_LORA_CRC_ON
                                : LR11XX_RADIO_LORA_CRC_OFF,
      .iq = source.invert_iq ? LR11XX_RADIO_LORA_IQ_INVERTED
                             : LR11XX_RADIO_LORA_IQ_STANDARD,
  };
}

/**
 * @brief 使用当前 LoRa 参数重新进入连续接收
 * @param lr1121 LR1121 驱动
 * @param config 应用层 LoRa 配置
 * @return 接收启动成功返回 true
 */
bool StartLr1121Receive(
    usp_cpp_bus_driver::Lr11xx& lr1121, const LoraRadioConfig& config) {
  const lr11xx_radio_pkt_params_lora_t packet =
      MakeLr1121PacketConfig(config, UINT8_MAX);
  return lr1121.Invoke(lr11xx_radio_set_lora_pkt_params, &packet) ==
             LR11XX_STATUS_OK &&
         lr1121.StartReceive(0);
}

struct Lr1121ImageCalibrationBand {
  uint16_t minimum_mhz = 0;
  uint16_t maximum_mhz = 0;
};

/**
 * @brief 选择覆盖目标 Sub-GHz 频率的 LR1121 镜像校准区间
 * @param frequency_hz 目标射频频率
 * @param band 校准区间输出地址
 * @return 目标位于 LR1121 Sub-GHz 路径且区间有效时返回 true
 */
bool SelectLr1121ImageCalibrationBand(
    uint32_t frequency_hz, Lr1121ImageCalibrationBand* band) {
  static constexpr Lr1121ImageCalibrationBand kStandardBands[] = {
      {430, 440},
      {470, 510},
      {779, 787},
      {863, 870},
      {902, 928},
  };
  if (band == nullptr || frequency_hz < 150000000U ||
      frequency_hz > 960000000U) {
    return false;
  }

  for (const Lr1121ImageCalibrationBand& standard_band : kStandardBands) {
    if (frequency_hz >=
            static_cast<uint32_t>(standard_band.minimum_mhz) * 1000000U &&
        frequency_hz <=
            static_cast<uint32_t>(standard_band.maximum_mhz) * 1000000U) {
      *band = standard_band;
      return true;
    }
  }

  // 非标准频段使用目标频率前后各 10 MHz 的窗口，频率变化超过
  // LR1121 手册要求的 10 MHz 阈值后会重新执行镜像校准。
  const uint16_t frequency_mhz = static_cast<uint16_t>(frequency_hz / 1000000U);
  band->minimum_mhz =
      frequency_mhz > 160 ? static_cast<uint16_t>(frequency_mhz - 10) : 150;
  band->maximum_mhz = static_cast<uint16_t>(
      std::min<uint32_t>(960U, static_cast<uint32_t>(frequency_mhz) + 10U));
  return band->minimum_mhz < band->maximum_mhz;
}

/**
 * @brief 确保 LR1121 已完成目标 Sub-GHz 区间的镜像校准
 * @param lr1121 LR1121 驱动
 * @param frequency_hz 目标射频频率
 * @param calibrated_minimum_mhz 已缓存校准区间下限
 * @param calibrated_maximum_mhz 已缓存校准区间上限
 * @return 无需校准或校准成功时返回 true
 */
bool EnsureLr1121ImageCalibration(usp_cpp_bus_driver::Lr11xx& lr1121,
    uint32_t frequency_hz, uint16_t* calibrated_minimum_mhz,
    uint16_t* calibrated_maximum_mhz) {
  if (frequency_hz >= 2400000000U && frequency_hz <= 2500000000U) {
    // CalibImage 只适用于 RFI_N/P_LF Sub-GHz 接收路径。
    return true;
  }
  if (calibrated_minimum_mhz == nullptr || calibrated_maximum_mhz == nullptr) {
    return false;
  }

  Lr1121ImageCalibrationBand band;
  if (!SelectLr1121ImageCalibrationBand(frequency_hz, &band)) {
    return false;
  }
  if (frequency_hz >=
          static_cast<uint32_t>(*calibrated_minimum_mhz) * 1000000U &&
      frequency_hz <=
          static_cast<uint32_t>(*calibrated_maximum_mhz) * 1000000U) {
    return true;
  }

  if (lr1121.Invoke(lr11xx_system_calibrate_image_in_mhz, band.minimum_mhz,
          band.maximum_mhz) != LR11XX_STATUS_OK) {
    return false;
  }
  *calibrated_minimum_mhz = band.minimum_mhz;
  *calibrated_maximum_mhz = band.maximum_mhz;
  return true;
}

/**
 * @brief 校验应用层 LoRa 参数并转换为板载射频驱动配置
 * @param source 应用层 LoRa 配置
 * @param target 板载射频驱动配置输出地址
 * @return 参数有效且转换成功时返回 true
 */
bool BuildRadioConfig(const LoraRadioConfig& source,
    usp_cpp_bus_driver::Lr11xx::LoraConfig* target) {
  const bool use_hf_path =
      source.frequency_hz >= 2400000000U && source.frequency_hz <= 2500000000U;
  const bool use_sub_ghz_path =
      source.frequency_hz >= 150000000U && source.frequency_hz <= 960000000U;
  const bool bandwidth_supported =
      use_hf_path
          ? (source.bandwidth_hz == 200000 || source.bandwidth_hz == 400000 ||
                source.bandwidth_hz == 800000)
          : (source.bandwidth_hz == 62500 || source.bandwidth_hz == 125000 ||
                source.bandwidth_hz == 250000 || source.bandwidth_hz == 500000);
  lr11xx_radio_lora_sf_t spreading_factor;
  lr11xx_radio_lora_bw_t bandwidth;
  lr11xx_radio_lora_cr_t coding_rate;
  if (target == nullptr || (!use_hf_path && !use_sub_ghz_path) ||
      !bandwidth_supported || source.preamble_length == 0 ||
      source.output_power_dbm < -9 ||
      source.output_power_dbm > (use_hf_path ? 13 : 22) ||
      !SelectLoraSpreadingFactor(
          source.spreading_factor, &spreading_factor) ||
      !SelectLoraBandwidth(source.bandwidth_hz, &bandwidth) ||
      !SelectLoraCodingRate(source.coding_rate_denominator, &coding_rate)) {
    return false;
  }

  *target = usp_cpp_bus_driver::Lr11xx::LoraConfig{
      .frequency_hz = source.frequency_hz,
      .modulation =
          {
              .sf = spreading_factor,
              .bw = bandwidth,
              .cr = coding_rate,
              .ldro = static_cast<uint8_t>(ShouldEnableLoraLdro(source)),
          },
      .packet = MakeLr1121PacketConfig(source, UINT8_MAX),
      .sync_word = source.sync_word,
      .rx_boosted = source.rx_boosted,
      .pa =
          {
              .pa_sel =
                  use_hf_path ? LR11XX_RADIO_PA_SEL_HF : LR11XX_RADIO_PA_SEL_HP,
              .pa_reg_supply = use_hf_path ? LR11XX_RADIO_PA_REG_SUPPLY_VREG
                                           : LR11XX_RADIO_PA_REG_SUPPLY_VBAT,
              .pa_duty_cycle = static_cast<uint8_t>(use_hf_path ? 0x00 : 0x04),
              .pa_hp_sel = static_cast<uint8_t>(use_hf_path ? 0x00 : 0x07),
          },
      .output_power_dbm = source.output_power_dbm,
      .ramp_time = LR11XX_RADIO_RAMP_48_US,
  };
  return true;
}

struct LoraTransmitTiming {
  // 根据当前调制参数计算的理论空中时间。
  uint32_t time_on_air_ms = 0;
  // 写入 LR1121 SetTx 命令的硬件超时。
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
  lr11xx_radio_lora_sf_t spreading_factor;
  lr11xx_radio_lora_bw_t bandwidth;
  lr11xx_radio_lora_cr_t coding_rate;
  if (!SelectLoraSpreadingFactor(
          config.spreading_factor, &spreading_factor) ||
      !SelectLoraBandwidth(config.bandwidth_hz, &bandwidth) ||
      !SelectLoraCodingRate(config.coding_rate_denominator, &coding_rate)) {
    return false;
  }
  const lr11xx_radio_mod_params_lora_t modulation_params = {
      .sf = spreading_factor,
      .bw = bandwidth,
      .cr = coding_rate,
      .ldro = static_cast<uint8_t>(ShouldEnableLoraLdro(config)),
  };
  const lr11xx_radio_pkt_params_lora_t packet_params = {
      .preamble_len_in_symb = config.preamble_length,
      .header_type = LR11XX_RADIO_LORA_PKT_EXPLICIT,
      .pld_len_in_bytes = static_cast<uint8_t>(payload_size),
      .crc = config.crc_enabled ? LR11XX_RADIO_LORA_CRC_ON
                                : LR11XX_RADIO_LORA_CRC_OFF,
      .iq = config.invert_iq ? LR11XX_RADIO_LORA_IQ_INVERTED
                             : LR11XX_RADIO_LORA_IQ_STANDARD,
  };
  const uint32_t time_on_air_ms = lr11xx_radio_get_lora_time_on_air_in_ms(
      &packet_params, &modulation_params);
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
  timing->hardware_timeout_ms = static_cast<uint32_t>(std::min<uint64_t>(
      requested_timeout_ms, std::numeric_limits<uint32_t>::max()));
  timing->watchdog_timeout_ms = static_cast<uint32_t>(std::min<uint64_t>(
      requested_timeout_ms + kRadioTransmitWatchdogGraceMs, UINT32_MAX));
  return true;
}

}  // namespace

TDisplayP4AirDevice::TDisplayP4AirDevice()
    : driver_(TDisplayP4AirBoardDriver::GetInstance()),
      tool_(std::make_unique<cpp_bus_driver::Tool>()) {
  wifi_.scan_results_mutex = xSemaphoreCreateMutex();
  radio_.mutex = xSemaphoreCreateMutex();
  otg_.mutex = xSemaphoreCreateMutex();
  nrf9151_mutex_ = xSemaphoreCreateMutex();
  imu_.mutex = xSemaphoreCreateMutex();
  nfc_.mutex = xSemaphoreCreateMutex();
  infrared_.mutex = xSemaphoreCreateMutex();
  cellular_.status_mutex = xSemaphoreCreateMutex();
}

bool TDisplayP4AirDevice::InitializeTouchInterrupt() {
  if (touch_interrupt_initialized_) {
    return true;
  }
  if (tool_ == nullptr || !IsTouchReady(driver_)) {
    return false;
  }

  touch_interrupt_pending_.store(false, std::memory_order_relaxed);
  if (!tool_->InitGpioInterrupt(gpio::hi8561::kTouchInt,
          cpp_bus_driver::Tool::InterruptMode::kFalling,
          TouchInterruptHandler, this)) {
    return false;
  }

  touch_interrupt_initialized_ = true;
  if (!tool_->GpioRead(gpio::hi8561::kTouchInt)) {
    touch_interrupt_pending_.store(true, std::memory_order_relaxed);
  }
  return true;
}

void TDisplayP4AirDevice::TouchInterruptHandler(void* context) {
  if (context == nullptr) {
    return;
  }
  auto* device = static_cast<TDisplayP4AirDevice*>(context);
  device->touch_interrupt_pending_.store(true, std::memory_order_relaxed);
}

bool TDisplayP4AirDevice::InitializePowerButton() {
  if (power_button_initialized_) {
    return true;
  }
  if (tool_ == nullptr) {
    return false;
  }

  power_button_initialized_ = tool_->SetGpioMode(gpio::button::kPower,
      cpp_bus_driver::Tool::GpioMode::kInput,
      cpp_bus_driver::Tool::GpioStatus::kPullup);
  return power_button_initialized_;
}

bool TDisplayP4AirDevice::InitializeVolumeButtons() {
  if (volume_buttons_initialized_) {
    return true;
  }
  if (tool_ == nullptr) {
    return false;
  }

  volume_buttons_initialized_ =
      tool_->SetGpioMode(gpio::button::kEsp32p4Boot,
          cpp_bus_driver::Tool::GpioMode::kInput,
          cpp_bus_driver::Tool::GpioStatus::kPullup) &&
      tool_->SetGpioMode(gpio::button::kKey1,
          cpp_bus_driver::Tool::GpioMode::kInput,
          cpp_bus_driver::Tool::GpioStatus::kPullup);
  return volume_buttons_initialized_;
}

bool TDisplayP4AirDevice::IsPowerButtonHeldForStartup() {
  if (!InitializePowerButton()) {
    return false;
  }

  bool pressed = false;
  if (!ReadPowerButtonPressed(&pressed) || !pressed) {
    return false;
  }

  uint32_t held_ms = 0;
  while (held_ms < kPowerOffStartupHoldMs) {
    vTaskDelay(pdMS_TO_TICKS(kPowerOffButtonPollMs));
    held_ms += kPowerOffButtonPollMs;
    if (!ReadPowerButtonPressed(&pressed) || !pressed) {
      return false;
    }
  }
  return true;
}

void TDisplayP4AirDevice::WaitForPowerButtonRelease() {
  if (!InitializePowerButton()) {
    return;
  }

  bool pressed = false;
  for (uint32_t waited_ms = 0;
       waited_ms < kPowerOffButtonReleaseTimeoutMs;
       waited_ms += kPowerOffButtonPollMs) {
    if (!ReadPowerButtonPressed(&pressed) || !pressed) {
      return;
    }
    vTaskDelay(pdMS_TO_TICKS(kPowerOffButtonPollMs));
  }
  LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
      "Power button remained pressed before power-off deep sleep\n");
}

bool TDisplayP4AirDevice::ConfigurePowerOffWakeSources() {
  const esp_err_t disable_result =
      esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
  if (disable_result != ESP_OK) {
    LogMessage(LogLevel::kError, __FILE__, __LINE__,
        "Disable previous sleep wake sources failed: %s (%#X)\n",
        esp_err_to_name(disable_result),
        static_cast<unsigned>(disable_result));
    return false;
  }

  const esp_err_t timer_result =
      esp_sleep_enable_timer_wakeup(kPowerOffMonitorWakeIntervalUs);
  if (timer_result != ESP_OK) {
    LogMessage(LogLevel::kError, __FILE__, __LINE__,
        "Enable power-off timer wakeup failed: %s (%#X)\n",
        esp_err_to_name(timer_result), static_cast<unsigned>(timer_result));
    return false;
  }

  const uint64_t power_button_mask =
      uint64_t{1} << static_cast<unsigned>(gpio::button::kPower);
  const esp_err_t gpio_result = esp_deep_sleep_enable_gpio_wakeup(
      power_button_mask, ESP_GPIO_WAKEUP_GPIO_LOW);
  if (gpio_result != ESP_OK) {
    LogMessage(LogLevel::kError, __FILE__, __LINE__,
        "Enable power button deep-sleep wakeup failed: %s (%#X)\n",
        esp_err_to_name(gpio_result), static_cast<unsigned>(gpio_result));
    return false;
  }
  return true;
}

PowerOffBootAction TDisplayP4AirDevice::PreparePowerOffDeepSleep() {
  WaitForPowerButtonRelease();
  if (!ConfigurePowerOffWakeSources()) {
    return PowerOffBootAction::kFailed;
  }
  if (!driver_.PrepareMinimalDriversForPowerOff()) {
    LogMessage(LogLevel::kError, __FILE__, __LINE__,
        "Prepare minimal power management path for deep sleep failed\n");
  }
  return PowerOffBootAction::kEnterDeepSleep;
}

PowerOffBootAction TDisplayP4AirDevice::PreparePowerOffShippingMode() {
  WaitForPowerButtonRelease();
  if (!ConfigurePowerOffWakeSources()) {
    return PowerOffBootAction::kFailed;
  }
  const bool shipping_mode_enabled =
      driver_.IsAxp517Ready() && driver_.chip().axp517 != nullptr &&
      driver_.chip().axp517->SetShippingModeEnable(true);
  const bool sleep_prepared = driver_.PrepareMinimalDriversForPowerOff();
  if (!shipping_mode_enabled) {
    LogMessage(LogLevel::kError, __FILE__, __LINE__,
        "Enter AXP517 shipping mode failed; retrying after timer wakeup\n");
  }
  if (!sleep_prepared) {
    LogMessage(LogLevel::kError, __FILE__, __LINE__,
        "Prepare AXP517 shipping-mode path failed\n");
  }
  return PowerOffBootAction::kEnterDeepSleep;
}

bool TDisplayP4AirDevice::InitDevice() {
  if (wifi_.scan_results_mutex == nullptr || radio_.mutex == nullptr ||
      otg_.mutex == nullptr ||
      nrf9151_mutex_ == nullptr || imu_.mutex == nullptr ||
      nfc_.mutex == nullptr || infrared_.mutex == nullptr ||
      cellular_.status_mutex == nullptr) {
    LogMessage(LogLevel::kError, __FILE__, __LINE__,
        "Create T-Display-P4-Air synchronization resources failed\n");
    return false;
  }

  const bool result = driver_.Init(TDisplayP4AirBoardDriver::InitMode::kAsync);
  otg_.source_role_enabled = false;
  otg_.power_output_enabled = false;
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
  if (!InitializePowerButton()) {
    LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
        "Initialize power button failed\n");
  }
  if (!InitializeVolumeButtons()) {
    LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
        "Initialize volume buttons failed\n");
  }
  if (!InitializeTouchInterrupt()) {
    LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
        "Initialize touch interrupt failed; using polling fallback\n");
  }
  return true;
}

bool TDisplayP4AirDevice::ReadPowerButtonPressed(bool* pressed) {
  if (pressed == nullptr || !power_button_initialized_ || tool_ == nullptr) {
    return false;
  }

  // 电源键使用上拉输入，按下时把 GPIO 拉低。
  *pressed = !tool_->GpioRead(gpio::button::kPower);
  return true;
}

bool TDisplayP4AirDevice::ReadVolumeUpButtonPressed(bool* pressed) {
  if (pressed == nullptr || !volume_buttons_initialized_ || tool_ == nullptr) {
    return false;
  }

  // BOOT 音量加按键使用上拉输入，按下时把 GPIO 拉低。
  *pressed = !tool_->GpioRead(gpio::button::kEsp32p4Boot);
  return true;
}

bool TDisplayP4AirDevice::ReadVolumeDownButtonPressed(bool* pressed) {
  if (pressed == nullptr || !volume_buttons_initialized_ || tool_ == nullptr) {
    return false;
  }

  // KEY1 音量减按键使用上拉输入，按下时把 GPIO 拉低。
  *pressed = !tool_->GpioRead(gpio::button::kKey1);
  return true;
}

PowerOffBootAction TDisplayP4AirDevice::ResolvePowerOffBoot(
    bool power_off_requested) {
  if (!driver_.InitMinimal() || !InitializePowerButton() ||
      !driver_.IsAxp517Ready() || driver_.chip().axp517 == nullptr) {
    LogMessage(LogLevel::kError, __FILE__, __LINE__,
        "Initialize AXP517 power-off boot path failed\n");
    return PowerOffBootAction::kFailed;
  }

  auto& axp517 = *driver_.chip().axp517;
  cpp_bus_driver::Axp517::ChipStatus0 chip_status0;
  if (!axp517.GetChipStatus0(chip_status0)) {
    LogMessage(LogLevel::kError, __FILE__, __LINE__,
        "Read AXP517 VBUS status during power-off boot failed\n");
    driver_.PrepareMinimalDriversForPowerOff();
    return PowerOffBootAction::kFailed;
  }

  cpp_bus_driver::Axp517::IrqStatus0 irq_status0;
  cpp_bus_driver::Axp517::IrqStatus1 irq_status1;
  cpp_bus_driver::Axp517::IrqStatus2 irq_status2;
  cpp_bus_driver::Axp517::IrqStatus3 irq_status3;
  const bool irq_ready = axp517.GetIrqStatus(
      irq_status0, irq_status1, irq_status2, irq_status3);
  const bool axp_long_press =
      irq_ready && irq_status1.pwr_on_long_press_flag;
  const bool axp_short_press =
      irq_ready && irq_status1.pwr_on_short_press_flag;
  const bool vbus_inserted = irq_ready && irq_status1.vbus_insert_flag;
  if (!axp517.ClearAllIrq()) {
    LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
        "Clear AXP517 power-off boot IRQ flags failed\n");
  }

  const bool external_power_present = chip_status0.vbus_good_indication;
  const esp_sleep_wakeup_cause_t wakeup_cause = esp_sleep_get_wakeup_cause();
  const uint64_t power_button_mask =
      uint64_t{1} << static_cast<unsigned>(gpio::button::kPower);
  const bool power_button_wakeup =
      wakeup_cause == ESP_SLEEP_WAKEUP_GPIO &&
      (esp_sleep_get_gpio_wakeup_status() & power_button_mask) != 0;
  const esp_reset_reason_t reset_reason = esp_reset_reason();
  const bool usb_power_on = !power_off_requested && external_power_present &&
      reset_reason == ESP_RST_POWERON && vbus_inserted;

  LogMessage(LogLevel::kInfo, __FILE__, __LINE__,
      "Resolve Air power-off boot: requested=%d, reset=%d, wakeup=%d, "
      "vbus=%d, button=%d, usb_insert=%d\n",
      power_off_requested ? 1 : 0, static_cast<int>(reset_reason),
      static_cast<int>(wakeup_cause), external_power_present ? 1 : 0,
      power_button_wakeup ? 1 : 0, vbus_inserted ? 1 : 0);

  if (!power_off_requested && !usb_power_on) {
    g_power_off_rtc_magic = 0;
    g_power_off_charging_screen_pending = false;
    return PowerOffBootAction::kContinueStartup;
  }

  // 定时器与按键可能同时触发，开机长按应在所有唤醒原因之前取得优先级。
  if (axp_long_press || IsPowerButtonHeldForStartup()) {
    g_power_off_rtc_magic = 0;
    g_power_off_charging_screen_pending = false;
    LogMessage(LogLevel::kInfo, __FILE__, __LINE__,
        "Long power-button press accepted; continuing normal startup\n");
    return PowerOffBootAction::kContinueStartup;
  }

  if (!external_power_present) {
    g_power_off_rtc_magic = kPowerOffRtcMagic;
    g_power_off_charging_screen_pending = true;
    return PreparePowerOffShippingMode();
  }

  if (usb_power_on || power_button_wakeup || axp_short_press) {
    g_power_off_rtc_magic = kPowerOffRtcMagic;
    g_power_off_charging_screen_pending = false;
    return PowerOffBootAction::kShowChargingScreen;
  }

  if (wakeup_cause == ESP_SLEEP_WAKEUP_TIMER) {
    if (g_power_off_rtc_magic == kPowerOffRtcMagic &&
        g_power_off_charging_screen_pending) {
      g_power_off_charging_screen_pending = false;
      return PowerOffBootAction::kShowChargingScreen;
    }
    return PreparePowerOffDeepSleep();
  }

  g_power_off_rtc_magic = kPowerOffRtcMagic;
  g_power_off_charging_screen_pending = false;
  return PowerOffBootAction::kShowChargingScreen;
}

PowerOffAction TDisplayP4AirDevice::RequestPowerOff() {
  return RequestPowerOffInternal(true);
}

PowerOffAction TDisplayP4AirDevice::RequestPowerOffFromChargingScreen() {
  return RequestPowerOffInternal(false);
}

PowerOffAction TDisplayP4AirDevice::RequestPowerOffInternal(
    bool prepare_device_services) {
  if (!driver_.IsAxp517Ready() || driver_.chip().axp517 == nullptr) {
    LogMessage(LogLevel::kError, __FILE__, __LINE__,
        "Power off through AXP517 failed: device unavailable\n");
    return PowerOffAction::kFailed;
  }

  auto& axp517 = *driver_.chip().axp517;
  cpp_bus_driver::Axp517::ChipStatus0 chip_status0;
  if (!axp517.GetChipStatus0(chip_status0)) {
    LogMessage(LogLevel::kError, __FILE__, __LINE__,
        "Read AXP517 VBUS status before power off failed\n");
    return PowerOffAction::kFailed;
  }
  bool external_power_present = chip_status0.vbus_good_indication;

  // 先准备两种唤醒源，避免 USB 在外设关闭期间插入时落入运输模式等待死区。
  WaitForPowerButtonRelease();
  if (!ConfigurePowerOffWakeSources()) {
    return PowerOffAction::kFailed;
  }
  if (!axp517.ClearAllIrq()) {
    LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
        "Clear AXP517 IRQ flags before power off failed\n");
  }

  if (prepare_device_services && !PrepareForPowerOff()) {
    LogMessage(LogLevel::kError, __FILE__, __LINE__,
        "Prepare Air device for power off failed\n");
    // 不再访问可能仍由应用任务占用的硬件，定时唤醒后重新处理关机状态。
    g_power_off_rtc_magic = kPowerOffRtcMagic;
    g_power_off_charging_screen_pending = true;
    return PowerOffAction::kEnterDeepSleep;
  }

  cpp_bus_driver::Axp517::ChipStatus0 pre_hardware_shutdown_status0;
  if (axp517.GetChipStatus0(pre_hardware_shutdown_status0)) {
    external_power_present =
        pre_hardware_shutdown_status0.vbus_good_indication;
  } else {
    LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
        "Re-read AXP517 VBUS status before hardware shutdown failed; "
        "using previous state\n");
  }

  if (prepare_device_services && external_power_present) {
    g_power_off_rtc_magic = kPowerOffRtcMagic;
    g_power_off_charging_screen_pending = false;
    return PowerOffAction::kShowChargingScreen;
  }

  if (!driver_.PrepareDriversForPowerOff()) {
    LogMessage(LogLevel::kError, __FILE__, __LINE__,
        "Prepare Air board hardware for power off failed\n");
    // 不再访问可能仍由初始化任务占用的硬件，定时唤醒后重新处理关机状态。
    g_power_off_rtc_magic = kPowerOffRtcMagic;
    g_power_off_charging_screen_pending = true;
    return PowerOffAction::kEnterDeepSleep;
  }

  cpp_bus_driver::Axp517::ChipStatus0 final_chip_status0;
  if (axp517.GetChipStatus0(final_chip_status0)) {
    external_power_present = final_chip_status0.vbus_good_indication;
  } else {
    LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
        "Re-read AXP517 VBUS status after hardware shutdown failed; "
        "using previous state\n");
  }

  if (external_power_present) {
    if (g_power_off_rtc_magic != kPowerOffRtcMagic) {
      g_power_off_rtc_magic = kPowerOffRtcMagic;
      g_power_off_charging_screen_pending = true;
    }
    LogMessage(LogLevel::kInfo, __FILE__, __LINE__,
        "External power present; entering power-off charging deep sleep\n");
    if (!driver_.PrepareMinimalDriversForPowerOff()) {
      LogMessage(LogLevel::kError, __FILE__, __LINE__,
          "Prepare power management path for deep sleep failed\n");
    }
    return PowerOffAction::kEnterDeepSleep;
  }

  // 若运输模式执行瞬间 USB 插入导致未断电，下次定时唤醒应显示充电页。
  g_power_off_rtc_magic = kPowerOffRtcMagic;
  g_power_off_charging_screen_pending = true;
  LogMessage(LogLevel::kInfo, __FILE__, __LINE__,
      "Battery-only power off; entering AXP517 shipping mode\n");
  const bool shipping_mode_enabled = axp517.SetShippingModeEnable(true);
  const bool sleep_prepared = driver_.PrepareMinimalDriversForPowerOff();
  if (!shipping_mode_enabled) {
    LogMessage(LogLevel::kError, __FILE__, __LINE__,
        "Enter AXP517 shipping mode failed; retrying after timer wakeup\n");
  }
  if (!sleep_prepared) {
    LogMessage(LogLevel::kError, __FILE__, __LINE__,
        "Prepare power management path after shipping-mode request failed\n");
  }
  // 正常情况下 BATFET 会立即断开；深度睡眠是 USB 同时插入时的安全后备。
  return PowerOffAction::kEnterDeepSleep;
}

int TDisplayP4AirDevice::ScreenWidth() const {
  return driver_.screen_info().width;
}

int TDisplayP4AirDevice::ScreenHeight() const {
  return driver_.screen_info().height;
}

int TDisplayP4AirDevice::ScreenBitsPerPixel() const {
  return driver_.screen_info().bits_per_pixel;
}

bool TDisplayP4AirDevice::ReadDeviceInfo(DeviceInfo* info) {
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
  info->battery_fuel_gauge_name = "AXP517";
  // 原理图未固定电池容量，运行时仅报告 PMIC 电量计数据。
  info->battery_capacity_mah = 0;
  return true;
}

bool TDisplayP4AirDevice::SetWifiEnabled(bool enabled) {
  if (!enabled) {
    wifi_time_test_.requested.store(false);
    wifi_.connect_cancel_requested.store(true);
    wifi_.stop_requested.store(true);
    wifi_.scan_requested.store(false);
    if (wifi_time_test_.active.load()) {
      StopWifiTimeTest();
    } else {
      StopWifiInternetCheck();
    }

    if (!wifi_.driver_initialized.load()) {
      if (wifi_.init_task_running.load()) {
        const bool power_disabled =
            SetWifiCoprocessorPowerEnabled(driver_, false);
        LogMessage(power_disabled ? LogLevel::kDebug : LogLevel::kError,
            __FILE__, __LINE__,
            power_disabled
                ? "WiFi power disabled while initialization is stopping\n"
                : "Disable WiFi power during initialization failed\n");
        return power_disabled;
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
      return SetWifiCoprocessorPowerEnabled(driver_, false);
    }

    if (wifi_.scan_running.load() || wifi_.scan_task_running.load()) {
      const esp_err_t scan_result = esp_wifi_scan_stop();
      if (scan_result != ESP_OK && scan_result != ESP_ERR_WIFI_NOT_STARTED &&
          scan_result != ESP_ERR_INVALID_STATE &&
          scan_result != ESP_ERR_WIFI_STATE) {
        SetWifiFailure(scan_result);
        SetWifiCoprocessorPowerEnabled(driver_, false);
        return false;
      }
    }
    esp_wifi_disconnect();
    wifi_config_t empty_config = {};
    esp_wifi_set_config(WIFI_IF_STA, &empty_config);
    esp_err_t result = esp_wifi_stop();
    if (result != ESP_OK && result != ESP_ERR_WIFI_NOT_STARTED) {
      SetWifiFailure(result);
      SetWifiCoprocessorPowerEnabled(driver_, false);
      return false;
    }

    result = esp_wifi_set_mode(WIFI_MODE_NULL);
    if (result != ESP_OK) {
      SetWifiFailure(result);
      SetWifiCoprocessorPowerEnabled(driver_, false);
      return false;
    }

    esp_event_handler_unregister(
        WIFI_EVENT, ESP_EVENT_ANY_ID, WifiEventHandler);
    esp_event_handler_unregister(
        IP_EVENT, IP_EVENT_STA_GOT_IP, WifiGotIpEventHandler);
    result = esp_wifi_deinit();
    if (result != ESP_OK && result != ESP_ERR_WIFI_NOT_INIT) {
      SetWifiFailure(result);
      SetWifiCoprocessorPowerEnabled(driver_, false);
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
        SetWifiCoprocessorPowerEnabled(driver_, false);
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
    return SetWifiCoprocessorPowerEnabled(driver_, false);
  }

  wifi_.stop_requested.store(false);
  if (wifi_.driver_initialized.load() && wifi_.running.load()) {
    return true;
  }

  if (!SetWifiCoprocessorPowerEnabled(driver_, true)) {
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
    wifi_.init_task_running.store(false);
    SetWifiFailure(ESP_ERR_NO_MEM);
    SetWifiCoprocessorPowerEnabled(driver_, false);
    return false;
  }
  return true;
}

bool TDisplayP4AirDevice::StartWifiScan() {
  wifi_.stop_requested.store(false);
  if (!wifi_.driver_initialized.load()) {
    wifi_.scan_requested.store(true);
    wifi_.scan_running.store(true);
    if (SetWifiEnabled(true)) {
      return true;
    }
    wifi_.scan_requested.store(false);
    wifi_.scan_running.store(false);
    wifi_.scan_failed.store(true);
    return false;
  }

  wifi_.scan_requested.store(false);
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

bool TDisplayP4AirDevice::ReadWifiScanStatus(WifiScanStatus* status) {
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

bool TDisplayP4AirDevice::ConnectWifi(const char* ssid, const char* password) {
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

bool TDisplayP4AirDevice::CancelWifiConnection() {
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

bool TDisplayP4AirDevice::RequestWifiInternetCheck() {
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

void TDisplayP4AirDevice::StopWifiInternetCheck() {
  wifi_time_test_.sync_started.store(false);
  wifi_time_test_.sntp_attempt_count.store(0);
  if (wifi_time_test_.sntp_attempt_timer != nullptr &&
      esp_timer_is_active(wifi_time_test_.sntp_attempt_timer)) {
    esp_timer_stop(wifi_time_test_.sntp_attempt_timer);
  }
  esp_sntp_set_time_sync_notification_cb(nullptr);
  TDisplayP4AirDevice* owner = this;
  g_wifi_time_sync_owner.compare_exchange_strong(owner, nullptr);
  if (esp_sntp_enabled()) {
    esp_sntp_stop();
  }
}

bool TDisplayP4AirDevice::StartWifiTimeTest() {
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

bool TDisplayP4AirDevice::StopWifiTimeTest() {
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

bool TDisplayP4AirDevice::ReadWifiStatus(WifiStatus* status) {
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

bool TDisplayP4AirDevice::EnsureSdCardMounted() {
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

bool TDisplayP4AirDevice::UnmountSdCard() { return driver_.DeinitSdmmc(); }

bool TDisplayP4AirDevice::IsSdCardMounted() const {
  if (!driver_.IsSdmmcReady()) {
    return false;
  }
  struct stat info = {};
  return stat(device::sd::kBasePath, &info) == 0 && S_ISDIR(info.st_mode);
}

const char* TDisplayP4AirDevice::SdCardBasePath() const {
  return device::sd::kBasePath;
}

bool TDisplayP4AirDevice::StartUsbStorage() {
  if (!driver_.SetUsbHostPowerEnabled(true)) {
    return false;
  }
  return usb_storage_manager_.Start();
}

bool TDisplayP4AirDevice::StopUsbStorage() {
  // USB PHY 供电保持开启可避免 ESP32-P4 产生约 20 mA 的额外功耗。
  return usb_storage_manager_.Stop();
}

bool TDisplayP4AirDevice::ReadUsbStorageSnapshot(
    UsbStorageSnapshot* snapshot) const {
  return usb_storage_manager_.ReadSnapshot(snapshot);
}

bool TDisplayP4AirDevice::RegisterScreenDisplayCallbacks(
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

bool TDisplayP4AirDevice::WriteScreenPixels(
    int x_start, int y_start, int x_end, int y_end, const void* pixels) {
  if (!driver_.IsScreenReady()) {
    return false;
  }

  switch (driver_.screen_type()) {
    case device::ScreenType::kHi8561:
      return driver_.chip().hi8561->SendColorStreamCoordinate(
          x_start, y_start, x_end, y_end, pixels);
    default:
      break;
  }
  return false;
}

bool TDisplayP4AirDevice::ReadScreenTouch(TouchPoint* point) {
  if (point == nullptr) {
    return false;
  }
  *point = TouchPoint();

  if (!IsTouchReady(driver_)) {
    return false;
  }

  // 亮屏轮询会顺带消费通知，避免休眠时误把旧中断当成新的手势。
  ConsumeTouchInterrupt();

  cpp_bus_driver::TouchFrame frame;
  const cpp_bus_driver::TouchReadStatus read_status =
      driver_.chip().hi8561_touch->ReadPrimaryTouch(&frame);
  if (frame.gesture == static_cast<uint8_t>(
          cpp_bus_driver::Hi8561Touch::Gesture::kDoubleTap)) {
    point->x = -1;
    point->y = -1;
    point->gesture = TouchGesture::kDoubleTap;
    return true;
  }
  if (read_status != cpp_bus_driver::TouchReadStatus::kSuccess) {
    return false;
  }
  if (frame.contact_count == 0) {
    if (!frame.edge_touch) {
      return false;
    }
    SetEdgeTouchPoint(point);
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

bool TDisplayP4AirDevice::ReadScreenTouchPoints(
    TouchPoint* points, size_t max_points, size_t* point_count) {
  if (point_count != nullptr) {
    *point_count = 0;
  }
  if (points == nullptr || max_points == 0 || point_count == nullptr) {
    return false;
  }

  if (!IsTouchReady(driver_)) {
    return false;
  }

  // 亮屏轮询会顺带消费通知，避免休眠时误把旧中断当成新的手势。
  ConsumeTouchInterrupt();

  cpp_bus_driver::TouchFrame frame;
  const cpp_bus_driver::TouchReadStatus read_status =
      driver_.chip().hi8561_touch->ReadTouchFrame(&frame);
  if (read_status != cpp_bus_driver::TouchReadStatus::kSuccess) {
    return false;
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
  if (*point_count == 0 && frame.edge_touch) {
    SetEdgeTouchPoint(&points[0]);
    *point_count = 1;
  }
  return *point_count > 0;
}

bool TDisplayP4AirDevice::SupportsTouchInterrupt() const {
  return touch_interrupt_initialized_;
}

bool TDisplayP4AirDevice::ConsumeTouchInterrupt() {
  return touch_interrupt_initialized_ &&
         touch_interrupt_pending_.exchange(false, std::memory_order_relaxed);
}

bool TDisplayP4AirDevice::ReadHapticWaveformCount(uint8_t* waveform_count) {
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

bool TDisplayP4AirDevice::PlayHapticWaveform(uint8_t waveform_sequence_number,
    uint8_t loop_count, uint8_t gain, bool auto_brake) {
  haptic_.waveform_sequence_number.store(waveform_sequence_number);
  haptic_.loop_count.store(std::clamp<uint8_t>(loop_count, 1, 16));
  haptic_.gain.store(gain);
  haptic_.auto_brake.store(auto_brake);

  const uint32_t now_ms =
      static_cast<uint32_t>(xTaskGetTickCount() * portTICK_PERIOD_MS);
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

  const BaseType_t result = xTaskCreate(HapticPlaybackTaskEntry, "haptic_play",
      kSpeakerPlaybackTaskStackBytes, this, kSpeakerPlaybackTaskPriority,
      nullptr);
  if (result != pdPASS) {
    haptic_.running.store(false);
    return false;
  }
  return true;
}

bool TDisplayP4AirDevice::PlaySpeakerTone(size_t* bytes_written) {
  if (bytes_written != nullptr) {
    *bytes_written = 0;
  }

  if (!Configure(kSpeakerPlaybackSampleRateHz, kSpeakerPlaybackChannelCount,
          kSpeakerPlaybackBitsPerSample)) {
    LogMessage(
        LogLevel::kWarning, __FILE__, __LINE__, "Audio codec is unavailable\n");
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
      "Speaker playback: bytes=%u, sample_rate=%u, channels=%u, "
      "duration=%u ms\n",
      static_cast<unsigned int>(audio_size),
      static_cast<unsigned int>(kSpeakerPlaybackSampleRateHz),
      static_cast<unsigned int>(kSpeakerPlaybackChannelCount),
      static_cast<unsigned int>(duration_ms));

  size_t total_written = 0;
  while (total_written < audio_size) {
    const size_t write_size =
        std::min(kSpeakerPlaybackChunkBytes, audio_size - total_written);
    const int write_result =
        esp_codec_dev_write(driver_.es8389_output_codec_dev(),
            const_cast<uint8_t*>(audio_data + total_written),
            static_cast<int>(write_size));
    const size_t written = write_result == ESP_CODEC_DEV_OK ? write_size : 0;
    if (written == 0) {
      LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
          "Audio PCM write failed, written=%u/%u\n",
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

bool TDisplayP4AirDevice::StartSpeakerTone() {
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

bool TDisplayP4AirDevice::StartSpeakerToneLoop() {
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

bool TDisplayP4AirDevice::StopSpeakerToneLoop() {
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

bool TDisplayP4AirDevice::SetSpeakerVolumePercent(int percent) {
  const int clamped_percent = std::clamp(percent, 0, 100);
  if (speaker_.volume_percent.load() == clamped_percent) {
    return true;
  }

  const bool audio_active =
      speaker_.running.load() || microphone_.running.load();
  if (!audio_active) {
    // 音频未运行时只缓存音量，下一次启用编解码器时会自动恢复。
    speaker_.volume_percent.store(clamped_percent);
    return true;
  }

  if (!driver_.IsEs8389Ready() && !driver_.InitEs8389()) {
    LogMessage(LogLevel::kWarning, __FILE__, __LINE__, "ES8389 init failed\n");
    return false;
  }
  if (!driver_.SetEs8389OperatingMode(
          TDisplayP4AirBoardDriver::Es8389OperatingMode::kActive)) {
    return false;
  }
  const bool result =
      esp_codec_dev_set_out_vol(driver_.es8389_output_codec_dev(),
          clamped_percent) == ESP_CODEC_DEV_OK;
  if (result) {
    speaker_.volume_percent.store(clamped_percent);
  }
  return result;
}

bool TDisplayP4AirDevice::ReadSpeakerToneStatus(SpeakerStatus* status) {
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

bool TDisplayP4AirDevice::StartAudioFile(
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

bool TDisplayP4AirDevice::PauseAudioFile() {
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

bool TDisplayP4AirDevice::ResumeAudioFile() {
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

bool TDisplayP4AirDevice::SeekAudioFile(uint32_t position_ms) {
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

bool TDisplayP4AirDevice::StopAudioFile() {
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

bool TDisplayP4AirDevice::ReadAudioFileStatus(AudioFilePlaybackStatus* status) {
  if (status == nullptr) {
    return false;
  }
  status->state = speaker_.file_state.load();
  status->elapsed_ms = speaker_.elapsed_ms.load();
  status->duration_ms = speaker_.duration_ms.load();
  return true;
}

void TDisplayP4AirDevice::SpeakerPlaybackTaskEntry(void* context) {
  auto* self = static_cast<TDisplayP4AirDevice*>(context);
  if (self != nullptr) {
    self->RunSpeakerPlaybackTask();
  }
  vTaskDelete(nullptr);
}

void TDisplayP4AirDevice::PausedAudioSpeakerToneTaskEntry(void* context) {
  auto* self = static_cast<TDisplayP4AirDevice*>(context);
  if (self != nullptr) {
    self->RunPausedAudioSpeakerToneTask();
  }
  vTaskDelete(nullptr);
}

void TDisplayP4AirDevice::RunSpeakerPlaybackTask() {
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

bool TDisplayP4AirDevice::StartPausedAudioSpeakerTone(bool loop_enabled) {
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

void TDisplayP4AirDevice::RunPausedAudioSpeakerToneTask() {
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

bool TDisplayP4AirDevice::TryAcquireAuxiliaryAudioOutput(
    AuxiliaryAudioOutput output) {
  AuxiliaryAudioOutput expected = AuxiliaryAudioOutput::kNone;
  return output != AuxiliaryAudioOutput::kNone &&
         speaker_.auxiliary_output.compare_exchange_strong(expected, output);
}

void TDisplayP4AirDevice::ReleaseAuxiliaryAudioOutput(
    AuxiliaryAudioOutput output) {
  speaker_.auxiliary_output.compare_exchange_strong(
      output, AuxiliaryAudioOutput::kNone);
}

bool TDisplayP4AirDevice::WaitForPausedAudioFile() {
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

bool TDisplayP4AirDevice::UpdateAudioCodecOperatingMode() {
  const bool audio_active =
      speaker_.running.load() || microphone_.running.load();
  const auto mode =
      audio_active ? TDisplayP4AirBoardDriver::Es8389OperatingMode::kActive
                   : TDisplayP4AirBoardDriver::Es8389OperatingMode::kSleep;
  if (!driver_.SetEs8389OperatingMode(mode)) {
    return false;
  }
  return !audio_active ||
         esp_codec_dev_set_out_vol(driver_.es8389_output_codec_dev(),
             speaker_.volume_percent.load()) == ESP_CODEC_DEV_OK;
}

bool TDisplayP4AirDevice::Configure(
    uint32_t sample_rate_hz, uint8_t channel_count, uint8_t bits_per_sample) {
  if ((channel_count != 1 && channel_count != 2) ||
      bits_per_sample != kSpeakerPlaybackBitsPerSample) {
    return false;
  }
  if (!driver_.IsEs8389Ready() && !driver_.InitEs8389()) {
    return false;
  }
  if (!UpdateAudioCodecOperatingMode()) {
    return false;
  }
  esp_codec_dev_handle_t output = driver_.es8389_output_codec_dev();
  if (output == nullptr) {
    return false;
  }
  if (speaker_.sample_rate_hz.load() == sample_rate_hz) {
    return true;
  }

  esp_codec_dev_sample_info_t sample_info = {
      .bits_per_sample = bits_per_sample,
      .channel = channel_count,
      .channel_mask = static_cast<uint16_t>(
          channel_count == 1 ? ESP_CODEC_DEV_MAKE_CHANNEL_MASK(0)
                             : ESP_CODEC_DEV_MAKE_CHANNEL_MASK(0) |
                                   ESP_CODEC_DEV_MAKE_CHANNEL_MASK(1)),
      .sample_rate = sample_rate_hz,
      .mclk_multiple = device::es8389::kMclkMultiple,
  };
  if (esp_codec_dev_close(output) != ESP_CODEC_DEV_OK ||
      esp_codec_dev_open(output, &sample_info) != ESP_CODEC_DEV_OK) {
    return false;
  }
  speaker_.sample_rate_hz.store(sample_rate_hz);
  return true;
}

bool TDisplayP4AirDevice::WaitUntilReady() {
  if (speaker_.paused.load() && !speaker_.stop_requested.load()) {
    speaker_.pause_acknowledged.store(true);
  }
  while (speaker_.paused.load() && !speaker_.stop_requested.load()) {
    vTaskDelay(pdMS_TO_TICKS(20));
  }
  speaker_.pause_acknowledged.store(false);
  return !speaker_.stop_requested.load();
}

bool TDisplayP4AirDevice::TakeSeekRequest(uint32_t* position_ms) {
  if (position_ms == nullptr || !speaker_.seek_requested.exchange(false)) {
    return false;
  }
  *position_ms = speaker_.seek_position_ms.load();
  return true;
}

bool TDisplayP4AirDevice::Write(const uint8_t* data, size_t size) {
  if (data == nullptr || size == 0) {
    return false;
  }
  if (!driver_.IsEs8389Ready() ||
      driver_.es8389_output_codec_dev() == nullptr) {
    return false;
  }
  size_t total_written = 0;
  while (total_written < size) {
    if (!WaitUntilReady()) {
      return false;
    }
    const size_t write_size =
        std::min(kSpeakerPlaybackChunkBytes, size - total_written);
    const int write_result =
        esp_codec_dev_write(driver_.es8389_output_codec_dev(),
            const_cast<uint8_t*>(data + total_written),
            static_cast<int>(write_size));
    const size_t written = write_result == ESP_CODEC_DEV_OK ? write_size : 0;
    if (written == 0) {
      return false;
    }
    total_written += written;
  }
  return true;
}

void TDisplayP4AirDevice::UpdateProgress(uint32_t elapsed_ms) {
  const uint32_t duration_ms = speaker_.duration_ms.load();
  speaker_.elapsed_ms.store(
      duration_ms == 0 ? elapsed_ms : std::min(elapsed_ms, duration_ms));
}

void TDisplayP4AirDevice::HapticPlaybackTaskEntry(void* context) {
  auto* self = static_cast<TDisplayP4AirDevice*>(context);
  if (self != nullptr) {
    self->RunHapticPlaybackTask();
  }
  vTaskDelete(nullptr);
}

void TDisplayP4AirDevice::RunHapticPlaybackTask() {
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

bool TDisplayP4AirDevice::StartMicrophone() {
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
        "Failed to activate the microphone capture path\n");
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

bool TDisplayP4AirDevice::StopMicrophone() {
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

bool TDisplayP4AirDevice::SetAudioAdcToDac(bool enable) {
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
  const bool codec_ready =
      driver_.IsEs8389Ready() || driver_.InitEs8389();
  if (!UpdateAudioCodecOperatingMode() || !codec_ready) {
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

bool TDisplayP4AirDevice::ReadMicrophoneStatus(MicrophoneStatus* status) {
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

void TDisplayP4AirDevice::HeapCapsBufferDeleter::operator()(
    uint8_t* pointer) const {
  if (pointer != nullptr) {
    heap_caps_free(pointer);
  }
}

bool TDisplayP4AirDevice::StartCameraPreview() {
  if (camera_preview_.task_active.load() || camera_preview_.running.load() ||
      camera_preview_.initialized.load()) {
    return !camera_preview_.stop_requested.load();
  }

  camera_preview_.error.store(CameraError::kNone);
  camera_preview_.stop_requested.store(false);
  if (!InitializeCameraPreview()) {
    DeinitializeCameraPreview();
    camera_preview_.stop_requested.store(true);
    return false;
  }

  camera_preview_.task_active.store(true);
  BaseType_t result = xTaskCreate(CameraPreviewTaskEntry, "camera_preview",
      kCameraPreviewTaskStackBytes, this, kCameraPreviewTaskPriority, nullptr);
  if (result != pdPASS) {
    camera_preview_.error.store(CameraError::kPreviewTaskCreateFailed);
    camera_preview_.task_active.store(false);
    camera_preview_.stop_requested.store(true);
    DeinitializeCameraPreview();
    return false;
  }
  return true;
}

CameraError TDisplayP4AirDevice::GetCameraPreviewError() const {
  return camera_preview_.error.load();
}

bool TDisplayP4AirDevice::StopCameraPreview() {
  camera_preview_.stop_requested.store(true);
  // 不在这里发 VIDIOC_STREAMOFF — 让 RunCameraPreviewTask 退出时由
  // DeinitializeCameraPreview 统一处理，避免与正在运行的 DQBUF/PPA 产生 I2C
  // 竞态
  const uint32_t start_ms =
      static_cast<uint32_t>(xTaskGetTickCount() * portTICK_PERIOD_MS);
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

bool TDisplayP4AirDevice::GetCameraPreviewFrameInfo(
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

bool TDisplayP4AirDevice::CopyCameraPreviewFrame(
    uint8_t* buffer, size_t buffer_size, CameraPreviewFrameInfo* info) {
  if (buffer == nullptr || info == nullptr ||
      !GetCameraPreviewFrameInfo(info) || buffer_size < info->data_size ||
      camera_preview_.output_mutex == nullptr) {
    return false;
  }

  if (xSemaphoreTake(camera_preview_.output_mutex, pdMS_TO_TICKS(20)) !=
      pdTRUE) {
    return false;
  }
  std::memcpy(buffer, camera_preview_.output_buffer.get(), info->data_size);
  info->sequence = camera_preview_.frame_sequence.load();
  xSemaphoreGive(camera_preview_.output_mutex);
  return true;
}

void TDisplayP4AirDevice::CameraPreviewTaskEntry(void* context) {
  static_cast<TDisplayP4AirDevice*>(context)->RunCameraPreviewTask();
}

void TDisplayP4AirDevice::RunCameraPreviewTask() {
  camera_preview_.running.store(true);
  while (!camera_preview_.stop_requested.load()) {
    v4l2_buffer buffer = {};
    buffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buffer.memory = V4L2_MEMORY_MMAP;
    if (ioctl(camera_preview_.video_fd, VIDIOC_DQBUF, &buffer) != 0) {
      vTaskDelay(pdMS_TO_TICKS(10));
      continue;
    }

    const bool frame_valid = buffer.index < kCameraBufferCount &&
                             buffer.bytesused > 0 &&
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

bool TDisplayP4AirDevice::InitializeCameraPreview() {
  if (!driver_.IsScreenReady()) {
    camera_preview_.error.store(CameraError::kScreenNotReady);
    LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
        "Camera preview start failed: screen is not ready\n");
    return false;
  }
  if (!driver_.SetCameraPowerEnabled(true)) {
    camera_preview_.error.store(CameraError::kPowerEnableFailed);
    LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
        "Camera preview start failed: power enable failed\n");
    return false;
  }

  const bool restore_sensor = camera_preview_.video_system_initialized.load();
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
    esp_err_t result = esp_video_init_with_flags(&camera_config, init_flags);
    if (result != ESP_OK) {
      // 组件初始化失败时会清理当前已经创建的全部视频设备。
      camera_preview_.video_system_initialized.store(false);
      camera_preview_.error.store(CameraError::kVideoInitFailed);
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
    camera_preview_.error.store(CameraError::kVideoDeviceOpenFailed);
    LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
        "Open camera video device failed\n");
    return false;
  }

  if (restore_sensor) {
    // 摄像头重新上电后，通过组件公开接口重写传感器的完整寄存器配置。
    auto& sensor_format = camera_preview_.sensor_format;
    if (ioctl(camera_preview_.video_fd, VIDIOC_G_SENSOR_FMT, &sensor_format) !=
            0 ||
        ioctl(camera_preview_.video_fd, VIDIOC_S_SENSOR_FMT, &sensor_format) !=
            0) {
      camera_preview_.error.store(CameraError::kSensorRestoreFailed);
      LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
          "Restore camera sensor format failed\n");
      return false;
    }
  }

  v4l2_format format = {};
  format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  if (ioctl(camera_preview_.video_fd, VIDIOC_G_FMT, &format) != 0) {
    camera_preview_.error.store(CameraError::kFormatConfigurationFailed);
    LogMessage(LogLevel::kWarning, __FILE__, __LINE__, "VIDIOC_G_FMT failed\n");
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
    camera_preview_.error.store(CameraError::kFormatConfigurationFailed);
    LogMessage(LogLevel::kWarning, __FILE__, __LINE__, "VIDIOC_S_FMT failed\n");
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
    camera_preview_.error.store(CameraError::kBufferAllocationFailed);
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
      camera_preview_.error.store(CameraError::kBufferAllocationFailed);
      LogMessage(
          LogLevel::kWarning, __FILE__, __LINE__, "VIDIOC_QUERYBUF failed\n");
      return false;
    }
    camera_preview_.frame_buffer_sizes[index] = buffer.length;
    camera_preview_.frame_buffers[index] =
        mmap(nullptr, buffer.length, PROT_READ | PROT_WRITE, MAP_SHARED,
            camera_preview_.video_fd, buffer.m.offset);
    if (camera_preview_.frame_buffers[index] == MAP_FAILED) {
      camera_preview_.frame_buffers[index] = nullptr;
      camera_preview_.error.store(CameraError::kBufferMappingFailed);
      LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
          "Camera buffer mmap failed\n");
      return false;
    }
    if (ioctl(camera_preview_.video_fd, VIDIOC_QBUF, &buffer) != 0) {
      camera_preview_.error.store(CameraError::kBufferAllocationFailed);
      LogMessage(
          LogLevel::kWarning, __FILE__, __LINE__, "VIDIOC_QBUF failed\n");
      return false;
    }
  }

  if (camera_preview_.output_mutex == nullptr) {
    camera_preview_.output_mutex = xSemaphoreCreateMutex();
    if (camera_preview_.output_mutex == nullptr) {
      camera_preview_.error.store(
          CameraError::kOutputBufferAllocationFailed);
      LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
          "Camera output mutex allocation failed\n");
      return false;
    }
  }

  if (!camera_preview_.ppa.Init()) {
    camera_preview_.error.store(CameraError::kProcessingInitFailed);
    LogMessage(LogLevel::kWarning, __FILE__, __LINE__, "PPA SRM init failed\n");
    return false;
  }
  const size_t bytes_per_pixel = ScreenBitsPerPixel() / 8;
  camera_preview_.output_rotation_angle = NormalizeCameraPreviewRotationAngle(
      app::GetDisplayPreferences().screen_rotation_angle);
  const bool output_rotated = camera_preview_.output_rotation_angle == 90 ||
                              camera_preview_.output_rotation_angle == 270;
  const uint32_t output_screen_width =
      output_rotated ? ScreenHeight() : ScreenWidth();
  const uint32_t output_screen_height =
      output_rotated ? ScreenWidth() : ScreenHeight();
  camera_preview_.output_width = output_screen_width;
  camera_preview_.output_height = output_screen_height;
  camera_preview_.output_width =
      std::max<uint32_t>(1, camera_preview_.output_width);
  camera_preview_.output_height =
      std::max<uint32_t>(1, camera_preview_.output_height);
  camera_preview_.output_stride =
      camera_preview_.output_width * bytes_per_pixel;
  camera_preview_.output_buffer_size =
      AlignUp(camera_preview_.output_stride * camera_preview_.output_height,
          camera_preview_.ppa.CacheLineSize());
  void* output_buffer =
      heap_caps_aligned_calloc(camera_preview_.ppa.CacheLineSize(), 1,
          camera_preview_.output_buffer_size, MALLOC_CAP_SPIRAM);
  if (output_buffer == nullptr) {
    camera_preview_.error.store(
        CameraError::kOutputBufferAllocationFailed);
    LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
        "Camera output buffer allocation failed\n");
    return false;
  }
  camera_preview_.output_buffer.reset(static_cast<uint8_t*>(output_buffer));
  camera_preview_.clear_output_frames_remaining = kCameraOutputClearFrameCount;
  camera_preview_.warmup_frames_remaining = kCameraWarmupFrameCount;

  int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  if (ioctl(camera_preview_.video_fd, VIDIOC_STREAMON, &type) != 0) {
    camera_preview_.error.store(CameraError::kStreamStartFailed);
    LogMessage(
        LogLevel::kWarning, __FILE__, __LINE__, "VIDIOC_STREAMON failed\n");
    return false;
  }

  camera_preview_.initialized.store(true);
  LogMessage(LogLevel::kInfo, __FILE__, __LINE__,
      "Camera preview started (%lux%lu)\n", camera_preview_.frame_width,
      camera_preview_.frame_height);
  return true;
}

void TDisplayP4AirDevice::DeinitializeCameraPreview() {
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

bool TDisplayP4AirDevice::RenderCameraFrame(
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
  const float scale = std::min(static_cast<float>(output_width) /
                                   static_cast<float>(rotated_source_width),
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
  if (xSemaphoreTake(camera_preview_.output_mutex, pdMS_TO_TICKS(20)) !=
      pdTRUE) {
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
  const bool transformed =
      camera_preview_.ppa.Transform(input, output, transform);
  if (transformed) {
    camera_preview_.frame_sequence.fetch_add(1);
  }
  xSemaphoreGive(camera_preview_.output_mutex);
  return transformed;
}

bool TDisplayP4AirDevice::SetGpsEnabled(bool enabled) {
  if (enabled && gps_running_) {
    return true;
  }
  if (enabled && cellular_.task_active.load() && !SetCellularEnabled(false)) {
    return false;
  }
  if (nrf9151_mutex_ == nullptr ||
      xSemaphoreTake(nrf9151_mutex_, portMAX_DELAY) != pdTRUE) {
    return false;
  }

  bool result = true;
  if (!enabled) {
    if (gps_running_ && driver_.IsNrf9151Ready() &&
        driver_.chip().nrf9151 != nullptr) {
      std::string response;
      const auto stop_result = driver_.chip().nrf9151->SendCommand(
          "AT#XGNSS=0", &response, kNrf9151CommandTimeoutMs);
      response.clear();
      const auto nmea_result = driver_.chip().nrf9151->SendCommand(
          "AT#XGNSSNMEA=0", &response, kNrf9151CommandTimeoutMs);
      result &= stop_result == cpp_bus_driver::Nrf9151::CommandResult::kOk;
      result &= nmea_result == cpp_bus_driver::Nrf9151::CommandResult::kOk;
    }
    gps_running_ = false;
    gps_status_.running = false;
    gps_pending_data_.clear();
    if (!cellular_.task_active.load()) {
      result &= driver_.DeinitNrf9151();
    }
    xSemaphoreGive(nrf9151_mutex_);
    if (!result) {
      LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
          "Disable nRF9151 GNSS failed\n");
    }
    return result;
  }

  if (!driver_.InitNrf9151() || !driver_.IsNrf9151Ready() ||
      driver_.chip().nrf9151 == nullptr ||
      driver_.bus().nrf9151_uart_bus == nullptr) {
    driver_.DeinitNrf9151();
    xSemaphoreGive(nrf9151_mutex_);
    LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
        "Enable nRF9151 GNSS failed: modem unavailable\n");
    return false;
  }

  vTaskDelay(pdMS_TO_TICKS(kNrf9151StartupDelayMs));
  constexpr std::array<const char*, 5> kStartupCommands = {{
      "AT+CFUN=0",
      "AT%XSYSTEMMODE=0,0,1,0",
      "AT+CFUN=31",
      "AT#XGNSSNMEA=1",
      "AT#XGNSS=1,0,1",
  }};
  size_t completed_command_count = 0;
  for (const char* command : kStartupCommands) {
    std::string response;
    const auto command_result = driver_.chip().nrf9151->SendCommand(
        command, &response, kNrf9151CommandTimeoutMs);
    if (command_result != cpp_bus_driver::Nrf9151::CommandResult::kOk) {
      LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
          "Enable nRF9151 GNSS failed at %s: %s\n", command,
          cpp_bus_driver::Nrf9151::CommandResultToString(command_result));
      break;
    }
    ++completed_command_count;
  }

  if (completed_command_count != kStartupCommands.size()) {
    std::string response;
    if (completed_command_count >= 4) {
      driver_.chip().nrf9151->SendCommand(
          "AT#XGNSS=0", &response, kNrf9151CommandTimeoutMs);
    }
    if (completed_command_count >= 3) {
      response.clear();
      driver_.chip().nrf9151->SendCommand(
          "AT#XGNSSNMEA=0", &response, kNrf9151CommandTimeoutMs);
    }
    gps_running_ = false;
    gps_status_.running = false;
    driver_.DeinitNrf9151();
    xSemaphoreGive(nrf9151_mutex_);
    return false;
  }

  gps_status_ = GpsStatus();
  gps_status_.running = true;
  gps_status_.update_interval_ms = kNrf9151GnssUpdateIntervalMs;
  gps_pending_data_.clear();
  gps_running_ = true;
  result = driver_.bus().nrf9151_uart_bus->ClearRxBufferData();
  if (!result) {
    gps_running_ = false;
    gps_status_.running = false;
    driver_.DeinitNrf9151();
  }
  xSemaphoreGive(nrf9151_mutex_);
  return result;
}

bool TDisplayP4AirDevice::ReadGpsStatus(GpsStatus* status) {
  if (status == nullptr) {
    return false;
  }

  gps_status_.running = gps_running_;
  gps_status_.update_interval_ms = kNrf9151GnssUpdateIntervalMs;
  *status = gps_status_;
  if (!gps_running_) {
    return true;
  }
  if (!driver_.IsNrf9151Ready() || driver_.bus().nrf9151_uart_bus == nullptr ||
      nrf9151_mutex_ == nullptr ||
      xSemaphoreTake(nrf9151_mutex_, pdMS_TO_TICKS(20)) != pdTRUE) {
    return false;
  }

  auto& uart = *driver_.bus().nrf9151_uart_bus;
  const size_t rx_buffer_length = uart.GetRxBufferLength();
  if (rx_buffer_length == 0) {
    xSemaphoreGive(nrf9151_mutex_);
    return true;
  }

  const size_t buffer_length =
      std::min(rx_buffer_length, kGpsMaxReadBufferBytes);
  std::unique_ptr<uint8_t[]> buffer(
      new (std::nothrow) uint8_t[buffer_length + 1]);
  if (buffer == nullptr) {
    xSemaphoreGive(nrf9151_mutex_);
    return false;
  }

  const int32_t read_length =
      uart.Read(buffer.get(), static_cast<uint32_t>(buffer_length));
  if (read_length == 0) {
    xSemaphoreGive(nrf9151_mutex_);
    return true;
  }
  if (read_length < 0) {
    xSemaphoreGive(nrf9151_mutex_);
    return false;
  }

  const size_t data_length =
      std::min(static_cast<size_t>(read_length), buffer_length);
  buffer[data_length] = '\0';

  GpsStatus next_status = gps_status_;
  next_status.running = true;
  next_status.data_ready = true;
  next_status.bytes_read = data_length;
  next_status.update_interval_ms = kNrf9151GnssUpdateIntervalMs;

  cpp_bus_driver::GnssParser::Info info;
  gps_pending_data_.append(
      reinterpret_cast<const char*>(buffer.get()), data_length);
  const size_t complete_data_end = gps_pending_data_.rfind('\n');
  bool parse_success = false;
  if (complete_data_end != std::string::npos) {
    const size_t complete_data_length = complete_data_end + 1;
    parse_success = gps_parser_.ParseInfo(
        reinterpret_cast<const uint8_t*>(gps_pending_data_.data()),
        complete_data_length, info);
    gps_pending_data_.erase(0, complete_data_length);
  }
  if (gps_pending_data_.size() > kNrf9151PendingDataLimit) {
    LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
        "Discard oversized nRF9151 GNSS partial frame: %u bytes\n",
        static_cast<unsigned>(gps_pending_data_.size()));
    gps_pending_data_.clear();
  }
  xSemaphoreGive(nrf9151_mutex_);
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

bool TDisplayP4AirDevice::SetNfcPollingEnabled(bool enabled) {
  if (!enabled) {
    nfc_.stop_requested.store(true);
    for (uint32_t elapsed_ms = 0;
        elapsed_ms < kNfcTaskStopTimeoutMs && nfc_.task_active.load();
        elapsed_ms += kPowerOffTaskPollMs) {
      vTaskDelay(pdMS_TO_TICKS(kPowerOffTaskPollMs));
    }
    return !nfc_.task_active.load();
  }

  if (nfc_.task_active.load()) {
    return true;
  }
  if (nfc_.mutex == nullptr || driver_.chip().st25r3916 == nullptr) {
    LogMessage(LogLevel::kError, __FILE__, __LINE__,
        "Start NFC polling failed: resources are unavailable\n");
    return false;
  }

  if (xSemaphoreTake(nfc_.mutex, pdMS_TO_TICKS(20)) != pdTRUE) {
    return false;
  }
  nfc_.status = NfcStatus();
  nfc_.status.polling = true;
  xSemaphoreGive(nfc_.mutex);

  nfc_.stop_requested.store(false);
  nfc_.task_active.store(true);
  const BaseType_t task_result = xTaskCreate(NfcPollingTaskEntry, "nfc_poll",
      kNfcPollingTaskStackBytes, this, kNfcPollingTaskPriority, nullptr);
  if (task_result != pdPASS) {
    nfc_.task_active.store(false);
    if (xSemaphoreTake(nfc_.mutex, pdMS_TO_TICKS(20)) == pdTRUE) {
      nfc_.status.polling = false;
      nfc_.status.last_error = ESP_ERR_NO_MEM;
      xSemaphoreGive(nfc_.mutex);
    }
    LogMessage(LogLevel::kError, __FILE__, __LINE__,
        "Create NFC polling task failed\n");
    return false;
  }
  LogMessage(
      LogLevel::kDebug, __FILE__, __LINE__, "NFC polling task started\n");
  return true;
}

bool TDisplayP4AirDevice::ReadNfcStatus(NfcStatus* status) {
  if (status == nullptr) {
    return false;
  }
  if (nfc_.mutex == nullptr ||
      xSemaphoreTake(nfc_.mutex, pdMS_TO_TICKS(20)) != pdTRUE) {
    return false;
  }
  *status = nfc_.status;
  status->hardware_ready = driver_.IsSt25r3916Ready();
  status->polling = nfc_.task_active.load() && !nfc_.stop_requested.load();
  xSemaphoreGive(nfc_.mutex);
  return true;
}

void TDisplayP4AirDevice::NfcPollingTaskEntry(void* context) {
  auto* self = static_cast<TDisplayP4AirDevice*>(context);
  if (self != nullptr) {
    self->RunNfcPollingTask();
  }
  vTaskDelete(nullptr);
}

void TDisplayP4AirDevice::RunNfcPollingTask() {
  auto* const nfc_driver = driver_.chip().st25r3916.get();
  const bool initialized = nfc_driver != nullptr && driver_.InitSt25r3916() &&
                           driver_.IsSt25r3916Ready();
  if (!initialized) {
    const auto& driver_status = driver_.status().st25r3916;
    int error = RFAL_ERR_INTERNAL;
    if (driver_status.result != RFAL_ERR_NONE) {
      error = static_cast<int>(driver_status.result);
    } else if (driver_status.platform_error !=
               stsw_st25rfal002_cpp_bus_driver::PlatformError::kNone) {
      error = kNfcPlatformErrorBase +
              static_cast<int>(driver_status.platform_error);
    }
    if (xSemaphoreTake(nfc_.mutex, pdMS_TO_TICKS(20)) == pdTRUE) {
      nfc_.status.hardware_ready = false;
      nfc_.status.polling = false;
      nfc_.status.last_error = error;
      xSemaphoreGive(nfc_.mutex);
    }
    LogMessage(LogLevel::kError, __FILE__, __LINE__,
        "Start NFC polling failed (RFAL: %u, platform: %u)\n",
        static_cast<unsigned>(driver_status.result),
        static_cast<unsigned>(driver_status.platform_error));
    nfc_.task_active.store(false);
    return;
  }

  if (xSemaphoreTake(nfc_.mutex, pdMS_TO_TICKS(20)) == pdTRUE) {
    nfc_.status.hardware_ready = true;
    nfc_.status.last_error = 0;
    xSemaphoreGive(nfc_.mutex);
  }

  const bool debug_logging_enabled = ShouldLog(LogLevel::kDebug);
  RunNfcDebugDiagnostics(*nfc_driver);

  rfalNfcDiscoverParam parameters = CreateNfcDiscoveryParameters();
  ReturnCode result = rfalNfcDiscover(&parameters);
  if (result != RFAL_ERR_NONE) {
    if (xSemaphoreTake(nfc_.mutex, pdMS_TO_TICKS(20)) == pdTRUE) {
      nfc_.status.hardware_ready = false;
      nfc_.status.polling = false;
      nfc_.status.last_error = static_cast<int>(result);
      xSemaphoreGive(nfc_.mutex);
    }
    LogMessage(LogLevel::kError, __FILE__, __LINE__,
        "Start NFC discovery failed (RFAL: %u)\n",
        static_cast<unsigned>(result));
    driver_.DeinitSt25r3916();
    nfc_.task_active.store(false);
    return;
  }
  LogMessage(LogLevel::kDebug, __FILE__, __LINE__,
      "NFC discovery started (duration: %u ms, removal: %u ms, "
      "hold: %u ms)\n",
      static_cast<unsigned>(kNfcDiscoveryDurationMs),
      static_cast<unsigned>(kNfcCardRemovalTimeoutMs),
      static_cast<unsigned>(kNfcActiveDeviceHoldMs));

  TickType_t last_card_tick = xTaskGetTickCount();
  TickType_t last_debug_log_tick = last_card_tick;
  rfalNfcState previous_debug_state = rfalNfcGetState();
  uint32_t debug_discovery_cycle_count = 0;
  bool card_present = false;
  NfcTechnology last_technology = NfcTechnology::kUnknown;
  std::array<uint8_t, kNfcIdentifierCapacity> last_identifier = {};
  size_t last_identifier_length = 0;
  while (!nfc_.stop_requested.load()) {
    nfc_driver->NfcWorker();
    const auto platform_error = nfc_driver->platform_error();
    if (platform_error !=
        stsw_st25rfal002_cpp_bus_driver::PlatformError::kNone) {
      if (xSemaphoreTake(nfc_.mutex, pdMS_TO_TICKS(20)) == pdTRUE) {
        nfc_.status.last_error =
            kNfcPlatformErrorBase + static_cast<int>(platform_error);
        xSemaphoreGive(nfc_.mutex);
      }
      LogMessage(LogLevel::kError, __FILE__, __LINE__,
          "NFC platform failure (platform: %u)\n",
          static_cast<unsigned>(platform_error));
      break;
    }

    const rfalNfcState state = rfalNfcGetState();
    if (rfalNfcIsDevActivated(state)) {
      rfalNfcDevice* active_device = nullptr;
      result = rfalNfcGetActiveDevice(&active_device);
      if (result != RFAL_ERR_NONE || active_device == nullptr) {
        const ReturnCode error = result == RFAL_ERR_NONE ? RFAL_ERR_INTERNAL
                                                        : result;
        if (xSemaphoreTake(nfc_.mutex, pdMS_TO_TICKS(20)) == pdTRUE) {
          nfc_.status.last_error = static_cast<int>(error);
          xSemaphoreGive(nfc_.mutex);
        }
        LogMessage(LogLevel::kError, __FILE__, __LINE__,
            "Read active NFC device failed (RFAL: %u, device: %s)\n",
            static_cast<unsigned>(error),
            active_device == nullptr ? "null" : "ready");
      } else {
        const size_t identifier_length = active_device->nfcid == nullptr
                                             ? 0
                                             : std::min<size_t>(
                                                   active_device->nfcidLen,
                                                   kNfcIdentifierCapacity);
        const NfcTechnology technology = ToNfcTechnology(active_device->type);
        const bool same_card =
            card_present && last_technology == technology &&
            last_identifier_length == identifier_length &&
            (identifier_length == 0 ||
                std::memcmp(last_identifier.data(), active_device->nfcid,
                    identifier_length) == 0);

        NfcStatus detected_status;
        if (!same_card) {
          detected_status.hardware_ready = true;
          detected_status.polling = true;
          detected_status.card_present = true;
          detected_status.technology = technology;
          detected_status.identifier_length = identifier_length;
          if (identifier_length > 0) {
            std::memcpy(detected_status.identifier, active_device->nfcid,
                identifier_length);
          }
          PopulateNfcTagDetails(*active_device, &detected_status);
        }

        uint32_t detection_count = 0;
        bool status_updated = false;
        if (xSemaphoreTake(nfc_.mutex, pdMS_TO_TICKS(20)) == pdTRUE) {
          if (same_card) {
            nfc_.status.card_present = true;
            nfc_.status.last_error = 0;
          } else {
            detected_status.detection_count =
                nfc_.status.detection_count + 1;
            nfc_.status = detected_status;
          }
          detection_count = nfc_.status.detection_count;
          xSemaphoreGive(nfc_.mutex);
          status_updated = true;
        }

        if (status_updated) {
          card_present = true;
          last_card_tick = xTaskGetTickCount();
        }
        if (status_updated && !same_card) {
          last_technology = technology;
          last_identifier_length = identifier_length;
          last_identifier.fill(0);
          if (identifier_length > 0) {
            std::memcpy(last_identifier.data(), active_device->nfcid,
                identifier_length);
          }
          LogMessage(LogLevel::kDebug, __FILE__, __LINE__,
              "NFC tag detected (type: %u, identifier length: %u, "
              "count: %u, NDEF: %s, content error: %d)\n",
              static_cast<unsigned>(active_device->type),
              static_cast<unsigned>(active_device->nfcidLen),
              static_cast<unsigned>(detection_count),
              detected_status.ndef_present ? "present" : "none",
              detected_status.content_error);
        }
      }
      vTaskDelay(pdMS_TO_TICKS(kNfcActiveDeviceHoldMs));

      result = rfalNfcDeactivate(RFAL_NFC_DEACTIVATE_DISCOVERY);
      if (result != RFAL_ERR_NONE &&
          xSemaphoreTake(nfc_.mutex, pdMS_TO_TICKS(20)) == pdTRUE) {
        nfc_.status.last_error = static_cast<int>(result);
        xSemaphoreGive(nfc_.mutex);
      }
      if (result != RFAL_ERR_NONE) {
        LogMessage(LogLevel::kError, __FILE__, __LINE__,
            "Restart NFC discovery failed (RFAL: %u)\n",
            static_cast<unsigned>(result));
        vTaskDelay(pdMS_TO_TICKS(kNfcDiscoveryRestartDelayMs));
      }
    } else if (card_present && rfalNfcIsInDiscovery(state) &&
               xTaskGetTickCount() - last_card_tick >=
                   pdMS_TO_TICKS(kNfcCardRemovalTimeoutMs) &&
               xSemaphoreTake(nfc_.mutex, pdMS_TO_TICKS(20)) == pdTRUE) {
      nfc_.status.card_present = false;
      xSemaphoreGive(nfc_.mutex);
      card_present = false;
    }

    if (debug_logging_enabled) {
      const TickType_t now = xTaskGetTickCount();
      if (state != previous_debug_state) {
        if (state == RFAL_NFC_STATE_START_DISCOVERY) {
          ++debug_discovery_cycle_count;
        }
        previous_debug_state = state;
      }
      if (now - last_debug_log_tick >=
          pdMS_TO_TICKS(kNfcDebugStatusLogIntervalMs)) {
        LogMessage(LogLevel::kDebug, __FILE__, __LINE__,
            "NFC discovery active (state: %u, cycles: %u, interrupt: %s)\n",
            static_cast<unsigned>(state),
            static_cast<unsigned>(debug_discovery_cycle_count),
            tool_ != nullptr && tool_->GpioRead(gpio::st25r3916::kInt)
                ? "high"
                : "low");
        last_debug_log_tick = now;
      }
    }
    vTaskDelay(pdMS_TO_TICKS(1));
  }

  rfalNfcDeactivate(RFAL_NFC_DEACTIVATE_IDLE);
  for (int worker_count = 0; worker_count < 20; ++worker_count) {
    nfc_driver->NfcWorker();
    if (rfalNfcGetState() == RFAL_NFC_STATE_IDLE) {
      break;
    }
    vTaskDelay(pdMS_TO_TICKS(1));
  }
  if (xSemaphoreTake(nfc_.mutex, pdMS_TO_TICKS(20)) == pdTRUE) {
    nfc_.status.polling = false;
    nfc_.status.card_present = false;
    nfc_.status.hardware_ready = false;
    xSemaphoreGive(nfc_.mutex);
  }
  const bool deinitialized = driver_.DeinitSt25r3916();
  nfc_.task_active.store(false);
  LogMessage(deinitialized ? LogLevel::kDebug : LogLevel::kError, __FILE__,
      __LINE__, deinitialized ? "NFC polling task stopped\n"
                              : "NFC polling task cleanup failed\n");
}

bool TDisplayP4AirDevice::SetInfraredReceiverEnabled(bool enabled) {
  if (!enabled) {
    infrared_.receiver_enabled.store(false);
    if (infrared_.mutex == nullptr ||
        xSemaphoreTake(infrared_.mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
      return false;
    }
    esp_err_t result = ESP_OK;
    if (infrared_.receive_channel_enabled &&
        infrared_.receive_channel != nullptr) {
      result = rmt_disable(infrared_.receive_channel);
      if (result == ESP_OK) {
        infrared_.receive_channel_enabled = false;
      }
    }
    infrared_.receive_pending.store(false);
    infrared_.receive_complete.store(false);
    infrared_.received_symbol_count.store(0);
    std::fill(std::begin(infrared_.receive_symbols),
        std::end(infrared_.receive_symbols), rmt_symbol_word_t{});
    infrared_.status.receiver_enabled = false;
    infrared_.status.frame_received = false;
    infrared_.status.repeat = false;
    infrared_.status.address = 0;
    infrared_.status.command = 0;
    infrared_.status.receive_count = 0;
    infrared_.status.decode_error_count = 0;
    infrared_.status.last_error = result;
    xSemaphoreGive(infrared_.mutex);
    return result == ESP_OK;
  }
  if (!InitializeInfraredHardware()) {
    return false;
  }
  if (xSemaphoreTake(infrared_.mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
    return false;
  }
  esp_err_t result = ESP_OK;
  if (!infrared_.receive_channel_enabled) {
    result = rmt_enable(infrared_.receive_channel);
    infrared_.receive_channel_enabled = result == ESP_OK;
  }
  infrared_.status.receiver_enabled = result == ESP_OK;
  infrared_.status.last_error = result;
  xSemaphoreGive(infrared_.mutex);
  if (result != ESP_OK) {
    return false;
  }
  infrared_.receiver_enabled.store(true);
  return StartInfraredReceive();
}

bool TDisplayP4AirDevice::SendInfraredNec(uint8_t address, uint8_t command) {
  if (!InitializeInfraredHardware()) {
    return false;
  }

  std::array<rmt_symbol_word_t, kNecFrameSymbolCount> symbols = {};
  symbols[0].level0 = 1;
  symbols[0].duration0 = kNecLeaderMarkUs;
  symbols[0].level1 = 0;
  symbols[0].duration1 = kNecLeaderSpaceUs;

  const uint32_t raw_data =
      static_cast<uint32_t>(address) |
      (static_cast<uint32_t>(static_cast<uint8_t>(~address)) << 8) |
      (static_cast<uint32_t>(command) << 16) |
      (static_cast<uint32_t>(static_cast<uint8_t>(~command)) << 24);
  for (size_t bit = 0; bit < kNecDataBitCount; ++bit) {
    rmt_symbol_word_t& symbol = symbols[bit + 1];
    symbol.level0 = 1;
    symbol.duration0 = kNecBitMarkUs;
    symbol.level1 = 0;
    symbol.duration1 =
        (raw_data & (1UL << bit)) != 0 ? kNecOneSpaceUs : kNecZeroSpaceUs;
  }
  symbols.back().level0 = 1;
  symbols.back().duration0 = kNecBitMarkUs;
  symbols.back().level1 = 0;
  symbols.back().duration1 = kNecFrameEndSpaceUs;

  rmt_transmit_config_t transmit_config = {};
  transmit_config.loop_count = 0;
  esp_err_t result =
      rmt_transmit(infrared_.transmit_channel, infrared_.copy_encoder,
          symbols.data(), sizeof(symbols), &transmit_config);
  if (result == ESP_OK) {
    result = rmt_tx_wait_all_done(
        infrared_.transmit_channel, kInfraredTransmitTimeoutMs);
  }
  if (xSemaphoreTake(infrared_.mutex, pdMS_TO_TICKS(20)) == pdTRUE) {
    infrared_.status.last_error = result;
    xSemaphoreGive(infrared_.mutex);
  }
  return result == ESP_OK;
}

bool TDisplayP4AirDevice::ReadInfraredStatus(InfraredStatus* status) {
  if (status == nullptr) {
    return false;
  }
  if (infrared_.mutex == nullptr ||
      xSemaphoreTake(infrared_.mutex, pdMS_TO_TICKS(20)) != pdTRUE) {
    return false;
  }

  if (infrared_.receive_complete.exchange(false)) {
    uint8_t address = infrared_.status.address;
    uint8_t command = infrared_.status.command;
    const NecDecodeResult decode_result =
        DecodeNecSymbols(infrared_.receive_symbols,
            infrared_.received_symbol_count.load(), &address, &command);
    if (decode_result == NecDecodeResult::kFrame) {
      infrared_.status.frame_received = true;
      infrared_.status.repeat = false;
      infrared_.status.address = address;
      infrared_.status.command = command;
      ++infrared_.status.receive_count;
      infrared_.status.last_error = 0;
    } else if (decode_result == NecDecodeResult::kRepeat &&
               infrared_.status.frame_received) {
      infrared_.status.repeat = true;
      ++infrared_.status.receive_count;
      infrared_.status.last_error = 0;
    } else {
      ++infrared_.status.decode_error_count;
    }
  }
  infrared_.status.receiver_enabled = infrared_.receiver_enabled.load();
  *status = infrared_.status;
  xSemaphoreGive(infrared_.mutex);

  if (infrared_.receiver_enabled.load()) {
    StartInfraredReceive();
  }
  return true;
}

bool IRAM_ATTR TDisplayP4AirDevice::InfraredReceiveDoneCallback(
    rmt_channel_handle_t channel, const rmt_rx_done_event_data_t* event_data,
    void* context) {
  (void)channel;
  auto* self = static_cast<TDisplayP4AirDevice*>(context);
  if (self == nullptr || event_data == nullptr) {
    return false;
  }
  if (!self->infrared_.receiver_enabled.load()) {
    self->infrared_.receive_pending.store(false);
    self->infrared_.receive_complete.store(false);
    return false;
  }
  self->infrared_.received_symbol_count.store(std::min<size_t>(
      event_data->num_symbols, std::size(self->infrared_.receive_symbols)));
  self->infrared_.receive_pending.store(false);
  self->infrared_.receive_complete.store(true);
  return false;
}

bool TDisplayP4AirDevice::InitializeInfraredHardware() {
  if (infrared_.mutex == nullptr) {
    return false;
  }
  if (xSemaphoreTake(infrared_.mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
    return false;
  }
  if (infrared_.status.hardware_ready) {
    xSemaphoreGive(infrared_.mutex);
    return true;
  }

  esp_err_t result = ESP_OK;
  rmt_rx_channel_config_t receive_config = {};
  receive_config.gpio_num = static_cast<gpio_num_t>(gpio::infrared::kRx);
  receive_config.clk_src = RMT_CLK_SRC_DEFAULT;
  receive_config.resolution_hz = kInfraredRmtResolutionHz;
  receive_config.mem_block_symbols = std::size(infrared_.receive_symbols);
  receive_config.flags.with_dma = false;
  result = rmt_new_rx_channel(&receive_config, &infrared_.receive_channel);

  if (result == ESP_OK) {
    rmt_rx_event_callbacks_t callbacks = {};
    callbacks.on_recv_done = InfraredReceiveDoneCallback;
    result = rmt_rx_register_event_callbacks(
        infrared_.receive_channel, &callbacks, this);
  }

  if (result == ESP_OK) {
    rmt_tx_channel_config_t transmit_config = {};
    transmit_config.gpio_num = static_cast<gpio_num_t>(gpio::infrared::kTx);
    transmit_config.clk_src = RMT_CLK_SRC_DEFAULT;
    transmit_config.resolution_hz = kInfraredRmtResolutionHz;
    transmit_config.mem_block_symbols = 64;
    transmit_config.trans_queue_depth = 4;
    transmit_config.flags.with_dma = false;
    result = rmt_new_tx_channel(&transmit_config, &infrared_.transmit_channel);
  }

  if (result == ESP_OK) {
    rmt_copy_encoder_config_t encoder_config = {};
    result = rmt_new_copy_encoder(&encoder_config, &infrared_.copy_encoder);
  }

  if (result == ESP_OK) {
    rmt_carrier_config_t carrier_config = {};
    carrier_config.frequency_hz = 38000;
    carrier_config.duty_cycle = 0.33F;
    carrier_config.flags.polarity_active_low = false;
    carrier_config.flags.always_on = false;
    result = rmt_apply_carrier(infrared_.transmit_channel, &carrier_config);
  }
  if (result == ESP_OK) {
    result = rmt_enable(infrared_.receive_channel);
    infrared_.receive_channel_enabled = result == ESP_OK;
  }
  if (result == ESP_OK) {
    result = rmt_enable(infrared_.transmit_channel);
  }

  if (result != ESP_OK) {
    if (infrared_.receive_channel != nullptr) {
      rmt_disable(infrared_.receive_channel);
      rmt_del_channel(infrared_.receive_channel);
      infrared_.receive_channel = nullptr;
    }
    infrared_.receive_channel_enabled = false;
    if (infrared_.transmit_channel != nullptr) {
      rmt_disable(infrared_.transmit_channel);
      rmt_del_channel(infrared_.transmit_channel);
      infrared_.transmit_channel = nullptr;
    }
    if (infrared_.copy_encoder != nullptr) {
      rmt_del_encoder(infrared_.copy_encoder);
      infrared_.copy_encoder = nullptr;
    }
  }
  infrared_.status.hardware_ready = result == ESP_OK;
  infrared_.status.last_error = result;
  xSemaphoreGive(infrared_.mutex);
  return result == ESP_OK;
}

bool TDisplayP4AirDevice::StartInfraredReceive() {
  if (!infrared_.receiver_enabled.load() ||
      infrared_.receive_channel == nullptr) {
    return false;
  }
  bool expected = false;
  if (!infrared_.receive_pending.compare_exchange_strong(expected, true)) {
    return true;
  }

  infrared_.received_symbol_count.store(0);
  rmt_receive_config_t receive_config = {};
  receive_config.signal_range_min_ns = kInfraredReceiveMinimumNs;
  receive_config.signal_range_max_ns = kInfraredReceiveMaximumNs;
  const esp_err_t result =
      rmt_receive(infrared_.receive_channel, infrared_.receive_symbols,
          sizeof(infrared_.receive_symbols), &receive_config);
  if (result != ESP_OK) {
    infrared_.receive_pending.store(false);
    if (xSemaphoreTake(infrared_.mutex, pdMS_TO_TICKS(20)) == pdTRUE) {
      infrared_.status.last_error = result;
      xSemaphoreGive(infrared_.mutex);
    }
  }
  return result == ESP_OK;
}

bool TDisplayP4AirDevice::SetCellularEnabled(bool enabled) {
  if (!enabled) {
    cellular_.stop_requested.store(true);
    for (uint32_t elapsed_ms = 0;
        elapsed_ms < kCellularTaskStopTimeoutMs && cellular_.task_active.load();
        elapsed_ms += kPowerOffTaskPollMs) {
      vTaskDelay(pdMS_TO_TICKS(kPowerOffTaskPollMs));
    }
    return !cellular_.task_active.load();
  }

  if (cellular_.task_active.load()) {
    return true;
  }
  if (nrf9151_mutex_ == nullptr || cellular_.status_mutex == nullptr ||
      driver_.chip().nrf9151 == nullptr ||
      driver_.bus().nrf9151_uart_bus == nullptr) {
    return false;
  }
  // nRF9151 的系统模式和 UART 为单实例资源，蜂窝模式启动前先结束 GNSS。
  if (gps_running_ && !SetGpsEnabled(false)) {
    return false;
  }

  if (xSemaphoreTake(cellular_.status_mutex, pdMS_TO_TICKS(20)) != pdTRUE) {
    return false;
  }
  cellular_.status = CellularStatus();
  cellular_.status.hardware_ready = true;
  cellular_.status.enabled = true;
  xSemaphoreGive(cellular_.status_mutex);

  cellular_.stop_requested.store(false);
  cellular_.task_active.store(true);
  const BaseType_t task_result = xTaskCreate(CellularTaskEntry, "cellular",
      kCellularTaskStackBytes, this, kCellularTaskPriority, nullptr);
  if (task_result != pdPASS) {
    cellular_.task_active.store(false);
    if (xSemaphoreTake(cellular_.status_mutex, pdMS_TO_TICKS(20)) == pdTRUE) {
      cellular_.status.enabled = false;
      cellular_.status.last_error = ESP_ERR_NO_MEM;
      xSemaphoreGive(cellular_.status_mutex);
    }
    return false;
  }
  return true;
}

bool TDisplayP4AirDevice::ReadCellularStatus(CellularStatus* status) {
  if (status == nullptr) {
    return false;
  }
  if (cellular_.status_mutex == nullptr ||
      xSemaphoreTake(cellular_.status_mutex, pdMS_TO_TICKS(20)) != pdTRUE) {
    return false;
  }
  *status = cellular_.status;
  status->enabled =
      cellular_.task_active.load() && !cellular_.stop_requested.load();
  xSemaphoreGive(cellular_.status_mutex);
  return true;
}

bool TDisplayP4AirDevice::SendCellularCommand(const char* command,
    char* response, size_t response_size, uint32_t timeout_ms) {
  if (response != nullptr && response_size > 0) {
    response[0] = '\0';
  }
  if (command == nullptr || std::strncmp(command, "AT", 2) != 0 ||
      response == nullptr || response_size == 0 || timeout_ms == 0 ||
      !cellular_.task_active.load() || nrf9151_mutex_ == nullptr) {
    return false;
  }
  if (xSemaphoreTake(nrf9151_mutex_, pdMS_TO_TICKS(timeout_ms)) != pdTRUE) {
    return false;
  }
  if (!driver_.IsNrf9151Ready() || driver_.chip().nrf9151 == nullptr) {
    xSemaphoreGive(nrf9151_mutex_);
    return false;
  }

  std::string command_response;
  const auto result = driver_.chip().nrf9151->SendCommand(
      command, &command_response, timeout_ms);
  std::snprintf(response, response_size, "%s", command_response.c_str());
  xSemaphoreGive(nrf9151_mutex_);
  return result == cpp_bus_driver::Nrf9151::CommandResult::kOk;
}

void TDisplayP4AirDevice::CellularTaskEntry(void* context) {
  auto* self = static_cast<TDisplayP4AirDevice*>(context);
  if (self != nullptr) {
    self->RunCellularTask();
  }
  vTaskDelete(nullptr);
}

void TDisplayP4AirDevice::RunCellularTask() {
  CellularStatus snapshot;
  snapshot.hardware_ready = driver_.chip().nrf9151 != nullptr &&
                            driver_.bus().nrf9151_uart_bus != nullptr;
  snapshot.enabled = true;

  const auto publish_status = [this, &snapshot]() {
    if (xSemaphoreTake(cellular_.status_mutex, pdMS_TO_TICKS(20)) == pdTRUE) {
      cellular_.status = snapshot;
      xSemaphoreGive(cellular_.status_mutex);
    }
  };
  using CellularCommandResult = cpp_bus_driver::Nrf9151::CommandResult;
  CellularCommandResult last_command_result = CellularCommandResult::kOk;
  const auto send_command =
      [this, &snapshot, &last_command_result](
          const char* command, std::string* response) {
        last_command_result = driver_.chip().nrf9151->SendCommand(
            command, response, kCellularCommandTimeoutMs);
        snapshot.last_error = static_cast<int>(last_command_result);
        return last_command_result == CellularCommandResult::kOk;
      };
  const auto log_command_error = [&last_command_result](const char* operation,
                                     const char* command,
                                     const std::string& response) {
    std::string compact_response = TrimAsciiWhitespace(response);
    std::replace(
        compact_response.begin(), compact_response.end(), '\r', ' ');
    std::replace(
        compact_response.begin(), compact_response.end(), '\n', ' ');
    LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
        "Cellular test %s (command: %s, result: %s, response: %s)\n",
        operation, command,
        cpp_bus_driver::Nrf9151::CommandResultToString(last_command_result),
        compact_response.empty() ? "-" : compact_response.c_str());
  };

  const bool modem_locked =
      snapshot.hardware_ready &&
      xSemaphoreTake(nrf9151_mutex_, portMAX_DELAY) == pdTRUE;
  bool initialized = modem_locked;
  if (initialized) {
    snapshot.powered = driver_.InitNrf9151();
    initialized = snapshot.powered && driver_.IsNrf9151Ready();
  }

  if (initialized && !cellular_.stop_requested.load()) {
    std::string response;
    initialized = send_command("AT+CFUN=0", &response);
    if (!initialized) {
      log_command_error(
          "failed to stop the modem before configuration", "AT+CFUN=0",
          response);
    }
    constexpr std::array<const char*, 3> kSystemModeCommands = {{
        "AT%XSYSTEMMODE=1,1,0,0",
        "AT%XSYSTEMMODE=1,0,0,0",
        "AT%XSYSTEMMODE=0,1,0,0",
    }};
    if (initialized) {
      // 优先同时启用 LTE-M 和 NB-IoT；旧固件不支持时依次退回单模式。
      bool mode_selected = false;
      for (const char* command : kSystemModeCommands) {
        if (cellular_.stop_requested.load()) {
          break;
        }
        response.clear();
        if (send_command(command, &response)) {
          mode_selected = true;
          break;
        }
      }
      if (!mode_selected && !cellular_.stop_requested.load()) {
        log_command_error(
            "failed to configure a supported cellular system mode",
            kSystemModeCommands.back(), response);
      }
      initialized = mode_selected;
    }
    if (initialized && !cellular_.stop_requested.load()) {
      response.clear();
      initialized = send_command("AT+CFUN=1", &response);
      if (!initialized) {
        log_command_error(
            "failed to activate the modem", "AT+CFUN=1", response);
      }
    }
    if (initialized && !cellular_.stop_requested.load()) {
      response.clear();
      if (!send_command("AT+CMEE=1", &response)) {
        log_command_error(
            "failed to enable numeric modem errors", "AT+CMEE=1",
            response);
      }
    }
  }

  if (initialized && !cellular_.stop_requested.load()) {
    CopyString(snapshot.model, sizeof(snapshot.model),
        driver_.chip().nrf9151->chip_id());
    std::string response;
    const bool imei_command_ok = send_command("AT+CGSN", &response);
    if (imei_command_ok) {
      std::string imei;
      if (ExtractAtNumericLine(response, &imei)) {
        CopyString(snapshot.imei, sizeof(snapshot.imei), imei);
      } else {
        log_command_error(
            "received an invalid IMEI response", "AT+CGSN", response);
      }
    } else {
      log_command_error("failed to query IMEI", "AT+CGSN", response);
    }
    std::string firmware;
    if (driver_.chip().nrf9151->GetModemFirmwareVersion(
            &firmware, kCellularCommandTimeoutMs)) {
      CopyString(snapshot.firmware, sizeof(snapshot.firmware), firmware);
      snapshot.last_error = 0;
    } else {
      LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
          "Cellular test failed to query modem firmware version\n");
    }
  }
  if (modem_locked) {
    xSemaphoreGive(nrf9151_mutex_);
  }

  if (!initialized) {
    if (!cellular_.stop_requested.load()) {
      LogMessage(LogLevel::kError, __FILE__, __LINE__,
          "Cellular test initialization failed (hardware: %s, power: %s, "
          "result: %s)\n",
          snapshot.hardware_ready ? "ready" : "unavailable",
          snapshot.powered ? "on" : "off",
          cpp_bus_driver::Nrf9151::CommandResultToString(
              last_command_result));
    }
    snapshot.enabled = false;
    publish_status();
    if (snapshot.powered) {
      if (xSemaphoreTake(nrf9151_mutex_, portMAX_DELAY) == pdTRUE) {
        driver_.DeinitNrf9151();
        xSemaphoreGive(nrf9151_mutex_);
      }
      snapshot.powered = false;
      publish_status();
    }
    cellular_.task_active.store(false);
    return;
  }
  snapshot.last_error = 0;
  publish_status();

  // CFUN=1 后 UICC 仍在异步启动，过早查询 CPIN 会短暂返回 ERROR。
  for (uint32_t elapsed_ms = 0;
      elapsed_ms < kCellularSimStartupDelayMs &&
      !cellular_.stop_requested.load();
      elapsed_ms += 100) {
    vTaskDelay(pdMS_TO_TICKS(100));
  }

  uint32_t network_time_poll_elapsed_ms = kCellularNetworkTimePollMs;
  bool sim_error_reported = false;
  bool registration_error_reported = false;
  bool signal_error_reported = false;
  bool operator_error_reported = false;
  bool network_time_error_reported = false;
  while (!cellular_.stop_requested.load()) {
    if (xSemaphoreTake(nrf9151_mutex_, pdMS_TO_TICKS(100)) == pdTRUE) {
      std::string response;
      const CellularSimState previous_sim_state = snapshot.sim_state;
      CellularSimState sim_state = CellularSimState::kUnavailable;
      const bool sim_command_ok = send_command("AT+CPIN?", &response);
      const bool sim_status_valid =
          sim_command_ok && ParseCellularSimState(response, &sim_state);
      if (!sim_status_valid && !sim_error_reported) {
        log_command_error(sim_command_ok ? "received an invalid SIM response"
                                         : "failed to query SIM status",
            "AT+CPIN?", response);
      }
      sim_error_reported = !sim_status_valid;
      if (sim_status_valid && sim_state != previous_sim_state) {
        LogMessage(sim_state == CellularSimState::kReady ? LogLevel::kInfo
                                                         : LogLevel::kWarning,
            __FILE__, __LINE__, "Cellular SIM status changed: %s\n",
            CellularSimStateLogText(sim_state));
      }
      snapshot.sim_state = sim_state;

      const bool sim_ready = snapshot.sim_state == CellularSimState::kReady;
      if (!sim_ready) {
        snapshot.registration = CellularRegistrationState::kUnknown;
        snapshot.signal_quality = 99;
        snapshot.rssi_dbm = 0;
        snapshot.operator_name[0] = '\0';
        snapshot.network_time[0] = '\0';
        snapshot.network_time_ready = false;
        network_time_poll_elapsed_ms = kCellularNetworkTimePollMs;
        registration_error_reported = false;
        signal_error_reported = false;
        operator_error_reported = false;
        network_time_error_reported = false;
      }

      if (sim_ready && !cellular_.stop_requested.load()) {
        response.clear();
        CellularRegistrationState registration =
            CellularRegistrationState::kUnknown;
        const bool registration_command_ok =
            send_command("AT+CEREG?", &response);
        const bool registration_status_valid =
            registration_command_ok &&
            ParseCellularRegistration(response, &registration);
        if (!registration_status_valid && !registration_error_reported) {
          log_command_error(
              registration_command_ok
                  ? "received an invalid network registration response"
                  : "failed to query network registration",
              "AT+CEREG?", response);
        }
        registration_error_reported = !registration_status_valid;
        if (registration_status_valid &&
            registration == CellularRegistrationState::kDenied &&
            snapshot.registration != CellularRegistrationState::kDenied) {
          LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
              "Cellular network registration denied\n");
        }
        snapshot.registration = registration;
      }
      if (sim_ready && !cellular_.stop_requested.load()) {
        response.clear();
        int signal_quality = 99;
        int rssi_dbm = 0;
        const bool signal_command_ok = send_command("AT+CSQ", &response);
        const bool signal_status_valid =
            signal_command_ok &&
            ParseCellularSignal(response, &signal_quality, &rssi_dbm);
        if (!signal_status_valid && !signal_error_reported) {
          log_command_error(signal_command_ok
                                ? "received an invalid signal response"
                                : "failed to query signal quality",
              "AT+CSQ", response);
        }
        signal_error_reported = !signal_status_valid;
        snapshot.signal_quality = signal_quality;
        snapshot.rssi_dbm = rssi_dbm;
      }
      if (sim_ready && !cellular_.stop_requested.load()) {
        response.clear();
        std::string operator_name;
        const bool operator_command_ok = send_command("AT+COPS?", &response);
        const bool operator_status_valid =
            operator_command_ok &&
            ParseCellularOperator(response, &operator_name);
        if (!operator_status_valid && !operator_error_reported) {
          log_command_error(operator_command_ok
                                ? "received an invalid operator response"
                                : "failed to query operator",
              "AT+COPS?", response);
        }
        operator_error_reported = !operator_status_valid;
        CopyString(snapshot.operator_name, sizeof(snapshot.operator_name),
            operator_name);
      }

      const bool network_registered =
          sim_ready && IsCellularRegistered(snapshot.registration);
      if (!network_registered) {
        snapshot.network_time[0] = '\0';
        snapshot.network_time_ready = false;
        network_time_poll_elapsed_ms = kCellularNetworkTimePollMs;
        network_time_error_reported = false;
      } else if (!cellular_.stop_requested.load() &&
                 network_time_poll_elapsed_ms >=
                     kCellularNetworkTimePollMs) {
        response.clear();
        std::string network_time;
        const bool network_time_command_ok =
            send_command("AT+CCLK?", &response);
        snapshot.network_time_ready = network_time_command_ok &&
                                      ParseCellularNetworkTime(
                                          response, &network_time);
        if (!snapshot.network_time_ready && !network_time_error_reported) {
          log_command_error(
              network_time_command_ok
                  ? "received an invalid network time response"
                  : "failed to query network time",
              "AT+CCLK?", response);
        }
        network_time_error_reported = !snapshot.network_time_ready;
        if (snapshot.network_time_ready) {
          CopyString(snapshot.network_time, sizeof(snapshot.network_time),
              network_time);
        } else {
          snapshot.network_time[0] = '\0';
        }
        network_time_poll_elapsed_ms = 0;
      }
      xSemaphoreGive(nrf9151_mutex_);
      publish_status();
    }

    for (uint32_t elapsed_ms = 0;
        elapsed_ms < kCellularStatusPollMs && !cellular_.stop_requested.load();
        elapsed_ms += 100) {
      vTaskDelay(pdMS_TO_TICKS(100));
    }
    network_time_poll_elapsed_ms =
        std::min(kCellularNetworkTimePollMs,
            network_time_poll_elapsed_ms + kCellularStatusPollMs);
  }

  if (xSemaphoreTake(nrf9151_mutex_, portMAX_DELAY) == pdTRUE) {
    if (driver_.IsNrf9151Ready() && driver_.chip().nrf9151 != nullptr) {
      std::string response;
      send_command("AT+CFUN=0", &response);
    }
    driver_.DeinitNrf9151();
    xSemaphoreGive(nrf9151_mutex_);
  }
  snapshot.enabled = false;
  snapshot.powered = false;
  publish_status();
  cellular_.task_active.store(false);
}

void TDisplayP4AirDevice::MicrophoneCaptureTaskEntry(void* context) {
  auto* self = static_cast<TDisplayP4AirDevice*>(context);
  if (self != nullptr) {
    self->RunMicrophoneCaptureTask();
  }
  vTaskDelete(nullptr);
}

void TDisplayP4AirDevice::RunMicrophoneCaptureTask() {
  std::array<int16_t, kMicrophoneReadSampleCount> samples = {};
  while (!microphone_.stop_requested.load()) {
    const size_t requested_bytes = samples.size() * sizeof(samples[0]);
    const int read_result = esp_codec_dev_read(driver_.es8389_input_codec_dev(),
        samples.data(), static_cast<int>(requested_bytes));
    const size_t read_bytes =
        read_result == ESP_CODEC_DEV_OK ? requested_bytes : 0;
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
          const int write_result =
              esp_codec_dev_write(driver_.es8389_output_codec_dev(),
                  const_cast<uint8_t*>(pcm_data + written_bytes),
                  static_cast<int>(read_bytes - written_bytes));
          const size_t written =
              write_result == ESP_CODEC_DEV_OK ? read_bytes - written_bytes : 0;
          if (written == 0) {
            LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
                "Microphone PCM loopback write failed\n");
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

void TDisplayP4AirDevice::WifiInitTaskEntry(void* context) {
  auto* self = static_cast<TDisplayP4AirDevice*>(context);
  if (self != nullptr) {
    self->RunWifiInitTask();
  }
  vTaskDelete(nullptr);
}

esp_err_t TDisplayP4AirDevice::WifiCoprocessorResetCallback(
    void* context, bool level) {
  auto* self = static_cast<TDisplayP4AirDevice*>(context);
  if (self == nullptr) {
    return ESP_ERR_INVALID_ARG;
  }

  // 关闭请求发出后拒绝 ESP-Hosted 再次拉高 EN，确保协处理器保持断电状态。
  const bool enabled = level && !self->wifi_.stop_requested.load();
  return SetWifiCoprocessorPowerEnabled(self->driver_, enabled) ? ESP_OK
                                                                : ESP_FAIL;
}

void TDisplayP4AirDevice::RunWifiInitTask() {
  if (!WaitForWifiHardwareReady()) {
    const bool stopping = wifi_.stop_requested.load();
    wifi_.init_task_running.store(false);
    if (stopping) {
      SetWifiEnabled(false);
      return;
    }
    LogMessage(
        LogLevel::kWarning, __FILE__, __LINE__, "WiFi hardware is not ready\n");
    SetWifiEnabled(false);
    SetWifiFailure(ESP_ERR_TIMEOUT);
    return;
  }

  const int result = InitializeWifiStack();
  if (result != ESP_OK) {
    const bool stopping = wifi_.stop_requested.load();
    wifi_.init_task_running.store(false);
    SetWifiEnabled(false);
    if (stopping) {
      return;
    }
    LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
        "WiFi init failed: %s (%#X)\n",
        esp_err_to_name(static_cast<esp_err_t>(result)),
        static_cast<unsigned>(result));
    SetWifiFailure(result);
    return;
  }

  wifi_.init_task_running.store(false);
  if (wifi_.stop_requested.load()) {
    SetWifiEnabled(false);
    return;
  }
  if (wifi_.scan_requested.exchange(false) && !StartWifiScan()) {
    LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
        "Start deferred WiFi scan failed\n");
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

void TDisplayP4AirDevice::WifiScanTaskEntry(void* context) {
  auto* self = static_cast<TDisplayP4AirDevice*>(context);
  if (self != nullptr) {
    self->RunWifiScanTask();
  }
  vTaskDelete(nullptr);
}

void TDisplayP4AirDevice::WifiConnectTaskEntry(void* context) {
  auto* self = static_cast<TDisplayP4AirDevice*>(context);
  if (self != nullptr) {
    self->RunWifiConnectTask();
  }
  vTaskDelete(nullptr);
}

void TDisplayP4AirDevice::RunWifiScanTask() {
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

void TDisplayP4AirDevice::RunWifiConnectTask() {
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

  const esp_err_t config_result =
      esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
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

bool TDisplayP4AirDevice::WaitForWifiHardwareReady() {
  uint32_t elapsed_ms = 0;
  while (!wifi_.stop_requested.load() && !driver_.IsXl9535Ready() &&
         elapsed_ms < kWifiHardwareReadyTimeoutMs) {
    vTaskDelay(pdMS_TO_TICKS(kWifiHardwareReadyPollMs));
    elapsed_ms += kWifiHardwareReadyPollMs;
  }

  if (wifi_.stop_requested.load() || !driver_.IsXl9535Ready()) {
    return false;
  }

  for (elapsed_ms = 0;
      elapsed_ms < kWifiCoprocessorBootDelayMs &&
      !wifi_.stop_requested.load();
      elapsed_ms += kWifiHardwareReadyPollMs) {
    const uint32_t delay_ms = std::min(kWifiHardwareReadyPollMs,
        kWifiCoprocessorBootDelayMs - elapsed_ms);
    vTaskDelay(pdMS_TO_TICKS(delay_ms));
  }
  return !wifi_.stop_requested.load();
}

int TDisplayP4AirDevice::InitializeWifiStack() {
  if (wifi_.driver_initialized.load()) {
    return PrepareWifiStation();
  }

  if (wifi_.stop_requested.load()) {
    return ESP_ERR_INVALID_STATE;
  }

  if (!wifi_.hosted_bridge_initialized.load()) {
    const esp_hosted_transport_err_t reset_callback_result =
        esp_hosted_sdio_set_reset_callback(
            WifiCoprocessorResetCallback, this);
    if (reset_callback_result != ESP_TRANSPORT_OK) {
      return static_cast<int>(reset_callback_result);
    }
    if (wifi_.stop_requested.load()) {
      return ESP_ERR_INVALID_STATE;
    }
    const esp_err_t hosted_result = esp_hosted_init();
    if (hosted_result != ESP_OK && hosted_result != ESP_ERR_INVALID_STATE) {
      return hosted_result;
    }
    wifi_.hosted_bridge_initialized.store(true);
    if (wifi_.stop_requested.load()) {
      return ESP_ERR_INVALID_STATE;
    }
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
  // 账号密码由 ESP32-P4 侧管理，C5 只接收 RAM 中的临时 WiFi 配置。
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

int TDisplayP4AirDevice::PrepareWifiStation() {
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

void TDisplayP4AirDevice::CopyWifiScanResultsFromDriver() {
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
  for (uint16_t i = 0;
      i < record_count && network_count < kMaxWifiScanNetworkCount; ++i) {
    const auto* ssid = reinterpret_cast<const char*>(records[i].ssid);
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

int TDisplayP4AirDevice::StartWifiTimeTestInternal() {
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

int TDisplayP4AirDevice::StartWifiSntp() {
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

int TDisplayP4AirDevice::StartWifiSntpAttemptTimer() {
  if (wifi_time_test_.sntp_attempt_timer == nullptr) {
    esp_timer_create_args_t timer_config = {};
    timer_config.callback = WifiSntpAttemptTimerCallback;
    timer_config.arg = this;
    timer_config.dispatch_method = ESP_TIMER_TASK;
    timer_config.name = "sntp_attempt";
    const esp_err_t create_result =
        esp_timer_create(&timer_config, &wifi_time_test_.sntp_attempt_timer);
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
             ? esp_timer_restart(
                   wifi_time_test_.sntp_attempt_timer, interval_us)
             : esp_timer_start_periodic(
                   wifi_time_test_.sntp_attempt_timer, interval_us);
}

void TDisplayP4AirDevice::WifiSntpAttemptTimerCallback(void* argument) {
  auto* self = static_cast<TDisplayP4AirDevice*>(argument);
  if (self == nullptr) {
    return;
  }

  const int attempt_count = self->wifi_time_test_.sntp_attempt_count.load();
  if (self->wifi_time_test_.synced.load() || !self->wifi_.got_ip.load() ||
      attempt_count >= kWifiSntpMaxAttemptCount) {
    self->StopWifiInternetCheck();
    return;
  }

  self->wifi_time_test_.sntp_attempt_count.store(attempt_count + 1);
  if (!esp_sntp_enabled() || !esp_sntp_restart()) {
    self->StopWifiInternetCheck();
  }
}

void TDisplayP4AirDevice::SetWifiFailure(int error) {
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

void TDisplayP4AirDevice::WifiEventHandler(
    void* arg, const char* event_base, int32_t event_id, void* event_data) {
  (void)event_base;
  auto* self = static_cast<TDisplayP4AirDevice*>(arg);
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

void TDisplayP4AirDevice::WifiGotIpEventHandler(
    void* arg, const char* event_base, int32_t event_id, void* event_data) {
  (void)event_base;
  (void)event_id;
  auto* self = static_cast<TDisplayP4AirDevice*>(arg);
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

bool TDisplayP4AirDevice::ReadDeviceDiagnostics(
    DeviceDiagnostics* diagnostics) {
  if (diagnostics == nullptr) {
    return false;
  }

  *diagnostics = DeviceDiagnostics();
  const bool battery_management_result =
      ReadBatteryManagementStatus(&diagnostics->battery_management);
  const bool imu_result = ReadImuStatus(&diagnostics->imu);
  return battery_management_result || imu_result;
}

bool TDisplayP4AirDevice::ReadBatteryManagementStatus(
    BatteryManagementStatus* status) {
  if (status == nullptr) {
    return false;
  }

  *status = BatteryManagementStatus();

  if (!driver_.IsAxp517Ready() || driver_.chip().axp517 == nullptr) {
    return false;
  }

  auto& axp517 = *driver_.chip().axp517;
  cpp_bus_driver::Axp517::ChipStatus0 chip_status0;
  cpp_bus_driver::Axp517::ChipStatus1 chip_status1;
  if (!axp517.GetChipStatus0(chip_status0) ||
      !axp517.GetChipStatus1(chip_status1)) {
    return false;
  }

  const uint16_t voltage_mv = axp517.GetBatteryVoltage();
  const uint8_t charge_percent = axp517.GetBatteryLevel();
  const uint8_t health_percent = axp517.GetBatteryHealth();
  const float current_ma = axp517.GetBatteryCurrent();

  status->ready = true;
  status->pack_present = chip_status0.battery_present_status;
  status->charging = status->pack_present && chip_status0.vbus_good_indication &&
                     (chip_status1.charging_status ==
                             cpp_bus_driver::Axp517::ChargeStatus::kTrickleCharge ||
                         chip_status1.charging_status ==
                             cpp_bus_driver::Axp517::ChargeStatus::kPrecharge ||
                         chip_status1.charging_status ==
                             cpp_bus_driver::Axp517::ChargeStatus::kConstantCurrent ||
                         chip_status1.charging_status ==
                             cpp_bus_driver::Axp517::ChargeStatus::kConstantVoltage ||
                         chip_status1.charging_status ==
                             cpp_bus_driver::Axp517::ChargeStatus::kChargeDone);
  status->full_charged =
      chip_status1.charging_status ==
          cpp_bus_driver::Axp517::ChargeStatus::kChargeDone ||
      charge_percent == 100;
  status->full_discharged = status->pack_present && charge_percent == 0;
  status->voltage_mv = voltage_mv;
  status->current_ma = static_cast<int>(std::lround(current_ma));
  status->charge_percent = charge_percent;
  status->health_percent = health_percent;
  status->pack_temperature_c = axp517.GetBatteryTemperatureCelsius();
  if (axp517.SetAdcDataSelect(
          cpp_bus_driver::Axp517::AdcData::kChipTemperatureCelsius)) {
    status->chip_temperature_c = axp517.GetChipDieJunctionTemperatureCelsius();
  }
  return true;
}

bool TDisplayP4AirDevice::ReadBatteryLevel(int* percent) {
  if (percent == nullptr || !driver_.IsAxp517Ready() ||
      driver_.chip().axp517 == nullptr) {
    return false;
  }

  cpp_bus_driver::Axp517::ChipStatus0 chip_status;
  if (!driver_.chip().axp517->GetChipStatus0(chip_status) ||
      !chip_status.battery_present_status) {
    return false;
  }
  const uint8_t charge_percent = driver_.chip().axp517->GetBatteryLevel();
  if (charge_percent > 100) {
    return false;
  }
  *percent = charge_percent;
  return true;
}

bool TDisplayP4AirDevice::SetOtgPowerEnabled(bool enabled) {
  if (otg_.mutex == nullptr ||
      xSemaphoreTake(otg_.mutex, pdMS_TO_TICKS(kOtgMutexTimeoutMs)) != pdTRUE) {
    return false;
  }

  auto* axp517 =
      driver_.IsAxp517Ready() ? driver_.chip().axp517.get() : nullptr;
  bool result = axp517 != nullptr || !enabled;
  if (axp517 == nullptr) {
    otg_.source_role_enabled = false;
    otg_.power_output_enabled = false;
  } else if (!enabled) {
    otg_.source_role_enabled = false;
    result = SetOtgPowerOutputEnabledLocked(false);
    result &= axp517->SetVbusDetectEnable(true);
    result &= axp517->SetPdRole(false, false);
  } else {
    bool external_power_present = false;
    result = ReadExternalPowerPresentLocked(&external_power_present);
    if (result && external_power_present) {
      otg_.source_role_enabled = false;
      bool safe_state_result = SetOtgPowerOutputEnabledLocked(false);
      safe_state_result &= axp517->SetPdRole(false, false);
      if (!safe_state_result) {
        LogMessage(LogLevel::kError, __FILE__, __LINE__,
            "Keep OTG disabled while external power is present failed\n");
      }
      result = false;
    } else if (result) {
      result = SetOtgPowerOutputEnabledLocked(false);
      result &= axp517->SetVbusDetectEnable(true);
      result &= axp517->SetPdRole(true, true);
      if (result) {
        otg_.source_role_enabled = true;
        result = UpdateOtgPowerStateLocked();
      } else {
        otg_.source_role_enabled = false;
        SetOtgPowerOutputEnabledLocked(false);
        axp517->SetPdRole(false, false);
      }
    }
  }

  xSemaphoreGive(otg_.mutex);
  return result;
}

bool TDisplayP4AirDevice::UpdateOtgPowerState() {
  if (otg_.mutex == nullptr ||
      xSemaphoreTake(otg_.mutex, pdMS_TO_TICKS(kOtgMutexTimeoutMs)) != pdTRUE) {
    return false;
  }
  const bool result = driver_.IsAxp517Ready() &&
                      driver_.chip().axp517 != nullptr &&
                      UpdateOtgPowerStateLocked();
  xSemaphoreGive(otg_.mutex);
  return result;
}

bool TDisplayP4AirDevice::ReadExternalPowerPresent(bool* present) {
  if (present == nullptr || otg_.mutex == nullptr ||
      xSemaphoreTake(otg_.mutex, pdMS_TO_TICKS(kOtgMutexTimeoutMs)) != pdTRUE) {
    return false;
  }
  const bool result = driver_.IsAxp517Ready() &&
                      driver_.chip().axp517 != nullptr &&
                      ReadExternalPowerPresentLocked(present);
  xSemaphoreGive(otg_.mutex);
  return result;
}

bool TDisplayP4AirDevice::SetOtgPowerOutputEnabledLocked(bool enabled) {
  auto* axp517 = driver_.chip().axp517.get();
  if (axp517 == nullptr) {
    return false;
  }
  if (otg_.power_output_enabled == enabled) {
    return true;
  }

  if (enabled) {
    if (!axp517->SetBoostEnable(true) ||
        !axp517->SetForceRbfetEnable(true)) {
      axp517->SetForceRbfetEnable(false);
      axp517->SetBoostEnable(false);
      otg_.power_output_enabled = false;
      return false;
    }
  } else {
    bool result = axp517->SetForceRbfetEnable(false);
    result &= axp517->SetBoostEnable(false);
    if (!result) {
      return false;
    }
  }

  otg_.power_output_enabled = enabled;
  LogMessage(LogLevel::kInfo, __FILE__, __LINE__,
      enabled ? "OTG reverse-power output enabled\n"
              : "OTG reverse-power output disabled\n");
  return true;
}

bool TDisplayP4AirDevice::ReadExternalPowerPresentLocked(bool* present) {
  auto* axp517 = driver_.chip().axp517.get();
  if (axp517 == nullptr) {
    return false;
  }
  cpp_bus_driver::Axp517::PdConnectionStatus connection_status;
  cpp_bus_driver::Axp517::ChipStatus0 chip_status;
  if (!axp517->GetPdConnectionStatus(connection_status) ||
      !axp517->GetChipStatus0(chip_status)) {
    return false;
  }
  *present = connection_status.sink_power_attached ||
             (!otg_.power_output_enabled &&
                 chip_status.vbus_good_indication);
  return true;
}

bool TDisplayP4AirDevice::UpdateOtgPowerStateLocked() {
  auto* axp517 = driver_.chip().axp517.get();
  if (axp517 == nullptr) {
    return false;
  }
  if (!otg_.source_role_enabled) {
    return SetOtgPowerOutputEnabledLocked(false);
  }

  cpp_bus_driver::Axp517::PdConnectionStatus connection_status;
  cpp_bus_driver::Axp517::ChipStatus0 chip_status;
  if (!axp517->GetPdConnectionStatus(connection_status) ||
      !axp517->GetChipStatus0(chip_status)) {
    return false;
  }
  const bool external_power_present =
      connection_status.sink_power_attached ||
      (!otg_.power_output_enabled && chip_status.vbus_good_indication);
  if (external_power_present) {
    otg_.source_role_enabled = false;
    bool result = SetOtgPowerOutputEnabledLocked(false);
    result &= axp517->SetPdRole(false, false);
    return result;
  }
  if (connection_status.looking_for_connection) {
    return SetOtgPowerOutputEnabledLocked(false);
  }
  if (connection_status.source_device_attached) {
    return SetOtgPowerOutputEnabledLocked(true);
  }
  if (!SetOtgPowerOutputEnabledLocked(false)) {
    return false;
  }
  return axp517->SetPdRole(true, true);
}

bool TDisplayP4AirDevice::ReadRadioCapabilities(
    RadioCapabilities* capabilities) {
  if (capabilities == nullptr) {
    return false;
  }
  *capabilities = RadioCapabilities();
  RadioCapability& capability = capabilities->entries[0];
  capability.chip = radio::ChipType::kLr1121;
  capability.protocol = radio::ProtocolType::kLora;
  capability.maximum_payload_size = kRadioPayloadCapacity;
  capability.frequency_bands[0] = {
      .minimum_hz = 150000000U,
      .maximum_hz = 960000000U,
  };
  capability.frequency_bands[1] = {
      .minimum_hz = 2400000000U,
      .maximum_hz = 2500000000U,
  };
  capability.frequency_band_count = 2;
  capabilities->count = 1;
  capabilities->supports_external_antenna = false;
  return true;
}

bool TDisplayP4AirDevice::ActivateRadio(const RadioConfig& config) {
  if (radio_.mutex == nullptr ||
      xSemaphoreTake(radio_.mutex, pdMS_TO_TICKS(50)) != pdTRUE) {
    LogMessage(LogLevel::kError, __FILE__, __LINE__,
        "Radio activate failed: mutex unavailable, profile=%lu\n",
        static_cast<unsigned long>(config.client_token));
    return false;
  }
  usp_cpp_bus_driver::Lr11xx::LoraConfig driver_config;
  bool result = config.chip == radio::ChipType::kLr1121 &&
                config.protocol == radio::ProtocolType::kLora &&
                config.antenna == radio::AntennaType::kInternal &&
                BuildRadioConfig(config.lora, &driver_config);
  if (result && driver_.SetLr1121OperatingMode(
                    TDisplayP4AirBoardDriver::Lr1121OperatingMode::kStandby)) {
    auto* radio = driver_.chip().lr1121.get();
    result = radio != nullptr &&
             EnsureLr1121ImageCalibration(*radio, config.lora.frequency_hz,
                 &radio_.calibrated_image_minimum_mhz,
                 &radio_.calibrated_image_maximum_mhz) &&
             radio->Configure(driver_config) &&
             radio->Invoke(lr11xx_system_clear_irq_status,
                 LR11XX_SYSTEM_IRQ_ALL_MASK) == LR11XX_STATUS_OK &&
             radio->Invoke(lr11xx_system_set_dio_irq_params, kRadioEventIrqMask,
                 LR11XX_SYSTEM_IRQ_NONE) == LR11XX_STATUS_OK &&
             StartLr1121Receive(*radio, config.lora);
  } else {
    result = false;
  }
  if (!result) {
    driver_.SetLr1121OperatingMode(
        TDisplayP4AirBoardDriver::Lr1121OperatingMode::kSleep);
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
      config.antenna == radio::AntennaType::kExternal ? "external"
                                                      : "internal");
  return result;
}

bool TDisplayP4AirDevice::DeactivateRadio() {
  if (radio_.mutex == nullptr ||
      xSemaphoreTake(radio_.mutex, pdMS_TO_TICKS(50)) != pdTRUE) {
    LogMessage(LogLevel::kError, __FILE__, __LINE__,
        "Radio deactivate failed: mutex unavailable\n");
    return false;
  }
  bool result = true;
  if (driver_.IsLr1121Ready()) {
    auto* radio = driver_.chip().lr1121.get();
    if (radio_.active) {
      result = radio != nullptr &&
               radio->Invoke(lr11xx_system_set_standby,
                   LR11XX_SYSTEM_STANDBY_CFG_RC) == LR11XX_STATUS_OK &&
               radio->Invoke(lr11xx_system_clear_irq_status,
                   LR11XX_SYSTEM_IRQ_ALL_MASK) == LR11XX_STATUS_OK;
    }
    result &= driver_.SetLr1121OperatingMode(
        TDisplayP4AirBoardDriver::Lr1121OperatingMode::kStandby);
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

bool TDisplayP4AirDevice::SendRadio(
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
  const bool hardware_ready = driver_.IsLr1121Ready();
  constexpr const char* kRadioChipName = "LR1121";
  if (!hardware_ready) {
    radio_.chip_error = true;
    LogMessage(LogLevel::kError, __FILE__, __LINE__,
        "Radio send rejected: %s is unavailable, message=%lu\n", kRadioChipName,
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
  auto* radio = driver_.chip().lr1121.get();
  const lr11xx_radio_pkt_params_lora_t packet_config =
      MakeLr1121PacketConfig(radio_.lora_config, static_cast<uint8_t>(size));
  const bool result = radio != nullptr &&
                      radio->Invoke(lr11xx_system_clear_irq_status,
                          LR11XX_SYSTEM_IRQ_ALL_MASK) == LR11XX_STATUS_OK &&
                      radio->Invoke(lr11xx_radio_set_lora_pkt_params,
                          &packet_config) == LR11XX_STATUS_OK &&
                      radio->WriteBuffer(data, size) &&
                      radio->StartTransmit(timing.hardware_timeout_ms);
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

bool TDisplayP4AirDevice::PollRadioEvent(RadioEvent* event) {
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
  if (!driver_.IsLr1121Ready() || driver_.chip().lr1121 == nullptr) {
    radio_.active = false;
    radio_.transmitting = false;
    radio_.chip_error = true;
    event->type = RadioEventType::kChipError;
    event->failure_reason = RadioFailureReason::kHardwareUnavailable;
    radio_.transmit_request_token = 0;
    radio_.transmit_deadline_us = 0;
    xSemaphoreGive(radio_.mutex);
    LogMessage(LogLevel::kError, __FILE__, __LINE__,
        "Radio event failed: LR1121 is unavailable, profile=%lu, "
        "message=%lu\n",
        static_cast<unsigned long>(event->client_token),
        static_cast<unsigned long>(
            static_cast<uint32_t>(event->request_token)));
    return false;
  }

  auto& lr1121 = *driver_.chip().lr1121;
  lr11xx_system_irq_mask_t irq_mask = LR11XX_SYSTEM_IRQ_NONE;
  if (lr1121.Invoke(lr11xx_system_get_irq_status, &irq_mask) !=
      LR11XX_STATUS_OK) {
    radio_.active = false;
    radio_.transmitting = false;
    radio_.chip_error = true;
    event->type = RadioEventType::kChipError;
    event->failure_reason = RadioFailureReason::kIrqReadFailed;
    radio_.transmit_request_token = 0;
    radio_.transmit_deadline_us = 0;
    xSemaphoreGive(radio_.mutex);
    LogMessage(LogLevel::kError, __FILE__, __LINE__,
        "Radio event failed: cannot read LR1121 IRQ, profile=%lu, "
        "message=%lu\n",
        static_cast<unsigned long>(event->client_token),
        static_cast<unsigned long>(
            static_cast<uint32_t>(event->request_token)));
    return false;
  }

  if (irq_mask == LR11XX_SYSTEM_IRQ_NONE) {
    if (radio_.transmitting && radio_.transmit_deadline_us > 0 &&
        esp_timer_get_time() >= radio_.transmit_deadline_us) {
      const bool recovered = StartLr1121Receive(lr1121, radio_.lora_config);
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

  const bool timed_out = (irq_mask & LR11XX_SYSTEM_IRQ_TIMEOUT) != 0;
  const bool tx_done = (irq_mask & LR11XX_SYSTEM_IRQ_TX_DONE) != 0;
  const bool rx_done = (irq_mask & LR11XX_SYSTEM_IRQ_RX_DONE) != 0;
  const bool receive_error = (irq_mask & (LR11XX_SYSTEM_IRQ_HEADER_ERROR |
                                             LR11XX_SYSTEM_IRQ_CRC_ERROR)) != 0;
  char irq_text[kRadioIrqTextCapacity] = {};
  FormatRadioIrqMask(irq_mask, irq_text, sizeof(irq_text));
  bool result = lr1121.Invoke(lr11xx_system_clear_irq_status, irq_mask) ==
                LR11XX_STATUS_OK;
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
        "Radio event failed: cannot clear IRQ %s, message=%lu\n", irq_text,
        static_cast<unsigned long>(
            static_cast<uint32_t>(event->request_token)));
    return false;
  }

  if (radio_.transmitting && (tx_done || timed_out)) {
    radio_.transmitting = false;
    radio_.transmit_request_token = 0;
    radio_.transmit_deadline_us = 0;
    const bool receive_restarted =
        StartLr1121Receive(lr1121, radio_.lora_config);
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
  } else if (radio_.transmitting) {
    LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
        "Radio send ignored unrelated IRQ %s, message=%lu\n", irq_text,
        static_cast<unsigned long>(
            static_cast<uint32_t>(event->request_token)));
  } else if (rx_done && !receive_error) {
    lr11xx_radio_rx_buffer_status_t buffer_status = {};
    lr11xx_radio_pkt_status_lora_t packet_status = {};
    result = lr1121.Invoke(lr11xx_radio_get_rx_buffer_status, &buffer_status) ==
                 LR11XX_STATUS_OK &&
             buffer_status.pld_len_in_bytes > 0 &&
             buffer_status.pld_len_in_bytes <= kRadioPayloadCapacity &&
             lr1121.ReadBuffer(buffer_status.buffer_start_pointer,
                 event->payload, buffer_status.pld_len_in_bytes) &&
             lr1121.Invoke(lr11xx_radio_get_lora_pkt_status, &packet_status) ==
                 LR11XX_STATUS_OK &&
             StartLr1121Receive(lr1121, radio_.lora_config);
    if (result) {
      event->type = RadioEventType::kPacketReceived;
      event->payload_size = buffer_status.pld_len_in_bytes;
      event->rssi_dbm = packet_status.rssi_pkt_in_dbm;
      event->snr_db = packet_status.snr_pkt_in_db;
    }
  } else {
    result = StartLr1121Receive(lr1121, radio_.lora_config);
    if (receive_error) {
      LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
          "Radio RX packet rejected: IRQ=%s\n", irq_text);
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
        static_cast<unsigned long>(static_cast<uint32_t>(event->request_token)),
        irq_text);
  }
  xSemaphoreGive(radio_.mutex);
  return result;
}

bool TDisplayP4AirDevice::ReadRadioStatus(RadioStatus* status) {
  if (status == nullptr || radio_.mutex == nullptr ||
      xSemaphoreTake(radio_.mutex, pdMS_TO_TICKS(20)) != pdTRUE) {
    return false;
  }
  status->hardware_ready = driver_.IsLr1121Ready();
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

void TDisplayP4AirDevice::Bhi260apAccelerationCallback(
    const struct bhy2_fifo_parse_data_info* callback_info, void* context) {
  auto* self = static_cast<TDisplayP4AirDevice*>(context);
  if (self == nullptr || callback_info == nullptr ||
      callback_info->data_ptr == nullptr || callback_info->data_size < 6) {
    return;
  }

  struct bhy2_data_xyz data = {};
  bhy2_parse_xyz(callback_info->data_ptr, &data);
  self->imu_.acceleration[0] = data.x * kBhi260apAccelerometerScale;
  self->imu_.acceleration[1] = data.y * kBhi260apAccelerometerScale;
  self->imu_.acceleration[2] = data.z * kBhi260apAccelerometerScale;
  self->imu_.acceleration_ready = true;
}

bool TDisplayP4AirDevice::SetImuEnabled(bool enabled) {
  if (imu_.mutex == nullptr ||
      xSemaphoreTake(imu_.mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
    LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
        "Set IMU enabled state failed: mutex unavailable\n");
    return false;
  }

  bool result = true;
  if (!enabled) {
    if (imu_.configured && driver_.IsBhi260apReady() &&
        driver_.chip().bhi260ap != nullptr) {
      result &= driver_.chip().bhi260ap->ConfigureSensor(
          BHY2_SENSOR_ID_ACC_PASS, 0.0F, kBhi260apReportLatencyMs);
    }
    result &= driver_.SetBhi260apSleep(true);
    result &= driver_.SetQmc6310nSleep(true);
    imu_.configured = false;
    imu_.acceleration_ready = false;
    imu_.magnetic_field_ready = false;
    imu_enabled_.store(false);
    xSemaphoreGive(imu_.mutex);
    return result;
  }

  if (imu_enabled_.load() && imu_.configured &&
      driver_.IsBhi260apReady() && driver_.IsQmc6310nReady() &&
      driver_.chip().bhi260ap != nullptr &&
      driver_.chip().qmc6310n != nullptr) {
    xSemaphoreGive(imu_.mutex);
    return true;
  }

  uint32_t elapsed_ms = 0;
  while (driver_.chip().bhi260ap != nullptr &&
         driver_.chip().qmc6310n != nullptr &&
         (!driver_.IsBhi260apReady() || !driver_.IsQmc6310nReady()) &&
         elapsed_ms < kImuHardwareReadyTimeoutMs) {
    vTaskDelay(pdMS_TO_TICKS(kImuHardwareReadyPollMs));
    elapsed_ms += kImuHardwareReadyPollMs;
  }
  if (!driver_.IsBhi260apReady() || !driver_.IsQmc6310nReady() ||
      driver_.chip().bhi260ap == nullptr ||
      driver_.chip().qmc6310n == nullptr) {
    LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
        "Enable IMU failed: BHI260AP ready=%u, QMC6310N ready=%u\n",
        static_cast<unsigned int>(driver_.IsBhi260apReady()),
        static_cast<unsigned int>(driver_.IsQmc6310nReady()));
    xSemaphoreGive(imu_.mutex);
    return false;
  }

  auto& bhi260ap = *driver_.chip().bhi260ap;
  result = driver_.SetBhi260apSleep(false);
  if (!result) {
    LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
        "Enable IMU failed: wake BHI260AP failed\n");
  }
  if (result) {
    result = driver_.SetQmc6310nSleep(false);
    if (!result) {
      LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
          "Enable IMU failed: wake QMC6310N failed\n");
    }
  }
  if (result) {
    result = bhi260ap.RegisterFifoCallback(
        BHY2_SENSOR_ID_ACC_PASS, Bhi260apAccelerationCallback, this);
    if (!result) {
      LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
          "Enable IMU failed: register BHI260AP FIFO callback failed "
          "(error code: %d)\n",
          static_cast<int>(bhi260ap.last_error()));
    }
  }
  if (result) {
    result = bhi260ap.ProcessFifo();
    if (!result) {
      LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
          "Enable IMU failed: process BHI260AP FIFO failed "
          "(error code: %d)\n",
          static_cast<int>(bhi260ap.last_error()));
    }
  }
  if (result) {
    result = bhi260ap.UpdateVirtualSensorList();
    if (!result) {
      LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
          "Enable IMU failed: update BHI260AP virtual sensor list failed "
          "(error code: %d)\n",
          static_cast<int>(bhi260ap.last_error()));
    }
  }
  if (result) {
    result = bhi260ap.ConfigureSensor(BHY2_SENSOR_ID_ACC_PASS,
        kBhi260apSampleRateHz, kBhi260apReportLatencyMs);
    if (!result) {
      LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
          "Enable IMU failed: configure BHI260AP accelerometer failed "
          "(error code: %d)\n",
          static_cast<int>(bhi260ap.last_error()));
    }
  }
  imu_.configured = result;
  imu_.acceleration_ready = false;
  imu_.magnetic_field_ready = false;
  imu_enabled_.store(result);
  if (!result) {
    driver_.SetBhi260apSleep(true);
    driver_.SetQmc6310nSleep(true);
  }
  xSemaphoreGive(imu_.mutex);
  return result;
}

bool TDisplayP4AirDevice::ReadImuStatus(ImuStatus* status) {
  if (status == nullptr) {
    return false;
  }

  *status = ImuStatus();

  if (!imu_enabled_.load() || !imu_.configured) {
    return false;
  }
  if (!driver_.IsBhi260apReady() || !driver_.IsQmc6310nReady() ||
      driver_.chip().bhi260ap == nullptr ||
      driver_.chip().qmc6310n == nullptr) {
    LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
        "Read IMU status failed: BHI260AP ready=%u, QMC6310N ready=%u\n",
        static_cast<unsigned int>(driver_.IsBhi260apReady()),
        static_cast<unsigned int>(driver_.IsQmc6310nReady()));
    return false;
  }
  if (imu_.mutex == nullptr ||
      xSemaphoreTake(imu_.mutex, pdMS_TO_TICKS(20)) != pdTRUE) {
    return false;
  }

  bool result = driver_.chip().bhi260ap->ProcessFifo();
  if (!result) {
    LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
        "Read IMU status failed: process BHI260AP FIFO failed "
        "(error code: %d)\n",
        static_cast<int>(driver_.chip().bhi260ap->last_error()));
  }
  MagnetometerData magnetic_data;
  if (driver_.chip().qmc6310n->readData(magnetic_data)) {
    imu_.magnetic_field[0] = magnetic_data.magnetic_field.x;
    imu_.magnetic_field[1] = magnetic_data.magnetic_field.y;
    imu_.magnetic_field[2] = magnetic_data.magnetic_field.z;
    imu_.magnetic_field_ready = true;
  } else if (!imu_.magnetic_field_ready) {
    result = false;
    LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
        "Read IMU status failed: QMC6310N has no valid data\n");
  }

  if (!result || !imu_.acceleration_ready ||
      !imu_.magnetic_field_ready) {
    xSemaphoreGive(imu_.mutex);
    return false;
  }

  const float acceleration_z = -imu_.acceleration[2];
  const float pitch =
      std::atan2(-imu_.acceleration[0],
          std::sqrt(imu_.acceleration[1] * imu_.acceleration[1] +
                    acceleration_z * acceleration_z)) *
      kRadiansToDegrees;
  const float roll =
      std::atan2(imu_.acceleration[1], acceleration_z) * kRadiansToDegrees;
  const float pitch_radians = pitch * kDegreesToRadians;
  const float roll_radians = roll * kDegreesToRadians;
  const float magnetic_x_horizontal =
      imu_.magnetic_field[0] * std::cos(pitch_radians) +
      imu_.magnetic_field[2] * std::sin(pitch_radians);
  const float magnetic_y_horizontal =
      imu_.magnetic_field[0] * std::sin(roll_radians) *
          std::sin(pitch_radians) -
      imu_.magnetic_field[2] * std::sin(roll_radians) *
          std::cos(pitch_radians) +
      imu_.magnetic_field[1] * std::cos(roll_radians);
  float yaw = std::atan2(magnetic_y_horizontal, magnetic_x_horizontal) *
              kRadiansToDegrees;
  if (yaw < 0.0F) {
    yaw += 360.0F;
  }

  status->ready = true;
  status->pitch_deg = pitch;
  status->yaw_deg = yaw;
  status->roll_deg = roll;
  xSemaphoreGive(imu_.mutex);
  return true;
}

bool TDisplayP4AirDevice::SetScreenBrightnessPercent(int percent) {
  if (!WaitForScreenReady()) {
    return false;
  }

  const int clamped_percent = ClampScreenBrightnessPercent(percent);
  switch (driver_.screen_type()) {
    case device::ScreenType::kHi8561:
      if (driver_.IsSy7200aReady()) {
        const cpp_bus_driver::Pwm::DutyCycle duty =
            ScreenBrightnessPercentToHi8561DutyCycle(clamped_percent);
        return driver_.chip().sy7200a->SetDuty(duty);
      }
      break;
    default:
      break;
  }
  return false;
}

bool TDisplayP4AirDevice::FadeScreenBrightnessPercent(
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
      if (driver_.IsSy7200aReady()) {
        const cpp_bus_driver::Pwm::DutyCycle target_duty =
            ScreenBrightnessPercentToHi8561DutyCycle(clamped_percent);
        if (driver_.chip().sy7200a->FadeTo(target_duty, duration_ms,
                cpp_bus_driver::Pwm::FadeMode::kWaitForCompletion)) {
          return true;
        }
      }
      break;
    default:
      break;
  }
  return false;
}

bool TDisplayP4AirDevice::EnterDeviceSleep(bool deep_sleep) {
  if (!deep_sleep && !WaitForScreenReady()) {
    return false;
  }
  if (!deep_sleep) {
    touch_gesture_wake_enabled_ =
        driver_.IsHi8561TouchReady() &&
        driver_.chip().hi8561_touch->SetGestureWakeEnabled(true);
    const bool screen_slept = driver_.SetScreenSleep(true);
    if (!screen_slept && touch_gesture_wake_enabled_) {
      driver_.chip().hi8561_touch->SetGestureWakeEnabled(false);
      touch_gesture_wake_enabled_ = false;
    }
    return screen_slept;
  }

  const bool prepared = PrepareForPowerOff();
  if (!prepared) {
    LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
        "Prepare device for power off failed\n");
    return false;
  }
  if (!driver_.PrepareDriversForPowerOff()) {
    return false;
  }
  return driver_.PrepareMinimalDriversForPowerOff();
}

bool TDisplayP4AirDevice::ExitDeviceSleep(bool deep_sleep) {
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
  return WaitForScreenReady();
}

bool TDisplayP4AirDevice::PrepareForPowerOff() {
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
  if (microphone_.running.load() || microphone_.adc_to_dac_enabled.load()) {
    result &= StopMicrophone();
  }
  if (camera_preview_.task_active.load() ||
      camera_preview_.initialized.load()) {
    result &= StopCameraPreview();
  }
  if (radio_.active || radio_.transmitting) {
    result &= DeactivateRadio();
  }
  result &= SetNfcPollingEnabled(false);
  result &= SetInfraredReceiverEnabled(false);
  result &= SetCellularEnabled(false);
  result &= SetGpsEnabled(false);
  result &= SetImuEnabled(false);
  result &= SetWifiEnabled(false);
  result &= SetOtgPowerEnabled(false);
  result &= StopUsbStorage();
  result &= WaitForPowerOffTasks();
  return result;
}

bool TDisplayP4AirDevice::WaitForPowerOffTasks() {
  for (int elapsed_ms = 0; elapsed_ms < kPowerOffTaskTimeoutMs;
      elapsed_ms += kPowerOffTaskPollMs) {
    const bool tasks_running =
        speaker_.running.load() || haptic_.running.load() ||
        microphone_.running.load() || camera_preview_.task_active.load() ||
        nfc_.task_active.load() || cellular_.task_active.load() ||
        wifi_.init_task_running.load() || wifi_.scan_task_running.load() ||
        wifi_.connect_task_running.load();
    if (!tasks_running) {
      return true;
    }
    vTaskDelay(pdMS_TO_TICKS(kPowerOffTaskPollMs));
  }
  return false;
}

bool TDisplayP4AirDevice::WaitForScreenReady() {
  for (int elapsed_ms = 0; elapsed_ms < kScreenReadyTimeoutMs;
      elapsed_ms += kScreenReadyPollMs) {
    if (driver_.IsScreenReady()) {
      return true;
    }
    vTaskDelay(pdMS_TO_TICKS(kScreenReadyPollMs));
  }
  return driver_.IsScreenReady();
}

bool TDisplayP4AirDevice::WaitForTouchReady() {
  for (int elapsed_ms = 0; elapsed_ms < kScreenReadyTimeoutMs;
      elapsed_ms += kScreenReadyPollMs) {
    if (IsTouchReady(driver_)) {
      return true;
    }
    vTaskDelay(pdMS_TO_TICKS(kScreenReadyPollMs));
  }
  return IsTouchReady(driver_);
}

}  // namespace lilygo_box::hal
