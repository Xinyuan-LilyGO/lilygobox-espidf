/*
 * @Description: OTG reverse-power preference storage
 * @Author: LILYGO_L
 * @Date: 2026-08-18 00:00:00
 * @LastEditTime: 2026-08-18 00:00:00
 * @License: GPL 3.0
 */
#pragma once

namespace lilygo_box::app {

// OTG 反向供电用户偏好。
struct OtgPreferences {
  // 是否允许开启 OTG 反向供电。
  bool enabled = false;
};

/**
 * @brief 初始化 OTG 偏好缓存，从 NVS 加载到内存
 */
void InitOtgCache();

/**
 * @brief 读取 OTG 偏好（纯内存，零 NVS 访问）
 * @return OTG 偏好
 */
OtgPreferences GetOtgPreferences();

/**
 * @brief 比较并更新 OTG 偏好，存在变化时立即写入 NVS
 * @param preferences 新的 OTG 偏好
 * @return 无变化或 NVS 提交成功返回 true，否则返回 false
 */
bool UpdateOtgPreferences(const OtgPreferences& preferences);

}  // namespace lilygo_box::app
