// main/pq_fonts.h —— 由 tools/gen_font.py 生成的中文点阵字体。
//
// 源文件在 assets/fonts/，字符集见 tools/font_charset.txt。
// 改了界面文案后先跑 `python tools/gen_font.py --check ...`
// 再按 assets/README 的命令重新生成，不要手改 .c。
#pragma once

#include "lvgl.h"

LV_FONT_DECLARE(pq_font_16)
LV_FONT_DECLARE(pq_font_20)
