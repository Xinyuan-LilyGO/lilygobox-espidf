/*
 * @Description: UI haptic feedback helpers
 * @Author: LILYGO_L
 * @Date: 2026-06-30 00:00:00
 * @LastEditTime: 2026-06-30 00:00:00
 * @License: GPL 3.0
 */
#pragma once

namespace lilygo_box::hal {
class HapticProvider;
}  // namespace lilygo_box::hal

namespace lilygo_box::ui {

/**
 * @brief 注册 UI 交互振动反馈提供者
 * @param haptic 振动反馈提供者指针
 */
void RegisterUiHapticProvider(hal::HapticProvider* haptic);

/**
 * @brief 设置 UI 交互振动反馈偏好
 * @param enabled true 表示启用振动反馈，false 表示关闭振动反馈
 * @param strength_percent 振动强度百分比，范围 0~100
 */
void SetUiHapticPreferences(bool enabled, int strength_percent);

/**
 * @brief 播放一次 UI 交互振动反馈
 */
void PlayUiHapticFeedback();

}  // namespace lilygo_box::ui
