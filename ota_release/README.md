<h1 align="center">LilygoBox OTA Release Tool</h1>

## **English | [Chinese](./README_CN.md)**

`ota_release` generates LilygoBox OTA manifests and firmware assets for
GitHub Releases.

## Naming

- Public JSON properties follow Google's `lowerCamelCase` convention.
- Collection fields use plural names such as `targets`, `files`, and
  `downloadUrls`.
- Dynamic map keys use stable lowercase identifiers such as
  `device-v1.0-esp32p4-rev1.0`.
- Timestamp fields use the `Time` suffix, so publication uses `publishTime`.
- Quantity fields use a direct unit suffix, so file size uses `sizeBytes`.
- `name` is reserved for full resource names; device identity uses
  `deviceId`.
- `deviceId` identifies the device model. `deviceVersion` identifies its OTA
  hardware compatibility version, not necessarily the printed PCB revision;
  firmware uses `release.version`.
- Python follows PEP 8 and C++ follows the Google C++ Style Guide.
- JSON versions omit `v`; Git tags and firmware filenames include `v`.
- Firmware versions use constrained SemVer: Alpha uses `X.Y.Z-alpha.N`,
  Beta uses `X.Y.Z-beta.N`, and Stable uses `X.Y.Z`.
- Timestamps are UTC RFC 3339 with second precision.

Field names and semantics follow Google
[AIP-122](https://google.aip.dev/122),
[AIP-126](https://google.aip.dev/126),
[AIP-140](https://google.aip.dev/140),
[AIP-141](https://google.aip.dev/141), and
[AIP-142](https://google.aip.dev/142).

## Compatibility Model

`manifestVersion` identifies only the JSON contract. `deviceVersion`, chip
models, and complete chip revisions are data in `targets.compatibility`.
Adding those targets does not change `manifestVersion`.

`deviceVersion` is the OTA hardware compatibility version, not a value that
must increase for every PCB revision. If PCB v1.0, v1.1, and v1.2 only contain
hardware fixes and remain identical in pins, peripherals, partition table,
drivers, and firmware BIN compatibility, they may all use
`deviceVersion: "1.0"`. Create a new `deviceVersion` only when a hardware
change requires different firmware. Distinct compatibility versions must be
read from a source that OTA cannot overwrite, such as eFuse, factory NVS,
EEPROM, or GPIO/ADC identification. The current T-Display-P4 installer uses
the compiled `kDeviceModelInfo.version`, so one firmware BIN cannot reliably
distinguish multiple PCB revisions.

The version fields have distinct meanings:

| Field | Example | Meaning |
| --- | --- | --- |
| `manifestVersion` | `1.0` | Manifest JSON contract version |
| `deviceVersion` | `1.0` | OTA hardware compatibility version |
| `chips.main.revision` | `1.1` | Main chip silicon revision |
| `release.version` | `1.2.0` | LilygoBox software release version |
| `files.*.version` | `2.12.3` | Component firmware version |

A device accepts exactly one target matching the product identity, compiled
release channel, `deviceVersion`, and the complete `model` plus `revision`
of every chip. For example, silicon revisions `1.0` and `1.1` of the same chip
model are distinct compatibility conditions; they are unrelated to
`deviceVersion`, `release.version`, or `manifestVersion`. No match or multiple
matches reject the update.

### Wireless coprocessor revision limitation

The current Wireless coprocessor interface can read the firmware version, but
it does not expose the silicon revision at runtime. The installer therefore
uses the fixed Wireless chip revision from the device configuration. In the
current manifest, `chips.wireless.revision` is a hardware configuration
assumption, not a value detected from the running coprocessor.

Before adding hardware batches with another Wireless chip model or silicon
revision, make the coprocessor interface return the real chip model and
revision, then use those values for OTA target matching. A read failure must
reject the update instead of falling back to a fixed value. Until then, do not
rely on `chips.wireless.revision` to select firmware for different Wireless
chip silicon revisions.

The complete format is defined by
[ota_manifest_v1.schema.json](./ota_manifest_v1.schema.json), and the
generator configuration by
[ota_device_config_v1.schema.json](./ota_device_config_v1.schema.json).
The manifest uses these top-level fields:

- `kind` and `manifestVersion` identify the contract.
- `release` identifies the publisher ID, device ID, and release version.
- `channel` is `alpha`, `beta`, or `stable`.
- `publishTime` is a UTC RFC 3339 timestamp.
- `targets` maps exact hardware combinations to installation roles.
- `files` stores deduplicated firmware assets.
- `releaseNotes` contains up to three device-displayable notes.

The generator checks the embedded ESP chip ID, ESP-IDF project name, version,
complete image length, and full minimum/maximum chip revisions, then
calculates file size and SHA-256. The device independently checks the size,
SHA-256, and application image metadata.

## Release Channels

The device firmware selects one fixed channel through Kconfig. The release
lifecycle is Alpha, Beta, then Stable:

- `alpha`: internal Alpha builds.
- `beta`: public test builds.
- `stable`: production and the default.

The Release version and embedded Main firmware version must match the
channel:

```text
alpha  -> 1.1.0-alpha.1, 1.1.0-alpha.2
beta   -> 1.1.0-beta.1, 1.1.0-beta.2
stable -> 1.1.0
```

The pre-release sequence is a non-negative integer without leading zeroes.
Wireless firmware keeps its independent version, such as `2.12.3`, and does
not require a channel suffix. Each channel therefore uses a distinct Git tag
and Main firmware filename without duplicating device configuration.

Kconfig is converted to the application-wide `ReleaseChannel` enum only in
`main/app/release_channel.h`. Other C++ code uses `kReleaseChannel` and
`ReleaseChannelName()` instead of depending directly on
`CONFIG_LILYGO_BOX_RELEASE_CHANNEL_*` macros.

Each channel has an independent previous manifest and version sequence:

```text
ota-alpha  -> lilygobox-t-display-p4-ota-manifest-alpha-v1.json
ota-beta   -> lilygobox-t-display-p4-ota-manifest-beta-v1.json
ota-stable -> lilygobox-t-display-p4-ota-manifest-stable-v1.json
```

The left side is the permanent GitHub channel-pointer Release tag. Devices
fetch the manifest from that tag instead of GitHub `latest`, so Alpha and
Beta channels remain discoverable.

## Requirements and Layout

- Python 3.9 or later.
- No third-party Python packages.
- Standalone ESP application BIN files beginning at offset `0`.
- Merged, factory, and multi-partition images are rejected.

```text
ota_release/
├─ devices/
│  ├─ t-display-p4.json
│  └─ t-display-p4-air.json
├─ input/                           User-provided, ignored by Git
├─ output/                          Generated, ignored by Git
├─ generate_manifest.py
├─ ota_device_config_v1.schema.json
├─ ota_manifest_v1.schema.json
├─ README.md
└─ README_CN.md
```

Generated files are stored in:

```text
output/<deviceId>/<channel>/<Release tag>/
```

## Usage

Run these single-line commands from `lilygobox-espidf/ota_release`.
The generator defaults to `devices/t-display-p4.json`. For T-Display-P4-Air,
add `--config .\devices\t-display-p4-air.json` to the command.

The project defaults `PROJECT_VER` to `1.0.0`. Supply the matching embedded
version through the CMake cache when building a pre-release image:

```bat
idf.py -DPROJECT_VER=1.1.0-beta.1 build
```

Select the matching Alpha or Beta Kconfig channel as well. The
generator automatically reads the embedded BIN version and rejects mismatched
versions or channels. `PROJECT_VER` remains in the CMake build cache, so use
separate build directories and `sdkconfig` files for different channels to
avoid configuration overlap.

The recommended `--firmware-file` format is `FILE_ID=BIN`; there is no need
to repeat the firmware version. The original `FILE_ID=VERSION=BIN` format
remains supported and verifies that the declared version matches the embedded
BIN version. When a main firmware image is supplied, `--release` can also be
omitted and the generator uses the main BIN's embedded version as the Release
version. An explicit `--release` is still required when updating only wireless
firmware or otherwise omitting the main firmware. If supplied alongside a main
firmware image, `--release` must match the main firmware version.

### First Stable Release

```bat
python .\generate_manifest.py --channel stable --firmware-file "device-v1.0-esp32p4-rev1.0=.\input\lilygobox-espidf.bin" --firmware-file "device-v1.0-esp32c6-rev0.0=.\input\network_adapter.bin" --note "Initial LilygoBox firmware release"
```

T-Display-P4-Air uses its independent device ID and ESP32-C5 Wireless image:

```bat
python .\generate_manifest.py --config .\devices\t-display-p4-air.json --channel stable --firmware-file "device-v1.0-esp32p4-rev1.0=.\input\lilygobox-espidf.bin" --firmware-file "device-v1.0-esp32c5-rev0.0=.\input\network_adapter.bin" --note "Initial T-Display-P4-Air firmware release"
```

### Update Main Firmware Only

```bat
python .\generate_manifest.py --channel stable --firmware-file "device-v1.0-esp32p4-rev1.0=.\input\lilygobox-espidf.bin" --previous-manifest ".\output\t-display-p4\stable\v1.0.0\lilygobox-t-display-p4-ota-manifest-stable-v1.json" --note "Updated main firmware"
```

### Update Wireless Firmware Only

```bat
python .\generate_manifest.py --release 1.0.2 --channel stable --firmware-file "device-v1.0-esp32c6-rev0.0=.\input\network_adapter.bin" --previous-manifest ".\output\t-display-p4\stable\v1.0.1\lilygobox-t-display-p4-ota-manifest-stable-v1.json" --note "Updated wireless firmware"
```

Unchanged files are inherited from the previous manifest in the same channel
and continue to reference the historical Release containing each BIN.

### Generate Alpha or Beta

First Alpha example:

```bat
python .\generate_manifest.py --channel alpha --firmware-file "device-v1.0-esp32p4-rev1.0=.\input\lilygobox-espidf.bin" --firmware-file "device-v1.0-esp32c6-rev0.0=.\input\network_adapter.bin" --note "1.1.0 alpha build"
```

First Beta example:

```bat
python .\generate_manifest.py --channel beta --firmware-file "device-v1.0-esp32p4-rev1.0=.\input\lilygobox-espidf.bin" --firmware-file "device-v1.0-esp32c6-rev0.0=.\input\network_adapter.bin" --note "1.1.0 public beta"
```

Later Alpha or Beta releases inherit unchanged files from the previous
Manifest in the same channel. The generator rejects mismatches such as a
stable channel with `-beta.N` or a Beta channel without a suffix. Each device
uses one hardware configuration file shared by all three channels.

## Add a Board or Chip Revision

Edit the matching file under `devices/`, such as `t-display-p4.json` or
`t-display-p4-air.json`:

1. Add a file ID with its trusted `chip` and `projectName`.
2. Add a target containing the new `deviceVersion` and complete chip
   `revision` values.
3. Map installation roles to file IDs through `components`.
4. Provide new BINs and inherit existing files from the previous manifest in
   the same channel.

As long as the roles remain the already-supported `main` and `wireless`, no
manifest format or device parser change is required. A new installation role
requires corresponding device-side installation logic.

## Publish

The first stable output resembles:

```text
output/t-display-p4/stable/v1.0.0/
├─ lilygobox-t-display-p4-ota-manifest-stable-v1.json
├─ lilygobox-t-display-p4-device-v1.0-esp32p4-rev1.0-v1.0.0.bin
└─ lilygobox-t-display-p4-device-v1.0-esp32c6-rev0.0-v2.12.3.bin
```

Create a versioned Draft Release with the matching tag and upload the new BIN
files printed by the generator. After every firmware asset is available,
upload or replace the manifest in the permanent `ota-alpha`, `ota-beta`, or
`ota-stable` channel-pointer Release. Channel-pointer Releases contain only
the current manifest; versioned Releases contain immutable BIN assets.

Use `--force` only to replace local output that has not been published.
