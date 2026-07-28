#!/usr/bin/env python3
"""生成基于目标匹配和固件文件引用的 LilygoBox OTA Release 文件。"""

from __future__ import annotations

import argparse
import copy
import hashlib
import json
import re
import shutil
import struct
import sys
from datetime import datetime, timezone
from pathlib import Path
from typing import Any, Optional


SUPPORTED_MANIFEST_VERSION = "1.0"
DEVICE_CONFIG_KIND = "lilygobox#otaDeviceConfig"
MANIFEST_KIND = "lilygobox#otaManifest"
RELEASE_CHANNELS = ("alpha", "beta", "stable")
MAX_FIRMWARE_ASSET_SIZE = 64 * 1024 * 1024
MAX_DOWNLOAD_SOURCE_COUNT = 4
MAX_DOWNLOAD_URL_LENGTH = 383
MAX_MANIFEST_SIZE = 32 * 1024
MAX_TARGET_COUNT = 32
MAX_FIRMWARE_FILE_COUNT = 64
MAX_FIRMWARE_FILE_ID_LENGTH = 95
MAX_FIRMWARE_FILENAME_LENGTH = 159
MAX_VERSION_NUMBER = (1 << 32) - 1
ESP_IMAGE_MAGIC = 0xE9
ESP_APP_DESCRIPTION_MAGIC = 0xABCD5432
ESP_IMAGE_HEADER_SIZE = 24
ESP_IMAGE_SEGMENT_HEADER_SIZE = 8
ESP_IMAGE_MIN_CHIP_REVISION_OFFSET = 15
ESP_IMAGE_MAX_CHIP_REVISION_OFFSET = 17
ESP_APP_DESCRIPTION_TEXT_LENGTH = 32
MAX_ESP_IMAGE_SEGMENT_COUNT = 16
SUPPORTED_ESP_CHIP_IDS = {
    "esp32c5": 23,
    "esp32c6": 13,
    "esp32p4": 18,
}
SEMANTIC_VERSION_PATTERN = re.compile(
    r"^(?P<major>0|[1-9][0-9]*)\."
    r"(?P<minor>0|[1-9][0-9]*)\."
    r"(?P<patch>0|[1-9][0-9]*)"
    r"(?:-(?P<prerelease>alpha|beta)\."
    r"(?P<prerelease_number>0|[1-9][0-9]*))?$"
)
DEVICE_VERSION_PATTERN = re.compile(r"^[0-9]+\.[0-9]+$")
CHIP_REVISION_PATTERN = re.compile(r"^[0-9]+\.[0-9]+$")
RESOURCE_ID_PATTERN = re.compile(r"^[a-z0-9]+(?:-[a-z0-9]+)*$")
NAME_PATTERN = re.compile(r"^[a-z0-9]+(?:[a-z0-9.-]*[a-z0-9])?$")
REPOSITORY_PATTERN = re.compile(
    r"^[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+$"
)
SHA256_PATTERN = re.compile(r"^[0-9a-f]{64}$")
PUBLISH_TIME_PATTERN = re.compile(
    r"^[0-9]{4}-[0-9]{2}-[0-9]{2}T"
    r"[0-9]{2}:[0-9]{2}:[0-9]{2}Z$"
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
        "min_chip_revision": struct.unpack_from(
            "<H", data, ESP_IMAGE_MIN_CHIP_REVISION_OFFSET
        )[0],
        "max_chip_revision": struct.unpack_from(
            "<H", data, ESP_IMAGE_MAX_CHIP_REVISION_OFFSET
        )[0],
        "project_name": project_name,
        "version": version,
    }


def referenced_chip_revisions(
    config: dict[str, Any], file_id: str
) -> set[int]:
    """收集设备配置中引用指定固件文件的完整芯片修订号。"""
    revisions: set[int] = set()
    for target in config["targets"]:
        for role, referenced_file_id in target["components"].items():
            if referenced_file_id != file_id:
                continue
            revision = target["compatibility"]["chips"][role]["revision"]
            major, minor = (int(part) for part in revision.split("."))
            revisions.add(major * 100 + minor)
    return revisions


def validate_firmware_application(
    path: Path,
    file_id: str,
    version: str,
    config: dict[str, Any],
) -> None:
    """检查 BIN 内嵌芯片、项目名和版本是否符合固件文件配置。"""
    try:
        data = path.read_bytes()
    except OSError as error:
        raise ValueError(f"无法读取固件文件：{path}: {error}") from error
    file_config = config["files"][file_id]
    expected_chip = file_config["chip"]
    expected_chip_id = SUPPORTED_ESP_CHIP_IDS[expected_chip]
    expected_project_name = file_config["projectName"]
    application = read_esp_application(data)
    if application is None:
        raise ValueError(
            f"固件文件 {file_id} 必须是从偏移 0 开始且不包含额外数据的"
            "未合并 ESP 应用 BIN"
        )
    if (
        application["chip_id"] != expected_chip_id
        or application["project_name"] != expected_project_name
    ):
        raise ValueError(
            f"固件文件 {file_id} 的 BIN 不符合 chip={expected_chip}、"
            f"project_name={expected_project_name}："
            f"chip_id={application['chip_id']} "
            f"project_name={application['project_name']}"
        )
    embedded_version = application["version"]
    if embedded_version != version:
        raise ValueError(
            f"固件文件 {file_id} 声明版本为 {version}，"
            f"但 BIN 内嵌版本为 {embedded_version}"
        )
    minimum_revision = application["min_chip_revision"]
    maximum_revision = application["max_chip_revision"]
    if maximum_revision < minimum_revision:
        raise ValueError(
            f"固件文件 {file_id} 的 BIN 芯片修订范围无效"
        )
    for revision in referenced_chip_revisions(config, file_id):
        if minimum_revision <= revision <= maximum_revision:
            continue
        raise ValueError(
            f"固件文件 {file_id} 的 BIN 支持芯片修订范围为 "
            f"{minimum_revision // 100}.{minimum_revision % 100} 至 "
            f"{maximum_revision // 100}.{maximum_revision % 100}，"
            f"不能用于目标 {revision // 100}.{revision % 100}"
        )


def normalize_version(value: Any, name: str) -> str:
    """统一并校验稳定版或受支持的预发布版本号。"""
    if not isinstance(value, str):
        raise ValueError(
            f"{name} 必须使用 1.2.3、1.2.3-alpha.1 或 "
            "1.2.3-beta.1"
        )
    version = value.strip()
    if version.startswith("v"):
        version = version[1:]
    match = SEMANTIC_VERSION_PATTERN.fullmatch(version)
    if match is None:
        raise ValueError(
            f"{name} 必须使用 1.2.3、1.2.3-alpha.1 或 "
            "1.2.3-beta.1"
        )
    number_names = ("major", "minor", "patch", "prerelease_number")
    if len(version) > 30 or any(
        match.group(number_name) is not None
        and int(match.group(number_name)) > MAX_VERSION_NUMBER
        for number_name in number_names
    ):
        raise ValueError(f"{name} 超出设备支持的版本范围")
    return version


def version_key(version: str) -> tuple[int, int, int, int, int]:
    """把已经校验的版本转换为符合 SemVer 优先级的比较键。"""
    match = SEMANTIC_VERSION_PATTERN.fullmatch(version)
    if match is None:
        raise ValueError(f"版本格式无效：{version}")
    prerelease = match.group("prerelease")
    # 常见发布阶段按 Alpha、Beta、正式版依次递增。
    prerelease_rank = {
        "alpha": 0,
        "beta": 1,
        None: 2,
    }[prerelease]
    prerelease_number = match.group("prerelease_number")
    return (
        int(match.group("major")),
        int(match.group("minor")),
        int(match.group("patch")),
        prerelease_rank,
        int(prerelease_number) if prerelease_number is not None else 0,
    )


def current_publish_time() -> str:
    """生成使用 UTC 和秒精度的 RFC 3339 发布时间。"""
    return (
        datetime.now(timezone.utc)
        .isoformat(timespec="seconds")
        .replace("+00:00", "Z")
    )


def validate_publish_time(value: Any) -> str:
    """校验使用 UTC 和秒精度的 RFC 3339 发布时间。"""
    if not isinstance(value, str) or not PUBLISH_TIME_PATTERN.fullmatch(value):
        raise ValueError("publishTime 必须是 UTC 秒精度的 RFC 3339 时间")
    try:
        datetime.fromisoformat(value.replace("Z", "+00:00"))
    except ValueError as error:
        raise ValueError("publishTime 日期或时间无效") from error
    return value


def validate_release_channel(value: Any) -> str:
    """校验固件发布频道。"""
    if not isinstance(value, str) or value not in RELEASE_CHANNELS:
        raise ValueError("channel 必须是 alpha、beta 或 stable")
    return value


def validate_release_version_channel(
    version: str, channel: str, name: str
) -> None:
    """确保 Release 版本后缀与发布频道严格对应。"""
    match = SEMANTIC_VERSION_PATTERN.fullmatch(version)
    if match is None:
        raise ValueError(f"{name} 版本格式无效")
    expected_prerelease = {
        "alpha": "alpha",
        "beta": "beta",
        "stable": None,
    }[channel]
    if match.group("prerelease") != expected_prerelease:
        expected_format = {
            "alpha": "major.minor.patch-alpha.number",
            "beta": "major.minor.patch-beta.number",
            "stable": "major.minor.patch",
        }[channel]
        raise ValueError(
            f"{name} 必须为 {channel} 频道使用 {expected_format}"
        )


def main_component_file_ids(config: dict[str, Any]) -> set[str]:
    """返回设备全部目标引用的主固件文件 ID。"""
    return {
        target["components"]["main"]
        for target in config["targets"]
        if "main" in target["components"]
    }


def validate_main_component_versions(
    firmware_files: dict[str, dict[str, Any]],
    config: dict[str, Any],
    channel: str,
) -> None:
    """确保所有主固件版本都与 Manifest 频道一致。"""
    for file_id in main_component_file_ids(config):
        firmware_file = firmware_files.get(file_id)
        if firmware_file is None:
            continue
        validate_release_version_channel(
            firmware_file["version"],
            channel,
            f"固件文件 {file_id}.version",
        )


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
    except (OSError, UnicodeError, json.JSONDecodeError) as error:
        raise ValueError(f"无法读取 JSON：{path}: {error}") from error
    if not isinstance(value, dict):
        raise ValueError(f"JSON 根节点必须是对象：{path}")
    return value


def parse_firmware_file_argument(
    value: str,
) -> tuple[str, Optional[str], Path]:
    """解析 FILE_ID=PATH 或兼容的 FILE_ID=VERSION=PATH 参数。"""
    parts = value.split("=", 2)
    if len(parts) not in (2, 3) or not all(parts):
        raise argparse.ArgumentTypeError(
            "固件文件参数必须是 FILE_ID=BIN 或 FILE_ID=VERSION=BIN"
        )
    file_id = parts[0]
    if (
        not NAME_PATTERN.fullmatch(file_id)
        or len(file_id) > MAX_FIRMWARE_FILE_ID_LENGTH
    ):
        raise argparse.ArgumentTypeError(f"固件文件 ID 无效：{file_id}")
    if len(parts) == 2:
        return file_id, None, Path(parts[1])
    version, firmware_path = parts[1:]
    try:
        normalized_version = normalize_version(version, file_id)
    except ValueError as error:
        raise argparse.ArgumentTypeError(str(error)) from error
    return file_id, normalized_version, Path(firmware_path)


def read_embedded_firmware_version(path: Path, file_id: str) -> str:
    """从未合并 ESP 应用 BIN 的 esp_app_desc_t 中读取版本。"""
    source = path.expanduser().resolve()
    if not source.is_file():
        raise ValueError(f"固件文件不存在：{source}")
    source_size = source.stat().st_size
    if source_size <= 0:
        raise ValueError(f"固件文件为空：{source}")
    if source_size > MAX_FIRMWARE_ASSET_SIZE:
        raise ValueError(f"固件文件超过 64 MiB 限制：{source}")
    try:
        data = source.read_bytes()
    except OSError as error:
        raise ValueError(f"无法读取固件文件：{source}: {error}") from error
    application = read_esp_application(data)
    if application is None:
        raise ValueError(
            f"固件文件 {file_id} 必须是从偏移 0 开始且不包含额外数据的"
            "未合并 ESP 应用 BIN"
        )
    embedded_version = application["version"]
    version = normalize_version(
        embedded_version, f"固件文件 {file_id} 的 BIN 内嵌版本"
    )
    if embedded_version != version:
        raise ValueError(
            f"固件文件 {file_id} 的 BIN 内嵌版本必须使用不带 v 前缀的"
            f"规范格式：{version}"
        )
    return version


def extract_release_tag(
    url: str, repository: str, filename: str
) -> Optional[str]:
    """从固件文件的 GitHub 主下载地址提取 Release tag。"""
    prefix = f"https://github.com/{repository}/releases/download/"
    if not url.startswith(prefix):
        return None
    remainder = url[len(prefix) :]
    parts = remainder.split("/")
    if not (
        len(parts) == 2
        and parts[0].startswith("v")
        and SEMANTIC_VERSION_PATTERN.fullmatch(parts[0][1:]) is not None
        and parts[1] == filename
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
    return (
        8 < len(encoded) <= MAX_DOWNLOAD_URL_LENGTH
        and all(0x21 <= character <= 0x7E for character in encoded)
    )


def build_download_urls(
    templates: list[str],
    repository: str,
    release_tag: str,
    filename: str,
) -> list[str]:
    """按设备配置顺序生成完整固件下载地址。"""
    urls: list[str] = []
    for template in templates:
        try:
            url = template.format(
                repository=repository,
                release=release_tag,
                filename=filename,
            )
        except (KeyError, ValueError) as error:
            raise ValueError(f"下载地址模板无效：{template}") from error
        if not is_supported_download_url(url):
            raise ValueError(f"下载地址必须是受支持长度的 HTTPS 地址：{url}")
        if url in urls:
            raise ValueError(f"下载地址重复：{url}")
        urls.append(url)
    return urls


def validate_project_name(file_id: str, project_name: Any) -> str:
    """校验 ESP-IDF 应用描述中的项目名。"""
    if not isinstance(project_name, str):
        raise ValueError(f"固件文件 {file_id}.projectName 必须是字符串")
    try:
        encoded = project_name.encode("ascii")
    except UnicodeEncodeError as error:
        raise ValueError(
            f"固件文件 {file_id}.projectName 必须是 ASCII 文本"
        ) from error
    if (
        not 1 <= len(encoded) < 32
        or any(character < 0x21 or character > 0x7E for character in encoded)
    ):
        raise ValueError(
            f"固件文件 {file_id}.projectName "
            "必须是 1 至 31 个可见 ASCII 字符"
        )
    return project_name


def target_key(target: dict[str, Any]) -> str:
    """生成只用于检查目标是否重复的稳定键。"""
    return json.dumps(
        target["compatibility"], sort_keys=True, separators=(",", ":")
    )


def validate_config(config: dict[str, Any]) -> None:
    """校验设备配置中的目标、固件文件及其引用关系。"""
    if config.get("kind") != DEVICE_CONFIG_KIND:
        raise ValueError(f"设备配置 kind 必须是 {DEVICE_CONFIG_KIND}")
    if config.get("manifestVersion") != SUPPORTED_MANIFEST_VERSION:
        raise ValueError(
            "生成器仅支持 manifestVersion "
            f"{SUPPORTED_MANIFEST_VERSION}"
        )
    for name in ("publisherId", "deviceId", "repository"):
        if not isinstance(config.get(name), str) or not config[name]:
            raise ValueError(f"设备配置缺少 {name}")
    if (
        not 1 <= len(config["publisherId"]) <= 64
        or not RESOURCE_ID_PATTERN.fullmatch(config["publisherId"])
    ):
        raise ValueError(
            "publisherId 只能包含小写字母、数字和单个连字符"
        )
    if not RESOURCE_ID_PATTERN.fullmatch(config["deviceId"]):
        raise ValueError("deviceId 只能包含小写字母、数字和单个连字符")
    if len(config["deviceId"]) >= 32:
        raise ValueError("deviceId 最多只能包含 31 个字符")
    if not REPOSITORY_PATTERN.fullmatch(config["repository"]):
        raise ValueError("repository 必须使用 owner/repository 格式")
    templates = config.get("downloadUrlTemplates")
    if (
        not isinstance(templates, list)
        or not 1 <= len(templates) <= MAX_DOWNLOAD_SOURCE_COUNT
        or any(
            not isinstance(template, str)
            or "{release}" not in template
            or "{filename}" not in template
            for template in templates
        )
    ):
        raise ValueError(
            f"downloadUrlTemplates 必须包含 1 至 "
            f"{MAX_DOWNLOAD_SOURCE_COUNT} 个使用 release 和 filename 的模板"
        )
    build_download_urls(
        templates,
        config["repository"],
        "v1.0.0",
        "firmware.bin",
    )

    firmware_files = config.get("files")
    if (
        not isinstance(firmware_files, dict)
        or not 1 <= len(firmware_files) <= MAX_FIRMWARE_FILE_COUNT
    ):
        raise ValueError(
            f"files 必须包含 1 至 {MAX_FIRMWARE_FILE_COUNT} 个对象"
        )
    for file_id, file_config in firmware_files.items():
        if (
            not isinstance(file_id, str)
            or not NAME_PATTERN.fullmatch(file_id)
            or len(file_id) > MAX_FIRMWARE_FILE_ID_LENGTH
        ):
            raise ValueError(f"固件文件 ID 无效：{file_id}")
        if not isinstance(file_config, dict) or set(file_config) != {
            "chip", "projectName"
        }:
            raise ValueError(
                f"固件文件 {file_id} 必须且只能包含 chip 和 projectName"
            )
        if file_config["chip"] not in SUPPORTED_ESP_CHIP_IDS:
            raise ValueError(f"固件文件 {file_id}.chip 不受支持")
        validate_project_name(file_id, file_config["projectName"])

    targets = config.get("targets")
    if (
        not isinstance(targets, list)
        or not 1 <= len(targets) <= MAX_TARGET_COUNT
    ):
        raise ValueError(f"targets 必须包含 1 至 {MAX_TARGET_COUNT} 个对象")
    used_file_ids: set[str] = set()
    compatibility_keys: set[str] = set()
    for index, target in enumerate(targets):
        label = f"targets[{index}]"
        if not isinstance(target, dict) or set(target) != {
            "compatibility",
            "components",
        }:
            raise ValueError(
                f"{label} 必须且只能包含 compatibility 和 components"
            )
        compatibility = target["compatibility"]
        components = target["components"]
        if not isinstance(compatibility, dict) or set(compatibility) != {
            "deviceVersion", "chips"
        }:
            raise ValueError(
                f"{label}.compatibility 必须包含 deviceVersion 和 chips"
            )
        device_version = compatibility["deviceVersion"]
        if (
            not isinstance(device_version, str)
            or not DEVICE_VERSION_PATTERN.fullmatch(device_version)
        ):
            raise ValueError(
                f"{label}.compatibility.deviceVersion "
                "必须使用 <major>.<minor>"
            )
        chips = compatibility["chips"]
        if not isinstance(chips, dict) or not chips:
            raise ValueError(f"{label}.compatibility.chips 不能为空")
        if not isinstance(components, dict) or set(components) != set(chips):
            raise ValueError(
                f"{label}.components 必须与 compatibility.chips 使用相同角色"
            )
        for role, chip_condition in chips.items():
            if not isinstance(role, str) or not NAME_PATTERN.fullmatch(role):
                raise ValueError(f"{label} 包含无效组件角色：{role}")
            if not isinstance(chip_condition, dict) or set(chip_condition) != {
                "model", "revision"
            }:
                raise ValueError(
                    f"{label}.compatibility.chips.{role} 必须包含 "
                    "model 和 revision"
                )
            model = chip_condition["model"]
            revision = chip_condition["revision"]
            if model not in SUPPORTED_ESP_CHIP_IDS:
                raise ValueError(f"{label} 的芯片型号不受支持：{model}")
            if (
                not isinstance(revision, str)
                or not CHIP_REVISION_PATTERN.fullmatch(revision)
            ):
                raise ValueError(
                    f"{label}.compatibility.chips.{role}.revision "
                    "必须使用 <major>.<minor>"
                )
            major, minor = (int(part) for part in revision.split("."))
            if major > 255 or minor > 99:
                raise ValueError(
                    f"{label}.compatibility.chips.{role}.revision 超出范围"
                )
            file_id = components[role]
            if not isinstance(file_id, str) or file_id not in firmware_files:
                raise ValueError(
                    f"{label}.components.{role} 引用了未知固件文件"
                )
            if firmware_files[file_id]["chip"] != model:
                raise ValueError(
                    f"{label}.components.{role} 的固件芯片与目标条件不一致"
                )
            used_file_ids.add(file_id)
        key = target_key(target)
        if key in compatibility_keys:
            raise ValueError(f"{label} 的兼容条件与其他目标重复")
        compatibility_keys.add(key)
    unused_file_ids = set(firmware_files) - used_file_ids
    if unused_file_ids:
        raise ValueError(
            "files 包含未被任何目标引用的文件："
            + ", ".join(sorted(unused_file_ids))
        )


def build_manifest_filename(config: dict[str, Any], channel: str) -> str:
    """根据设备标识和格式版本生成固定 Manifest 资产文件名。"""
    major_version = config["manifestVersion"].split(".", 1)[0]
    return (
        f"lilygobox-{config['deviceId']}-ota-manifest-"
        f"{channel}-v{major_version}.json"
    )


def build_firmware_filename(
    config: dict[str, Any], file_id: str, version: str
) -> str:
    """根据设备、固件文件 ID 和版本生成唯一固件资产文件名。"""
    filename = (
        f"lilygobox-{config['deviceId']}-{file_id}-v{version}.bin"
    )
    if len(filename) > MAX_FIRMWARE_FILENAME_LENGTH:
        raise ValueError(f"固件文件名过长：{filename}")
    return filename


def validate_manifest_firmware_file(
    file_id: str,
    value: Any,
    config: dict[str, Any],
) -> dict[str, Any]:
    """校验上一版 Manifest 中可继承的一个固件文件。"""
    if not isinstance(value, dict) or set(value) != {
        "chip",
        "projectName",
        "version",
        "fileName",
        "downloadUrls",
        "sizeBytes",
        "hashes",
    }:
        raise ValueError(f"固件文件 {file_id} 的字段不完整")
    file_config = config["files"][file_id]
    if (
        value["chip"] != file_config["chip"]
        or value["projectName"] != file_config["projectName"]
    ):
        raise ValueError(f"固件文件 {file_id} 与设备配置不一致")
    version_value = value["version"]
    if not isinstance(version_value, str):
        raise ValueError(f"固件文件 {file_id}.version 无效")
    version = normalize_version(version_value, f"{file_id}.version")
    expected_filename = build_firmware_filename(config, file_id, version)
    filename = value["fileName"]
    if filename != expected_filename:
        raise ValueError(f"固件文件 {file_id}.fileName 无效")
    urls = value["downloadUrls"]
    if (
        not isinstance(urls, list)
        or not 1 <= len(urls) <= MAX_DOWNLOAD_SOURCE_COUNT
    ):
        raise ValueError(f"固件文件 {file_id}.downloadUrls 数量无效")
    validated_urls: list[str] = []
    for url in urls:
        if not is_supported_download_url(url) or not url.endswith(filename):
            raise ValueError(
                f"固件文件 {file_id}.downloadUrls 包含无效地址"
            )
        if url in validated_urls:
            raise ValueError(
                f"固件文件 {file_id}.downloadUrls 包含重复地址"
            )
        validated_urls.append(url)
    source_release = extract_release_tag(
        validated_urls[0], config["repository"], filename
    )
    if source_release is None:
        raise ValueError(f"固件文件 {file_id} 的 GitHub 主地址无效")
    size_bytes = value["sizeBytes"]
    if (
        not isinstance(size_bytes, int)
        or isinstance(size_bytes, bool)
        or not 0 < size_bytes <= MAX_FIRMWARE_ASSET_SIZE
    ):
        raise ValueError(f"固件文件 {file_id}.sizeBytes 无效")
    hashes = value["hashes"]
    if (
        not isinstance(hashes, dict)
        or set(hashes) != {"sha256"}
        or not isinstance(hashes["sha256"], str)
        or not SHA256_PATTERN.fullmatch(hashes["sha256"].lower())
    ):
        raise ValueError(f"固件文件 {file_id}.hashes.sha256 无效")
    return {
        "chip": file_config["chip"],
        "projectName": file_config["projectName"],
        "version": version,
        "fileName": filename,
        "downloadUrls": build_download_urls(
            config["downloadUrlTemplates"],
            config["repository"],
            source_release,
            filename,
        ),
        "sizeBytes": size_bytes,
        "hashes": {"sha256": hashes["sha256"].lower()},
    }


def validate_previous_manifest(
    manifest: dict[str, Any], config: dict[str, Any]
) -> dict[str, dict[str, Any]]:
    """校验上一版清单并提取可继承的固件文件。"""
    if manifest.get("kind") != MANIFEST_KIND:
        raise ValueError("上一版清单 kind 不受支持")
    if manifest.get("manifestVersion") != config["manifestVersion"]:
        raise ValueError("上一版清单 manifestVersion 不受支持")
    release = manifest.get("release")
    if not isinstance(release, dict) or set(release) != {
        "publisherId", "deviceId", "version"
    }:
        raise ValueError("上一版清单 release 无效")
    if (
        release["publisherId"] != config["publisherId"]
        or release["deviceId"] != config["deviceId"]
    ):
        raise ValueError("上一版清单属于其他设备")
    previous_release_version = normalize_version(
        release["version"], "上一版清单 release.version"
    )
    previous_channel = validate_release_channel(manifest.get("channel"))
    validate_release_version_channel(
        previous_release_version,
        previous_channel,
        "上一版清单 release.version",
    )
    validate_publish_time(manifest.get("publishTime"))

    previous_targets = manifest.get("targets")
    if not isinstance(previous_targets, list) or not previous_targets:
        raise ValueError("上一版清单缺少 targets")
    configured_target_keys = {
        target_key(target) for target in config["targets"]
    }
    previous_target_keys: set[str] = set()
    referenced_file_ids: set[str] = set()
    for index, target in enumerate(previous_targets):
        if not isinstance(target, dict) or set(target) != {
            "compatibility",
            "components",
        }:
            raise ValueError(f"上一版 targets[{index}] 无效")
        key = target_key(target)
        if key not in configured_target_keys:
            raise ValueError("设备配置缺少上一版清单中的硬件目标")
        if key in previous_target_keys:
            raise ValueError("上一版清单包含重复硬件目标")
        previous_target_keys.add(key)
        components = target["components"]
        if not isinstance(components, dict) or not components:
            raise ValueError(f"上一版 targets[{index}].components 无效")
        for file_id in components.values():
            if (
                not isinstance(file_id, str)
                or file_id not in config["files"]
            ):
                raise ValueError("上一版清单引用了未知固件文件")
            referenced_file_ids.add(file_id)

    firmware_files = manifest.get("files")
    if not isinstance(firmware_files, dict) or not firmware_files:
        raise ValueError("上一版清单缺少 files")
    unknown_file_ids = set(firmware_files) - set(config["files"])
    if unknown_file_ids:
        raise ValueError(
            "设备配置缺少上一版固件文件："
            + ", ".join(sorted(unknown_file_ids))
        )
    if not referenced_file_ids.issubset(firmware_files):
        raise ValueError("上一版清单目标引用了不存在的 files 条目")
    inherited: dict[str, dict[str, Any]] = {}
    for file_id, value in firmware_files.items():
        inherited[file_id] = validate_manifest_firmware_file(
            file_id, value, config
        )
    validate_main_component_versions(
        inherited, config, previous_channel
    )
    return inherited


def validate_notes(notes: list[str]) -> list[str]:
    """限制设备界面能够显示的更新说明数量和 UTF-8 长度。"""
    if len(notes) > 3:
        raise ValueError("更新说明最多只能提供三条")
    validated: list[str] = []
    for note in notes:
        text = note.strip()
        if not text or len(text.encode("utf-8")) >= 128:
            raise ValueError(
                "每条更新说明必须是 1 至 127 个 UTF-8 字节"
            )
        validated.append(text)
    return validated


def copy_and_describe_firmware_file(
    source: Path,
    destination: Path,
    version: str,
    config: dict[str, Any],
    release_tag: str,
    file_id: str,
) -> dict[str, Any]:
    """复制本次更新固件并生成对应 Manifest 文件对象。"""
    source = source.expanduser().resolve()
    if not source.is_file():
        raise ValueError(f"固件文件不存在：{source}")
    source_size = source.stat().st_size
    if source_size <= 0:
        raise ValueError(f"固件文件为空：{source}")
    if source_size > MAX_FIRMWARE_ASSET_SIZE:
        raise ValueError(f"固件文件超过 64 MiB 限制：{source}")
    file_config = config["files"][file_id]
    validate_firmware_application(source, file_id, version, config)
    destination.parent.mkdir(parents=True, exist_ok=True)
    if source != destination.resolve():
        shutil.copy2(source, destination)
    size_bytes = destination.stat().st_size
    if size_bytes != source_size:
        raise ValueError(f"固件复制后的长度不一致：{source}")
    filename = destination.name
    return {
        "chip": file_config["chip"],
        "projectName": file_config["projectName"],
        "version": version,
        "fileName": filename,
        "downloadUrls": build_download_urls(
            config["downloadUrlTemplates"],
            config["repository"],
            release_tag,
            filename,
        ),
        "sizeBytes": size_bytes,
        "hashes": {"sha256": calculate_sha256(destination)},
    }


def clear_generated_files(
    output_directory: Path,
    config: dict[str, Any],
    channel: str,
) -> None:
    """重新生成同一版本时只删除工具自身创建的 Release 文件。"""
    manifest_name = build_manifest_filename(config, channel)
    firmware_prefix = f"lilygobox-{config['deviceId']}-"
    for path in output_directory.iterdir():
        generated_firmware = (
            path.is_file()
            and path.name.startswith(firmware_prefix)
            and path.name.endswith(".bin")
        )
        if path.is_file() and (
            path.name == manifest_name or generated_firmware
        ):
            path.unlink()


def build_argument_parser(default_config: Path) -> argparse.ArgumentParser:
    """创建命令行参数解析器。"""
    parser = argparse.ArgumentParser(
        description="生成 LilygoBox OTA Manifest 和待上传固件"
    )
    parser.add_argument(
        "--release",
        help="Release 版本；默认使用本次 main 固件的 BIN 内嵌版本",
    )
    parser.add_argument(
        "--channel",
        choices=RELEASE_CHANNELS,
        default="stable",
        help="发布频道，默认 stable",
    )
    parser.add_argument(
        "--firmware-file",
        action="append",
        default=[],
        type=parse_firmware_file_argument,
        metavar="FILE_ID=BIN",
        help="本次发生变化的固件文件，版本自动读取，可重复传入",
    )
    parser.add_argument(
        "--previous-manifest",
        type=Path,
        help="未更新固件文件需要继承的上一版 Manifest",
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
        "--force",
        action="store_true",
        help="允许覆盖同版本的已生成 Release 文件",
    )
    return parser


def main() -> int:
    """生成仅包含本次需要上传资源的 Release 目录。"""
    script_directory = Path(__file__).resolve().parent
    default_config = script_directory / "devices" / "t-display-p4.json"
    parser = build_argument_parser(default_config)
    args = parser.parse_args()

    try:
        config = load_json(args.config.expanduser().resolve())
        validate_config(config)

        file_updates: dict[str, tuple[str, Path]] = {}
        for file_id, declared_version, path in args.firmware_file:
            if file_id not in config["files"]:
                raise ValueError(f"不支持固件文件：{file_id}")
            if file_id in file_updates:
                raise ValueError(f"固件文件重复：{file_id}")
            version = (
                declared_version
                if declared_version is not None
                else read_embedded_firmware_version(path, file_id)
            )
            file_updates[file_id] = (version, path)
        main_file_ids = main_component_file_ids(config)
        updated_main_versions = {
            file_updates[file_id][0]
            for file_id in main_file_ids
            if file_id in file_updates
        }
        if updated_main_versions:
            if len(updated_main_versions) != 1:
                raise ValueError(
                    "本次 main 固件包含多个不同版本，无法确定 Release 版本"
                )
            main_version = next(iter(updated_main_versions))
            if args.release is None:
                release_version = main_version
            else:
                release_version = normalize_version(args.release, "release")
                if release_version != main_version:
                    raise ValueError(
                        f"Release 版本必须与本次 main 固件版本一致："
                        f"{main_version}"
                    )
        else:
            if args.release is None:
                raise ValueError(
                    "未提供本次 main 固件时必须显式指定 --release"
                )
            release_version = normalize_version(args.release, "release")
        validate_release_version_channel(
            release_version, args.channel, "release"
        )
        release_tag = f"v{release_version}"

        for file_id in main_file_ids:
            if file_id not in file_updates:
                continue
            validate_release_version_channel(
                file_updates[file_id][0],
                args.channel,
                f"固件文件 {file_id}.version",
            )
        if not file_updates and args.previous_manifest is None:
            raise ValueError(
                "第一次发布至少需要提供一个 --firmware-file"
            )

        inherited: dict[str, dict[str, Any]] = {}
        if args.previous_manifest is not None:
            previous = load_json(args.previous_manifest.expanduser().resolve())
            inherited = validate_previous_manifest(previous, config)
            if previous["channel"] != args.channel:
                raise ValueError("上一版清单与当前发布频道不一致")
            previous_release = normalize_version(
                previous["release"]["version"],
                "上一版清单 release.version",
            )
            if version_key(release_version) <= version_key(previous_release):
                raise ValueError("新 Release 版本必须高于上一版清单")
            for file_id, (version, _) in file_updates.items():
                if file_id not in inherited:
                    continue
                previous_version = inherited[file_id]["version"]
                if version_key(version) <= version_key(previous_version):
                    raise ValueError(
                        f"固件文件 {file_id} 的新版本必须高于上一版"
                    )

        missing = [
            file_id
            for file_id in config["files"]
            if file_id not in file_updates and file_id not in inherited
        ]
        if missing:
            raise ValueError(
                "缺少固件文件且没有可继承的上一版清单："
                + ", ".join(missing)
            )

        output_root = (
            args.output_root.expanduser().resolve()
            if args.output_root is not None
            else script_directory / "output"
        )
        output_directory = (
            output_root
            / config["deviceId"]
            / args.channel
            / release_tag
        )
        if output_directory.exists() and any(output_directory.iterdir()):
            if not args.force:
                raise ValueError(
                    "输出目录非空，请更换版本或使用 --force："
                    f"{output_directory}"
                )
            clear_generated_files(output_directory, config, args.channel)
        output_directory.mkdir(parents=True, exist_ok=True)

        manifest: dict[str, Any] = {
            "kind": MANIFEST_KIND,
            "manifestVersion": config["manifestVersion"],
            "release": {
                "publisherId": config["publisherId"],
                "deviceId": config["deviceId"],
                "version": release_version,
            },
            "channel": args.channel,
            "publishTime": current_publish_time(),
            "targets": copy.deepcopy(config["targets"]),
            "files": {},
        }
        upload_files: list[Path] = []
        for file_id in config["files"]:
            if file_id in file_updates:
                version, source = file_updates[file_id]
                filename = build_firmware_filename(
                    config, file_id, version
                )
                destination = output_directory / filename
                file_manifest = copy_and_describe_firmware_file(
                    source,
                    destination,
                    version,
                    config,
                    release_tag,
                    file_id,
                )
                upload_files.append(destination)
            else:
                file_manifest = inherited[file_id]
            manifest["files"][file_id] = file_manifest
        validate_main_component_versions(
            manifest["files"], config, args.channel
        )
        manifest["releaseNotes"] = validate_notes(args.note)

        manifest_path = output_directory / build_manifest_filename(
            config, args.channel
        )
        manifest_text = (
            json.dumps(manifest, ensure_ascii=False, indent=2) + "\n"
        )
        if len(manifest_text.encode("utf-8")) > MAX_MANIFEST_SIZE:
            raise ValueError("生成的 Manifest 超过设备支持的 32 KiB")
        manifest_path.write_text(manifest_text, encoding="utf-8")
        validate_previous_manifest(load_json(manifest_path), config)
        print(f"Release 文件已生成：{output_directory}")
        print(f"版本固件 Release：{release_tag}")
        for path in upload_files:
            print(f"  {path.name} ({path.stat().st_size} bytes)")
        print(f"频道指针 Release：ota-{args.channel}")
        print(
            f"  {manifest_path.name} "
            f"({manifest_path.stat().st_size} bytes)"
        )
        return 0
    except ValueError as error:
        print(f"错误：{error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
