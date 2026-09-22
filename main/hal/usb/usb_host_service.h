/*
 * @Description: MSC 与 ECM 共用的 USB Host 生命周期接口
 * @Author: LILYGO_L
 * @License: GPL 3.0
 */
#pragma once

#include "esp_err.h"

namespace lilygo_box::hal::usb_host_service {

/**
 * @brief 申请共享 USB Host，首次申请时安装 Host 并启动事件任务
 * @return 申请成功返回 ESP_OK，否则返回错误码
 */
esp_err_t Acquire();

/**
 * @brief 释放共享 USB Host，最后一个使用者等待 Host 关闭
 * @return 释放成功返回 ESP_OK，关闭超时返回 ESP_ERR_TIMEOUT
 * @note 调用前须先卸载调用方自己的 USB 类驱动
 */
esp_err_t Release();

}  // namespace lilygo_box::hal::usb_host_service
