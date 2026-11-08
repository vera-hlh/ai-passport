// main/pq_ui.h —— 本应用自己的界面基础件(配色、页面、文字)。
// 不复用 demo 菜单的 ui_pixel 外壳：派生应用必须有自己的视觉与结构。
#pragma once

#include "lvgl.h"

#include <stdint.h>

// 深色工牌风：底色近黑、青绿点缀、白字。
#define PQ_COLOR_BG 0x0E1218
#define PQ_COLOR_PANEL 0x18202B
#define PQ_COLOR_INK 0xF2F5F7
#define PQ_COLOR_MUTED 0x8B98A8
#define PQ_COLOR_ACCENT 0x3FD2C7
#define PQ_COLOR_WARN 0xFF7A6B
#define PQ_COLOR_OK 0x7BD88F

// 建立一个满屏页面并写上标题；返回页面根对象，退出时必须用 pq_ui_page_delete() 删除。
lv_obj_t *pq_ui_page_create(const char *title);

// 删除页面(root 置空)。调用方必须持 LVGL 锁。
void pq_ui_page_delete(lv_obj_t **page);

// 在页面底部写一行提示(浅灰、14 px)。
void pq_ui_hint(lv_obj_t *page, const char *text);

// 在页面里放一行文字，返回标签对象以便后续更新。
lv_obj_t *pq_ui_text(lv_obj_t *page, const char *text, int x, int y,
                     const lv_font_t *font, uint32_t color);

// 带色块的行(用于选中项)：返回行容器，标签由调用方创建。
lv_obj_t *pq_ui_row(lv_obj_t *page, int x, int y, int w, int h, bool selected);

// 把 ppm 误差格式化成带符号的百分比，例如 +0.20% / -1.35%。
// 直接用整数除法会把 |ppm| < 10000 的负值写成 0，故单独提供。
void pq_ui_format_ppm(char *buf, size_t len, int32_t ppm);
