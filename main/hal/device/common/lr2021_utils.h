/*
 * @Description: LR2021 LoRa 配置与收发通用辅助接口
 * @Author: LILYGO_L
 * @Date: 2026-10-28 00:00:00
 * @LastEditTime: 2026-10-28 00:00:00
 * @License: GPL 3.0
 */
#pragma once

#include <cstddef>
#include <cstdint>

#include "usp_cpp_bus_driver.h"
#include "hal/providers/radio/radio_provider.h"

namespace lilygo_box::hal::lr2021_utils {

/**
 * @brief 将通用 LoRa 配置转换为 LR2021 驱动配置
 * @param source 通用 LoRa 配置
 * @param payload_size LoRa 数据负载长度
 * @param target 输出的 LR2021 LoRa 配置
 * @return 配置有效并成功转换时返回 true
 */
bool BuildConfig(const LoraRadioConfig& source, uint8_t payload_size,
    usp_cpp_bus_driver::Lr20xx::LoraConfig* target);

/**
 * @brief 清理 LR2021 接收状态并启动 LoRa 接收
 * @param radio LR2021 驱动实例
 * @param config 当前 LoRa 配置
 * @return 接收成功启动时返回 true
 */
bool StartReceive(usp_cpp_bus_driver::Lr20xx* radio,
    const LoraRadioConfig& config);

/**
 * @brief 根据 LR2021 LoRa 配置计算发送超时时间
 * @param config 当前 LoRa 配置
 * @param payload_size LoRa 数据负载长度
 * @return 发送超时时间，单位为微秒；配置无效时返回 0
 */
int64_t CalculateTransmitTimeoutUs(const LoraRadioConfig& config,
    size_t payload_size);

}
