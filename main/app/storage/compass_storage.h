/*
 * @Description: 指南针磁场校准参数持久化接口
 * @Author: LILYGO_L
 * @Date: 2026-09-29 00:00:00
 * @LastEditTime: 2026-09-29 00:00:00
 * @License: GPL 3.0
 */
#pragma once

namespace lilygo_box::app {

struct CompassCalibrationData {
  bool ready = false;
  float offset[3] = {};  // 三轴磁场偏置，单位 uT。
  float scale[3] = {1.0F, 1.0F, 1.0F};
};

/**
 * @brief 从应用 NVS 分区加载并验证指南针校准参数
 * @return 缓存初始化成功且存储读取正常返回 true，无已保存参数也返回 true
 */
bool InitCompassStorage();

/**
 * @brief 读取最后一次有效校准参数
 * @return 校准参数副本，没有可用数据时 ready 为 false
 */
CompassCalibrationData GetCompassCalibration();

/**
 * @brief 保存成功校准的参数，复用统一 NVS 事务及失败重试机制
 * @param calibration 通过校准质量检查的参数
 * @return 参数有效且持久化成功返回 true，否则返回 false
 */
bool SaveCompassCalibration(const CompassCalibrationData& calibration);

}  // namespace lilygo_box::app
