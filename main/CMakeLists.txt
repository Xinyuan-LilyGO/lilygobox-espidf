set(src_dirs
    "."
    "app"
    "app/diagnostics"
    "app/storage"
    "audio"
    "base"
    "hal"
    "hal/ppa"
    "hal/providers"
    "ui"
    "ui/animation"
    "ui/input"
    "ui/resources/fonts/icons"
    "ui/resources/fonts/text"
    "ui/resources/images"
    "ui/theme"
    "ui/views"
    "ui/views/settings"
    "ui/widgets"
    "ui/widgets/prompt")

if(CONFIG_LILYGO_DEVICE_DRIVER_T_DISPLAY_P4)
    list(APPEND src_dirs
        "hal/device/common"
        "hal/device/t_display_p4"
        "hal/usb")
    if(CONFIG_LILYGO_DEVICE_DRIVER_DEVICE_VERSION_V2)
        list(APPEND src_dirs "hal/device/t_display_p4/v2" "hal/usb/ethernet")
        list(APPEND ethernet_requires iot_eth iot_usbh_ecm iot_usbh_cdc)
    else()
        list(APPEND src_dirs "hal/device/t_display_p4/v1")
    endif()
elseif(CONFIG_LILYGO_DEVICE_DRIVER_T_DISPLAY_P4_AIR)
    list(APPEND src_dirs
        "hal/device/common"
        "hal/device/t_display_p4_air"
        "hal/usb")
endif()

idf_component_register(
    SRC_DIRS ${src_dirs}
    EXCLUDE_SRCS ${exclude_srcs}
    INCLUDE_DIRS "."
    REQUIRES
        esp_driver_ppa
        esp_lcd
        esp_timer
        esp_video
        lvgl
        lilygo_device_driver
    PRIV_REQUIRES
        ${ethernet_requires}
        app_update
        bootloader_support
        esp_app_format
        esp_common
        esp_audio_codec
        esp_driver_rmt
        esp_eth
        esp_event
        esp_hosted
        esp_http_client
        esp_https_ota
        fatfs
        esp_hw_support
        esp_netif
        nvs_flash
        esp_system
        esp_wifi
        freertos
        heap
        littlefs
        lwip
        mbedtls
        json
        spi_flash
        usb
        usb_host_msc)

if(CONFIG_LILYGO_DEVICE_DRIVER_T_DISPLAY_P4 AND
   CONFIG_LILYGO_DEVICE_DRIVER_DEVICE_VERSION_V2)
    # 板载 Hub 端口筛选、已有设备补扫、RTL8152B 帧尾短包和软件 MAC 接收过滤。
    foreach(symbol usb_host_client_register usb_host_client_handle_events
                   usb_host_transfer_submit usbh_cdc_send_custom_request)
        target_link_options(${COMPONENT_LIB} INTERFACE
            "-Wl,--wrap=${symbol}" "-Wl,--undefined=__wrap_${symbol}")
    endforeach()
endif()
