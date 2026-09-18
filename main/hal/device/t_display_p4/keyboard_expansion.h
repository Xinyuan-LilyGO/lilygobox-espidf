#pragma once

#include <cstdint>

#include "device/t_display_p4/driver.h"
#include "hal/providers/radio/radio_provider.h"

namespace lilygo_box::hal::keyboard_expansion {

/**
 * @brief 将公共 GFSK 配置转换为 CC1101 驱动配置
 * @param source 公共 GFSK 配置
 * @param target CC1101 驱动配置输出地址
 * @return 配置参数有效且转换成功时返回 true
 */
bool BuildCc1101Config(
    const GfskRadioConfig& source, cpp_bus_driver::Cc1101::Config* target);

/**
 * @brief 根据工作频率选择键盘扩展板上的 CC1101 射频通路
 * @param frequency_hz CC1101 中心频率，单位为 Hz
 * @param rf_switch 射频开关配置输出地址
 * @return 频率属于键盘扩展板支持频段时返回 true
 */
bool SelectCc1101RfSwitch(uint32_t frequency_hz,
    lilygo_device_driver::TDisplayP4Driver::Cc1101RfSwitch* rf_switch);

/**
 * @brief 将公共 Enhanced ShockBurst 配置转换为 nRF24L01 驱动配置
 * @param source 公共 Enhanced ShockBurst 配置
 * @param target nRF24L01 驱动配置输出地址
 * @return 配置参数有效且转换成功时返回 true
 */
bool BuildNrf24l01Config(const EnhancedShockBurstRadioConfig& source,
    cpp_bus_driver::Nrf24l01x::Config* target);

/**
 * @brief 按 nRF24L01 寄存器写入顺序编码空中地址
 * @param address 数值形式的空中地址
 * @param output 五字节地址输出地址
 */
void EncodeNrf24l01Address(uint64_t address, uint8_t* output);

}  // namespace lilygo_box::hal::keyboard_expansion
