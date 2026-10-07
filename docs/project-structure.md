# 项目结构

LilygoBox 设备端基于 ESP-IDF 和 LVGL，提供触控桌面、设置、硬件功能页面以及与 LilygoBox 桌面应用的局域网连接。

## 支持的硬件

| 设备 | 主控 | 无线协处理器 | HAL 目录 |
| --- | --- | --- | --- |
| T-Display-P4 V1 | ESP32-P4 | ESP32-C6 | `main/hal/device/t_display_p4`，版本实现位于 `v1` |
| T-Display-P4 V2 | ESP32-P4 | ESP32-C5 | `main/hal/device/t_display_p4`，版本实现位于 `v2` |
| T-Display-P4-Air | ESP32-P4 | ESP32-C5 | `main/hal/device/t_display_p4_air` |

硬件选择由 Kconfig 决定。驱动库提供的其他设备选项不等于本应用已适配这些设备。

## 目录职责

| 路径 | 职责 |
| --- | --- |
| `main/app` | 应用生命周期、网络监控、Wi-Fi 管理、应用连接及固件更新 |
| `main/app/storage` | 配置缓存、NVS 和 TLV 编解码、文件持久化 |
| `main/app/diagnostics` | 设备诊断 |
| `main/hal/providers` | 硬件能力接口，隔离页面与具体设备实现 |
| `main/hal/device` | 各设备的硬件适配 |
| `main/hal` | LVGL 显示适配、内存策略及其他硬件公共实现 |
| `main/ui/views` | 桌面、应用和设置页面 |
| `main/ui/widgets` | 公共控件及提示框模板 |
| `main/ui/theme`、`main/ui/animation` | 主题与动画 |
| `main/ui/resources` | 生成后的图片、文本字体和图标字体 |
| `main/audio` | 音频相关实现 |
| `main/base` | 日志等基础设施 |
| `assets`、`tools` | 原始素材和资源生成工具 |
| `libraries` | 本地组件和硬件驱动依赖 |

## 功能入口与开发约定

应用目录由 [app_catalog.cpp](../main/app/app_catalog.cpp) 管理，包括硬件测试、Radio、音乐、文件、指南针，以及相机和设置入口。页面创建与切换由 [ui_manager.cpp](../main/ui/ui_manager.cpp) 和 [app_view_factory.cpp](../main/ui/app_view_factory.cpp) 管理。具体可用能力以页面及对应硬件实现为准。

设置页面位于 `main/ui/views/settings`，包括 WLAN、显示、声音、输入设备和应用连接等。蓝牙、个人热点等保留页面不作为当前已开放功能说明；桌面投屏和 AI 数据业务也不属于当前连接协议的既有能力。

页面通过 HAL 接口访问硬件，通过存储模块修改设置。耗时网络请求放在后台任务中，LVGL 页面读取状态缓存，避免在界面线程执行同步 RPC。离开设置页面不应停止后台网络服务。

应用日志使用 `main/base` 的日志接口，遵循现有 `LogMessage` 格式。公共函数在头文件中描述职责、参数和返回值，实现文件仅补充必要的算法或设计原因说明。
