/*
 * @Description: Settings lock screen page
 * @Author: LILYGO_L
 * @Date: 2026-05-23 00:00:00
 * @LastEditTime: 2026-05-23 00:00:00
 * @License: GPL 3.0
 */
#include "ui/views/settings/settings_basic_view_common.h"

#include <cstdio>

namespace lilygo_box::ui {
namespace {

/**
 * @brief 构建锁屏设置内容
 * @param body 内容容器
 * @param state 设置页状态
 * @return 创建成功返回 true，否则返回 false
 */
bool BuildLockScreenContent(lv_obj_t* body, SettingsViewState* state) {
  const int width = state->config.width;
  char value[24] = {};
  std::snprintf(value, sizeof(value), "%d minutes",
      state->auto_lock_minutes);
  if (!CreateSectionLabel(body, "Lock screen settings", 0, width)) {
    return false;
  }
  return CreateArrowRow(body, "Auto lock", value, kBasicSectionHeight, width,
      nullptr, state);
}

}  // namespace

/**
 * @brief 从设置主页打开锁屏详情页
 * @param state 设置页状态
 * @return 打开成功返回 true，否则返回 false
 */
bool ShowLockScreenPage(SettingsViewState* state) {
  return ShowBasicPage(state, "Lock Screen", BuildLockScreenContent);
}

}  // namespace lilygo_box::ui
