/*
 * @Description: None
 * @Author: LILYGO_L
 * @Date: 2026-05-10 13:27:05
 * @LastEditTime: 2026-05-13 23:20:00
 * @License: GPL 3.0
 */
#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

#include "hal/audio_provider.h"
#include "hal/bmu_provider.h"
#include "hal/device_diagnostics.h"
#include "hal/ethernet_provider.h"
#include "hal/gps_provider.h"
#include "hal/haptic_provider.h"
#include "hal/imu_provider.h"
#include "hal/screen_provider.h"
#include "t_display_p4_driver.h"

namespace lilygo_box::hal {
namespace device = lilygo_device_driver::t_display_p4::device;

class TDisplayP4Device final : public ScreenProvider,
                               public DeviceDiagnosticsProvider,
                               public GpsProvider,
                               public ImuProvider,
                               public AudioProvider,
                               public HapticProvider,
                               public BmuProvider,
                               public EthernetProvider {
 public:
  TDisplayP4Device();

  /**
   * @brief 初始化 T-Display-P4 到屏幕可用状态
   * @return 初始化成功返回 true，否则返回 false
   * @Date 2026-05-10 13:01:03
   */
  bool Init() override;

  /**
   * @brief 获取设备名称
   * @return 设备名称字符串
   * @Date 2026-05-10 13:01:03
   */
  const char* name() const override { return "T-Display-P4"; }

  /**
   * @brief 获取屏幕宽度
   * @return 屏幕宽度，单位为像素
   * @Date 2026-05-10 13:01:03
   */
  int width() const override {
    return device::kScreenWidth;
  }

  /**
   * @brief 获取屏幕高度
   * @return 屏幕高度，单位为像素
   * @Date 2026-05-10 13:01:03
   */
  int height() const override {
    return device::kScreenHeight;
  }

  /**
   * @brief 获取单个像素的位数
   * @return 单个像素的位数
   * @Date 2026-05-10 13:01:03
   */
  int bits_per_pixel() const override {
    return device::kScreenBitsPerPixel;
  }

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
  bool SetAdcToDac(bool enable) override;

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
  bool RegisterFlushReadyCallback(
      ScreenProviderFlushReadyCallback callback,
      void* callback_context) override;

  /**
   * @brief 写入指定屏幕区域的像素数据
   * @param x_start 起始 X 坐标
   * @param y_start 起始 Y 坐标
   * @param x_end 结束 X 坐标
   * @param y_end 结束 Y 坐标
   * @param pixels 像素数据地址
   * @return 写入成功返回 true，否则返回 false
   * @Date 2026-05-10 13:01:03
   */
  bool WritePixels(int x_start, int y_start, int x_end, int y_end,
      const void* pixels) override;

  /**
   * @brief 读取当前触摸点
   * @param point 触摸点输出地址
   * @return 读取到触摸点返回 true，否则返回 false
   * @Date 2026-05-10 13:01:03
   */
  bool ReadTouch(TouchPoint* point) override;

  /**
   * @brief 读取当前多个触摸点
   * @param points 触摸点输出数组
   * @param max_points 输出数组可容纳的最大触点数量
   * @param point_count 实际读取到的触点数量输出地址
   * @return 读取到至少一个触摸点返回 true，否则返回 false
   * @Date 2026-05-13 09:55:00
   */
  bool ReadTouchPoints(
      TouchPoint* points, size_t max_points, size_t* point_count) override;

  /**
   * @brief 读取设备诊断快照
   * @param diagnostics 诊断数据输出地址
   * @return 读取到有效诊断数据返回 true，否则返回 false
   * @Date 2026-05-10 13:01:03
   */
  bool ReadDiagnostics(DeviceDiagnostics* diagnostics) override;

  /**
   * @brief 读取 BMU 电池管理状态
   * @param status BMU 状态输出地址
   * @return 读取到有效 BMU 状态返回 true，否则返回 false
   * @Date 2026-05-14 00:20:00
   */
  bool ReadBmuStatus(BmuStatus* status) override;

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
   * @brief 启动屏幕背光
   * @return
   * @Date 2026-05-10 13:01:03
   */
  void StartBacklight() override;

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
  bool gps_running_ = false;
  GpsStatus gps_status_;
};

}  // namespace lilygo_box::hal
