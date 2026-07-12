# LVGL 图片资源管理

本目录保存由根目录 `assets/icon/*.svg` 转换而来的 LVGL ARGB8888 图片。
生成的 `.c` 文件会直接编译进固件，不应手工修改。

## 文件说明

- `image_assets.h`：集中声明全部 `lv_image_dsc_t` 图片对象。
- `image_manifest.json`：记录输入 SVG、输出文件、对象名称、尺寸和留白。
- `generate_images.py`：检查引用并调用现有 Node 转换器生成图片。
- `*_inner_icon_*x*.c`：自动生成的 LVGL ARGB8888 图片数组。
- `tools/svg_to_lvgl_image.js`：实际执行 SVG 渲染和 C 数组输出。

## 检查和生成

只检查清单、源码引用、头文件声明和安全路径：

```powershell
python main/ui/resources/images/generate_images.py --check-only
```

使用指定 Node 重新生成全部图片：

```powershell
python main/ui/resources/images/generate_images.py `
  --node D:\node-v24.18.0-win-x64\node.exe
```

Python 工具负责管理和安全检查，Node 工具负责 SVG 渲染。生成后会检查对象
名称、宽高和 `LV_COLOR_FORMAT_ARGB8888`，并移除动态日期，保证重复生成稳定。

## 添加图片

1. 将原始 SVG 放入根目录 `assets/icon/`。
2. 在 `image_manifest.json` 中登记名称、输入、输出、宽高和留白。
3. 在 `image_assets.h` 中增加对应的 `LV_IMAGE_DECLARE(...)`。
4. 在业务代码中引用 `lv_image_dsc_t` 图片对象。
5. 先运行 `--check-only`，再运行生成命令。

## 安全限制

生成工具不会删除文件，并强制执行以下限制：

- 输入必须位于工程根目录 `assets/` 内。
- 输出必须直接位于 `main/ui/resources/images/`。
- 输出扩展名必须是 `.c`。
- 清单名称、源码引用和 `image_assets.h` 声明必须完全一致。
- 未登记、无引用、重复名称或非法尺寸都会停止执行。

工具只会覆盖 `image_manifest.json` 明确登记的图片输出文件。
