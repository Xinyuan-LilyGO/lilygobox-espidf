<h1 align="center">LilygoBox OTA Release Tool</h1>

## **English | [Chinese](./README_CN.md)**

[![Release](https://img.shields.io/github/v/release/Xinyuan-LilyGO/lilygobox-espidf?style=flat-square)](https://github.com/Xinyuan-LilyGO/lilygobox-espidf/releases)
[![Python](https://img.shields.io/badge/Python-3.9%2B-3776ab?style=flat-square)](https://www.python.org/)

The `ota_release` tool creates validated GitHub Release assets for LilygoBox devices with multiple firmware components. It supports combined ESP32-P4 and ESP32-C6 releases as well as P4-only or C6-only updates.

## Table of Contents

- [Requirements](#requirements)
- [Directory Structure](#directory-structure)
- [Manifest Fields](#manifest-fields)
- [Usage](#usage)
- [Publish to GitHub Releases](#publish-to-github-releases)
- [Add Another Device](#add-another-device)

## Requirements

- Python 3.9 or later
- No third-party Python packages
- ESP32-P4 application image placed at `input/lilygobox-espidf.bin`
- ESP32-C6 image with the embedded project name `network_adapter`

Do not use a merged or factory image as the ESP32-P4 OTA input. The ESP32-C6 input may be located in any directory.

## Directory Structure

Place the P4 and C6 binaries in `input`, then run the script. The script creates the output directory and release files automatically:

```text
ota_release/
├─ devices/
│  └─ t-display-p4.json
├─ input/                          User-provided and ignored by Git
│  ├─ lilygobox-espidf.bin
│  └─ network_adapter.bin
├─ output/                         Generated and ignored by Git
│  └─ t_display_p4/
│     └─ v1.0.1/
├─ .gitignore
├─ generate_manifest.py
├─ README.md
└─ README_CN.md
```

Generated files are stored in `output/<device_id>/<Release tag>/`. The included `.gitignore` prevents these temporary release assets from being committed.

## Manifest Fields

| Field | Description |
| --- | --- |
| `manifest_version` | Manifest format version understood by the device |
| `device_id` | Target hardware identifier |
| `release` | GitHub Release tag, such as `v1.0.1` |
| `version` | Three-part component firmware version |
| `url` | Exact GitHub Release URL for the component asset |
| `size_bytes` | Exact binary size calculated by Python |
| `sha256` | SHA-256 calculated from the binary without modifying it |
| `whats_new` | Up to three short update notes |

## Usage

Run all commands from the `lilygobox-espidf/ota_release` directory. From the project directory, enter it with:

```bat
cd .\ota_release
```

The examples use one-line commands so they can be pasted directly into either Command Prompt or PowerShell.

### First Release

The first manifest has no previous component information, so both P4 and C6 binaries are required:

```bat
python .\generate_manifest.py --release 1.0.0 --component "esp32p4=1.0.0=.\input\lilygobox-espidf.bin" --component "esp32c6=2.12.3=.\input\network_adapter.bin" --note "Initial LilygoBox firmware release"
```

### P4-Only Release

When C6 has not changed, provide only the new P4 binary and the previous manifest:

```bat
python .\generate_manifest.py --release 1.0.1 --component "esp32p4=1.0.1=.\input\lilygobox-espidf.bin" --previous-manifest ".\output\t_display_p4\v1.0.0\lilygobox-t-display-p4-ota-manifest.json" --note "Updated main firmware"
```

The script inherits the C6 version, historical Release URL, size, and SHA-256. Upload only the manifest and P4 binary printed by the script.

### C6-Only Release

```bat
python .\generate_manifest.py --release 1.0.2 --component "esp32c6=2.12.4=.\input\network_adapter.bin" --previous-manifest ".\output\t_display_p4\v1.0.1\lilygobox-t-display-p4-ota-manifest.json" --note "Updated wireless firmware"
```

The script inherits the P4 information. Upload only the manifest and C6 binary printed by the script.

If the previous local output is unavailable, download the manifest asset from the previous GitHub Release and pass its path to `--previous-manifest`.

### Regenerate an Unpublished Release

The script stops when the target output directory is not empty. To intentionally regenerate the same unpublished version, add:

```bat
--force
```

When a previous manifest is used, the new Release version and each changed component version must be higher than their previous values.

## Publish to GitHub Releases

For a P4-only `v1.0.1` release, the generated directory looks like this:

```text
ota_release/output/t_display_p4/v1.0.1/
├─ lilygobox-t-display-p4-ota-manifest.json
└─ lilygobox-t-display-p4-esp32p4.bin
```

1. Create a Draft Release in the [lilygobox-espidf Releases page](https://github.com/Xinyuan-LilyGO/lilygobox-espidf/releases).
2. Use the same tag printed in the output path, such as `v1.0.1`.
3. Upload exactly the files listed by the script.
4. Verify the manifest, component versions, and update notes.
5. Mark the release as Latest and publish it only after every required asset has been uploaded.

Do not publish the manifest first and add its changed binary later. Devices may discover the incomplete release immediately.

## Add Another Device

Copy `devices/t-display-p4.json` and change:

- `device_id`
- GitHub `repository`
- Manifest asset name
- Fixed asset name for each firmware component

Select the new file with `--config`. The device firmware must also define the matching manifest URL and `device_id`; adding only the generator configuration does not enable OTA for a new board.
