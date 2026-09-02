/*
 * @Description: 设备 Provider 通用数据转换辅助接口
 * @Author: LILYGO_L
 * @Date: 2026-08-28 00:00:00
 * @LastEditTime: 2026-09-02 17:52:47
 * @License: GPL 3.0
 */
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "cpp_bus_driver_library.h"
#include "hal/providers/screen_provider.h"

namespace lilygo_box::hal::device_utils {

inline constexpr int kMinimumPercent = 0;
inline constexpr int kMaximumPercent = 100;
inline constexpr int64_t kValidUnixTimeThreshold = 1700000000LL;

/**
 * @brief 将触摸点设置为仅包含硬件边缘提示的无坐标样本
 * @param point 待设置的触摸点
 */
void SetHardwareEdgeTouchPoint(TouchPoint* point);

/**
 * @brief 将百分比限制到 0～100
 * @param percent 待限制的百分比
 * @return 限制后的百分比
 */
int ClampPercent(int percent);

/**
 * @brief 将用户亮度映射为经过感知校正的背光 PWM 占空比
 * @param clamped_percent 已限制到 0～100 的用户亮度
 * @param duty_scale PWM 占空比比例值
 * @return 0 表示关闭背光，非零亮度按平方曲线映射
 */
cpp_bus_driver::Pwm::DutyCycle ScreenBrightnessPercentToDutyCycle(
    int clamped_percent, uint32_t duty_scale);

/**
 * @brief 将百分比转换为指定上限的 8 位数值
 * @param percent 待转换的百分比
 * @param maximum_value 输出数值上限
 * @return 按比例计算的 8 位数值
 */
uint8_t PercentToUint8Value(int percent, uint8_t maximum_value);

/**
 * @brief 判断 GNSS 浮点字段是否已经被解析更新
 * @param value GNSS 浮点字段
 * @return 已更新返回 true，否则返回 false
 */
bool IsGnssFloatReady(float value);

/**
 * @brief 将字符串安全复制到固定长度 C 字符数组
 * @param destination 目标字符数组
 * @param destination_size 目标字符数组长度
 * @param source 源字符串
 */
void CopyString(
    char* destination, size_t destination_size, const std::string& source);

}  // namespace lilygo_box::hal::device_utils
