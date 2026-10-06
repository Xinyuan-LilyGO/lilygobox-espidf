/*
 * @Description: 管理 LilygoBox 应用的局域网连接验证、设备确认及心跳状态。
 * @Author: LILYGO_L
 * @Date: 2026-10-04 19:00:00
 * @LastEditTime: 2026-10-06 13:38:15
 * @License: GPL 3.0
 */
#pragma once

#include <cstdint>

#include "app/storage/connection_storage.h"

namespace lilygo_box::hal {
class WifiProvider;
}

namespace lilygo_box::app {

struct AppConnectionStatus {
  bool network_ready = false;
  bool service_ready = false;
  bool discovering = false;
  bool pending = false;
  bool connected = false;
  bool auto_connect = false;
  bool paired = false;
  uint32_t request_id = 0;
  char device_id[32] = {};
  char address[16] = {};
  char client_name[49] = {};
};

struct AuthorizedApp {
  char id[65] = {};
  char name[49] = {};
  bool connected = false;
};

struct AuthorizedApps {
  size_t count = 0;
  AuthorizedApp clients[kAppCredentialCapacity] = {};
};

// 已授权应用凭随机令牌恢复只读连接；新客户端必须在设备上确认。
/**
 * @brief 启动局域网连接服务并恢复已保存的自动连接偏好
 * @param wifi 设备 Wi-Fi 状态接口
 * @return 服务任务创建成功返回 true
 */
bool InitializeAppConnection(hal::WifiProvider* wifi);

/**
 * @brief 在状态锁保护下读取连接状态，供 UI 线程刷新
 * @return 包含网络、配对和会话状态的快照
 */
AppConnectionStatus ReadAppConnectionStatus();

/**
 * @brief 保存自动连接偏好，网络未就绪时等待；关闭时断开会话但保留授权
 * @param enabled 是否允许发现和建立连接
 * @return 配置持久化成功返回 true，保存失败返回 false
 */
bool SetAppAutoConnect(bool enabled);

/**
 * @brief 读取按优先级排列的授权应用和实时连接标记，不向界面暴露密钥
 * @return 已授权应用快照
 */
AuthorizedApps ReadAuthorizedApps();

/**
 * @brief 将指定应用移动到目标优先级并持久保存，不打断当前连接
 * @param id 应用授权的公开标识
 * @param position 从零开始的目标位置
 * @return 排序保存成功返回 true
 */
bool MoveAppCredential(const char* id, size_t position);

/**
 * @brief 持久删除应用授权并断开其连接，保存失败则保留授权和连接
 * @param id 应用授权的公开标识
 * @return 删除保存成功返回 true
 */
bool RemoveAppCredential(const char* id);

/**
 * @brief 主动断开应用并保留授权，暂停该应用自动重连直至再次手动连接
 * @param id 授权公开标识
 * @return 已提交断开请求返回 true
 */
bool DisconnectApp(const char* id);

/**
 * @brief 处理首次连接确认，只接受仍有效的请求编号
 * @param request_id 待确认请求的唯一编号
 * @param allow 是否允许该应用连接并记住授权
 */
void ConfirmAppConnection(uint32_t request_id, bool allow);

}  // namespace lilygo_box::app
