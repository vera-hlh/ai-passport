#!/usr/bin/env python3
"""生成 LVGL 9 的 4bpp 中文位图字体子集，并回读校验。

为什么不用 lv_font_conv：1.5.3（最后一版）在当前 Node 下产出的位图按 LVGL 的
PLAIN 语义解出来是噪点，设备上的表现是整屏乱码（见
docs/reference/shinku-chen/lvgl-cjk-font-subsets.zh_CN.md）。这里直接用 Pillow
栅格化并自己写 LVGL 的位图格式，生成后再把 C 文件解析回来、按 LVGL 的语义逐
像素比对；任何不一致都让构建失败，而不是靠肉眼看屏幕。

用法：
  # 生成
  python tools/gen_font.py --font <otf> --size 16 --charset tools/font_charset.txt \
      --output assets/fonts/pq_font_16.c --name pq_font_16

  # 覆盖检查（不需要 Pillow）：源文件字符串里出现的非 ASCII 字符必须都在字符集里
  python tools/gen_font.py --check --charset tools/font_charset.txt main

字体格式要点（LVGL 9，fmt_txt）：
  * PLAIN、4bpp、高半字节在前，字形之间按字节对齐（dsc->stride = 0，行间不补齐）；
  * ASCII 用 FORMAT0_TINY 连续段，其余用 SPARSE_TINY（unicode_list 存相对 range_start 的偏移）；
  * adv_w 是 4 位小数的定点数（真实宽度 x 16）；
  * ofs_y 从行顶算起，行的基线位置 = line_height - base_line。
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

# ---------------------------------------------------------------- 字符集与解析

def read_charset(path: Path) -> set[str]:
    """字符集文件：UTF-8 文本，所有非空白字符都计入。"""
    chars: set[str] = set()
    for line in path.read_text(encoding="utf-8").splitlines():
        for ch in line:
            if not ch.isspace():
                chars.add(ch)
    return chars


ASCII_START = 32
ASCII_END = 126


def codepoints(chars: set[str]) -> list[int]:
    cps = {ord(ch) for ch in chars}
    cps.update(range(ASCII_START, ASCII_END + 1))
    return sorted(cps)


# ---------------------------------------------------------------- 源码扫描（--check）

_STRING_RE = re.compile(r'"((?:[^"\\]|\\.)*)"')


def scan_sources(paths: list[Path]) -> dict[str, set[str]]:
    """粗略扫描 C 字符串字面量里的非 ASCII 字符：按文件分组返回。"""
    found: dict[str, set[str]] = {}
    for root in paths:
        files = [root] if root.is_file() else [
            p for p in root.rglob("*") if p.suffix in (".c", ".h") and p.is_file()
        ]
        for path in files:
            text = path.read_text(encoding="utf-8", errors="replace")
            # 去掉行注释，避免把说明文字里的中文当成界面文案
            text = re.sub(r"//[^\n]*", "", text)
            chars = {ch for literal in _STRING_RE.findall(text)
                     for ch in literal if ord(ch) > ASCII_END}
            if chars:
                found[str(path)] = chars
    return found


def run_check(charset_path: Path, sources: list[Path]) -> int:
    charset = read_charset(charset_path)
    missing: dict[str, set[str]] = {}
    for path, chars in scan_sources(sources).items():
        gap = chars - charset
        if gap:
            missing[path] = gap

    if not missing:
        print("font charset: PASS")
        return 0

    for path, gap in missing.items():
        codes = " ".join(f"U+{ord(ch):04X}({ch})" for ch in sorted(gap))
        print(f"ERROR {path}: 字符集缺少 {codes}", file=sys.stderr)
    return 1


# ---------------------------------------------------------------- 栅格化

def rasterize(font_path: Path, size: int, cps: list[int]) -> tuple[list[dict], int, int]:
    from PIL import Image, ImageFont

    font = ImageFont.truetype(str(font_path), size)
    ascent, descent = font.getmetrics()
    line_height = ascent + descent
    base_line = descent

    glyphs: list[dict] = []
    for cp in cps:
        ch = chr(cp)
        # anchor="ls"：原点在基线左端，Pillow 的 y 轴向下，bbox 的 y0 因此为负。
        mask, offset = font.getmask2(ch, mode="L", anchor="ls")
        box_w, box_h = mask.size
        if box_w == 0 or box_h == 0:
            box_w = box_h = 0
            nibbles = bytearray()
        else:
            # ImagingCore 没有 tobytes()：借助 Image 包装一层再取字节。
            raw = Image.Image()._new(mask).tobytes()
            nibbles = bytearray((value * 15 + 127) // 255 for value in raw)
        ox, oy = offset
        glyphs.append({
            "cp": cp,
            "box_w": box_w,
            "box_h": box_h,
            "ofs_x": int(ox),
            # 行顶到基线是 line_height - base_line，再加上字形在基线上方的偏移
            "ofs_y": int(line_height - base_line + oy),
            "adv_w": int(round(font.getlength(ch) * 16)),
            "nibbles": nibbles,
        })
    return glyphs, line_height, base_line


# ---------------------------------------------------------------- 打包与生成

def pack_bitmap(glyphs: list[dict]) -> tuple[bytes, list[int]]:
    """把每个字形的 4bpp 位图按字节对齐依次拼接，返回位图与各字形的起始偏移。"""
    out = bytearray()
    starts: list[int] = []
    for glyph in glyphs:
        starts.append(len(out))
        nibbles = glyph["nibbles"]
        for i in range(0, len(nibbles), 2):
            high = nibbles[i]
            low = nibbles[i + 1] if i + 1 < len(nibbles) else 0
            out.append((high << 4) | low)
    return bytes(out), starts


def build_cmap(cps: list[int]) -> tuple[list[dict], list[int]]:
    """ASCII 一段连续 cmap + 其余一段稀疏 cmap；返回 cmap 描述与稀疏偏移表。"""
    ascii_cps = [cp for cp in cps if ASCII_START <= cp <= ASCII_END]
    sparse_cps = [cp for cp in cps if cp > ASCII_END]
    if len(ascii_cps) != ASCII_END - ASCII_START + 1:
        raise SystemExit("ASCII 段不完整，检查字符集生成逻辑")

    cmaps = [{
        "range_start": ASCII_START,
        "range_length": ASCII_END - ASCII_START + 1,
        "glyph_id_start": 1,
        "sparse": False,
    }]
    offsets: list[int] = []
    if sparse_cps:
        start = sparse_cps[0]
        for cp in sparse_cps:
            off = cp - start
            if not 0 <= off <= 0xFFFF:
                raise SystemExit(f"U+{cp:04X} 与稀疏段起点相差超过 16 位，需要再分一段")
            offsets.append(off)
        cmaps.append({
            "range_start": start,
            "range_length": sparse_cps[-1] - start + 1,
            "glyph_id_start": 1 + len(ascii_cps),
            "sparse": True,
        })
    return cmaps, offsets


def emit_c(name: str, size: int, glyphs: list[dict], line_height: int, base_line: int,
           cmaps: list[dict], offsets: list[int], bitmap: bytes, starts: list[int]) -> str:
    guard = name.upper()
    lines: list[str] = []
    add = lines.append

    add("/* 由 tools/gen_font.py 生成，请勿手改。")
    add(f" * 字号 {size} px、4 bpp、PLAIN、无压缩；行高 {line_height}、基线 {base_line}。")
    add(" * 字符集见 tools/font_charset.txt；重新生成命令见 assets/README。 */")
    add("")
    add('#include "lvgl.h"')
    add("")
    add(f"#ifndef {guard}")
    add(f"#define {guard} 1")
    add(f"#endif")
    add("")
    add(f"#if {guard}")
    add("")
    add("static LV_ATTRIBUTE_LARGE_CONST const uint8_t glyph_bitmap[] = {")
    for i in range(0, len(bitmap), 12):
        chunk = ", ".join(f"0x{b:02x}" for b in bitmap[i:i + 12])
        add(f"    {chunk},")
    add("};")
    add("")
    add("static const lv_font_fmt_txt_glyph_dsc_t glyph_dsc[] = {")
    add("    { .bitmap_index = 0, .adv_w = 0, .box_w = 0, .box_h = 0, .ofs_x = 0, .ofs_y = 0 },")
    for glyph, start in zip(glyphs, starts):
        add(f"    {{ .bitmap_index = {start}, .adv_w = {glyph['adv_w']}, "
            f".box_w = {glyph['box_w']}, .box_h = {glyph['box_h']}, "
            f".ofs_x = {glyph['ofs_x']}, .ofs_y = {glyph['ofs_y']} }},")
    add("};")
    add("")

    if offsets:
        add("static const uint16_t unicode_list_1[] = {")
        for i in range(0, len(offsets), 12):
            chunk = ", ".join(f"0x{v:04x}" for v in offsets[i:i + 12])
            add(f"    {chunk},")
        add("};")
        add("")

    add("static const lv_font_fmt_txt_cmap_t cmaps[] = {")
    for cmap in cmaps:
        ctype = "LV_FONT_FMT_TXT_CMAP_SPARSE_TINY" if cmap["sparse"] \
            else "LV_FONT_FMT_TXT_CMAP_FORMAT0_TINY"
        if cmap["sparse"]:
            add("    {")
            add(f"        .range_start = {cmap['range_start']}, .range_length = {cmap['range_length']},")
            add(f"        .glyph_id_start = {cmap['glyph_id_start']},")
            add("        .unicode_list = unicode_list_1, .glyph_id_ofs_list = NULL,")
            add(f"        .list_length = {len(offsets)}, .type = {ctype}")
            add("    },")
        else:
            add(f"    {{ .range_start = {cmap['range_start']}, .range_length = {cmap['range_length']}, "
                f".glyph_id_start = {cmap['glyph_id_start']}, .unicode_list = NULL, "
                f".glyph_id_ofs_list = NULL, .list_length = 0, .type = {ctype} }},")
    add("};")
    add("")
    add("static const lv_font_fmt_txt_dsc_t font_dsc = {")
    add("    .glyph_bitmap = glyph_bitmap,")
    add("    .glyph_dsc = glyph_dsc,")
    add("    .cmaps = cmaps,")
    add("    .kern_dsc = NULL,")
    add("    .kern_scale = 0,")
    add(f"    .cmap_num = {len(cmaps)},")
    add("    .bpp = 4,")
    add("    .kern_classes = 0,")
    add("    .bitmap_format = 0,")
    add("    .stride = 0,")
    add("};")
    add("")
    add(f"const lv_font_t {name} = {{")
    add("    .get_glyph_dsc = lv_font_get_glyph_dsc_fmt_txt,")
    add("    .get_glyph_bitmap = lv_font_get_bitmap_fmt_txt,")
    add(f"    .line_height = {line_height},")
    add(f"    .base_line = {base_line},")
    add("    .subpx = LV_FONT_SUBPX_NONE,")
    add("    .underline_position = -2,")
    add("    .underline_thickness = 1,")
    add("    .dsc = &font_dsc,")
    add("};")
    add("")
    add(f"#endif /* {guard} */")
    add("")
    return "\n".join(lines)


# ---------------------------------------------------------------- 回读校验

_DS_RE = re.compile(
    r"\{ \.bitmap_index = (\d+), \.adv_w = (\d+), \.box_w = (\d+), \.box_h = (\d+), "
    r"\.ofs_x = (-?\d+), \.ofs_y = (-?\d+) \}")


def verify_c(text: str, glyphs: list[dict], offsets: list[int]) -> list[str]:
    """按 LVGL 的语义把生成的 C 解析回来，与栅格化结果逐像素比对。"""
    problems: list[str] = []

    bitmap_match = re.search(r"glyph_bitmap\[\] = \{(.*?)\};", text, re.S)
    if not bitmap_match:
        return ["找不到 glyph_bitmap"]
    bitmap = bytes(int(v, 16) for v in re.findall(r"0x([0-9a-f]{2})", bitmap_match.group(1)))

    ds_entries = _DS_RE.findall(text)[1:]   # 跳过 glyph 0
    if len(ds_entries) != len(glyphs):
        return [f"glyph_dsc 数量不符: C 里 {len(ds_entries)}，期望 {len(glyphs)}"]

    for glyph, entry in zip(glyphs, ds_entries):
        index, adv, box_w, box_h, ofs_x, ofs_y = (int(v) for v in entry)
        if (box_w, box_h, ofs_x, ofs_y, adv) != (
                glyph["box_w"], glyph["box_h"], glyph["ofs_x"], glyph["ofs_y"], glyph["adv_w"]):
            problems.append(f"U+{glyph['cp']:04X} 描述符不一致")
            continue
        expected = glyph["nibbles"]
        got = []
        for bit in range(box_w * box_h):
            byte = bitmap[index + bit // 2]
            got.append((byte >> 4) if bit % 2 == 0 else (byte & 0x0F))
        if list(expected) != got:
            problems.append(f"U+{glyph['cp']:04X} 位图不一致")

    # cmap 语义：ASCII 连续段 + 稀疏偏移表，逐个码点反查 glyph id 是否正确。
    sparse_start = None
    cmap_blocks = re.findall(r"\.range_start = (\d+), \.range_length = (\d+),", text)
    if len(cmap_blocks) >= 2 and offsets:
        sparse_start = int(cmap_blocks[1][0])
    for position, glyph in enumerate(glyphs):
        expected_gid = position + 1
        if glyph["cp"] <= ASCII_END:
            got_gid = 1 + (glyph["cp"] - ASCII_START)
        else:
            if sparse_start is None:
                problems.append(f"U+{glyph['cp']:04X} 缺少稀疏 cmap")
                continue
            try:
                got_gid = 96 + offsets.index(glyph["cp"] - sparse_start)
            except ValueError:
                problems.append(f"U+{glyph['cp']:04X} 不在稀疏偏移表里")
                continue
        if got_gid != expected_gid:
            problems.append(f"U+{glyph['cp']:04X} cmap 指向 glyph {got_gid}，期望 {expected_gid}")
    return problems


# ---------------------------------------------------------------- 入口

def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--font")
    parser.add_argument("--size", type=int)
    parser.add_argument("--charset", required=True)
    parser.add_argument("--output")
    parser.add_argument("--name")
    parser.add_argument("--check", action="store_true")
    parser.add_argument("sources", nargs="*", help="--check 时扫描的源码目录或文件")
    args = parser.parse_args()

    charset_path = Path(args.charset)
    if args.check:
        return run_check(charset_path, [Path(s) for s in args.sources] or [Path("main")])

    if not (args.font and args.size and args.output and args.name):
        parser.error("生成模式需要 --font --size --output --name")

    chars = read_charset(charset_path)
    cps = codepoints(chars)
    glyphs, line_height, base_line = rasterize(Path(args.font), args.size, cps)
    cmaps, offsets = build_cmap(cps)
    bitmap, starts = pack_bitmap(glyphs)

    text = emit_c(args.name, args.size, glyphs, line_height, base_line,
                  cmaps, offsets, bitmap, starts)

    problems = verify_c(text, glyphs, offsets)
    if problems:
        for problem in problems[:20]:
            print(f"ERROR {problem}", file=sys.stderr)
        return 1

    Path(args.output).write_text(text, encoding="utf-8", newline="\n")
    print(f"{args.name}: {len(cps)} 码位, {len(bitmap)} 字节位图, "
          f"行高 {line_height}, 基线 {base_line} -> {args.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
