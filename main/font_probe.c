// main/font_probe.c —— 中文字体自检页。
//
// 两个用途：
//   1. 用生成的两套字号真渲染中文，肉眼确认不是方块/错位；
//   2. 把 main/ui_strings.h 的每个字逐码点对照 cmap，列出缺字——
//      缺字清单就是“该补字符集了”的信号，正常情况下应为 0。
#include "font_probe.h"

#include "app_home.h"
#include "bsp_display.h"
#include "pq_fonts.h"
#include "pq_ui.h"
#include "ui_strings.h"

#include "esp_log.h"
#include "lvgl.h"

#include <stdint.h>
#include <stdio.h>

static const char *TAG = "pq_font";

#define MISSING_MAX 12

static lv_obj_t *s_page;
static lv_obj_t *s_coverage_label;
static lv_obj_t *s_missing_label;
static bool s_active;

// 解一个 UTF-8 码点；遇到非法字节返回 0 并前进一字节。
static uint32_t utf8_next(const char **cursor)
{
    const unsigned char *p = (const unsigned char *)*cursor;
    if (*p == 0) return 0;

    uint32_t cp;
    int extra;
    if (*p < 0x80) {
        *cursor += 1;
        return *p;
    } else if ((*p & 0xE0) == 0xC0) {
        cp = *p & 0x1F;
        extra = 1;
    } else if ((*p & 0xF0) == 0xE0) {
        cp = *p & 0x0F;
        extra = 2;
    } else if ((*p & 0xF8) == 0xF0) {
        cp = *p & 0x07;
        extra = 3;
    } else {
        *cursor += 1;
        return 0;
    }

    for (int i = 1; i <= extra; i++) {
        if ((p[i] & 0xC0) != 0x80) {
            *cursor += 1;
            return 0;
        }
        cp = (cp << 6) | (p[i] & 0x3F);
    }
    *cursor += extra + 1;
    return cp;
}

typedef struct {
    uint32_t total;
    uint32_t missing;
    uint32_t missing_cp[MISSING_MAX];
} coverage_t;

static void check_coverage(coverage_t *out)
{
    out->total = 0;
    out->missing = 0;

    for (size_t i = 0; i < PQ_UI_STRING_COUNT; i++) {
        const char *cursor = PQ_UI_STRINGS[i];
        uint32_t cp;
        while ((cp = utf8_next(&cursor)) != 0) {
            out->total++;
            lv_font_glyph_dsc_t dsc;
            if (!lv_font_get_glyph_dsc(&pq_font_16, &dsc, cp, 0)) {
                if (out->missing < MISSING_MAX) out->missing_cp[out->missing] = cp;
                out->missing++;
                ESP_LOGW(TAG, "缺字 U+%04X（文案 \"%s\"）", (unsigned)cp, PQ_UI_STRINGS[i]);
            }
        }
    }
    ESP_LOGI(TAG, "字形覆盖: %lu/%lu 命中", (unsigned long)(out->total - out->missing),
             (unsigned long)out->total);
}

static void refresh(void)
{
    coverage_t coverage;
    check_coverage(&coverage);

    if (s_coverage_label) {
        char text[48];
        snprintf(text, sizeof text, "%s %lu / %lu", PQ_TXT_COVERAGE,
                 (unsigned long)(coverage.total - coverage.missing),
                 (unsigned long)coverage.total);
        lv_label_set_text(s_coverage_label, text);
        lv_obj_set_style_text_color(
            s_coverage_label,
            lv_color_hex(coverage.missing == 0 ? PQ_COLOR_OK : PQ_COLOR_WARN), 0);
    }

    if (s_missing_label) {
        if (coverage.missing == 0) {
            lv_label_set_text(s_missing_label, PQ_TXT_NO_MISSING);
        } else {
            char buf[128];
            size_t used = (size_t)snprintf(buf, sizeof buf, "%s ", PQ_TXT_MISSING);
            for (uint32_t i = 0; i < coverage.missing && i < MISSING_MAX; i++) {
                used += (size_t)snprintf(buf + used, sizeof buf - used, "%sU+%04X",
                                         i == 0 ? "" : " ", (unsigned)coverage.missing_cp[i]);
                if (used >= sizeof buf) break;
            }
            lv_label_set_text(s_missing_label, buf);
        }
    }
}

void font_probe_open(void)
{
    s_active = true;
    s_page = pq_ui_page_create(PQ_TXT_FONT_PROBE);

    pq_ui_text(s_page, PQ_TXT_HOME_TITLE, 14, 44, &pq_font_20, PQ_COLOR_INK);
    pq_ui_text(s_page, PQ_TXT_FONT_SAMPLE, 14, 80, &pq_font_16, PQ_COLOR_INK);

    char row[96];
    snprintf(row, sizeof row, "%s %s %s", PQ_TXT_SELF_CHECK, PQ_TXT_FOCUS, PQ_TXT_REPORT);
    pq_ui_text(s_page, row, 14, 110, &pq_font_16, PQ_COLOR_MUTED);

    snprintf(row, sizeof row, "%s %s %s", PQ_TXT_DEEP_SLEEP, PQ_TXT_SLEEPING, PQ_TXT_WAKE);
    pq_ui_text(s_page, row, 14, 138, &pq_font_16, PQ_COLOR_MUTED);

    s_coverage_label = pq_ui_text(s_page, "", 14, 178, &pq_font_16, PQ_COLOR_ACCENT);
    s_missing_label = pq_ui_text(s_page, "", 14, 204, &pq_font_16, PQ_COLOR_MUTED);
    lv_obj_set_width(s_missing_label, 212);
    lv_label_set_long_mode(s_missing_label, LV_LABEL_LONG_WRAP);

    lv_obj_t *hint = pq_ui_text(s_page, PQ_TXT_BACK_HINT, 0, 288, &pq_font_16,
                                PQ_COLOR_MUTED);
    lv_obj_set_width(hint, LV_PCT(100));
    lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_CENTER, 0);

    refresh();
    ESP_LOGI(TAG, "字体探针页已建立");
}

bool font_probe_is_active(void)
{
    return s_active;
}

void font_probe_handle_key(bsp_btn_t btn, bsp_btn_ev_t ev)
{
    if (!s_active) return;
    if (ev != BSP_BTN_LONG || btn != BSP_BTN_OK) return;
    if (!bsp_lvgl_lock(300)) return;
    s_active = false;
    pq_ui_page_delete(&s_page);
    s_coverage_label = s_missing_label = NULL;
    app_home_open();
    bsp_lvgl_unlock();
}
