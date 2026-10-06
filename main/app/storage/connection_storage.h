/*
 * @Description: 应用自动连接和授权凭据的持久化接口。
 * @Author: LILYGO_L
 * @Date: 2026-10-06 09:01:38
 * @LastEditTime: 2026-10-06 10:37:39
 * @License: GPL 3.0
 */
#pragma once

#include <cstddef>

namespace lilygo_box::app {

constexpr size_t kAppCredentialCapacity = 5;

struct AppCredential {
  char id[65] = {};
  char token[65] = {};
  char client_name[49] = {};
};

struct ConnectionPreferences {
  bool enabled = false;
  size_t count = 0;
  // 数组顺序即自动连接优先级，最前面的应用优先。
  AppCredential clients[kAppCredentialCapacity] = {};
};

/**
 * @brief 从随机授权密钥派生公开标识，避免重连时传输永久密钥
 * @param token 64 位小写十六进制随机密钥
 * @param output 65 字节标识输出缓冲区
 * @return 密钥格式合法且 SHA-256 计算成功返回 true
 */
bool MakeAppCredentialId(const char* token, char (&output)[65]);

/**
 * @brief 初始化应用连接偏好缓存，从独立 NVS 分区加载开关和授权
 */
void InitConnectionCache();

/**
 * @brief 读取应用连接偏好，仅访问内存缓存
 * @return 当前自动连接开关和已授权应用凭据的快照
 */
ConnectionPreferences GetConnectionPreferences();

/**
 * @brief 更新应用连接偏好，并通过统一存储协调器提交变更
 * @param preferences 新的连接开关和授权配置
 * @return 无变化或持久化成功返回 true，否则返回 false
 */
bool UpdateConnectionPreferences(const ConnectionPreferences& preferences);

}  // namespace lilygo_box::app
