<p align="right">
  <a href="README.zh_CN.md">简体中文</a> · <strong>English</strong>
</p>

# Generated CJK fonts

`pq_font_16.c` and `pq_font_20.c` are LVGL 9 bitmap fonts generated from a licensed
Simplified Chinese typeface. They are build inputs: never edit them by hand, and never
commit the source font itself.

| Item | Value |
| --- | --- |
| Family | Noto Sans CJK SC Regular (Source Han Sans lineage) |
| Source file | `NotoSansCJKsc-Regular.otf`, 16 437 364 bytes |
| SHA-256 | `2c76254f6fc379fddfce0a7e84fb5385bb135d3e399294f6eeb6680d0365b74b` |
| Download | <https://raw.githubusercontent.com/notofonts/noto-cjk/main/Sans/OTF/SimplifiedChinese/NotoSansCJKsc-Regular.otf> |
| License | SIL Open Font License 1.1 — redistribution of the generated bitmaps is allowed; the OTF itself is not committed |
| Sizes | 16 px and 20 px, 4 bpp, PLAIN (uncompressed) |
| Coverage | 152 code points: printable ASCII plus the characters in `tools/font_charset.txt` |
| Flash cost | about 12 KB + 18 KB of glyph bitmaps, plus descriptors |

## Why not `lv_font_conv`

Version 1.5.3 produces corrupted bitmaps under current Node, which shows up on the
device as garbled text across the whole screen. The details and the reasoning are in
[LVGL 9 CJK bitmap font subsets](../../docs/reference/shinku-chen/lvgl-cjk-font-subsets.md).
`tools/gen_font.py` rasterises with Pillow and writes the LVGL format directly, then
parses its own output back and compares every glyph pixel by pixel.

## Regenerating

```bash
python tools/gen_font.py \
    --font /path/to/NotoSansCJKsc-Regular.otf --size 16 \
    --charset tools/font_charset.txt \
    --output assets/fonts/pq_font_16.c --name pq_font_16
python tools/gen_font.py \
    --font /path/to/NotoSansCJKsc-Regular.otf --size 20 \
    --charset tools/font_charset.txt \
    --output assets/fonts/pq_font_20.c --name pq_font_20
```

## Adding characters

1. Add the new UI strings to `tools/font_charset.txt` (or fix the string).
2. `python tools/gen_font.py --check --charset tools/font_charset.txt main` — it fails
   with the missing code points; the repository gate runs the same command.
3. Regenerate both sizes; the line height follows the font metrics (24 px and 30 px), so
   check layout if you switch typefaces.
