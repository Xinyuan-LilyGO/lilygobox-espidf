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

// 页面切换动画作用的属性类型
enum class TransitionAnimationType {
  kX,
  kOpacity,
  kBackgroundOpacity,
};

// 页面切换动画配置
struct TransitionAnimationConfig {
  lv_obj_t* object = nullptr;
  TransitionAnimationType type = TransitionAnimationType::kX;
  int32_t start_value = 0;
  int32_t end_value = 0;
  uint32_t duration_ms = 0;
  lv_anim_path_cb_t path_callback = lv_anim_path_ease_out;
  void* user_data = nullptr;
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
 * @brief 设置对象整体透明度
 * @param object LVGL 对象
 * @param opacity 透明度
 */
void SetTransitionOpacity(void* object, int32_t opacity) {
  lv_obj_set_style_opa(
      static_cast<lv_obj_t*>(object), opacity, LV_PART_MAIN);
}

/**
 * @brief 设置对象背景透明度
 * @param object LVGL 对象
 * @param opacity 背景透明度
 */
void SetTransitionBackgroundOpacity(void* object, int32_t opacity) {
  lv_obj_set_style_bg_opa(
      static_cast<lv_obj_t*>(object), opacity, LV_PART_MAIN);
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

  lv_anim_exec_xcb_t exec_callback = nullptr;
  switch (config.type) {
    case TransitionAnimationType::kX:
      exec_callback = SetTransitionX;
      break;
    case TransitionAnimationType::kOpacity:
      exec_callback = SetTransitionOpacity;
      break;
    case TransitionAnimationType::kBackgroundOpacity:
      exec_callback = SetTransitionBackgroundOpacity;
      break;
  }
  if (exec_callback == nullptr) {
    return false;
  }

  lv_anim_delete(config.object, exec_callback);
  exec_callback(config.object, config.start_value);

  lv_anim_t animation;
  lv_anim_init(&animation);
  lv_anim_set_var(&animation, config.object);
  lv_anim_set_values(&animation, config.start_value, config.end_value);
  lv_anim_set_duration(&animation, config.duration_ms);
  if (config.path_callback != nullptr) {
    lv_anim_set_path_cb(&animation, config.path_callback);
  }
  lv_anim_set_exec_cb(&animation, exec_callback);
  lv_anim_set_user_data(&animation, config.user_data);
  if (config.completed_callback != nullptr) {
    lv_anim_set_completed_cb(&animation, config.completed_callback);
  }
  return lv_anim_start(&animation) != nullptr;
}

/**
 * @brief 删除指定对象上的页面切换动画
 * @param object LVGL 对象
 * @param type 动画类型
 */
void DeleteTransitionAnimation(
    lv_obj_t* object, TransitionAnimationType type) {
  if (object == nullptr) {
    return;
  }

  switch (type) {
    case TransitionAnimationType::kX:
      lv_anim_delete(object, SetTransitionX);
      break;
    case TransitionAnimationType::kOpacity:
      lv_anim_delete(object, SetTransitionOpacity);
      break;
    case TransitionAnimationType::kBackgroundOpacity:
      lv_anim_delete(object, SetTransitionBackgroundOpacity);
      break;
  }
}

}  // namespace

bool StartFadeWindowTransition(lv_obj_t* object, int32_t start_opacity,
    int32_t end_opacity, uint32_t duration_ms, void* user_data,
    lv_anim_completed_cb_t completed_callback) {
  TransitionAnimationConfig config;
  config.object = object;
  config.type = TransitionAnimationType::kOpacity;
  config.start_value = start_opacity;
  config.end_value = end_opacity;
  config.duration_ms = duration_ms;
  config.path_callback = nullptr;
  config.user_data = user_data;
  config.completed_callback = completed_callback;
  return StartTransitionAnimation(config);
}

bool StartSlideLeftWindowTransition(lv_obj_t* object, int32_t distance,
    uint32_t duration_ms, void* user_data,
    lv_anim_completed_cb_t completed_callback) {
  TransitionAnimationConfig config;
  config.object = object;
  config.type = TransitionAnimationType::kX;
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
  config.type = TransitionAnimationType::kX;
  config.start_value = lv_obj_get_x(object);
  config.end_value = distance;
  config.duration_ms = duration_ms;
  config.user_data = user_data;
  config.completed_callback = completed_callback;
  return StartTransitionAnimation(config);
}

bool StartBackgroundOpacityTransition(lv_obj_t* object, int32_t start_opacity,
    int32_t end_opacity, uint32_t duration_ms, void* user_data,
    lv_anim_completed_cb_t completed_callback) {
  TransitionAnimationConfig config;
  config.object = object;
  config.type = TransitionAnimationType::kBackgroundOpacity;
  config.start_value = start_opacity;
  config.end_value = end_opacity;
  config.duration_ms = duration_ms;
  config.user_data = user_data;
  config.completed_callback = completed_callback;
  return StartTransitionAnimation(config);
}

void DeleteWindowTransition(lv_obj_t* object, WindowTransitionMode mode) {
  switch (mode) {
    case WindowTransitionMode::kFade:
      DeleteTransitionAnimation(object, TransitionAnimationType::kOpacity);
      break;
    case WindowTransitionMode::kSlideLeft:
    case WindowTransitionMode::kSlideRight:
      DeleteTransitionAnimation(object, TransitionAnimationType::kX);
      break;
  }
}

void DeleteBackgroundOpacityTransition(lv_obj_t* object) {
  DeleteTransitionAnimation(object, TransitionAnimationType::kBackgroundOpacity);
}

}  // namespace lilygo_box::ui
