<h1 align="center">LilygoBox OTA Release 工具</h1>

## **[English](./README.md) | Chinese**

用于生成 GitHub Releases 固件资产和 OTA 清单。互联网 OTA 与 Flutter 局域网 OTA
共用同一套硬件目标、组件版本、文件大小、SHA-256、芯片型号和内嵌项目名校验。

## 产品清单

每个产品型号、每个发布通道发布一份 v2 清单。不同硬件版本和芯片组合均作为目标放在该产品清单中，不另设清单入口。

| 配置 | Stable 清单 |
| --- | --- |
| `devices/t-display-p4.json` | `lilygobox-t-display-p4-ota-manifest-stable-v2.json` |
| `devices/t-display-p4-air.json` | `lilygobox-t-display-p4-air-ota-manifest-stable-v2.json` |

P4 配置同时包含 V1.0 和 V2.0；Air 目前只包含已有的 V1.0 目标。只有在存在对应且实际支持的 BIN 时，才能将未来的板子版本或不兼容芯片系列加入同一产品配置。不得用其他板型的固件代替，也不能只修改 JSON 来扩大兼容范围。

固件资产名保留板子硬件版本和芯片系列。v2 资产名不包含软件版本；软件版本记录在清单和带版本号的 GitHub Release 标签中：

```text
lilygobox-t-display-p4-v1.0-esp32p4-v1.0.bin
lilygobox-t-display-p4-v2.0-esp32p4-v1.0.bin
lilygobox-t-display-p4-air-v1.0-esp32p4-v1.0.bin
lilygobox-t-display-p4-air-v1.0-esp32c5-v1.0.bin
```

内部文件 ID 格式为 `<产品>-v<板子版本>-<芯片>-v<芯片标识>`，例如 `t-display-p4-v2.0-esp32p4-v1.0`。新的 v2 资产名为 `lilygobox-<文件ID>.bin`，芯片标识中的 `.0` 必须保留。清单继续记录每个组件的软件版本、文件大小和 SHA-256；GitHub Release 标签仍带版本号，例如 `v1.1.3`。v1 中转资产继续使用原有的带版本旧名称。

芯片标识不能替代清单兼容范围或 BIN 镜像头校验。产品、芯片和所属板子必须与配置一致。跨板共用固件时，在 `files` 中保留一个所属文件 ID，其他目标引用同一个文件。

生成器使用产品配置中的规范文件 ID，并从上一版 v2 清单继承未变更组件。旧 v1 中转清单继续使用旧 ID 和资产名；v1 Schema 保持不变，以便工厂固件读取。

新的 v2 设备配置使用单个 `downloadUrlTemplate` 字符串；生成的 v2 清单为每个固件资产使用单个 `downloadUrl` 字符串。设备先访问该地址，遇到网络失败后才在运行时构造 `gh-proxy.com` 备用地址。已发布的 v1 中转清单仍使用 `downloadUrls` 数组。

新的 v2 文件 ID 全部使用连字符。旧解析器设备应先通过一次性 v1 中转清单升级；新固件只接受 v2 产品清单。旧设备完成中转升级前，不要直接替换旧的在线清单入口。

## 协议与目标匹配

- JSON 字段使用 `lowerCamelCase`；JSON 版本值不带 `v`；时间使用 UTC RFC 3339 秒精度格式。
- `manifestVersion: "2.0"` 使用闭区间 `minRevision` / `maxRevision`。
- `release.deviceId` 标识产品；`compatibility.deviceVersion` 标识 OTA 硬件兼容版本。
- 每个目标都匹配板子版本、主芯片型号及修订范围、无线芯片型号及修订范围。`components.main` 和 `components.wireless` 引用 `files` 中的固件。
- 不兼容的芯片系列必须使用不同文件 ID；兼容的无线固件可以由多个目标共用。
- 实际设备必须且只能匹配一个目标。目标范围重叠、上下限倒置或缺失、引用未知文件、资产名冲突都会被拒绝。
- 限制：每份清单最多 32 个目标、64 个文件、32 KiB；每个 BIN 最大 64 MiB。
- Stable 版本使用 `X.Y.Z`；Alpha 和 Beta 使用 `X.Y.Z-alpha.N`、`X.Y.Z-beta.N`。每个组件的版本通道必须与发布通道一致。

例如，若 Air V1.0 上 ESP32-P4 1.x 和 2.x 不兼容，应在同一份 Air 清单中设置两个目标，分别引用 `t-display-p4-air-v1.0-esp32p4-v1.0` 和 `t-display-p4-air-v1.0-esp32p4-v2.0`。实际 BIN 镜像头必须覆盖清单声明范围的每个端点。当前 P4 镜像支持 0.0 到 1.99，不支持 2.0 或 3.0。

Schema：[清单 v2](./ota_manifest_v2.schema.json)、[设备配置 v2](./ota_device_config_v2.schema.json)。保留 v1 Schema，供工厂设备迁移使用。

## 设备与 Flutter

授权 WebSocket 的 `ready` 和 `pong` 响应包含 `chips`：

```json
{
  "chips": {
    "main": {"model": "esp32p4", "revision": "1.3", "revisionSource": "runtime"},
    "wireless": {"model": "esp32c5", "revision": "1.0", "revisionSource": "boardConfig"}
  }
}
```

主芯片修订号通过 `esp_chip_info()` 读取。Flutter 先根据板子和芯片信息选择唯一目标，再比较组件版本。没有上报 `chips` 的旧固件，仅当所有候选目标引用同一组组件固件时才可匹配；有多个不明确的硬件目标时拒绝猜选。设备还会独立校验镜像头、文件大小、SHA-256、项目名和内嵌版本。

无线芯片的 silicon revision 目前仍由板级配置提供：P4 V1 为 C6 0.0，P4 V2 和 Air 为 C5 1.0。当前协处理器接口只提供固件版本，不能读取真实 silicon revision。在同一板子版本中加入不兼容的无线芯片修订批次前，必须先实现实际修订号读取。

互联网和局域网 OTA 使用同一个串行管理器。连接应用不会禁用互联网 OTA。局域网取消或失败后，设置页重新检查在线清单，不重试已经过期的应用端 URL。安装、重启验证、pending 状态和回滚继续使用设备现有流程。

## P4 V1 工厂设备迁移

只有 P4 配置包含 `legacyTargets`，覆盖硬件版本 1.0，以及已确认的主芯片修订号 1.0 和 1.3；两个精确匹配目标引用同一组兼容固件。默认只生成 v2 产品清单。首次中转发布时增加 `--legacy-manifest`，才会额外生成一次性清单：

```text
lilygobox-t-display-p4-ota-manifest-stable-v1.json
```

该清单保留协议版本 `1.0` 和精确 `revision` 匹配，只包含 V1 文件。生成器会为 v1 清单保留历史 BIN 名称。中转发布时上传列出的 BIN 和两份清单。发布后保持中转清单不变，并保留它引用的版本 Release 及所有旧名称 BIN。旧固件设备可以先升级到中转固件，再读取 v2 产品清单。后续发布省略 `--legacy-manifest`，只更新 v2。

生成中转清单时，必须提供所有 V1 迁移组件的实际 BIN，不能只提供继承的元数据，确保生成器会创建旧名称资产。不要将历史入口的 JSON 替换成 v2。中转升级后，新固件的在线更新只使用 v2 产品清单。内部 `lilygobox#localOtaStateV1` 记录用于局域网 OTA 恢复，不代表支持 v1 在线清单。按当前策略，P4 V2 和 Air 没有旧版清单入口。

要升级出厂版本 1.1.2，必须编译更高的内嵌版本，例如 1.1.3，并包含 v2 解析、产品清单入口以及 `lilygobox#localOtaStateV1` 恢复逻辑。修改 JSON 或重命名文件不能给旧 BIN 增加代码。不要覆盖已发布的 v1.1.2 资产。

## 生成发布资产

在 `lilygobox-espidf/ota_release` 目录执行以下命令。输入 BIN 的内嵌版本不会被改写。

首次发布 P4 时必须提供 V1、V2 的全部四个组件。以下示例同时生成一次性中转清单；后续发布需移除 `--legacy-manifest`：

```powershell
python .\generate_manifest.py --config .\devices\t-display-p4.json --channel stable --legacy-manifest --firmware-file "t-display-p4-v1.0-esp32p4-v1.0=.\input\t_display_p4_v1\lilygobox-espidf.bin" --firmware-file "t-display-p4-v1.0-esp32c6-v0.0=.\input\t_display_p4_v1\network_adapter.bin" --firmware-file "t-display-p4-v2.0-esp32p4-v1.0=.\input\t_display_p4_v2\lilygobox-espidf.bin" --firmware-file "t-display-p4-v2.0-esp32c5-v1.0=.\input\t_display_p4_v2\network_adapter.bin" --note "更新说明"
```

首次发布 Air：

```powershell
python .\generate_manifest.py --config .\devices\t-display-p4-air.json --channel stable --firmware-file "t-display-p4-air-v1.0-esp32p4-v1.0=.\input\t_display_p4_air\lilygobox-espidf.bin" --firmware-file "t-display-p4-air-v1.0-esp32c5-v1.0=.\input\t_display_p4_air\network_adapter.bin" --note "更新说明"
```

只更新 P4 V2 主组件时，继承产品完整清单中的 V1 和无线组件元数据：

```powershell
python .\generate_manifest.py --config .\devices\t-display-p4.json --channel stable --firmware-file "t-display-p4-v2.0-esp32p4-v1.0=.\input\t_display_p4_v2\lilygobox-espidf.bin" --previous-manifest ".\output\t-display-p4\stable\v1.1.3\lilygobox-t-display-p4-ota-manifest-stable-v2.json" --note "更新 V2 主固件"
```

- Release 版本默认取本次更新的主固件内嵌版本中的最高版本。不同目标的组件可以使用不同软件版本。
- 显式指定的 `--release` 不能低于本次更新的任一主固件版本；仅更新无线固件时必须指定该参数。
- 使用上一版清单继承时，Release 版本和发生变化的组件版本必须升高，发布通道必须保持一致。目标组件的实际版本以 `files.version` 为准。
- 首次从分板清单迁移到产品清单，或新增、修改目标时，必须提供全部实际 BIN，不能使用 `--previous-manifest`。工具必须校验每个新兼容范围，不能只继承元数据来扩大范围。
- 始终发布完整产品清单，不能只发布当前刷入设备的目标子集。继承会保留未变组件的版本和下载地址。
- `--legacy-manifest` 用于生成可选的一次性 P4 V1 中转资产。
- `--channel alpha|beta|stable` 选择发布通道；最多传入三个 `--note`，每条为 1～127 个 UTF-8 字节；`--output-root` 选择输出目录；`--force` 只替换所选生成模式的资产。v2 生成会使用新的文件名；`--force` 仅替换当前版本对应的输出文件。

## 输出与发布

以下 P4 目录示例包含 `--legacy-manifest` 中转资产；普通发布只包含 v2 清单和新名称 BIN。

```text
output/
├─ t-display-p4/stable/v1.1.3/
│  ├─ lilygobox-t-display-p4-ota-manifest-stable-v2.json
│  ├─ lilygobox-t-display-p4-ota-manifest-stable-v1.json
│  ├─ lilygobox-t-display-p4-v1.0-esp32p4-v1.0.bin
│  ├─ lilygobox-t-display-p4-v2.0-esp32p4-v1.0.bin
│  └─ 无线 BIN 和 V1 历史名称别名
└─ t-display-p4-air/stable/v1.1.3/
   ├─ lilygobox-t-display-p4-air-ota-manifest-stable-v2.json
   └─ Air 主固件和无线 BIN
```

先将所有列出的新 BIN 上传到对应版本 Release，再将清单上传或更新到 `ota-stable`。首次中转发布时同时上传两份清单；后续保留旧中转清单及其 BIN，只替换 v2 清单。继承的资产仍保留在原 Release 中，无需重新上传。预发布版本使用 `ota-alpha` 或 `ota-beta`，清单名也应匹配对应通道。

`output/product-manifest-v2-preview/` 使用现有 1.1.2 镜像进行格式校验，不是中转迁移固件。此前按板子拆分清单的 `protocol-v2-preview` 已被取代。
