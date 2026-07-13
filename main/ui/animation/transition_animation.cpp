/*
 * @Description: None
 * @Author: LILYGO_L
 * @Date: 2026-05-16 18:20:00
 * @LastEditTime: 2026-05-16 18:35:00
 * @License: GPL 3.0
 */
#include "ui/animation/transition_animation.h"

namespace lilygo_box::ui {
namespace {

// 页面切换动画配置
struct TransitionAnimationConfig {
  // 执行动画的 LVGL 对象。
  lv_obj_t* object = nullptr;
  // 动画起始 X 坐标。
  int32_t start_value = 0;
  // 动画结束 X 坐标。
  int32_t end_value = 0;
  // 动画时长，单位为毫秒。
  uint32_t duration_ms = 0;
  // 动画缓动路径回调。
  lv_anim_path_cb_t path_callback = lv_anim_path_ease_out;
  // 传递给完成回调的用户数据。
  void* user_data = nullptr;
  // 动画完成回调。
  lv_anim_completed_cb_t completed_callback = nullptr;
};

/**
 * @brief 设置对象 X 坐标
 * @param object LVGL 对象
 * @param x X 坐标
 */
void SetTransitionX(void* object, int32_t x) {
  lv_obj_set_x(static_cast<lv_obj_t*>(object), x);
}

/**
 * @brief 启动页面切换动画
 * @param config 动画配置
 * @return 启动成功返回 true，否则返回 false
 */
bool StartTransitionAnimation(const TransitionAnimationConfig& config) {
  if (config.object == nullptr) {
    return false;
  }

  lv_anim_delete(config.object, SetTransitionX);
  SetTransitionX(config.object, config.start_value);

  lv_anim_t animation;
  lv_anim_init(&animation);
  lv_anim_set_var(&animation, config.object);
  lv_anim_set_values(&animation, config.start_value, config.end_value);
  lv_anim_set_duration(&animation, config.duration_ms);
  if (config.path_callback != nullptr) {
    lv_anim_set_path_cb(&animation, config.path_callback);
  }
  lv_anim_set_exec_cb(&animation, SetTransitionX);
  lv_anim_set_user_data(&animation, config.user_data);
  if (config.completed_callback != nullptr) {
    lv_anim_set_completed_cb(&animation, config.completed_callback);
  }
  return lv_anim_start(&animation) != nullptr;
}

/**
 * @brief 删除指定对象上的页面切换动画
 * @param object LVGL 对象
 */
void DeleteTransitionAnimation(lv_obj_t* object) {
  if (object == nullptr) {
    return;
  }

  lv_anim_delete(object, SetTransitionX);
}

}  // namespace

bool StartSlideLeftWindowTransition(lv_obj_t* object, int32_t distance,
    uint32_t duration_ms, void* user_data,
    lv_anim_completed_cb_t completed_callback) {
  TransitionAnimationConfig config;
  config.object = object;
  config.start_value = distance;
  config.end_value = 0;
  config.duration_ms = duration_ms;
  config.user_data = user_data;
  config.completed_callback = completed_callback;
  return StartTransitionAnimation(config);
}

bool StartSlideRightWindowTransition(lv_obj_t* object, int32_t distance,
    uint32_t duration_ms, void* user_data,
    lv_anim_completed_cb_t completed_callback) {
  if (object == nullptr) {
    return false;
  }

  TransitionAnimationConfig config;
  config.object = object;
  config.start_value = lv_obj_get_x(object);
  config.end_value = distance;
  config.duration_ms = duration_ms;
  config.user_data = user_data;
  config.completed_callback = completed_callback;
  return StartTransitionAnimation(config);
}

void DeleteWindowTransition(lv_obj_t* object) {
  DeleteTransitionAnimation(object);
}

}  // namespace lilygo_box::ui
