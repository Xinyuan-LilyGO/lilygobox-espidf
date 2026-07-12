# LVGL Image Resource Management

This directory contains LVGL ARGB8888 images converted from the project-level
`assets/icon/*.svg` sources. Generated `.c` files are linked directly into the
firmware and must not be edited manually.

## Files

- `image_assets.h` declares every `lv_image_dsc_t` image object.
- `image_manifest.json` records SVG inputs, outputs, names, sizes, and padding.
- `generate_images.py` validates references and invokes the Node converter.
- `*_inner_icon_*x*.c` files are generated LVGL ARGB8888 image arrays.
- `tools/svg_to_lvgl_image.js` performs SVG rendering and C array output.

## Validation and generation

Validate the manifest, source references, declarations, and safe paths:

```powershell
python main/ui/resources/images/generate_images.py --check-only
```

Regenerate every image with an explicit Node executable:

```powershell
python main/ui/resources/images/generate_images.py `
  --node D:\node-v24.18.0-win-x64\node.exe
```

Python performs orchestration and safety validation while Node renders SVGs.
After generation, the tool verifies names, dimensions, and
`LV_COLOR_FORMAT_ARGB8888`, then removes dynamic dates for reproducible output.

## Adding an image

1. Put the source SVG in the project-level `assets/icon/` directory.
2. Register its input, output, name, dimensions, and padding in the manifest.
3. Add the matching `LV_IMAGE_DECLARE(...)` to `image_assets.h`.
4. Reference the `lv_image_dsc_t` object from application code.
5. Run `--check-only`, then run the generation command.

## Safety restrictions

The generator never deletes files and enforces these restrictions:

- Inputs must remain inside the project-level `assets/` directory.
- Outputs must be direct children of `main/ui/resources/images/`.
- Output files must use the `.c` extension.
- Manifest names, source references, and declarations must match exactly.
- Missing, unused, duplicate, or invalid entries stop execution.

Only output files explicitly registered in `image_manifest.json` are replaced.
