#!/usr/bin/env python3
"""生成 LilygoBox 多组件 OTA Release 文件。"""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import shutil
import struct
import sys
from datetime import datetime
from pathlib import Path
from typing import Any, Optional


SUPPORTED_SCHEMA_VERSION = 1
RELEASE_CHANNELS = ("stable", "beta", "dev")
MAX_FIRMWARE_ASSET_SIZE = 64 * 1024 * 1024
MAX_DOWNLOAD_SOURCE_COUNT = 4
MAX_DOWNLOAD_URL_LENGTH = 383
MAX_MANIFEST_SIZE = 8 * 1024
MAX_VERSION_NUMBER = (1 << 32) - 1
ESP_IMAGE_MAGIC = 0xE9
ESP_APP_DESCRIPTION_MAGIC = 0xABCD5432
ESP_IMAGE_HEADER_SIZE = 24
ESP_IMAGE_SEGMENT_HEADER_SIZE = 8
ESP_APP_DESCRIPTION_TEXT_LENGTH = 32
MAX_ESP_IMAGE_SEGMENT_COUNT = 16
SUPPORTED_ESP_CHIP_IDS = {
    "esp32c5": 23,
    "esp32c6": 13,
    "esp32p4": 18,
}
SEMANTIC_VERSION_PATTERN = re.compile(r"^[0-9]+\.[0-9]+\.[0-9]+$")
DEVICE_VERSION_PATTERN = re.compile(r"^v[0-9]+\.[0-9]+$")
DEVICE_ID_PATTERN = re.compile(r"^[a-z0-9]+(?:-[a-z0-9]+)*$")
COMPONENT_NAME_PATTERN = re.compile(r"^[a-z0-9]+(?:-[a-z0-9]+)*$")
SHA256_PATTERN = re.compile(r"^[0-9a-f]{64}$")
RELEASE_TIME_PATTERN = re.compile(
    r"^[0-9]{4}-[0-9]{2}-[0-9]{2}T[0-9]{2}:[0-9]{2}[+-][0-9]{2}:[0-9]{2}$"
)


def decode_app_description_text(value: bytes) -> Optional[str]:
    """读取 esp_app_desc_t 中以 NUL 结尾的 ASCII 文本。"""
    try:
        text = value.split(b"\0", 1)[0].decode("ascii")
    except UnicodeDecodeError:
        return None
    return text if text else None


def calculate_esp_image_size(data: bytes) -> Optional[int]:
    """校验 ESP 镜像段布局并返回镜像完整长度。"""
    if ESP_IMAGE_HEADER_SIZE > len(data):
        return None
    segment_count = data[1]
    if not 1 <= segment_count <= MAX_ESP_IMAGE_SEGMENT_COUNT:
        return None
    cursor = ESP_IMAGE_HEADER_SIZE
    total_size = ESP_IMAGE_HEADER_SIZE
    for _ in range(segment_count):
        if cursor + ESP_IMAGE_SEGMENT_HEADER_SIZE > len(data):
            return None
        segment_size = struct.unpack_from("<I", data, cursor + 4)[0]
        cursor += ESP_IMAGE_SEGMENT_HEADER_SIZE
        total_size += ESP_IMAGE_SEGMENT_HEADER_SIZE
        if segment_size > len(data) - cursor:
            return None
        cursor += segment_size
        total_size += segment_size
    total_size += 16 - total_size % 16
    if data[23] == 1:
        total_size += 32
    if total_size > len(data):
        return None
    return total_size


def read_esp_application(data: bytes) -> Optional[dict[str, Any]]:
    """读取占满整个文件的未合并 ESP 应用镜像。"""
    minimum_size = (
        ESP_IMAGE_HEADER_SIZE
        + ESP_IMAGE_SEGMENT_HEADER_SIZE
        + 16
        + ESP_APP_DESCRIPTION_TEXT_LENGTH * 2
    )
    if len(data) < minimum_size or data[0] != ESP_IMAGE_MAGIC:
        return None
    description_offset = ESP_IMAGE_HEADER_SIZE + ESP_IMAGE_SEGMENT_HEADER_SIZE
    if (
        struct.unpack_from("<I", data, description_offset)[0]
        != ESP_APP_DESCRIPTION_MAGIC
    ):
        return None
    image_size = calculate_esp_image_size(data)
    if image_size != len(data):
        return None
    version = decode_app_description_text(
        data[
            description_offset
            + 16 : description_offset
            + 16
            + ESP_APP_DESCRIPTION_TEXT_LENGTH
        ]
    )
    project_name = decode_app_description_text(
        data[
            description_offset
            + 48 : description_offset
            + 48
            + ESP_APP_DESCRIPTION_TEXT_LENGTH
        ]
    )
    if version is None or project_name is None:
        return None
    return {
        "chip_id": struct.unpack_from("<H", data, 12)[0],
        "image_size": image_size,
        "project_name": project_name,
        "version": version,
    }


def validate_firmware_application(
    path: Path,
    selector: str,
    version: str,
    component_config: dict[str, str],
) -> None:
    """检查 BIN 内嵌芯片、项目名和版本是否符合设备配置。"""
    try:
        data = path.read_bytes()
    except OSError as error:
        raise ValueError(f"无法读取固件文件：{path}: {error}") from error
    expected_chip = component_config["chip"]
    expected_chip_id = SUPPORTED_ESP_CHIP_IDS[expected_chip]
    expected_project_name = component_config["project_name"]
    application = read_esp_application(data)
    if application is None:
        raise ValueError(
            f"组件 {selector} 必须是从偏移 0 开始且不包含额外数据的"
            "未合并 ESP 应用 BIN"
        )
    if (
        application["chip_id"] != expected_chip_id
        or application["project_name"] != expected_project_name
    ):
        raise ValueError(
            f"组件 {selector} 的 BIN 不符合 chip={expected_chip}、"
            f"project_name={expected_project_name}："
            f"chip_id={application['chip_id']} "
            f"project_name={application['project_name']}"
        )
    embedded_version = application["version"]
    if embedded_version != version:
        raise ValueError(
            f"组件 {selector} 声明版本为 {version}，"
            f"但 BIN 内嵌版本为 {embedded_version}"
        )


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


def current_release_time() -> str:
    """生成包含本地时区且精确到分钟的 Release 时间。"""
    return datetime.now().astimezone().isoformat(timespec="minutes")


def validate_release_time(value: Any) -> str:
    """校验带时区且精确到分钟的 ISO 8601 Release 时间。"""
    if not isinstance(value, str) or not RELEASE_TIME_PATTERN.fullmatch(value):
        raise ValueError("release_time 必须是带时区的分钟时间")
    try:
        datetime.fromisoformat(value)
    except ValueError as error:
        raise ValueError("release_time 日期或时间无效") from error
    return value


def validate_release_channel(value: Any) -> str:
    """校验固件发布频道。"""
    if not isinstance(value, str) or value not in RELEASE_CHANNELS:
        raise ValueError("channel 必须是 stable、beta 或 dev")
    return value


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


def parse_component_argument(value: str) -> tuple[str, str, str, Path]:
    """解析 device/component=version=path 形式的组件参数。"""
    parts = value.split("=", 2)
    if len(parts) != 3 or not all(parts):
        raise argparse.ArgumentTypeError(
            "组件参数必须是 hardware/component=version=path"
        )
    selector, version, firmware_path = parts
    selector_parts = selector.split("/", 1)
    if len(selector_parts) != 2:
        raise argparse.ArgumentTypeError(
            "组件必须包含设备版本，例如 v1.0/main"
        )
    device_version, component = selector_parts
    if (
        not DEVICE_VERSION_PATTERN.fullmatch(device_version)
        or not COMPONENT_NAME_PATTERN.fullmatch(component)
    ):
        raise argparse.ArgumentTypeError(f"组件选择器无效：{selector}")
    try:
        normalized_version = normalize_version(version, selector)
    except ValueError as error:
        raise argparse.ArgumentTypeError(str(error)) from error
    return device_version, component, normalized_version, Path(firmware_path)


def extract_release_tag(
    url: str, repository: str, asset_name: str
) -> Optional[str]:
    """从组件的 GitHub 主下载地址提取 Release tag。"""
    prefix = f"https://github.com/{repository}/releases/download/"
    if not url.startswith(prefix):
        return None
    remainder = url[len(prefix) :]
    parts = remainder.split("/")
    if not (
        len(parts) == 2
        and parts[0].startswith("v")
        and SEMANTIC_VERSION_PATTERN.fullmatch(parts[0][1:]) is not None
        and parts[1] == asset_name
    ):
        return None
    return parts[0]


def is_supported_download_url(value: Any) -> bool:
    """检查设备 HTTP 客户端可以直接使用的 HTTPS 地址。"""
    if not isinstance(value, str) or not value.startswith("https://"):
        return False
    try:
        encoded = value.encode("ascii")
    except UnicodeEncodeError:
        return False
    return 8 < len(encoded) <= MAX_DOWNLOAD_URL_LENGTH


def build_download_urls(
    templates: list[str],
    repository: str,
    release_tag: str,
    asset_name: str,
) -> list[str]:
    """按设备配置顺序生成完整固件下载地址。"""
    urls: list[str] = []
    for template in templates:
        try:
            url = template.format(
                repository=repository,
                release=release_tag,
                asset_name=asset_name,
            )
        except (KeyError, ValueError) as error:
            raise ValueError(f"下载地址模板无效：{template}") from error
        if not is_supported_download_url(url):
            raise ValueError(f"下载地址必须是受支持长度的 HTTPS 地址：{url}")
        if url in urls:
            raise ValueError(f"下载地址重复：{url}")
        urls.append(url)
    return urls


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
    urls = value.get("urls")
    size_bytes = value.get("size_bytes")
    sha256 = value.get("sha256")
    if not isinstance(version, str):
        raise ValueError(f"组件 {name}.version 无效")
    version = normalize_version(version, f"{name}.version")
    if (
        not isinstance(urls, list)
        or not 1 <= len(urls) <= MAX_DOWNLOAD_SOURCE_COUNT
    ):
        raise ValueError(
            f"组件 {name}.urls 必须包含 1 至 "
            f"{MAX_DOWNLOAD_SOURCE_COUNT} 个下载地址"
        )
    validated_urls: list[str] = []
    for url in urls:
        if not is_supported_download_url(url):
            raise ValueError(f"组件 {name}.urls 包含无效 HTTPS 地址")
        if url in validated_urls:
            raise ValueError(f"组件 {name}.urls 包含重复地址")
        validated_urls.append(url)
    if extract_release_tag(validated_urls[0], repository, asset_name) is None:
        raise ValueError(f"组件 {name}.urls 的第一个地址不是 GitHub 主地址")
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
        "urls": validated_urls,
        "size_bytes": size_bytes,
        "sha256": sha256.lower(),
    }


def validate_previous_manifest(
    manifest: dict[str, Any], config: dict[str, Any]
) -> dict[tuple[str, str], dict[str, Any]]:
    """校验上一版清单并提取可继承组件。"""
    if manifest.get("schema_version") != config["schema_version"]:
        raise ValueError("上一版清单 schema_version 不受支持")
    if manifest.get("device_id") != config["device_id"]:
        raise ValueError("上一版清单属于其他设备")
    release = manifest.get("release")
    if not isinstance(release, str) or not release.startswith("v"):
        raise ValueError("上一版清单 release 无效")
    normalize_version(release[1:], "上一版清单 release")
    release_time = manifest.get("release_time")
    if release_time is not None:
        validate_release_time(release_time)
    validate_release_channel(manifest.get("channel", "stable"))
    repository = config["repository"]
    templates = config["download_url_templates"]
    device_versions = manifest.get("device_versions")
    if not isinstance(device_versions, dict) or not device_versions:
        raise ValueError("上一版清单缺少 device_versions")
    configured_versions = set(config["device_versions"])
    unknown_versions = set(device_versions) - configured_versions
    if unknown_versions:
        raise ValueError(
            "设备配置缺少上一版设备版本："
            + ", ".join(sorted(unknown_versions))
        )
    inherited: dict[tuple[str, str], dict[str, Any]] = {}
    for device_version, component_configs in config[
        "device_versions"
    ].items():
        device = device_versions.get(device_version)
        if device is None:
            continue
        if not isinstance(device, dict):
            raise ValueError(f"设备版本 {device_version} 必须是对象")
        unknown_components = set(device) - set(component_configs)
        if unknown_components:
            raise ValueError(
                f"设备版本 {device_version} 包含未知组件："
                + ", ".join(sorted(unknown_components))
            )
        for component_name in component_configs:
            value = device.get(component_name)
            if value is None:
                continue
            selector = f"{device_version}/{component_name}"
            asset_name = build_component_filename(
                config, device_version, component_name
            )
            component = validate_component(
                selector, value, repository, asset_name
            )
            source_release = extract_release_tag(
                component["urls"][0], repository, asset_name
            )
            if source_release is None:
                raise ValueError(f"组件 {selector} 的 GitHub 主地址无效")
            component["urls"] = build_download_urls(
                templates, repository, source_release, asset_name
            )
            inherited[(device_version, component_name)] = component
    return inherited


def validate_config(config: dict[str, Any]) -> None:
    """校验设备配置中的固定标识和资源名。"""
    required_strings = (
        "device_id",
        "repository",
    )
    for name in required_strings:
        if not isinstance(config.get(name), str) or not config[name]:
            raise ValueError(f"设备配置缺少 {name}")
    if not DEVICE_ID_PATTERN.fullmatch(config["device_id"]):
        raise ValueError("device_id 只能包含小写字母、数字和单个连字符")
    if len(config["device_id"]) >= 32:
        raise ValueError("device_id 最多只能包含 31 个字符")
    schema_version = config.get("schema_version")
    if (
        not isinstance(schema_version, int)
        or isinstance(schema_version, bool)
        or schema_version <= 0
    ):
        raise ValueError("schema_version 必须是正整数")
    if schema_version != SUPPORTED_SCHEMA_VERSION:
        raise ValueError(
            f"生成器仅支持 schema_version "
            f"{SUPPORTED_SCHEMA_VERSION}"
        )
    templates = config.get("download_url_templates")
    if (
        not isinstance(templates, list)
        or not 1 <= len(templates) <= MAX_DOWNLOAD_SOURCE_COUNT
        or any(
            not isinstance(template, str)
            or "{release}" not in template
            or "{asset_name}" not in template
            for template in templates
        )
    ):
        raise ValueError(
            f"download_url_templates 必须包含 1 至 "
            f"{MAX_DOWNLOAD_SOURCE_COUNT} 个模板"
        )
    build_download_urls(
        templates,
        config["repository"],
        "v1.0.0",
        "firmware.bin",
    )
    device_versions = config.get("device_versions")
    if not isinstance(device_versions, dict) or not device_versions:
        raise ValueError("设备配置必须包含 device_versions")
    for device_version, component_configs in device_versions.items():
        if (
            not isinstance(device_version, str)
            or not DEVICE_VERSION_PATTERN.fullmatch(device_version)
        ):
            raise ValueError(
                "device_versions 必须使用 v<major>.<minor> 格式"
            )
        if not isinstance(component_configs, dict) or not component_configs:
            raise ValueError(
                f"设备版本 {device_version} 必须至少配置一个组件"
            )
        for component_name, component_config in component_configs.items():
            if (
                not isinstance(component_name, str)
                or not COMPONENT_NAME_PATTERN.fullmatch(component_name)
            ):
                raise ValueError(
                    f"设备版本 {device_version} 的组件名称无效"
                )
            if not isinstance(component_config, dict):
                raise ValueError(
                    f"组件 {device_version}/{component_name} 的配置必须是对象"
                )
            required_fields = {"chip", "project_name"}
            if set(component_config) != required_fields:
                raise ValueError(
                    f"组件 {device_version}/{component_name} 必须且只能包含 "
                    "chip 和 project_name"
                )
            chip = component_config["chip"]
            if chip not in SUPPORTED_ESP_CHIP_IDS:
                raise ValueError(
                    f"组件 {device_version}/{component_name} 的 chip 不受支持"
                )
            project_name = component_config["project_name"]
            if not isinstance(project_name, str):
                raise ValueError(
                    f"组件 {device_version}/{component_name} 的 "
                    "project_name 必须是字符串"
                )
            try:
                encoded_project_name = project_name.encode("ascii")
            except UnicodeEncodeError as error:
                raise ValueError(
                    f"组件 {device_version}/{component_name} 的 "
                    "project_name 必须是 ASCII 文本"
                ) from error
            if (
                not 1 <= len(encoded_project_name) < 32
                or any(
                    character < 0x21 or character > 0x7E
                    for character in encoded_project_name
                )
            ):
                raise ValueError(
                    f"组件 {device_version}/{component_name} 的 "
                    "project_name 必须是 1 至 31 个可见 ASCII 字符"
                )


def build_manifest_filename(config: dict[str, Any]) -> str:
    """根据设备标识和格式版本生成固定的 manifest 资产文件名。"""
    return (
        f"lilygobox-{config['device_id']}-ota-"
        f"manifest-v{config['schema_version']}.json"
    )


def build_component_filename(
    config: dict[str, Any],
    device_version: str,
    component_name: str,
) -> str:
    """根据设备、设备版本和组件职责生成固件资产文件名。"""
    return (
        f"lilygobox-{config['device_id']}-{device_version}-"
        f"{component_name}.bin"
    )


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
    download_url_templates: list[str],
    selector: str,
    component_config: dict[str, str],
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
    validate_firmware_application(
        source, selector, version, component_config
    )
    destination.parent.mkdir(parents=True, exist_ok=True)
    if source != destination.resolve():
        shutil.copy2(source, destination)
    size_bytes = destination.stat().st_size
    if size_bytes != source_size:
        raise ValueError(f"固件复制后的长度不一致：{source}")
    return {
        "version": version,
        "urls": build_download_urls(
            download_url_templates,
            repository,
            release_tag,
            asset_name,
        ),
        "size_bytes": size_bytes,
        "sha256": calculate_sha256(destination),
    }


def clear_generated_files(output_directory: Path, config: dict[str, Any]) -> None:
    """重新生成同一版本时只删除工具自身会创建的文件。"""
    generated_names = {build_manifest_filename(config)}
    for device_version, component_configs in config[
        "device_versions"
    ].items():
        for component_name in component_configs:
            generated_names.add(
                build_component_filename(
                    config, device_version, component_name
                )
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
        "--channel",
        choices=RELEASE_CHANNELS,
        default="stable",
        help="发布频道，默认 stable",
    )
    parser.add_argument(
        "--component",
        action="append",
        default=[],
        type=parse_component_argument,
        metavar="DEVICE/COMPONENT=VERSION=BIN",
        help="本次更新的硬件组件，可重复传入",
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
        component_updates: dict[tuple[str, str], tuple[str, Path]] = {}
        for device_version, component_name, version, path in args.component:
            if device_version not in config["device_versions"]:
                raise ValueError(
                    f"不支持设备版本：{device_version}"
                )
            if component_name not in config["device_versions"][
                device_version
            ]:
                raise ValueError(
                    f"设备版本 {device_version} 不支持组件："
                    f"{component_name}"
                )
            component_key = (device_version, component_name)
            if component_key in component_updates:
                raise ValueError(
                    f"组件重复：{device_version}/{component_name}"
                )
            component_updates[component_key] = (version, path)
        if not component_updates:
            raise ValueError("本次至少需要提供一个 --component")

        inherited: dict[tuple[str, str], dict[str, Any]] = {}
        if args.previous_manifest is not None:
            previous = load_json(args.previous_manifest.expanduser().resolve())
            inherited = validate_previous_manifest(previous, config)
            previous_release = normalize_version(
                previous["release"][1:], "上一版清单 release"
            )
            if version_key(release_version) <= version_key(previous_release):
                raise ValueError("新 Release 版本必须高于上一版清单")
            for component_key, (version, _) in component_updates.items():
                if component_key not in inherited:
                    continue
                if version_key(version) <= version_key(
                    inherited[component_key]["version"]
                ):
                    selector = "/".join(component_key)
                    raise ValueError(
                        f"组件 {selector} 的新版本必须高于上一版"
                    )

        missing = [
            f"{device_version}/{component_name}"
            for device_version, component_configs in config[
                "device_versions"
            ].items()
            for component_name in component_configs
            if (device_version, component_name) not in component_updates
            and (device_version, component_name) not in inherited
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
            "schema_version": config["schema_version"],
            "device_id": config["device_id"],
            "release": release_tag,
            "channel": args.channel,
            "release_time": current_release_time(),
            "device_versions": {},
        }
        upload_files: list[Path] = []
        for device_version, component_configs in config[
            "device_versions"
        ].items():
            device: dict[str, Any] = {}
            for component_name, component_config in component_configs.items():
                component_key = (device_version, component_name)
                asset_name = build_component_filename(
                    config, device_version, component_name
                )
                if component_key in component_updates:
                    version, source = component_updates[component_key]
                    destination = output_directory / asset_name
                    component = copy_and_describe_component(
                        source,
                        destination,
                        version,
                        repository,
                        release_tag,
                        asset_name,
                        config["download_url_templates"],
                        f"{device_version}/{component_name}",
                        component_config,
                    )
                    upload_files.append(destination)
                else:
                    component = inherited[component_key]
                component = {
                    key: value
                    for key, value in component.items()
                    if key not in ("chip", "project_name")
                }
                device[component_name] = {
                    "chip": component_config["chip"],
                    "project_name": component_config["project_name"],
                    **component,
                }
            manifest["device_versions"][device_version] = device

        manifest["whats_new"] = validate_notes(args.note)
        manifest_path = output_directory / build_manifest_filename(config)
        manifest_text = (
            json.dumps(manifest, ensure_ascii=False, indent=2) + "\n"
        )
        if len(manifest_text.encode("utf-8")) > MAX_MANIFEST_SIZE:
            raise ValueError("生成的 manifest 超过设备支持的 8 KiB")
        manifest_path.write_text(manifest_text, encoding="utf-8")
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
