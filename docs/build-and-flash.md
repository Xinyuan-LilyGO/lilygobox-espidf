# 编译与烧录

## 环境和依赖

需要 ESP-IDF v5.5.4 或更高版本、ESP32-P4 工具链及串口环境；升级 ESP-IDF 后仍需验证对应硬件。组件依赖见 [main/idf_component.yml](../main/idf_component.yml)，当前使用 LVGL 9.5.0。本地组件位于 `libraries`，下载的组件由 Component Manager 管理。

```bash
git clone --recursive https://github.com/Xinyuan-LilyGO/lilygobox-espidf.git
cd lilygobox-espidf
git submodule update --init --recursive
```

## 选择硬件

首次配置时，在 ESP-IDF 终端执行：

```bash
idf.py set-target esp32p4
idf.py menuconfig
```

`set-target` 会重新配置目标，不需要在每次编译前执行。已有 `sdkconfig` 时，其配置优先于默认文件。

在 `lilygo_device_driver configuration` 中选择设备及硬件版本，并检查相机、屏幕格式等选项。无线协处理器必须匹配硬件：

| 设备 | ESP-Hosted 从机目标 |
| --- | --- |
| T-Display-P4 V1 | ESP32-C6 |
| T-Display-P4 V2 | ESP32-C5 |
| T-Display-P4-Air | ESP32-C5 |

仓库默认选择 T-Display-P4，版本默认值为 V1。使用 V2 或 Air 时应显式检查配置；无线协处理器也需要运行匹配的 ESP-Hosted 从机固件。`LilygoBox Configuration` 提供应用日志等级、发布通道等选项。

## 构建和运行

```bash
idf.py build
idf.py -p COMx flash monitor
```

`COMx` 是串口占位符，请替换为开发板端口。其他系统使用对应的串口设备名称。构建结果位于 `build`，分区布局见 [partitions_esp32p4.csv](../partitions_esp32p4.csv)。

普通升级应保留原有配置分区。更改分区布局或擦除整片 Flash 可能删除设置和授权信息，详见[本地存储](local-storage.md)。

## 常见检查

- 本地组件目录不存在：先初始化子模块，核对组件清单的相对路径，不要填写开发者电脑上的绝对路径。
- 无线初始化失败：检查硬件版本、C5/C6 从机目标及从机固件是否匹配。
- 修改默认配置后未生效：检查已有 `sdkconfig` 中的实际值。
- WLAN 页面性能回退：检查 [LVGL 内存适配](wlan-performance.md) 是否参与构建和链接。
