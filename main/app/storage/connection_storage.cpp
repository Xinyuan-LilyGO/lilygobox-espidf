/*
 * @Description: 保存应用自动连接开关和最多五个按优先级排列的已授权应用，复用
 * NVS 与 TLV 存储协调器。
 * @Author: LILYGO_L
 * @Date: 2026-10-06 09:01:38
 * @LastEditTime: 2026-10-06 10:37:39
 * @License: GPL 3.0
 */
#include "app/storage/connection_storage.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "app/storage/storage_internal.h"
#include "app/storage/tlv_storage.h"
#include "base/logger.h"
#include "esp_err.h"
#include "mbedtls/sha256.h"
#include "nvs.h"

namespace lilygo_box::app {
namespace {

constexpr const char* kConnectionNvsNamespace = "settings";
constexpr const char* kConnectionNvsKey = "app_connections";
constexpr size_t kConnectionTlvCapacity = 1536;

// 已分配字段编号只允许保留，禁止改号或复用。
enum class ConnectionField : uint16_t {
  kEnabled = 1,
  kCredential = 2,
};

enum class AppCredentialField : uint16_t {
  kId = 1,
  kToken = 2,
  kClientName = 3,
};
constexpr size_t kAppCredentialTlvCapacity = 256;

/**
 * @brief 比较自动连接开关、授权密钥和客户端名称
 * @param left 左侧偏好
 * @param right 右侧偏好
 * @return 全部字段相同返回 true
 */
bool AreConnectionPreferencesEqual(
    const ConnectionPreferences& left, const ConnectionPreferences& right) {
  if (left.enabled != right.enabled || left.count != right.count) return false;
  for (size_t i = 0; i < left.count; ++i) {
    if (std::strcmp(left.clients[i].id, right.clients[i].id) != 0 ||
        std::strcmp(left.clients[i].token, right.clients[i].token) != 0 ||
        std::strcmp(
            left.clients[i].client_name, right.clients[i].client_name) != 0)
      return false;
  }
  return true;
}

/**
 * @brief 解码单条授权记录，验证标识与密钥一致性
 * @param data 内层 TLV 数据
 * @param size 内层 TLV 长度
 * @param client 解码输出
 * @return 完整有效返回 true
 */
bool DecodeAppCredential(
    const uint8_t* data, size_t size, AppCredential* client) {
  AppCredential decoded;
  storage::TlvReader reader(storage::TlvDomain::kAppCredential, data, size);
  storage::TlvField field;
  uint8_t present = 0;
  while (true) {
    const auto result = reader.Next(&field);
    if (result == storage::TlvReadResult::kInvalid) return false;
    if (result == storage::TlvReadResult::kEnd) {
      char id[65] = {};
      if (present != 7 || !MakeAppCredentialId(decoded.token, id) ||
          std::strcmp(decoded.id, id) != 0 || decoded.client_name[0] == '\0')
        return false;
      *client = decoded;
      return true;
    }
    switch (static_cast<AppCredentialField>(field.tag())) {
      case AppCredentialField::kId:
        if ((present & 1) || !field.CopyString(decoded.id, sizeof(decoded.id)))
          return false;
        present |= 1;
        break;
      case AppCredentialField::kToken:
        if ((present & 2) ||
            !field.CopyString(decoded.token, sizeof(decoded.token)))
          return false;
        present |= 2;
        break;
      case AppCredentialField::kClientName:
        if ((present & 4) ||
            !field.CopyString(decoded.client_name, sizeof(decoded.client_name)))
          return false;
        present |= 4;
        break;
      default:
        break;
    }
  }
}

/**
 * @brief 按 WLAN 保存列表的嵌套 TLV 方式读取应用，记录顺序即优先级
 * @param buffer 外层 TLV 数据
 * @param preferences 连接偏好输出
 * @return 配置完整有效返回 true
 */
bool DecodeConnectionPreferences(
    const storage::TlvBuffer& buffer, ConnectionPreferences* preferences) {
  if (preferences == nullptr) return false;
  ConnectionPreferences decoded;
  storage::TlvReader reader(
      storage::TlvDomain::kConnection, buffer.data.get(), buffer.size);
  storage::TlvField field;
  while (true) {
    const auto result = reader.Next(&field);
    if (result == storage::TlvReadResult::kInvalid) return false;
    if (result == storage::TlvReadResult::kEnd) {
      *preferences = decoded;
      return true;
    }
    switch (static_cast<ConnectionField>(field.tag())) {
      case ConnectionField::kEnabled:
        if (!field.ReadBool(&decoded.enabled)) return false;
        break;
      case ConnectionField::kCredential: {
        if (decoded.count >= kAppCredentialCapacity) return false;
        auto& client = decoded.clients[decoded.count];
        if (!DecodeAppCredential(field.data(), field.size(), &client))
          return false;
        for (size_t i = 0; i < decoded.count; ++i)
          if (std::strcmp(client.id, decoded.clients[i].id) == 0) return false;
        ++decoded.count;
        break;
      }
      default:
        break;
    }
  }
}

/**
 * @brief 将单个应用编码为独立 TLV 记录，字段编号在每条记录内复用
 * @param client 应用授权
 * @param output 编码缓冲区
 * @param capacity 缓冲区容量
 * @param size 实际长度输出
 * @return 编码成功返回 true
 */
bool EncodeAppCredential(const AppCredential& client, uint8_t* output,
    size_t capacity, size_t* size) {
  storage::TlvWriter writer(
      storage::TlvDomain::kAppCredential, output, capacity);
  return writer.WriteString(static_cast<uint16_t>(AppCredentialField::kId),
             client.id, sizeof(client.id)) &&
         writer.WriteString(static_cast<uint16_t>(AppCredentialField::kToken),
             client.token, sizeof(client.token)) &&
         writer.WriteString(
             static_cast<uint16_t>(AppCredentialField::kClientName),
             client.client_name, sizeof(client.client_name)) &&
         writer.Finalize(size);
}

/**
 * @brief 编码连接开关与有序应用列表，列表采用与 WLAN 相同的重复记录字段
 * @param preferences 待保存连接配置
 * @param output 编码缓冲区
 * @param capacity 缓冲区容量
 * @param encoded_size 实际长度输出
 * @return 编码成功返回 true
 */
bool EncodeConnectionPreferences(const ConnectionPreferences& preferences,
    uint8_t* output, size_t capacity, size_t* encoded_size) {
  storage::TlvWriter writer(storage::TlvDomain::kConnection, output, capacity);
  if (preferences.count > kAppCredentialCapacity ||
      !writer.WriteBool(static_cast<uint16_t>(ConnectionField::kEnabled),
          preferences.enabled))
    return false;
  std::array<uint8_t, kAppCredentialTlvCapacity> entry = {};
  for (size_t i = 0; i < preferences.count; ++i) {
    size_t size = 0;
    if (!EncodeAppCredential(
            preferences.clients[i], entry.data(), entry.size(), &size) ||
        !writer.WriteBytes(static_cast<uint16_t>(ConnectionField::kCredential),
            entry.data(), size))
      return false;
  }
  return writer.Finalize(encoded_size);
}

NvsStorageCache<ConnectionPreferences> g_connection_cache(
    StorageDomain::kConnection, AreConnectionPreferencesEqual);

}  // namespace

/**
 * @brief 校验授权密钥并计算公开 SHA-256 标识，不记录密钥
 * @param token 64 位小写十六进制随机密钥
 * @param output 标识输出缓冲区
 * @return 格式和哈希计算均成功返回 true
 */
bool MakeAppCredentialId(const char* token, char (&output)[65]) {
  if (token == nullptr || std::strlen(token) != 64) return false;
  for (size_t i = 0; i < 64; ++i)
    if (!((token[i] >= '0' && token[i] <= '9') ||
            (token[i] >= 'a' && token[i] <= 'f')))
      return false;
  unsigned char digest[32] = {};
  if (mbedtls_sha256(
          reinterpret_cast<const unsigned char*>(token), 64, digest, 0) != 0)
    return false;
  constexpr char hex[] = "0123456789abcdef";
  for (size_t i = 0; i < 32; ++i) {
    output[i * 2] = hex[digest[i] >> 4];
    output[i * 2 + 1] = hex[digest[i] & 15];
  }
  output[64] = '\0';
  return true;
}

/**
 * @brief 初始化应用连接偏好缓存，从独立 NVS 分区加载开关和授权
 */
void InitConnectionCache() {
  ConnectionPreferences loaded;
  nvs_handle_t handle = 0;
  if (OpenApplicationNvs(kConnectionNvsNamespace, NVS_READONLY, &handle) ==
      ESP_OK) {
    storage::TlvBuffer buffer;
    esp_err_t error = ESP_OK;
    const storage::TlvLoadResult result = storage::LoadTlvBuffer(handle,
        kConnectionNvsKey, storage::TlvDomain::kConnection,
        kConnectionTlvCapacity, &buffer, &error);
    if (result == storage::TlvLoadResult::kLoaded &&
        !DecodeConnectionPreferences(buffer, &loaded)) {
      LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
          "Connection TLV payload is invalid\n");
    } else if (result == storage::TlvLoadResult::kInvalid) {
      LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
          "Connection TLV container is invalid\n");
    } else if (result == storage::TlvLoadResult::kError) {
      LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
          "Load Connection TLV failed: %s\n", esp_err_to_name(error));
    }
    nvs_close(handle);
  }
  if (!g_connection_cache.Initialize(loaded)) {
    LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
        "Initialize Connection storage cache failed\n");
  }
}

/**
 * @brief 读取应用连接偏好，仅访问内存缓存
 * @return 当前自动连接开关和已授权应用凭据的快照
 */
ConnectionPreferences GetConnectionPreferences() {
  ConnectionPreferences preferences;
  g_connection_cache.Read(&preferences);
  return preferences;
}

/**
 * @brief 更新应用连接偏好，并通过统一存储协调器提交变更
 * @param preferences 新的连接开关和授权配置
 * @return 无变化或持久化成功返回 true，否则返回 false
 */
bool UpdateConnectionPreferences(const ConnectionPreferences& preferences) {
  if (preferences.count > kAppCredentialCapacity) return false;
  const auto previous = GetConnectionPreferences();
  if (g_connection_cache.UpdateAndPersist(preferences)) return true;
  // 协调器会保留失败写入的缓存；回滚，避免稍后把未获确认的授权写入 NVS。
  g_connection_cache.UpdateAndPersist(previous);
  return false;
}

/**
 * @brief 将连接偏好的固定快照暂存到当前 NVS 事务
 * @param handle 共享 NVS 句柄
 * @return 无变更、暂存成功或暂存失败
 */
StorageStageResult StageConnectionStorage(nvs_handle_t handle) {
  const ConnectionPreferences* preferences = nullptr;
  if (!g_connection_cache.BeginFlush(&preferences)) {
    return StorageStageResult::kClean;
  }
  std::array<uint8_t, kConnectionTlvCapacity> buffer = {};
  size_t encoded_size = 0;
  if (!EncodeConnectionPreferences(
          *preferences, buffer.data(), buffer.size(), &encoded_size) ||
      nvs_set_blob(handle, kConnectionNvsKey, buffer.data(), encoded_size) !=
          ESP_OK) {
    return StorageStageResult::kFailed;
  }
  return StorageStageResult::kStaged;
}

/**
 * @brief 根据统一事务结果推进连接配置快照或保留重试状态
 * @param committed 事务是否提交成功
 */
void FinishConnectionStorage(bool committed) {
  g_connection_cache.FinishFlush(committed);
}

}  // namespace lilygo_box::app
