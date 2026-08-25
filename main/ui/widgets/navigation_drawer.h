/*
 * @Description: 公共导航抽屉控件
 * @Author: LILYGO_L
 * @Date: 2026-07-11 00:00:00
 * @LastEditTime: 2026-07-11 00:00:00
 * @License: GPL 3.0
 */
#pragma once

#include <cstdint>

#include "lvgl.h"

namespace lilygo_box::ui {

constexpr int kNavigationDrawerItemHeight = 96;

// 侧边栏标题顶部位置，避开系统状态栏。
constexpr int kNavigationDrawerTitleTop = 70;

// 侧边栏首项顶部位置，与标题保持统一间距。
constexpr int kNavigationDrawerContentTop = 132;

// 导航抽屉尺寸、颜色、文本和字体配置。
struct NavigationDrawerConfig {
  int screen_width = 0;
  int screen_height = 0;
  int width_percent = 78;
  int animation_ms = 220;
  uint32_t background_color = 0xFFFBFE;
  uint32_t scrim_color = 0x000000;
  uint32_t primary_text_color = 0x1D1B20;
  uint32_t icon_color = 0x49454F;
  uint32_t pressed_color = 0xE7E0EC;
  uint32_t divider_color = 0xCAC4D0;
  const char* title = nullptr;
  const lv_font_t* title_font = nullptr;
  const lv_font_t* item_font = nullptr;
  const lv_font_t* icon_font = nullptr;
};

// 导航抽屉运行期间的对象状态。
struct NavigationDrawerState {
  lv_obj_t* overlay = nullptr;
  lv_obj_t* panel = nullptr;
  int panel_width = 0;
  NavigationDrawerConfig config = {};
};

/**
 * @brief 创建位于屏幕外的导航抽屉
 * @param parent 父对象
 * @param state 导航抽屉状态
 * @param config 导航抽屉配置
 * @return 创建成功返回抽屉内容面板，否则返回 nullptr
 */
lv_obj_t* OpenNavigationDrawer(lv_obj_t* parent,
    NavigationDrawerState* state, const NavigationDrawerConfig& config);

/**
 * @brief 完成抽屉布局并播放进入动画
 * @param state 导航抽屉状态
 * @return 动画启动成功返回 true，否则返回 false
 */
bool PresentNavigationDrawer(NavigationDrawerState* state);

/**
 * @brief 播放退出动画并关闭导航抽屉
 * @param state 导航抽屉状态
 */
void CloseNavigationDrawer(NavigationDrawerState* state);

/**
 * @brief 判断导航抽屉当前是否打开
 * @param state 导航抽屉状态
 * @return 已打开返回 true，否则返回 false
 */
bool IsNavigationDrawerOpen(const NavigationDrawerState* state);

/**
 * @brief 获取导航抽屉面板宽度
 * @param state 导航抽屉状态
 * @return 抽屉面板宽度
 */
int NavigationDrawerWidth(const NavigationDrawerState* state);

/**
 * @brief 在已打开的抽屉中创建标准操作行
 * @param state 导航抽屉状态
 * @param symbol 图标字符
 * @param text 标题文本
 * @param y 顶部坐标
 * @param callback 点击回调
 * @param callback_context 点击回调上下文
 * @return 创建成功返回操作行，否则返回 nullptr
 */
lv_obj_t* CreateNavigationDrawerItem(NavigationDrawerState* state,
    const char* symbol, const char* text, int y, lv_event_cb_t callback,
    void* callback_context);

/**
 * @brief 在已打开的抽屉中创建全宽分隔线
 * @param state 导航抽屉状态
 * @param y 顶部坐标
 * @return 创建成功返回分隔线，否则返回 nullptr
 */
lv_obj_t* CreateNavigationDrawerDivider(
    NavigationDrawerState* state, int y);

}  // namespace lilygo_box::ui
