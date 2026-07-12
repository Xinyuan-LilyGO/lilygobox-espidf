# 字体资源管理

本目录保存经过 `lv_font_conv` 转换、可直接由 LVGL 使用的字体资源。
生成的 `.c` 文件不要手工修改，应从原始 TTF 重新生成。

## 分类和命名

文本字体位于 `text/`，按“字体家族 + 字号”命名：

- `lvgl_font_google_sans_flex_<字号>.c`：界面正文和标题。
- `lvgl_font_lineseedkr_<字重>_<字号>.c`：时钟和日期。

Material Symbols 图标字体位于 `icons/`，按“样式 + 字号”命名：

- `lvgl_font_material_rounded_fill_<字号>.c`：填充图标。
- `lvgl_font_material_rounded_outline_<字号>.c`：轮廓图标。

当前只保留七个实际使用的图标字体：

| 字体 | 用途 |
| --- | --- |
| `fill_22` | 状态栏小型填充图标 |
| `fill_32` | 状态、设置、列表等常规填充图标 |
| `fill_44` | 文件夹、抽屉操作等中型填充图标 |
| `fill_56` | 菜单和电源操作等大型填充图标 |
| `outline_44` | 返回、添加、删除、发送等轮廓图标 |
| `outline_w200_46` | 状态栏 46 号 Weight 200 电池轮廓图标 |
| `outline_56` | 启动器、锁屏和音乐页面的大型轮廓图标 |

原来的 `action_44` 和 `near_me_44` 已合并到 `outline_44`，不再为单个
图标创建专用字体。

## 相关文件

- `font_assets.h`：声明全部 LVGL 字体对象。
- `icon_assets.h`：保存图标的语义名称和 UTF-8 编码。
- `icon_manifest.json`：记录图标码点、字体成员、轴参数和光学偏移。
- `generate_icons.py`：检查引用并按清单重新生成图标字体。
- `assets/icon/material_symbols/MaterialSymbolsRounded.ttf`：轮廓源字体。
- `assets/icon/material_symbols/MaterialSymbolsRoundedFill.ttf`：填充源字体。
- `tools/lv_font_conv`：字体转换工具。

`icon_assets.h` 只保留代码中实际使用的图标。禁止使用
`kAction1`、`kFilesIcon` 等场景名称，应使用 Material Symbols 的语义名称，
例如 `kArrowBack`、`kDelete`、`kNearMe`。

## 添加图标

1. 在 `icon_assets.h` 中增加语义常量。
2. 在 `icon_manifest.json` 中登记名称、码点和目标字体。
3. 确认图标需要 Fill 还是 Outline 样式。
4. 确认界面实际使用的字号。
5. 在 `font_assets.h` 中复用现有字体声明，不要增加单图标字体。
6. 运行清单检查和生成脚本。

清单检查和生成命令：

```powershell
python main/ui/resources/fonts/generate_icons.py --check-only
python main/ui/resources/fonts/generate_icons.py --node <node.exe>
```

包含 `variation_axes` 的字体需要 FontTools：

```powershell
python -m pip install fonttools
```

生成文件开头的 `Opts` 注释记录了该字体当前包含的码点，可以据此复现。

## 图标宽度和对齐

不同图标的字形宽度、视觉中心和留白不同，这是字体本身的正常现象。
不要通过复制字体或修改 advance width 来修正单个界面。推荐做法：

- 图标标签放入固定宽高的父控件。
- 使用 `lv_obj_center()` 或 `LV_ALIGN_CENTER` 居中。
- 仅在确有视觉偏差时，对该控件设置少量 `x/y` 偏移。
- 偏移属于控件布局，不属于字体资源，不应写入字体名称。

## 可变字体轴

LVGL 字体在转换后是静态资源，不能在运行时修改可变字体轴。当前统一采用
`Weight=400`、`Grade=0`，Fill 由字体系列决定，Optical Size 按输出字号记录
并最大限制为 48。轴参数记录在 `icon_manifest.json` 中。

如果以后需要另一种 Weight 或 Grade，应新增明确命名的完整字体实例，不能
为单个图标建立临时字体。不同 glyph 的宽度不通过轴参数修复，而是使用固定
尺寸容器居中；少数视觉偏差记录在清单的 `adjustments` 字段中。

状态栏电池是正式的 `Weight=200` 轴实例，因此使用明确的
`outline_w200_46` 名称；原 `outline_44` 不再包含该电池码点。

## 删除检查

删除字体前必须同时确认：

1. `font_assets.h` 中没有声明。
2. C/C++ 源码中没有字体对象引用。
3. 没有控件依赖该字体内独有的码点。
4. ESP-IDF 的 `main/CMakeLists.txt` 仍能收集保留的字体文件。

`main/CMakeLists.txt` 已显式加入 `ui/resources/fonts/text` 和
`ui/resources/fonts/icons`，新增字体文件必须放在对应目录中。
