/*
 * @Description: 为应用连接提供每台设备独立且持久保存的 TLS 证书。
 * @Author: LILYGO_L
 * @Date: 2026-10-06 16:00:00
 * @LastEditTime: 2026-10-06 16:00:00
 * @License: GPL 3.0
 */
#pragma once

#include "esp_https_server.h"

namespace lilygo_box::app {

/**
 * @brief 加载或首次生成设备证书，并填充加密服务器配置
 * @param config 待填充的 HTTPS 配置
 * @return 身份加载并持久化成功返回 true，失败时禁止启动服务
 * @note 仅由连接任务调用；证书缓冲区在服务运行期间保持有效
 */
bool ConfigureConnectionTls(httpd_ssl_config_t* config);

}  // namespace lilygo_box::app
