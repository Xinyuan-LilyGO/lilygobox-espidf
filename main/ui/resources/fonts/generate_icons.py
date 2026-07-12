#!/usr/bin/env python3
"""检查图标清单并生成 LilygoBox 使用的 LVGL 图标字体。"""

from __future__ import annotations

import argparse
import json
import re
import subprocess
import tempfile
from pathlib import Path


ICON_REFERENCE_PATTERN = re.compile(r"icon::(k[A-Za-z0-9_]+)")


def parse_arguments() -> argparse.Namespace:
    """解析命令行参数。"""
    parser = argparse.ArgumentParser(
        description="检查图标引用并生成 LVGL Material Symbols 字体。"
    )
    parser.add_argument(
        "--node",
        default="node",
        help="Node.js 可执行文件路径，默认使用 PATH 中的 node。",
    )
    parser.add_argument(
        "--check-only",
        action="store_true",
        help="只检查图标清单，不生成字体。",
    )
    return parser.parse_args()


def load_manifest(manifest_path: Path) -> dict:
    """读取并解析图标清单。"""
    with manifest_path.open("r", encoding="utf-8") as stream:
        return json.load(stream)


def collect_icon_references(main_directory: Path) -> set[str]:
    """扫描 C++ 源码并收集 icon::k... 图标引用。"""
    references: set[str] = set()
    for pattern in ("*.cpp", "*.h"):
        for source_path in main_directory.rglob(pattern):
            content = source_path.read_text(encoding="utf-8")
            references.update(ICON_REFERENCE_PATTERN.findall(content))
    return references


def validate_manifest(manifest: dict, references: set[str]) -> dict[str, str]:
    """校验清单与源码引用，并返回 C++ 名称到清单名称的索引。"""
    icons = manifest.get("icons", {})
    cpp_to_manifest = {
        properties["cpp_name"]: name
        for name, properties in icons.items()
    }
    missing = sorted(references - set(cpp_to_manifest))
    if missing:
        raise ValueError(
            "icon_manifest.json 缺少图标: " + ", ".join(missing)
        )
    unused = sorted(set(cpp_to_manifest) - references)
    if unused:
        raise ValueError(
            "icon_manifest.json 包含未使用图标: " + ", ".join(unused)
        )
    return cpp_to_manifest


def font_icon_names(
    font: dict,
    references: set[str],
    cpp_to_manifest: dict[str, str],
) -> list[str]:
    """取得单个字体应包含的清单图标名称。"""
    if font.get("icon_set") == "all_used":
        names = {cpp_to_manifest[name] for name in references}
        names.difference_update(font.get("exclude_icons", []))
        return sorted(names)
    return list(font.get("icons", []))


def normalize_generated_file(
    output_path: Path,
    conversion_source: Path,
    source_path: Path,
    variation_axes: dict[str, int] | None,
) -> None:
    """统一生成文件编码、来源注释和文件末尾空行。"""
    content = output_path.read_text(encoding="utf-8")
    content = content.replace(str(conversion_source), str(source_path))
    if variation_axes:
        axes_text = ", ".join(
            f"{name}={value}" for name, value in variation_axes.items()
        )
        lines = content.splitlines()
        for index, line in enumerate(lines):
            if line.startswith(" * Opts:"):
                lines.insert(index + 1, f" * Variation axes: {axes_text}")
                break
        content = "\n".join(lines)
    output_path.write_text(content.rstrip("\r\n") + "\n", encoding="utf-8")


def instantiate_variable_font(
    source_path: Path,
    output_path: Path,
    variation_axes: dict[str, int],
) -> None:
    """根据指定可变轴生成供 lv_font_conv 使用的静态 TTF。"""
    try:
        from fontTools.ttLib import TTFont
        from fontTools.varLib.instancer import instantiateVariableFont
    except ImportError as error:
        raise RuntimeError(
            "生成可变轴字体需要 FontTools，请运行: "
            "python -m pip install fonttools"
        ) from error

    variable_font = TTFont(source_path)
    static_font = instantiateVariableFont(
        variable_font,
        variation_axes,
        inplace=False,
    )
    static_font.save(output_path)
    static_font.close()
    variable_font.close()


def generate_font(
    app_root: Path,
    node_path: str,
    converter_path: Path,
    manifest: dict,
    font: dict,
    icon_names: list[str],
) -> None:
    """根据清单生成一个 LVGL 图标字体。"""
    icons = manifest["icons"]
    unknown = sorted(name for name in icon_names if name not in icons)
    if unknown:
        raise ValueError(
            f"字体 {font['id']} 引用了未知图标: " + ", ".join(unknown)
        )
    codepoints = sorted({int(icons[name]["codepoint"]) for name in icon_names})
    if not codepoints:
        raise ValueError(f"字体 {font['id']} 没有任何图标。")

    source_path = app_root / manifest["sources"][font["style"]]
    output_path = app_root / font["output"]
    with tempfile.TemporaryDirectory(prefix="lilygo_font_") as temp_directory:
        conversion_source = source_path
        variation_axes = font.get("variation_axes")
        if variation_axes:
            conversion_source = Path(temp_directory) / "instance.ttf"
            instantiate_variable_font(
                source_path,
                conversion_source,
                variation_axes,
            )
        command = [
            node_path,
            str(converter_path),
            "--no-compress",
            "--no-prefilter",
            "--bpp",
            "4",
            "--size",
            str(font["size"]),
            "--font",
            str(conversion_source),
            "-r",
            ",".join(str(codepoint) for codepoint in codepoints),
            "--format",
            "lvgl",
            "--lv-include",
            "lvgl.h",
            "--lv-font-name",
            font["object_name"],
            "-o",
            str(output_path),
        ]
        subprocess.run(command, cwd=app_root, check=True)
    normalize_generated_file(
        output_path,
        conversion_source,
        source_path,
        variation_axes,
    )
    print(f"已生成 {font['output']}")


def main() -> int:
    """执行清单检查，并按需重新生成全部图标字体。"""
    arguments = parse_arguments()
    font_directory = Path(__file__).resolve().parent
    app_root = font_directory.parents[3]
    manifest = load_manifest(font_directory / "icon_manifest.json")
    references = collect_icon_references(app_root / "main")
    cpp_to_manifest = validate_manifest(manifest, references)
    print(f"图标清单检查通过，共 {len(references)} 个图标。")
    if arguments.check_only:
        return 0

    converter_path = app_root / "tools/lv_font_conv/package/lv_font_conv.js"
    for font in manifest.get("fonts", []):
        names = font_icon_names(font, references, cpp_to_manifest)
        generate_font(
            app_root,
            arguments.node,
            converter_path,
            manifest,
            font,
            names,
        )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
