/*
 * @Description: None
 * @Author: LILYGO_L
 * @Date: 2026-05-10 13:27:05
 * @LastEditTime: 2026-05-31 22:35:02
 * @License: GPL 3.0
 */
#include "hal/device/t_display_p4/t_display_p4_device.h"

#include <sys/time.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <new>
#include <string>

#include "audio/new_notification_010_c2_b16_s44100.h"
#include "base/logger.h"
#include "esp_err.h"
#include "esp_eth.h"
#include "esp_eth_mac.h"
#include "esp_eth_phy_802_3.h"
#include "esp_event.h"
#include "esp_hosted.h"
#include "esp_lcd_mipi_dsi.h"
#include "esp_netif.h"
#include "esp_sntp.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "esp_wifi_default.h"
#include "esp_wifi_remote.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace lilygo_box::hal {
namespace device = lilygo_device_driver::t_display_p4::device;
namespace gpio = lilygo_device_driver::t_display_p4::gpio;
namespace {

constexpr uint8_t kVibrationTestGain = 255;
constexpr uint8_t kVibrationTestLoopCount = 15;
constexpr uint32_t kVibrationTestPlayMs = 220;
constexpr uint32_t kVibrationTestStopMs = 180;
constexpr size_t kSpeakerPlaybackChunkBytes = 4096;
constexpr uint32_t kSpeakerPlaybackTaskStackBytes = 4 * 1024;
constexpr UBaseType_t kSpeakerPlaybackTaskPriority = 3;
constexpr uint32_t kSpeakerPlaybackSampleRateHz = 44100;
constexpr uint8_t kSpeakerPlaybackChannelCount = 2;
constexpr uint8_t kSpeakerPlaybackBitsPerSample = 16;
constexpr uint32_t kMicrophoneCaptureTaskStackBytes = 4 * 1024;
constexpr UBaseType_t kMicrophoneCaptureTaskPriority = 3;
constexpr size_t kMicrophoneReadSampleCount = 128;
constexpr uint32_t kMicrophoneReadDelayMs = 40;
constexpr int kMicrophoneLevelFullScale = 1000;
constexpr int kMicrophoneLevelRiseDivisor = 4;
constexpr int kMicrophoneLevelFallDivisor = 8;
constexpr size_t kGpsMaxReadBufferBytes = 4096;
constexpr uint32_t kEthernetInitTaskStackBytes = 6 * 1024;
constexpr UBaseType_t kEthernetInitTaskPriority = 3;
constexpr uint32_t kWifiInitTaskStackBytes = 6 * 1024;
constexpr UBaseType_t kWifiInitTaskPriority = 3;
constexpr uint32_t kWifiScanTaskStackBytes = 6 * 1024;
constexpr UBaseType_t kWifiScanTaskPriority = 3;
constexpr uint32_t kWifiHardwareReadyTimeoutMs = 8000;
constexpr uint32_t kWifiHardwareReadyPollMs = 50;
constexpr uint32_t kWifiEsp32c6BootDelayMs = 500;
constexpr uint32_t kWifiDisconnectSettleDelayMs = 180;
constexpr uint32_t kWifiStartSettleDelayMs = 600;
constexpr uint32_t kWifiScanTimeoutMs = 8000;
constexpr const char* kFactoryWifiSsid = "LilyGo-AABB";
constexpr const char* kFactoryWifiPassword = "xinyuandianzi";
constexpr const char* kWifiSntpServer = "pool.ntp.org";
constexpr int kWifiMaxReconnectCount = 8;
constexpr int64_t kWifiValidUnixTimeThreshold = 1700000000LL;
constexpr uint32_t kWifiSntpSyncIntervalMs = 20 * 1000;

// 当前接收 SNTP 同步回调的设备实例
std::atomic<TDisplayP4Device*> g_wifi_time_sync_owner{nullptr};

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

}  // namespace

TDisplayP4Device::TDisplayP4Device()
    : driver_(lilygo_device_driver::TDisplayP4Driver::GetInstance()) {}

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

/**
 * @brief 读取当前 T-Display-P4 设备信息
 * @param info 设备信息输出地址
 * @return 读取成功返回 true，否则返回 false
 */
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

bool TDisplayP4Device::StartEthernet() {
  if (ethernet_.driver_initialized.load()) {
    if (!ethernet_.running.load() && ethernet_.handle != nullptr) {
      const esp_err_t result =
          esp_eth_start(reinterpret_cast<esp_eth_handle_t>(ethernet_.handle));
      if (result != ESP_OK && result != ESP_ERR_INVALID_STATE) {
        SetEthernetFailure(result);
        return false;
      }
      ethernet_.running.store(true);
      ethernet_.start_failed.store(false);
      ethernet_.last_error.store(ESP_OK);
    }
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

bool TDisplayP4Device::StartWifi() {
  if (wifi_.driver_initialized.load()) {
    return true;
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
    return false;
  }
  return true;
}

/**
 * @brief 停止 hosted WiFi 并重置面向 UI 的扫描和连接状态
 * @return 停止流程完成或 WiFi 已关闭返回 true
 */
bool TDisplayP4Device::StopWifi() {
  wifi_time_test_.requested.store(false);
  if (!wifi_.driver_initialized.load()) {
    wifi_.scan_running.store(false);
    wifi_.scan_task_running.store(false);
    wifi_.scan_started_tick.store(0);
    wifi_.running.store(false);
    wifi_.connected.store(false);
    wifi_.got_ip.store(false);
    return true;
  }

  if (wifi_time_test_.active.load()) {
    StopWifiTimeTest();
  }

  if (wifi_.scan_task_running.load()) {
    // 扫描期间停止 hosted WiFi 容易触发 SDIO 超时，先只收敛 P4 状态。
    wifi_.scan_running.store(false);
    wifi_.scan_started_tick.store(0);
    wifi_.running.store(false);
    wifi_.connected.store(false);
    wifi_.got_ip.store(false);
    wifi_.start_failed.store(false);
    wifi_.last_error.store(ESP_OK);
    wifi_.disconnect_reason.store(0);
    wifi_.retry_count.store(0);
    wifi_.scan_failed.store(false);
    wifi_.scan_network_count.store(0);
    wifi_.scan_generation.fetch_add(1);
    wifi_.scan_timeout_handled.store(false);
    wifi_.ip_address.store(0);
    wifi_.netmask.store(0);
    wifi_.gateway.store(0);
    return true;
  }

  esp_wifi_disconnect();
  wifi_config_t empty_config = {};
  esp_wifi_set_config(WIFI_IF_STA, &empty_config);
  esp_err_t result = esp_wifi_stop();
  if (result != ESP_OK && result != ESP_ERR_WIFI_NOT_STARTED &&
      result != ESP_ERR_INVALID_STATE && result != ESP_ERR_WIFI_STATE) {
    SetWifiFailure(result);
    return false;
  }

  result = esp_wifi_set_mode(WIFI_MODE_NULL);
  if (result != ESP_OK && result != ESP_ERR_INVALID_STATE &&
      result != ESP_ERR_WIFI_STATE) {
    SetWifiFailure(result);
    return false;
  }

  wifi_.running.store(false);
  wifi_.connected.store(false);
  wifi_.got_ip.store(false);
  wifi_.start_failed.store(false);
  wifi_.last_error.store(ESP_OK);
  wifi_.disconnect_reason.store(0);
  wifi_.retry_count.store(0);
  wifi_.scan_running.store(false);
  wifi_.scan_task_running.store(false);
  wifi_.scan_started_tick.store(0);
  wifi_.scan_failed.store(false);
  wifi_.scan_timeout_handled.store(false);
  wifi_.ip_address.store(0);
  wifi_.netmask.store(0);
  wifi_.gateway.store(0);
  return true;
}

/**
 * @brief 启动异步 STA 扫描，供设置页轮询结果
 * @return 扫描已启动、已在运行或已转入初始化流程返回 true
 */
bool TDisplayP4Device::StartWifiScan() {
  if (!wifi_.driver_initialized.load()) {
    return StartWifi();
  }

  bool expected = false;
  if (!wifi_.scan_task_running.compare_exchange_strong(expected, true)) {
    return true;
  }

  wifi_.scan_failed.store(false);
  wifi_.scan_timeout_handled.store(false);
  wifi_.last_error.store(ESP_OK);
  wifi_.scan_running.store(true);
  wifi_.scan_started_tick.store(static_cast<uint32_t>(xTaskGetTickCount()));
  const BaseType_t result = xTaskCreate(WifiScanTaskEntry, "wifi_scan",
      kWifiScanTaskStackBytes, this, kWifiScanTaskPriority, nullptr);
  if (result != pdPASS) {
    wifi_.scan_task_running.store(false);
    wifi_.scan_running.store(false);
    wifi_.scan_started_tick.store(0);
    wifi_.scan_failed.store(true);
    wifi_.last_error.store(ESP_ERR_NO_MEM);
    wifi_.scan_generation.fetch_add(1);
    return false;
  }
  return true;
}

/**
 * @brief 将最近一次 WiFi 扫描状态复制到调用方结构体
 * @param status 扫描状态输出地址
 * @return 输出地址有效返回 true
 */
bool TDisplayP4Device::ReadWifiScanStatus(WifiScanStatus* status) {
  if (status == nullptr) {
    return false;
  }

  const uint32_t started_tick = wifi_.scan_started_tick.load();
  const uint32_t timeout_tick =
      started_tick + pdMS_TO_TICKS(kWifiScanTimeoutMs);
  if (wifi_.scan_running.load() && started_tick != 0 &&
      static_cast<int32_t>(xTaskGetTickCount() - timeout_tick) >= 0) {
    bool expected = true;
    if (wifi_.scan_running.compare_exchange_strong(expected, false)) {
      wifi_.scan_started_tick.store(0);
      wifi_.scan_failed.store(true);
      wifi_.last_error.store(ESP_ERR_TIMEOUT);
      // 非阻塞扫描超时后主动停止，避免后续连接或扫描一直被等待状态挡住。
      esp_wifi_scan_stop();
      wifi_.scan_task_running.store(false);
      wifi_.scan_timeout_handled.store(true);
      wifi_.scan_generation.fetch_add(1);
    }
  }

  *status = WifiScanStatus();
  status->scan_running = wifi_.scan_running.load();
  status->scan_failed = wifi_.scan_failed.load();
  status->last_error = wifi_.last_error.load();
  status->generation = wifi_.scan_generation.load();
  status->network_count = std::min(
      wifi_.scan_network_count.load(), kMaxWifiScanNetworkCount);
  for (size_t i = 0; i < status->network_count; ++i) {
    status->networks[i] = wifi_.scan_networks[i];
  }
  return true;
}

/**
 * @brief 设置 STA 账号密码并请求连接目标热点
 * @param ssid 目标热点 SSID
 * @param password 目标热点密码，开放热点可为空
 * @return ESP-IDF 接受连接请求返回 true
 */
bool TDisplayP4Device::ConnectWifi(
    const char* ssid, const char* password) {
  if (ssid == nullptr || ssid[0] == '\0') {
    return false;
  }

  if (!wifi_.driver_initialized.load()) {
    if (!StartWifi()) {
      return false;
    }
    return false;
  }

  const int prepare_result = PrepareWifiStation();
  if (prepare_result != ESP_OK) {
    SetWifiFailure(prepare_result);
    return false;
  }

  if (wifi_.scan_running.load() || wifi_.scan_task_running.load()) {
    // 非阻塞扫描完成事件未返回前不叠加连接 RPC，避免 hosted 通道并发。
    wifi_.scan_running.store(false);
    wifi_.scan_failed.store(true);
    wifi_.last_error.store(ESP_ERR_TIMEOUT);
    wifi_.scan_generation.fetch_add(1);
    return false;
  }

  if (wifi_.connected.load() || wifi_.got_ip.load()) {
    // 已关联热点时先断开；失败或连接中状态下直接覆盖配置，减少 hosted RPC。
    esp_wifi_disconnect();
    vTaskDelay(pdMS_TO_TICKS(kWifiDisconnectSettleDelayMs));
  }

  wifi_config_t wifi_config = {};
  std::snprintf(reinterpret_cast<char*>(wifi_config.sta.ssid),
      sizeof(wifi_config.sta.ssid), "%s", ssid);
  if (password != nullptr && password[0] != '\0') {
    std::snprintf(reinterpret_cast<char*>(wifi_config.sta.password),
        sizeof(wifi_config.sta.password), "%s", password);
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
    SetWifiFailure(config_result);
    return false;
  }

  const esp_err_t connect_result = esp_wifi_connect();
  if (connect_result != ESP_OK && connect_result != ESP_ERR_WIFI_CONN) {
    SetWifiFailure(connect_result);
    return false;
  }
  return true;
}

/**
 * @brief 取消当前 WiFi 连接请求并把状态标记为连接失败
 * @return ESP-IDF 接受取消请求返回 true
 */
bool TDisplayP4Device::CancelWifiConnection() {
  if (!wifi_.driver_initialized.load()) {
    wifi_.connected.store(false);
    wifi_.got_ip.store(false);
    wifi_.start_failed.store(true);
    wifi_.last_error.store(ESP_ERR_WIFI_CONN);
    return true;
  }

  // 仅在已经关联成功时主动断开；关联过程中的取消仍只收敛 P4 侧状态，
  // 避免 ESP-Hosted 在认证/关联中途收到 disconnect 后出现 SDIO 超时。
  const bool was_associated = wifi_.connected.load() || wifi_.got_ip.load();
  if (was_associated) {
    esp_wifi_disconnect();
    vTaskDelay(pdMS_TO_TICKS(kWifiDisconnectSettleDelayMs));
    wifi_config_t empty_config = {};
    esp_wifi_set_config(WIFI_IF_STA, &empty_config);
  }

  wifi_.connected.store(false);
  wifi_.got_ip.store(false);
  wifi_.start_failed.store(!was_associated);
  wifi_.last_error.store(was_associated ? ESP_OK : ESP_ERR_WIFI_CONN);
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

bool TDisplayP4Device::StartWifiTimeTest() {
  wifi_time_test_.requested.store(true);
  if (!wifi_.driver_initialized.load()) {
    return StartWifi();
  }

  const int result = StartWifiTimeTestInternal();
  if (result != ESP_OK) {
    SetWifiFailure(result);
    LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
        "WiFi time test start failed (error code: %#X)\n", result);
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

  if (wifi_time_test_.sync_started.exchange(false)) {
    esp_sntp_set_time_sync_notification_cb(nullptr);
    TDisplayP4Device* owner = this;
    g_wifi_time_sync_owner.compare_exchange_strong(owner, nullptr);
    if (esp_sntp_enabled()) {
      esp_sntp_stop();
    }
  }
  wifi_time_test_.synced.store(false);
  wifi_time_test_.sntp_unix_time.store(0);
  wifi_time_test_.sntp_sync_monotonic_ms.store(0);
  wifi_.start_failed.store(false);
  wifi_.last_error.store(ESP_OK);
  wifi_.retry_count.store(0);

  esp_wifi_disconnect();

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
    if (start_result != ESP_OK && start_result != ESP_ERR_WIFI_CONN &&
        start_result != ESP_ERR_WIFI_NOT_INIT &&
        start_result != ESP_ERR_INVALID_STATE) {
      SetWifiFailure(start_result);
      return false;
    }
    wifi_.running.store(true);
    if (wifi_time_test_.previous_connected) {
      esp_wifi_connect();
    }
  } else {
    const esp_err_t stop_result = esp_wifi_stop();
    if (stop_result != ESP_OK && stop_result != ESP_ERR_WIFI_NOT_INIT &&
        stop_result != ESP_ERR_WIFI_NOT_STARTED &&
        stop_result != ESP_ERR_INVALID_STATE) {
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

bool TDisplayP4Device::RegisterScreenFlushReadyCallback(
    ScreenProviderFlushReadyCallback callback, void* callback_context) {
  if (!IsScreenReady()) {
    LogMessage(LogLevel::kError, __FILE__, __LINE__,
        "Screen is not ready for flush callback registration\n");
    return false;
  }

  flush_ready_handler_.callback = callback;
  flush_ready_handler_.context = callback_context;

  esp_lcd_dpi_panel_event_callbacks_t panel_callbacks = {
      .on_color_trans_done = [](esp_lcd_panel_handle_t,
                                 esp_lcd_dpi_panel_event_data_t*,
                                 void* user_context) -> bool {
        auto* handler =
            static_cast<ScreenProviderFlushReadyHandler*>(user_context);
        if (handler != nullptr && handler->callback != nullptr) {
          handler->callback(handler->context);
        }
        return false;
      },
      .on_refresh_done = [](esp_lcd_panel_handle_t,
                             esp_lcd_dpi_panel_event_data_t*,
                             void*) -> bool { return false; },
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
      panel, &panel_callbacks, &flush_ready_handler_);
  if (result != 0) {
    LogMessage(LogLevel::kError, __FILE__, __LINE__,
        "esp_lcd_dpi_panel_register_event_callbacks failed "
        "(error code: %#X)\n",
        result);
    return false;
  }
  return true;
}

bool TDisplayP4Device::WriteScreenPixels(
    int x_start, int y_start, int x_end, int y_end, const void* pixels) {
  if (!IsScreenReady()) {
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

  if (!IsTouchReady()) {
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

  if (!IsTouchReady()) {
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
        points[*point_count].id = touch_point.info[i].finger_id;
        points[*point_count].x = touch_point.info[i].x;
        points[*point_count].y = touch_point.info[i].y;
        points[*point_count].pressure = touch_point.info[i].pressure_value;
        points[*point_count].edge_touch_flag = touch_point.edge_touch_flag;
        ++(*point_count);
      }
      return *point_count > 0;
    }
    default:
      break;
  }
  return false;
}

bool TDisplayP4Device::PlayHapticWaveform(uint8_t* waveform_count) {
  if (waveform_count != nullptr) {
    *waveform_count = 0;
  }

  if (!driver_.status().aw86224.init_flag && !driver_.InitAw86224()) {
    LogMessage(
        LogLevel::kWarning, __FILE__, __LINE__, "Aw86224 init retry failed\n");
    return false;
  }

  const auto info = cpp_bus_driver::Aw862xx::GetRamWaveformInfo(
      cpp_bus_driver::Aw862xx::RamWaveformLibrary::kRam12k041230_235);
  if (info.waveform_count == 0) {
    LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
        "Aw86224 RAM waveform count is zero\n");
    return false;
  }

  LogMessage(LogLevel::kInfo, __FILE__, __LINE__,
      "Aw86224 CIT vibration test: library=%s count=%u gain=%u\n",
      info.name == nullptr ? "unknown" : info.name,
      static_cast<unsigned int>(info.waveform_count),
      static_cast<unsigned int>(kVibrationTestGain));

  for (uint8_t sequence = 1; sequence <= info.waveform_count; ++sequence) {
    if (!driver_.chip().aw86224->PlayRamWaveform(
            sequence, kVibrationTestLoopCount, kVibrationTestGain)) {
      LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
          "Aw86224 PlayRamWaveform failed, sequence=%u\n",
          static_cast<unsigned int>(sequence));
      driver_.chip().aw86224->StopRamPlaybackWaveform();
      return false;
    }

    vTaskDelay(pdMS_TO_TICKS(kVibrationTestPlayMs));

    if (!driver_.chip().aw86224->StopRamPlaybackWaveform()) {
      LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
          "Aw86224 StopRamPlaybackWaveform failed, sequence=%u\n",
          static_cast<unsigned int>(sequence));
      return false;
    }

    if (waveform_count != nullptr) {
      *waveform_count = sequence;
    }

    vTaskDelay(pdMS_TO_TICKS(kVibrationTestStopMs));
  }

  return true;
}

bool TDisplayP4Device::PlaySpeakerTone(size_t* bytes_written) {
  if (bytes_written != nullptr) {
    *bytes_written = 0;
  }

  if (!driver_.status().es8311.init_flag && !driver_.InitEs8311()) {
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

  LogMessage(LogLevel::kInfo, __FILE__, __LINE__,
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

  const BaseType_t result = xTaskCreate(SpeakerPlaybackTaskEntry,
      "speaker_play", kSpeakerPlaybackTaskStackBytes, this,
      kSpeakerPlaybackTaskPriority, nullptr);
  if (result != pdPASS) {
    speaker_.running.store(false);
    speaker_.completed.store(true);
    return false;
  }

  return true;
}

bool TDisplayP4Device::ReadSpeakerToneStatus(SpeakerStatus* status) {
  if (status == nullptr) {
    return false;
  }

  status->running = speaker_.running.load();
  status->completed = speaker_.completed.load();
  status->success = speaker_.success.load();
  status->bytes_written = speaker_.bytes_written.load();
  status->total_bytes = speaker_.total_bytes.load();
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
  size_t bytes_written = 0;
  const bool played = PlaySpeakerTone(&bytes_written);
  speaker_.bytes_written.store(bytes_written);
  speaker_.success.store(played);
  speaker_.completed.store(true);
  speaker_.running.store(false);
}

bool TDisplayP4Device::StartMicrophone() {
  if (!driver_.status().es8311.init_flag && !driver_.InitEs8311()) {
    LogMessage(
        LogLevel::kWarning, __FILE__, __LINE__, "Es8311 init retry failed\n");
    return false;
  }

  bool expected = false;
  if (!microphone_.running.compare_exchange_strong(expected, true)) {
    return !microphone_.stop_requested.load();
  }

  microphone_.stop_requested.store(false);
  microphone_.level_percent.store(0);
  microphone_.peak_sample.store(0);
  microphone_.bytes_read.store(0);
  if (!SetAudioAdcToDac(false)) {
    microphone_.running.store(false);
    return false;
  }

  const BaseType_t result = xTaskCreate(MicrophoneCaptureTaskEntry,
      "mic_capture", kMicrophoneCaptureTaskStackBytes, this,
      kMicrophoneCaptureTaskPriority, nullptr);
  if (result != pdPASS) {
    microphone_.running.store(false);
    microphone_.stop_requested.store(true);
    return false;
  }

  return true;
}

bool TDisplayP4Device::StopMicrophone() {
  microphone_.stop_requested.store(true);
  microphone_.level_percent.store(0);
  microphone_.peak_sample.store(0);
  if (!driver_.status().es8311.init_flag) {
    microphone_.adc_to_dac_enabled.store(false);
    return true;
  }
  return SetAudioAdcToDac(false);
}

bool TDisplayP4Device::SetAudioAdcToDac(bool enable) {
  if (!driver_.status().es8311.init_flag) {
    return false;
  }

  if (!driver_.chip().es8311->SetAdcDataToDac(enable)) {
    LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
        "Es8311 SetAdcDataToDac failed\n");
    return false;
  }

  microphone_.adc_to_dac_enabled.store(enable);
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

bool TDisplayP4Device::StartGps() {
  if (!IsGpsReady() && !driver_.InitL76k()) {
    LogMessage(LogLevel::kWarning, __FILE__, __LINE__, "InitL76k failed\n");
    return false;
  }
  if (!IsGpsReady()) {
    LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
        "L76k is not ready for GPS test\n");
    return false;
  }

  gps_status_ = GpsStatus();
  gps_running_ = true;
  gps_status_.running = true;
  gps_status_.update_interval_ms = driver_.chip().l76k->update_interval_ms();

  bool result = driver_.chip().l76k->ClearRxBufferData();
  result &= driver_.chip().l76k->Sleep(false);
  if (!result) {
    gps_running_ = false;
    gps_status_.running = false;
    LogMessage(LogLevel::kWarning, __FILE__, __LINE__, "StartGps failed\n");
    return false;
  }
  return true;
}

bool TDisplayP4Device::StopGps() {
  gps_running_ = false;
  gps_status_.running = false;
  if (!IsGpsReady()) {
    return true;
  }

  const bool result = driver_.chip().l76k->Sleep(true);
  if (!result) {
    LogMessage(LogLevel::kWarning, __FILE__, __LINE__, "StopGps failed\n");
  }
  return result;
}

bool TDisplayP4Device::ReadGpsStatus(GpsStatus* status) {
  if (status == nullptr) {
    return false;
  }

  gps_status_.running = gps_running_;
  if (IsGpsReady()) {
    gps_status_.update_interval_ms = driver_.chip().l76k->update_interval_ms();
  }
  *status = gps_status_;
  if (!gps_running_) {
    return true;
  }
  if (!IsGpsReady()) {
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
      const int target_level_percent =
          std::min(100, (average_sample * 100) / kMicrophoneLevelFullScale);
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
    }

    vTaskDelay(pdMS_TO_TICKS(kMicrophoneReadDelayMs));
  }

  if (microphone_.adc_to_dac_enabled.load()) {
    driver_.chip().es8311->SetAdcDataToDac(false);
    microphone_.adc_to_dac_enabled.store(false);
  }
  microphone_.level_percent.store(0);
  microphone_.peak_sample.store(0);
  microphone_.running.store(false);
}

void TDisplayP4Device::EthernetInitTaskEntry(void* context) {
  auto* self = static_cast<TDisplayP4Device*>(context);
  if (self != nullptr) {
    self->RunEthernetInitTask();
  }
  vTaskDelete(nullptr);
}

void TDisplayP4Device::RunEthernetInitTask() {
  const int result = InitializeEthernetStack();
  if (result != ESP_OK) {
    SetEthernetFailure(result);
    LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
        "Ethernet init failed (error code: %#X)\n", result);
  }
  ethernet_.init_task_running.store(false);
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
    SetWifiFailure(ESP_ERR_TIMEOUT);
    LogMessage(
        LogLevel::kWarning, __FILE__, __LINE__, "WiFi hardware is not ready\n");
    return;
  }

  const int result = InitializeWifiStack();
  if (result != ESP_OK) {
    SetWifiFailure(result);
    LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
        "WiFi init failed (error code: %#X)\n", result);
    wifi_.init_task_running.store(false);
    return;
  }

  wifi_.init_task_running.store(false);
  if (wifi_time_test_.requested.load()) {
    const int test_result = StartWifiTimeTestInternal();
    if (test_result != ESP_OK) {
      SetWifiFailure(test_result);
      LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
          "WiFi time test start failed (error code: %#X)\n", test_result);
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

void TDisplayP4Device::RunWifiScanTask() {
  if (!wifi_.running.load()) {
    const int prepare_result = PrepareWifiStation();
    if (prepare_result != ESP_OK) {
      wifi_.scan_failed.store(true);
      wifi_.last_error.store(prepare_result);
      wifi_.scan_network_count.store(0);
      wifi_.scan_generation.fetch_add(1);
      wifi_.scan_running.store(false);
      wifi_.scan_started_tick.store(0);
      wifi_.scan_task_running.store(false);
      return;
    }
    vTaskDelay(pdMS_TO_TICKS(kWifiStartSettleDelayMs));
  }

  // STA 先启动完成，再发起非阻塞扫描；结果在 WIFI_EVENT_SCAN_DONE 中读取。
  const esp_err_t scan_result = esp_wifi_scan_start(nullptr, false);
  if (scan_result == ESP_OK) {
    return;
  } else {
    // ReadWifiScanStatus 可能已通过 P4 侧超时处理了这次失败，
    // 此时 scan_timeout_handled 为 true，避免重复递增 scan_generation。
    if (!wifi_.scan_timeout_handled.exchange(false)) {
      wifi_.scan_failed.store(true);
      wifi_.last_error.store(scan_result);
      wifi_.scan_network_count.store(0);
      wifi_.scan_generation.fetch_add(1);
    }
    // 扫描 RPC 超时后 C6 WiFi 可能处于无效状态。
    // 尝试 stop + start 尽可能恢复 C6 侧 WiFi 栈，避免后续
    // ConnectWifi / StartWifiScan 触发 SDIO 崩溃。
    // 如果 stop/start 也失败，至少不会让问题更严重。
    if (wifi_.running.load() &&
        (scan_result == ESP_ERR_TIMEOUT || scan_result == ESP_FAIL)) {
      const esp_err_t stop_result = esp_wifi_stop();
      if (stop_result == ESP_OK || stop_result == ESP_ERR_WIFI_NOT_STARTED ||
          stop_result == ESP_ERR_INVALID_STATE ||
          stop_result == ESP_ERR_WIFI_STATE) {
        esp_wifi_set_mode(WIFI_MODE_STA);
        const esp_err_t start_result = esp_wifi_start();
        if (start_result == ESP_OK || start_result == ESP_ERR_INVALID_STATE ||
            start_result == ESP_ERR_WIFI_STATE) {
          wifi_.running.store(true);
        }
      }
    }
  }

  if (!wifi_.running.load()) {
    esp_wifi_disconnect();
    wifi_config_t empty_config = {};
    esp_wifi_set_config(WIFI_IF_STA, &empty_config);
    esp_wifi_stop();
    esp_wifi_set_mode(WIFI_MODE_NULL);
  }
  wifi_.scan_running.store(false);
  wifi_.scan_started_tick.store(0);
  wifi_.scan_task_running.store(false);
}

bool TDisplayP4Device::WaitForWifiHardwareReady() {
  uint32_t elapsed_ms = 0;
  while (!driver_.status().xl9535.init_flag &&
         elapsed_ms < kWifiHardwareReadyTimeoutMs) {
    vTaskDelay(pdMS_TO_TICKS(kWifiHardwareReadyPollMs));
    elapsed_ms += kWifiHardwareReadyPollMs;
  }

  if (!driver_.status().xl9535.init_flag) {
    return false;
  }

  vTaskDelay(pdMS_TO_TICKS(kWifiEsp32c6BootDelayMs));
  return true;
}

int TDisplayP4Device::InitializeWifiStack() {
  if (wifi_.driver_initialized.load()) {
    return ESP_OK;
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
  if (result != ESP_OK && result != ESP_ERR_INVALID_STATE &&
      result != ESP_ERR_WIFI_STATE) {
    return result;
  }
  // 默认 STA netif 会在 STA_START 后读取 MAC，先让这次 hosted RPC 完成。
  vTaskDelay(pdMS_TO_TICKS(kWifiStartSettleDelayMs));

  wifi_.driver_initialized.store(true);
  wifi_.running.store(true);
  wifi_.connected.store(false);
  wifi_.got_ip.store(false);
  wifi_.start_failed.store(false);
  wifi_.last_error.store(ESP_OK);
  return ESP_OK;
}

/**
 * @brief 确保 hosted WiFi 驱动已经以 STA 模式启动
 * @return 成功返回 ESP_OK，否则返回 ESP-IDF 错误码
 */
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
  if (result != ESP_OK && result != ESP_ERR_INVALID_STATE &&
      result != ESP_ERR_WIFI_STATE) {
    return result;
  }

  wifi_.running.store(true);
  wifi_.start_failed.store(false);
  wifi_.last_error.store(ESP_OK);
  return ESP_OK;
}

/**
 * @brief 读取、去重并缓存最近一次 ESP-IDF 热点扫描记录
 */
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

  for (size_t i = 0; i < kMaxWifiScanNetworkCount; ++i) {
    wifi_.scan_networks[i] = networks[i];
  }
  wifi_.scan_network_count.store(network_count);
  wifi_.scan_failed.store(false);
  wifi_.last_error.store(ESP_OK);
  wifi_.scan_generation.fetch_add(1);
}

int TDisplayP4Device::StartWifiTimeTestInternal() {
  if (!wifi_.driver_initialized.load()) {
    return ESP_ERR_WIFI_NOT_INIT;
  }

  if (wifi_time_test_.active.load()) {
    return ESP_OK;
  }

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
    vTaskDelay(pdMS_TO_TICKS(kWifiDisconnectSettleDelayMs));
    const esp_err_t stop_result = esp_wifi_stop();
    if (stop_result != ESP_OK && stop_result != ESP_ERR_WIFI_NOT_STARTED &&
        stop_result != ESP_ERR_INVALID_STATE &&
        stop_result != ESP_ERR_WIFI_STATE) {
      return stop_result;
    }
    vTaskDelay(pdMS_TO_TICKS(kWifiDisconnectSettleDelayMs));
  }

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
  if (result != ESP_OK && result != ESP_ERR_INVALID_STATE) {
    StopWifiTimeTest();
    return result;
  }
  wifi_.running.store(true);

  result = esp_wifi_connect();
  if (result != ESP_OK && result != ESP_ERR_WIFI_CONN) {
    StopWifiTimeTest();
    return result;
  }
  return ESP_OK;
}

int TDisplayP4Device::StartWifiSntp() {
  if (wifi_time_test_.sync_started.load()) {
    return ESP_OK;
  }

  if (esp_sntp_enabled()) {
    esp_sntp_stop();
  }
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
  });
  esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);
  esp_sntp_set_sync_mode(SNTP_SYNC_MODE_IMMED);
  esp_sntp_set_sync_interval(kWifiSntpSyncIntervalMs);
  esp_sntp_setservername(0, kWifiSntpServer);
  esp_sntp_init();
  wifi_time_test_.sync_started.store(true);
  return ESP_OK;
}

void TDisplayP4Device::SetWifiFailure(int error) {
  wifi_.init_task_running.store(false);
  wifi_.start_failed.store(true);
  wifi_.last_error.store(error);
  wifi_.connected.store(false);
  wifi_.got_ip.store(false);
  wifi_.scan_running.store(false);
  wifi_.scan_started_tick.store(0);
  wifi_.scan_failed.store(true);
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
      self->wifi_.scan_started_tick.store(0);
      self->wifi_.scan_task_running.store(false);
      self->wifi_.scan_timeout_handled.store(false);
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
      self->wifi_.connected.store(false);
      self->wifi_.got_ip.store(false);
      self->wifi_time_test_.synced.store(false);
      self->wifi_time_test_.sntp_unix_time.store(0);
      self->wifi_time_test_.sntp_sync_monotonic_ms.store(0);
      self->wifi_.ip_address.store(0);
      self->wifi_.netmask.store(0);
      self->wifi_.gateway.store(0);
      if (self->wifi_time_test_.active.load() &&
          self->wifi_time_test_.sync_started.exchange(false)) {
        esp_sntp_set_time_sync_notification_cb(nullptr);
        TDisplayP4Device* owner = self;
        g_wifi_time_sync_owner.compare_exchange_strong(owner, nullptr);
        if (esp_sntp_enabled()) {
          esp_sntp_stop();
        }
      }
      if (event_data != nullptr) {
        const auto* disconnected =
            static_cast<wifi_event_sta_disconnected_t*>(event_data);
        self->wifi_.disconnect_reason.store(disconnected->reason);
      }

      if (self->wifi_time_test_.active.load()) {
        const int retry_count = self->wifi_.retry_count.fetch_add(1) + 1;
        if (retry_count <= kWifiMaxReconnectCount) {
          esp_wifi_connect();
        } else {
          self->wifi_.start_failed.store(true);
          self->wifi_.last_error.store(ESP_ERR_WIFI_CONN);
        }
      }
      break;
    }
    case WIFI_EVENT_STA_STOP:
      self->wifi_.running.store(false);
      self->wifi_.connected.store(false);
      self->wifi_.got_ip.store(false);
      self->wifi_.scan_running.store(false);
      self->wifi_.scan_started_tick.store(0);
      self->wifi_time_test_.synced.store(false);
      self->wifi_time_test_.sntp_unix_time.store(0);
      self->wifi_time_test_.sntp_sync_monotonic_ms.store(0);
      self->wifi_.ip_address.store(0);
      self->wifi_.netmask.store(0);
      self->wifi_.gateway.store(0);
      if (self->wifi_time_test_.active.load() &&
          self->wifi_time_test_.sync_started.exchange(false)) {
        esp_sntp_set_time_sync_notification_cb(nullptr);
        TDisplayP4Device* owner = self;
        g_wifi_time_sync_owner.compare_exchange_strong(owner, nullptr);
        if (esp_sntp_enabled()) {
          esp_sntp_stop();
        }
      }
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
  self->wifi_.got_ip.store(true);
  self->wifi_.ip_address.store(event->ip_info.ip.addr);
  self->wifi_.netmask.store(event->ip_info.netmask.addr);
  self->wifi_.gateway.store(event->ip_info.gw.addr);
  if (self->wifi_time_test_.active.load()) {
    const int result = self->StartWifiSntp();
    if (result != ESP_OK) {
      self->SetWifiFailure(result);
    }
  }
}

bool TDisplayP4Device::ReadDeviceDiagnostics(DeviceDiagnostics* diagnostics) {
  if (diagnostics == nullptr) {
    return false;
  }

  *diagnostics = DeviceDiagnostics();
  const bool bmu_result = ReadBmuStatus(&diagnostics->bmu);
  const bool imu_result = ReadImuStatus(&diagnostics->imu);
  return bmu_result || imu_result;
}

bool TDisplayP4Device::ReadBmuStatus(BmuStatus* status) {
  if (status == nullptr) {
    return false;
  }

  *status = BmuStatus();

  if (driver_.status().bq27220.init_flag && driver_.chip().bq27220 != nullptr) {
    cpp_bus_driver::Bq27220::BatteryStatus bmu_status_flags;
    const bool bmu_status_ok =
        driver_.chip().bq27220->GetBatteryStatus(bmu_status_flags);
    const uint16_t voltage_mv = driver_.chip().bq27220->GetVoltage();
    const int16_t current_ma = driver_.chip().bq27220->GetCurrent();
    const uint16_t charge_percent = driver_.chip().bq27220->GetStatusOfCharge();

    if (voltage_mv > 0 && voltage_mv != UINT16_MAX) {
      status->ready = true;
      status->voltage_mv = voltage_mv;
      status->current_ma = current_ma;
      status->average_current_ma = driver_.chip().bq27220->GetAverageCurrent();
      status->average_bmu_mw = driver_.chip().bq27220->GetAveragePower();
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
          bmu_status_ok && bmu_status_flags.flag.battery_present;
      status->discharging =
          bmu_status_ok ? bmu_status_flags.flag.discharging : current_ma > 0;
      status->charging =
          bmu_status_ok ? (!bmu_status_flags.flag.discharging && current_ma < 0)
                        : current_ma < 0;
      status->full_charged =
          bmu_status_ok && bmu_status_flags.flag.full_charged;
      status->full_discharged =
          bmu_status_ok && bmu_status_flags.flag.full_discharged;
      return true;
    }
  }

  return false;
}

bool TDisplayP4Device::ReadRtcStatus(RtcStatus* status) {
  if (status == nullptr) {
    return false;
  }

  *status = RtcStatus();

  if (!driver_.status().pcf8563.init_flag && !driver_.InitPcf8563()) {
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

bool TDisplayP4Device::ReadImuStatus(ImuStatus* status) {
  if (status == nullptr) {
    return false;
  }

  *status = ImuStatus();

  if (driver_.status().icm20948.init_flag &&
      driver_.chip().icm20948 != nullptr) {
    xyzFloat acceleration;
    driver_.chip().icm20948->readSensor();
    driver_.chip().icm20948->getGValues(&acceleration);

    status->ready = true;
    status->acceleration_x_g = acceleration.x;
    status->acceleration_y_g = acceleration.y;
    status->acceleration_z_g = acceleration.z;
    return true;
  }

  return false;
}

void TDisplayP4Device::StartScreenBacklight() {
  if (!WaitForScreenReady()) {
    return;
  }

  switch (driver_.screen_type()) {
    case device::ScreenType::kHi8561:
      if (driver_.status().hi8561_backlight.init_flag) {
        driver_.chip().hi8561_backlight->StartGradientTime(100, 500);
      }
      break;
    case device::ScreenType::kRm69a10:
      if (driver_.status().rm69a10.init_flag) {
        for (uint16_t brightness = 0; brightness < 255; brightness += 5) {
          driver_.chip().rm69a10->SetBrightness(brightness);
          vTaskDelay(pdMS_TO_TICKS(10));
        }
      }
      break;
    default:
      break;
  }
}

bool TDisplayP4Device::WaitForScreenReady() {
  for (int elapsed_ms = 0; elapsed_ms < kScreenReadyTimeoutMs;
      elapsed_ms += kScreenReadyPollMs) {
    if (IsScreenReady()) {
      return true;
    }
    vTaskDelay(pdMS_TO_TICKS(kScreenReadyPollMs));
  }
  return IsScreenReady();
}

bool TDisplayP4Device::IsScreenReady() const {
  const auto screen_bus = driver_.bus().screen_mipi_bus;
  if (screen_bus == nullptr || screen_bus->device_handle() == nullptr) {
    return false;
  }

  switch (driver_.screen_type()) {
    case device::ScreenType::kHi8561:
      return driver_.status().hi8561.init_flag &&
             driver_.status().hi8561_backlight.init_flag &&
             driver_.chip().hi8561 != nullptr;
    case device::ScreenType::kRm69a10:
      return driver_.status().rm69a10.init_flag &&
             driver_.chip().rm69a10 != nullptr;
    default:
      break;
  }
  return false;
}

bool TDisplayP4Device::IsTouchReady() const {
  switch (driver_.screen_type()) {
    case device::ScreenType::kHi8561:
      return driver_.status().hi8561_touch.init_flag &&
             driver_.chip().hi8561_touch != nullptr;
    case device::ScreenType::kRm69a10:
      return driver_.status().gt9895.init_flag &&
             driver_.chip().gt9895 != nullptr;
    default:
      break;
  }
  return false;
}

bool TDisplayP4Device::IsGpsReady() const {
  return driver_.status().l76k.init_flag && driver_.chip().l76k != nullptr;
}

}  // namespace lilygo_box::hal
