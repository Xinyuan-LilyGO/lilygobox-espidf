# Compiled UI Resources

This directory contains C/C++ UI resources that are linked directly into the
firmware.

- `fonts/` contains LVGL text fonts, icon fonts, manifests, and generators.
- `images/` contains launcher images, manifests, declarations, and generators.

The project-level `assets/` directory contains source material such as TTF
files and is used as conversion input. This directory contains generated
firmware resources and is used as ESP-IDF compilation input. Do not edit
generated font or image arrays manually; update the source asset and regenerate
the output instead.
