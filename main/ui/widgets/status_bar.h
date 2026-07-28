/*
 * @Description: 系统状态栏创建、刷新与外观控制接口
 * @Author: LILYGO_L
 * @Date: 2026-05-12 01:08:42
 * @LastEditTime: 2026-07-08 12:08:51
 * @License: GPL 3.0
 */
#pragma once

#include "lvgl.h"

namespace lilygo_box::ui {

class StatusBar final {
 public:
  StatusBar() = default;

  /**
   * @brief 初始化全局状态栏
   * @param parent 父对象
   * @param width 状态栏宽度
   * @return 初始化成功返回 true，否则返回 false
   */
  bool Init(lv_obj_t* parent, int width);

  /**
   * @brief 设置状态栏时间文本
   * @param text 时间文本
   */
  void SetTimeText(const char* text);

  /**
   * @brief 设置状态栏电池电量和充电状态
   * @param percent 电池百分比
   * @param charging 是否正在充电
   */
  void SetBatteryStatus(int percent, bool charging);

  /**
   * @brief 设置状态栏 WiFi 连接、互联网访问和信号强度状态
   * @param connected 是否已经连接 WiFi
   * @param rssi WiFi 信号强度，单位为 dBm
   * @param internet_unavailable 已连接局域网但不能访问互联网
   */
  void SetWifiStatus(bool connected, int rssi, bool internet_unavailable);

  /**
   * @brief 获取状态栏对象
   * @return 状态栏对象指针
   */
  lv_obj_t* object() const { return object_; }

  /**
   * @brief 将状态栏移动到当前屏幕最上层
   */
  void MoveToTop();

  /**
   * @brief 设置状态栏文字和图标颜色
   * @param color 文字和图标颜色
   */
  void SetTextColor(lv_color_t color);

  /**
   * @brief 设置状态栏是否显示
   * @param visible true 显示，false 隐藏
   */
  void SetVisible(bool visible);

 private:
  lv_obj_t* object_ = nullptr;
  lv_obj_t* time_label_ = nullptr;
  lv_obj_t* wifi_label_ = nullptr;
  lv_obj_t* battery_management_label_ = nullptr;
  lv_obj_t* battery_management_fill_ = nullptr;
  lv_obj_t* battery_management_bolt_label_ = nullptr;
  lv_obj_t* battery_management_percent_label_ = nullptr;
  uint32_t text_color_hex_ = 0xFFFFFF;
  int battery_management_percent_ = -1;
  int wifi_signal_level_ = -1;
  char time_text_[6] = "09:15";
  char battery_management_percent_text_[8] = "--%";
  bool battery_management_charging_ = false;
  bool wifi_connected_ = false;
  bool wifi_internet_unavailable_ = false;
};

}  // namespace lilygo_box::ui
