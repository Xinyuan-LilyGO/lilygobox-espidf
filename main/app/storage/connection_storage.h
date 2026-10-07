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
  // 安装级公开标识，仅用于确认后的凭证更新，不代替密钥认证；旧记录可为空。
  char client_id[65] = {};
};

struct ConnectionPreferences {
  bool enabled = false;
  size_t count = 0;
  // 数组顺序即自动连接优先级，最前面的应用优先。
  AppCredential clients[kAppCredentialCapacity] = {};
};

/**
 * @brief 校验应用安装标识的固定长度小写十六进制格式
 * @param id 待校验的公开标识
 * @return 64 字符小写十六进制返回 true
 */
bool IsAppClientId(const char* id);

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
 * @brief 直接读取到调用方缓冲区，避免启动任务创建按值返回的凭证临时副本
 * @param preferences 已有配置缓冲区，读取失败时不修改
 * @return 缓存已初始化且读取成功返回 true
 */
bool ReadConnectionPreferences(ConnectionPreferences* preferences);

/**
 * @brief 更新应用连接偏好，并通过统一存储协调器提交变更
 * @param preferences 新的连接开关和授权配置
 * @return 无变化或持久化成功返回 true，否则返回 false
 */
bool UpdateConnectionPreferences(const ConnectionPreferences& preferences);

}  // namespace lilygo_box::app
