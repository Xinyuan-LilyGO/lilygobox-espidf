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
#include <memory>

#include "esp_wifi_types.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "hal/ppa/ppa_srm_helper.h"
#include "hal/providers/providers.h"
#include "t_display_p4_driver.h"

namespace lilygo_box::hal {
class TDisplayP4Device final : public ScreenProvider,
                               public DeviceProvider,
                               public DeviceDiagnosticsProvider,
                               public DeviceInfoProvider,
                               public GpsProvider,
                               public ImuProvider,
                               public AudioProvider,
                               public HapticProvider,
                               public CameraProvider,
                               public BmuProvider,
                               public RtcProvider,
                               public EthernetProvider,
                               public WifiProvider,
                               public StorageProvider {
 public:
  TDisplayP4Device();

  /**
   * @brief 初始化 T-Display-P4 到屏幕可用状态
   * @return 初始化成功返回 true，否则返回 false
   */
  bool InitDevice() override;

  /**
   * @brief 读取当前 T-Display-P4 设备信息
   * @param info 设备信息输出地址
   * @return 读取成功返回 true，否则返回 false
   */
  bool ReadDeviceInfo(DeviceInfo* info) override;

  /**
   * @brief 获取屏幕宽度
   * @return 屏幕宽度，单位为像素
   */
  int ScreenWidth() const override;

  /**
   * @brief 获取屏幕高度
   * @return 屏幕高度，单位为像素
   */
  int ScreenHeight() const override;

  /**
   * @brief 获取单个像素的位数
   * @return 单个像素的位数
   */
  int ScreenBitsPerPixel() const override;

  /**
   * @brief 读取 AW86224 可用 RAM 振动波形数量
   * @param waveform_count 波形数量输出地址
   * @return 读取成功返回 true，否则返回 false
   */
  bool ReadHapticWaveformCount(uint8_t* waveform_count) override;

  /**
   * @brief 播放 AW86224 指定 RAM 振动波形
   * @param waveform_sequence_number RAM 波形 sequence 编号
   * @param loop_count 播放循环次数，范围 1~16
   * @param gain 振动增益，范围 0~255
   * @param auto_brake true 表示启用自动制动，false 表示关闭自动制动
   * @return 播放任务启动成功返回 true，否则返回 false
   */
  bool PlayHapticWaveform(uint8_t waveform_sequence_number,
      uint8_t loop_count, uint8_t gain, bool auto_brake) override;

  /**
   * @brief 播放 ES8311 扬声器音频提示
   * @param bytes_written 实际写入 I2S 的字节数输出地址
   * @return 播放成功返回 true，否则返回 false
   */
  bool PlaySpeakerTone(size_t* bytes_written) override;

  /**
   * @brief 创建后台任务播放 ES8311 扬声器音频
   * @return 任务创建成功返回 true，否则返回 false
   */
  bool StartSpeakerTone() override;

  /**
   * @brief 创建后台任务循环播放 ES8311 扬声器音频预览
   * @return 任务创建成功或已经在播放返回 true，否则返回 false
   */
  bool StartSpeakerToneLoop() override;

  /**
   * @brief 停止后台循环播放 ES8311 扬声器音频预览
   * @return 停止命令发送成功返回 true，否则返回 false
   */
  bool StopSpeakerToneLoop() override;

  /**
   * @brief 设置 ES8311 扬声器播放音量百分比
   * @param percent 音量百分比，范围 0~100
   * @return 设置成功返回 true，否则返回 false
   */
  bool SetSpeakerVolumePercent(int percent) override;

  /**
   * @brief 读取 ES8311 扬声器播放状态
   * @param status 播放状态输出地址
   * @return 读取成功返回 true，否则返回 false
   */
  bool ReadSpeakerToneStatus(SpeakerStatus* status) override;

  /**
   * @brief 创建后台任务读取 ES8311 麦克风采样数据
   * @return 任务创建成功返回 true，否则返回 false
   */
  bool StartMicrophone() override;

  /**
   * @brief 停止 ES8311 麦克风采样并关闭 ADC 到 DAC 直通
   * @return 停止命令发送成功返回 true，否则返回 false
   */
  bool StopMicrophone() override;

  /**
   * @brief 设置 ES8311 麦克风 ADC 数据是否直通到 DAC
   * @param enable true 表示打开直通，false 表示关闭直通
   * @return 设置成功返回 true，否则返回 false
   */
  bool SetAudioAdcToDac(bool enable) override;

  /**
   * @brief 读取 ES8311 麦克风状态
   * @param status 麦克风状态输出地址
   * @return 读取成功返回 true，否则返回 false
   */
  bool ReadMicrophoneStatus(MicrophoneStatus* status) override;

  /**
   * @brief 启动摄像头预览并直接写入屏幕
   * @return 启动成功返回 true，否则返回 false
   */
  bool StartCameraPreview() override;

  /**
   * @brief 停止摄像头预览
   * @return 停止成功或已经停止返回 true，否则返回 false
   */
  bool StopCameraPreview() override;

  /**
   * @brief 获取最新摄像头预览帧信息
   * @param info 预览帧信息输出地址
   * @return 获取成功返回 true，否则返回 false
   */
  bool GetCameraPreviewFrameInfo(CameraPreviewFrameInfo* info) override;

  /**
   * @brief 复制最新摄像头预览帧到调用方缓冲区
   * @param buffer 输出缓冲区
   * @param buffer_size 输出缓冲区大小
   * @param info 预览帧信息输出地址
   * @return 复制成功返回 true，否则返回 false
   */
  bool CopyCameraPreviewFrame(uint8_t* buffer, size_t buffer_size,
      CameraPreviewFrameInfo* info) override;

  /**
   * @brief 启动 L76K GPS 测试并唤醒模块
   * @return 启动成功返回 true，否则返回 false
   */
  bool StartGps() override;

  /**
   * @brief 停止 L76K GPS 测试并让模块进入睡眠
   * @return 停止成功返回 true，否则返回 false
   */
  bool StopGps() override;

  /**
   * @brief 读取 L76K GPS 测试状态和最新 GNSS 解析数据
   * @param status GPS 测试状态输出地址
   * @return 读取成功返回 true，否则返回 false
   */
  bool ReadGpsStatus(GpsStatus* status) override;

  /**
   * @brief 注册屏幕 flush 完成回调
   * @param callback flush 完成时调用的回调函数
   * @param callback_context 回调上下文
   * @return 注册成功返回 true，否则返回 false
   */
  bool RegisterScreenFlushReadyCallback(
      ScreenProviderFlushReadyCallback callback,
      void* callback_context) override;

  /**
   * @brief 写入屏幕像素区域
   * @param x_start 起始 X 坐标
   * @param y_start 起始 Y 坐标
   * @param x_end 结束 X 坐标
   * @param y_end 结束 Y 坐标
   * @param pixels 像素数据
   * @return 写入成功返回 true，否则返回 false
   */
  bool WriteScreenPixels(int x_start, int y_start, int x_end, int y_end,
      const void* pixels) override;

  /**
   * @brief 读取当前触摸点
   * @param point 触摸点输出地址
   * @return 读取到触摸点返回 true，否则返回 false
   */
  bool ReadScreenTouch(TouchPoint* point) override;

  /**
   * @brief 读取当前多个触摸点
   * @param points 触摸点输出数组
   * @param max_points 输出数组可容纳的最大触点数量
   * @param point_count 实际读取到的触点数量输出地址
   * @return 读取到至少一个触摸点返回 true，否则返回 false
   */
  bool ReadScreenTouchPoints(
      TouchPoint* points, size_t max_points, size_t* point_count) override;

  /**
   * @brief 读取设备诊断快照
   * @param diagnostics 诊断数据输出地址
   * @return 读取到有效诊断数据返回 true，否则返回 false
   */
  bool ReadDeviceDiagnostics(DeviceDiagnostics* diagnostics) override;

  /**
   * @brief 读取 BMU 电池管理状态
   * @param status BMU 状态输出地址
   * @return 读取到有效 BMU 状态返回 true，否则返回 false
   */
  bool ReadBmuStatus(BmuStatus* status) override;

  /**
   * @brief 读取 PCF8563 RTC 日期时间和时钟完整性状态
   * @param status RTC 状态输出地址
   * @return 读取到有效 RTC 数据返回 true，否则返回 false
   */
  bool ReadRtcStatus(RtcStatus* status) override;

  /**
   * @brief 读取 ICM20948 IMU 运动状态
   * @param status IMU 状态输出地址
   * @return 读取到有效 IMU 状态返回 true，否则返回 false
   */
  bool ReadImuStatus(ImuStatus* status) override;

  /**
   * @brief 异步启动 IP101 以太网链路检测
   * @return 启动命令发送成功返回 true，否则返回 false
   */
  bool StartEthernet() override;

  /**
   * @brief 读取 IP101 以太网链路和 DHCP 状态
   * @param status 以太网状态输出地址
   * @return 读取成功返回 true，否则返回 false
   */
  bool ReadEthernetStatus(EthernetStatus* status) override;

  /**
   * @brief 异步初始化 hosted WiFi 驱动并保持默认关闭
   * @return 启动命令发送成功返回 true，否则返回 false
  */
  bool StartWifi() override;

  /**
   * @brief 停止 hosted WiFi STA 并清理连接和扫描状态
   * @return 停止成功或已经处于关闭状态返回 true，否则返回 false
   */
  bool StopWifi() override;

  /**
   * @brief 异步扫描附近的 hosted WiFi 热点
   * @return 扫描命令发送成功或扫描已在进行返回 true，否则返回 false
   */
  bool StartWifiScan() override;

  /**
   * @brief 读取最近一次 hosted WiFi 扫描进度和缓存热点列表
   * @param status 扫描状态输出地址
   * @return 读取成功返回 true，否则返回 false
   */
  bool ReadWifiScanStatus(WifiScanStatus* status) override;

  /**
   * @brief 连接指定的 hosted WiFi 热点
   * @param ssid 目标热点 SSID
   * @param password 目标热点密码，开放热点可传空字符串或 nullptr
   * @return 连接命令发送成功返回 true，否则返回 false
   */
  bool ConnectWifi(const char* ssid, const char* password) override;

  /**
   * @brief 取消当前 hosted WiFi 连接等待状态并保持 STA 启动
   * @return 取消成功或当前无需取消返回 true，否则返回 false
   */
  bool CancelWifiConnection() override;

  /**
   * @brief 启动 WiFi 获取时间测试并连接工厂测试热点
   * @return 启动命令发送成功返回 true，否则返回 false
   */
  bool StartWifiTimeTest() override;

  /**
   * @brief 停止 WiFi 获取时间测试并恢复测试前状态
   * @return 恢复命令发送成功返回 true，否则返回 false
   */
  bool StopWifiTimeTest() override;

  /**
   * @brief 读取 hosted WiFi 连接和 SNTP 时间同步状态
   * @param status WiFi 状态输出地址
   * @return 读取成功返回 true，否则返回 false
   */
  bool ReadWifiStatus(WifiStatus* status) override;

  /**
   * @brief 确保 SD 卡已经挂载到文件系统
   * @return 挂载操作是否成功
   */
  bool EnsureSdCardMounted() override;

  /**
   * @brief 卸载 SD 卡文件系统并释放 SDMMC 总线资源
   * @return 卸载操作是否成功
   */
  bool UnmountSdCard() override;

  /**
   * @brief 判断 SD 卡文件系统是否已经挂载
   * @return 当前挂载状态
   */
  bool IsSdCardMounted() const override;

  /**
   * @brief 获取 SD 卡挂载路径
   * @return SD 卡挂载路径
   */
  const char* SdCardBasePath() const override;

  /**
   * @brief 启动屏幕背光
   * @param initial_percent 初始亮度百分比，范围 0~100
   */
  void StartScreenBacklight(int initial_percent = 100) override;

  /**
   * @brief 设置屏幕背光亮度
   * @param percent 亮度百分比，范围 0~100
   * @return 设置成功返回 true，否则返回 false
   */
  bool SetScreenBrightnessPercent(int percent) override;

  /**
   * @brief 让设备进入芯片睡眠状态
   * @param deep_sleep true 使用深度睡眠级别，false 使用轻度睡眠级别
   * @return 进入成功返回 true，否则返回 false
   */
  bool EnterDeviceSleep(bool deep_sleep = false) override;

  /**
   * @brief 从设备芯片睡眠状态恢复
   * @param deep_sleep true 恢复深度睡眠级别，false 恢复轻度睡眠级别
   * @return 恢复成功返回 true，否则返回 false
   */
  bool ExitDeviceSleep(bool deep_sleep = false) override;

  /**
   * @brief 读取 BOOT 物理按键按下状态
   * @return BOOT 键按下返回 true，否则返回 false
   */
  bool IsLockWakeButtonPressed() override;

 private:
  static constexpr int kScreenReadyTimeoutMs = 5000;
  static constexpr int kScreenReadyPollMs = 20;

  /**
   * @brief 等待异步屏幕初始化进入可用状态
   * @return 屏幕可用返回 true，否则返回 false
   */
  bool WaitForScreenReady();

  /**
   * @brief 判断屏幕是否已经可写入像素
   * @return 屏幕可用返回 true，否则返回 false
   */
  bool IsScreenReady() const;

  /**
   * @brief 判断触摸芯片是否已经可读取
   * @return 触摸可用返回 true，否则返回 false
   */
  bool IsTouchReady() const;

  /**
   * @brief 判断 L76K GPS 模块是否已经初始化完成
   * @return GPS 模块可用返回 true，否则返回 false
   */
  bool IsGpsReady() const;

  /**
   * @brief 扬声器播放任务入口
   * @param context 设备对象指针
   */
  static void SpeakerPlaybackTaskEntry(void* context);

  /**
   * @brief 执行后台扬声器播放
   */
  void RunSpeakerPlaybackTask();

  /**
   * @brief 振动播放任务入口
   * @param context 设备对象指针
   */
  static void HapticPlaybackTaskEntry(void* context);

  /**
   * @brief 执行后台振动播放
   */
  void RunHapticPlaybackTask();

  /**
   * @brief 麦克风采样读取任务入口
   * @param context 设备对象指针
   */
  static void MicrophoneCaptureTaskEntry(void* context);

  /**
   * @brief 执行后台麦克风采样读取
   */
  void RunMicrophoneCaptureTask();

  /**
   * @brief 摄像头预览任务入口
   * @param context 设备对象指针
   */
  static void CameraPreviewTaskEntry(void* context);

  /**
   * @brief 执行摄像头预览任务
   */
  void RunCameraPreviewTask();

  /**
   * @brief 初始化 ESP-IDF camera video 设备
   * @return 初始化成功返回 true，否则返回 false
   */
  bool InitializeCameraPreview();

  /**
   * @brief 关闭 camera video 设备并释放缓冲区
   */
  void DeinitializeCameraPreview();

  /**
   * @brief 处理一帧摄像头数据并更新预览缓冲区
   * @param buffer 摄像头帧缓冲区
   * @param width 摄像头帧宽度
   * @param height 摄像头帧高度
   * @return 处理成功返回 true，否则返回 false
   */
  bool RenderCameraFrame(uint8_t* buffer, uint32_t width, uint32_t height);

  /**
   * @brief 以太网初始化任务入口
   * @param context 设备对象指针
   */
  static void EthernetInitTaskEntry(void* context);

  /**
   * @brief 执行 IP101 以太网异步初始化
   */
  void RunEthernetInitTask();

  /**
   * @brief 初始化 ESP-IDF 以太网驱动和 netif
   * @return 初始化成功返回 ESP_OK，否则返回错误码
   */
  int InitializeEthernetStack();

  /**
   * @brief 记录以太网初始化失败状态
   * @param error 错误码
   */
  void SetEthernetFailure(int error);

  /**
   * @brief 处理以太网链路事件
   * @param arg 设备对象指针
   * @param event_base 事件类型
   * @param event_id 事件 ID
   * @param event_data 事件数据
   */
  static void EthernetEventHandler(
      void* arg, const char* event_base, int32_t event_id, void* event_data);

  /**
   * @brief 处理以太网 DHCP 获取 IP 事件
   * @param arg 设备对象指针
   * @param event_base 事件类型
   * @param event_id 事件 ID
   * @param event_data 事件数据
   */
  static void EthernetGotIpEventHandler(
      void* arg, const char* event_base, int32_t event_id, void* event_data);

  /**
   * @brief hosted WiFi 初始化任务入口
   * @param context 设备对象指针
   */
  static void WifiInitTaskEntry(void* context);

  /**
   * @brief hosted WiFi 扫描任务入口
   * @param context 设备对象指针
   */
  static void WifiScanTaskEntry(void* context);

  /**
   * @brief hosted WiFi 连接任务入口
   * @param context 设备对象指针
   */
  static void WifiConnectTaskEntry(void* context);

  /**
   * @brief 执行 hosted WiFi 异步初始化
   */
  void RunWifiInitTask();

  /**
   * @brief 执行官方示例同款的阻塞 WLAN 扫描流程
   */
  void RunWifiScanTask();

  /**
   * @brief 执行 hosted WiFi 异步连接流程
   */
  void RunWifiConnectTask();

  /**
   * @brief 等待 ESP32-C6 桥接芯片完成上电复位
   * @return 就绪返回 true，否则返回 false
   */
  bool WaitForWifiHardwareReady();

  /**
   * @brief 初始化 hosted WiFi 驱动和默认 STA netif
   * @return 初始化成功返回 ESP_OK，否则返回错误码
  */
  int InitializeWifiStack();

  /**
   * @brief 将 hosted WiFi 驱动切换到已启动的 STA 模式
   * @return 成功返回 ESP_OK，否则返回 ESP-IDF 错误码
   */
  int PrepareWifiStation();

  /**
   * @brief 将 ESP-IDF 扫描结果整理后写入 wifi_ 缓存
   */
  void CopyWifiScanResultsFromDriver();

  /**
   * @brief 连接工厂测试 WiFi 并启动网络时间同步
   * @return 启动成功返回 ESP_OK，否则返回错误码
   */
  int StartWifiTimeTestInternal();

  /**
   * @brief 启动 SNTP 时间同步
   * @return 启动成功返回 ESP_OK，否则返回错误码
   */
  int StartWifiSntp();

  /**
   * @brief 记录 hosted WiFi 初始化或连接失败状态
   * @param error 错误码
   */
  void SetWifiFailure(int error);

  /**
   * @brief 处理 hosted WiFi STA 连接事件
   * @param arg 设备对象指针
   * @param event_base 事件类型
   * @param event_id 事件 ID
   * @param event_data 事件数据
   */
  static void WifiEventHandler(
      void* arg, const char* event_base, int32_t event_id, void* event_data);

  /**
   * @brief 处理 hosted WiFi DHCP 获取 IP 事件
   * @param arg 设备对象指针
   * @param event_base 事件类型
   * @param event_id 事件 ID
   * @param event_data 事件数据
   */
  static void WifiGotIpEventHandler(
      void* arg, const char* event_base, int32_t event_id, void* event_data);

  struct SpeakerState {
    // 播放任务是否正在运行
    std::atomic<bool> running{false};
    // 最近一次播放任务是否已经完成
    std::atomic<bool> completed{false};
    // 最近一次播放是否成功
    std::atomic<bool> success{false};
    // 最近一次写入 I2S 的字节数
    std::atomic<size_t> bytes_written{0};
    // 播放音频总字节数
    std::atomic<size_t> total_bytes{0};
    // 是否循环播放音频预览
    std::atomic<bool> loop_enabled{false};
    // 是否请求停止循环播放音频预览
    std::atomic<bool> stop_requested{false};
  };

  struct HapticState {
    // 振动任务是否正在运行
    std::atomic<bool> running{false};
    // RAM 波形 sequence 编号
    std::atomic<uint8_t> waveform_sequence_number{1};
    // 播放循环次数
    std::atomic<uint8_t> loop_count{1};
    // 振动增益
    std::atomic<uint8_t> gain{255};
    // 是否启用自动制动
    std::atomic<bool> auto_brake{true};
    // 是否已经配置过 RAM 播放参数
    bool ram_playback_configured = false;
    // 已配置的 RAM 波形 sequence 编号
    uint8_t configured_sequence_number = 0;
    // 已配置的播放循环次数
    uint8_t configured_loop_count = 0;
    // 已配置的振动增益
    uint8_t configured_gain = 0;
    // 已配置的自动制动状态
    bool configured_auto_brake = false;
    // 最近一次快速预览启动时间，单位 ms
    std::atomic<uint32_t> last_preview_ms{0};
  };

  struct MicrophoneState {
    // 采样任务是否正在运行
    std::atomic<bool> running{false};
    // 采样任务是否请求停止
    std::atomic<bool> stop_requested{false};
    // ADC 数据是否直通到 DAC
    std::atomic<bool> adc_to_dac_enabled{false};
    // 当前音量百分比
    std::atomic<int> level_percent{0};
    // 当前峰值采样
    std::atomic<int> peak_sample{0};
    // 累计读取字节数
    std::atomic<size_t> bytes_read{0};
  };

  struct HeapCapsBufferDeleter {
    /**
     * @brief 释放 heap_caps 分配的内存
     * @param pointer 内存指针
     */
    void operator()(uint8_t* pointer) const;
  };

  struct CameraPreviewState {
    // 摄像头预览资源是否已经初始化
    std::atomic<bool> initialized{false};
    // 摄像头预览任务是否正在运行
    std::atomic<bool> running{false};
    // 摄像头预览任务是否请求停止
    std::atomic<bool> stop_requested{false};
    // 摄像头预览任务句柄
    TaskHandle_t task_handle = nullptr;
    // video 设备文件描述符
    int video_fd = -1;
    // 摄像头帧宽度
    uint32_t frame_width = 0;
    // 摄像头帧高度
    uint32_t frame_height = 0;
    // 摄像头帧缓冲区长度
    size_t frame_buffer_sizes[2] = {};
    // 摄像头 MMAP 缓冲区地址
    void* frame_buffers[2] = {};
    // PPA 输出缓冲区互斥锁
    SemaphoreHandle_t output_mutex = nullptr;
    // PPA 输出缓冲区
    std::unique_ptr<uint8_t, HeapCapsBufferDeleter> output_buffer;
    // PPA 输出缓冲区长度
    size_t output_buffer_size = 0;
    // PPA 输出图像宽度
    uint32_t output_width = 0;
    // PPA 输出图像高度
    uint32_t output_height = 0;
    // PPA 输出图像 stride
    uint32_t output_stride = 0;
    // PPA 输出图像旋转角度
    int output_rotation_angle = 0;
    // 启动后需要清空 PPA 输出缓冲区的帧数
    uint32_t clear_output_frames_remaining = 0;
    // 预览帧序号
    std::atomic<uint32_t> frame_sequence{0};
    // PPA SRM helper
    PpaSrmHelper ppa;
  };

  struct EthernetState {
    // 初始化任务是否正在运行
    std::atomic<bool> init_task_running{false};
    // 驱动是否已经初始化完成
    std::atomic<bool> driver_initialized{false};
    // 驱动是否已经启动
    std::atomic<bool> running{false};
    // 网线链路是否已经连接
    std::atomic<bool> link_up{false};
    // 是否已经获取 DHCP 地址
    std::atomic<bool> got_ip{false};
    // 启动或连接是否失败
    std::atomic<bool> start_failed{false};
    // 端口数量
    std::atomic<int> port_count{0};
    // 最近一次错误码
    std::atomic<int> last_error{0};
    // MAC 地址打包值
    std::atomic<uint64_t> mac_address{0};
    // DHCP IP 地址
    std::atomic<uint32_t> ip_address{0};
    // DHCP 子网掩码
    std::atomic<uint32_t> netmask{0};
    // DHCP 网关
    std::atomic<uint32_t> gateway{0};
    // ESP-IDF 以太网驱动句柄
    void* handle = nullptr;
  };

  struct WifiState {
    // esp_hosted 桥接组件是否已经初始化完成
    std::atomic<bool> hosted_bridge_initialized{false};
    // hosted WiFi 初始化任务是否正在运行
    std::atomic<bool> init_task_running{false};
    // hosted WiFi 驱动是否已经初始化完成
    std::atomic<bool> driver_initialized{false};
    // hosted WiFi 驱动是否已经启动
    std::atomic<bool> running{false};
    // hosted WiFi STA 是否已经关联到热点
    std::atomic<bool> connected{false};
    // hosted WiFi 是否已经获取 DHCP 地址
    std::atomic<bool> got_ip{false};
    // hosted WiFi 启动或连接是否失败
    std::atomic<bool> start_failed{false};
    // WiFi 连接重试次数
    std::atomic<int> retry_count{0};
    // WiFi 最近一次错误码
    std::atomic<int> last_error{0};
    // WiFi 最近一次断开原因
    std::atomic<int> disconnect_reason{0};
    // WiFi RSSI 信号强度
    std::atomic<int> rssi{0};
    // WiFi 连接信道
    std::atomic<int> channel{0};
    // WiFi STA MAC 地址打包值
    std::atomic<uint64_t> mac_address{0};
    // WiFi DHCP IP 地址
    std::atomic<uint32_t> ip_address{0};
    // WiFi DHCP 子网掩码
    std::atomic<uint32_t> netmask{0};
    // WiFi DHCP 网关
    std::atomic<uint32_t> gateway{0};
    // ESP-IDF WiFi netif 指针
    void* netif = nullptr;
    // 是否正在执行异步 WiFi 扫描。
    std::atomic<bool> scan_running{false};
    // 扫描启动任务或非阻塞扫描完成事件是否还未结束。
    std::atomic<bool> scan_task_running{false};
    // 最近一次 WiFi 扫描是否失败。
    std::atomic<bool> scan_failed{false};
    // 扫描结果版本号，结果刷新后递增。
    std::atomic<uint32_t> scan_generation{0};
    // 当前缓存的有效热点数量。
    std::atomic<size_t> scan_network_count{0};
    // 供 UI 轮询读取的热点扫描缓存。
    WifiNetworkInfo scan_networks[kMaxWifiScanNetworkCount] = {};
    // 最近一次扫描开始 tick，用来在事件丢失时软超时。
    std::atomic<uint32_t> scan_started_tick{0};
    // ReadWifiScanStatus 超时处理是否已经标记，防止扫描任务返回后重复递增 generation
    std::atomic<bool> scan_timeout_handled{false};
    // WiFi 连接任务是否正在后台执行。
    std::atomic<bool> connect_task_running{false};
    // WiFi 连接任务是否已经请求取消。
    std::atomic<bool> connect_cancel_requested{false};
    // 后台连接任务使用的 SSID 副本。
    char connect_ssid[kWifiSsidMaxLength + 1] = {};
    // 后台连接任务使用的密码副本。
    char connect_password[kWifiPasswordMaxLength + 1] = {};
  };

  struct WifiTimeTestState {
    // WiFi 获取时间测试是否已经请求
    std::atomic<bool> requested{false};
    // WiFi 获取时间测试是否正在运行
    std::atomic<bool> active{false};
    // SNTP 时间同步是否已经启动
    std::atomic<bool> sync_started{false};
    // SNTP 是否已经获取到有效时间
    std::atomic<bool> synced{false};
    // SNTP 最新一次从网络同步到的 UTC Unix 时间戳
    std::atomic<int64_t> sntp_unix_time{0};
    // SNTP 最新一次同步完成时的单调时间，单位为毫秒
    std::atomic<int64_t> sntp_sync_monotonic_ms{0};
    // 进入测试前驱动是否已启动
    bool previous_running = false;
    // 进入测试前 STA 是否已连接
    bool previous_connected = false;
    // 进入测试前工作模式是否有效
    bool previous_mode_valid = false;
    // 进入测试前 STA 配置是否有效
    bool previous_sta_config_valid = false;
    // 进入测试前工作模式
    wifi_mode_t previous_mode = WIFI_MODE_NULL;
    // 进入测试前 STA 配置
    wifi_config_t previous_sta_config = {};
  };

  lilygo_device_driver::TDisplayP4Driver& driver_;
  std::unique_ptr<cpp_bus_driver::Tool> tool_;
  ScreenProviderFlushReadyHandler flush_ready_handler_;
  // 扬声器播放状态，供 UI 和后台播放任务共享
  SpeakerState speaker_;
  // 振动播放状态，供 UI 和后台播放任务共享
  HapticState haptic_;
  // 麦克风采样状态，供 UI 和后台采样任务共享
  MicrophoneState microphone_;
  // 摄像头预览状态，供 UI 和后台预览任务共享
  CameraPreviewState camera_preview_;
  // 以太网运行状态，供事件回调和 UI 查询共享
  EthernetState ethernet_;
  // WiFi 运行状态，供事件回调和 UI 查询共享
  WifiState wifi_;
  // WiFi 获取时间测试状态，保存测试流程和进入前配置
  WifiTimeTestState wifi_time_test_;
  bool gps_running_ = false;
  GpsStatus gps_status_;
};

}  // namespace lilygo_box::hal
