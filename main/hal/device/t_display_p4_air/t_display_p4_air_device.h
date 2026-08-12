/*
 * @Description: T-Display-P4-Air 设备及硬件 Provider 适配接口
 * @Author: LILYGO_L
 * @Date: 2026-05-10 13:27:05
 * @LastEditTime: 2026-08-06 18:11:44
 * @License: GPL 3.0
 */
#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

#include "audio/mp3_decoder.h"
#include "driver/rmt_encoder.h"
#include "driver/rmt_rx.h"
#include "driver/rmt_tx.h"
#include "esp_cam_sensor_types.h"
#include "esp_timer.h"
#include "esp_wifi_types.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "hal/ppa/ppa_srm_helper.h"
#include "hal/providers/providers.h"
#include "hal/usb/usb_storage_manager.h"
#include "t_display_p4_air_driver.h"

namespace lilygo_box::hal {

using TDisplayP4AirBoardDriver = lilygo_device_driver::TDisplayP4AirDriver;

class TDisplayP4AirDevice final : public ScreenProvider,
                                  public DeviceProvider,
                                  public DeviceDiagnosticsProvider,
                                  public DeviceInfoProvider,
                                  public GpsProvider,
                                  public ImuProvider,
                                  public AudioProvider,
                                  public HapticProvider,
                                  public CameraProvider,
                                  public CellularProvider,
                                  public BatteryManagementProvider,
                                  public InfraredProvider,
                                  public NfcProvider,
                                  public RadioProvider,
                                  public WifiProvider,
                                  public StorageProvider,
                                  private audio::PcmOutput {
 public:
  /**
   * @brief 创建使用独立 Air 板级驱动的设备适配对象
   */
  TDisplayP4AirDevice();

  /**
   * @brief 初始化 T-Display-P4-Air 到屏幕可用状态
   * @return 初始化成功返回 true，否则返回 false
   */
  bool InitDevice() override;

  /**
   * @brief 完成设备关机准备并通过 AXP517 运输模式切断电池供电。
   * @return 运输模式请求成功时返回 kWaitForPowerCut，否则返回 kFailed。
   */
  PowerOffAction RequestPowerOff() override;

  /**
   * @brief 读取当前 T-Display-P4-Air 设备信息
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
  bool PlayHapticWaveform(uint8_t waveform_sequence_number, uint8_t loop_count,
      uint8_t gain, bool auto_brake) override;

  /**
   * @brief 播放 ES8389 扬声器音频提示
   * @param bytes_written 实际写入 I2S 的字节数输出地址
   * @return 播放成功返回 true，否则返回 false
   */
  bool PlaySpeakerTone(size_t* bytes_written) override;

  /**
   * @brief 创建后台任务播放 ES8389 扬声器音频
   * @return 任务创建成功返回 true，否则返回 false
   */
  bool StartSpeakerTone() override;

  /**
   * @brief 创建后台任务循环播放 ES8389 扬声器音频预览
   * @return 任务创建成功或已经在播放返回 true，否则返回 false
   */
  bool StartSpeakerToneLoop() override;

  /**
   * @brief 停止后台循环播放 ES8389 扬声器音频预览
   * @return 停止命令发送成功返回 true，否则返回 false
   */
  bool StopSpeakerToneLoop() override;

  /**
   * @brief 设置 ES8389 扬声器播放音量百分比
   * @param percent 音量百分比，范围 0~100
   * @return 设置成功返回 true，否则返回 false
   */
  bool SetSpeakerVolumePercent(int percent) override;

  /**
   * @brief 读取 ES8389 扬声器播放状态
   * @param status 播放状态输出地址
   * @return 读取成功返回 true，否则返回 false
   */
  bool ReadSpeakerToneStatus(SpeakerStatus* status) override;

  /**
   * @brief 创建后台任务解码并播放 MP3 文件
   * @param path MP3 文件绝对路径
   * @param duration_ms 音频总时长，单位毫秒
   * @return 任务创建成功返回 true，否则返回 false
   */
  bool StartAudioFile(const char* path, uint32_t duration_ms) override;

  /**
   * @brief 暂停当前 MP3 文件播放
   * @return 暂停成功返回 true，否则返回 false
   */
  bool PauseAudioFile() override;

  /**
   * @brief 恢复当前 MP3 文件播放
   * @return 恢复成功返回 true，否则返回 false
   */
  bool ResumeAudioFile() override;

  /**
   * @brief 请求将当前 MP3 文件定位到指定播放时间
   * @param position_ms 目标播放时间，单位毫秒
   * @return 定位请求发送成功返回 true，否则返回 false
   */
  bool SeekAudioFile(uint32_t position_ms) override;

  /**
   * @brief 请求停止当前 MP3 文件播放
   * @return 停止请求发送成功返回 true，否则返回 false
   */
  bool StopAudioFile() override;

  /**
   * @brief 读取当前 MP3 文件播放状态
   * @param status 播放状态输出地址
   * @return 读取成功返回 true，否则返回 false
   */
  bool ReadAudioFileStatus(AudioFilePlaybackStatus* status) override;

  /**
   * @brief 创建后台任务读取 ES8389 麦克风采样数据
   * @return 任务创建成功返回 true，否则返回 false
   */
  bool StartMicrophone() override;

  /**
   * @brief 停止 ES8389 麦克风采样并关闭 ADC PCM 到 DAC 的实时转送
   * @return 停止命令发送成功返回 true，否则返回 false
   */
  bool StopMicrophone() override;

  /**
   * @brief 设置是否将 ES8389 麦克风 ADC PCM 数据实时转送到 DAC
   * @param enable true 表示打开实时转送，false 表示关闭实时转送
   * @return 设置成功返回 true，否则返回 false
   */
  bool SetAudioAdcToDac(bool enable) override;

  /**
   * @brief 读取 ES8389 麦克风状态
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
   * @brief 获取最近一次摄像头预览启动错误
   * @return 摄像头错误
   */
  CameraError GetCameraPreviewError() const override;

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
   * @brief 启动或停止 nRF9151 GNSS 定位模式
   * @param enabled true 启动 GNSS，false 停止 GNSS
   * @return 状态切换成功或目标状态已经满足返回 true
   */
  bool SetGpsEnabled(bool enabled) override;

  /**
   * @brief 读取板载 GNSS 模块状态和最新解析数据
   * @param status GPS 测试状态输出地址
   * @return 读取成功返回 true，否则返回 false
   */
  bool ReadGpsStatus(GpsStatus* status) override;

  /**
   * @brief 启动或停止 ST25R3916 NFC 后台发现
   * @param enabled true 启动轮询，false 停止轮询并关闭射频场
   * @return 请求成功接受或目标状态已经满足返回 true
   */
  bool SetNfcPollingEnabled(bool enabled) override;

  /**
   * @brief 非阻塞读取 ST25R3916 轮询和最近卡片状态
   * @param status NFC 状态输出地址
   * @return 状态读取成功返回 true，否则返回 false
   */
  bool ReadNfcStatus(NfcStatus* status) override;

  /**
   * @brief 启动或停止板载红外接收
   * @param enabled true 连续接收，false 停止接收
   * @return 状态切换成功或目标状态已经满足返回 true
   */
  bool SetInfraredReceiverEnabled(bool enabled) override;

  /**
   * @brief 使用板载红外发射器发送标准 NEC 指令
   * @param address NEC 八位地址
   * @param command NEC 八位命令
   * @return 完整帧发送成功返回 true，否则返回 false
   */
  bool SendInfraredNec(uint8_t address, uint8_t command) override;

  /**
   * @brief 非阻塞读取最近的红外接收状态
   * @param status 红外状态输出地址
   * @return 状态读取成功返回 true，否则返回 false
   */
  bool ReadInfraredStatus(InfraredStatus* status) override;

  /**
   * @brief 异步启动或停止 nRF9151 蜂窝网络管理
   * @param enabled true 启动蜂窝模式，false 停止并关闭模块电源
   * @return 请求成功接受或目标状态已经满足返回 true
   */
  bool SetCellularEnabled(bool enabled) override;

  /**
   * @brief 非阻塞读取 nRF9151 蜂窝网络状态
   * @param status 蜂窝状态输出地址
   * @return 状态读取成功返回 true，否则返回 false
   */
  bool ReadCellularStatus(CellularStatus* status) override;

  /**
   * @brief 向已经启用的 nRF9151 发送一条 AT 指令
   * @param command 不包含换行符的 AT 指令
   * @param response AT 完整响应输出缓冲区
   * @param response_size 输出缓冲区容量
   * @param timeout_ms 等待最终响应的超时时间
   * @return 收到 OK 最终响应返回 true，否则返回 false
   */
  bool SendCellularCommand(const char* command, char* response,
      size_t response_size, uint32_t timeout_ms) override;

  /**
   * @brief 注册像素传输完成和物理画面刷新完成回调
   * @param callbacks 屏幕显示回调集合
   * @return 注册成功返回 true，否则返回 false
   */
  bool RegisterScreenDisplayCallbacks(
      const ScreenProviderDisplayCallbacks& callbacks) override;

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
   * @brief 判断是否已启用板载触摸中断通知
   * @return 触摸中断可用返回 true，否则返回 false
   */
  bool SupportsTouchInterrupt() const override;

  /**
   * @brief 消费一个来自 HI8561 TCH_ATTN 的触摸报告通知
   * @return 存在新的触摸报告通知返回 true，否则返回 false
   */
  bool ConsumeTouchInterrupt() override;

  /**
   * @brief 读取设备诊断快照
   * @param diagnostics 诊断数据输出地址
   * @return 读取到有效诊断数据返回 true，否则返回 false
   */
  bool ReadDeviceDiagnostics(DeviceDiagnostics* diagnostics) override;

  /**
   * @brief 读取电池管理状态
   * @param status 电池管理状态输出地址
   * @return 读取到有效电池管理状态返回 true，否则返回 false
   */
  bool ReadBatteryManagementStatus(BatteryManagementStatus* status) override;

  /**
   * @brief 读取当前有效的电池电量百分比
   * @param percent 电量百分比输出地址，范围为 0 到 100
   * @return 检测到电池且电量读取成功返回 true，否则返回 false
   */
  bool ReadBatteryLevel(int* percent) override;

  /**
   * @brief 读取板载 LR1121 支持的射频协议和负载能力
   * @param capabilities 射频能力输出地址
   * @return 能力信息读取成功时返回 true
   */
  bool ReadRadioCapabilities(RadioCapabilities* capabilities) override;

  /**
   * @brief 配置板载 LR1121 并启动指定射频会话的连续接收
   * @param config 待激活的射频配置
   * @return 配置成功且连续接收已启动时返回 true
   */
  bool ActivateRadio(const RadioConfig& config) override;

  /**
   * @brief 停止当前射频会话并将板载 LR1121 切换到待机状态
   * @return 停止成功或 LR1121 无需处理时返回 true
   */
  bool DeactivateRadio() override;

  /**
   * @brief 使用板载 LR1121 启动一条可关联异步事件的射频发送
   * @param data 待发送数据
   * @param size 数据长度
   * @param request_token 调用方提供的发送请求唯一序号
   * @return 发送命令成功启动时返回 true
   */
  bool SendRadio(
      const uint8_t* data, size_t size, uint64_t request_token) override;

  /**
   * @brief 非阻塞轮询板载 LR1121 的收发和芯片错误事件
   * @param event 射频事件输出地址，无事件时类型保持为 kNone
   * @return 轮询及必要的硬件状态处理成功时返回 true
   */
  bool PollRadioEvent(RadioEvent* event) override;

  /**
   * @brief 读取板载 LR1121 的会话、硬件和发送状态
   * @param status 射频状态输出地址
   * @return 状态读取成功时返回 true
   */
  bool ReadRadioStatus(RadioStatus* status) override;

  /**
   * @brief 启动或停止 BHI260AP 与 QMC6310N 姿态传感器
   * @param enabled true 启动传感器，false 停止传感器
   * @return 状态切换成功或目标状态已经满足返回 true
   */
  bool SetImuEnabled(bool enabled) override;

  /**
   * @brief 读取 Air 板组合姿态传感器的最近状态
   * @param status IMU 状态输出地址
   * @return 读取到有效状态返回 true，否则返回 false
   */
  bool ReadImuStatus(ImuStatus* status) override;

  /**
   * @brief 启动或停止由 ESP32-C5 提供的 hosted WiFi
   * @param enabled true 启动 WiFi，false 停止 WiFi
   * @return 状态切换请求成功返回 true，否则返回 false
   */
  bool SetWifiEnabled(bool enabled) override;

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
   * @brief 对当前普通 WiFi 连接主动发起一次 SNTP 入网复检
   * @return 复检请求发送成功返回 true，否则返回 false
   */
  bool RequestWifiInternetCheck() override;

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
   * @brief 启动 USB Host MSC 监控并自动挂载接入的 U 盘
   * @return 监控已经运行或启动任务创建成功返回 true，否则返回 false
   */
  bool StartUsbStorage() override;

  /**
   * @brief 停止 USB Host MSC 监控并关闭 USB Host 供电
   * @return USB 资源和供电均成功释放返回 true，否则返回 false
   */
  bool StopUsbStorage() override;

  /**
   * @brief 读取当前已经挂载的 USB 存储设备快照
   * @param snapshot 快照输出地址
   * @return 读取成功返回 true，否则返回 false
   */
  bool ReadUsbStorageSnapshot(UsbStorageSnapshot* snapshot) const override;

  /**
   * @brief 设置屏幕亮度
   * @param percent 亮度百分比，范围 0~100
   * @return 设置成功返回 true，否则返回 false
   */
  bool SetScreenBrightnessPercent(int percent) override;

  /**
   * @brief 将屏幕亮度渐变到目标值
   * @param target_percent 目标亮度百分比，范围 0~100
   * @param duration_ms 渐变持续时间
   * @return 渐变成功返回 true，否则返回 false
   */
  bool FadeScreenBrightnessPercent(
      int target_percent, uint32_t duration_ms) override;

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

 private:
  enum class AuxiliaryAudioOutput : uint8_t {
    kNone,
    kSpeakerTone,
    kMicrophoneLoopback,
  };

  static constexpr int kScreenReadyTimeoutMs = 5000;
  static constexpr int kScreenReadyPollMs = 20;
  static constexpr int kPowerOffTaskTimeoutMs = 5000;
  static constexpr int kPowerOffTaskPollMs = 20;

  /**
   * @brief 初始化 HI8561 TCH_ATTN 对应的 ESP32-P4 GPIO 中断
   * @return 初始化成功返回 true，否则返回 false
   */
  bool InitializeTouchInterrupt();

  /**
   * @brief 记录 HI8561 触摸中断，实际 I2C 读取由任务上下文完成
   * @param context 当前设备对象
   */
  static void TouchInterruptHandler(void* context);

  /**
   * @brief 等待异步屏幕初始化进入可用状态
   * @return 屏幕可用返回 true，否则返回 false
   */
  bool WaitForScreenReady();

  /**
   * @brief 等待异步触摸初始化进入可用状态
   * @return 触摸可用返回 true，否则返回 false
   */
  bool WaitForTouchReady();

  /**
   * @brief 停止仍可能访问外设的后台任务和网络协议栈
   * @return 所有停止请求完成且任务退出返回 true，否则返回 false
   */
  bool PrepareForPowerOff();

  /**
   * @brief 等待关机相关异步任务退出
   * @return 在超时前全部退出返回 true，否则返回 false
   */
  bool WaitForPowerOffTasks();

  /**
   * @brief 扬声器播放任务入口
   * @param context 设备对象指针
   */
  static void SpeakerPlaybackTaskEntry(void* context);

  /**
   * @brief 在 MP3 保持暂停时播放扬声器测试音的任务入口
   * @param context 设备对象指针
   */
  static void PausedAudioSpeakerToneTaskEntry(void* context);

  /**
   * @brief 执行后台扬声器播放
   */
  void RunSpeakerPlaybackTask();

  /**
   * @brief 在暂停的 MP3 上方启动独立扬声器测试音
   * @param loop_enabled 是否持续循环播放
   * @return 后台测试音任务创建成功返回 true，否则返回 false
   */
  bool StartPausedAudioSpeakerTone(bool loop_enabled);

  /**
   * @brief 在不终止暂停 MP3 任务的情况下播放扬声器测试音
   */
  void RunPausedAudioSpeakerToneTask();

  /**
   * @brief 尝试独占音乐之外的临时音频输出
   * @param output 请求占用输出的功能
   * @return 成功取得输出返回 true，否则返回 false
   */
  bool TryAcquireAuxiliaryAudioOutput(AuxiliaryAudioOutput output);

  /**
   * @brief 释放音乐之外的临时音频输出
   * @param output 当前持有输出的功能
   */
  void ReleaseAuxiliaryAudioOutput(AuxiliaryAudioOutput output);

  /**
   * @brief 等待暂停的 MP3 解码任务停止写入音频设备
   * @return 暂停状态稳定返回 true，否则返回 false
   */
  bool WaitForPausedAudioFile();

  /**
   * @brief 根据扬声器和麦克风的实际占用情况选择 ES8389 工作模式
   * @return 工作模式更新成功返回 true，否则返回 false
   */
  bool UpdateAudioCodecOperatingMode();

  /**
   * @brief 根据 MP3 流参数配置 ES8389 PCM 输出
   * @param sample_rate_hz 采样率
   * @param channel_count 声道数
   * @param bits_per_sample 采样位宽
   * @return 配置成功返回 true，否则返回 false
   */
  bool Configure(uint32_t sample_rate_hz, uint8_t channel_count,
      uint8_t bits_per_sample) override;

  /**
   * @brief 等待 MP3 播放从暂停状态恢复
   * @return 可以继续播放返回 true，请求停止返回 false
   */
  bool WaitUntilReady() override;

  /**
   * @brief 读取并清除一个待处理的 MP3 定位请求
   * @param position_ms 目标播放时间输出地址，单位毫秒
   * @return 存在待处理请求返回 true，否则返回 false
   */
  bool TakeSeekRequest(uint32_t* position_ms) override;

  /**
   * @brief 向 ES8389 写入解码后的 PCM 数据
   * @param data PCM 数据地址
   * @param size PCM 数据字节数
   * @return 完整写入返回 true，否则返回 false
   */
  bool Write(const uint8_t* data, size_t size) override;

  /**
   * @brief 更新当前 MP3 文件已播放时间
   * @param elapsed_ms 已播放时间，单位毫秒
   */
  void UpdateProgress(uint32_t elapsed_ms) override;

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
   * @brief ST25R3916 NFC 轮询任务入口
   * @param context 设备对象指针
   */
  static void NfcPollingTaskEntry(void* context);

  /**
   * @brief 执行 ST25R3916 NFC 发现和卡片状态维护
   */
  void RunNfcPollingTask();

  /**
   * @brief 处理 RMT 红外接收完成中断
   * @param channel RMT 接收通道
   * @param event_data 接收完成数据
   * @param context 设备对象指针
   * @return 是否唤醒更高优先级任务
   */
  static bool InfraredReceiveDoneCallback(rmt_channel_handle_t channel,
      const rmt_rx_done_event_data_t* event_data, void* context);

  /**
   * @brief 创建并启用红外 RMT 收发资源
   * @return 初始化成功或资源已经可用返回 true
   */
  bool InitializeInfraredHardware();

  /**
   * @brief 提交下一次非阻塞红外接收事务
   * @return 接收已经挂起或提交成功返回 true
   */
  bool StartInfraredReceive();

  /**
   * @brief nRF9151 蜂窝管理任务入口
   * @param context 设备对象指针
   */
  static void CellularTaskEntry(void* context);

  /**
   * @brief 执行 nRF9151 蜂窝模式初始化和周期状态查询
   */
  void RunCellularTask();

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
   * @brief 等待 ESP32-C5 桥接芯片完成上电复位
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
   * @brief 停止当前一次 SNTP 入网检测并保留同步结果
   */
  void StopWifiInternetCheck();

  /**
   * @brief 启动 SNTP 三次检测调度定时器
   * @return 启动成功返回 ESP_OK，否则返回 ESP-IDF 错误码
   */
  int StartWifiSntpAttemptTimer();

  /**
   * @brief 触发下一次 SNTP 检测或在第三次检测结束后停止客户端
   * @param argument 设备对象指针
   */
  static void WifiSntpAttemptTimerCallback(void* argument);

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
    enum class PlaybackKind {
      kNone,
      kTone,
      kToneLoop,
      kAudioFile,
    };

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
    // 当前播放内容类型，用于让提示音和 MP3 共用同一播放任务
    std::atomic<PlaybackKind> playback_kind{PlaybackKind::kNone};
    // MP3 文件播放是否暂停
    std::atomic<bool> paused{false};
    // MP3 解码任务是否已经停止向音频设备写入数据
    std::atomic<bool> pause_acknowledged{false};
    // 暂停音乐上方的独立扬声器测试音是否正在播放
    std::atomic<bool> tone_overlay_running{false};
    // 独立扬声器测试音是否持续循环播放
    std::atomic<bool> tone_overlay_loop_enabled{false};
    // 是否请求停止独立扬声器测试音
    std::atomic<bool> tone_overlay_stop_requested{false};
    // 当前独占临时音频输出的功能
    std::atomic<AuxiliaryAudioOutput> auxiliary_output{
        AuxiliaryAudioOutput::kNone};
    // MP3 文件播放状态
    std::atomic<AudioFilePlaybackState> file_state{
        AudioFilePlaybackState::kStopped};
    // MP3 文件已播放时间，单位毫秒
    std::atomic<uint32_t> elapsed_ms{0};
    // MP3 文件总时长，单位毫秒
    std::atomic<uint32_t> duration_ms{0};
    // 是否存在尚未由解码任务处理的定位请求
    std::atomic<bool> seek_requested{false};
    // MP3 文件待定位的播放时间，单位毫秒
    std::atomic<uint32_t> seek_position_ms{0};
    // 当前 ES8389 输出采样率
    std::atomic<uint32_t> sample_rate_hz{44100};
    // 当前扬声器音量百分比，ES8389 重新唤醒后用于恢复用户设置。
    std::atomic<int> volume_percent{100};
    // 当前 MP3 文件绝对路径
    char audio_file_path[512] = {};
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
    // ESP Video 的 video0 和 video20 是否已经完成一次性初始化
    std::atomic<bool> video_system_initialized{false};
    // 最近一次摄像头预览启动错误
    std::atomic<CameraError> error{CameraError::kNone};
    // 组件会长期保存该地址，用于摄像头重新上电后恢复传感器格式
    esp_cam_sensor_format_t sensor_format{};
    // 摄像头预览资源是否已经初始化
    std::atomic<bool> initialized{false};
    // 在创建任务前置位，确保停止预览时也会等待尚未开始运行的任务退出
    std::atomic<bool> task_active{false};
    // 摄像头预览任务是否正在运行
    std::atomic<bool> running{false};
    // 摄像头预览任务是否请求停止
    std::atomic<bool> stop_requested{false};
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
    // 摄像头上电后需要丢弃的预热帧数
    uint32_t warmup_frames_remaining = 0;
    // 预览帧序号
    std::atomic<uint32_t> frame_sequence{0};
    // PPA SRM helper
    PpaSrmHelper ppa;
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
    // 每次取得 DHCP 地址后递增的连接版本号
    std::atomic<uint32_t> connection_generation{0};
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
    // 保护扫描结果数组，避免事件任务写入时 UI 同时读取。
    SemaphoreHandle_t scan_results_mutex = nullptr;
    // 用户是否已经请求关闭 WiFi。
    std::atomic<bool> stop_requested{false};
    // WiFi 初始化完成后是否需要立即启动一次扫描。
    std::atomic<bool> scan_requested{false};
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
    // 当前一轮入网检测已经启动的 SNTP 尝试次数
    std::atomic<int> sntp_attempt_count{0};
    // 每 10 秒触发下一次尝试，并在 30 秒时结束检测
    esp_timer_handle_t sntp_attempt_timer = nullptr;
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

  struct RadioState {
    // 保护射频配置、发送事务和芯片状态。
    SemaphoreHandle_t mutex = nullptr;
    // 当前激活 Radio 配置的稳定 ID。
    uint32_t active_client_token = 0;
    // 当前发送消息的唯一序号，空闲时为 0。
    uint64_t transmit_request_token = 0;
    // 当前发送的软件看门狗截止时间，单位为微秒。
    int64_t transmit_deadline_us = 0;
    // 当前激活配置的 LoRa 调制和数据包参数。
    LoraRadioConfig lora_config;
    // 最近一次成功执行的 LR1121 Sub-GHz 镜像校准区间。
    uint16_t calibrated_image_minimum_mhz = 0;
    uint16_t calibrated_image_maximum_mhz = 0;
    // LR1121 是否处于连续接收或发送会话。
    bool active = false;
    // LR1121 是否正在执行发送命令。
    bool transmitting = false;
    // 最近一次芯片操作是否需要重新激活配置。
    bool chip_error = false;
  };

  struct ImuState {
    // 保护传感器配置、FIFO 解析结果和磁力计采样。
    SemaphoreHandle_t mutex = nullptr;
    // BHI260AP 加速度虚拟传感器是否已经完成配置。
    bool configured = false;
    // 最近一次 BHI260AP 三轴加速度，单位为 g。
    float acceleration[3] = {};
    // 最近一次 QMC6310N 三轴磁场读数。
    float magnetic_field[3] = {};
    // 是否已经取得有效加速度样本。
    bool acceleration_ready = false;
    // 是否已经取得有效磁场样本。
    bool magnetic_field_ready = false;
  };

  struct NfcState {
    // 保护 NFC 状态快照，RFAL 调用只由轮询任务执行。
    SemaphoreHandle_t mutex = nullptr;
    // NFC 轮询任务是否仍占用对象。
    std::atomic<bool> task_active{false};
    // 是否请求轮询任务停止并关闭射频场。
    std::atomic<bool> stop_requested{false};
    // 供应用层非阻塞读取的 NFC 状态快照。
    NfcStatus status;
  };

  struct InfraredState {
    // 保护 RMT 资源创建、解码结果和状态快照。
    SemaphoreHandle_t mutex = nullptr;
    // 红外接收 RMT 通道。
    rmt_channel_handle_t receive_channel = nullptr;
    // 红外发送 RMT 通道。
    rmt_channel_handle_t transmit_channel = nullptr;
    // 直接复制 NEC symbol 的 RMT 编码器。
    rmt_encoder_handle_t copy_encoder = nullptr;
    // RMT 接收目标缓冲区。
    rmt_symbol_word_t receive_symbols[64] = {};
    // 接收通道当前是否处于启用状态。
    bool receive_channel_enabled = false;
    // 最近一次完成事务中的 symbol 数量。
    std::atomic<size_t> received_symbol_count{0};
    // 是否存在尚未完成的非阻塞接收事务。
    std::atomic<bool> receive_pending{false};
    // 中断是否已经提交一组待解码 symbol。
    std::atomic<bool> receive_complete{false};
    // 用户是否要求连续监听红外信号。
    std::atomic<bool> receiver_enabled{false};
    // 供应用层读取的红外状态快照。
    InfraredStatus status;
  };

  struct CellularState {
    // 保护蜂窝状态快照，避免 UI 读取到一半更新的数据。
    SemaphoreHandle_t status_mutex = nullptr;
    // 蜂窝管理任务是否仍占用对象。
    std::atomic<bool> task_active{false};
    // 是否请求蜂窝任务停止并关闭模块电源。
    std::atomic<bool> stop_requested{false};
    // 供应用层非阻塞读取的蜂窝状态快照。
    CellularStatus status;
  };

  /**
   * @brief 接收 BHI260AP FIFO 加速度数据并写入当前设备状态
   * @param callback_info Bosch FIFO 解析数据
   * @param context 当前设备对象
   */
  static void Bhi260apAccelerationCallback(
      const struct bhy2_fifo_parse_data_info* callback_info, void* context);

  // Air 设备独占的底层板级驱动实例。
  TDisplayP4AirBoardDriver& driver_;
  // 底层驱动异步初始化与任务调度工具。
  std::unique_ptr<cpp_bus_driver::Tool> tool_;
  // SD 卡与 USB Host MSC 的统一存储管理器。
  UsbStorageManager usb_storage_manager_;
  // LVGL 端注册的像素传输和物理刷新回调。
  ScreenProviderDisplayCallbacks display_callbacks_;
  // HI8561 TCH_ATTN 中断是否已经完成注册。
  bool touch_interrupt_initialized_ = false;
  // 中断服务等待任务上下文处理的通知标志。
  std::atomic<bool> touch_interrupt_pending_{false};
  // 轻度熄屏期间是否启用了触摸固件双击唤醒。
  bool touch_gesture_wake_enabled_ = false;
  // 扬声器播放状态，供 UI 和后台播放任务共享
  SpeakerState speaker_;
  // 振动播放状态，供 UI 和后台播放任务共享
  HapticState haptic_;
  // 麦克风采样状态，供 UI 和后台采样任务共享
  MicrophoneState microphone_;
  // 摄像头预览状态，供 UI 和后台预览任务共享
  CameraPreviewState camera_preview_;
  // WiFi 运行状态，供事件回调和 UI 查询共享
  WifiState wifi_;
  // WiFi 获取时间测试状态，保存测试流程和进入前配置
  WifiTimeTestState wifi_time_test_;
  // Radio 会话状态，单芯片只允许一个活动配置。
  RadioState radio_;
  // BHI260AP 与 QMC6310N 组合姿态状态。
  ImuState imu_;
  // Air 板 ST25R3916 后台轮询状态。
  NfcState nfc_;
  // Air 板红外 NEC 收发状态。
  InfraredState infrared_;
  // Air 板 nRF9151 蜂窝状态。
  CellularState cellular_;
  std::atomic<bool> imu_enabled_{false};
  // 保护 nRF9151 的 AT 指令和异步 NMEA 串口数据，避免 GNSS 与蜂窝业务抢占。
  SemaphoreHandle_t nrf9151_mutex_ = nullptr;
  // Air 板使用独立解析器处理 nRF9151 输出的标准 NMEA 语句。
  cpp_bus_driver::GnssParser gps_parser_;
  // 保存尚未接收到换行符的 nRF9151 UART 半包。
  std::string gps_pending_data_;
  bool gps_running_ = false;
  GpsStatus gps_status_;
};

}  // namespace lilygo_box::hal
