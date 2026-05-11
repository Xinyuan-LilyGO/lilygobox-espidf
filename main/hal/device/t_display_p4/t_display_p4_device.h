/*
 * @Description: None
 * @Author: LILYGO_L
 * @Date: 2026-05-10 13:27:05
 * @LastEditTime: 2026-05-10 23:51:34
 * @License: GPL 3.0
 */
#pragma once

#include "hal/device_diagnostics.h"
#include "hal/screen_device.h"
#include "t_display_p4_driver.h"

namespace lilygo_box::hal {

// ScreenDevice implementation for Lilygo T-Display-P4.
class TDisplayP4Device final : public ScreenDevice,
                               public DeviceDiagnosticsProvider {
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
  int width() const override { return SCREEN_WIDTH; }

  /**
   * @brief 获取屏幕高度
   * @return 屏幕高度，单位为像素
   * @Date 2026-05-10 13:01:03
   */
  int height() const override { return SCREEN_HEIGHT; }

  /**
   * @brief 获取单个像素的位数
   * @return 单个像素的位数
   * @Date 2026-05-10 13:01:03
   */
  int bits_per_pixel() const override { return SCREEN_BITS_PER_PIXEL; }

  /**
   * @brief 获取设备诊断提供者
   * @return 设备诊断提供者指针
   * @Date 2026-05-10 13:01:03
   */
  DeviceDiagnosticsProvider* diagnostics_provider() override { return this; }

  /**
   * @brief 注册屏幕 flush 完成回调
   * @param callback flush 完成时调用的回调函数
   * @param callback_context 回调上下文
   * @return 注册成功返回 true，否则返回 false
   * @Date 2026-05-10 13:01:03
   */
  bool RegisterFlushReadyCallback(
      ScreenFlushReadyCallback callback, void* callback_context) override;

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
   * @brief 读取设备诊断快照
   * @param diagnostics 诊断数据输出地址
   * @return 读取到有效诊断数据返回 true，否则返回 false
   * @Date 2026-05-10 13:01:03
   */
  bool ReadDiagnostics(DeviceDiagnostics* diagnostics) override;

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

  lilygo_device_driver::TDisplayP4Driver& driver_;
  ScreenFlushReadyHandler flush_ready_handler_;
};

}  // namespace lilygo_box::hal
