<h1 align="center">LilygoBox OTA Release 工具</h1>

## **[English](./README.md) | Chinese**

[![Release](https://img.shields.io/github/v/release/Xinyuan-LilyGO/lilygobox-espidf?style=flat-square)](https://github.com/Xinyuan-LilyGO/lilygobox-espidf/releases)
[![Python](https://img.shields.io/badge/Python-3.9%2B-3776ab?style=flat-square)](https://www.python.org/)

`ota_release` 用于为包含多个固件组件的 LilygoBox 设备生成经过校验的 GitHub Release 文件，支持 ESP32-P4 与 ESP32-C6 同时更新、仅更新 P4 或仅更新 C6。

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
| `manifest_version` | 设备能够识别的 manifest 格式版本 |
| `device_id` | 目标硬件标识 |
| `release` | GitHub Release tag，例如 `v1.0.1` |
| `channel` | 发布频道：`stable`、`beta` 或 `dev`，默认 `stable` |
| `release_time` | Python 自动生成的发布时间，包含时区并精确到分钟 |
| `version` | 对应组件的三段式固件版本 |
| `url` | 组件资产所在的准确 GitHub Release 地址 |
| `size_bytes` | Python 从二进制文件自动读取的精确大小 |
| `sha256` | Python 在不修改文件的情况下计算的 SHA-256 |
| `whats_new` | 最多三条简短更新说明 |

## 使用方法

下面的命令都需要在 `lilygobox-espidf/ota_release` 目录中运行。从项目目录进入：

```bat
cd .\ota_release
```

示例全部使用单行命令，可以直接粘贴到 CMD 或 PowerShell 中运行。
不传 `--channel` 时默认生成稳定版；测试版本可显式使用 `--channel beta`
或 `--channel dev`。

### 首次发布

首次发布没有上一版组件信息，所以必须同时提供 P4 和 C6 固件：

```bat
python .\generate_manifest.py --release 1.0.0 --component "esp32p4=1.0.0=.\input\lilygobox-espidf.bin" --component "esp32c6=2.12.3=.\input\network_adapter.bin" --note "Initial LilygoBox firmware release"
```

### 仅更新 P4

C6 没有变化时，只需要提供新的 P4 固件和上一版 manifest：

```bat
python .\generate_manifest.py --release 1.0.1 --component "esp32p4=1.0.1=.\input\lilygobox-espidf.bin" --previous-manifest ".\output\t-display-p4\v1.0.0\lilygobox-t-display-p4-ota-manifest.json" --note "Updated main firmware"
```

脚本会继承 C6 的版本、历史 Release 地址、大小和 SHA-256。本次只上传脚本列出的 manifest 与 P4 固件。

### 仅更新 C6

```bat
python .\generate_manifest.py --release 1.0.2 --component "esp32c6=2.12.4=.\input\network_adapter.bin" --previous-manifest ".\output\t-display-p4\v1.0.1\lilygobox-t-display-p4-ota-manifest.json" --note "Updated wireless firmware"
```

脚本会继承 P4 的信息。本次只上传脚本列出的 manifest 与 C6 固件。

如果本地已经没有上一版输出，可以从上一版 GitHub Release 下载 manifest，再将其路径传给 `--previous-manifest`。

### 重新生成尚未发布的版本

目标输出目录非空时，脚本默认停止。确认需要重新生成同一个尚未发布的版本时添加：

```bat
--force
```

使用上一版 manifest 时，新 Release 版本和本次发生变化的组件版本都必须高于上一版。

## 发布到 GitHub Releases

例如仅更新 P4 的 `v1.0.1` 输出目录为：

```text
ota_release/output/t-display-p4/v1.0.1/
├─ lilygobox-t-display-p4-ota-manifest.json
└─ lilygobox-t-display-p4-esp32p4.bin
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
- GitHub `repository`
- Manifest 资产文件名
- 每个固件组件的固定资产文件名

通过 `--config` 选择新的设备配置。设备端程序也必须配置对应的 manifest URL 和 `device_id`；仅添加生成器配置不会让新板卡自动获得 OTA 功能。
