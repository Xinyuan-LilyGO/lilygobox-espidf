set(src_dirs
    "."
    "app"
    "app/storage"
    "base"
    "hal"
    "hal/providers"
    "ui"
    "ui/animation"
    "ui/font"
    "ui/icon"
    "ui/input"
    "ui/views"
    "ui/views/settings"
    "ui/widgets"
    "ui/widgets/prompt")

if(CONFIG_LILYGO_DEVICE_DRIVER_T_DISPLAY_P4)
    list(APPEND src_dirs "hal/device/t_display_p4")
endif()

idf_component_register(
    SRC_DIRS ${src_dirs}
    INCLUDE_DIRS "."
    REQUIRES
        esp_lcd
        esp_timer
        lvgl
        lilygo_device_driver
    PRIV_REQUIRES
        app_update
        bootloader_support
        esp_app_format
        esp_common
        esp_eth
        esp_event
        esp_hw_support
        esp_netif
        nvs_flash
        esp_system
        esp_wifi
        freertos
        heap
        lwip
        spi_flash)
