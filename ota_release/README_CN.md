<h1 align="center">LilygoBox OTA Release 工具</h1>

## **[English](./README.md) | Chinese**

此工具根据设备配置和实际固件生成 GitHub Releases 资产及 OTA 清单。每个产品、每个通道使用一份 v2 清单；不同硬件和芯片修订作为目标放在同一产品清单中。实际支持范围及文件 ID 以 [设备配置](./devices/) 的 `targets` 和 `files` 为准。

## 固件和兼容性

主固件资产命名为 `lilygobox-<产品>-<芯片>-v<组件版本>.bin`；无线固件（v1 和 v2）统一使用跨产品的 `lilygobox-<芯片及修订>-v<组件版本>.bin` 名称，芯片型号和修订仍须匹配。文件 ID 中的芯片修订标识使用该固件支持的最低修订号。版本、大小和 SHA-256 从实际 BIN 获取，不能只修改配置或清单来扩大兼容范围。不同的不兼容范围应使用独立目标及对应固件；兼容的固件可由多个目标引用。

设备按板子版本、主芯片和无线芯片的型号及修订范围选择唯一目标，并校验固件的镜像头、项目名、版本、大小和 SHA-256。Flutter 局域网更新同样使用这些信息。旧设备未上报芯片信息时，仅在候选目标引用同一组组件时才能匹配。

v2 清单每个文件只包含一个 `downloadUrl`；网络失败时设备可在运行时构造备用代理地址。JSON 字段使用 `lowerCamelCase`，版本值不带 `v`，时间采用 UTC RFC 3339。Stable 版本格式为 `X.Y.Z`，Alpha 和 Beta 分别为 `X.Y.Z-alpha.N` 和 `X.Y.Z-beta.N`，组件版本须符合发布通道。

每份清单最多包含 32 个目标、64 个文件，大小不超过 32 KiB；每个 BIN 不超过 64 MiB。目标不能重叠，设备必须恰好匹配一个目标。更多字段和约束见 [清单 Schema](./ota_manifest_v2.schema.json) 和 [设备配置 Schema](./ota_device_config_v2.schema.json)。

## 生成资产

在 `ota_release` 目录运行，替换尖括号中的占位内容（不要保留尖括号）。从产品配置的 `files` 获取文件 ID，并为需要更新的每个文件重复传入 `--firmware-file`：

```powershell
python .\generate_manifest.py `
  --config .\devices\<产品配置>.json `
  --channel stable `
  --firmware-file "<文件ID>=<固件BIN路径>" `
  --firmware-file "<另一个文件ID>=<固件BIN路径>" `
```

首次生成或目标兼容条件发生变化时，须提供配置所需的全部 BIN。只更新部分组件时，可通过 `--previous-manifest <上一版v2清单路径>` 继承未变更组件；发布版本和更新的组件版本必须递增，通道必须一致。此选项仅供生成器读取本地清单，不表示设备会请求历史清单。


## 旧设备迁移

仍需支持旧清单的设备时，保留已发布的 v1 固定入口及其引用的旧固件。配置包含 `legacyTargets` 时，可使用 `--legacy-manifest` 生成一次性 v1 中转清单和历史名称资产；必须提供所有迁移组件的实际 BIN。不要将 v1 入口替换为 v2 清单。旧设备通过中转固件升级后，新固件使用 v2 产品清单。已发布的版本资产应保持不变，升级固件应使用更高的内嵌版本。

## 发布

生成结果默认位于 `output/<产品>/<通道>/v<发布版本>/`。先将生成的 BIN 上传到对应版本 Release，再将清单上传或替换到对应的 `ota-<通道>` Release。继承的组件仍指向原版本 Release，须保留清单引用的资产；仍需供旧设备迁移的 v1 清单及固件也须保留。
