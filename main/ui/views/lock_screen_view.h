/*
 * @Description: None
 * @Author: LILYGO_L
 * @Date: 2026-05-10 13:27:05
 * @LastEditTime: 2026-06-25 10:18:00
 * @License: GPL 3.0
 */
#pragma once

#include "lvgl.h"

namespace lilygo_box::ui {

struct LockScreenViewOptions {
  int screen_width = 0;
  int screen_height = 0;
  const char* time_text = "09:15";
  const char* date_text = "June 21th";
  const char* week_text = "Sat";
};

/**
 * @brief 创建锁屏覆盖页面
 * @param parent 父对象
 * @param options 锁屏视图配置
 * @return 创建成功返回对象指针，否则返回 nullptr
 */
lv_obj_t* CreateLockScreenView(lv_obj_t* parent,
    const LockScreenViewOptions& options);

/**
 * @brief 更新锁屏页面时间日期文本
 * @param lock_screen 锁屏页面对象
 * @param time_text 时间文本
 * @param date_text 日期文本
 * @param week_text 星期文本
 */
void UpdateLockScreenViewClock(lv_obj_t* lock_screen, const char* time_text,
    const char* date_text, const char* week_text);

/**
 * @brief 根据视觉上滑拖拽距离更新锁屏页面位置
 * @param lock_screen 锁屏页面对象
 * @param offset Y 轴偏移，负数表示向上移动
 */
void SetLockScreenDragOffset(lv_obj_t* lock_screen, int offset);

/**
 * @brief 播放锁屏页面回弹动画
 * @param lock_screen 锁屏页面对象
 */
void StartLockScreenResetAnimation(lv_obj_t* lock_screen);

/**
 * @brief 播放锁屏页面上滑退出动画
 * @param lock_screen 锁屏页面对象
 */
void StartLockScreenUnlockAnimation(lv_obj_t* lock_screen);

}  // namespace lilygo_box::ui
