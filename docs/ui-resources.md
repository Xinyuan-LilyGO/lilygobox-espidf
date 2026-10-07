# 界面与资源

设备界面使用 LVGL。具体页面位于 `main/ui/views`，主题、动画及公共控件分别由对应公共目录管理。

## 页面开发

- 新增设置项优先复用已有设置页面的行高、标题、间距和配色。
- 提示框使用 `main/ui/widgets/prompt` 的公共模板，保持标题、说明、按钮和确认交互一致。
- 图标使用已有语义名称和字体资源，不为普通图标手工绘制近似图形。
- 长名称按页面需求使用 LVGL 的标签滚动模式，设置合理宽度并避免内容溢出。
- 页面更新先比较值，内容未变化时不反复设置文本或样式。
- 网络和硬件的耗时操作放在后台；页面销毁时清理自己拥有的定时器、事件和动画。

## 图片资源

原始 SVG 位于 `assets/icon`。图片生成清单为 [image_manifest.json](../main/ui/resources/images/image_manifest.json)，其中记录输入素材、目标大小、留白和输出文件。生成的 C 数组通过 `image_assets.h` 统一声明。

```bash
python main/ui/resources/images/generate_images.py --check-only
python main/ui/resources/images/generate_images.py
```

生成依赖 Python、Node.js 和仓库中的转换工具。图片输出为 LVGL 可使用的 ARGB8888 资源；需要修改图标时调整 SVG 或清单后重新生成，不直接编辑 C 数组。提交时包含原始素材、清单和生成结果。

## 字体与图标

- Google Sans Flex 用于界面文本，LINE Seed 字体用于部分时钟和日期内容。
- Material Symbols 用于图标，Fill 与 Outline 按现有场景选择。
- `font_assets.h` 声明字体，`icon_assets.h` 定义语义图标名称，`icon_manifest.json` 管理生成参数。

```bash
python main/ui/resources/fonts/generate_icons.py --check-only
python main/ui/resources/fonts/generate_icons.py
```

LVGL 使用转换后的静态字体，运行时不能直接修改源字体的可变轴。新增图标尽量加入现有字号和字体集合，不创建大量单图标字体。

工具依赖和生成约定见[字体资源说明](../main/ui/resources/fonts/README_CN.md)与[图片资源说明](../main/ui/resources/images/README_CN.md)。

## 性能相关

ESP32-P4 的 LVGL 内存由 [lvgl_memory.cpp](../main/hal/lvgl_memory.cpp) 适配，显示端口在 [lvgl_port.cpp](../main/hal/lvgl_port.cpp)。涉及 WLAN 页面卡顿时，先查看[性能说明](wlan-performance.md)。
