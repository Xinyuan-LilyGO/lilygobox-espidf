/*
 * @Description: None
 * @Author: LILYGO_L
 * @Date: 2026-05-10 13:27:05
 * @LastEditTime: 2026-05-10 23:46:34
 * @License: GPL 3.0
 */
#pragma once

#include <cstdint>

namespace lilygo_box::hal {

class DeviceDiagnosticsProvider;

// Logical touch coordinate in screen pixels.
struct TouchPoint {
  int16_t x = 0;
  int16_t y = 0;
};

using ScreenFlushReadyCallback = void (*)(void* context);

// Callback payload kept by the concrete screen implementation.
struct ScreenFlushReadyHandler {
  ScreenFlushReadyCallback callback = nullptr;
  void* context = nullptr;
};

// Hardware-neutral screen contract used by the application and LVGL port.
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
   * @brief 启动屏幕背光
   * @return
   * @Date 2026-05-10 13:01:03
   */
  virtual void StartBacklight() = 0;
};

}  // namespace lilygo_box::hal
