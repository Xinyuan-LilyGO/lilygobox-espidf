/*
 * @Description: ST25R3916 NFC 发现与卡片状态公共辅助接口
 * @Author: LILYGO_L
 * @Date: 2026-08-21 00:00:00
 * @LastEditTime: 2026-08-21 00:00:00
 * @License: GPL 3.0
 */
#pragma once

#include <cstdint>

#include "hal/providers/nfc_provider.h"

extern "C" {
#include "rfal_nfc.h"
}

namespace lilygo_box::hal::st25r3916_nfc {

inline constexpr uint32_t kDiscoveryDurationMs = 500;
inline constexpr uint32_t kCardRemovalTimeoutMs = 800;
inline constexpr uint32_t kActiveDeviceHoldMs = 100;
inline constexpr uint32_t kDiscoveryRestartDelayMs = 50;
inline constexpr int kPlatformErrorBase = 1000;

/**
 * @brief 将 RFAL 卡片类型转换为应用层 NFC 技术
 * @param type RFAL 卡片类型
 * @return 应用层 NFC 技术
 */
NfcTechnology ToNfcTechnology(rfalNfcDevType type);

/**
 * @brief 提取已激活 NFC 标签的协议字段与 Type 2 NDEF 内容
 * @param device RFAL 已激活设备
 * @param status NFC 状态输出地址
 */
void PopulateNfcTagDetails(const rfalNfcDevice& device, NfcStatus* status);

/**
 * @brief 创建 NFC-A、B、F、V 与 ST25TB 轮询发现参数
 * @return RFAL 发现参数
 */
rfalNfcDiscoverParam CreateNfcDiscoveryParameters();

}  // namespace lilygo_box::hal::st25r3916_nfc
