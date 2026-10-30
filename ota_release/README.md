<h1 align="center">LilygoBox OTA Release Tool</h1>

## **English | [Chinese](./README_CN.md)**

This tool generates GitHub Releases assets and OTA manifests from device configurations and actual firmware binaries. Each product and channel has one v2 manifest; hardware and chip revisions are represented as targets in that product manifest. See `targets` and `files` in the [device configurations](./devices/) for supported ranges and file IDs.

## Firmware and compatibility

Main firmware assets are named `lilygobox-<product>-<chip>-v<component-version>.bin`. Wireless firmware uses the same product-independent name in v1 and v2 manifests: `lilygobox-<chip-and-revision>-v<component-version>.bin`; chip model and revision remain part of compatibility matching. The chip revision in a file ID identifies the lowest revision supported by that firmware. Versions, sizes, and SHA-256 hashes come from the actual BINs. Do not expand compatibility merely by editing a configuration or manifest. Incompatible ranges need separate targets and corresponding firmware; compatible firmware can be referenced by multiple targets.

Devices select exactly one target using the board version and the models and revision ranges of the main and wireless chips, then verify the image header, project name, version, size, and SHA-256. Flutter LAN updates use the same information. When an older device does not report chip information, it can match only if the candidate targets reference the same component files.

Each v2 file has one `downloadUrl`; on network failure the device can construct a fallback proxy URL at runtime. JSON fields use `lowerCamelCase`, version values omit `v`, and timestamps use UTC RFC 3339. Stable versions use `X.Y.Z`, while Alpha and Beta use `X.Y.Z-alpha.N` and `X.Y.Z-beta.N`; component versions must match the release channel.

A manifest supports at most 32 targets and 64 files and must not exceed 32 KiB; each BIN must not exceed 64 MiB. Targets must not overlap, and a device must match exactly one target. See the [manifest schema](./ota_manifest_v2.schema.json) and [device configuration schema](./ota_device_config_v2.schema.json) for fields and constraints.

## Generate assets

Run from `ota_release`, replacing the placeholders and omitting the angle brackets. Find file IDs in the product configuration's `files` and repeat `--firmware-file` for each file being updated:

```powershell
python .\generate_manifest.py `
  --config .\devices\<product-config>.json `
  --channel stable `
  --firmware-file "<file-id>=<firmware-BIN-path>" `
  --firmware-file "<another-file-id>=<firmware-BIN-path>" `
```

For the first generation or when target compatibility changes, provide all BINs required by the configuration. To update only some components, use `--previous-manifest <previous-v2-manifest-path>` to inherit unchanged components; the release version and changed component versions must increase, and the channel must match. This option reads a local manifest for generation; it does not cause devices to request historical manifests.


## Legacy device migration

When older devices still need their legacy manifest, retain the published fixed v1 endpoint and its referenced firmware. Configurations containing `legacyTargets` support `--legacy-manifest` to generate a one-time v1 bridge manifest and historical-name assets; actual BINs for all migration components are required. Do not replace the v1 endpoint with a v2 manifest. After upgrading through the bridge firmware, devices use the v2 product manifest. Keep published version assets unchanged and use a higher embedded version for firmware upgrades.

## Publication

By default, output is written to `output/<product>/<channel>/v<release-version>/`. Upload generated BINs to the corresponding version Release first, then upload or replace the manifest in the corresponding `ota-<channel>` Release. Inherited components still point to their original version Releases, so retain the referenced assets. Also retain any v1 manifest and firmware still needed for legacy device migration.
