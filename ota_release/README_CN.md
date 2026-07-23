<h1 align="center">LilygoBox OTA Release 工具</h1>

## **[English](./README.md) | Chinese**

[![Release](https://img.shields.io/github/v/release/Xinyuan-LilyGO/lilygobox-espidf?style=flat-square)](https://github.com/Xinyuan-LilyGO/lilygobox-espidf/releases)
[![Python](https://img.shields.io/badge/Python-3.9%2B-3776ab?style=flat-square)](https://www.python.org/)

`ota_release` 用于为包含多个设备版本和固件组件的 LilygoBox 设备生成经过校验的 GitHub Release 文件，支持同时更新或单独更新 Main 与 Wireless 固件。

## 目录

- [环境要求](#环境要求)
- [目录结构](#目录结构)
- [Manifest 字段](#manifest-字段)
- [使用方法](#使用方法)
- [发布到 GitHub Releases](#发布到-github-releases)
- [添加其他设备](#添加其他设备)

## 环境要求

- Python 3.9 或更高版本
- 不需要安装第三方 Python 包
- 放入 `input/lilygobox-espidf.bin` 的 ESP32-P4 应用镜像
- 镜像内项目名为 `network_adapter` 的 ESP32-C6 固件

ESP32-P4 OTA 不能使用 merged 或 factory 镜像。ESP32-C6 固件可以位于任意目录。

## 目录结构

先把 P4 和 C6 固件放入 `input`，再运行脚本。脚本会自动创建输出目录和待上传文件：

```text
ota_release/
├─ devices/
│  └─ t-display-p4.json
├─ input/                          用户放入并被 Git 忽略
│  ├─ lilygobox-espidf.bin
│  └─ network_adapter.bin
├─ output/                         由脚本生成并被 Git 忽略
│  └─ t-display-p4/
│     └─ v1.0.1/
├─ .gitignore
├─ generate_manifest.py
├─ README.md
└─ README_CN.md
```

生成结果保存在 `output/<device_id>/<Release tag>/`。目录中的 `.gitignore` 会阻止这些临时发布文件被提交到仓库。

## Manifest 字段

| 字段 | 用途 |
| --- | --- |
| `schema_version` | 设备能够识别的 Manifest 结构版本，同时用于生成文件名 |
| `device_id` | 目标设备系列标识 |
| `device_versions` | 按设备版本组织的固件对象，例如 `v1.0`、`v2.0` |
| `main` | 当前设备版本使用的主固件 |
| `wireless` | 当前设备版本使用的无线固件 |
| `release` | GitHub Release tag，例如 `v1.0.1` |
| `channel` | 发布频道：`stable`、`beta` 或 `dev`，默认 `stable` |
| `release_time` | Python 自动生成的发布时间，包含时区并精确到分钟 |
| `version` | 对应组件的三段式固件版本 |
| `urls` | 按尝试顺序排列的组件 HTTPS 下载地址，支持 1 至 4 个 |
| `size_bytes` | Python 从二进制文件自动读取的精确大小 |
| `sha256` | Python 在不修改文件的情况下计算的 SHA-256 |
| `whats_new` | 最多三条简短更新说明 |

生成器不再写死组件名称。每个设备版本需要使用的组件由
`devices/t-display-p4.json` 独立声明，并配置可信的 BIN 信息：

```json
"device_versions": {
  "v1.0": {
    "main": {
      "chip": "esp32p4",
      "project_name": "lilygobox-espidf"
    },
    "wireless": {
      "chip": "esp32c6",
      "project_name": "network_adapter"
    }
  }
}
```

复制本次更新的 BIN 前，生成器会扫描独立或合并 ESP 镜像，要求 BIN 内嵌的
芯片、项目名和版本分别与该配置及 `--component` 版本完全一致。`chip` 和
`project_name` 只属于生成器的可信配置，不会写入公开 Manifest。

不同设备版本可以使用不同的组件集合。在这里增加组件后，生成器会负责生成
和校验对应信息，但设备端程序还必须实现该组件的下载和安装逻辑。
组件名称表示固件职责而不是芯片型号，因此 V2 即使把 ESP32-C6 更换为
ESP32-C5，也可以继续使用 `wireless` 这个职责名称。

每个组件都使用 `urls` 数组。设备从第一个地址开始下载，遇到连接失败、
限流或服务端错误时依次尝试后续地址：

```json
"urls": [
  "https://github.com/Xinyuan-LilyGO/lilygobox-espidf/releases/download/v1.0.0/lilygobox-t-display-p4-v1.0-main.bin",
  "https://gh-proxy.com/https://github.com/Xinyuan-LilyGO/lilygobox-espidf/releases/download/v1.0.0/lilygobox-t-display-p4-v1.0-main.bin"
]
```

同一份 Manifest 可以包含多个设备版本。设备检测自身设备版本后，只读取
对应节点：

```json
"device_versions": {
  "v1.0": {
    "main": { "...": "..." },
    "wireless": { "...": "..." }
  },
  "v2.0": {
    "main": { "...": "..." },
    "wireless": { "...": "..." }
  }
}
```

`devices/t-display-p4.json` 中的 `download_url_templates` 是这些完整地址的
生成模板。需要添加下载源时，把新的 HTTPS 模板追加到该数组末尾即可；
顺序就是设备的尝试优先级，最多配置四个。生成新 manifest 时，脚本也会
为从上一版继承的组件重新生成完整下载源列表。

当前设备固定读取 `lilygobox-t-display-p4-ota-manifest-v1.json`。如果以后
启用不兼容的 `schema_version: 2`，必须新建 `-v2.json`，并继续保留
V1 清单作为旧固件进入过渡固件的更新入口。
该文件名由设备配置中的 `device_id` 和 `schema_version` 自动生成，
不需要单独维护资产文件名。

## 使用方法

下面的命令都需要在 `lilygobox-espidf/ota_release` 目录中运行。从项目目录进入：

```bat
cd .\ota_release
```

示例全部使用单行命令，可以直接粘贴到 CMD 或 PowerShell 中运行。
不传 `--channel` 时默认生成稳定版；测试版本可显式使用 `--channel beta`
或 `--channel dev`。

### 首次发布

首次发布没有上一版组件信息，所以必须同时提供当前设备版本的 Main 和
Wireless 固件：

```bat
python .\generate_manifest.py --release 1.0.0 --component "v1.0/main=1.0.0=.\input\lilygobox-espidf.bin" --component "v1.0/wireless=2.12.3=.\input\network_adapter.bin" --note "Initial LilygoBox firmware release"
```

### 仅更新 Main

Wireless 没有变化时，只需要提供新的 Main 固件和上一版 manifest：

```bat
python .\generate_manifest.py --release 1.0.1 --component "v1.0/main=1.0.1=.\input\lilygobox-espidf.bin" --previous-manifest ".\output\t-display-p4\v1.0.0\lilygobox-t-display-p4-ota-manifest-v1.json" --note "Updated main firmware"
```

脚本会继承 Wireless 的版本、历史 Release tag、大小和 SHA-256，并按当前
设备配置重新生成它的下载源列表。本次只上传脚本列出的 manifest 与 Main 固件。

### 仅更新 Wireless

```bat
python .\generate_manifest.py --release 1.0.2 --component "v1.0/wireless=2.12.4=.\input\network_adapter.bin" --previous-manifest ".\output\t-display-p4\v1.0.1\lilygobox-t-display-p4-ota-manifest-v1.json" --note "Updated wireless firmware"
```

脚本会继承 Main 的信息。本次只上传脚本列出的 manifest 与 Wireless 固件。

### 添加新的设备版本

先在设备配置的 `device_versions` 中增加 `v2.0` 及其组件配置，再同时
提供新设备版本要求的所有固件和上一版 Manifest：

```bat
python .\generate_manifest.py --release 2.0.0 --component "v2.0/main=1.0.0=.\input\lilygobox-espidf-v2.bin" --component "v2.0/wireless=1.0.0=.\input\network-adapter-v2.bin" --previous-manifest ".\output\t-display-p4\v1.0.2\lilygobox-t-display-p4-ota-manifest-v1.json" --note "Added T-Display-P4 V2.0 support"
```

脚本会继承 V1.0 的两个组件，并在同一份 Manifest 中增加 V2.0。

如果本地已经没有上一版输出，可以从上一版 GitHub Release 下载 manifest，再将其路径传给 `--previous-manifest`。

### 重新生成尚未发布的版本

目标输出目录非空时，脚本默认停止。确认需要重新生成同一个尚未发布的版本时添加：

```bat
--force
```

使用上一版 manifest 时，新 Release 版本和本次发生变化的组件版本都必须高于上一版。

## 发布到 GitHub Releases

例如仅更新 V1.0 Main 的 `v1.0.1` 输出目录为：

```text
ota_release/output/t-display-p4/v1.0.1/
├─ lilygobox-t-display-p4-ota-manifest-v1.json
└─ lilygobox-t-display-p4-v1.0-main.bin
```

1. 在 [lilygobox-espidf Releases 页面](https://github.com/Xinyuan-LilyGO/lilygobox-espidf/releases)创建 Draft Release。
2. 使用与输出目录一致的 tag，例如 `v1.0.1`。
3. 只上传脚本在终端列出的文件。
4. 检查 manifest、组件版本和更新说明。
5. 所有需要的文件上传完成后，再设为 Latest 并正式发布。

不要先发布 manifest，之后再补传本次更新的 `.bin`。设备可能会立即发现这个不完整的 Release。

## 添加其他设备

复制 `devices/t-display-p4.json` 并修改：

- `device_id`
- `device_versions`
- 各组件的 `chip` 和 `project_name` 可信配置
- `schema_version`
- GitHub `repository`
- `download_url_templates` 下载源模板及其优先级

通过 `--config` 选择新的设备配置。设备端程序也必须配置对应的 manifest URL、
`device_id`，并且能够检测 `device_versions` 中对应的设备版本；仅添加
生成器配置不会让新板卡自动获得 OTA 功能。固件资产文件名由生成器根据
设备、设备版本以及配置中声明的组件名称自动生成。
