/*
 * @Description: 屏幕边缘返回手势状态与事件接口
 * @Author: LILYGO_L
 * @Date: 2026-05-12 21:20:00
 * @LastEditTime: 2026-05-18 12:00:00
 * @License: GPL 3.0
 */
#pragma once

#include "lvgl.h"

namespace lilygo_box::ui {

// 边缘返回滑动状态
struct EdgeBackSwipeState {
  // 手指按下时的起点，用于计算横向返回距离。
  lv_point_t start_point = {};
  // 真实按下点，用于判断手指是否真的连续滑动，避免硬件边缘标志导致点击误显示动画。
  lv_point_t gesture_start_point = {};
  // 指示器固定的垂直中心点，避免横向滑出后跟随手指上下漂移。
  int indicator_center_y = 0;
  // 是否正在跟踪一次从屏幕边缘开始的触摸。
  bool tracking = false;
  // 是否已经进入有效侧滑阶段。
  bool active = false;
  // 当前手势是否已经播放过完整显示振动反馈。
  bool haptic_feedback_played = false;
  // 手势是否从左边缘开始。
  bool from_left_edge = false;
  // 手势是否从右边缘开始。
  bool from_right_edge = false;
};

constexpr int kBackGestureMinEdgeWidth = 36;
constexpr int kBackGestureMaxEdgeWidth = 76;
constexpr int kBackGestureWidthDivisor = 9;
constexpr int kBackGestureIndicatorStartDistance = 18;
constexpr int kBackGestureMinSwipeDistance = 90;
constexpr int kBackGestureMaxVerticalOffset = 140;

/**
 * @brief 根据屏幕宽度计算返回手势边缘区域宽度
 * @param screen_width 屏幕宽度
 * @return 边缘区域宽度
 */
inline int BackGestureEdgeWidth(int screen_width) {
  int edge_width = screen_width / kBackGestureWidthDivisor;
  if (edge_width < kBackGestureMinEdgeWidth) {
    edge_width = kBackGestureMinEdgeWidth;
  }
  if (edge_width > kBackGestureMaxEdgeWidth) {
    edge_width = kBackGestureMaxEdgeWidth;
  }
  return edge_width;
}

/**
 * @brief 处理按下和释放事件并判断是否完成边缘返回滑动
 * @param event LVGL 事件对象
 * @param screen_width 屏幕宽度
 * @param state 边缘返回滑动状态
 * @return 完成边缘返回滑动返回 true，否则返回 false
 */
bool HandleEdgeBackSwipeEvent(
    lv_event_t* event, int screen_width, EdgeBackSwipeState* state);

/**
 * @brief 给对象添加边缘返回滑动事件并设置为可点击
 * @param object LVGL 对象
 * @param callback 事件回调
 * @param user_data 事件用户数据
 */
void AddEdgeBackSwipeEvents(
    lv_obj_t* object, lv_event_cb_t callback, void* user_data);

/**
 * @brief 递归开启对象和子对象的事件冒泡
 * @param object LVGL 对象
 */
void EnableEdgeBackSwipeEventBubble(lv_obj_t* object);

}  // namespace lilygo_box::ui
