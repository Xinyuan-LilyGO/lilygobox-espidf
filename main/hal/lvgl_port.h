/*
 * @Description: LVGL 显示、触摸与任务端口管理接口
 * @Author: LILYGO_L
 * @Date: 2026-05-10 13:27:05
 * @LastEditTime: 2026-07-17 02:10:57
 * @License: GPL 3.0
 */
#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "hal/ppa/ppa_srm_helper.h"
#include "hal/providers/screen_provider.h"
#include "lvgl.h"
#include "sys/lock.h"

namespace lilygo_box::hal {

class LvglPort final {
 public:
  LvglPort() = default;
  ~LvglPort();

  /**
   * @brief 初始化 LVGL 显示、输入和 tick timer
   * @param screen 屏幕设备对象
   * @return 初始化成功返回 true，否则返回 false
   */
  bool Init(ScreenProvider* screen);

  /**
   * @brief 启动 LVGL 任务
   * @return 启动成功返回 true，否则返回 false
   */
  bool Start();

  /**
   * @brief 判断当前 LVGL 输入是否带有硬件边缘触摸标志。
   * @return 当前输入来自硬件边缘触摸检测返回 true，否则返回 false。
   */
  static bool ActiveInputEdgeTouch();

  /**
   * @brief 获取 LVGL 显示对象
   * @return LVGL 显示对象指针
   */
  lv_display_t* lvgl_display() const { return lvgl_display_; }

  /**
   * @brief 设置 LVGL 指针输入是否屏蔽
   * @param blocked true 表示屏蔽触摸输入，false 表示恢复输入
   */
  void SetInputBlocked(bool blocked);

  /**
   * @brief 判断 LVGL 指针输入是否已被屏蔽
   * @return 输入已被屏蔽返回 true
   */
  bool IsInputBlocked() const;

  /**
   * @brief 读取 LVGL 最近一次缓存的有效触摸点
   * @param point 触摸点输出地址
   * @return 当前缓存状态为按下返回 true
   */
  bool ReadCachedTouch(TouchPoint* point) const;

  /**
   * @brief 为一个熄屏所有者增加输入屏蔽引用
   */
  void AcquireSleepInputBlock();

  /**
   * @brief 释放一个熄屏所有者持有的输入屏蔽引用
   */
  void ReleaseSleepInputBlock();

  /**
   * @brief 独占一次屏幕休眠或唤醒硬件转换
   * @return 取得转换互斥锁返回 true，否则返回 false
   */
  bool BeginScreenTransition();

  /**
   * @brief 非阻塞尝试独占一次屏幕硬件转换
   * @return 立即取得转换互斥锁返回 true
   */
  bool TryBeginScreenTransition();

  /**
   * @brief 结束当前屏幕硬件转换
   */
  void EndScreenTransition();

  /**
   * @brief 暂停新的硬件显示刷新并等待当前刷新结束
   * @return 显示刷新已安全暂停返回 true，否则返回 false
   */
  bool PauseDisplayFlush();

  /**
   * @brief 恢复向物理屏幕发送 LVGL 刷新
   */
  void ResumeDisplayFlush();

  /**
   * @brief 恢复硬件刷新并等待专用 LVGL 任务完成首个完整帧
   * @return 最后一个异步 flush 已完成返回 true，超时返回 false
   */
  bool ResumeDisplayFlushAndWaitForRefresh();

  /**
   * @brief 判断是否仍有熄屏所有者暂停物理屏幕刷新
   * @return 存在暂停引用返回 true
   */
  bool IsDisplayFlushPaused() const;

  /**
   * @brief 设置 LVGL 软件旋转角度
   * @param angle 旋转角度，须为 0/90/180/270
   */
  void SetDisplayRotation(int angle);

  /**
   * @brief 锁定 LVGL API 访问
   */
  void Lock();

  /**
   * @brief 解锁 LVGL API 访问
   */
  void Unlock();

 private:
  static constexpr int kLvglTickPeriodMs = 1;
  static constexpr int kLvglTaskStackBytes = 16 * 1024;
  static constexpr UBaseType_t kLvglTaskPriority = 1;
  static constexpr uint32_t kMinimumHandlerDelayMs = 10;
  static constexpr uint32_t kFlushPauseTimeoutMs = 1000;
  static constexpr uint32_t kDisplayRefreshTimeoutMs = 1000;

  /**
   * @brief 处理 LVGL flush 回调
   * @param lvgl_display LVGL 显示对象
   * @param area 待刷新的屏幕区域
   * @param pixel_map 像素数据地址
   */
  static void FlushCallback(
      lv_display_t* lvgl_display, const lv_area_t* area, uint8_t* pixel_map);

  /**
   * @brief 处理屏幕 flush 完成回调
   * @param context 回调上下文
   */
  static void FlushReadyCallback(void* context);

  /**
   * @brief 读取 LVGL 指针输入状态
   * @param indev LVGL 输入设备
   * @param data 输入数据输出地址
   */
  static void TouchReadCallback(lv_indev_t* indev, lv_indev_data_t* data);

  /**
   * @brief 处理 LVGL tick 定时器回调
   * @param context 回调上下文
   */
  static void TickCallback(void* context);

  /**
   * @brief 进入 LVGL 任务入口
   * @param arg 任务参数
   */
  static void TaskEntry(void* arg);

  /**
   * @brief 获取当前 LVGL 颜色格式
   * @return LVGL 颜色格式
   */
  lv_color_format_t ColorFormat() const;

  /**
   * @brief 获取当前 PPA 颜色格式
   * @return PPA 颜色格式
   */
  ppa_srm_color_mode_t PpaColorMode() const;

  /**
   * @brief 获取绘制缓冲区字节数
   * @return 绘制缓冲区字节数
   */
  size_t DrawBufferSize() const;

  /**
   * @brief 运行 LVGL 任务循环
   */
  void TaskLoop();

  /**
   * @brief 减少暂停引用并请求专用 LVGL 任务执行全屏刷新
   * @return 本次刷新代次，未真正恢复硬件刷新返回 0
   */
  uint32_t ResumeDisplayFlushInternal();

  /**
   * @brief 获取旋转后的临时缓冲区
   * @param size 需要的缓冲区大小
   * @return 缓冲区地址，分配失败返回 nullptr
   */
  void* EnsureRotationBuffer(size_t size);

  /**
   * @brief 使用 PPA 或 LVGL 软件旋转刷新区域
   * @param lvgl_display LVGL 显示对象
   * @param area 待旋转区域
   * @param pixel_map 原始像素数据
   * @param rotated_area 旋转后的区域输出
   * @param rotated_pixel_map 旋转后的像素数据输出
   * @return 旋转成功返回 true
   */
  bool RotateFlushBuffer(lv_display_t* lvgl_display, const lv_area_t* area,
      uint8_t* pixel_map, lv_area_t* rotated_area, uint8_t** rotated_pixel_map);

  ScreenProvider* screen_ = nullptr;
  lv_display_t* lvgl_display_ = nullptr;
  lv_indev_t* input_device_ = nullptr;
  // 专用 LVGL 任务句柄，用于在恢复刷新时立即唤醒渲染循环。
  TaskHandle_t task_handle_ = nullptr;
  std::atomic<bool> input_blocked_{false};
  // 多个熄屏所有者共享的输入屏蔽引用数。
  std::atomic<uint32_t> sleep_input_block_count_{0};
  // 多个熄屏所有者共享的 LVGL 硬件刷新暂停引用数。
  std::atomic<uint32_t> display_flush_pause_count_{0};
  // 标记旋转处理或异步面板传输仍在执行，供熄屏流程等待。
  std::atomic<bool> display_flush_in_progress_{false};
  // 恢复显示后请求 LVGL 任务在自己的锁内执行一次全屏重绘。
  std::atomic<bool> display_refresh_requested_{false};
  // 每次恢复硬件刷新时递增，用于区分不同的首帧请求。
  std::atomic<uint32_t> display_refresh_request_generation_{0};
  // 当前由 LVGL 任务绘制并等待异步传输结束的首帧代次。
  std::atomic<uint32_t> display_refresh_rendering_generation_{0};
  // FlushReadyCallback 已确认完整传输结束的首帧代次。
  std::atomic<uint32_t> display_refresh_completed_generation_{0};
  // 当前首帧的任一刷新区域是否未能提交到物理面板。
  std::atomic<bool> display_refresh_failed_{false};
  std::atomic<bool> active_edge_touch_flag_{false};
  std::atomic<bool> pending_edge_touch_flag_{false};
  std::atomic<bool> has_last_touch_point_{false};
  // 跨任务读取的触摸坐标快照，低 16 位为 X，高 16 位为 Y。
  std::atomic<uint32_t> cached_touch_coordinates_{0};
  bool ppa_rotation_available_ = false;
  PpaSrmHelper ppa_rotation_;
  void* rotation_buffer_ = nullptr;
  size_t rotation_buffer_size_ = 0;
  lv_point_t last_touch_point_ = {};
  _lock_t lock_ = nullptr;
  // 串行化面板休眠、唤醒以及相邻的熄屏存储事务。
  StaticSemaphore_t screen_transition_mutex_buffer_;
  SemaphoreHandle_t screen_transition_mutex_ = nullptr;
};

}  // namespace lilygo_box::hal
