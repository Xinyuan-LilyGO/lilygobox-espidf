/*
 * @Description: RTL8152B USB 传输筛选与 CDC 生命周期实现
 * @Author: LILYGO_L
 * @License: GPL 3.0
 */
#include "ethernet_transport.h"

#include <atomic>
#include <mutex>

#include "base/logger.h"
#include "esp_err.h"
#include "freertos/task.h"
#include "usb/usb_host.h"

extern "C" esp_err_t __real_usb_host_client_register(
    const usb_host_client_config_t* config,
    usb_host_client_handle_t* client_ret);
extern "C" esp_err_t __real_usb_host_transfer_submit(usb_transfer_t* transfer);
extern "C" esp_err_t __real_usb_host_client_handle_events(
    usb_host_client_handle_t client, TickType_t timeout_ticks);

namespace {

constexpr uint16_t kBoardHubVid = 0x05E3;
constexpr uint16_t kBoardHubPid = 0x0610;
// T-Display-P4 V2.0 原理图中，GL852G 端口 2 接板载网卡，端口 4 接 Type-A。
constexpr uint8_t kOnboardPort = 2;
std::atomic<bool> s_device_events_cancelled{false};

std::atomic<TaskHandle_t> s_installing_task{nullptr};
std::atomic<usb_device_handle_t> s_selected_device{nullptr};
std::atomic<bool> s_device_events_enabled{false};
std::atomic<bool> s_device_scan_pending{false};
std::atomic<usb_host_client_handle_t> s_cdc_client{nullptr};
const iot_eth_driver_t* s_ecm_driver = nullptr;
uint8_t s_selected_address = 0;
usb_host_client_event_cb_t s_cdc_callback = nullptr;
void* s_cdc_callback_arg = nullptr;
std::mutex s_device_event_mutex;
std::mutex s_control_request_mutex;

/**
 * @brief 沿父设备查找板载 Hub 分支，支持 Type-A 外接多级 Hub
 * @param device 待检查的设备句柄
 * @param port 成功时写入板载 Hub 的下游端口号
 * @return 找到板载 Hub 返回 ESP_OK，否则返回错误码
 */
esp_err_t GetBoardHubPort(usb_device_handle_t device, uint8_t* port) {
  for (int depth = 0; depth < 7; ++depth) {
    usb_device_info_t info = {};
    esp_err_t result = usb_host_device_info(device, &info);
    if (result != ESP_OK) {
      lilygo_box::LogMessage(lilygo_box::LogLevel::kError, __FILE__, __LINE__,
          "Read USB device topology failed: %s\n", esp_err_to_name(result));
      return result;
    }
    if (info.parent.dev_hdl == nullptr) {
      return ESP_ERR_NOT_FOUND;
    }

    usb_device_info_t parent_info = {};
    result = usb_host_device_info(info.parent.dev_hdl, &parent_info);
    if (result != ESP_OK) {
      lilygo_box::LogMessage(lilygo_box::LogLevel::kError, __FILE__, __LINE__,
          "Read parent USB device topology failed: %s\n",
          esp_err_to_name(result));
      return result;
    }
    if (parent_info.parent.dev_hdl == nullptr) {
      const usb_device_desc_t* descriptor = nullptr;
      result = usb_host_get_device_descriptor(info.parent.dev_hdl, &descriptor);
      if (result != ESP_OK) {
        lilygo_box::LogMessage(lilygo_box::LogLevel::kError, __FILE__, __LINE__,
            "Read root hub descriptor failed: %s\n", esp_err_to_name(result));
        return result;
      }
      if (descriptor->bDeviceClass != USB_CLASS_HUB ||
          descriptor->idVendor != kBoardHubVid ||
          descriptor->idProduct != kBoardHubPid) {
        return ESP_ERR_NOT_SUPPORTED;
      }
      *port = info.parent.port_num;
      return ESP_OK;
    }
    device = info.parent.dev_hdl;
  }
  return ESP_ERR_NOT_SUPPORTED;
}

/**
 * @brief 筛选指定分支的 RTL8152B 后转发 CDC 设备通知
 * @param event USB 主机设备事件
 * @param arg 未使用
 */
void SelectedDeviceEvent(const usb_host_client_event_msg_t* event, void* arg) {
  (void)arg;
  std::unique_lock<std::mutex> lock(s_device_event_mutex, std::defer_lock);
  while (!lock.try_lock()) {
    // 关闭端口时不能阻塞 CDC 事件任务，否则它无法处理被 flush 的传输。
    if (s_device_events_cancelled.load()) {
      return;
    }
    vTaskDelay(pdMS_TO_TICKS(1));
  }
  if (s_device_events_cancelled.load()) {
    return;
  }
  if (event->event == USB_HOST_CLIENT_EVENT_NEW_DEV) {
    // CDC 安装后即开始收取事件，先等待 ECM 注册监听并完成 netif 绑定。
    while (
        !s_device_events_enabled.load() && !s_device_events_cancelled.load()) {
      vTaskDelay(pdMS_TO_TICKS(10));
    }
    if (s_device_events_cancelled.load()) {
      return;
    }
    // 补扫和真实枚举通知可能重叠；同一客户端不能重复打开已占用的设备。
    if (event->new_dev.address == s_selected_address) {
      return;
    }
    usb_device_handle_t device = nullptr;
    const esp_err_t open_result = usb_host_device_open(
        s_cdc_client.load(), event->new_dev.address, &device);
    if (open_result != ESP_OK) {
      // 地址快照不持有设备引用，扫描期间拔出属于正常竞态。
      const auto level = open_result == ESP_ERR_NOT_FOUND ||
                                 open_result == ESP_ERR_INVALID_STATE
                             ? lilygo_box::LogLevel::kWarning
                             : lilygo_box::LogLevel::kError;
      lilygo_box::LogMessage(level, __FILE__, __LINE__,
          "Open USB device %u for filtering failed: %s\n",
          event->new_dev.address, esp_err_to_name(open_result));
      return;
    }

    const usb_device_desc_t* descriptor = nullptr;
    esp_err_t result = usb_host_get_device_descriptor(device, &descriptor);
    uint8_t port = 0;
    bool selected = false;
    bool is_rtl8152 = false;
    if (result == ESP_OK && descriptor->bDeviceClass != USB_CLASS_HUB) {
      is_rtl8152 =
          descriptor->idVendor == 0x0BDA && descriptor->idProduct == 0x8152;
      result = GetBoardHubPort(device, &port);
      selected = is_rtl8152 && result == ESP_OK && port == kOnboardPort;
      lilygo_box::LogMessage(lilygo_box::LogLevel::kInfo, __FILE__, __LINE__,
          "%s USB device: address=%u, VID:%04X PID:%04X, "
          "board hub port=%u, topology=%s\n",
          selected ? "Select Ethernet" : "Skip device", event->new_dev.address,
          descriptor->idVendor, descriptor->idProduct, port,
          esp_err_to_name(result));
    } else if (result != ESP_OK) {
      lilygo_box::LogMessage(lilygo_box::LogLevel::kError, __FILE__, __LINE__,
          "Read USB device descriptor failed: %s\n", esp_err_to_name(result));
    }

    if (usb_host_device_close(s_cdc_client.load(), device) != ESP_OK) {
      lilygo_box::LogMessage(lilygo_box::LogLevel::kError, __FILE__, __LINE__,
          "Close USB device after filtering failed\n");
      return;
    }
    if (!selected) {
      return;
    }
    if (is_rtl8152) {
      s_selected_device.store(device);
    }
  } else if (event->event == USB_HOST_CLIENT_EVENT_DEV_GONE) {
    usb_device_handle_t device = event->dev_gone.dev_hdl;
    if (!s_selected_device.compare_exchange_strong(device, nullptr)) {
      return;
    }
    s_selected_address = 0;
  }
  s_cdc_callback(event, s_cdc_callback_arg);
  if (event->event == USB_HOST_CLIENT_EVENT_NEW_DEV) {
    // 只有 ECM 成功打开端口才去重；打开失败不能留下无法清除的设备记录。
    if (usb_ecm_get_cdc_port_handle(s_ecm_driver) != nullptr) {
      s_selected_address = event->new_dev.address;
    } else {
      s_selected_device.store(nullptr);
    }
  }
}

/**
 * @brief 在 CDC 事件任务中补发共享 Host 已枚举设备的连接通知
 * @note 沿用板载端口筛选，不重新枚举设备，也不改变其他客户端的状态
 */
void ScanExistingDevices() {
  constexpr int kMaxDeviceAddresses = 127;
  uint8_t addresses[kMaxDeviceAddresses] = {};
  int count = 0;
  const esp_err_t result =
      usb_host_device_addr_list_fill(kMaxDeviceAddresses, addresses, &count);
  if (result != ESP_OK) {
    lilygo_box::LogMessage(lilygo_box::LogLevel::kError, __FILE__, __LINE__,
        "Scan existing USB devices failed: %s\n", esp_err_to_name(result));
    return;
  }
  lilygo_box::LogMessage(lilygo_box::LogLevel::kInfo, __FILE__, __LINE__,
      "Ethernet scan existing USB devices: count=%d\n", count);
  for (int index = 0; index < count; ++index) {
    if (s_device_events_cancelled.load()) {
      return;
    }
    usb_host_client_event_msg_t event = {};
    event.event = USB_HOST_CLIENT_EVENT_NEW_DEV;
    event.new_dev.address = addresses[index];
    SelectedDeviceEvent(&event, nullptr);
  }
}

}  // namespace

/**
 * @brief 为本模块安装的 CDC 客户端注入板载 Hub 端口筛选
 * @param config USB 客户端配置
 * @param client_ret 客户端句柄输出地址
 * @return 注册成功返回 ESP_OK，否则返回错误码
 */
extern "C" esp_err_t __wrap_usb_host_client_register(
    const usb_host_client_config_t* config,
    usb_host_client_handle_t* client_ret) {
  if (s_installing_task.load() != xTaskGetCurrentTaskHandle()) {
    return __real_usb_host_client_register(config, client_ret);
  }
  if (config == nullptr || client_ret == nullptr || config->is_synchronous ||
      config->async.client_event_callback == nullptr) {
    return ESP_ERR_INVALID_ARG;
  }

  usb_host_client_config_t filtered_config = *config;
  s_cdc_callback = config->async.client_event_callback;
  s_cdc_callback_arg = config->async.callback_arg;
  filtered_config.async.client_event_callback = SelectedDeviceEvent;
  filtered_config.async.callback_arg = nullptr;
  const esp_err_t result =
      __real_usb_host_client_register(&filtered_config, client_ret);
  if (result == ESP_OK) {
    s_cdc_client.store(*client_ret);
  }
  return result;
}

/**
 * @brief 处理 CDC 已排队事件后，在同一任务中扫描启动前已连接的设备
 * @param client USB 客户端句柄
 * @param timeout_ticks 等待 USB 事件的最大 tick 数
 * @return USB 事件处理结果
 * @note 其他 USB 客户端保持原行为，避免影响已挂载的 U 盘
 */
extern "C" esp_err_t __wrap_usb_host_client_handle_events(
    usb_host_client_handle_t client, TickType_t timeout_ticks) {
  const esp_err_t result =
      __real_usb_host_client_handle_events(client, timeout_ticks);
  if (client != nullptr && client == s_cdc_client.load() &&
      s_device_events_enabled.load() && !s_device_events_cancelled.load() &&
      s_device_scan_pending.exchange(false)) {
    ScanExistingDevices();
  }
  return result;
}

/**
 * @brief 为 RTL8152B bulk OUT 传输补充帧尾短包标志
 * @param transfer 待提交的 USB 传输
 * @return 提交成功返回 ESP_OK，否则返回错误码
 */
extern "C" esp_err_t __wrap_usb_host_transfer_submit(usb_transfer_t* transfer) {
  const usb_device_handle_t device = s_selected_device.load();
  const bool is_ethernet = transfer != nullptr && device != nullptr &&
                           transfer->device_handle == device;
  if (is_ethernet && transfer->bEndpointAddress != 0 &&
      (transfer->bEndpointAddress & 0x80) == 0) {
    transfer->flags |= USB_TRANSFER_FLAG_ZERO_PACK;
  }
  const esp_err_t result = __real_usb_host_transfer_submit(transfer);
  if (is_ethernet && result != ESP_OK) {
    lilygo_box::LogMessage(lilygo_box::LogLevel::kError, __FILE__, __LINE__,
        "Ethernet USB submit failed: endpoint=0x%02X, bytes=%d, %s\n",
        transfer->bEndpointAddress, transfer->num_bytes,
        esp_err_to_name(result));
  }
  return result;
}

namespace lilygo_box::hal::usb_ethernet::internal {

esp_err_t InstallCdc(const usbh_cdc_driver_config_t* config) {
  s_device_events_enabled.store(false);
  s_device_events_cancelled.store(false);
  s_device_scan_pending.store(false);
  s_selected_device.store(nullptr);
  s_selected_address = 0;
  s_ecm_driver = nullptr;
  s_installing_task.store(xTaskGetCurrentTaskHandle());
  const esp_err_t result = usbh_cdc_driver_install(config);
  s_installing_task.store(nullptr);
  return result;
}

esp_err_t EnableDeviceEvents(const iot_eth_driver_t* driver) {
  const usb_host_client_handle_t client = s_cdc_client.load();
  if (driver == nullptr || client == nullptr) {
    return ESP_ERR_INVALID_STATE;
  }
  s_ecm_driver = driver;
  s_device_scan_pending.store(true);
  s_device_events_enabled.store(true);
  // 已枚举设备不会再次产生 NEW_DEV，唤醒 CDC 任务执行一次补扫。
  return usb_host_client_unblock(client);
}

void CancelDeviceEvents() {
  s_device_events_cancelled.store(true);
  s_device_scan_pending.store(false);
  // 先解开初始化等待，再等正在执行的 CDC 设备回调退出。
  std::lock_guard<std::mutex> lock(s_device_event_mutex);
}

esp_err_t CloseEcmPort(const iot_eth_driver_t* driver) {
  std::lock_guard<std::mutex> event_lock(s_device_event_mutex);
  std::lock_guard<std::mutex> request_lock(s_control_request_mutex);
  const auto port = usb_ecm_get_cdc_port_handle(driver);
  if (port == nullptr) {
    return ESP_OK;
  }
  // iot_usbh_ecm 0.4.0 的 deinit 不关闭端口；必须先停止端口回调，
  // 否则收包/通知会继续访问已经被 deinit 释放的 ECM 对象。
  const esp_err_t result = usbh_cdc_port_close(port);
  return result == ESP_ERR_INVALID_ARG ? ESP_OK : result;
}

void ResetTransport() {
  // 仅在 CDC 完全卸载后调用。
  s_selected_device.store(nullptr);
  s_selected_address = 0;
  s_ecm_driver = nullptr;
  s_device_scan_pending.store(false);
  s_device_events_enabled.store(false);
  s_cdc_client.store(nullptr);
  s_cdc_callback = nullptr;
  s_cdc_callback_arg = nullptr;
}

}  // namespace lilygo_box::hal::usb_ethernet::internal

namespace {

constexpr uint8_t kClassInterfaceOut = 0x21;
constexpr uint8_t kSetEthernetPacketFilter = 0x43;
constexpr uint16_t kDefaultPacketFilter = 0x000E;
constexpr uint16_t kPromiscuousPacketFilter = 0x0001;
std::atomic<bool> promiscuous_mode_enabled{false};

}  // namespace

namespace lilygo_box::hal::usb_ethernet::internal {

void EnableEcmPromiscuousMode(bool enabled) {
  promiscuous_mode_enabled.store(enabled);
}

}  // namespace lilygo_box::hal::usb_ethernet::internal

extern "C" esp_err_t __real_usbh_cdc_send_custom_request(
    usbh_cdc_port_handle_t cdc_port_handle, uint8_t bm_request_type,
    uint8_t request, uint16_t value, uint16_t index, uint16_t length,
    uint8_t* data);

/**
 * @brief 串行化 ECM 控制请求，并调整软件 MAC 所需的接收过滤
 * @param cdc_port_handle CDC 端口句柄
 * @param bm_request_type USB 请求类型
 * @param request USB 请求编号
 * @param value USB 请求值
 * @param index USB 请求索引
 * @param length 数据阶段长度
 * @param data 数据缓冲区
 * @return 请求成功返回 ESP_OK，否则返回错误码
 * @note 只改变接收过滤，不写入 RTL8152B 的 MAC、EEPROM 或 OTP
 */
extern "C" esp_err_t __wrap_usbh_cdc_send_custom_request(
    usbh_cdc_port_handle_t cdc_port_handle, uint8_t bm_request_type,
    uint8_t request, uint16_t value, uint16_t index, uint16_t length,
    uint8_t* data) {
  std::lock_guard<std::mutex> lock(s_control_request_mutex);
  if (s_device_events_cancelled.load()) {
    return ESP_ERR_INVALID_STATE;
  }
  const bool override_filter =
      promiscuous_mode_enabled.load() && cdc_port_handle != nullptr &&
      bm_request_type == kClassInterfaceOut &&
      request == kSetEthernetPacketFilter && value == kDefaultPacketFilter &&
      length == 0 && data == nullptr;
  if (override_filter) {
    value |= kPromiscuousPacketFilter;
  }
  const esp_err_t result = __real_usbh_cdc_send_custom_request(
      cdc_port_handle, bm_request_type, request, value, index, length, data);
  if (override_filter) {
    if (result == ESP_OK) {
      lilygo_box::LogMessage(lilygo_box::LogLevel::kInfo, __FILE__, __LINE__,
          "ECM packet filter=0x%04X, interface=%u (software MAC)\n",
          static_cast<unsigned>(value), static_cast<unsigned>(index));
    } else {
      lilygo_box::LogMessage(lilygo_box::LogLevel::kError, __FILE__, __LINE__,
          "Set ECM packet filter=0x%04X, interface=%u failed: %s\n",
          static_cast<unsigned>(value), static_cast<unsigned>(index),
          esp_err_to_name(result));
    }
  }
  return result;
}
