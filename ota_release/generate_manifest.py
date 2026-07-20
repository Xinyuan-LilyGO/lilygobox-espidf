#!/usr/bin/env python3
"""生成 LilygoBox 多组件 OTA Release 文件。"""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import shutil
import sys
from pathlib import Path
from typing import Any


MANIFEST_VERSION = 1
MAX_FIRMWARE_ASSET_SIZE = 64 * 1024 * 1024
MAX_VERSION_NUMBER = (1 << 32) - 1
SEMANTIC_VERSION_PATTERN = re.compile(r"^[0-9]+\.[0-9]+\.[0-9]+$")
SHA256_PATTERN = re.compile(r"^[0-9a-f]{64}$")


def normalize_version(value: str, name: str) -> str:
    """统一并校验三段式版本号。"""
    version = value.strip()
    if version.startswith("v"):
        version = version[1:]
    if not SEMANTIC_VERSION_PATTERN.fullmatch(version):
        raise ValueError(f"{name} 必须使用 major.minor.patch，例如 1.2.3")
    if len(version) > 30 or any(
        int(part) > MAX_VERSION_NUMBER for part in version.split(".")
    ):
        raise ValueError(f"{name} 超出设备支持的版本范围")
    return version


def version_key(version: str) -> tuple[int, int, int]:
    """把已经校验的三段式版本转换为可比较数值。"""
    return tuple(int(part) for part in version.split("."))


def calculate_sha256(path: Path) -> str:
    """以只读方式分块计算文件 SHA-256。"""
    digest = hashlib.sha256()
    with path.open("rb") as firmware_file:
        while chunk := firmware_file.read(1024 * 1024):
            digest.update(chunk)
    return digest.hexdigest()


def load_json(path: Path) -> dict[str, Any]:
    """读取 UTF-8 JSON 对象。"""
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise ValueError(f"无法读取 JSON：{path}: {error}") from error
    if not isinstance(value, dict):
        raise ValueError(f"JSON 根节点必须是对象：{path}")
    return value


def parse_component_argument(value: str) -> tuple[str, str, Path]:
    """解析 component=version=path 形式的组件参数。"""
    parts = value.split("=", 2)
    if len(parts) != 3 or not all(parts):
        raise argparse.ArgumentTypeError(
            "组件参数必须是 component=version=path"
        )
    component, version, firmware_path = parts
    try:
        normalized_version = normalize_version(version, component)
    except ValueError as error:
        raise argparse.ArgumentTypeError(str(error)) from error
    return component, normalized_version, Path(firmware_path)


def validate_release_url(
    url: str, repository: str, asset_name: str
) -> bool:
    """确认组件 URL 属于目标仓库的合法版本 Release。"""
    prefix = f"https://github.com/{repository}/releases/download/"
    if not url.startswith(prefix):
        return False
    remainder = url[len(prefix) :]
    parts = remainder.split("/")
    return (
        len(parts) == 2
        and parts[0].startswith("v")
        and SEMANTIC_VERSION_PATTERN.fullmatch(parts[0][1:]) is not None
        and parts[1] == asset_name
    )


def validate_component(
    name: str,
    value: Any,
    repository: str,
    asset_name: str,
) -> dict[str, Any]:
    """校验清单中的一个组件并返回规范字段。"""
    if not isinstance(value, dict):
        raise ValueError(f"组件 {name} 必须是对象")
    version = value.get("version")
    url = value.get("url")
    size_bytes = value.get("size_bytes")
    sha256 = value.get("sha256")
    if not isinstance(version, str):
        raise ValueError(f"组件 {name}.version 无效")
    version = normalize_version(version, f"{name}.version")
    if not isinstance(url, str) or not validate_release_url(
        url, repository, asset_name
    ):
        raise ValueError(f"组件 {name}.url 无效")
    if not isinstance(size_bytes, int) or isinstance(size_bytes, bool):
        raise ValueError(f"组件 {name}.size_bytes 必须是正整数")
    if size_bytes <= 0:
        raise ValueError(f"组件 {name}.size_bytes 必须大于 0")
    if not isinstance(sha256, str) or not SHA256_PATTERN.fullmatch(
        sha256.lower()
    ):
        raise ValueError(f"组件 {name}.sha256 无效")
    return {
        "version": version,
        "url": url,
        "size_bytes": size_bytes,
        "sha256": sha256.lower(),
    }


def validate_previous_manifest(
    manifest: dict[str, Any], config: dict[str, Any]
) -> dict[str, dict[str, Any]]:
    """校验上一版清单并提取可继承组件。"""
    if manifest.get("manifest_version") != MANIFEST_VERSION:
        raise ValueError("上一版清单 manifest_version 不受支持")
    if manifest.get("device_id") != config["device_id"]:
        raise ValueError("上一版清单属于其他设备")
    release = manifest.get("release")
    if not isinstance(release, str) or not release.startswith("v"):
        raise ValueError("上一版清单 release 无效")
    normalize_version(release[1:], "上一版清单 release")
    repository = config["repository"]
    inherited: dict[str, dict[str, Any]] = {}
    for name, component_config in config["components"].items():
        inherited[name] = validate_component(
            name,
            manifest.get(name),
            repository,
            component_config["asset_name"],
        )
    return inherited


def validate_config(config: dict[str, Any]) -> None:
    """校验设备配置中的固定标识和资源名。"""
    required_strings = (
        "device_id",
        "repository",
        "manifest_asset_name",
    )
    for name in required_strings:
        if not isinstance(config.get(name), str) or not config[name]:
            raise ValueError(f"设备配置缺少 {name}")
    components = config.get("components")
    if not isinstance(components, dict) or not components:
        raise ValueError("设备配置必须包含 components")
    for name, component in components.items():
        if not isinstance(component, dict) or not isinstance(
            component.get("asset_name"), str
        ):
            raise ValueError(f"设备组件 {name} 缺少 asset_name")


def validate_notes(notes: list[str]) -> list[str]:
    """限制设备界面能够显示的更新说明数量和 UTF-8 长度。"""
    if len(notes) > 3:
        raise ValueError("更新说明最多只能提供三条")
    validated: list[str] = []
    for note in notes:
        text = note.strip()
        if not text or len(text.encode("utf-8")) >= 128:
            raise ValueError("每条更新说明必须是 1 至 127 个 UTF-8 字节")
        validated.append(text)
    return validated


def copy_and_describe_component(
    source: Path,
    destination: Path,
    version: str,
    repository: str,
    release_tag: str,
    asset_name: str,
) -> dict[str, Any]:
    """复制本次更新固件并生成对应清单字段。"""
    source = source.expanduser().resolve()
    if not source.is_file():
        raise ValueError(f"固件文件不存在：{source}")
    source_size = source.stat().st_size
    if source_size <= 0:
        raise ValueError(f"固件文件为空：{source}")
    if source_size > MAX_FIRMWARE_ASSET_SIZE:
        raise ValueError(f"固件文件超过 64 MiB 限制：{source}")
    destination.parent.mkdir(parents=True, exist_ok=True)
    if source != destination.resolve():
        shutil.copy2(source, destination)
    size_bytes = destination.stat().st_size
    if size_bytes != source_size:
        raise ValueError(f"固件复制后的长度不一致：{source}")
    return {
        "version": version,
        "url": (
            f"https://github.com/{repository}/releases/download/"
            f"{release_tag}/{asset_name}"
        ),
        "size_bytes": size_bytes,
        "sha256": calculate_sha256(destination),
    }


def clear_generated_files(output_directory: Path, config: dict[str, Any]) -> None:
    """重新生成同一版本时只删除工具自身会创建的文件。"""
    generated_names = {config["manifest_asset_name"]}
    generated_names.update(
        component["asset_name"] for component in config["components"].values()
    )
    for name in generated_names:
        path = output_directory / name
        if path.is_file():
            path.unlink()


def build_argument_parser(default_config: Path) -> argparse.ArgumentParser:
    """创建命令行参数解析器。"""
    parser = argparse.ArgumentParser(
        description="生成 LilygoBox OTA manifest 和待上传固件"
    )
    parser.add_argument("--release", required=True, help="Release 版本，例如 1.2.0")
    parser.add_argument(
        "--component",
        action="append",
        default=[],
        type=parse_component_argument,
        metavar="NAME=VERSION=BIN",
        help="本次更新组件，可重复传入",
    )
    parser.add_argument(
        "--previous-manifest",
        type=Path,
        help="未更新组件需要继承的上一版 manifest",
    )
    parser.add_argument(
        "--note", action="append", default=[], help="更新说明，最多三条"
    )
    parser.add_argument(
        "--config",
        type=Path,
        default=default_config,
        help="设备配置 JSON",
    )
    parser.add_argument(
        "--output-root",
        type=Path,
        help="输出根目录，默认 ota_release/output",
    )
    parser.add_argument(
        "--force", action="store_true", help="允许覆盖同版本的已生成文件"
    )
    return parser


def main() -> int:
    """生成仅包含本次需要上传资源的 Release 目录。"""
    script_directory = Path(__file__).resolve().parent
    default_config = script_directory / "devices" / "t-display-p4.json"
    parser = build_argument_parser(default_config)
    args = parser.parse_args()

    try:
        release_version = normalize_version(args.release, "release")
        release_tag = f"v{release_version}"
        config = load_json(args.config.expanduser().resolve())
        validate_config(config)
        component_updates: dict[str, tuple[str, Path]] = {}
        for name, version, path in args.component:
            if name not in config["components"]:
                raise ValueError(f"设备不支持组件：{name}")
            if name in component_updates:
                raise ValueError(f"组件重复：{name}")
            component_updates[name] = (version, path)
        if not component_updates:
            raise ValueError("本次至少需要提供一个 --component")

        inherited: dict[str, dict[str, Any]] = {}
        if args.previous_manifest is not None:
            previous = load_json(args.previous_manifest.expanduser().resolve())
            inherited = validate_previous_manifest(previous, config)
            previous_release = normalize_version(
                previous["release"][1:], "上一版清单 release"
            )
            if version_key(release_version) <= version_key(previous_release):
                raise ValueError("新 Release 版本必须高于上一版清单")
            for name, (version, _) in component_updates.items():
                if version_key(version) <= version_key(inherited[name]["version"]):
                    raise ValueError(f"组件 {name} 的新版本必须高于上一版")

        missing = [
            name
            for name in config["components"]
            if name not in component_updates and name not in inherited
        ]
        if missing:
            raise ValueError(
                "缺少组件且没有可继承的上一版清单：" + ", ".join(missing)
            )

        output_root = (
            args.output_root.expanduser().resolve()
            if args.output_root is not None
            else script_directory / "output"
        )
        output_directory = output_root / config["device_id"] / release_tag
        if output_directory.exists() and any(output_directory.iterdir()):
            if not args.force:
                raise ValueError(
                    f"输出目录非空，请更换版本或使用 --force：{output_directory}"
                )
            clear_generated_files(output_directory, config)
        output_directory.mkdir(parents=True, exist_ok=True)

        repository = config["repository"]
        manifest: dict[str, Any] = {
            "manifest_version": MANIFEST_VERSION,
            "device_id": config["device_id"],
            "release": release_tag,
        }
        upload_files: list[Path] = []
        for name, component_config in config["components"].items():
            asset_name = component_config["asset_name"]
            if name in component_updates:
                version, source = component_updates[name]
                destination = output_directory / asset_name
                manifest[name] = copy_and_describe_component(
                    source,
                    destination,
                    version,
                    repository,
                    release_tag,
                    asset_name,
                )
                upload_files.append(destination)
            else:
                manifest[name] = inherited[name]

        manifest["whats_new"] = validate_notes(args.note)
        manifest_path = output_directory / config["manifest_asset_name"]
        manifest_path.write_text(
            json.dumps(manifest, ensure_ascii=False, indent=2) + "\n",
            encoding="utf-8",
        )
        validate_previous_manifest(load_json(manifest_path), config)
        upload_files.insert(0, manifest_path)

        print(f"Release 文件已生成：{output_directory}")
        print("请上传以下文件：")
        for path in upload_files:
            print(f"  {path.name} ({path.stat().st_size} bytes)")
        return 0
    except ValueError as error:
        print(f"错误：{error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
