/*
 * @Description: None
 * @Author: LILYGO_L
 * @Date: 2026-05-10 13:27:05
 * @LastEditTime: 2026-05-15 18:00:00
 * @License: GPL 3.0
 */
#pragma once

#include <cstddef>
#include <cstdint>

namespace lilygo_box::hal {

struct TouchPoint {
  uint8_t id = 0;
  int16_t x = 0;
  int16_t y = 0;
  uint8_t pressure = 0;
};

using ScreenProviderFlushReadyCallback = void (*)(void* context);

struct ScreenProviderFlushReadyHandler {
  ScreenProviderFlushReadyCallback callback = nullptr;
  void* context = nullptr;
};

class ScreenProvider {
 public:
  virtual ~ScreenProvider() = default;

  /**
   * @brief 获取屏幕设备名称
   * @return 屏幕设备名称字符串
   */
  virtual const char* ScreenName() const = 0;

  /**
   * @brief 获取当前屏幕类型名称
   * @return 屏幕类型名称字符串，未知时返回 unknown
   */
  virtual const char* ScreenType() const { return "unknown"; }

  /**
   * @brief 获取屏幕宽度
   * @return 屏幕宽度，单位为像素
   */
  virtual int ScreenWidth() const = 0;

  /**
   * @brief 获取屏幕高度
   * @return 屏幕高度，单位为像素
   */
  virtual int ScreenHeight() const = 0;

  /**
   * @brief 获取单个像素的位数
   * @return 单个像素的位数
   */
  virtual int ScreenBitsPerPixel() const = 0;

  /**
   * @brief 注册屏幕 flush 完成回调
   * @param callback flush 完成时调用的回调函数
   * @param callback_context 回调上下文
   * @return 注册成功返回 true，否则返回 false
   */
  virtual bool RegisterScreenFlushReadyCallback(
      ScreenProviderFlushReadyCallback callback, void* callback_context) = 0;

  /**
   * @brief 写入指定屏幕区域的像素数据
   * @param x_start 起始 X 坐标
   * @param y_start 起始 Y 坐标
   * @param x_end 结束 X 坐标
   * @param y_end 结束 Y 坐标
   * @param pixels 像素数据地址
   * @return 写入成功返回 true，否则返回 false
   */
  virtual bool WriteScreenPixels(
      int x_start, int y_start, int x_end, int y_end, const void* pixels) = 0;

  /**
   * @brief 读取当前触摸点
   * @param point 触摸点输出地址
   * @return 读取到触摸点返回 true，否则返回 false
   */
  virtual bool ReadScreenTouch(TouchPoint* point) = 0;

  /**
   * @brief 读取当前多个触摸点
   * @param points 触摸点输出数组
   * @param max_points 输出数组可容纳的最大触点数量
   * @param point_count 实际读取到的触点数量输出地址
   * @return 读取到至少一个触摸点返回 true，否则返回 false
   */
  virtual bool ReadScreenTouchPoints(
      TouchPoint* points, size_t max_points, size_t* point_count);

  /**
   * @brief 启动屏幕背光
   */
  virtual void StartScreenBacklight() = 0;
};

}  // namespace lilygo_box::hal
