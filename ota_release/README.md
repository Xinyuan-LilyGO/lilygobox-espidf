<h1 align="center">LilygoBox OTA Release Tool</h1>

## **English | [Chinese](./README_CN.md)**

Generate firmware assets and OTA manifests for GitHub Releases. Internet OTA and
Flutter LAN OTA share the same hardware, component version, size, SHA-256, chip
and embedded project checks.

## Product manifests

Publish one v2 manifest per product model and release channel. Hardware and chip
variants are targets inside that manifest, not separate manifest endpoints.

| Configuration | Stable manifest |
| --- | --- |
| `devices/t-display-p4.json` | `lilygobox_t-display-p4_ota-manifest_stable_v2.json` |
| `devices/t-display-p4-air.json` | `lilygobox_t-display-p4-air_ota-manifest_stable_v2.json` |

The P4 configuration contains both V1.0 and V2.0. Air currently contains only its
existing V1.0 target. Add future board versions or incompatible chip series to
the same product configuration only when their actual supported BINs exist.
Never substitute another board's firmware or expand compatibility in JSON alone.

Firmware assets retain their hardware version and chip series:

```text
lilygobox_t-display-p4-v1.0_esp32p4-v1.0_v1.1.3.bin
lilygobox_t-display-p4-v2.0_esp32p4-v1.0_v1.1.3.bin
lilygobox_t-display-p4-air-v1.0_esp32p4-v1.0_v1.1.3.bin
lilygobox_t-display-p4-air-v1.0_esp32c5-v1.0_v2.12.3.bin
```

Underscores separate groups; hyphens remain within product names. Internal IDs use
`<product>-v<board-version>_<chip>-v<chip-label>`, for example
`t-display-p4-v2.0_esp32p4-v1.0`. New asset names are
`lilygobox_<file-id>_v<firmware-version>.bin`, retaining the chip label's `.0`.
Labels do not replace compatibility ranges or BIN header checks. Product, chip
and owning board must agree with the configuration. Shared cross-board files
retain one owning ID and appear once in `files`; other targets reference it.

The generator accepts legacy `device-v<board>-<chip>-rev<chip-label>` CLI IDs and
previous v2 manifests. Inheritance converts references while preserving published
BIN names and URLs. Legacy v1 bridge manifests retain old IDs and asset names;
v1 schemas remain unchanged so factory firmware can still read them.
New v2 IDs contain underscores and require device firmware with the updated ID
validator. Devices with older parsers must keep using manifests with the original
IDs until migrated; do not directly replace their online entry with new IDs.

## Contract and matching

- JSON fields use `lowerCamelCase`; JSON versions omit `v`; timestamps use UTC
  RFC 3339 with second precision.
- `manifestVersion: "2.0"` requires inclusive `minRevision` / `maxRevision`.
- `release.deviceId` identifies the product; `compatibility.deviceVersion`
  identifies the OTA hardware compatibility version.
- A target matches the board version, both chip models and both chip revision
  ranges. `components.main` and `components.wireless` reference `files`.
- Incompatible chip series require distinct file IDs. Compatible wireless
  images may be shared between targets.
- Exactly one target must match an actual device. Overlap, inverted or missing
  ranges, unknown files and conflicting asset names are rejected.
- Limits: 32 targets, 64 files, 32 KiB manifest, 64 MiB per BIN.
- Stable versions use `X.Y.Z`; alpha and beta use `X.Y.Z-alpha.N` and
  `X.Y.Z-beta.N`. Each component must match its release channel.

For example, incompatible ESP32-P4 1.x and 2.x on Air V1.0 use two targets in
one Air manifest, referencing separate `t-display-p4-air-v1.0_esp32p4-v1.0` and
`t-display-p4-air-v1.0_esp32p4-v2.0` files. The real BIN header must cover every
advertised range endpoint. Current local P4 images support 0.0 through 1.99,
not 2.0 or 3.0.

Schemas: [manifest v2](./ota_manifest_v2.schema.json),
[configuration v2](./ota_device_config_v2.schema.json). Retain v1 schemas for
factory-device migration.

## Device and Flutter

Authorized WebSocket `ready` and `pong` responses include:

```json
{
  "chips": {
    "main": {"model": "esp32p4", "revision": "1.3", "revisionSource": "runtime"},
    "wireless": {"model": "esp32c5", "revision": "1.0", "revisionSource": "boardConfig"}
  }
}
```

The main revision comes from `esp_chip_info()`. Flutter selects a unique target
using board and chip information before comparing component versions. Old
firmware without `chips` is accepted only when all candidate targets reference
the same component pair; ambiguous variants are rejected. The device independently
checks image headers, size, SHA-256, project name and embedded versions.

Wireless silicon revision is still a board configuration assumption: C6 0.0 on
P4 V1, C5 1.0 on P4 V2 and Air. The current coprocessor interface exposes its
firmware version, not its actual silicon revision. Implement runtime revision
readout before using incompatible wireless revisions within one board version.

Internet and LAN updates use one serialized manager. App connection does not
disable Internet OTA. After LAN cancellation or failure, settings checks the
online manifest instead of expired app URLs. Installation, reboot validation,
pending state and rollback continue to use the existing device flow.

## Factory P4 V1 migration

Only the P4 configuration includes `legacyTargets`, covering hardware 1.0 with
confirmed factory main revisions 1.0 and 1.3. Both exact-match targets reference the same compatible component files. By default the tool generates only
the v2 product manifest. Add `--legacy-manifest` for the one-time bridge release
to also generate:

```text
lilygobox-t-display-p4-ota-manifest-stable-v1.json
```

This retains protocol `1.0` and exact `revision` matching. It includes only V1
files. Old firmware rejects underscores in BIN names, so the generator produces
identical-content aliases with historical hyphen names. Upload all listed aliases
and both manifests for that bridge release. Keep the bridge manifest unchanged
after publication, together with its version release and all referenced legacy
BINs. Devices still on old firmware can upgrade to the bridge, then read the v2
product endpoint. Later publications omit `--legacy-manifest` and update only v2.
Bridge generation requires actual BINs for every V1 migration component, not
only inherited metadata, so historical-name assets are guaranteed to be generated.
Do not replace the historical endpoint with v2 JSON.
New firmware also reads legacy persisted pending manifests. V2 and Air have no
legacy endpoint under the current publishing policy.

To upgrade installed 1.1.2, compile a higher embedded version, such as 1.1.3,
including v2 parsing, product endpoints and `lilygobox#localOtaStateV1` recovery.
Renaming or replacing JSON cannot add code to existing BINs. Do not overwrite
published v1.1.2 assets.

## Generate assets

Run from `lilygobox-espidf/ota_release`. Input embedded versions are never rewritten.
The first P4 publication requires all four components. This example also creates
the one-time bridge; omit `--legacy-manifest` for later publications:

```powershell
python .\generate_manifest.py --config .\devices\t-display-p4.json --channel stable --legacy-manifest --firmware-file "t-display-p4-v1.0_esp32p4-v1.0=.\input\t_display_p4_v1\lilygobox-espidf.bin" --firmware-file "t-display-p4-v1.0_esp32c6-v0.0=.\input\t_display_p4_v1\network_adapter.bin" --firmware-file "t-display-p4-v2.0_esp32p4-v1.0=.\input\t_display_p4_v2\lilygobox-espidf.bin" --firmware-file "t-display-p4-v2.0_esp32c5-v1.0=.\input\t_display_p4_v2\network_adapter.bin" --note "Release notes"
```

First Air publication:

```powershell
python .\generate_manifest.py --config .\devices\t-display-p4-air.json --channel stable --firmware-file "t-display-p4-air-v1.0_esp32p4-v1.0=.\input\t_display_p4_air\lilygobox-espidf.bin" --firmware-file "t-display-p4-air-v1.0_esp32c5-v1.0=.\input\t_display_p4_air\network_adapter.bin" --note "Release notes"
```

Update only the P4 V2 main component while retaining V1 and wireless metadata:

```powershell
python .\generate_manifest.py --config .\devices\t-display-p4.json --channel stable --firmware-file "t-display-p4-v2.0_esp32p4-v1.0=.\input\t_display_p4_v2\lilygobox-espidf.bin" --previous-manifest ".\output\t-display-p4\stable\v1.1.3\lilygobox_t-display-p4_ota-manifest_stable_v2.json" --note "Update V2 main firmware"
```

- Release version defaults to the highest updated main embedded version.
  Components for different targets may have different software versions.
- Explicit `--release` must not be lower than any updated main version;
  wireless-only publications require it.
- With inheritance, the release and changed components must increase, and the
  channel must remain the same. Actual target versions come from `files.version`.
- Initial migration from board-specific manifests, or changed/added targets,
  requires all actual BINs without `--previous-manifest`. The tool must validate
  every new range rather than extending inherited metadata.
- Always publish the complete product manifest, not the currently flashed board's
  subset. Inheritance retains unchanged component versions and URLs.
- `--legacy-manifest` generates optional one-time P4 V1 migration assets.
- `--channel alpha|beta|stable` selects a channel; up to three `--note` arguments,
  each 1–127 UTF-8 bytes; `--output-root` selects a directory; `--force` replaces
  only assets for the selected generation mode. Default v2 generation preserves
  existing legacy manifests and historical-name BINs in the same directory.

## Output and publication

The P4 tree below includes `--legacy-manifest` bridge assets; ordinary releases
contain only v2 manifests and their new-name BINs.

```text
output/
├─ t-display-p4/stable/v1.1.3/
│  ├─ lilygobox_t-display-p4_ota-manifest_stable_v2.json
│  ├─ lilygobox-t-display-p4-ota-manifest-stable-v1.json
│  ├─ lilygobox_t-display-p4-v1.0_esp32p4-v1.0_v1.1.3.bin
│  ├─ lilygobox_t-display-p4-v2.0_esp32p4-v1.0_v1.1.3.bin
│  └─ Wireless BINs and historical V1 aliases
└─ t-display-p4-air/stable/v1.1.3/
   ├─ lilygobox_t-display-p4-air_ota-manifest_stable_v2.json
   └─ Air main and wireless BINs
```

Upload every listed new BIN to the version release first, then upload/replace
manifests in `ota-stable`. The bridge release uploads both manifests once; later
releases keep the old bridge and its BINs available and replace only v2. Inherited
assets stay in their original release and need not be re-uploaded. Use `ota-alpha`
or `ota-beta` and corresponding manifest channel names for prereleases.

`output/product-manifest-v2-preview/` reuses existing 1.1.2 images only for format
validation; it is not migration firmware. The previous board-specific
`protocol-v2-preview` is superseded.
