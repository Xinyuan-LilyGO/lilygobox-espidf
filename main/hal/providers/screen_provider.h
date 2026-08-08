/*
 * @Description: 屏幕显示、背光与触摸输入抽象接口
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
  bool edge_touch_flag = false;
};

using ScreenProviderDisplayCallback = void (*)(void*);

struct ScreenProviderDisplayCallbacks {
  ScreenProviderDisplayCallback flush_ready_callback = nullptr;
  ScreenProviderDisplayCallback refresh_done_callback = nullptr;
  void* callback_context = nullptr;
};

class ScreenProvider {
 public:
  virtual ~ScreenProvider() = default;

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
   * @brief 注册像素传输完成和物理画面刷新完成回调
   * @param callbacks 屏幕显示回调集合
   * @return 注册成功返回 true，否则返回 false
   */
  virtual bool RegisterScreenDisplayCallbacks(
      const ScreenProviderDisplayCallbacks& callbacks) = 0;

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
   * @brief 设置屏幕亮度
   * @param percent 亮度百分比，范围 0~100
   * @return 设置成功返回 true，否则返回 false
   */
  virtual bool SetScreenBrightnessPercent(int percent) = 0;

  /**
   * @brief 将屏幕亮度渐变到目标值
   * @param target_percent 目标亮度百分比，范围 0~100
   * @param duration_ms 渐变持续时间
   * @return 渐变成功返回 true，否则返回 false
   */
  virtual bool FadeScreenBrightnessPercent(
      int target_percent, uint32_t duration_ms) = 0;

  /**
   * @brief 让设备进入芯片睡眠状态
   * @param deep_sleep true 使用深度睡眠级别，false 使用轻度睡眠级别
   * @return 进入成功返回 true，否则返回 false
   */
  virtual bool EnterDeviceSleep(bool deep_sleep = false) = 0;

  /**
   * @brief 从设备芯片睡眠状态恢复
   * @param deep_sleep true 恢复深度睡眠级别，false 恢复轻度睡眠级别
   * @return 恢复成功返回 true，否则返回 false
   */
  virtual bool ExitDeviceSleep(bool deep_sleep = false) = 0;

};

}  // namespace lilygo_box::hal
