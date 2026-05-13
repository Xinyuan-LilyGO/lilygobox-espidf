/*
 * @Description: None
 * @Author: LILYGO_L
 * @Date: 2026-05-10 13:27:05
 * @LastEditTime: 2026-05-12 23:00:02
 * @License: GPL 3.0
 */
#pragma once

#include <cstddef>
#include <cstdint>

namespace lilygo_box::hal {

class DeviceDiagnosticsProvider;

// 屏幕像素坐标系中的逻辑触摸点
struct TouchPoint {
  uint8_t id = 0;
  int16_t x = 0;
  int16_t y = 0;
  uint8_t pressure = 0;
};

using ScreenFlushReadyCallback = void (*)(void* context);

// 具体屏幕实现保存的 flush 完成回调信息
struct ScreenFlushReadyHandler {
  ScreenFlushReadyCallback callback = nullptr;
  void* context = nullptr;
};

// 扬声器测试播放状态
struct SpeakerTestPlaybackStatus {
  bool running = false;
  bool completed = false;
  bool success = false;
  size_t bytes_written = 0;
  size_t total_bytes = 0;
};

// 麦克风测试读取状态
struct MicrophoneTestStatus {
  bool running = false;
  bool adc_to_dac_enabled = false;
  int level_percent = 0;
  int peak_sample = 0;
  size_t bytes_read = 0;
};

class ScreenDevice {
 public:
  virtual ~ScreenDevice() = default;

  /**
   * @brief 初始化屏幕设备到可被 LVGL 使用的状态
   * @return 初始化成功返回 true，否则返回 false
   * @Date 2026-05-10 13:01:03
   */
  virtual bool Init() = 0;

  /**
   * @brief 获取屏幕设备名称
   * @return 屏幕设备名称字符串
   * @Date 2026-05-10 13:01:03
   */
  virtual const char* name() const = 0;

  /**
   * @brief 获取屏幕宽度
   * @return 屏幕宽度，单位为像素
   * @Date 2026-05-10 13:01:03
   */
  virtual int width() const = 0;

  /**
   * @brief 获取屏幕高度
   * @return 屏幕高度，单位为像素
   * @Date 2026-05-10 13:01:03
   */
  virtual int height() const = 0;

  /**
   * @brief 获取单个像素的位数
   * @return 单个像素的位数
   * @Date 2026-05-10 13:01:03
   */
  virtual int bits_per_pixel() const = 0;

  /**
   * @brief 获取设备诊断提供者
   * @return 诊断提供者指针，不支持诊断时返回 nullptr
   * @Date 2026-05-10 13:01:03
   */
  virtual DeviceDiagnosticsProvider* diagnostics_provider() { return nullptr; }

  /**
   * @brief 播放振动测试波形
   * @param waveform_count 实际播放的 RAM 波形数量输出地址
   * @return 播放成功返回 true，否则返回 false
   * @Date 2026-05-13 18:20:00
   */
  virtual bool PlayVibrationTest(uint8_t* waveform_count);

  /**
   * @brief 播放扬声器测试音频
   * @param bytes_written 实际写入 I2S 的字节数输出地址
   * @return 播放成功返回 true，否则返回 false
   * @Date 2026-05-13 16:55:00
   */
  virtual bool PlaySpeakerTest(size_t* bytes_written);

  /**
   * @brief 创建后台任务播放扬声器测试音频
   * @return 任务创建成功返回 true，否则返回 false
   * @Date 2026-05-13 21:00:00
   */
  virtual bool StartSpeakerTest();

  /**
   * @brief 读取扬声器测试播放状态
   * @param status 播放状态输出地址
   * @return 读取成功返回 true，否则返回 false
   * @Date 2026-05-13 21:00:00
   */
  virtual bool ReadSpeakerTestStatus(SpeakerTestPlaybackStatus* status);

  /**
   * @brief 创建后台任务读取麦克风测试数据
   * @return 任务创建成功返回 true，否则返回 false
   * @Date 2026-05-13 21:20:00
   */
  virtual bool StartMicrophoneTest();

  /**
   * @brief 停止麦克风测试并关闭 ADC 到 DAC 直通
   * @return 停止命令发送成功返回 true，否则返回 false
   * @Date 2026-05-13 21:20:00
   */
  virtual bool StopMicrophoneTest();

  /**
   * @brief 设置麦克风 ADC 数据是否直通到 DAC
   * @param enable true 表示打开直通，false 表示关闭直通
   * @return 设置成功返回 true，否则返回 false
   * @Date 2026-05-13 21:20:00
   */
  virtual bool SetMicrophoneAdcToDac(bool enable);

  /**
   * @brief 读取麦克风测试状态
   * @param status 麦克风测试状态输出地址
   * @return 读取成功返回 true，否则返回 false
   * @Date 2026-05-13 21:20:00
   */
  virtual bool ReadMicrophoneTestStatus(MicrophoneTestStatus* status);

  /**
   * @brief 注册屏幕 flush 完成回调
   * @param callback flush 完成时调用的回调函数
   * @param callback_context 回调上下文
   * @return 注册成功返回 true，否则返回 false
   * @Date 2026-05-10 13:01:03
   */
  virtual bool RegisterFlushReadyCallback(
      ScreenFlushReadyCallback callback, void* callback_context) = 0;

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
  virtual bool WritePixels(
      int x_start, int y_start, int x_end, int y_end, const void* pixels) = 0;

  /**
   * @brief 读取当前触摸点
   * @param point 触摸点输出地址
   * @return 读取到触摸点返回 true，否则返回 false
   * @Date 2026-05-10 13:01:03
   */
  virtual bool ReadTouch(TouchPoint* point) = 0;

  /**
   * @brief 读取当前多个触摸点
   * @param points 触摸点输出数组
   * @param max_points 输出数组可容纳的最大触点数量
   * @param point_count 实际读取到的触点数量输出地址
   * @return 读取到至少一个触摸点返回 true，否则返回 false
   * @Date 2026-05-13 09:55:00
   */
  virtual bool ReadTouchPoints(
      TouchPoint* points, size_t max_points, size_t* point_count);

  /**
   * @brief 启动屏幕背光
   * @return
   * @Date 2026-05-10 13:01:03
   */
  virtual void StartBacklight() = 0;
};

}  // namespace lilygo_box::hal
