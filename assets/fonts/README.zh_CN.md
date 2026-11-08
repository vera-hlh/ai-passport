<p align="right">
  <strong>简体中文</strong> · <a href="README.md">English</a>
</p>

# 生成的中文点阵字体

`pq_font_16.c` 与 `pq_font_20.c` 是从授权允许的简体中文字体生成的 LVGL 9 位图字体。
它们是构建输入：不要手改，也不要把源字体提交进仓库。

| 项 | 值 |
| --- | --- |
| 字族 | Noto Sans CJK SC Regular（思源黑体同源） |
| 源文件 | `NotoSansCJKsc-Regular.otf`，16 437 364 字节 |
| SHA-256 | `2c76254f6fc379fddfce0a7e84fb5385bb135d3e399294f6eeb6680d0365b74b` |
| 下载 | <https://raw.githubusercontent.com/notofonts/noto-cjk/main/Sans/OTF/SimplifiedChinese/NotoSansCJKsc-Regular.otf> |
| 许可 | SIL Open Font License 1.1——生成的位图可以再分发，OTF 本身不入库 |
| 字号 | 16 px 与 20 px，4 bpp，PLAIN（不压缩） |
| 覆盖 | 152 个码位：可打印 ASCII + `tools/font_charset.txt` 里的字符 |
| Flash 占用 | 字形位图约 12 KB + 18 KB，另有描述符开销 |

## 为什么不用 `lv_font_conv`

它的最后一版 1.5.3 在当前 Node 下产出的位图是坏的，设备上的表现是整屏文字乱码。
细节与推理见 [LVGL 9 的中文位图字体子集](../../docs/reference/shinku-chen/lvgl-cjk-font-subsets.zh_CN.md)。
`tools/gen_font.py` 直接用 Pillow 栅格化并写 LVGL 格式，再把生成的 C 解析回来、逐像素比对。

## 重新生成

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

## 增加字符

1. 把新的界面文案加进 `tools/font_charset.txt`（或修正已有字符串）；
2. `python tools/gen_font.py --check --charset tools/font_charset.txt main` 会列出缺失码点；
   仓库门禁跑的是同一条命令；
3. 两个字号都重新生成；行高跟随字体度量（24 px 与 30 px），换字族时要复查版面。
