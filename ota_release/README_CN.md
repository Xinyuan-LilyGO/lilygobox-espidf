<h1 align="center">LilygoBox OTA Release 工具</h1>

## **[English](./README.md) | Chinese**

生成 GitHub Releases 的固件资产和 OTA 清单。互联网更新和 Flutter 局域网更新
使用同一套产品目标、版本、大小、SHA-256、芯片及项目名校验。

## 产品清单与资产命名

每个产品型号、每个发布频道只有一份 v2 清单；清单名称不含板子硬件版本或芯片系列。

| 产品配置 | stable 清单 |
| --- | --- |
| `devices/t-display-p4.json` | `lilygobox_t-display-p4_ota-manifest_stable_v2.json` |
| `devices/t-display-p4-air.json` | `lilygobox_t-display-p4-air_ota-manifest_stable_v2.json` |

P4 配置同时包含板子 V1.0、V2.0；Air 当前只包含已有的 V1.0 固件目标。
未来 Air V2.0 或新的不兼容芯片系列加入同一产品配置的 `targets`，不另建清单。
不要将另一板型的 BIN 当成新硬件的固件，也不要在没有真实 BIN 时扩大兼容范围。

固件继续区分产品、板子硬件版本、芯片系列和软件版本：

```text
lilygobox_t-display-p4-v1.0_esp32p4-v1.0_v1.1.3.bin
lilygobox_t-display-p4-v2.0_esp32p4-v1.0_v1.1.3.bin
lilygobox_t-display-p4-air-v1.0_esp32p4-v1.0_v1.1.3.bin
lilygobox_t-display-p4-air-v1.0_esp32c5-v1.0_v2.12.3.bin
```

下划线分组，产品内部使用连字符。内部文件 ID 使用
`<产品>-v<板子版本>_<芯片>-v<芯片标识>`，例如
`t-display-p4-v2.0_esp32p4-v1.0`；生成文件名为
`lilygobox_<文件ID>_v<固件版本>.bin`，保留芯片标识中的 `.0`。
芯片标识不能替代清单范围或镜像头校验。文件 ID 的产品、芯片和硬件归属
必须与配置一致。跨硬件共用文件时保留一个归属 ID，其他目标引用同一条 `files`。

生成器兼容旧 `device-v<板子版本>-<芯片>-rev<芯片标识>` 命令参数和上一版 v2 清单，
继承时只转换引用 ID，不修改已发布 BIN 的文件名或地址。v1 中转清单仍输出旧 ID
和旧资产名；v1 Schema 保持原样，确保工厂旧固件可以读取。
新 v2 ID 含下划线，需要包含新版 ID 校验规则的设备固件；尚未升级解析器的设备
仍应使用原 ID 的发布清单，不能直接覆盖其在线入口。

## 协议与目标匹配

- JSON 字段使用 `lowerCamelCase`，版本值不带 `v`，时间为 UTC RFC 3339 秒精度。
- `manifestVersion: "2.0"` 使用 `minRevision`、`maxRevision` 闭区间。
- `release.deviceId` 标识产品；`compatibility.deviceVersion` 标识 OTA 硬件版本。
- 每个目标同时匹配板子版本、主芯片型号和修订范围、无线芯片型号和修订范围。
- `components.main` / `components.wireless` 引用 `files` 中的固件，不兼容芯片使用不同文件 ID。
- 同一实际硬件只能命中一个目标。重叠、倒置、缺失范围以及未知引用会被拒绝。
- 单个清单最多 32 个目标、64 个文件、32 KiB；每个 BIN 最大 64 MiB。
- 版本使用 `X.Y.Z`、`X.Y.Z-alpha.N`、`X.Y.Z-beta.N`，发布频道必须一致。

例如 Air V1.0 的芯片 1.x 和 2.x 若不兼容，使用同一份清单的两个目标，分别引用
`t-display-p4-air-v1.0_esp32p4-v1.0`、`t-display-p4-air-v1.0_esp32p4-v2.0`。
无线固件相同时可共用文件。实际范围必须由对应 BIN 的最小/最大修订号覆盖，
不能只改清单扩大范围。当前实际 P4 BIN 范围为 0.0～1.99，不包含芯片 2.0 或 3.0。

Schema：
[清单 v2](./ota_manifest_v2.schema.json)、
[配置 v2](./ota_device_config_v2.schema.json)。
v1 Schema 保留供出厂设备迁移使用。

## Flutter 与设备

授权 WebSocket 的 `ready` / `pong` 增加 `chips`：

```json
{
  "chips": {
    "main": {"model": "esp32p4", "revision": "1.3", "revisionSource": "runtime"},
    "wireless": {"model": "esp32c5", "revision": "1.0", "revisionSource": "boardConfig"}
  }
}
```

主芯片修订号通过 `esp_chip_info()` 实际读取，Flutter 和设备使用同一来源。
Flutter 根据硬件和芯片条件选择唯一目标，再比较各组件版本。旧设备未上报 `chips`
时，仅允许匹配目标引用同一组固件；存在不兼容的多个选择时拒绝猜选。
设备下载后再次验证大小、SHA-256、芯片、项目名和内嵌版本。

无线接口目前只提供固件版本，修订号仍来自板级配置：P4 V1 为 C6 0.0，
P4 V2 / Air 为 C5 1.0，不能将它描述为运行时读取的真实 silicon revision。
在同一板子上增加不兼容的无线芯片修订批次前，必须先补齐无线芯片实际修订号读取。

互联网 OTA 与局域网 OTA 由同一管理器串行执行。应用连接不禁用互联网更新。
局域网取消或失败后，设置页重新读取在线清单，不重试过期的本地 URL。

## P4 V1 出厂设备迁移

只有 P4 配置带 `legacyTargets`，且仅包含硬件 1.0 的已确认的主芯片修订号 1.0、1.3，两个精确匹配目标共用同一组兼容固件。
默认只生成 v2 产品清单，不生成旧清单或旧名称 BIN。
首次中转发布时，在 P4 生成命令上增加 `--legacy-manifest`，才额外输出历史入口：

```text
lilygobox-t-display-p4-ota-manifest-stable-v1.json
```

它保留 `manifestVersion: "1.0"` 和精确 `revision`，只包含 V1 目标及其引用文件。
旧设备拒绝 BIN 文件名中的下划线，因此生成器同时输出旧名称的相同内容副本。
首次中转发布时必须上传所有列出的新旧名称 BIN，并同时更新新旧清单。
中转清单发布一次后保持不变，并永久保留它引用的版本 Release 与旧名称 BIN。
尚未升级的设备仍可先更新到中转固件，再从 v2 产品清单获取后续版本。
以后发布省略 `--legacy-manifest`，只更新 v2；不需要反复生成或覆盖旧清单。
生成中转清单必须提供全部 V1 迁移组件的实际 BIN，不能仅继承元数据，
避免清单引用尚未上传的旧名称副本。
不要把旧地址的 JSON 替换为 v2；新固件仍能读取升级前保存的 v1 pending 清单。
P4 V2 与 Air 不生成旧入口。

出厂 1.1.2 的升级目标应重新编译更高版本（例如 1.1.3），包含 v2 读取代码、
新的产品清单地址及 `lilygobox#localOtaStateV1` 恢复逻辑。
仅改 JSON 或改文件名不能让旧 BIN 获得新功能，也不要覆盖已发布的 v1.1.2。

## 生成发布文件

以下命令从 `lilygobox-espidf/ota_release` 目录执行。不修改输入 BIN 的内嵌版本。

首次 P4 发布必须提供 V1、V2 的全部组件。下面示例额外生成一次性中转清单；
后续发布去掉 `--legacy-manifest`：

```powershell
python .\generate_manifest.py --config .\devices\t-display-p4.json --channel stable --legacy-manifest --firmware-file "t-display-p4-v1.0_esp32p4-v1.0=.\input\t_display_p4_v1\lilygobox-espidf.bin" --firmware-file "t-display-p4-v1.0_esp32c6-v0.0=.\input\t_display_p4_v1\network_adapter.bin" --firmware-file "t-display-p4-v2.0_esp32p4-v1.0=.\input\t_display_p4_v2\lilygobox-espidf.bin" --firmware-file "t-display-p4-v2.0_esp32c5-v1.0=.\input\t_display_p4_v2\network_adapter.bin" --note "更新说明"
```

首次 Air 发布：

```powershell
python .\generate_manifest.py --config .\devices\t-display-p4-air.json --channel stable --firmware-file "t-display-p4-air-v1.0_esp32p4-v1.0=.\input\t_display_p4_air\lilygobox-espidf.bin" --firmware-file "t-display-p4-air-v1.0_esp32c5-v1.0=.\input\t_display_p4_air\network_adapter.bin" --note "更新说明"
```

后续只更新 P4 V2 主固件时，继承产品完整清单，其他组件的版本和下载地址继续保留：

```powershell
python .\generate_manifest.py --config .\devices\t-display-p4.json --channel stable --firmware-file "t-display-p4-v2.0_esp32p4-v1.0=.\input\t_display_p4_v2\lilygobox-espidf.bin" --previous-manifest ".\output\t-display-p4\stable\v1.1.3\lilygobox_t-display-p4_ota-manifest_stable_v2.json" --note "更新 V2 主固件"
```

- `--release` 默认取本次主固件最高内嵌版本；不同目标的组件可以使用不同软件版本。
- 显式 `--release` 不能低于本次主固件最高版本；只更新无线固件时必须指定它。
- 继承时新 Release 和发生变化组件的版本必须升高，频道必须相同。
- 每个组件的目标版本以其 `files.version` 为准，不以产品 `release.version` 替代。
- 首次从分板清单切换为产品清单、增加或改变兼容目标时，重新提供全部实际 BIN，
  不使用 `--previous-manifest`。生成器需要验证所有新范围，不能只继承元数据。
- 每次输出完整产品清单，不能只发布当前板子的子集覆盖产品入口。
- `--channel alpha|beta|stable` 切换频道；`--note` 最多三条，每条 1～127 UTF-8 字节。
- `--legacy-manifest` 仅在需要生成 P4 V1 一次性中转资产时使用。
- `--output-root` 指定输出根目录；`--force` 只清理当前模式的生成资产，
  默认模式不会删除同目录已有的旧清单及旧名称 BIN。

## 输出与上传

以下 P4 目录展示带 `--legacy-manifest` 的首次中转发布；普通发布没有旧清单和旧名称 BIN。

```text
output/
├─ t-display-p4/stable/v1.1.3/
│  ├─ lilygobox_t-display-p4_ota-manifest_stable_v2.json
│  ├─ lilygobox-t-display-p4-ota-manifest-stable-v1.json
│  ├─ lilygobox_t-display-p4-v1.0_esp32p4-v1.0_v1.1.3.bin
│  ├─ lilygobox_t-display-p4-v2.0_esp32p4-v1.0_v1.1.3.bin
│  └─ 无线 BIN 以及 V1 旧名称 BIN 副本
└─ t-display-p4-air/stable/v1.1.3/
   ├─ lilygobox_t-display-p4-air_ota-manifest_stable_v2.json
   └─ Air 主固件及无线 BIN
```

1. 将生成器列出的新 BIN 全部上传至对应版本 Release（例如 `v1.1.3`），继承文件不用重传。
2. 核对目标、版本、大小、SHA-256、下载地址和更新说明。
3. BIN 上传完成后，向 `ota-stable` 上传/替换产品清单；首次中转发布额外上传历史 v1 清单，
   后续保留该中转清单及其引用的 BIN，只替换 v2 产品清单。
4. alpha / beta 分别发布至 `ota-alpha` / `ota-beta`，清单名频道随之变化。

先上传 BIN，再更新清单，防止用户获取到尚未上传的固件。
`output/product-manifest-v2-preview/` 仅用现有 1.1.2 BIN 检查格式和校验，
不是可发布的迁移固件。旧 `protocol-v2-preview` 为此前分硬件清单预览，已被取代。
