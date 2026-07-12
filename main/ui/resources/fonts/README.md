# Font Asset Management

This directory contains fonts converted by `lv_font_conv` for direct use by
LVGL. Generated `.c` files must not be edited manually; regenerate them from
the source TTF files instead.

## Classification and naming

Text fonts live in `text/` and use the `family_weight_size` or `family_size`
pattern:

- `lvgl_font_google_sans_flex_<size>.c` for UI text and headings.
- `lvgl_font_lineseedkr_<weight>_<size>.c` for the clock and date.

Material Symbols fonts live in `icons/` and use the `style_size` pattern:

- `lvgl_font_material_rounded_fill_<size>.c` for filled icons.
- `lvgl_font_material_rounded_outline_<size>.c` for outlined icons.

Only seven icon fonts used by the application are retained:

| Font | Purpose |
| --- | --- |
| `fill_22` | Small filled status-bar icons |
| `fill_32` | General status, settings, and list icons |
| `fill_44` | Medium filled folder and drawer-action icons |
| `fill_56` | Large filled menu and power-action icons |
| `outline_44` | Back, add, delete, and send icons |
| `outline_w200_46` | Size 46, Weight 200 status-bar battery outline |
| `outline_56` | Large launcher, lock-screen, and music icons |

The former `action_44` and `near_me_44` fonts were merged into `outline_44`.
Do not create a dedicated font for one icon.

## Related files

- `font_assets.h` declares every LVGL font object.
- `icon_assets.h` maps semantic icon names to UTF-8 values.
- `icon_manifest.json` records code points, font membership, axes, and optical
  adjustments.
- `generate_icons.py` validates references and regenerates icon fonts.
- `assets/icon/material_symbols/MaterialSymbolsRounded.ttf` is the outline
  source font.
- `assets/icon/material_symbols/MaterialSymbolsRoundedFill.ttf` is the filled
  source font.
- `tools/lv_font_conv` contains the conversion tool.

`icon_assets.h` contains only icons referenced by the code. Use
semantic Material Symbols names such as `kArrowBack`, `kDelete`, and `kNearMe`.
Avoid context-specific names such as `kAction1` or `kFilesIcon`.

## Adding an icon

1. Add a semantic constant to `icon_assets.h`.
2. Register its name, code point, and target fonts in `icon_manifest.json`.
3. Choose the Fill or Outline source style.
4. Choose an existing size actually used by the UI.
5. Reuse an existing declaration in `font_assets.h`; do not create a
   single-icon font.
6. Run the manifest check and generation script.

Validation and generation commands:

```powershell
python main/ui/resources/fonts/generate_icons.py --check-only
python main/ui/resources/fonts/generate_icons.py --node <node.exe>
```

Fonts with `variation_axes` require FontTools:

```powershell
python -m pip install fonttools
```

The `Opts` comment at the beginning of every generated file records its exact
code-point list and can be used to reproduce the file.

## Icon width and alignment

Glyph widths, visual centers, and side bearings naturally differ. Do not fix
one screen by copying a font or changing a glyph's advance width. Instead:

- Put the icon label inside a fixed-size parent object.
- Center it with `lv_obj_center()` or `LV_ALIGN_CENTER`.
- Apply a small local `x/y` offset only when optical correction is required.
- Keep layout offsets in widget code, never in font names.

## Variable font axes

LVGL fonts are static after conversion, so variable axes cannot be changed at
runtime. The project standard is `Weight=400` and `Grade=0`; Fill is selected
by the font family, and Optical Size follows the output size with a maximum of
48. These values are documented in `icon_manifest.json`.

If another Weight or Grade is required, generate a clearly named complete font
instance. Never create a temporary font for one glyph. Width differences are
handled by fixed-size centered containers; exceptional optical corrections are
stored in the manifest's `adjustments` field.

The status-bar battery is a formal `Weight=200` axis instance and therefore
uses the explicit `outline_w200_46` name. The regular `outline_44` font no
longer contains the battery code point.

## Safe removal checklist

Before deleting a font, verify that:

1. `font_assets.h` no longer declares it.
2. C/C++ sources no longer reference its font object.
3. No widget depends on a glyph unique to that font.
4. `main/CMakeLists.txt` still collects every retained font source.

`main/CMakeLists.txt` explicitly includes `ui/resources/fonts/text` and
`ui/resources/fonts/icons`. New generated fonts must be placed in the matching
directory.
