#!/usr/bin/env python3
"""检查图片清单并生成 LilygoBox 使用的 LVGL 图片资源。"""

from __future__ import annotations

import argparse
import json
import re
import subprocess
from pathlib import Path


IMAGE_REFERENCE_PATTERN = re.compile(
    r"&([A-Za-z][A-Za-z0-9_]*_inner_icon_[0-9]+x[0-9]+)\b"
)
IMAGE_DECLARE_PATTERN = re.compile(r"LV_IMAGE_DECLARE\(([A-Za-z0-9_]+)\)")


def parse_arguments() -> argparse.Namespace:
    """解析命令行参数。"""
    parser = argparse.ArgumentParser(
        description="检查图片引用并生成 LVGL ARGB8888 图片资源。"
    )
    parser.add_argument(
        "--node",
        default="node",
        help="Node.js 可执行文件路径，默认使用 PATH 中的 node。",
    )
    parser.add_argument(
        "--check-only",
        action="store_true",
        help="只检查图片清单，不生成图片。",
    )
    return parser.parse_args()


def load_manifest(manifest_path: Path) -> dict:
    """读取并解析图片清单。"""
    with manifest_path.open("r", encoding="utf-8") as stream:
        return json.load(stream)


def require_path_within(path: Path, directory: Path, label: str) -> Path:
    """要求路径位于指定目录内，并返回规范化后的绝对路径。"""
    resolved_path = path.resolve()
    resolved_directory = directory.resolve()
    try:
        resolved_path.relative_to(resolved_directory)
    except ValueError as error:
        raise ValueError(f"{label}超出允许目录: {resolved_path}") from error
    return resolved_path


def collect_image_references(main_directory: Path) -> set[str]:
    """扫描 C++ 源码并收集实际使用的图片对象。"""
    references: set[str] = set()
    for pattern in ("*.cpp", "*.h"):
        for source_path in main_directory.rglob(pattern):
            content = source_path.read_text(encoding="utf-8")
            references.update(IMAGE_REFERENCE_PATTERN.findall(content))
    return references


def declared_images(header_path: Path) -> set[str]:
    """读取图片声明头并返回全部 LV_IMAGE_DECLARE 名称。"""
    content = header_path.read_text(encoding="utf-8")
    return set(IMAGE_DECLARE_PATTERN.findall(content))


def validate_manifest(
    app_root: Path,
    images_directory: Path,
    manifest: dict,
) -> list[dict]:
    """校验图片清单、源码引用、声明和安全路径。"""
    images = list(manifest.get("images", []))
    names = [image.get("name") for image in images]
    if None in names or len(names) != len(set(names)):
        raise ValueError("image_manifest.json 存在空名称或重复名称。")

    references = collect_image_references(app_root / "main")
    manifest_names = set(names)
    missing = sorted(references - manifest_names)
    if missing:
        raise ValueError("图片清单缺少引用: " + ", ".join(missing))
    unused = sorted(manifest_names - references)
    if unused:
        raise ValueError("图片清单包含未使用图片: " + ", ".join(unused))

    declarations = declared_images(images_directory / "image_assets.h")
    if declarations != manifest_names:
        missing_declarations = sorted(manifest_names - declarations)
        stale_declarations = sorted(declarations - manifest_names)
        details = []
        if missing_declarations:
            details.append("缺少声明: " + ", ".join(missing_declarations))
        if stale_declarations:
            details.append("多余声明: " + ", ".join(stale_declarations))
        raise ValueError("；".join(details))

    source_directory = app_root / "assets"
    for image in images:
        input_path = require_path_within(
            app_root / image["input"],
            source_directory,
            "图片输入路径",
        )
        if not input_path.is_file():
            raise FileNotFoundError(f"图片输入不存在: {input_path}")
        output_path = require_path_within(
            app_root / image["output"],
            images_directory,
            "图片输出路径",
        )
        if output_path.parent != images_directory.resolve():
            raise ValueError(f"图片输出必须直接位于 images/: {output_path}")
        if output_path.suffix.lower() != ".c":
            raise ValueError(f"图片输出必须是 .c 文件: {output_path}")
        if int(image["width"]) <= 0 or int(image["height"]) <= 0:
            raise ValueError(f"图片尺寸无效: {image['name']}")
    return images


def normalize_generated_file(output_path: Path) -> None:
    """移除动态日期并统一生成文件编码和末尾空行。"""
    content = output_path.read_text(encoding="utf-8")
    content = re.sub(r"^ \* @Date:.*\r?\n", "", content, flags=re.MULTILINE)
    content = re.sub(
        r"^ \* @LastEditTime:.*\r?\n",
        "",
        content,
        flags=re.MULTILINE,
    )
    output_path.write_text(content.rstrip("\r\n") + "\n", encoding="utf-8")


def validate_generated_file(output_path: Path, image: dict) -> None:
    """校验生成文件的对象名称、颜色格式和尺寸。"""
    content = output_path.read_text(encoding="utf-8")
    expected_fragments = (
        f"const lv_image_dsc_t {image['name']} =",
        ".header.cf = LV_COLOR_FORMAT_ARGB8888,",
        f".header.w = {int(image['width'])},",
        f".header.h = {int(image['height'])},",
    )
    missing = [item for item in expected_fragments if item not in content]
    if missing:
        raise ValueError(
            f"生成图片 {image['name']} 校验失败: " + ", ".join(missing)
        )


def generate_image(
    app_root: Path,
    images_directory: Path,
    converter_path: Path,
    node_path: str,
    image: dict,
) -> None:
    """调用现有 Node 转换器生成一个 LVGL 图片资源。"""
    input_path = require_path_within(
        app_root / image["input"],
        app_root / "assets",
        "图片输入路径",
    )
    output_path = require_path_within(
        app_root / image["output"],
        images_directory,
        "图片输出路径",
    )
    command = [
        node_path,
        str(converter_path),
        "--input",
        str(input_path),
        "--output",
        str(output_path),
        "--name",
        image["name"],
        "--width",
        str(int(image["width"])),
        "--height",
        str(int(image["height"])),
        "--padding",
        str(int(image.get("padding", 0))),
    ]
    subprocess.run(command, cwd=app_root, check=True)
    normalize_generated_file(output_path)
    validate_generated_file(output_path, image)
    print(f"已生成 {image['output']}")


def main() -> int:
    """执行清单检查，并按需重新生成全部图片。"""
    arguments = parse_arguments()
    images_directory = Path(__file__).resolve().parent
    app_root = images_directory.parents[3]
    manifest = load_manifest(images_directory / "image_manifest.json")
    images = validate_manifest(app_root, images_directory, manifest)
    print(f"图片清单检查通过，共 {len(images)} 张图片。")
    if arguments.check_only:
        return 0

    converter_path = app_root / "tools/svg_to_lvgl_image.js"
    if not converter_path.is_file():
        raise FileNotFoundError(f"图片转换器不存在: {converter_path}")
    for image in images:
        generate_image(
            app_root,
            images_directory,
            converter_path,
            arguments.node,
            image,
        )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
