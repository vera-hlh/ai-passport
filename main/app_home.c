// main/app_home.c —— 主屏：状态区 + 五个入口（中文，使用生成的中文字体）。
//
// 这一页是本应用自己的界面，不复用 demo 菜单的卡片/像素吉祥物外壳。
// 三键语义与全局一致：上/下移动，OK 进入，长按 OK 在本页无动作。
#include "app_home.h"

#include "bsp_battery.h"
#include "bsp_display.h"
#include "demo_menu.h"
#include "font_probe.h"
#include "pq_fonts.h"
#include "pq_ui.h"
#include "power.h"
#include "quest.h"
#include "ui_strings.h"

#include "esp_log.h"
#include "lvgl.h"

#include <stdint.h>
#include <stdio.h>

static const char *TAG = "pq_home";

#define ENTRY_COUNT 5
#define ROW_Y0 126
#define ROW_STEP 30
#define ROW_H 28

typedef struct {
    const char *name;
    const char *sub;   // 右侧说明；NULL 表示可进入
} home_entry_t;

static const home_entry_t ENTRIES[ENTRY_COUNT] = {
    { PQ_TXT_SELF_CHECK, NULL },
    { PQ_TXT_FOCUS, "M4" },
    { PQ_TXT_REPORT, "M3" },
    { PQ_TXT_FONT_PROBE, "M1" },
    { PQ_TXT_LEGACY, NULL },
};

static lv_obj_t *s_page;
static lv_obj_t *s_list;
static lv_obj_t *s_status_batt;
static lv_obj_t *s_status_sleep;
static lv_obj_t *s_status_last;
static lv_obj_t *s_hint;
static int s_selected;
static bool s_active;

static const char *wake_name(pq_wake_t wake)
{
    switch (wake) {
        case PQ_WAKE_TIMER: return PQ_TXT_TIMER;
        case PQ_WAKE_KEY: return PQ_TXT_KEY;
        default: return "--";
    }
}

static void format_last_sleep(char *buf, size_t len)
{
    const pq_power_info_t *info = pq_power_info();
    if (!info->restored) {
        snprintf(buf, len, "%s  --", PQ_TXT_LAST_SLEEP);
        return;
    }
    if (!info->last.valid) {
        snprintf(buf, len, "%s  %s", PQ_TXT_LAST_SLEEP, PQ_TXT_UNMEASURED);
        return;
    }
    char percent[16];
    pq_ui_format_ppm(percent, sizeof percent, info->last.error_ppm);
    snprintf(buf, len, "%s  %.2fs  %s", PQ_TXT_LAST_SLEEP,
             (double)info->last.measured_us / 1000000.0, percent);
}

static void refresh_status(void)
{
    if (!s_status_batt || !s_status_sleep || !s_status_last) return;
    const pq_power_info_t *info = pq_power_info();

    char text[64];
    snprintf(text, sizeof text, "%s %d%%  %dmV", PQ_TXT_BATTERY,
             bsp_battery_soc(), bsp_battery_mv());
    lv_label_set_text(s_status_batt, text);

    snprintf(text, sizeof text, "%s %lu  %s", PQ_TXT_WAKE_COUNT,
             (unsigned long)info->count, wake_name(info->wake));
    lv_label_set_text(s_status_sleep, text);

    format_last_sleep(text, sizeof text);
    lv_label_set_text(s_status_last, text);
}

static void list_build(void)
{
    if (!s_page) return;
    if (s_list) {
        lv_obj_delete(s_list);
        s_list = NULL;
    }
    s_list = lv_obj_create(s_page);
    lv_obj_set_size(s_list, LV_PCT(100), LV_PCT(100));
    lv_obj_set_pos(s_list, 0, 0);
    lv_obj_set_style_bg_opa(s_list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_list, 0, 0);
    lv_obj_set_style_pad_all(s_list, 0, 0);
    lv_obj_clear_flag(s_list, LV_OBJ_FLAG_SCROLLABLE);

    for (int i = 0; i < ENTRY_COUNT; i++) {
        const bool selected = i == s_selected;
        lv_obj_t *row = pq_ui_row(s_list, 12, ROW_Y0 + i * ROW_STEP, 216, ROW_H, selected);
        lv_obj_t *label = lv_label_create(row);
        lv_obj_set_style_text_font(label, &pq_font_16, 0);
        lv_obj_set_style_text_color(
            label, lv_color_hex(selected ? PQ_COLOR_INK : PQ_COLOR_MUTED), 0);
        lv_label_set_text(label, ENTRIES[i].name);
        lv_obj_align(label, LV_ALIGN_LEFT_MID, 10, 0);

        if (ENTRIES[i].sub) {
            lv_obj_t *tag = lv_label_create(row);
            lv_obj_set_style_text_font(tag, &pq_font_16, 0);
            lv_obj_set_style_text_color(tag, lv_color_hex(PQ_COLOR_MUTED), 0);
            lv_label_set_text(tag, ENTRIES[i].sub);
            lv_obj_align(tag, LV_ALIGN_RIGHT_MID, -10, 0);
        }
    }
}

// 提示行同时承担“临时消息”的显示：有消息时用警示色显示消息，清空后恢复默认提示。
static void set_notice(const char *text)
{
    if (!s_hint) return;
    if (text && text[0]) {
        lv_label_set_text(s_hint, text);
        lv_obj_set_style_text_color(s_hint, lv_color_hex(PQ_COLOR_WARN), 0);
    } else {
        lv_label_set_text(s_hint, PQ_TXT_HOME_HINT);
        lv_obj_set_style_text_color(s_hint, lv_color_hex(PQ_COLOR_MUTED), 0);
    }
}

void app_home_open(void)
{
    s_active = true;
    s_page = pq_ui_page_create(PQ_TXT_HOME_TITLE);

    // 状态区：一个暗色面板，三行读数。
    lv_obj_t *panel = lv_obj_create(s_page);
    lv_obj_set_size(panel, 216, 76);
    lv_obj_set_pos(panel, 12, 44);
    lv_obj_set_style_bg_color(panel, lv_color_hex(PQ_COLOR_PANEL), 0);
    lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(panel, 10, 0);
    lv_obj_set_style_border_width(panel, 0, 0);
    lv_obj_set_style_pad_all(panel, 4, 0);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);

    s_status_batt = pq_ui_text(panel, "", 4, 0, &pq_font_16, PQ_COLOR_INK);
    s_status_sleep = pq_ui_text(panel, "", 4, 24, &pq_font_16, PQ_COLOR_ACCENT);
    s_status_last = pq_ui_text(panel, "", 4, 48, &pq_font_16, PQ_COLOR_MUTED);
    refresh_status();

    s_selected = 0;
    list_build();

    s_hint = pq_ui_text(s_page, PQ_TXT_HOME_HINT, 0, 288, &pq_font_16, PQ_COLOR_MUTED);
    lv_obj_set_width(s_hint, LV_PCT(100));
    lv_obj_set_style_text_align(s_hint, LV_TEXT_ALIGN_CENTER, 0);

    ESP_LOGI(TAG, "主屏已建立");
}

bool app_home_is_active(void)
{
    return s_active;
}

static void goto_other_page(void)
{
    s_active = false;
    pq_ui_page_delete(&s_page);
    s_list = NULL;
    s_status_batt = s_status_sleep = s_status_last = s_hint = NULL;
}

void app_home_handle_key(bsp_btn_t btn, bsp_btn_ev_t ev)
{
    if (!s_active) return;
    if (ev != BSP_BTN_CLICK) return;
    if (!bsp_lvgl_lock(300)) return;

    if (btn == BSP_BTN_UP) {
        s_selected = (s_selected + ENTRY_COUNT - 1) % ENTRY_COUNT;
        set_notice("");
        list_build();
    } else if (btn == BSP_BTN_DOWN) {
        s_selected = (s_selected + 1) % ENTRY_COUNT;
        set_notice("");
        list_build();
    } else if (btn == BSP_BTN_OK) {
        switch (s_selected) {
            case 0:
                goto_other_page();
                quest_open();
                break;
            case 1:
                set_notice(PQ_TXT_COMING_SOON);
                break;
            case 2:
                set_notice(PQ_TXT_COMING_SOON);
                break;
            case 3:
                goto_other_page();
                font_probe_open();
                break;
            case 4:
                goto_other_page();
                demo_menu_show();
                break;
            default:
                break;
        }
    }
    bsp_lvgl_unlock();
}
