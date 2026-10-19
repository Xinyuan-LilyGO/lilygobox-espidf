/*
 * @Description: 提供持续局域网发现、首次授权、可信重连和跨线程安全的连接状态。
 * @Author: LILYGO_L
 * @Date: 2026-10-04 19:00:00
 * @LastEditTime: 2026-10-13 12:00:00
 * @License: GPL 3.0
 */
#include "app/app_connection.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <mutex>

#include "app/connection_tls.h"
#include "app/device_control.h"
#include "app/device_identity.h"
#include "app/firmware_update_manager.h"
#include "app/storage/connection_storage.h"
#include "app/storage/display_storage.h"
#include "base/logger.h"
#include "cJSON.h"
#include "esp_app_desc.h"
#include "esp_http_server.h"
#include "esp_mac.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "hal/providers/wifi_provider.h"
#include "hal/providers/device_info_provider.h"
#include "lwip/inet.h"
#include "lwip/sockets.h"
#include "mbedtls/md.h"

namespace lilygo_box::app {
namespace {

constexpr int kDiscoveryPort = 18463;
constexpr int kWebSocketPort = 18464;
constexpr int64_t kConfirmMs = 60000;
constexpr int64_t kHeartbeatMs = 15000;
constexpr size_t kMaxMessageBytes = 1024;
char g_model_version[65] = {};

#if CONFIG_LILYGO_DEVICE_DRIVER_T_DISPLAY_P4_AIR
constexpr char kModel[] = "T-Display-P4-Air";
#else
constexpr char kModel[] = "T-Display-P4";
#endif

std::mutex g_mutex;
AppConnectionStatus g_status;
hal::WifiProvider* g_wifi = nullptr;
bool g_initialized = false;
ConnectionPreferences g_preferences;
constexpr size_t kSessionCapacity = kAppCredentialCapacity + 1;
constexpr int64_t kSelectionWindowMs = 4000;
struct AppSession {
  int fd = -1;
  uint32_t session_id = 0;
  DeviceControlCommand control;
  bool control_pending = false;
  bool control_executing = false;
  bool control_completed = false;
  char control_error[32] = {};
  int64_t control_until = 0;
  char credential_id[65] = {};
  char client_id[65] = {};
  char name[49] = {};
  char challenge[65] = {};
  bool authenticating = false;
  bool authenticated = false;
  bool new_pairing = false;
  bool close_requested = false;
  bool disconnect_requested = false;
  bool automatic = true;
  bool firmware_task_protocol = false;
  int64_t until = 0;
  int64_t last_seen = 0;
};
AppSession g_sessions[kSessionCapacity];
uint32_t g_next_session_id = 0;
uint32_t g_local_update_session_id = 0;
DeviceControlState g_control_state;
int g_active_fd = -1;
int g_pending_fd = -1;
int64_t g_selection_until = 0;
// 仅保存本次运行的主动断开状态，不删除凭证或改变已保存的自动连接设置。
char g_paused_apps[kAppCredentialCapacity][65] = {};

/**
 * @brief 查找主动断开的应用标识槽位
 * @param id 授权公开标识
 * @return 已暂停槽位，未找到返回 nullptr
 */
char* PausedApp(const char* id) {
  for (auto& paused : g_paused_apps)
    if (std::strcmp(paused, id) == 0) return paused;
  return nullptr;
}

/**
 * @brief 读取单调递增的系统时间
 * @return 启动以来的毫秒数
 */
int64_t NowMs() { return esp_timer_get_time() / 1000; }

/**
 * @brief 安全读取 JSON 字符串字段
 * @param object JSON 对象
 * @param key 字段名称
 * @return 字段文本，字段缺失或类型错误时返回空字符串
 */
const char* StringField(const cJSON* object, const char* key) {
  const cJSON* value = cJSON_GetObjectItemCaseSensitive(object, key);
  return cJSON_IsString(value) ? value->valuestring : "";
}

/**
 * @brief 检查连接消息是否使用协议版本 1
 * @param object 收到的 JSON 对象
 * @return 版本匹配返回 true
 */
bool IsVersionOne(const cJSON* object) {
  const cJSON* value = cJSON_GetObjectItemCaseSensitive(object, "v");
  return cJSON_IsNumber(value) && value->valuedouble == 1;
}

/**
 * @brief 生成 256 位随机值并编码为小写十六进制
 * @param output 65 字节输出缓冲区，包含终止符
 */
void RandomHex(char (&output)[65]) {
  uint8_t random[32];
  esp_fill_random(random, sizeof(random));
  for (size_t i = 0; i < sizeof(random); ++i) {
    std::snprintf(output + i * 2, 3, "%02x", random[i]);
  }
}

/**
 * @brief 使用保存的授权密钥计算当前会话的 HMAC-SHA256 证明
 * @param session 当前独立握手会话
 * @param token 对应应用的授权密钥
 * @param role 证明方向，server 或 client
 * @param output 证明字符串输出缓冲区
 * @return 计算成功返回 true
 */
bool MakeProof(const AppSession& session, const char* token, const char* role,
    char (&output)[65]) {
  char input[160] = {};
  std::snprintf(input, sizeof(input), "%s|%s|%s", role, g_status.device_id,
      session.challenge);
  unsigned char digest[32] = {};
  const auto* algorithm = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
  if (algorithm == nullptr ||
      mbedtls_md_hmac(algorithm, reinterpret_cast<const unsigned char*>(token),
          std::strlen(token), reinterpret_cast<const unsigned char*>(input),
          std::strlen(input), digest) != 0) {
    return false;
  }
  for (size_t i = 0; i < sizeof(digest); ++i) {
    std::snprintf(output + i * 2, 3, "%02x", digest[i]);
  }
  return true;
}

/**
 * @brief 比较长度固定的授权证明，避免逐字符提前返回
 * @param left 本地计算的 64 字符证明
 * @param right 收到的证明
 * @return 证明完全一致返回 true
 */
bool SameProof(const char* left, const char* right) {
  if (std::strlen(right) != 64) return false;
  unsigned char difference = 0;
  for (size_t i = 0; i < 64; ++i) difference |= left[i] ^ right[i];
  return difference == 0;
}

/**
 * @brief 在锁内按套接字查找独立会话
 * @param fd 套接字描述符
 * @return 会话地址，未找到时返回 nullptr
 */
AppSession* FindSession(int fd) {
  if (fd < 0) return nullptr;
  for (auto& session : g_sessions)
    if (session.fd == fd) return &session;
  return nullptr;
}

/**
 * @brief 按公开标识查找凭证优先级，禁止依赖客户端声明的名称
 * @param id 授权公开标识
 * @return 优先级索引，不存在时返回 count
 */
size_t CredentialIndex(const char* id) {
  for (size_t i = 0; i < g_preferences.count; ++i)
    if (std::strcmp(g_preferences.clients[i].id, id) == 0) return i;
  return g_preferences.count;
}

/**
 * @brief 按安装标识查找待更新的凭证，公开标识本身不授予连接权限
 * @param id 桌面应用的安装标识，旧客户端可为空
 * @return 已有槽位，未找到时返回 count
 */
size_t ClientCredentialIndex(const char* id) {
  if (!IsAppClientId(id)) return g_preferences.count;
  for (size_t i = 0; i < g_preferences.count; ++i)
    if (std::strcmp(g_preferences.clients[i].client_id, id) == 0) return i;
  return g_preferences.count;
}

/**
 * @brief 清理指定会话，保留授权和其他候选连接
 * @param session 待释放会话，调用方持有状态锁
 */
void ResetSession(AppSession& session) {
  if (session.fd < 0) return;
  if (session.fd == g_active_fd) {
    g_active_fd = -1;
    g_status.connected = false;
    if (g_pending_fd < 0) g_status.client_name[0] = '\0';
    g_selection_until = 0;
    LogMessage(LogLevel::kInfo, __FILE__, __LINE__, "App disconnected\n");
  }
  if (session.fd == g_pending_fd) {
    g_pending_fd = -1;
    g_status.pending = false;
  }
  session = {};
}

/**
 * @brief 在有限收集窗口后选择最高优先级的已认证应用，不抢占现有连接
 * @note 调用方持有状态锁；设备端统一仲裁，不能以网络到达顺序代替优先级
 */
void SelectCandidate() {
  if (g_active_fd >= 0 || g_pending_fd >= 0 || !g_preferences.enabled) return;
  AppSession* best = nullptr;
  size_t priority = g_preferences.count;
  for (auto& session : g_sessions) {
    if (session.fd < 0 || !session.authenticated || session.close_requested ||
        session.disconnect_requested ||
        PausedApp(session.credential_id) != nullptr ||
        NowMs() - session.last_seen > kHeartbeatMs)
      continue;
    const size_t index = CredentialIndex(session.credential_id);
    if (index < priority) {
      best = &session;
      priority = index;
    }
  }
  if (best == nullptr) {
    g_selection_until = 0;
    return;
  }
  if (g_selection_until == 0) g_selection_until = NowMs() + kSelectionWindowMs;
  if (NowMs() < g_selection_until) return;
  g_active_fd = best->fd;
  best->until = NowMs() + kHeartbeatMs;
  g_selection_until = 0;
  std::snprintf(
      g_status.client_name, sizeof(g_status.client_name), "%s", best->name);
  LogMessage(LogLevel::kInfo, __FILE__, __LINE__,
      "App selected by priority: %u\n", static_cast<unsigned>(priority + 1));
}

/**
 * @brief 创建包含协议版本和设备 ID 的响应对象
 * @param type 响应类型
 * @return 新的 JSON 对象，内存不足时返回 nullptr
 */
cJSON* Message(const char* type) {
  cJSON* message = cJSON_CreateObject();
  if (message != nullptr) {
    cJSON_AddNumberToObject(message, "v", 1);
    cJSON_AddStringToObject(message, "type", type);
    cJSON_AddStringToObject(message, "deviceId", g_status.device_id);
  }
  return message;
}

/**
 * @brief 将统一更新任务快照写入协议对象，远程任务不依赖应用会话存活
 * @param message 输出 JSON 对象
 */
void AddFirmwareTaskInfo(cJSON* message) {
  const auto snapshot = FirmwareUpdateManager::Instance().GetSnapshot();
  const char* stage = "idle";
  switch (snapshot.stage) {
    case FirmwareUpdateStage::kChecking: stage = "checking"; break;
    case FirmwareUpdateStage::kWaitingForNetwork: stage = "waitingForNetwork"; break;
    case FirmwareUpdateStage::kDownloadingWireless: stage = "downloadingWireless"; break;
    case FirmwareUpdateStage::kDownloadingMain: stage = "downloadingMain"; break;
    case FirmwareUpdateStage::kInstallingWireless: stage = "installingWireless"; break;
    case FirmwareUpdateStage::kReadyToInstall: stage = snapshot.busy ? "installing" : "readyToInstall"; break;
    case FirmwareUpdateStage::kRestarting: stage = "restarting"; break;
    case FirmwareUpdateStage::kPaused: stage = "paused"; break;
    case FirmwareUpdateStage::kFailed: stage = "failed"; break;
    case FirmwareUpdateStage::kUpdateAvailable: stage = "updateAvailable"; break;
    case FirmwareUpdateStage::kUpToDate: stage = "upToDate"; break;
    default: break;
  }
  if (snapshot.stage == FirmwareUpdateStage::kDownloadingMain ||
      snapshot.stage == FirmwareUpdateStage::kDownloadingWireless) {
    switch (snapshot.operation) {
      case FirmwareUpdateOperation::kVerifyingMain: stage = "verifyingMain"; break;
      case FirmwareUpdateOperation::kVerifyingWireless: stage = "verifyingWireless"; break;
      case FirmwareUpdateOperation::kPreparingInstall: stage = "preparingInstall"; break;
      default: break;
    }
  }
  if (snapshot.cancelled) stage = "cancelled";
  cJSON_AddStringToObject(message, "taskId", snapshot.task_id);
  cJSON_AddStringToObject(message, "source", snapshot.local_update ? "local" : "remote");
  cJSON_AddStringToObject(message, "stage", stage);
  cJSON_AddBoolToObject(message, "busy", snapshot.busy);
  cJSON_AddNumberToObject(message, "progress", snapshot.progress_percent);
  cJSON_AddNumberToObject(message, "receivedBytes", snapshot.received_bytes);
  cJSON_AddNumberToObject(message, "totalBytes", snapshot.total_bytes);
  cJSON_AddStringToObject(message, "message", snapshot.message);
  cJSON_AddBoolToObject(message, "mainCached", snapshot.main_cached);
  cJSON_AddBoolToObject(message, "wirelessCached", snapshot.wireless_cached);
  cJSON_AddStringToObject(message, "mainVersion", snapshot.main_target_version);
  cJSON_AddStringToObject(message, "wirelessVersion", snapshot.wireless_target_version);
}

/**
 * @brief 向响应写入设备信息、运行时间及更新任务快照
 * @param message 待补充的 JSON 对象
 */
void AddDeviceInfo(cJSON* message) {
  cJSON_AddNumberToObject(message, "firmwareTaskVersion", 1);
  cJSON* task = cJSON_AddObjectToObject(message, "firmwareTask");
  if (task != nullptr) AddFirmwareTaskInfo(task);
  cJSON_AddNumberToObject(message, "controlVersion", 1);
  char device_name[kMaxDeviceNameLength + 1] = {};
  CopyConfiguredDeviceName(device_name, sizeof(device_name));
  // 显示名称包含持久化用户名称或LilygoBox 与完整 MAC；型号字段保持独立。
  cJSON_AddStringToObject(message, "name",
                          device_name[0] == '\0' ? kModel : device_name);
  cJSON_AddStringToObject(message, "model", kModel);
  cJSON_AddStringToObject(message, "modelVersion", g_model_version);
  const FirmwareHardwareInfo hardware = FirmwareUpdateManager::ReadHardwareInfo();
  cJSON* chips = cJSON_AddObjectToObject(message, "chips");
  cJSON* main_chip = cJSON_AddObjectToObject(chips, "main");
  cJSON_AddStringToObject(main_chip, "model", hardware.main_model);
  cJSON_AddStringToObject(main_chip, "revision", hardware.main_revision);
  cJSON_AddStringToObject(main_chip, "revisionSource", "runtime");
  cJSON* wireless_chip = cJSON_AddObjectToObject(chips, "wireless");
  cJSON_AddStringToObject(wireless_chip, "model", hardware.wireless_model);
  cJSON_AddStringToObject(wireless_chip, "revision", hardware.wireless_revision);
  cJSON_AddStringToObject(wireless_chip, "revisionSource", "boardConfig");
  cJSON_AddStringToObject(message, "firmware",
                          esp_app_get_description()->version);
  char wireless_version[32] = {};
  if (FirmwareUpdateManager::ReadWirelessVersion(
          wireless_version, sizeof(wireless_version))) {
    cJSON_AddStringToObject(message, "wirelessFirmware", wireless_version);
  }
  cJSON_AddNumberToObject(message, "uptimeMs", static_cast<double>(NowMs()));
}

/**
 * @brief 发送 WebSocket 文本响应并释放 JSON 对象
 * @param request 当前 HTTPD 请求
 * @param message 待发送对象，调用后不再持有
 * @return ESP_OK 或发送错误码
 */
esp_err_t SendMessage(httpd_req_t* request, cJSON* message) {
  if (message == nullptr) return ESP_ERR_NO_MEM;
  char* data = cJSON_PrintUnformatted(message);
  cJSON_Delete(message);
  if (data == nullptr) return ESP_ERR_NO_MEM;
  httpd_ws_frame_t frame = {};
  frame.type = HTTPD_WS_TYPE_TEXT;
  frame.payload = reinterpret_cast<uint8_t*>(data);
  frame.len = std::strlen(data);
  const esp_err_t result = httpd_ws_send_frame(request, &frame);
  cJSON_free(data);
  return result;
}

/**
 * @brief 在 HTTP 服务线程发送主动断开通知，再关闭套接字
 * @param context HTTP 服务句柄，生命周期由连接任务负责
 */
void SendRequestedDisconnects(void* context) {
  auto server = static_cast<httpd_handle_t>(context);
  for (size_t i = 0; i < kSessionCapacity; ++i) {
    int fd;
    char payload[128] = {};
    {
      std::lock_guard<std::mutex> lock(g_mutex);
      auto& session = g_sessions[i];
      if (session.fd < 0 || !session.disconnect_requested ||
          session.close_requested)
        continue;
      fd = session.fd;
      session.disconnect_requested = false;
      session.close_requested = true;
      std::snprintf(payload, sizeof(payload),
          "{\"v\":1,\"type\":\"disconnected\",\"deviceId\":\"%s\"}",
          g_status.device_id);
    }
    httpd_ws_frame_t frame = {};
    frame.type = HTTPD_WS_TYPE_TEXT;
    frame.payload = reinterpret_cast<uint8_t*>(payload);
    frame.len = std::strlen(payload);
    httpd_ws_send_frame_async(server, fd, &frame);
    httpd_sess_trigger_close(server, fd);
  }
}

/**
 * @brief 将固件更新管理器的实时状态推送给当前授权应用
 * @param context HTTP 服务句柄
 */
void SendFirmwareUpdateStatus(void* context) {
  auto server = static_cast<httpd_handle_t>(context);
  int fd = -1;
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_active_fd >= 0 && g_status.connected) fd = g_active_fd;
  }
  if (fd < 0) return;
  const auto snapshot = FirmwareUpdateManager::Instance().GetSnapshot();
  bool task_protocol = false;
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    const auto* session = FindSession(fd);
    task_protocol = session != nullptr && session->firmware_task_protocol;
  }
  if (!snapshot.local_update && !task_protocol) return;
  const char* stage = "idle";
  switch (snapshot.stage) {
    case FirmwareUpdateStage::kDownloadingWireless: stage = "downloadingWireless"; break;
    case FirmwareUpdateStage::kInstallingWireless: stage = "installingWireless"; break;
    case FirmwareUpdateStage::kDownloadingMain: stage = "downloadingMain"; break;
    case FirmwareUpdateStage::kReadyToInstall: stage = "readyToInstall"; break;
    case FirmwareUpdateStage::kRestarting: stage = "restarting"; break;
    case FirmwareUpdateStage::kFailed: stage = "failed"; break;
    default: break;
  }
  if (snapshot.stage == FirmwareUpdateStage::kDownloadingMain ||
      snapshot.stage == FirmwareUpdateStage::kDownloadingWireless) {
    switch (snapshot.operation) {
      case FirmwareUpdateOperation::kVerifyingWireless: stage = "verifyingWireless"; break;
      case FirmwareUpdateOperation::kVerifyingMain: stage = "verifyingMain"; break;
      case FirmwareUpdateOperation::kPreparingInstall: stage = "preparingInstall"; break;
      case FirmwareUpdateOperation::kNone: break;
    }
  }
  if (snapshot.cancelled) stage = "cancelled";
  std::lock_guard<std::mutex> lock(g_mutex);
  if (fd != g_active_fd || !g_status.connected) return;
  cJSON* message = Message(task_protocol ? "firmware.task.status" : "firmware.local.status");
  if (message == nullptr) return;
  if (task_protocol) {
    AddFirmwareTaskInfo(message);
  } else {
    cJSON_AddStringToObject(message, "stage", stage);
    cJSON_AddNumberToObject(message, "progress", snapshot.progress_percent);
    cJSON_AddNumberToObject(message, "receivedBytes", snapshot.received_bytes);
    cJSON_AddNumberToObject(message, "totalBytes", snapshot.total_bytes);
    cJSON_AddBoolToObject(message, "busy", snapshot.busy);
    cJSON_AddStringToObject(message, "message", snapshot.message);
  }
  char* payload = cJSON_PrintUnformatted(message);
  cJSON_Delete(message);
  if (payload == nullptr) return;
  httpd_ws_frame_t frame = {};
  frame.type = HTTPD_WS_TYPE_TEXT;
  frame.payload = reinterpret_cast<uint8_t*>(payload);
  frame.len = std::strlen(payload);
  httpd_ws_send_frame_async(server, fd, &frame);
  cJSON_free(payload);
}

/**
 * @brief 处理 HTTPD 会话关闭并释放底层套接字
 * @param fd 已关闭会话对应的文件描述符
 */
void SocketClosed(httpd_handle_t, int fd) {
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (auto* session = FindSession(fd)) ResetSession(*session);
  }
  close(fd);
}

/**
 * @brief 将缓存状态附加到响应，不访问硬件或 LVGL
 * @param reply 目标响应对象
 */
void AddControlState(cJSON* reply) {
  const auto& source = g_control_state;
  cJSON* state = cJSON_AddObjectToObject(reply, "state");
  cJSON_AddBoolToObject(state, "ready", source.ready);
  cJSON* capabilities = cJSON_AddObjectToObject(state, "capabilities");
  cJSON_AddBoolToObject(capabilities, "identity", true);
  cJSON_AddStringToObject(state, "deviceName", source.device_name);
  cJSON_AddBoolToObject(capabilities, "screen", source.screen_supported);
  cJSON_AddBoolToObject(capabilities, "audio", source.audio_supported);
  cJSON_AddBoolToObject(capabilities, "haptic", source.haptic_supported);
  cJSON_AddBoolToObject(capabilities, "battery", source.battery_supported);
  cJSON_AddNumberToObject(state, "batteryCapacityMah",
                          source.battery_capacity_mah);
  cJSON_AddNumberToObject(state, "batteryCapacityMinimumMah",
                          source.battery_capacity_minimum_mah);
  cJSON_AddNumberToObject(state, "batteryCapacityMaximumMah",
                          source.battery_capacity_maximum_mah);
  cJSON_AddBoolToObject(state, "screenEnabled", source.screen_enabled);
  cJSON_AddBoolToObject(state, "soundEnabled", source.sound_enabled);
  cJSON_AddBoolToObject(state, "hapticEnabled", source.haptic_enabled);
  cJSON_AddNumberToObject(state, "brightness", source.brightness);
  cJSON_AddNumberToObject(state, "rotation", source.rotation);
  cJSON_AddNumberToObject(state, "lockTimeoutSeconds", source.lock_timeout_seconds);
  cJSON_AddNumberToObject(state, "volume", source.volume);
  cJSON_AddNumberToObject(state, "hapticStrength", source.haptic_strength);
  if (g_wifi != nullptr) {
    hal::WifiStatus wifi_status;
    if (g_wifi->ReadWifiStatus(&wifi_status) && wifi_status.got_ip) {
      cJSON_AddNumberToObject(state, "wifiRssi", wifi_status.rssi);
    }
  }
  const auto& source_battery = source.battery;
  cJSON* battery = cJSON_AddObjectToObject(state, "battery");
  cJSON_AddBoolToObject(battery, "available",
                        source_battery.ready && source_battery.pack_present);
  if (source_battery.ready && source_battery.pack_present) {
    cJSON_AddBoolToObject(battery, "charging", source_battery.charging);
    cJSON_AddBoolToObject(battery, "full", source_battery.full_charged);
    cJSON_AddNumberToObject(battery, "percent", source_battery.charge_percent);
    cJSON_AddNumberToObject(battery, "voltageMv", source_battery.voltage_mv);
    cJSON_AddNumberToObject(battery, "currentMa", source_battery.current_ma);
    cJSON_AddNumberToObject(battery, "temperatureC",
                            source_battery.pack_temperature_c);
    // 不支持或无效的估算不发送数值，客户端明确显示未知。
    if (source_battery.health_percent > 0 &&
        source_battery.health_percent <= 100) {
      cJSON_AddNumberToObject(battery, "healthPercent",
                              source_battery.health_percent);
    }
    if (source_battery.capabilities.remaining_time) {
      if (source_battery.charging && !source_battery.full_charged &&
          source_battery.time_to_full_min > 0 &&
          source_battery.time_to_full_min < 43200) {
        cJSON_AddNumberToObject(battery, "timeToFullMinutes",
                                source_battery.time_to_full_min);
      }
      if (!source_battery.charging && source_battery.time_to_empty_min > 0 &&
          source_battery.time_to_empty_min < 43200) {
        cJSON_AddNumberToObject(battery, "timeToEmptyMinutes",
                                source_battery.time_to_empty_min);
      }
    }
  }
}

/**
 * @brief 严格校验单字段目标值，布尔开关与数值字段不能互相替代
 * @param input 客户端消息
 * @param command 已填写会话标识的命令输出
 * @return 合法且当前设备支持时返回 true
 */
bool ParseControlCommand(const cJSON* input, DeviceControlCommand* command) {
  const char* field = StringField(input, "field");
  const cJSON* value = cJSON_GetObjectItemCaseSensitive(input, "value");
  bool boolean = false;
  bool supported = false;
  int minimum = 0;
  int maximum = 100;
  if (std::strcmp(field, "deviceName") == 0) {
    if (!g_control_state.ready || !cJSON_IsString(value)) return false;
    const char* name = value->valuestring;
    const size_t length = std::strlen(name);
    if (length == 0 || length > kMaxDeviceNameLength) return false;
    bool has_content = false;
    for (size_t i = 0; i < length; ++i) {
      const auto c = static_cast<unsigned char>(name[i]);
      if (c < 0x20 || c == 0x7f) return false;
      if (c != ' ') has_content = true;
    }
    if (!has_content) return false;
    command->field = DeviceControlField::kDeviceName;
    std::snprintf(command->text, sizeof(command->text), "%s", name);
    return true;
  } else if (std::strcmp(field, "screenEnabled") == 0) {
    command->field = DeviceControlField::kScreenEnabled;
    boolean = true;
    supported = g_control_state.screen_supported;
  } else if (std::strcmp(field, "brightness") == 0) {
    command->field = DeviceControlField::kBrightness;
    minimum = 10;
    supported = g_control_state.screen_supported;
  } else if (std::strcmp(field, "rotation") == 0) {
    command->field = DeviceControlField::kRotation;
    maximum = 270;
    supported = g_control_state.screen_supported;
  } else if (std::strcmp(field, "lockTimeoutSeconds") == 0) {
    command->field = DeviceControlField::kLockTimeoutSeconds;
    maximum = 600;
    supported = g_control_state.screen_supported;
  } else if (std::strcmp(field, "soundEnabled") == 0) {
    command->field = DeviceControlField::kSoundEnabled;
    boolean = true;
    supported = g_control_state.audio_supported;
  } else if (std::strcmp(field, "volume") == 0) {
    command->field = DeviceControlField::kVolume;
    supported = g_control_state.audio_supported;
  } else if (std::strcmp(field, "hapticEnabled") == 0) {
    command->field = DeviceControlField::kHapticEnabled;
    boolean = true;
    supported = g_control_state.haptic_supported;
  } else if (std::strcmp(field, "hapticStrength") == 0) {
    command->field = DeviceControlField::kHapticStrength;
    supported = g_control_state.haptic_supported;
  } else if (std::strcmp(field, "batteryCapacityMah") == 0) {
    command->field = DeviceControlField::kBatteryCapacityMah;
    minimum = g_control_state.battery_capacity_minimum_mah;
    maximum = g_control_state.battery_capacity_maximum_mah;
    supported = g_control_state.battery_supported;
  } else {
    return false;
  }
  if (!g_control_state.ready || !supported) return false;
  if (boolean) {
    if (!cJSON_IsBool(value)) return false;
    command->value = cJSON_IsTrue(value) ? 1 : 0;
  } else {
    if (!cJSON_IsNumber(value) || value->valuedouble < minimum ||
        value->valuedouble > maximum || value->valuedouble != value->valueint) {
      return false;
    }
    command->value = value->valueint;
    if (command->field == DeviceControlField::kLockTimeoutSeconds &&
        !IsDisplayLockTimeoutSupported(command->value)) return false;
    if (command->field == DeviceControlField::kRotation &&
        command->value % 90 != 0)
      return false;
  }
  return true;
}

/**
 * @brief 接收授权与控制消息；硬件控制仅入队，结果由客户端轮询领取
 * @param request WebSocket 请求
 * @return 消息处理结果
 */
esp_err_t WebSocketHandler(httpd_req_t* request) {
  const int fd = httpd_req_to_sockfd(request);
  if (request->method == HTTP_GET) {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (!g_preferences.enabled) return ESP_FAIL;
    for (auto& session : g_sessions) {
      if (session.fd >= 0) continue;
      session = {};
      session.fd = fd;
      session.session_id = ++g_next_session_id;
      session.last_seen = NowMs();
      session.until = NowMs() + 10000;
      return ESP_OK;
    }
    return ESP_FAIL;
  }
  httpd_ws_frame_t frame = {};
  if (httpd_ws_recv_frame(request, &frame, 0) != ESP_OK ||
      frame.type != HTTPD_WS_TYPE_TEXT || frame.len == 0 ||
      frame.len > kMaxMessageBytes || !frame.final)
    return ESP_FAIL;
  char buffer[kMaxMessageBytes + 1] = {};
  frame.payload = reinterpret_cast<uint8_t*>(buffer);
  if (httpd_ws_recv_frame(request, &frame, frame.len) != ESP_OK)
    return ESP_FAIL;
  cJSON* input = cJSON_ParseWithLength(buffer, frame.len);
  if (!cJSON_IsObject(input) || !IsVersionOne(input)) {
    cJSON_Delete(input);
    return ESP_FAIL;
  }
  if (std::strcmp(StringField(input, "type"), "hello") == 0) {
    const auto* pairing_version =
        cJSON_GetObjectItemCaseSensitive(input, "pairingVersion");
    if (!cJSON_IsNumber(pairing_version) || pairing_version->valuedouble != 2) {
      // 旧客户端没有多授权标识，不能误报为凭证被删除或重复创建授权。
      cJSON_Delete(input);
      LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
          "App pairing protocol is outdated; update the desktop app\n");
      SendMessage(request, Message("upgrade_required"));
      return ESP_FAIL;
    }
  }
  cJSON* reply = nullptr;
  bool ready_reply = false;
  bool rejected = false;
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    AppSession* session = FindSession(fd);
    if (session == nullptr || session->close_requested ||
        !g_preferences.enabled) {
      cJSON_Delete(input);
      return ESP_FAIL;
    }
    const char* type = StringField(input, "type");
    if (std::strcmp(type, "firmware.local.start") == 0 &&
        fd == g_active_fd && g_status.connected) {
      const char* main_url = StringField(input, "mainUrl");
      const char* wireless_url = StringField(input, "wirelessUrl");
      const char* main_version = StringField(input, "mainVersion");
      const char* wireless_version = StringField(input, "wirelessVersion");
      const char* main_sha256 = StringField(input, "mainSha256");
      const char* wireless_sha256 = StringField(input, "wirelessSha256");
      const cJSON* main_size = cJSON_GetObjectItemCaseSensitive(input, "mainSize");
      const cJSON* wireless_size = cJSON_GetObjectItemCaseSensitive(input, "wirelessSize");
      const cJSON* release_notes = cJSON_GetObjectItemCaseSensitive(input, "releaseNotes");
      const char* notes[kFirmwareUpdateNoteCapacity] = {};
      const int note_count = release_notes == nullptr ? 0 : cJSON_GetArraySize(release_notes);
      bool notes_valid = release_notes == nullptr ||
          (cJSON_IsArray(release_notes) && note_count <= static_cast<int>(kFirmwareUpdateNoteCapacity));
      if (notes_valid) {
        for (int index = 0; index < note_count; ++index) {
          const cJSON* note = cJSON_GetArrayItem(release_notes, index);
          if (!cJSON_IsString(note) || note->valuestring == nullptr) {
            notes_valid = false;
            break;
          }
          notes[index] = note->valuestring;
        }
      }
      const bool accepted = notes_valid && g_local_update_session_id == 0 &&
          cJSON_IsNumber(main_size) && cJSON_IsNumber(wireless_size) &&
          main_size->valueint > 0 && wireless_size->valueint > 0 &&
          FirmwareUpdateManager::Instance().StartLocalUpdate(
              main_url, wireless_url, main_version, wireless_version,
              static_cast<size_t>(main_size->valueint), static_cast<size_t>(wireless_size->valueint),
              main_sha256, wireless_sha256,
              !cJSON_IsFalse(cJSON_GetObjectItemCaseSensitive(input, "reuseCache")),
              notes, static_cast<size_t>(note_count));
      if (accepted) g_local_update_session_id = session->session_id;
      reply = Message(accepted ? "firmware.local.started"
                               : "firmware.local.rejected");
      cJSON_AddStringToObject(reply, "requestId", StringField(input, "requestId"));
      if (accepted) {
        const auto snapshot = FirmwareUpdateManager::Instance().GetSnapshot();
        cJSON_AddStringToObject(reply, "taskId", snapshot.task_id);
      }
    } else if (std::strcmp(type, "firmware.task.get") == 0 &&
               fd == g_active_fd && g_status.connected && session->firmware_task_protocol) {
      reply = Message("firmware.task.status");
      AddFirmwareTaskInfo(reply);
    } else if ((std::strcmp(type, "firmware.task.cancel") == 0 ||
                   std::strcmp(type, "firmware.task.install") == 0) &&
               fd == g_active_fd && g_status.connected && session->firmware_task_protocol) {
      const auto snapshot = FirmwareUpdateManager::Instance().GetSnapshot();
      const char* task_id = StringField(input, "taskId");
      const bool cancel = std::strcmp(type, "firmware.task.cancel") == 0;
      const bool matches = task_id[0] != '\0' &&
          std::strcmp(task_id, snapshot.task_id) == 0 &&
          (!snapshot.local_update || session->session_id == g_local_update_session_id);
      const bool accepted = matches && (cancel
          ? FirmwareUpdateManager::Instance().Cancel(task_id)
          : FirmwareUpdateManager::Instance().InstallAndRestart(task_id));
      reply = Message(cancel ? "firmware.task.cancel.result" : "firmware.task.install.result");
      cJSON_AddStringToObject(reply, "taskId", task_id);
      cJSON_AddBoolToObject(reply, "ok", accepted);
    } else if (std::strcmp(type, "firmware.local.cancel") == 0 &&
               fd == g_active_fd && g_status.connected) {
      const bool accepted = session->session_id == g_local_update_session_id &&
          FirmwareUpdateManager::Instance().CancelLocalUpdate();
      reply = Message(accepted ? "firmware.local.cancelled"
                               : "firmware.local.cancel.rejected");
    } else if (std::strcmp(type, "firmware.local.install") == 0 &&
               fd == g_active_fd && g_status.connected) {
      const bool accepted = session->session_id == g_local_update_session_id &&
          FirmwareUpdateManager::Instance().InstallAndRestart();
      reply = Message(accepted ? "firmware.local.install.started"
                               : "firmware.local.install.rejected");
    } else if (session->disconnect_requested) {
      session->disconnect_requested = false;
      reply = Message("disconnected");
      rejected = true;
    } else if (std::strcmp(type, "hello") == 0 && session->name[0] == '\0') {
      const cJSON* task_version = cJSON_GetObjectItemCaseSensitive(input, "firmwareTaskVersion");
      session->firmware_task_protocol = cJSON_IsNumber(task_version) && task_version->valueint == 1;
      const char* name = StringField(input, "clientName");
      const char* client_id = StringField(input, "clientId");
      if (std::strlen(name) == 0 || std::strlen(name) > 48 ||
          (cJSON_HasObjectItem(input, "clientId") && !IsAppClientId(client_id)) ||
          std::strcmp(StringField(input, "deviceId"), g_status.device_id) !=
              0) {
        cJSON_Delete(input);
        return ESP_FAIL;
      }
      std::snprintf(session->name, sizeof(session->name), "%s", name);
      std::snprintf(session->client_id, sizeof(session->client_id), "%s", client_id);
      for (char& c : session->name)
        if (c != '\0' && static_cast<unsigned char>(c) < 32) c = ' ';
      session->automatic =
          !cJSON_IsFalse(cJSON_GetObjectItemCaseSensitive(input, "automatic"));
      const bool resume =
          cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(input, "resume"));
      const char* id = StringField(input, "pairingId");
      const size_t index = CredentialIndex(id);
      if (resume && std::strlen(id) != 64) {
        reply = Message("upgrade_required");
        rejected = true;
      } else if (resume && index == g_preferences.count) {
        reply = Message("authorization_revoked");
        rejected = true;
      } else if (g_active_fd >= 0 || (!resume && g_pending_fd >= 0)) {
        reply = Message("busy");
        rejected = true;
      } else if (resume) {
        bool duplicate = false;
        for (const auto& other : g_sessions)
          if (other.fd >= 0 && other.fd != fd &&
              std::strcmp(other.credential_id, id) == 0)
            duplicate = true;
        if (duplicate) {
          reply = Message("busy");
          rejected = true;
        } else {
          std::snprintf(
              session->credential_id, sizeof(session->credential_id), "%s", id);
          // 名称使用已授权记录，重连不能静默改名。
          std::snprintf(session->name, sizeof(session->name), "%s",
              g_preferences.clients[index].client_name);
          RandomHex(session->challenge);
          char proof[65] = {};
          if (!MakeProof(*session, g_preferences.clients[index].token, "server",
                  proof)) {
            cJSON_Delete(input);
            return ESP_FAIL;
          }
          session->authenticating = true;
          reply = Message("challenge");
          cJSON_AddStringToObject(reply, "challenge", session->challenge);
          cJSON_AddStringToObject(reply, "proof", proof);
        }
      } else if (g_preferences.count >= kAppCredentialCapacity &&
                 ClientCredentialIndex(session->client_id) == g_preferences.count) {
        reply = Message("pairing_full");
        rejected = true;
      } else {
        g_pending_fd = fd;
        ++g_status.request_id;
        g_status.pending = true;
        std::snprintf(g_status.client_name, sizeof(g_status.client_name), "%s",
            session->name);
        session->until = NowMs() + kConfirmMs;
        reply = Message("waiting");
        LogMessage(LogLevel::kInfo, __FILE__, __LINE__,
            "New app requests approval: %s\n", session->name);
      }
    } else if (std::strcmp(type, "authenticate") == 0 &&
               session->authenticating) {
      const size_t index = CredentialIndex(session->credential_id);
      char proof[65] = {};
      session->authenticating = false;
      if (index == g_preferences.count ||
          !MakeProof(
              *session, g_preferences.clients[index].token, "client", proof) ||
          !SameProof(proof, StringField(input, "proof"))) {
        reply = Message("authorization_revoked");
        rejected = true;
      } else if (session->automatic &&
                 PausedApp(session->credential_id) != nullptr) {
        reply = Message("disconnected");
        rejected = true;
      } else {
        // 旧授权只有在密钥认证成功后才补记安装标识，不能凭名称或 hello 直接绑定。
        if (g_preferences.clients[index].client_id[0] == '\0' &&
            IsAppClientId(session->client_id) &&
            ClientCredentialIndex(session->client_id) == g_preferences.count) {
          auto updated = g_preferences;
          std::snprintf(updated.clients[index].client_id,
              sizeof(updated.clients[index].client_id), "%s", session->client_id);
          if (UpdateConnectionPreferences(updated)) {
            g_preferences = updated;
          } else {
            LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
                "Save app installation identity failed\n");
          }
        }
        if (auto* paused = PausedApp(session->credential_id)) paused[0] = '\0';
        session->authenticated = true;
        session->until = NowMs() + kConfirmMs;
        reply = Message("queued");
        SelectCandidate();
      }
    } else if (std::strcmp(type, "poll") == 0 &&
               (fd == g_pending_fd || session->authenticated)) {
      SelectCandidate();
      if (fd == g_active_fd) {
        reply = Message("ready");
        AddDeviceInfo(reply);
        AddControlState(reply);
        const size_t index = CredentialIndex(session->credential_id);
        if (index == g_preferences.count) {
          cJSON_Delete(input);
          cJSON_Delete(reply);
          return ESP_FAIL;
        }
        if (session->new_pairing)
          cJSON_AddStringToObject(
              reply, "pairingToken", g_preferences.clients[index].token);
        ready_reply = true;
      } else if (g_active_fd >= 0) {
        reply = Message("busy");
        rejected = true;
      } else {
        reply = Message(fd == g_pending_fd ? "waiting" : "queued");
      }
    } else if (std::strcmp(type, "ping") == 0 && fd == g_active_fd &&
               g_status.connected) {
      reply = Message("pong");
      AddDeviceInfo(reply);
      AddControlState(reply);
    } else if (std::strcmp(type, "state.get") == 0 && fd == g_active_fd &&
               g_status.connected) {
      reply = Message("state");
      AddDeviceInfo(reply);
      AddControlState(reply);
    } else if ((std::strcmp(type, "control.set") == 0 ||
                std::strcmp(type, "control.poll") == 0) &&
               fd == g_active_fd && g_status.connected) {
      const cJSON* id = cJSON_GetObjectItemCaseSensitive(input, "requestId");
      if (!cJSON_IsNumber(id) || id->valuedouble < 1 ||
          id->valuedouble > INT32_MAX || id->valuedouble != id->valueint) {
        cJSON_Delete(input);
        return ESP_FAIL;
      }
      const char* error = nullptr;
      if (std::strcmp(type, "control.set") == 0 &&
          id->valueint != session->control.request_id) {
        DeviceControlCommand command;
        command.session_id = session->session_id;
        command.request_id = id->valueint;
        if (session->control_pending || session->control_executing) {
          error = "busy";
        } else if (id->valueint <= session->control.request_id) {
          error = "stale_request";
        } else if (!ParseControlCommand(input, &command)) {
          error = "invalid_or_unsupported";
        } else {
          session->control = command;
          session->control_pending = true;
          session->control_completed = false;
          session->control_until = NowMs() + 5000;
          session->control_error[0] = '\0';
        }
      } else if (id->valueint != session->control.request_id) {
        error = "unknown_request";
      }
      const bool completed = error != nullptr || session->control_completed;
      reply = Message(completed ? "control.result" : "control.queued");
      cJSON_AddNumberToObject(reply, "requestId", id->valueint);
      if (completed) {
        if (error == nullptr) error = session->control_error;
        cJSON_AddBoolToObject(reply, "ok", error[0] == '\0');
        cJSON_AddStringToObject(reply, "error", error);
        AddControlState(reply);
      }
    } else {
      cJSON_Delete(input);
      return ESP_FAIL;
    }
    session->last_seen = NowMs();
    if (rejected) session->close_requested = true;
  }
  cJSON_Delete(input);
  const esp_err_t result = SendMessage(request, reply);
  if (result == ESP_OK && ready_reply) {
    std::lock_guard<std::mutex> lock(g_mutex);
    auto* session = FindSession(fd);
    if (session != nullptr && fd == g_active_fd && !session->close_requested) {
      if (!g_status.connected)
        LogMessage(LogLevel::kInfo, __FILE__, __LINE__,
            "Connected: client=%s\n", session->name);
      g_status.connected = true;
      session->new_pairing = false;
    }
  }
  return rejected ? ESP_FAIL : result;
}

/**
 * @brief 自动连接开启且空闲时响应合法的 UDP 发现请求
 * @param socket_fd 发现服务的 UDP 套接字
 */
void ReplyToDiscovery(int socket_fd) {
  char buffer[513] = {};
  sockaddr_in sender = {};
  socklen_t sender_size = sizeof(sender);
  const int size = recvfrom(socket_fd, buffer, sizeof(buffer) - 1, 0,
      reinterpret_cast<sockaddr*>(&sender), &sender_size);
  if (size <= 0) return;
  cJSON* input = cJSON_ParseWithLength(buffer, size);
  if (!cJSON_IsObject(input) || !IsVersionOne(input) ||
      std::strcmp(StringField(input, "type"), "lilygobox.discover") != 0) {
    cJSON_Delete(input);
    return;
  }
  const char* nonce = StringField(input, "nonce");
  if (std::strlen(nonce) < 8 || std::strlen(nonce) > 64) {
    cJSON_Delete(input);
    return;
  }
  cJSON* reply = nullptr;
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_preferences.enabled) {
      reply = Message("lilygobox.device");
      AddDeviceInfo(reply);
      cJSON_AddStringToObject(reply, "nonce", nonce);
      cJSON_AddNumberToObject(reply, "port", kWebSocketPort);
      cJSON_AddStringToObject(reply, "transport", "wss");
      cJSON_AddBoolToObject(reply, "paired", g_preferences.count != 0);
    }
  }
  cJSON_Delete(input);
  if (reply == nullptr) return;
  char* encoded = cJSON_PrintUnformatted(reply);
  cJSON_Delete(reply);
  if (encoded != nullptr) {
    sendto(socket_fd, encoded, std::strlen(encoded), 0,
        reinterpret_cast<sockaddr*>(&sender), sender_size);
    cJSON_free(encoded);
  }
}

/**
 * @brief 监视网络变化、维护发现服务并清理超时会话
 */
void ConnectionTask(void*) {
  httpd_handle_t server = nullptr;
  int discovery_socket = -1;
  uint32_t network_generation = 0;
  uint32_t network_address = 0;
  int64_t last_firmware_status_ms = 0;
  while (true) {
    hal::WifiStatus wifi;
    const bool network_ready = g_wifi->ReadWifiStatus(&wifi) && wifi.got_ip;
    if (server != nullptr &&
        (!network_ready || network_generation != wifi.connection_generation ||
            network_address != wifi.ip_address)) {
      // 不持有状态锁等待 HTTP 服务停止，避免关闭回调死锁。
      httpd_ssl_stop(server);
      server = nullptr;
      if (discovery_socket >= 0) close(discovery_socket);
      discovery_socket = -1;
      std::lock_guard<std::mutex> lock(g_mutex);
      for (auto& session : g_sessions) ResetSession(session);
      LogMessage(LogLevel::kInfo, __FILE__, __LINE__,
          "Network changed; waiting to restore local service\n");
    }
    if (network_ready && server == nullptr) {
      httpd_ssl_config_t tls = HTTPD_SSL_CONFIG_DEFAULT();
      auto& config = tls.httpd;
      tls.port_secure = kWebSocketPort;
      tls.tls_handshake_timeout_ms = 3000;
      config.ctrl_port = 18465;
      config.max_open_sockets = kSessionCapacity;
      // 保留当前会话，避免候选应用挤掉已连接应用；握手由服务任务串行处理。
      config.lru_purge_enable = false;
      config.recv_wait_timeout = 2;
      config.send_wait_timeout = 2;
      config.close_fn = SocketClosed;
      if (ConfigureConnectionTls(&tls) &&
          httpd_ssl_start(&server, &tls) == ESP_OK) {
        httpd_uri_t endpoint = {};
        endpoint.uri = "/api/v1/ws";
        endpoint.method = HTTP_GET;
        endpoint.handler = WebSocketHandler;
        endpoint.is_websocket = true;
        if (httpd_register_uri_handler(server, &endpoint) != ESP_OK) {
          httpd_ssl_stop(server);
          server = nullptr;
        }
      }
      if (server != nullptr) {
        discovery_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        sockaddr_in address = {};
        address.sin_family = AF_INET;
        address.sin_port = htons(kDiscoveryPort);
        address.sin_addr.s_addr = htonl(INADDR_ANY);
        timeval timeout = {.tv_sec = 0, .tv_usec = 100000};
        if (discovery_socket < 0 ||
            setsockopt(discovery_socket, SOL_SOCKET, SO_RCVTIMEO, &timeout,
                sizeof(timeout)) != 0 ||
            bind(discovery_socket, reinterpret_cast<sockaddr*>(&address),
                sizeof(address)) != 0) {
          if (discovery_socket >= 0) close(discovery_socket);
          discovery_socket = -1;
          httpd_ssl_stop(server);
          server = nullptr;
        } else {
          network_generation = wifi.connection_generation;
          network_address = wifi.ip_address;
          LogMessage(LogLevel::kInfo, __FILE__, __LINE__,
              "Local discovery and encrypted WebSocket ready\n");
        }
      }
    }
    int close_fds[kSessionCapacity];
    size_t close_count = 0;
    bool notify_disconnect = false;
    bool cancel_local_update = false;
    {
      std::lock_guard<std::mutex> lock(g_mutex);
      g_status.network_ready = network_ready;
      g_status.service_ready = server != nullptr && discovery_socket >= 0;
      if (network_ready) {
        in_addr ip = {.s_addr = wifi.ip_address};
        inet_ntop(AF_INET, &ip, g_status.address, sizeof(g_status.address));
      } else {
        g_status.address[0] = '\0';
      }
      SelectCandidate();
      for (auto& session : g_sessions) {
        if (session.fd >= 0 && session.disconnect_requested &&
            !session.close_requested)
          notify_disconnect = true;
        if (session.fd >= 0 &&
            (session.close_requested ||
                NowMs() - session.last_seen > kHeartbeatMs ||
                (!(session.fd == g_active_fd && g_status.connected) &&
                    NowMs() >= session.until))) {
          close_fds[close_count++] = session.fd;
          session.close_requested = true;
        }
      }
      if (g_local_update_session_id != 0) {
        bool owner_connected = false;
        for (const auto& session : g_sessions) {
          if (session.session_id == g_local_update_session_id &&
              session.fd >= 0 && session.fd == g_active_fd &&
              session.authenticated && g_status.connected &&
              !session.close_requested && !session.disconnect_requested &&
              NowMs() - session.last_seen <= kHeartbeatMs) {
            owner_connected = true;
            break;
          }
        }
        if (!FirmwareUpdateManager::Instance().IsLocalUpdateActive()) {
          g_local_update_session_id = 0;
        } else if (!owner_connected) {
          cancel_local_update = true;
        }
      }
    }
    /**
     * @brief 在连接状态锁外取消失联应用的更新，避免网络关闭和文件清理阻塞会话锁
     * @note 会话编号保留到更新结束，防止重连或套接字复用接管旧更新；安装阶段不取消
     */
    if (cancel_local_update) {
      FirmwareUpdateManager::Instance().CancelLocalUpdate();
    }
    if (notify_disconnect && server != nullptr)
      httpd_queue_work(server, SendRequestedDisconnects, server);
    for (size_t i = 0; i < close_count && server != nullptr; ++i)
      httpd_sess_trigger_close(server, close_fds[i]);
    if (discovery_socket >= 0) ReplyToDiscovery(discovery_socket);
    if (server != nullptr && NowMs() - last_firmware_status_ms >= 500) {
      httpd_queue_work(server, SendFirmwareUpdateStatus, server);
      last_firmware_status_ms = NowMs();
    }
    vTaskDelay(pdMS_TO_TICKS(server == nullptr ? 1000 : 100));
  }
}

}  // namespace

/**
 * @brief 启动局域网连接服务并恢复已保存的自动连接偏好
 * @param wifi 设备 Wi-Fi 状态接口
 * @return 服务任务创建成功返回 true
 */
bool InitializeAppConnection(hal::WifiProvider* wifi,
                             hal::DeviceInfoProvider* device_info) {
  hal::DeviceInfo info;
  if (device_info != nullptr && device_info->ReadDeviceInfo(&info) &&
      info.device_model_version != nullptr) {
    const char* model_version = info.device_model_version;
    if (model_version[0] == 'v') ++model_version;
    std::snprintf(g_model_version, sizeof(g_model_version), "%s",
                  model_version);
  }

  if (wifi == nullptr) return false;
  std::lock_guard<std::mutex> lock(g_mutex);
  if (g_initialized) return true;
  uint8_t mac[6] = {};
  if (esp_efuse_mac_get_default(mac) != ESP_OK) return false;
  std::snprintf(g_status.device_id, sizeof(g_status.device_id),
      "%02x%02x%02x%02x%02x%02x", mac[0], mac[1], mac[2], mac[3], mac[4],
      mac[5]);
  g_wifi = wifi;
  if (!ReadConnectionPreferences(&g_preferences)) return false;
  LogMessage(LogLevel::kInfo, __FILE__, __LINE__,
      "Device=%s auto-connect=%d paired=%d\n", g_status.device_id,
      g_preferences.enabled, g_preferences.count != 0);
  g_initialized = xTaskCreate(ConnectionTask, "app_connection", 10240, nullptr,
                      3, nullptr) == pdPASS;
  return g_initialized;
}

/**
 * @brief 在状态锁保护下读取连接状态，供 UI 线程刷新
 * @return 包含网络、配对和会话状态的快照
 */
AppConnectionStatus ReadAppConnectionStatus() {
  std::lock_guard<std::mutex> lock(g_mutex);
  AppConnectionStatus status = g_status;
  status.auto_connect = g_preferences.enabled;
  status.paired = g_preferences.count != 0;
  status.discovering =
      status.service_ready && status.auto_connect && g_active_fd < 0;
  return status;
}

/**
 * @brief 保存自动连接偏好，网络未就绪时等待；关闭时断开会话但保留授权
 * @param enabled 是否允许发现和建立连接
 * @return 配置持久化成功返回 true，保存失败返回 false
 */
bool SetAppAutoConnect(bool enabled) {
  std::lock_guard<std::mutex> lock(g_mutex);
  // 用户可先开启偏好；连接任务会在 WLAN 获得地址后自动启动发现服务。
  auto updated = g_preferences;
  updated.enabled = enabled;
  const bool saved = UpdateConnectionPreferences(updated);
  if (saved) {
    g_preferences = updated;
    if (enabled) std::memset(g_paused_apps, 0, sizeof(g_paused_apps));
    if (!enabled) {
      for (auto& session : g_sessions)
        if (session.fd >= 0) session.close_requested = true;
      g_status.pending = false;
    }
  }
  LogMessage(saved ? LogLevel::kInfo : LogLevel::kWarning, __FILE__, __LINE__,
      "Auto-connect=%d saved=%d\n", enabled, saved);
  if (enabled && !g_status.network_ready) {
    LogMessage(LogLevel::kInfo, __FILE__, __LINE__,
        "App auto-connect remains enabled; waiting for WLAN\n");
  }
  return saved;
}

/**
 * @brief 处理首次连接确认，只接受仍有效的请求编号
 * @param request_id 待确认请求的唯一编号
 * @param allow 是否允许该应用连接并记住授权
 */
void ConfirmAppConnection(uint32_t request_id, bool allow) {
  std::lock_guard<std::mutex> lock(g_mutex);
  auto* session = FindSession(g_pending_fd);
  if (!g_status.pending || g_status.request_id != request_id ||
      session == nullptr || NowMs() >= session->until ||
      session->close_requested)
    return;
  const size_t index = ClientCredentialIndex(session->client_id);
  const bool replacing = index < g_preferences.count;
  allow = allow && (replacing || g_preferences.count < kAppCredentialCapacity) &&
          g_active_fd < 0;
  if (allow) {
    auto updated = g_preferences;
    // 用户重新确认后轮换密钥并复用原槽位，保留排序；满额也允许更新自身。
    auto& client = updated.clients[index];
    RandomHex(client.token);
    std::snprintf(client.client_id, sizeof(client.client_id), "%s", session->client_id);
    std::snprintf(
        client.client_name, sizeof(client.client_name), "%s", session->name);
    allow = MakeAppCredentialId(client.token, client.id);
    if (allow) {
      if (!replacing) ++updated.count;
      allow = UpdateConnectionPreferences(updated);
    }
    if (allow) {
      if (replacing) {
        const char* previous_id = g_preferences.clients[index].id;
        if (auto* paused = PausedApp(previous_id)) paused[0] = '\0';
        for (auto& other : g_sessions)
          if (other.fd >= 0 && other.fd != session->fd &&
              std::strcmp(other.credential_id, previous_id) == 0)
            other.close_requested = true;
      }
      g_preferences = updated;
      std::snprintf(session->credential_id, sizeof(session->credential_id),
          "%s", client.id);
      session->authenticated = true;
      session->new_pairing = true;
      session->until = NowMs() + kHeartbeatMs;
      // 首次确认属于用户明确操作，优先于后台候选重连。
      g_active_fd = session->fd;
    }
  }
  g_status.pending = false;
  g_pending_fd = -1;
  if (!allow) session->close_requested = true;
  LogMessage(allow ? LogLevel::kInfo : LogLevel::kWarning, __FILE__, __LINE__,
      "App approval=%d saved=%u\n", allow,
      static_cast<unsigned>(g_preferences.count));
}

/**
 * @brief 返回有序应用名称与当前连接标记，密钥不会传到界面
 * @return 授权列表快照
 */
AuthorizedApps ReadAuthorizedApps() {
  std::lock_guard<std::mutex> lock(g_mutex);
  AuthorizedApps result;
  result.count = g_preferences.count;
  const auto* active = FindSession(g_active_fd);
  for (size_t i = 0; i < result.count; ++i) {
    const auto& source = g_preferences.clients[i];
    auto& target = result.clients[i];
    std::snprintf(target.id, sizeof(target.id), "%s", source.id);
    std::snprintf(target.name, sizeof(target.name), "%s", source.client_name);
    target.connected = active != nullptr && !active->close_requested &&
                       g_status.connected &&
                       std::strcmp(active->credential_id, source.id) == 0;
  }
  return result;
}

/**
 * @brief 调整指定应用的优先级并持久保存，不打断正在使用的会话
 * @param id 授权公开标识
 * @param position 目标位置，从零开始
 * @return 保存成功返回 true
 */
bool MoveAppCredential(const char* id, size_t position) {
  std::lock_guard<std::mutex> lock(g_mutex);
  if (id == nullptr) return false;
  const size_t from = CredentialIndex(id);
  if (from >= g_preferences.count || position >= g_preferences.count)
    return false;
  auto updated = g_preferences;
  const auto moving = updated.clients[from];
  if (from < position)
    for (size_t i = from; i < position; ++i)
      updated.clients[i] = updated.clients[i + 1];
  else
    for (size_t i = from; i > position; --i)
      updated.clients[i] = updated.clients[i - 1];
  updated.clients[position] = moving;
  if (!UpdateConnectionPreferences(updated)) return false;
  g_preferences = updated;
  LogMessage(LogLevel::kInfo, __FILE__, __LINE__, "App priority updated\n");
  return true;
}

/**
 * @brief 请求断开指定应用，保留凭证并禁止其后台立即重连
 * @param id 授权公开标识
 * @return 已提交请求返回 true
 */
bool DisconnectApp(const char* id) {
  std::lock_guard<std::mutex> lock(g_mutex);
  if (id == nullptr || CredentialIndex(id) >= g_preferences.count) return false;
  auto* session = FindSession(g_active_fd);
  if (session == nullptr || session->close_requested ||
      std::strcmp(session->credential_id, id) != 0)
    return false;
  if (PausedApp(id) == nullptr) {
    for (auto& paused : g_paused_apps) {
      if (paused[0] != '\0') continue;
      std::snprintf(paused, sizeof(paused), "%s", id);
      break;
    }
  }
  session->disconnect_requested = true;
  LogMessage(LogLevel::kInfo, __FILE__, __LINE__,
      "App manually disconnected; authorization retained\n");
  return true;
}

/**
 * @brief 持久删除指定授权后关闭其活动及候选连接，保存失败时保留连接
 * @param id 授权公开标识
 * @return 删除保存成功返回 true
 */
bool RemoveAppCredential(const char* id) {
  std::lock_guard<std::mutex> lock(g_mutex);
  if (id == nullptr) return false;
  const size_t index = CredentialIndex(id);
  if (index >= g_preferences.count) return false;
  char removed_id[65] = {};
  std::snprintf(removed_id, sizeof(removed_id), "%s", id);
  auto updated = g_preferences;
  for (size_t i = index + 1; i < updated.count; ++i)
    updated.clients[i - 1] = updated.clients[i];
  updated.clients[--updated.count] = {};
  if (!UpdateConnectionPreferences(updated)) return false;
  g_preferences = updated;
  if (auto* paused = PausedApp(removed_id)) paused[0] = '\0';
  for (auto& session : g_sessions)
    if (session.fd >= 0 && std::strcmp(session.credential_id, removed_id) == 0)
      session.close_requested = true;
  LogMessage(
      LogLevel::kInfo, __FILE__, __LINE__, "App authorization removed\n");
  return true;
}

bool TakeDeviceControlCommand(bool screen_task, DeviceControlCommand* command) {
  if (command == nullptr) return false;
  std::lock_guard<std::mutex> lock(g_mutex);
  auto* session = FindSession(g_active_fd);
  if (session == nullptr || !g_status.connected || !g_preferences.enabled ||
      session->close_requested || session->disconnect_requested ||
      !session->control_pending)
    return false;
  if (NowMs() > session->control_until) {
    session->control_pending = false;
    session->control_completed = true;
    std::snprintf(session->control_error, sizeof(session->control_error),
                  "expired");
    return false;
  }
  if (screen_task !=
      (session->control.field == DeviceControlField::kScreenEnabled))
    return false;
  *command = session->control;
  session->control_pending = false;
  session->control_executing = true;
  return true;
}

void CompleteDeviceControlCommand(const DeviceControlCommand& command,
                                  const char* error) {
  std::lock_guard<std::mutex> lock(g_mutex);
  auto* session = FindSession(g_active_fd);
  if (session == nullptr || session->session_id != command.session_id ||
      session->control.request_id != command.request_id)
    return;
  session->control_executing = false;
  session->control_completed = true;
  std::snprintf(session->control_error, sizeof(session->control_error), "%s",
                error);
}

void PublishDeviceControlState(const DeviceControlState& state) {
  std::lock_guard<std::mutex> lock(g_mutex);
  g_control_state = state;
}

}  // namespace lilygo_box::app
