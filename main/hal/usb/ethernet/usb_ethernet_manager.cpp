/*
 * @Description: T-Display-P4 V2 USB 以太网异步管理实现
 * @Author: LILYGO_L
 * @License: GPL 3.0
 */
#include "hal/usb/ethernet/usb_ethernet_manager.h"

#include <atomic>
#include <cstdlib>
#include <mutex>
#include <utility>

#include "base/logger.h"
#include "esp_event.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "ethernet_transport.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "hal/device/common/wifi_utils.h"
#include "hal/usb/usb_host_service.h"
#include "iot_eth.h"
#include "iot_usbh_ecm.h"

namespace lilygo_box::hal {
namespace transport = usb_ethernet::internal;
namespace {
ESP_EVENT_DEFINE_BASE(USB_ETH_DRAIN_EVENT);
}  // namespace

struct EthernetNetifDriver : esp_netif_driver_base_t {
  UsbEthernetState* owner = nullptr;
};

struct UsbEthernetState {
  explicit UsbEthernetState(UsbEthernetManager::PowerControl control)
      : power_control(std::move(control)) {}

  UsbEthernetManager::PowerControl power_control;
  std::mutex control_mutex;
  std::mutex status_mutex;
  std::atomic<bool> requested{false};
  std::atomic<bool> active{false};
  EthernetStatus status;
  EthernetNetifDriver netif_driver = {};
  iot_eth_handle_t handle = nullptr;
  iot_eth_driver_t* ecm_driver = nullptr;
  esp_event_handler_instance_t ethernet_events = nullptr;
  esp_event_handler_instance_t ip_events = nullptr;
  bool host_acquired = false;
  bool cdc_installed = false;
  bool driver_started = false;
  // 只有默认事件循环访问，卸载事件处理器之后才由清理任务复位。
  bool netif_started = false;
  bool link_connected = false;
};

namespace {
/**
 * @brief 清除上一次 DHCP 地址及已获取地址标志
 * @param status 待更新的以太网状态
 */
void ClearAddress(EthernetStatus& status) {
  status.got_ip = false;
  status.ip_address = 0;
  status.netmask = 0;
  status.gateway = 0;
}

/**
 * @brief 保存并记录最近一次以太网操作错误
 * @param state 以太网管理器状态
 * @param error 底层错误码
 */
void SetFailure(UsbEthernetState& state, esp_err_t error) {
  std::lock_guard<std::mutex> lock(state.status_mutex);
  state.status.start_failed = true;
  state.status.last_error = error;
  LogMessage(LogLevel::kError, __FILE__, __LINE__,
      "Ethernet operation failed: %s\n", esp_err_to_name(error));
}

/**
 * @brief 将 ECM 接收缓冲区交给网络栈并由网络栈释放
 * @param buffer 接收到的以太网帧
 * @param length 帧长度，单位为字节
 * @param context 目标网络接口
 * @return 网络栈接收成功返回 ESP_OK，否则返回错误码
 */
esp_err_t ReceiveFrame(
    iot_eth_handle_t, uint8_t* buffer, size_t length, void* context) {
  return esp_netif_receive(
      static_cast<esp_netif_t*>(context), buffer, length, nullptr);
}

/**
 * @brief 释放 ECM 分配并移交网络栈的接收缓冲区
 * @param buffer 待释放的缓冲区
 */
void FreeFrame(void*, void* buffer) { std::free(buffer); }

/**
 * @brief 将 ECM 收发接口绑定到 ESP-NETIF
 * @param netif 待绑定的网络接口
 * @param args 包含管理器引用的网络驱动对象
 * @return 绑定成功返回 ESP_OK，否则返回错误码
 */
esp_err_t AttachNetif(esp_netif_t* netif, void* args) {
  auto* state = static_cast<EthernetNetifDriver*>(
      static_cast<esp_netif_driver_base_t*>(args))
                    ->owner;
  state->netif_driver.netif = netif;
  esp_netif_driver_ifconfig_t config = {};
  config.handle = state->handle;
  config.transmit = iot_eth_transmit;
  config.driver_free_rx_buffer = FreeFrame;
  esp_err_t result = esp_netif_set_driver_config(netif, &config);
  if (result == ESP_OK) {
    result = iot_eth_update_input_path(state->handle, ReceiveFrame, netif);
  }
  return result;
}

/**
 * @brief 在驱动启动且物理链路已连接时启动网络接口和 DHCP
 * @param state 以太网管理器状态
 */
void ConnectNetif(UsbEthernetState& state) {
  if (state.netif_started && state.link_connected && state.requested.load()) {
    esp_netif_action_connected(state.netif_driver.netif, IOT_ETH_EVENT,
        IOT_ETH_EVENT_CONNECTED, &state.handle);
  }
}

/**
 * @brief 同步 ECM 链路、网络接口和 DHCP 状态
 * @param arg 以太网管理器状态指针
 * @param base 事件来源
 * @param id 事件编号
 * @param data 事件数据
 */
void EthernetEvent(void* arg, esp_event_base_t base, int32_t id, void* data) {
  auto& state = *static_cast<UsbEthernetState*>(arg);
  if (data == nullptr) {
    return;
  }
  std::lock_guard<std::mutex> lock(state.status_mutex);
  if (base == IP_EVENT) {
    if (id == IP_EVENT_ETH_GOT_IP) {
      const auto* event = static_cast<ip_event_got_ip_t*>(data);
      if (event->esp_netif != state.netif_driver.netif ||
          !state.requested.load() || !state.netif_started ||
          !state.link_connected) {
        return;
      }
      state.status.got_ip = true;
      state.status.ip_address = event->ip_info.ip.addr;
      state.status.netmask = event->ip_info.netmask.addr;
      state.status.gateway = event->ip_info.gw.addr;
      LogMessage(LogLevel::kInfo, __FILE__, __LINE__,
          "RTL8152B got IP: " IPSTR "\n", IP2STR(&event->ip_info.ip));
    } else if (id == IP_EVENT_ETH_LOST_IP &&
               static_cast<ip_event_got_ip_t*>(data)->esp_netif ==
                   state.netif_driver.netif) {
      ClearAddress(state.status);
    }
    return;
  }
  if (base != IOT_ETH_EVENT ||
      *static_cast<iot_eth_handle_t*>(data) != state.handle) {
    return;
  }
  switch (id) {
    case IOT_ETH_EVENT_START:
      if (!state.requested.load()) {
        break;
      }
      esp_netif_action_start(state.netif_driver.netif, base, id, data);
      state.netif_started = true;
      state.status.running = true;
      ConnectNetif(state);
      break;
    case IOT_ETH_EVENT_CONNECTED:
      state.link_connected = true;
      state.status.link_up = true;
      ClearAddress(state.status);
      ConnectNetif(state);
      break;
    case IOT_ETH_EVENT_DISCONNECTED:
      state.link_connected = false;
      state.status.link_up = false;
      ClearAddress(state.status);
      if (state.netif_started) {
        esp_netif_action_disconnected(state.netif_driver.netif, base, id, data);
      }
      break;
    case IOT_ETH_EVENT_STOP:
      esp_netif_action_stop(state.netif_driver.netif, base, id, data);
      state.netif_started = false;
      state.link_connected = false;
      state.status.running = false;
      state.status.link_up = false;
      ClearAddress(state.status);
      break;
    default:
      break;
  }
}

/**
 * @brief 安装共享 Host、CDC 和 ECM，并创建网络接口
 * @param state 以太网管理器状态
 * @return 初始化成功返回 ESP_OK，否则返回错误码
 */
esp_err_t Initialize(UsbEthernetState& state) {
  esp_err_t result = esp_netif_init();
  if (result != ESP_OK && result != ESP_ERR_INVALID_STATE) {
    return result;
  }
  result = esp_event_loop_create_default();
  if (result != ESP_OK && result != ESP_ERR_INVALID_STATE) {
    return result;
  }
  result = usb_host_service::Acquire();
  if (result != ESP_OK) {
    return result;
  }
  state.host_acquired = true;
  transport::EnableEcmPromiscuousMode(true);
  const usbh_cdc_driver_config_t cdc_config = {
      .task_stack_size = 4096,
      .task_priority = configMAX_PRIORITIES - 1,
      .task_coreid = 0,
      .skip_init_usb_host_driver = true,
  };
  result = transport::InstallCdc(&cdc_config);
  if (result != ESP_OK) {
    return result;
  }
  state.cdc_installed = true;

  static usb_device_match_id_t matches[2] = {};
  matches[0].match_flags = USB_DEVICE_ID_MATCH_VID_PID;
  matches[0].idVendor = 0x0BDA;
  matches[0].idProduct = 0x8152;
  iot_usbh_ecm_config_t ecm_config = {};
  ecm_config.match_id_list = matches;
  iot_eth_driver_t* driver = nullptr;
  result = iot_eth_new_usb_ecm(&ecm_config, &driver);
  if (result != ESP_OK) {
    return result;
  }
  iot_eth_config_t eth_config = {};
  eth_config.driver = driver;
  LogMessage(LogLevel::kInfo, __FILE__, __LINE__,
      "Ethernet CDC ready; installing ECM driver\n");
  result = iot_eth_install(&eth_config, &state.handle);
  if (result != ESP_OK) {
    // iot_eth_install 失败会清理 mediator；ECM init 自身回滚已创建的资源。
    std::free(driver);
    return result;
  }
  state.ecm_driver = driver;
  LogMessage(LogLevel::kInfo, __FILE__, __LINE__,
      "Ethernet ECM installed; creating network interface\n");

  esp_netif_inherent_config_t inherent = ESP_NETIF_INHERENT_DEFAULT_ETH();
  inherent.if_key = "LILYGO_USB_ETH";
  inherent.if_desc = "rtl8152b";
  inherent.route_prio = 64;
  const esp_netif_config_t netif_config = {
      .base = &inherent,
      .driver = nullptr,
      .stack = ESP_NETIF_NETSTACK_DEFAULT_ETH,
  };
  state.netif_driver.netif = esp_netif_new(&netif_config);
  if (state.netif_driver.netif == nullptr) {
    return ESP_ERR_NO_MEM;
  }
  state.netif_driver.post_attach = AttachNetif;
  state.netif_driver.owner = &state;
  result = esp_netif_attach(state.netif_driver.netif,
      static_cast<esp_netif_driver_base_t*>(&state.netif_driver));
  if (result != ESP_OK) {
    return result;
  }

  uint8_t mac[6] = {};
  result = esp_efuse_mac_get_default(mac);
  if (result != ESP_OK) {
    return result;
  }
  // 不写 RTL8152B OTP；软件 MAC 配合 ECM promiscuous filter 接收单播。
  mac[0] = static_cast<uint8_t>((mac[0] | 0x02) & 0xFE);
  result = esp_netif_set_mac(state.netif_driver.netif, mac);
  if (result != ESP_OK) {
    return result;
  }
  {
    std::lock_guard<std::mutex> lock(state.status_mutex);
    state.status.mac_address = wifi_utils::PackMacAddress(mac);
    state.status.port_count = 1;
  }
  result = esp_event_handler_instance_register(IOT_ETH_EVENT, ESP_EVENT_ANY_ID,
      EthernetEvent, &state, &state.ethernet_events);
  if (result != ESP_OK) {
    return result;
  }
  result = esp_event_handler_instance_register(
      IP_EVENT, ESP_EVENT_ANY_ID, EthernetEvent, &state, &state.ip_events);
  if (result != ESP_OK) {
    return result;
  }
  result = iot_eth_start(state.handle);
  if (result == ESP_OK) {
    state.driver_started = true;
    LogMessage(LogLevel::kInfo, __FILE__, __LINE__,
        "Ethernet network interface ready; enabling USB device discovery\n");
    result = transport::EnableDeviceEvents(state.ecm_driver);
  }
  return result;
}

/**
 * @brief 通知清理任务，默认事件循环已处理到本会话末尾
 * @param arg 等待通知的任务句柄
 */
void DrainEvent(void* arg, esp_event_base_t, int32_t, void*) {
  xTaskNotifyGive(static_cast<TaskHandle_t>(arg));
}

/**
 * @brief 等待本会话已排队的事件结束，避免句柄复用污染下一次测试
 */
void DrainEvents() {
  esp_event_handler_instance_t instance = nullptr;
  if (esp_event_handler_instance_register(USB_ETH_DRAIN_EVENT, 0, DrainEvent,
          xTaskGetCurrentTaskHandle(), &instance) != ESP_OK) {
    return;
  }
  if (esp_event_post(USB_ETH_DRAIN_EVENT, 0, nullptr, 0, portMAX_DELAY) ==
      ESP_OK) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
  }
  esp_event_handler_instance_unregister(USB_ETH_DRAIN_EVENT, 0, instance);
}

/**
 * @brief 依次停止网络接口、释放 USB 类驱动并归还 Host 引用
 * @param state 以太网管理器状态
 * @return 清理完成返回 ESP_OK，否则返回错误码供后台任务重试
 */
esp_err_t Cleanup(UsbEthernetState& state) {
  transport::CancelDeviceEvents();
  // unregister 与正在执行的默认事件回调同步；之后才销毁 netif。
  if (state.ethernet_events != nullptr) {
    esp_event_handler_instance_unregister(
        IOT_ETH_EVENT, ESP_EVENT_ANY_ID, state.ethernet_events);
    state.ethernet_events = nullptr;
  }
  if (state.ip_events != nullptr) {
    esp_event_handler_instance_unregister(
        IP_EVENT, ESP_EVENT_ANY_ID, state.ip_events);
    state.ip_events = nullptr;
  }
  if (state.netif_started) {
    esp_netif_action_stop(state.netif_driver.netif, IOT_ETH_EVENT,
        IOT_ETH_EVENT_STOP, &state.handle);
    state.netif_started = false;
  }
  if (state.driver_started) {
    const esp_err_t result = iot_eth_stop(state.handle);
    if (result != ESP_OK) {
      return result;
    }
    state.driver_started = false;
  }
  if (state.handle != nullptr) {
    esp_err_t result = transport::CloseEcmPort(state.ecm_driver);
    if (result != ESP_OK) {
      return result;
    }
    result = iot_eth_uninstall(state.handle);
    if (result != ESP_OK) {
      return result;
    }
    state.handle = nullptr;
    state.ecm_driver = nullptr;
  }
  if (state.netif_driver.netif != nullptr) {
    esp_netif_destroy(state.netif_driver.netif);
    state.netif_driver = {};
  }
  if (state.cdc_installed) {
    const esp_err_t result = usbh_cdc_driver_uninstall();
    if (result != ESP_OK) {
      return result;
    }
    state.cdc_installed = false;
    transport::ResetTransport();
  }
  if (state.host_acquired) {
    const esp_err_t result = usb_host_service::Release();
    state.host_acquired = false;
    if (result != ESP_OK) {
      LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
          "Shared USB Host release: %s\n", esp_err_to_name(result));
    }
  }
  transport::EnableEcmPromiscuousMode(false);
  state.link_connected = false;
  DrainEvents();
  return ESP_OK;
}

/**
 * @brief 处理以太网启停请求及失败后的资源清理
 * @param arg 以太网管理器状态指针
 */
void EthernetTask(void* arg) {
  auto& state = *static_cast<UsbEthernetState*>(arg);
  while (true) {
    {
      std::lock_guard<std::mutex> lock(state.status_mutex);
      state.status = {};
      state.status.init_task_running = true;
    }
    const bool powered = state.power_control(true);
    esp_err_t result = powered ? Initialize(state) : ESP_FAIL;
    if (result != ESP_OK) {
      SetFailure(state, result);
      std::lock_guard<std::mutex> lock(state.control_mutex);
      state.requested.store(false);
    }
    {
      std::lock_guard<std::mutex> lock(state.status_mutex);
      state.status.init_task_running = false;
      state.status.driver_initialized = result == ESP_OK;
    }
    while (state.requested.load()) {
      vTaskDelay(pdMS_TO_TICKS(20));
    }
    // 保留 Host 事件泵和状态，直到 class driver 真正退出。
    while ((result = Cleanup(state)) != ESP_OK) {
      SetFailure(state, result);
      vTaskDelay(pdMS_TO_TICKS(100));
    }
    if (powered) {
      while (!state.power_control(false)) {
        SetFailure(state, ESP_FAIL);
        vTaskDelay(pdMS_TO_TICKS(100));
      }
    }
    {
      std::lock_guard<std::mutex> lock(state.status_mutex);
      state.status.driver_initialized = false;
      state.status.running = false;
      state.status.link_up = false;
      ClearAddress(state.status);
    }
    {
      std::lock_guard<std::mutex> lock(state.control_mutex);
      if (!state.requested.load()) {
        state.active.store(false);
        break;
      }
    }
  }
  vTaskDelete(nullptr);
}
}  // namespace

UsbEthernetManager::UsbEthernetManager(PowerControl power_control)
    : state_(std::make_unique<UsbEthernetState>(std::move(power_control))) {}

UsbEthernetManager::~UsbEthernetManager() {
  SetEnabled(false);
  // 板级对象正常驻留至重启；若销毁，必须等待后台任务放弃回调上下文。
  while (state_->active.load()) {
    vTaskDelay(pdMS_TO_TICKS(20));
  }
  std::lock_guard<std::mutex> lock(state_->control_mutex);
}

bool UsbEthernetManager::SetEnabled(bool enabled) {
  std::lock_guard<std::mutex> lock(state_->control_mutex);
  state_->requested.store(enabled);
  if (!enabled || state_->active.load()) {
    return true;
  }
  {
    std::lock_guard<std::mutex> status_lock(state_->status_mutex);
    state_->status = {};
    state_->status.init_task_running = true;
  }
  state_->active.store(true);
  if (xTaskCreate(EthernetTask, "usb_ethernet", 6144, state_.get(), 4,
          nullptr) != pdPASS) {
    state_->active.store(false);
    state_->requested.store(false);
    {
      std::lock_guard<std::mutex> status_lock(state_->status_mutex);
      state_->status.init_task_running = false;
    }
    SetFailure(*state_, ESP_ERR_NO_MEM);
    return false;
  }
  return true;
}

bool UsbEthernetManager::ReadStatus(EthernetStatus* status) const {
  if (status == nullptr) {
    return false;
  }
  std::lock_guard<std::mutex> lock(state_->status_mutex);
  *status = state_->status;
  return true;
}

bool UsbEthernetManager::IsActive() const { return state_->active.load(); }
}  // namespace lilygo_box::hal
