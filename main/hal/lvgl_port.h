/*
 * @Description: None
 * @Author: LILYGO_L
 * @Date: 2026-05-10 13:27:05
 * @LastEditTime: 2026-05-10 23:51:34
 * @License: GPL 3.0
 */
#pragma once

#include <cstddef>

#include "hal/screen_device.h"
#include "lvgl.h"
#include "sys/lock.h"

namespace lilygo_box::hal {

// Bridges LVGL rendering and input callbacks to a ScreenDevice.
class LvglPort final {
 public:
  LvglPort() = default;

  /**
   * @brief 初始化 LVGL 显示、输入和 tick timer
   * @param screen 屏幕设备对象
   * @return 初始化成功返回 true，否则返回 false
   * @Date 2026-05-10 13:01:03
   */
  bool Init(ScreenDevice* screen);

  /**
   * @brief 启动 LVGL 任务
   * @return 启动成功返回 true，否则返回 false
   * @Date 2026-05-10 13:01:03
   */
  bool Start();

  /**
   * @brief 获取 LVGL 显示对象
   * @return LVGL 显示对象指针
   * @Date 2026-05-10 13:01:03
   */
  lv_display_t* lvgl_display() const { return lvgl_display_; }

  /**
   * @brief 锁定 LVGL API 访问
   * @return
   * @Date 2026-05-10 13:01:03
   */
  void Lock();

  /**
   * @brief 解锁 LVGL API 访问
   * @return
   * @Date 2026-05-10 13:01:03
   */
  void Unlock();

 private:
  /**
   * @brief 处理 LVGL flush 回调
   * @param lvgl_display LVGL 显示对象
   * @param area 待刷新的屏幕区域
   * @param pixel_map 像素数据地址
   * @return
   * @Date 2026-05-10 13:01:03
   */
  static void FlushCallback(
      lv_display_t* lvgl_display, const lv_area_t* area, uint8_t* pixel_map);

  /**
   * @brief 处理屏幕 flush 完成回调
   * @param context 回调上下文
   * @return
   * @Date 2026-05-10 13:01:03
   */
  static void FlushReadyCallback(void* context);

  /**
   * @brief 读取 LVGL 指针输入状态
   * @param indev LVGL 输入设备
   * @param data 输入数据输出地址
   * @return
   * @Date 2026-05-10 13:01:03
   */
  static void TouchReadCallback(lv_indev_t* indev, lv_indev_data_t* data);

  /**
   * @brief 处理 LVGL tick 定时器回调
   * @param context 回调上下文
   * @return
   * @Date 2026-05-10 13:01:03
   */
  static void TickCallback(void* context);

  /**
   * @brief 进入 LVGL 任务入口
   * @param arg 任务参数
   * @return
   * @Date 2026-05-10 13:01:03
   */
  static void TaskEntry(void* arg);

  /**
   * @brief 获取当前 LVGL 颜色格式
   * @return LVGL 颜色格式
   * @Date 2026-05-10 13:01:03
   */
  lv_color_format_t ColorFormat() const;

  /**
   * @brief 获取绘制缓冲区行数
   * @return 绘制缓冲区行数
   * @Date 2026-05-10 13:01:03
   */
  int DrawBufferRows() const;

  /**
   * @brief 获取绘制缓冲区字节数
   * @return 绘制缓冲区字节数
   * @Date 2026-05-10 13:01:03
   */
  size_t DrawBufferSize() const;

  /**
   * @brief 运行 LVGL 任务循环
   * @return
   * @Date 2026-05-10 13:01:03
   */
  void TaskLoop();

  ScreenDevice* screen_ = nullptr;
  lv_display_t* lvgl_display_ = nullptr;
  lv_indev_t* input_device_ = nullptr;
  _lock_t lock_ = nullptr;
};

}  // namespace lilygo_box::hal
