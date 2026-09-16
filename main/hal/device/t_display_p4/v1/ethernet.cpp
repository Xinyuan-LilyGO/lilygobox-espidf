/*
 * @Description: T-Display-P4 以太网硬件实现
 * @Author: LILYGO_L
 * @Date: 2026-08-28 00:00:00
 * @LastEditTime: 2026-09-02 17:52:59
 * @License: GPL 3.0
 */
#include <cstdint>

#include "base/logger.h"
#include "esp_err.h"
#include "esp_eth.h"
#include "esp_eth_mac.h"
#include "esp_eth_phy_802_3.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "hal/device/common/wifi_utils.h"
#include "hal/device/t_display_p4/device.h"

namespace lilygo_box::hal {
namespace device = lilygo_device_driver::t_display_p4::device;
namespace gpio = lilygo_device_driver::t_display_p4::gpio;
namespace {

constexpr uint32_t kEthernetInitTaskStackBytes = 6 * 1024;
constexpr UBaseType_t kEthernetInitTaskPriority = 3;

}  // namespace

bool TDisplayP4Device::SetEthernetEnabled(bool enabled) {
  ethernet_.stop_requested.store(!enabled);
  if (!enabled) {
    if (ethernet_.init_task_running.load()) {
      return true;
    }
    if (ethernet_.handle != nullptr && ethernet_.running.load()) {
      const esp_err_t result =
          esp_eth_stop(reinterpret_cast<esp_eth_handle_t>(ethernet_.handle));
      if (result != ESP_OK && result != ESP_ERR_INVALID_STATE) {
        SetEthernetFailure(result);
        driver_.SetEthernetPowerEnabled(false);
        return false;
      }
    }
    ethernet_.running.store(false);
    ethernet_.link_up.store(false);
    ethernet_.got_ip.store(false);
    ethernet_.start_failed.store(false);
    ethernet_.last_error.store(ESP_OK);
    ethernet_.ip_address.store(0);
    ethernet_.netmask.store(0);
    ethernet_.gateway.store(0);
    return driver_.SetEthernetPowerEnabled(false);
  }

  if (ethernet_.driver_initialized.load() && ethernet_.running.load()) {
    return true;
  }

  bool expected = false;
  if (!ethernet_.init_task_running.compare_exchange_strong(expected, true)) {
    return true;
  }

  ethernet_.start_failed.store(false);
  ethernet_.last_error.store(ESP_OK);
  const BaseType_t result = xTaskCreate(EthernetInitTaskEntry, "ethernet",
      kEthernetInitTaskStackBytes, this, kEthernetInitTaskPriority, nullptr);
  if (result != pdPASS) {
    SetEthernetFailure(ESP_ERR_NO_MEM);
    driver_.SetEthernetPowerEnabled(false);
    return false;
  }
  return true;
}

bool TDisplayP4Device::ReadEthernetStatus(EthernetStatus* status) {
  if (status == nullptr) {
    return false;
  }

  status->init_task_running = ethernet_.init_task_running.load();
  status->driver_initialized = ethernet_.driver_initialized.load();
  status->running = ethernet_.running.load();
  status->link_up = ethernet_.link_up.load();
  status->got_ip = ethernet_.got_ip.load();
  status->start_failed = ethernet_.start_failed.load();
  status->port_count = ethernet_.port_count.load();
  status->last_error = ethernet_.last_error.load();
  status->mac_address = ethernet_.mac_address.load();
  status->ip_address = ethernet_.ip_address.load();
  status->netmask = ethernet_.netmask.load();
  status->gateway = ethernet_.gateway.load();
  return true;
}

void TDisplayP4Device::EthernetInitTaskEntry(void* context) {
  auto* self = static_cast<TDisplayP4Device*>(context);
  if (self != nullptr) {
    self->RunEthernetInitTask();
  }
  vTaskDelete(nullptr);
}

void TDisplayP4Device::RunEthernetInitTask() {
  if (ethernet_.stop_requested.load()) {
    ethernet_.init_task_running.store(false);
    SetEthernetEnabled(false);
    return;
  }

  int result = ESP_OK;
  if (!driver_.SetEthernetPowerEnabled(true)) {
    result = ESP_FAIL;
  } else if (ethernet_.stop_requested.load()) {
    driver_.SetEthernetPowerEnabled(false);
    ethernet_.init_task_running.store(false);
    SetEthernetEnabled(false);
    return;
  } else {
    result = InitializeEthernetStack();
  }
  if (result != ESP_OK) {
    SetEthernetFailure(result);
    driver_.SetEthernetPowerEnabled(false);
    LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
        "Ethernet init failed: %s (%#X)\n",
        esp_err_to_name(static_cast<esp_err_t>(result)),
        static_cast<unsigned>(result));
  }
  ethernet_.init_task_running.store(false);
  if (ethernet_.stop_requested.load()) {
    SetEthernetEnabled(false);
  }
}

int TDisplayP4Device::InitializeEthernetStack() {
  if (ethernet_.handle != nullptr) {
    const esp_err_t start_result =
        esp_eth_start(reinterpret_cast<esp_eth_handle_t>(ethernet_.handle));
    if (start_result != ESP_OK && start_result != ESP_ERR_INVALID_STATE) {
      return start_result;
    }
    ethernet_.driver_initialized.store(true);
    ethernet_.running.store(true);
    ethernet_.start_failed.store(false);
    ethernet_.last_error.store(ESP_OK);
    return ESP_OK;
  }

  esp_err_t result = esp_netif_init();
  if (result != ESP_OK && result != ESP_ERR_INVALID_STATE) {
    return result;
  }

  result = esp_event_loop_create_default();
  if (result != ESP_OK && result != ESP_ERR_INVALID_STATE) {
    return result;
  }

  eth_mac_config_t mac_config = ETH_MAC_DEFAULT_CONFIG();
  eth_phy_config_t phy_config = ETH_PHY_DEFAULT_CONFIG();
  phy_config.phy_addr = device::ip101::kPhyAddress;
  phy_config.reset_gpio_num = gpio::ip101::kPhyRst;

  eth_esp32_emac_config_t emac_config = {};
  emac_config.smi_gpio.mdc_num = gpio::ip101::kRmiiMdc;
  emac_config.smi_gpio.mdio_num = gpio::ip101::kRmiiMdio;
  emac_config.interface = EMAC_DATA_INTERFACE_RMII;
  emac_config.clock_config.rmii.clock_mode = EMAC_CLK_EXT_IN;
  emac_config.clock_config.rmii.clock_gpio =
      static_cast<emac_rmii_clock_gpio_t>(gpio::ip101::kRmiiRefClk);
  emac_config.dma_burst_len = ETH_DMA_BURST_LEN_32;
  emac_config.intr_priority = 0;
#if SOC_EMAC_USE_MULTI_IO_MUX || SOC_EMAC_MII_USE_GPIO_MATRIX
  emac_config.emac_dataif_gpio.rmii.tx_en_num = gpio::ip101::kRmiiTxEn;
  emac_config.emac_dataif_gpio.rmii.txd0_num = gpio::ip101::kRmiiTxd0;
  emac_config.emac_dataif_gpio.rmii.txd1_num = gpio::ip101::kRmiiTxd1;
  emac_config.emac_dataif_gpio.rmii.crs_dv_num = gpio::ip101::kRmiiCrsDv;
  emac_config.emac_dataif_gpio.rmii.rxd0_num = gpio::ip101::kRmiiRxd0;
  emac_config.emac_dataif_gpio.rmii.rxd1_num = gpio::ip101::kRmiiRxd1;
#endif
#if !SOC_EMAC_RMII_CLK_OUT_INTERNAL_LOOPBACK
  emac_config.clock_config_out_in.rmii.clock_mode = EMAC_CLK_EXT_IN;
  emac_config.clock_config_out_in.rmii.clock_gpio =
      static_cast<emac_rmii_clock_gpio_t>(gpio::ip101::kRmiiClkOut);
#endif
  emac_config.mdc_freq_hz = 0;

  esp_eth_mac_t* mac = esp_eth_mac_new_esp32(&emac_config, &mac_config);
  if (mac == nullptr) {
    return ESP_ERR_NO_MEM;
  }

  esp_eth_phy_t* phy = esp_eth_phy_new_ip101(&phy_config);
  if (phy == nullptr) {
    mac->del(mac);
    return ESP_ERR_NO_MEM;
  }

  esp_eth_handle_t handle = nullptr;
  esp_eth_config_t config = ETH_DEFAULT_CONFIG(mac, phy);
  result = esp_eth_driver_install(&config, &handle);
  if (result != ESP_OK) {
    mac->del(mac);
    phy->del(phy);
    return result;
  }

  esp_netif_inherent_config_t inherent_config = *ESP_NETIF_BASE_DEFAULT_ETH;
  esp_netif_config_t netif_config = {
      .base = &inherent_config,
      .driver = nullptr,
      .stack = ESP_NETIF_NETSTACK_DEFAULT_ETH,
  };
  esp_netif_t* netif = esp_netif_new(&netif_config);
  if (netif == nullptr) {
    return ESP_ERR_NO_MEM;
  }

  auto glue = esp_eth_new_netif_glue(handle);
  if (glue == nullptr) {
    return ESP_ERR_NO_MEM;
  }

  result = esp_netif_attach(netif, glue);
  if (result != ESP_OK) {
    return result;
  }

  result = esp_event_handler_register(
      ETH_EVENT, ESP_EVENT_ANY_ID, EthernetEventHandler, this);
  if (result != ESP_OK) {
    return result;
  }

  result = esp_event_handler_register(
      IP_EVENT, IP_EVENT_ETH_GOT_IP, EthernetGotIpEventHandler, this);
  if (result != ESP_OK) {
    return result;
  }

  ethernet_.handle = handle;
  ethernet_.port_count.store(1);

  result = esp_eth_start(handle);
  if (result != ESP_OK && result != ESP_ERR_INVALID_STATE) {
    return result;
  }

  ethernet_.driver_initialized.store(true);
  ethernet_.running.store(true);
  ethernet_.start_failed.store(false);
  ethernet_.last_error.store(ESP_OK);
  return ESP_OK;
}

void TDisplayP4Device::SetEthernetFailure(int error) {
  ethernet_.init_task_running.store(false);
  ethernet_.driver_initialized.store(ethernet_.handle != nullptr);
  ethernet_.running.store(false);
  ethernet_.link_up.store(false);
  ethernet_.got_ip.store(false);
  ethernet_.start_failed.store(true);
  ethernet_.last_error.store(error);
  ethernet_.ip_address.store(0);
  ethernet_.netmask.store(0);
  ethernet_.gateway.store(0);
}

void TDisplayP4Device::EthernetEventHandler(
    void* arg, const char* event_base, int32_t event_id, void* event_data) {
  (void)event_base;
  auto* self = static_cast<TDisplayP4Device*>(arg);
  if (self == nullptr) {
    return;
  }

  switch (event_id) {
    case ETHERNET_EVENT_CONNECTED: {
      self->ethernet_.running.store(true);
      self->ethernet_.link_up.store(true);
      self->ethernet_.got_ip.store(false);
      self->ethernet_.ip_address.store(0);
      self->ethernet_.netmask.store(0);
      self->ethernet_.gateway.store(0);

      if (event_data != nullptr) {
        esp_eth_handle_t handle = *static_cast<esp_eth_handle_t*>(event_data);
        uint8_t mac_address[6] = {};
        if (esp_eth_ioctl(handle, ETH_CMD_G_MAC_ADDR, mac_address) == ESP_OK) {
          self->ethernet_.mac_address.store(
              wifi_utils::PackMacAddress(mac_address));
        }
      }
      break;
    }
    case ETHERNET_EVENT_DISCONNECTED:
      self->ethernet_.link_up.store(false);
      self->ethernet_.got_ip.store(false);
      self->ethernet_.ip_address.store(0);
      self->ethernet_.netmask.store(0);
      self->ethernet_.gateway.store(0);
      break;
    case ETHERNET_EVENT_START:
      self->ethernet_.running.store(true);
      self->ethernet_.start_failed.store(false);
      self->ethernet_.last_error.store(ESP_OK);
      break;
    case ETHERNET_EVENT_STOP:
      self->ethernet_.running.store(false);
      self->ethernet_.link_up.store(false);
      self->ethernet_.got_ip.store(false);
      self->ethernet_.ip_address.store(0);
      self->ethernet_.netmask.store(0);
      self->ethernet_.gateway.store(0);
      break;
    default:
      break;
  }
}

void TDisplayP4Device::EthernetGotIpEventHandler(
    void* arg, const char* event_base, int32_t event_id, void* event_data) {
  (void)event_base;
  (void)event_id;
  auto* self = static_cast<TDisplayP4Device*>(arg);
  auto* event = static_cast<ip_event_got_ip_t*>(event_data);
  if (self == nullptr || event == nullptr) {
    return;
  }

  self->ethernet_.link_up.store(true);
  self->ethernet_.got_ip.store(true);
  self->ethernet_.ip_address.store(event->ip_info.ip.addr);
  self->ethernet_.netmask.store(event->ip_info.netmask.addr);
  self->ethernet_.gateway.store(event->ip_info.gw.addr);
}

}  // namespace lilygo_box::hal
