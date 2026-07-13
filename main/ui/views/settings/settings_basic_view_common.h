/*
 * @Description: Settings basic page shared helpers
 * @Author: LILYGO_L
 * @Date: 2026-05-23 00:00:00
 * @LastEditTime: 2026-07-13 22:08:45
 * @License: GPL 3.0
 */
#pragma once

#include <cstdint>

#include "lvgl.h"
#include "ui/views/settings/settings_view_internal.h"

namespace lilygo_box::ui {

constexpr int kBasicTitleTop = 154;
constexpr int kBasicBodyTop = 238;
constexpr int kBasicSidePadding = 34;
constexpr int kBasicRowHeight = 86;
constexpr int kBasicSectionHeight = 54;
constexpr int kBasicSwitchWidth = 78;
constexpr int kBasicSwitchHeight = 44;
constexpr uint32_t kBasicBlueColor = theme::LightNeutralTheme().action;
constexpr uint32_t kBasicTextAreaColor =
    theme::LightNeutralTheme().surface_container_high;
constexpr uint32_t kBasicMutedColor =
    theme::LightNeutralTheme().on_surface_variant;

using SettingsContentBuilder = bool (*)(lv_obj_t*, SettingsViewState*);

/**
 * @brief 读取当前设备名称，未设置时返回默认名称
 * @return 设备名称文本
 */
const char* ReadBasicDeviceName();

/**
 * @brief 打开普通设置详情页
 * @param state 设置页状态
 * @param title 页面标题
 * @param builder 内容构建函数
 * @return 打开成功返回 true，否则返回 false
 */
bool ShowBasicPage(
    SettingsViewState* state, const char* title,
    SettingsContentBuilder builder);

/**
 * @brief 打开普通设置二级详情页
 * @param state 设置页状态
 * @param title 页面标题
 * @param builder 内容构建函数
 * @return 打开成功返回 true，否则返回 false
 */
bool ShowNestedPage(
    SettingsViewState* state, const char* title,
    SettingsContentBuilder builder);

/**
 * @brief 创建分组标题文本
 * @param parent 父对象
 * @param text 标题文本
 * @param y 顶部坐标
 * @param width 页面宽度
 * @return 创建成功返回 true，否则返回 false
 */
bool CreateSectionLabel(lv_obj_t* parent, const char* text, int y, int width);

/**
 * @brief 创建普通设置页分割线
 * @param parent 父对象
 * @param y 顶部坐标
 * @param width 页面宽度
 * @return 创建成功返回 true，否则返回 false
 */
bool CreateBasicDivider(lv_obj_t* parent, int y, int width);

/**
 * @brief 创建带右箭头的普通设置行
 * @param parent 父对象
 * @param title 标题文本
 * @param value 右侧文本
 * @param y 顶部坐标
 * @param width 页面宽度
 * @param callback 点击回调
 * @param state 设置页状态
 * @return 创建成功返回 true，否则返回 false
 */
bool CreateArrowRow(lv_obj_t* parent, const char* title, const char* value,
    int y, int width, lv_event_cb_t callback, SettingsViewState* state);

/**
 * @brief 创建普通设置开关行
 * @param parent 父对象
 * @param title 标题文本
 * @param y 顶部坐标
 * @param width 页面宽度
 * @param checked 当前是否选中
 * @param callback 开关变化回调
 * @param state 设置页状态
 * @return 创建成功返回 true，否则返回 false
 */
bool CreateSwitchRow(lv_obj_t* parent, const char* title, int y, int width,
    bool checked, lv_event_cb_t callback, SettingsViewState* state);

/**
 * @brief 创建带图标的滑动条设置行
 * @param parent 父对象
 * @param icon_text Material Symbols 图标文本
 * @param title 标题文本
 * @param value 当前百分比
 * @param y 顶部坐标
 * @param width 页面宽度
 * @param callback 滑动条变化回调
 * @param state 设置页状态
 * @return 创建成功返回 true，否则返回 false
 */
bool CreateSliderRow(lv_obj_t* parent, const char* icon_text,
    const char* title, int value, int y, int width, lv_event_cb_t callback,
    SettingsViewState* state);

/**
 * @brief 设置页文本输入框样式
 * @param text_area 文本输入框对象
 * @param font 输入文本字体
 * @param height 输入框高度
 */
void ApplySettingsTextAreaStyle(
    lv_obj_t* text_area, const lv_font_t* font, int height);

/**
 * @brief 从滑动条事件读取当前百分比
 * @param event LVGL 事件对象
 * @return 当前滑动条百分比
 */
int SliderPercentFromEvent(lv_event_t* event);

}  // namespace lilygo_box::ui
