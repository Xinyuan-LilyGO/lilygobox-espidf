/*
 * @Description: None
 * @Author: LILYGO_L
 * @Date: 2026-05-12 21:20:00
 * @LastEditTime: 2026-05-12 22:15:00
 * @License: GPL 3.0
 */
#pragma once

#include "lvgl.h"

namespace lilygo_box::ui {

struct BackGestureInfo {
  lv_dir_t direction = LV_DIR_NONE;
  lv_point_t start_point = {};
};

constexpr int kBackGestureMinEdgeWidth = 36;
constexpr int kBackGestureMaxEdgeWidth = 76;
constexpr int kBackGestureWidthDivisor = 9;

/**
 * @brief 根据屏幕宽度计算返回手势边缘区域宽度
 * @param screen_width 屏幕宽度
 * @return 边缘区域宽度
 * @Date 2026-05-12 21:20:00
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
 * @brief 读取当前 LVGL 返回手势信息
 * @param indev LVGL 输入设备
 * @param info 手势信息输出
 * @return 读取成功返回 true，否则返回 false
 * @Date 2026-05-12 21:20:00
 */
bool ReadBackGestureInfo(const lv_indev_t* indev, BackGestureInfo* info);

/**
 * @brief 判断手势是否从屏幕边缘向内返回
 * @param info 手势信息
 * @param screen_width 屏幕宽度
 * @return 符合边缘返回返回 true，否则返回 false
 * @Date 2026-05-12 21:20:00
 */
bool IsBackGestureFromEdge(
    const BackGestureInfo& info, int screen_width);

}  // namespace lilygo_box::ui
