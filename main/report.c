// main/report.c —— 体检报告页。
//
// 只读展示 quest 模块的结果（结果由 quest 负责保存：RTC 内存 + NVS）。
// 上下移动查看每一关的详情；长按确定返回主屏；长按上键清空报告。
#include "report.h"

#include "app_home.h"
#include "bsp_display.h"
#include "pq_fonts.h"
#include "pq_ui.h"
#include "quest.h"
#include "ui_strings.h"

#include "esp_log.h"
#include "lvgl.h"

#include <stdint.h>
#include <stdio.h>

static const char *TAG = "pq_report";

#define ROW_Y0 96
#define ROW_STEP 32
#define ROW_H 32
#define MAX_ROWS 8

static lv_obj_t *s_page;
static lv_obj_t *s_rows[MAX_ROWS];
static lv_obj_t *s_detail;
static int s_count;
static int s_selected;
static bool s_active;

static uint32_t status_color(int status)
{
    switch (status) {
        case 1: return PQ_COLOR_OK;
        case 2: return PQ_COLOR_WARN;
        default: return PQ_COLOR_MUTED;
    }
}

static void detail_refresh(void)
{
    if (!s_detail) return;
    const char *text = quest_results_detail(s_selected);
    if (text && text[0]) {
        lv_label_set_text(s_detail, text);
        lv_obj_set_style_text_color(s_detail, lv_color_hex(PQ_COLOR_MUTED), 0);
    } else {
        lv_label_set_text(s_detail, PQ_TXT_PENDING);
        lv_obj_set_style_text_color(s_detail, lv_color_hex(PQ_COLOR_MUTED), 0);
    }
}

static void build(void)
{
    if (s_page) pq_ui_page_delete(&s_page);
    s_page = pq_ui_page_create(PQ_TXT_REPORT);

    s_count = quest_results_count();
    if (s_count > MAX_ROWS) s_count = MAX_ROWS;

    int passed = 0;
    for (int i = 0; i < s_count; i++) {
        if (quest_results_status(i) == 1) passed++;
        lv_obj_t *row = pq_ui_row(s_page, 12, ROW_Y0 + i * ROW_STEP, 216, ROW_H,
                                  i == s_selected);
        lv_obj_t *name = lv_label_create(row);
        lv_obj_set_style_text_font(name, &pq_font_16, 0);
        lv_obj_set_style_text_color(name, lv_color_hex(PQ_COLOR_INK), 0);
        lv_label_set_text(name, quest_results_title(i));
        lv_obj_set_height(name, 24);
        lv_obj_align(name, LV_ALIGN_LEFT_MID, 10, 2);

        lv_obj_t *state = lv_label_create(row);
        lv_obj_set_style_text_font(state, &pq_font_16, 0);
        lv_obj_set_style_text_color(
            state, lv_color_hex(status_color(quest_results_status(i))), 0);
        lv_label_set_text(state, quest_results_status_text(i));
        lv_obj_set_height(state, 24);
        lv_obj_align(state, LV_ALIGN_RIGHT_MID, -10, 2);
        s_rows[i] = row;
    }

    char text[48];
    snprintf(text, sizeof text, "%s %d/%d", PQ_TXT_QUEST_REPORT, passed, s_count);
    pq_ui_text(s_page, text, 14, 66, &pq_font_16, PQ_COLOR_ACCENT);

    s_detail = pq_ui_text(s_page, "", 0, 292, &pq_font_16, PQ_COLOR_MUTED);
    lv_obj_set_width(s_detail, LV_PCT(100));
    lv_obj_set_style_text_align(s_detail, LV_TEXT_ALIGN_CENTER, 0);
    detail_refresh();
}

void report_open(void)
{
    s_active = true;
    s_selected = 0;
    build();
    ESP_LOGI(TAG, "报告页已建立");
}

bool report_is_active(void)
{
    return s_active;
}

static void close_to_home(void)
{
    s_active = false;
    pq_ui_page_delete(&s_page);
    s_detail = NULL;
    app_home_open();
}

void report_handle_key(bsp_btn_t btn, bsp_btn_ev_t ev)
{
    if (!s_active) return;
    if (!bsp_lvgl_lock(300)) return;

    if (ev == BSP_BTN_LONG && btn == BSP_BTN_OK) {
        close_to_home();
        bsp_lvgl_unlock();
        return;
    }
    if (ev == BSP_BTN_LONG && btn == BSP_BTN_UP) {
        quest_results_clear();
        s_selected = 0;
        build();
        bsp_lvgl_unlock();
        return;
    }
    if (ev == BSP_BTN_CLICK) {
        if (btn == BSP_BTN_UP && s_count > 0) {
            s_selected = (s_selected + s_count - 1) % s_count;
            build();
        } else if (btn == BSP_BTN_DOWN && s_count > 0) {
            s_selected = (s_selected + 1) % s_count;
            build();
        }
    }
    bsp_lvgl_unlock();
}
