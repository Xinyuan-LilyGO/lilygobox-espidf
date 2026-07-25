<h1 align="center">LilygoBox OTA Release 工具</h1>

## **[English](./README.md) | Chinese**

`ota_release` 生成 LilygoBox OTA Manifest 和需要上传到 GitHub Releases
的固件资产。

## 命名规则

- JSON 属性严格使用 Google JSON 风格的 `lowerCamelCase`。
- 集合字段使用复数名，例如 `targets`、`files`、`downloadUrls`。
- 动态映射键使用稳定的小写标识，例如
  `device-v1.0-esp32p4-rev1.0`。
- 时间字段使用 `...Time`，因此发布时间使用 `publishTime`。
- 数量字段直接使用单位后缀，因此文件大小使用 `sizeBytes`。
- `name` 只保留给完整资源名；设备标识使用 `deviceId`。
- `deviceId` 标识设备型号；`deviceVersion` 标识该型号的 OTA 硬件兼容
  版本，不等同于 PCB 印刷版本；固件版本仍使用 `release.version`。
- Python 遵循 PEP 8，C++ 遵循 Google C++ Style Guide。
- JSON 内的版本不带 `v`；Git tag 和固件文件名使用 `v` 前缀。
- 固件版本使用受约束的 SemVer：Alpha 为 `X.Y.Z-alpha.N`，
  Beta 为 `X.Y.Z-beta.N`，Stable 为 `X.Y.Z`。
- 时间使用 UTC、秒精度的 RFC 3339，例如
  `2026-07-24T10:00:00Z`。

命名与字段语义依据 Google
[AIP-122](https://google.aip.dev/122)、
[AIP-126](https://google.aip.dev/126)、
[AIP-140](https://google.aip.dev/140)、
[AIP-141](https://google.aip.dev/141) 和
[AIP-142](https://google.aip.dev/142)。

## 兼容模型

`manifestVersion` 只表示 JSON 协议格式。`deviceVersion`、芯片型号和芯片完整
修订号属于 `targets.compatibility`，新增这些目标不需要修改
`manifestVersion`。

`deviceVersion` 表示 OTA 硬件兼容版本，而不是必须跟随每次 PCB 修订递增的
版本号。如果 PCB v1.0、v1.1 和 v1.2 只修复硬件问题，且引脚、外设、分区表、
驱动和固件 BIN 完全兼容，它们可以共同使用 `deviceVersion: "1.0"`。只有硬件
变化导致需要不同固件时，才应创建新的 `deviceVersion`。如果必须区分不同硬件
兼容版本，设备必须从 eFuse、工厂 NVS、EEPROM、GPIO/ADC 识别等不会随 OTA
固件改变的来源读取该版本；当前 T-Display-P4 安装器使用编译期
`kDeviceModelInfo.version`，无法在同一固件 BIN 中区分多个 PCB 版本。

各版本字段含义不同：

| 字段 | 示例 | 含义 |
| --- | --- | --- |
| `manifestVersion` | `1.0` | Manifest JSON 协议版本 |
| `deviceVersion` | `1.0` | OTA 硬件兼容版本 |
| `chips.main.revision` | `1.1` | 主芯片的 silicon revision |
| `release.version` | `1.2.0` | LilygoBox 软件发布版本 |
| `files.*.version` | `2.12.3` | 对应组件的固件版本 |

设备只接受唯一匹配的目标：

1. `release.publisherId` 和 `release.deviceId` 必须匹配当前产品。
2. Manifest 的 `channel` 必须匹配固件编译时选择的频道。
3. `deviceVersion` 必须与设备版本完全相同。
4. 主芯片和无线芯片的 `model`、`revision` 必须完全相同。
5. `components` 必须同时提供设备安装器支持的 `main` 和 `wireless`。

例如同一型号芯片的 silicon revision `1.0` 与 `1.1` 是两个不同的芯片兼容
条件，与 `deviceVersion`、`release.version` 和 `manifestVersion` 无关。没有
匹配或出现多个匹配时，设备都会拒绝更新，避免把不兼容固件写入错误硬件。

### 无线协处理器 revision 限制

当前无线协处理器接口可以读取固件版本，但没有提供 silicon revision 的运行时
读取能力。因此安装器当前使用设备配置中固定的无线芯片 revision；Manifest 中的
`chips.wireless.revision` 表示硬件配置假设，不是设备运行时检测到的真实值。

在增加使用其他无线芯片型号或 silicon revision 的硬件批次前，必须先让无线
协处理器接口返回真实的芯片型号和 revision，再让 OTA target 使用这些值。读取
失败时应拒绝更新，不能回退到固定值。在完成该能力前，不应依赖
`chips.wireless.revision` 区分不同无线芯片 silicon revision 的固件。

## Manifest v1

格式由 [ota_manifest_v1.schema.json](./ota_manifest_v1.schema.json)
定义，生成器配置由
[ota_device_config_v1.schema.json](./ota_device_config_v1.schema.json)
定义。以下示例省略了第二个固件文件的详细内容：

```json
{
  "kind": "lilygobox#otaManifest",
  "manifestVersion": "1.0",
  "release": {
    "publisherId": "lilygo",
    "deviceId": "t-display-p4",
    "version": "1.0.0"
  },
  "channel": "stable",
  "publishTime": "2026-07-24T10:00:00Z",
  "targets": [
    {
      "compatibility": {
        "deviceVersion": "1.0",
        "chips": {
          "main": {
            "model": "esp32p4",
            "revision": "1.0"
          },
          "wireless": {
            "model": "esp32c6",
            "revision": "0.0"
          }
        }
      },
      "components": {
        "main": "device-v1.0-esp32p4-rev1.0",
        "wireless": "device-v1.0-esp32c6-rev0.0"
      }
    }
  ],
  "files": {
    "device-v1.0-esp32p4-rev1.0": {
      "chip": "esp32p4",
      "projectName": "lilygobox-espidf",
      "version": "1.0.0",
      "fileName": "lilygobox-t-display-p4-device-v1.0-esp32p4-rev1.0-v1.0.0.bin",
      "downloadUrls": [
        "https://github.com/Xinyuan-LilyGO/lilygobox-espidf/releases/download/v1.0.0/lilygobox-t-display-p4-device-v1.0-esp32p4-rev1.0-v1.0.0.bin"
      ],
      "sizeBytes": 4032256,
      "hashes": {
        "sha256": "<由生成器计算>"
      }
    }
  },
  "releaseNotes": [
    "首次发布 LilygoBox 固件"
  ]
}
```

`files` 是去重后的固件资产映射。多个硬件目标可以引用同一个文件，
所以共用的无线固件不需要重复上传。生成器会检查 BIN 内嵌的芯片 ID、
ESP-IDF 项目名、版本、完整镜像长度，以及芯片最小/最大完整修订号，
并计算文件大小与 SHA-256。设备端下载后还会再次验证长度、SHA-256
和应用镜像信息。

## 发布频道

固件通过 Kconfig 固定选择一个频道，发布阶段依次为 Alpha、Beta、
Stable：

- `alpha`：内部 Alpha 版。
- `beta`：公开测试版。
- `stable`：正式版，生产固件默认值。

Release 版本和主固件内嵌版本必须与频道对应：

```text
alpha  -> 1.1.0-alpha.1、1.1.0-alpha.2
beta   -> 1.1.0-beta.1、1.1.0-beta.2
stable -> 1.1.0
```

预发布序号必须使用无前导零的非负整数。无线固件保持自己的独立版本，
例如 `2.12.3`，不要求添加频道后缀。三个频道使用不同 Git tag 和主固件
文件名，因此可以同时发布而不会覆盖资产。

Kconfig 只在 `main/app/release_channel.h` 中转换为应用公共的
`ReleaseChannel` 枚举。其他 C++ 代码使用 `kReleaseChannel` 和
`ReleaseChannelName()`，不直接依赖
`CONFIG_LILYGO_BOX_RELEASE_CHANNEL_*` 宏。

每个频道有独立的上一版 Manifest 和版本序列，不能跨频道继承：

```text
ota-alpha  -> lilygobox-t-display-p4-ota-manifest-alpha-v1.json
ota-beta   -> lilygobox-t-display-p4-ota-manifest-beta-v1.json
ota-stable -> lilygobox-t-display-p4-ota-manifest-stable-v1.json
```

左侧是固定的 GitHub 频道指针 Release tag。设备只从对应 tag 下载
Manifest，不依赖 GitHub `latest`，所以 Alpha 和 Beta 也能可靠发现。

## 环境与目录

- Python 3.9 或更高版本。
- 不需要第三方 Python 包。
- 输入必须是从偏移 `0` 开始、不含额外分区数据的 ESP 应用 BIN。
- merged、factory 或多分区镜像不能作为 OTA 输入。

```text
ota_release/
├─ devices/
│  └─ t-display-p4.json
├─ input/                           用户提供，Git 忽略
├─ output/                          自动生成，Git 忽略
├─ generate_manifest.py
├─ ota_device_config_v1.schema.json
├─ ota_manifest_v1.schema.json
├─ README.md
└─ README_CN.md
```

输出目录为：

```text
output/<deviceId>/<channel>/<Release tag>/
```

## 使用方法

以下命令在 `lilygobox-espidf/ota_release` 目录运行，并均为单行命令。

项目默认 `PROJECT_VER` 为 `1.0.0`。构建预发布固件时应通过 CMake
缓存参数提供与频道一致的内嵌版本，例如：

```bat
idf.py -DPROJECT_VER=1.1.0-beta.1 build
```

同时需要在 Kconfig 中选择对应的 Alpha 或 Beta 频道。生成器会
再次读取 BIN 内嵌版本，版本或频道不一致时拒绝生成发布文件。
`PROJECT_VER` 会保存在 CMake 构建缓存中，不同频道建议使用独立的
构建目录和 `sdkconfig`，避免配置相互覆盖。

### 第一次正式发布

```bat
python .\generate_manifest.py --release 1.0.0 --channel stable --firmware-file "device-v1.0-esp32p4-rev1.0=1.0.0=.\input\lilygobox-espidf.bin" --firmware-file "device-v1.0-esp32c6-rev0.0=2.12.3=.\input\network_adapter.bin" --note "首次发布 LilygoBox 固件"
```

### 只更新主固件

```bat
python .\generate_manifest.py --release 1.0.1 --channel stable --firmware-file "device-v1.0-esp32p4-rev1.0=1.0.1=.\input\lilygobox-espidf.bin" --previous-manifest ".\output\t-display-p4\stable\v1.0.0\lilygobox-t-display-p4-ota-manifest-stable-v1.json" --note "更新主固件"
```

### 只更新无线固件

```bat
python .\generate_manifest.py --release 1.0.2 --channel stable --firmware-file "device-v1.0-esp32c6-rev0.0=2.12.4=.\input\network_adapter.bin" --previous-manifest ".\output\t-display-p4\stable\v1.0.1\lilygobox-t-display-p4-ota-manifest-stable-v1.json" --note "更新无线固件"
```

未更新文件从同频道的上一版 Manifest 继承，下载地址继续指向真正包含
该 BIN 的历史 Release。

### 生成 Alpha 或 Beta

Alpha 首次发布示例：

```bat
python .\generate_manifest.py --release 1.1.0-alpha.1 --channel alpha --firmware-file "device-v1.0-esp32p4-rev1.0=1.1.0-alpha.1=.\input\lilygobox-espidf.bin" --firmware-file "device-v1.0-esp32c6-rev0.0=2.12.3=.\input\network_adapter.bin" --note "1.1.0 Alpha 版"
```

Beta 首次发布示例：

```bat
python .\generate_manifest.py --release 1.1.0-beta.1 --channel beta --firmware-file "device-v1.0-esp32p4-rev1.0=1.1.0-beta.1=.\input\lilygobox-espidf.bin" --firmware-file "device-v1.0-esp32c6-rev0.0=2.12.3=.\input\network_adapter.bin" --note "1.1.0 公开测试版"
```

后续 Alpha 或 Beta 版本使用本频道上一版 Manifest 继承未更新文件。生成器
会拒绝 `stable + -beta.N`、`beta + 无后缀` 等频道与版本不一致的组合。
三个频道共用同一个 `devices/t-display-p4.json`，不复制硬件配置。

### 增加硬件或芯片版本

只需修改 `devices/t-display-p4.json`：

1. 在 `files` 增加新固件文件 ID 及可信 `chip`、`projectName`。
2. 在 `targets` 增加新的 `deviceVersion` 和完整芯片 `revision` 组合。
3. 用 `components` 将安装角色指向固件文件 ID。
4. 生成新版本时提供新增 BIN，并从同频道上一版继承已有文件。

只要安装角色仍是设备端已经支持的 `main` 和 `wireless`，不需要修改
Manifest 格式或设备端解析器。只有增加全新的安装角色时，才需要增加
对应安装逻辑。

### 覆盖未发布的本地输出

输出目录非空时工具默认停止。仅在确认覆盖尚未发布内容时添加：

```bat
--force
```

## 发布检查

生成目录示例：

```text
output/t-display-p4/stable/v1.0.0/
├─ lilygobox-t-display-p4-ota-manifest-stable-v1.json
├─ lilygobox-t-display-p4-device-v1.0-esp32p4-rev1.0-v1.0.0.bin
└─ lilygobox-t-display-p4-device-v1.0-esp32c6-rev0.0-v2.12.3.bin
```

1. 创建与输出目录一致的版本 Draft Release，例如 `v1.0.0`。
2. 把生成器列出的新 BIN 上传到该版本 Release；继承的 BIN 不用重传。
3. 检查频道、版本、目标、下载地址和更新说明。
4. 固件资产上传完成后，把 Manifest 上传或替换到对应的固定频道
   Release：`ota-alpha`、`ota-beta` 或 `ota-stable`。
5. 固定频道 Release 只保存当前 Manifest；版本 Release 保存不可变 BIN。

不要先公开 Manifest 再补传 BIN，否则设备可能读取到不完整的发布。
