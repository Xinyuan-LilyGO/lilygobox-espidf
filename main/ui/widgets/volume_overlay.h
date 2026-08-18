/*
 * @Description: 系统音量快捷浮层
 * @Author: LILYGO_L
 * @Date: 2026-08-17 00:00:00
 * @LastEditTime: 2026-08-17 00:00:00
 * @License: GPL 3.0
 */
#pragma once

#include <functional>

#include "lvgl.h"

namespace lilygo_box::ui {

class VolumeOverlay final {
 public:
  using VolumeChangeCallback = std::function<bool(int, bool)>;

  /**
   * @brief 显示或刷新音量浮层并重新开始自动收起计时
   * @param parent 浮层父对象
   * @param screen_width 当前屏幕逻辑宽度
   * @param screen_height 当前屏幕逻辑高度
   * @param volume_percent 当前音量百分比
   * @param callback 音量变化回调，第二个参数表示是否保存最终值
   * @return 浮层创建并开始显示时返回 true
   */
  bool Show(lv_obj_t* parent, int screen_width, int screen_height,
      int volume_percent, VolumeChangeCallback callback);

  /**
   * @brief 删除音量浮层并停止相关动画和计时器
   */
  void Reset();

 private:
  /**
   * @brief 转发原生音量滑动条事件到当前浮层实例
   * @param event LVGL 事件对象
   */
  static void SliderEventCallback(lv_event_t* event);

  /**
   * @brief 转发音量图标点击事件到当前浮层实例
   * @param event LVGL 事件对象
   */
  static void IconEventCallback(lv_event_t* event);

  /**
   * @brief 在音量面板被删除时清理对象和定时器引用
   * @param event LVGL 删除事件对象
   */
  static void PanelDeletedEventCallback(lv_event_t* event);

  /**
   * @brief 自动隐藏计时结束后启动音量面板收起动画
   * @param timer LVGL 自动隐藏定时器
   */
  static void AutoHideTimerCallback(lv_timer_t* timer);

  /**
   * @brief 定时检查指针输入并在面板外按下时收起浮层
   * @param timer LVGL 指针输入检查定时器
   */
  static void InputMonitorTimerCallback(lv_timer_t* timer);

  /**
   * @brief 音量面板收起动画完成后隐藏面板对象
   * @param animation LVGL 动画对象
   */
  static void HideAnimationCompletedCallback(lv_anim_t* animation);

  /**
   * @brief 设置音量面板动画过程中的水平坐标
   * @param object 音量面板对象
   * @param x 目标 X 坐标
   */
  static void SetPanelX(void* object, int32_t x);

  /**
   * @brief 创建音量面板、原生滑动条和底部图标
   * @param parent 浮层父对象
   * @return 全部对象创建成功时返回 true
   */
  bool Create(lv_obj_t* parent);

  /**
   * @brief 通过应用层回调应用音量并刷新浮层状态
   * @param volume_percent 目标音量百分比
   * @param commit true 表示保存最终值，false 表示仅更新硬件
   * @return 应用层成功接受音量值时返回 true
   */
  bool ApplyVolume(int volume_percent, bool commit);

  /**
   * @brief 根据当前屏幕尺寸更新音量面板位置和大小
   * @param screen_width 当前屏幕逻辑宽度
   * @param screen_height 当前屏幕逻辑高度
   */
  void UpdateLayout(int screen_width, int screen_height);

  /**
   * @brief 根据当前音量更新滑动条、图标和颜色
   */
  void UpdateVisuals();

  /**
   * @brief 重新开始音量面板自动收起计时
   */
  void RestartAutoHideTimer();

  /**
   * @brief 播放音量面板从屏幕右侧向左进入的动画
   */
  void StartShowAnimation();

  /**
   * @brief 播放音量面板向屏幕右侧收起的动画
   */
  void StartHideAnimation();

  /**
   * @brief 处理原生音量滑动条的按下、拖动和释放事件
   * @param event LVGL 事件对象
   */
  void HandleSliderEvent(lv_event_t* event);

  /**
   * @brief 检查当前指针位置并处理面板外的新按下操作
   */
  void HandlePointerInput();

  /**
   * @brief 处理音量图标点击并切换静音或恢复先前音量
   */
  void HandleIconClick();

  // 音量变更由应用层统一写入硬件和持久化存储。
  VolumeChangeCallback volume_change_callback_;
  lv_obj_t* panel_ = nullptr;
  lv_obj_t* slider_ = nullptr;
  lv_obj_t* icon_label_ = nullptr;
  lv_timer_t* auto_hide_timer_ = nullptr;
  lv_timer_t* input_monitor_timer_ = nullptr;
  int screen_width_ = 0;
  int screen_height_ = 0;
  int panel_width_ = 0;
  int panel_height_ = 0;
  int visible_x_ = 0;
  int hidden_x_ = 0;
  int volume_percent_ = 0;
  int last_nonzero_volume_percent_ = 50;
  bool updating_slider_ = false;
  bool pointer_pressed_ = false;
  // 缓存离散图标状态，避免拖动时重复重绘未变化的标签。
  bool visual_state_initialized_ = false;
  bool muted_visual_ = false;
  bool icon_on_fill_visual_ = false;
};

}  // namespace lilygo_box::ui
