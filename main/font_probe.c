// main/font_probe.c —— 中文字体探针（M1）。
//
// 目的有两个：
//   1. 证明 LVGL 内置的 Source Han Sans SC 16 CJK 子集能在本板上真正画出汉字
//      （默认的 Montserrat 14/20 一个汉字都没有，UTF-8 编译通过并不代表能显示）。
//   2. 把计划中的界面文案逐字对照该字体的 cmap，列出缺字 —— 这份缺字清单就是
//      M2 生成自有字体子集时的字符输入，避免“上线才发现豆腐块”。
#include "font_probe.h"

#include "app_home.h"
#include "bsp_display.h"
#include "pq_ui.h"

#include "esp_log.h"
#include "lvgl.h"

#include <stdint.h>
#include <stdio.h>

static const char *TAG = "pq_font";

LV_FONT_DECLARE(lv_font_source_han_sans_sc_16_cjk);

// 计划中的界面文案。改界面时同步改这里，探针会自动告诉你缺哪些字。
static const char *const PLANNED_TEXT[] = {
    "工牌管家",
    "硬件自检",
    "番茄钟",
    "体检报告",
    "深睡测试",
    "正在入睡",
    "请松开按键",
    "电量",
    "唤醒次数",
    "上次睡眠",
    "误差",
    "完成",
    "失败",
    "旧演示",
    "字体探针",
};

#define PLANNED_COUNT (sizeof(PLANNED_TEXT) / sizeof(PLANNED_TEXT[0]))
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

    for (size_t i = 0; i < PLANNED_COUNT; i++) {
        const char *cursor = PLANNED_TEXT[i];
        uint32_t cp;
        while ((cp = utf8_next(&cursor)) != 0) {
            out->total++;
            lv_font_glyph_dsc_t dsc;
            if (!lv_font_get_glyph_dsc(&lv_font_source_han_sans_sc_16_cjk, &dsc, cp, 0)) {
                if (out->missing < MISSING_MAX) out->missing_cp[out->missing] = cp;
                out->missing++;
                ESP_LOGW(TAG, "缺字 U+%04X（文案 \"%s\"）", (unsigned)cp, PLANNED_TEXT[i]);
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
        lv_label_set_text_fmt(s_coverage_label, "COVERED %lu / %lu",
                              (unsigned long)(coverage.total - coverage.missing),
                              (unsigned long)coverage.total);
        lv_obj_set_style_text_color(
            s_coverage_label,
            lv_color_hex(coverage.missing == 0 ? PQ_COLOR_OK : PQ_COLOR_WARN), 0);
    }

    if (s_missing_label) {
        if (coverage.missing == 0) {
            lv_label_set_text(s_missing_label, "no missing glyphs");
        } else {
            char buf[128];
            size_t used = 0;
            used += (size_t)snprintf(buf + used, sizeof buf - used, "MISSING ");
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
    s_page = pq_ui_page_create("FONT PROBE");

    pq_ui_text(s_page, "built-in Source Han Sans SC 16 CJK", 14, 36,
               &lv_font_montserrat_14, PQ_COLOR_MUTED);

    // 真正拿 CJK 字体画几行，肉眼确认不是豆腐块。
    for (size_t i = 0; i < 3 && i < PLANNED_COUNT; i++) {
        lv_obj_t *sample = pq_ui_text(s_page, PLANNED_TEXT[i], 14,
                                      64 + (int)i * 26,
                                      &lv_font_source_han_sans_sc_16_cjk, PQ_COLOR_INK);
        (void)sample;
    }

    s_coverage_label = pq_ui_text(s_page, "", 14, 152, &lv_font_montserrat_14,
                                  PQ_COLOR_ACCENT);
    s_missing_label = pq_ui_text(s_page, "", 14, 174, &lv_font_montserrat_14,
                                 PQ_COLOR_MUTED);
    lv_obj_set_width(s_missing_label, 216);
    lv_label_set_long_mode(s_missing_label, LV_LABEL_LONG_WRAP);

    pq_ui_hint(s_page, "LONG OK  BACK");
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
