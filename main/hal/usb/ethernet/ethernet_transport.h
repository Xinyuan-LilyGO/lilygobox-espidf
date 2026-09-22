/*
 * @Description: RTL8152B USB 传输筛选与 CDC 生命周期接口
 * @Author: LILYGO_L
 * @License: GPL 3.0
 */
#pragma once

#include "iot_usbh_cdc.h"
#include "iot_usbh_ecm.h"

namespace lilygo_box::hal::usb_ethernet::internal {

/**
 * @brief 安装按板载 Hub 分支筛选 RTL8152B 的 CDC 驱动
 * @param config CDC 驱动配置
 * @return 成功返回 ESP_OK，否则返回错误码
 */
esp_err_t InstallCdc(const usbh_cdc_driver_config_t* config);

/**
 * @brief 网络接口绑定并启动后，放行 CDC 设备通知并请求补扫已有设备
 * @param driver 已安装的 ECM 驱动，在 CancelDeviceEvents 返回前须保持有效
 * @return 唤醒 CDC 任务成功返回 ESP_OK，否则返回错误码
 */
esp_err_t EnableDeviceEvents(const iot_eth_driver_t* driver);

/**
 * @brief 解除初始化等待并丢弃新设备通知，避免 CDC 卸载时阻塞
 */
void CancelDeviceEvents();

/**
 * @brief 等待在途控制请求结束并关闭 ECM 使用的 CDC 端口
 * @param driver 已安装的 ECM 驱动
 * @return 端口关闭或已经不存在返回 ESP_OK，否则返回错误码
 * @note 调用前须取消设备通知，调用后才可释放 ECM 回调上下文
 */
esp_err_t CloseEcmPort(const iot_eth_driver_t* driver);

/**
 * @brief 在 CDC 完全卸载后清除设备句柄和回调引用
 */
void ResetTransport();

/**
 * @brief 配置软件 MAC 所需的混杂接收，须在安装 ECM 驱动前调用
 * @param enabled 是否启用混杂接收
 */
void EnableEcmPromiscuousMode(bool enabled);

}  // namespace lilygo_box::hal::usb_ethernet::internal
