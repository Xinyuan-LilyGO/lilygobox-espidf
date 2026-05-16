/*
 * @Description: None
 * @Author: LILYGO_L
 * @Date: 2026-05-10 13:27:05
 * @LastEditTime: 2026-05-15 09:49:03
 * @License: GPL 3.0
 */
#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

#include "esp_wifi_types.h"
#include "hal/providers/providers.h"
#include "t_display_p4_driver.h"

namespace lilygo_box::hal {
class TDisplayP4Device final : public ScreenProvider,
                               public DeviceProvider,
                               public DeviceDiagnosticsProvider,
                               public GpsProvider,
                               public ImuProvider,
                               public AudioProvider,
                               public HapticProvider,
                               public BmuProvider,
                               public RtcProvider,
                               public EthernetProvider,
                               public WifiProvider {
 public:
  TDisplayP4Device();

  /**
   * @brief 初始化 T-Display-P4 到屏幕可用状态
   * @return 初始化成功返回 true，否则返回 false
   * @Date 2026-05-10 13:01:03
   */
  bool InitDevice() override;

  /**
   * @brief 获取设备名称
   * @return 设备名称字符串
   * @Date 2026-05-10 13:01:03
   */
  const char* ScreenName() const override { return "T-Display-P4"; }

  /**
   * @brief 获取屏幕宽度
   * @return 屏幕宽度，单位为像素
   * @Date 2026-05-10 13:01:03
   */
  int ScreenWidth() const override;

  /**
   * @brief 获取屏幕高度
   * @return 屏幕高度，单位为像素
   * @Date 2026-05-10 13:01:03
   */
  int ScreenHeight() const override;

  /**
   * @brief 获取单个像素的位数
   * @return 单个像素的位数
   * @Date 2026-05-10 13:01:03
   */
  int ScreenBitsPerPixel() const override;

  /**
   * @brief 获取当前自动识别到的屏幕类型名称
   * @return 屏幕类型名称字符串
   * @Date 2026-05-15 00:00:00
   */
  const char* ScreenType() const override;

  /**
   * @brief 播放 AW86224 RAM 振动波形
   * @param waveform_count 实际播放的 RAM 波形数量输出地址
   * @return 播放成功返回 true，否则返回 false
   * @Date 2026-05-13 18:20:00
   */
  bool PlayHapticWaveform(uint8_t* waveform_count) override;

  /**
   * @brief 播放 ES8311 扬声器音频提示
   * @param bytes_written 实际写入 I2S 的字节数输出地址
   * @return 播放成功返回 true，否则返回 false
   * @Date 2026-05-13 16:55:00
   */
  bool PlaySpeakerTone(size_t* bytes_written) override;

  /**
   * @brief 创建后台任务播放 ES8311 扬声器音频
   * @return 任务创建成功返回 true，否则返回 false
   * @Date 2026-05-13 21:00:00
   */
  bool StartSpeakerTone() override;

  /**
   * @brief 读取 ES8311 扬声器播放状态
   * @param status 播放状态输出地址
   * @return 读取成功返回 true，否则返回 false
   * @Date 2026-05-13 21:00:00
   */
  bool ReadSpeakerToneStatus(SpeakerPlaybackStatus* status) override;

  /**
   * @brief 创建后台任务读取 ES8311 麦克风采样数据
   * @return 任务创建成功返回 true，否则返回 false
   * @Date 2026-05-13 21:20:00
   */
  bool StartMicrophone() override;

  /**
   * @brief 停止 ES8311 麦克风采样并关闭 ADC 到 DAC 直通
   * @return 停止命令发送成功返回 true，否则返回 false
   * @Date 2026-05-13 21:20:00
   */
  bool StopMicrophone() override;

  /**
   * @brief 设置 ES8311 麦克风 ADC 数据是否直通到 DAC
   * @param enable true 表示打开直通，false 表示关闭直通
   * @return 设置成功返回 true，否则返回 false
   * @Date 2026-05-13 21:20:00
   */
  bool SetAudioAdcToDac(bool enable) override;

  /**
   * @brief 读取 ES8311 麦克风状态
   * @param status 麦克风状态输出地址
   * @return 读取成功返回 true，否则返回 false
   * @Date 2026-05-13 21:20:00
   */
  bool ReadMicrophoneStatus(MicrophoneStatus* status) override;

  /**
   * @brief 启动 L76K GPS 测试并唤醒模块
   * @return 启动成功返回 true，否则返回 false
   * @Date 2026-05-13 23:20:00
   */
  bool StartGps() override;

  /**
   * @brief 停止 L76K GPS 测试并让模块进入睡眠
   * @return 停止成功返回 true，否则返回 false
   * @Date 2026-05-13 23:20:00
   */
  bool StopGps() override;

  /**
   * @brief 读取 L76K GPS 测试状态和最新 RMC 解析数据
   * @param status GPS 测试状态输出地址
   * @return 读取成功返回 true，否则返回 false
   * @Date 2026-05-13 23:20:00
   */
  bool ReadGpsStatus(GpsStatus* status) override;

  /**
   * @brief 注册屏幕 flush 完成回调
   * @param callback flush 完成时调用的回调函数
   * @param callback_context 回调上下文
   * @return 注册成功返回 true，否则返回 false
   * @Date 2026-05-10 13:01:03
   */
  bool RegisterScreenFlushReadyCallback(ScreenProviderFlushReadyCallback callback,
      void* callback_context) override;

  /**
   * @brief 写入屏幕像素区域
   * @param x_start 起始 X 坐标
   * @param y_start 起始 Y 坐标
   * @param x_end 结束 X 坐标
   * @param y_end 结束 Y 坐标
   * @param pixels 像素数据
   * @return 写入成功返回 true，否则返回 false
   * @Date 2026-05-10 13:01:03
   */
  bool WriteScreenPixels(int x_start, int y_start, int x_end, int y_end,
      const void* pixels) override;

  /**
   * @brief 读取当前触摸点
   * @param point 触摸点输出地址
   * @return 读取到触摸点返回 true，否则返回 false
   * @Date 2026-05-10 13:01:03
   */
  bool ReadScreenTouch(TouchPoint* point) override;

  /**
   * @brief 读取当前多个触摸点
   * @param points 触摸点输出数组
   * @param max_points 输出数组可容纳的最大触点数量
   * @param point_count 实际读取到的触点数量输出地址
   * @return 读取到至少一个触摸点返回 true，否则返回 false
   * @Date 2026-05-13 09:55:00
   */
  bool ReadScreenTouchPoints(
      TouchPoint* points, size_t max_points, size_t* point_count) override;

  /**
   * @brief 读取设备诊断快照
   * @param diagnostics 诊断数据输出地址
   * @return 读取到有效诊断数据返回 true，否则返回 false
   * @Date 2026-05-10 13:01:03
   */
  bool ReadDeviceDiagnostics(DeviceDiagnostics* diagnostics) override;

  /**
   * @brief 读取 BMU 电池管理状态
   * @param status BMU 状态输出地址
   * @return 读取到有效 BMU 状态返回 true，否则返回 false
   * @Date 2026-05-14 00:20:00
   */
  bool ReadBmuStatus(BmuStatus* status) override;

  /**
   * @brief 读取 PCF8563 RTC 日期时间和时钟完整性状态
   * @param status RTC 状态输出地址
   * @return 读取到有效 RTC 数据返回 true，否则返回 false
   * @Date 2026-05-15 10:40:00
   */
  bool ReadRtcStatus(RtcStatus* status) override;

  /**
   * @brief 读取 ICM20948 IMU 运动状态
   * @param status IMU 状态输出地址
   * @return 读取到有效 IMU 状态返回 true，否则返回 false
   * @Date 2026-05-14 00:20:00
   */
  bool ReadImuStatus(ImuStatus* status) override;

  /**
   * @brief 异步启动 IP101 以太网链路检测
   * @return 启动命令发送成功返回 true，否则返回 false
   * @Date 2026-05-14 00:20:00
   */
  bool StartEthernet() override;

  /**
   * @brief 读取 IP101 以太网链路和 DHCP 状态
   * @param status 以太网状态输出地址
   * @return 读取成功返回 true，否则返回 false
   * @Date 2026-05-14 00:20:00
   */
  bool ReadEthernetStatus(EthernetStatus* status) override;

  /**
   * @brief 异步初始化 hosted WiFi 驱动并保持默认关闭
   * @return 启动命令发送成功返回 true，否则返回 false
   * @Date 2026-05-15 13:20:00
   */
  bool StartWifi() override;

  /**
   * @brief 启动 WiFi 获取时间测试并连接工厂测试热点
   * @return 启动命令发送成功返回 true，否则返回 false
   * @Date 2026-05-15 13:20:00
   */
  bool StartWifiTimeTest() override;

  /**
   * @brief 停止 WiFi 获取时间测试并恢复测试前状态
   * @return 恢复命令发送成功返回 true，否则返回 false
   * @Date 2026-05-15 13:20:00
   */
  bool StopWifiTimeTest() override;

  /**
   * @brief 读取 hosted WiFi 连接和 SNTP 时间同步状态
   * @param status WiFi 状态输出地址
   * @return 读取成功返回 true，否则返回 false
   * @Date 2026-05-15 13:20:00
   */
  bool ReadWifiStatus(WifiStatus* status) override;

  /**
   * @brief 启动屏幕背光
   * @return
   * @Date 2026-05-10 13:01:03
   */
  void StartScreenBacklight() override;

 private:
  static constexpr int kScreenReadyTimeoutMs = 5000;
  static constexpr int kScreenReadyPollMs = 20;

  /**
   * @brief 等待异步屏幕初始化进入可用状态
   * @return 屏幕可用返回 true，否则返回 false
   * @Date 2026-05-10 13:01:03
   */
  bool WaitForScreenReady();

  /**
   * @brief 判断屏幕是否已经可写入像素
   * @return 屏幕可用返回 true，否则返回 false
   * @Date 2026-05-10 13:01:03
   */
  bool IsScreenReady() const;

  /**
   * @brief 判断触摸芯片是否已经可读取
   * @return 触摸可用返回 true，否则返回 false
   * @Date 2026-05-10 13:01:03
   */
  bool IsTouchReady() const;

  /**
   * @brief 判断 L76K GPS 模块是否已经初始化完成
   * @return GPS 模块可用返回 true，否则返回 false
   * @Date 2026-05-13 23:20:00
   */
  bool IsGpsReady() const;

  /**
   * @brief 扬声器播放任务入口
   * @param context 设备对象指针
   * @return
   * @Date 2026-05-13 21:00:00
   */
  static void SpeakerToneTaskEntry(void* context);

  /**
   * @brief 执行后台扬声器播放
   * @return
   * @Date 2026-05-13 21:00:00
   */
  void RunSpeakerToneTask();

  /**
   * @brief 麦克风采样读取任务入口
   * @param context 设备对象指针
   * @return
   * @Date 2026-05-13 21:20:00
   */
  static void MicrophoneTestTaskEntry(void* context);

  /**
   * @brief 执行后台麦克风采样读取
   * @return
   * @Date 2026-05-13 21:20:00
   */
  void RunMicrophoneTestTask();

  /**
   * @brief 以太网初始化任务入口
   * @param context 设备对象指针
   * @return
   * @Date 2026-05-14 00:20:00
   */
  static void EthernetInitTaskEntry(void* context);

  /**
   * @brief 执行 IP101 以太网异步初始化
   * @return
   * @Date 2026-05-14 00:20:00
   */
  void RunEthernetInitTask();

  /**
   * @brief 初始化 ESP-IDF 以太网驱动和 netif
   * @return 初始化成功返回 ESP_OK，否则返回错误码
   * @Date 2026-05-14 00:20:00
   */
  int InitializeEthernetStack();

  /**
   * @brief 记录以太网初始化失败状态
   * @param error 错误码
   * @return
   * @Date 2026-05-14 00:20:00
   */
  void SetEthernetFailure(int error);

  /**
   * @brief 处理以太网链路事件
   * @param arg 设备对象指针
   * @param event_base 事件类型
   * @param event_id 事件 ID
   * @param event_data 事件数据
   * @return
   * @Date 2026-05-14 00:20:00
   */
  static void EthernetEventHandler(
      void* arg, const char* event_base, int32_t event_id, void* event_data);

  /**
   * @brief 处理以太网 DHCP 获取 IP 事件
   * @param arg 设备对象指针
   * @param event_base 事件类型
   * @param event_id 事件 ID
   * @param event_data 事件数据
   * @return
   * @Date 2026-05-14 00:20:00
   */
  static void EthernetGotIpEventHandler(
      void* arg, const char* event_base, int32_t event_id, void* event_data);

  /**
   * @brief hosted WiFi 初始化任务入口
   * @param context 设备对象指针
   * @return
   * @Date 2026-05-15 13:20:00
   */
  static void WifiInitTaskEntry(void* context);

  /**
   * @brief 执行 hosted WiFi 异步初始化
   * @return
   * @Date 2026-05-15 13:20:00
   */
  void RunWifiInitTask();

  /**
   * @brief 等待 ESP32-C6 桥接芯片完成上电复位
   * @return 就绪返回 true，否则返回 false
   * @Date 2026-05-15 14:40:00
   */
  bool WaitForWifiHardwareReady();

  /**
   * @brief 初始化 hosted WiFi 驱动和默认 STA netif
   * @return 初始化成功返回 ESP_OK，否则返回错误码
   * @Date 2026-05-15 13:20:00
   */
  int InitializeWifiStack();

  /**
   * @brief 连接工厂测试 WiFi 并启动网络时间同步
   * @return 启动成功返回 ESP_OK，否则返回错误码
   * @Date 2026-05-15 13:20:00
   */
  int StartWifiTimeTestInternal();

  /**
   * @brief 启动 SNTP 时间同步
   * @return 启动成功返回 ESP_OK，否则返回错误码
   * @Date 2026-05-15 13:20:00
   */
  int StartWifiSntp();

  /**
   * @brief 记录 hosted WiFi 初始化或连接失败状态
   * @param error 错误码
   * @return
   * @Date 2026-05-15 13:20:00
   */
  void SetWifiFailure(int error);

  /**
   * @brief 处理 hosted WiFi STA 连接事件
   * @param arg 设备对象指针
   * @param event_base 事件类型
   * @param event_id 事件 ID
   * @param event_data 事件数据
   * @return
   * @Date 2026-05-15 13:20:00
   */
  static void WifiEventHandler(
      void* arg, const char* event_base, int32_t event_id, void* event_data);

  /**
   * @brief 处理 hosted WiFi DHCP 获取 IP 事件
   * @param arg 设备对象指针
   * @param event_base 事件类型
   * @param event_id 事件 ID
   * @param event_data 事件数据
   * @return
   * @Date 2026-05-15 13:20:00
   */
  static void WifiGotIpEventHandler(
      void* arg, const char* event_base, int32_t event_id, void* event_data);

  lilygo_device_driver::TDisplayP4Driver& driver_;
  ScreenProviderFlushReadyHandler flush_ready_handler_;
  // 扬声器任务是否正在播放
  std::atomic<bool> speaker_test_running_{false};
  // 扬声器任务是否已经完成过一次
  std::atomic<bool> speaker_test_completed_{false};
  // 扬声器最近一次播放是否成功
  std::atomic<bool> speaker_test_success_{false};
  // 扬声器最近一次写入的字节数
  std::atomic<size_t> speaker_test_bytes_written_{0};
  // 扬声器音频总字节数
  std::atomic<size_t> speaker_test_total_bytes_{0};
  // 麦克风采样任务是否正在读取
  std::atomic<bool> microphone_test_running_{false};
  // 麦克风采样任务是否请求停止
  std::atomic<bool> microphone_test_stop_requested_{false};
  // 麦克风 ADC 数据是否直通到 DAC
  std::atomic<bool> microphone_adc_to_dac_enabled_{false};
  // 麦克风当前音量百分比
  std::atomic<int> microphone_level_percent_{0};
  // 麦克风当前峰值采样
  std::atomic<int> microphone_peak_sample_{0};
  // 麦克风累计读取字节数
  std::atomic<size_t> microphone_bytes_read_{0};
  // 以太网初始化任务是否正在运行
  std::atomic<bool> ethernet_initializing_{false};
  // 以太网驱动是否已经初始化完成
  std::atomic<bool> ethernet_initialized_{false};
  // 以太网驱动是否已经启动
  std::atomic<bool> ethernet_running_{false};
  // 以太网链路是否已经连接
  std::atomic<bool> ethernet_link_up_{false};
  // 以太网是否已经获取 DHCP 地址
  std::atomic<bool> ethernet_got_ip_{false};
  // 以太网启动是否失败
  std::atomic<bool> ethernet_start_failed_{false};
  // 以太网端口数量
  std::atomic<int> ethernet_port_count_{0};
  // 以太网最近一次错误码
  std::atomic<int> ethernet_last_error_{0};
  // 以太网 MAC 地址打包值
  std::atomic<uint64_t> ethernet_mac_address_{0};
  // 以太网 DHCP IP 地址
  std::atomic<uint32_t> ethernet_ip_address_{0};
  // 以太网 DHCP 子网掩码
  std::atomic<uint32_t> ethernet_netmask_{0};
  // 以太网 DHCP 网关
  std::atomic<uint32_t> ethernet_gateway_{0};
  // ESP-IDF 以太网驱动句柄
  void* ethernet_handle_ = nullptr;
  // esp_hosted 桥接组件是否已经初始化完成
  std::atomic<bool> wifi_hosted_initialized_{false};
  // hosted WiFi 初始化任务是否正在运行
  std::atomic<bool> wifi_initializing_{false};
  // hosted WiFi 驱动是否已经初始化完成
  std::atomic<bool> wifi_initialized_{false};
  // hosted WiFi 驱动是否已经启动
  std::atomic<bool> wifi_running_{false};
  // hosted WiFi STA 是否已经关联到热点
  std::atomic<bool> wifi_connected_{false};
  // hosted WiFi 是否已经获取 DHCP 地址
  std::atomic<bool> wifi_got_ip_{false};
  // hosted WiFi 启动或连接是否失败
  std::atomic<bool> wifi_start_failed_{false};
  // WiFi 获取时间测试是否已经请求
  std::atomic<bool> wifi_time_test_requested_{false};
  // WiFi 获取时间测试是否正在运行
  std::atomic<bool> wifi_time_test_active_{false};
  // SNTP 时间同步是否已经启动
  std::atomic<bool> wifi_time_sync_started_{false};
  // SNTP 是否已经获取到有效时间
  std::atomic<bool> wifi_time_synced_{false};
  // WiFi 连接重试次数
  std::atomic<int> wifi_retry_count_{0};
  // WiFi 最近一次错误码
  std::atomic<int> wifi_last_error_{0};
  // WiFi 最近一次断开原因
  std::atomic<int> wifi_disconnect_reason_{0};
  // WiFi RSSI 信号强度
  std::atomic<int> wifi_rssi_{0};
  // WiFi 连接信道
  std::atomic<int> wifi_channel_{0};
  // WiFi STA MAC 地址打包值
  std::atomic<uint64_t> wifi_mac_address_{0};
  // WiFi DHCP IP 地址
  std::atomic<uint32_t> wifi_ip_address_{0};
  // WiFi DHCP 子网掩码
  std::atomic<uint32_t> wifi_netmask_{0};
  // WiFi DHCP 网关
  std::atomic<uint32_t> wifi_gateway_{0};
  // WiFi 测试进入前驱动是否已启动
  bool wifi_previous_running_ = false;
  // WiFi 测试进入前 STA 是否已连接
  bool wifi_previous_connected_ = false;
  // WiFi 测试进入前模式是否有效
  bool wifi_previous_mode_valid_ = false;
  // WiFi 测试进入前 STA 配置是否有效
  bool wifi_previous_sta_config_valid_ = false;
  // WiFi 测试进入前工作模式
  wifi_mode_t wifi_previous_mode_ = WIFI_MODE_NULL;
  // WiFi 测试进入前 STA 配置
  wifi_config_t wifi_previous_sta_config_ = {};
  // ESP-IDF WiFi netif 指针
  void* wifi_netif_ = nullptr;
  bool gps_running_ = false;
  GpsStatus gps_status_;
};

}  // namespace lilygo_box::hal
