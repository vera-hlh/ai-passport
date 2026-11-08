// main/pq_ui.c —— 页面、文字与选中行的最小实现，见 pq_ui.h。
#include "pq_ui.h"

#include "pq_fonts.h"

#include <stdio.h>

lv_obj_t *pq_ui_page_create(const char *title)
{
    lv_obj_t *screen = lv_screen_active();
    lv_obj_set_style_bg_color(screen, lv_color_hex(PQ_COLOR_BG), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);

    lv_obj_t *page = lv_obj_create(screen);
    lv_obj_set_size(page, LV_PCT(100), LV_PCT(100));
    lv_obj_set_pos(page, 0, 0);
    lv_obj_set_style_bg_opa(page, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(page, 0, 0);
    lv_obj_set_style_pad_all(page, 0, 0);
    lv_obj_clear_flag(page, LV_OBJ_FLAG_SCROLLABLE);

    if (title) {
        lv_obj_t *label = lv_label_create(page);
        lv_obj_set_style_text_font(label, &pq_font_20, 0);
        lv_obj_set_style_text_color(label, lv_color_hex(PQ_COLOR_ACCENT), 0);
        lv_label_set_text(label, title);
        lv_obj_set_pos(label, 14, 10);
    }
    return page;
}

void pq_ui_page_delete(lv_obj_t **page)
{
    if (!page || !*page) return;
    lv_obj_delete(*page);
    *page = NULL;
}

void pq_ui_hint(lv_obj_t *page, const char *text)
{
    if (!page) return;
    lv_obj_t *label = lv_label_create(page);
    lv_obj_set_style_text_font(label, &pq_font_16, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(PQ_COLOR_MUTED), 0);
    lv_obj_set_width(label, LV_PCT(92));
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(label, text);
    lv_obj_align(label, LV_ALIGN_BOTTOM_MID, 0, -12);
}

lv_obj_t *pq_ui_text(lv_obj_t *page, const char *text, int x, int y,
                     const lv_font_t *font, uint32_t color)
{
    if (!page) return NULL;
    lv_obj_t *label = lv_label_create(page);
    lv_obj_set_style_text_font(label, font ? font : &pq_font_16, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
    lv_label_set_text(label, text ? text : "");
    lv_obj_set_pos(label, x, y);
    return label;
}

void pq_ui_format_ppm(char *buf, size_t len, int32_t ppm)
{
    if (!buf || len == 0) return;
    const long magnitude = (long)(ppm < 0 ? -ppm : ppm);
    snprintf(buf, len, "%c%ld.%02ld%%", ppm < 0 ? '-' : '+',
             magnitude / 10000, (magnitude % 10000) / 100);
}

lv_obj_t *pq_ui_row(lv_obj_t *page, int x, int y, int w, int h, bool selected)
{
    if (!page) return NULL;
    lv_obj_t *row = lv_obj_create(page);
    lv_obj_set_size(row, w, h);
    lv_obj_set_pos(row, x, y);
    lv_obj_set_style_radius(row, 8, 0);
    /* 边框宽度恒为 2：LVGL 的内容区会扣掉边框，若选中行才加边框，
       选中与未选中的文字会差 2 px。未选中时把边框画成底色即可。 */
    lv_obj_set_style_border_width(row, 2, 0);
    lv_obj_set_style_border_color(
        row, lv_color_hex(selected ? PQ_COLOR_ACCENT : PQ_COLOR_BG), 0);
    lv_obj_set_style_bg_color(row, lv_color_hex(selected ? PQ_COLOR_PANEL : PQ_COLOR_BG), 0);
    lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    return row;
}
