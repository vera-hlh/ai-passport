// main/quest.c —— 硬件自检关卡。
//
// M2 覆盖 L1–L5（屏幕、背光、接口扫描、电量、按键），并保留 M1 的 L12 深睡关卡。
// 能自动判的自动判；必须人眼的（屏幕、背光）明确记“人工确认”，不假装客观。
// 结果先保存在内存并逐条打到串口，NVS 落盘放到 M3。
#include "quest.h"

#include "app_home.h"
#include "bsp_battery.h"
#include "bsp_button.h"
#include "bsp_display.h"
#include "bsp_i2c.h"
#include "bsp_pins.h"
#include "pq_fonts.h"
#include "pq_ui.h"
#include "power.h"
#include "ui_strings.h"

#include "esp_attr.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static const char *TAG = "pq_quest";

#define LEVEL_COUNT 6
#define L12_INDEX (LEVEL_COUNT - 1)
#define L5_INDEX 4
#define ROW_Y0 96
#define ROW_STEP 32
#define ROW_H 32
#define DETAIL_MAX 32

typedef enum {
    QS_PENDING = 0,
    QS_PASS,
    QS_FAIL,
} quest_status_t;

typedef struct {
    const char *title;
    void (*enter)(void);
    void (*key)(bsp_btn_t btn, bsp_btn_ev_t ev);
} quest_level_t;

static void l1_enter(void); static void l1_key(bsp_btn_t, bsp_btn_ev_t);
static void l2_enter(void); static void l2_key(bsp_btn_t, bsp_btn_ev_t);
static void l3_enter(void); static void l3_key(bsp_btn_t, bsp_btn_ev_t);
static void l4_enter(void); static void l4_key(bsp_btn_t, bsp_btn_ev_t);
static void l5_enter(void); static void l5_key(bsp_btn_t, bsp_btn_ev_t);
static void l12_enter(void); static void l12_key(bsp_btn_t, bsp_btn_ev_t);

static const quest_level_t LEVELS[LEVEL_COUNT] = {
    { PQ_TXT_LEVEL_DISPLAY, l1_enter, l1_key },
    { PQ_TXT_LEVEL_BACKLIGHT, l2_enter, l2_key },
    { PQ_TXT_LEVEL_I2C, l3_enter, l3_key },
    { PQ_TXT_LEVEL_BATTERY, l4_enter, l4_key },
    { PQ_TXT_LEVEL_BUTTON, l5_enter, l5_key },
    { PQ_TXT_DEEP_SLEEP, l12_enter, l12_key },
};

// 结果放 RTC 内存：L12 的深睡会复位芯片，普通 RAM 里的结果会全部丢失，
// 表现为“做完全部关卡、一测深睡，报告就归零”。RTC 内存在深睡与软复位后都保留，
// 掉电才丢；用 magic 区分“上次会话的结果”与上电随机值。
#define QUEST_RESULTS_MAGIC 0x51523532u   // 'QR52'
static RTC_DATA_ATTR uint32_t s_results_magic;
static RTC_DATA_ATTR quest_status_t s_status[LEVEL_COUNT];
static RTC_DATA_ATTR char s_detail[LEVEL_COUNT][DETAIL_MAX];
static RTC_DATA_ATTR uint32_t s_l12_recorded_count;

// NVS 里的持久副本：RTC 内存掉电就没了，NVS 才是跨断电的存储。
// 注意整片烧写合并镜像会把 NVS 区间填 0xFF，报告可能被重置，这一点在报告页提示。
#define QUEST_NVS_NAMESPACE "pq_quest"
#define QUEST_NVS_KEY "results"
#define QUEST_BLOB_MAGIC 0x51424c31u   // 'QBL1'

typedef struct {
    uint32_t magic;
    uint32_t version;
    int32_t status[LEVEL_COUNT];
    char detail[LEVEL_COUNT][DETAIL_MAX];
} quest_blob_t;

static void results_save(void)
{
    nvs_handle_t handle;
    if (nvs_open(QUEST_NVS_NAMESPACE, NVS_READWRITE, &handle) != ESP_OK) return;
    quest_blob_t blob = { .magic = QUEST_BLOB_MAGIC, .version = 1 };
    for (int i = 0; i < LEVEL_COUNT; i++) {
        blob.status[i] = (int32_t)s_status[i];
        memcpy(blob.detail[i], s_detail[i], DETAIL_MAX);
    }
    esp_err_t e = nvs_set_blob(handle, QUEST_NVS_KEY, &blob, sizeof blob);
    if (e == ESP_OK) e = nvs_commit(handle);
    if (e != ESP_OK) ESP_LOGW(TAG, "报告写入 NVS 失败: %s", esp_err_to_name(e));
    nvs_close(handle);
}

static bool results_load(void)
{
    nvs_handle_t handle;
    if (nvs_open(QUEST_NVS_NAMESPACE, NVS_READONLY, &handle) != ESP_OK) return false;
    quest_blob_t blob;
    size_t len = sizeof blob;
    const bool ok = nvs_get_blob(handle, QUEST_NVS_KEY, &blob, &len) == ESP_OK &&
                    len == sizeof blob && blob.magic == QUEST_BLOB_MAGIC;
    nvs_close(handle);
    if (!ok) return false;
    for (int i = 0; i < LEVEL_COUNT; i++) {
        s_status[i] = (quest_status_t)blob.status[i];
        memcpy(s_detail[i], blob.detail[i], DETAIL_MAX);
        s_detail[i][DETAIL_MAX - 1] = 0;
    }
    ESP_LOGI(TAG, "已从 NVS 恢复上次报告");
    return true;
}

static void results_ensure(void)
{
    if (s_results_magic == QUEST_RESULTS_MAGIC) return;
    memset(s_status, 0, sizeof s_status);
    memset(s_detail, 0, sizeof s_detail);
    s_l12_recorded_count = 0;
    results_load();
    s_results_magic = QUEST_RESULTS_MAGIC;
}

static void results_clear(void)
{
    memset(s_status, 0, sizeof s_status);
    memset(s_detail, 0, sizeof s_detail);
    s_l12_recorded_count = 0;
    results_save();
}

static lv_obj_t *s_page;
static lv_obj_t *s_row_states[LEVEL_COUNT];
static lv_obj_t *s_summary;
static int s_selected;
static int s_current = -1;
static bool s_active;

// ------------------------------------------------------------------ 公共部分

static const char *status_text(quest_status_t status)
{
    switch (status) {
        case QS_PASS: return PQ_TXT_PASS;
        case QS_FAIL: return PQ_TXT_FAIL;
        default: return PQ_TXT_PENDING;
    }
}

static uint32_t status_color(quest_status_t status)
{
    switch (status) {
        case QS_PASS: return PQ_COLOR_OK;
        case QS_FAIL: return PQ_COLOR_WARN;
        default: return PQ_COLOR_MUTED;
    }
}

static void report_log(void)
{
    int passed = 0;
    ESP_LOGI(TAG, "==== %s ====", PQ_TXT_QUEST_REPORT);
    for (int i = 0; i < LEVEL_COUNT; i++) {
        if (s_status[i] == QS_PASS) passed++;
        ESP_LOGI(TAG, "  %s: %s %s", LEVELS[i].title, status_text(s_status[i]),
                 s_detail[i]);
    }
    ESP_LOGI(TAG, "  合计 %d/%d", passed, LEVEL_COUNT);
}

static void page_close(void)
{
    pq_ui_page_delete(&s_page);
    for (int i = 0; i < LEVEL_COUNT; i++) s_row_states[i] = NULL;
    s_summary = NULL;
}

// 结束当前关卡：写结果、打日志、回关卡列表。
static void finish(int index, quest_status_t status, const char *fmt, ...)
{
    s_status[index] = status;
    if (fmt) {
        va_list args;
        va_start(args, fmt);
        vsnprintf(s_detail[index], DETAIL_MAX, fmt, args);
        va_end(args);
    }
    ESP_LOGI(TAG, "%s: %s %s", LEVELS[index].title, status_text(status),
             s_detail[index]);
    report_log();
    results_save();
    s_current = -1;
    page_close();
    quest_open();
}

static lv_obj_t *level_page(const char *title, lv_obj_t **body)
{
    lv_obj_t *page = pq_ui_page_create(title);
    if (body) {
        *body = pq_ui_text(page, "", 14, 70, &pq_font_20, PQ_COLOR_INK);
    }
    lv_obj_t *hint = pq_ui_text(page, PQ_TXT_BACK_HINT, 0, 286, &pq_font_16,
                                PQ_COLOR_MUTED);
    lv_obj_set_width(hint, LV_PCT(100));
    lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_CENTER, 0);
    return page;
}

// ------------------------------------------------------------------ 关卡列表

static void list_refresh(void)
{
    int passed = 0;
    for (int i = 0; i < LEVEL_COUNT; i++) {
        if (s_status[i] == QS_PASS) passed++;
        if (!s_row_states[i]) continue;
        lv_label_set_text(s_row_states[i], status_text(s_status[i]));
        lv_obj_set_style_text_color(s_row_states[i],
                                    lv_color_hex(status_color(s_status[i])), 0);
    }
    if (s_summary) {
        char text[48];
        snprintf(text, sizeof text, "%s %d/%d", PQ_TXT_QUEST_REPORT, passed, LEVEL_COUNT);
        lv_label_set_text(s_summary, text);
    }
}

void quest_open(void)
{
    results_ensure();
    s_active = true;
    s_current = -1;

    if (s_page) page_close();
    s_page = pq_ui_page_create(PQ_TXT_SELF_CHECK);

    for (int i = 0; i < LEVEL_COUNT; i++) {
        lv_obj_t *row = pq_ui_row(s_page, 12, ROW_Y0 + i * ROW_STEP, 216, ROW_H,
                                  i == s_selected);
        lv_obj_t *name = lv_label_create(row);
        lv_obj_set_style_text_font(name, &pq_font_16, 0);
        lv_obj_set_style_text_color(name, lv_color_hex(PQ_COLOR_INK), 0);
        lv_label_set_text(name, LEVELS[i].title);
        lv_obj_set_height(name, 24);
        lv_obj_align(name, LV_ALIGN_LEFT_MID, 10, 2);

        lv_obj_t *state = lv_label_create(row);
        lv_obj_set_style_text_font(state, &pq_font_16, 0);
        lv_obj_set_style_text_color(state, lv_color_hex(status_color(s_status[i])), 0);
        lv_label_set_text(state, status_text(s_status[i]));
        lv_obj_set_height(state, 24);
        lv_obj_align(state, LV_ALIGN_RIGHT_MID, -10, 2);
        s_row_states[i] = state;
    }

    s_summary = pq_ui_text(s_page, "", 14, 66, &pq_font_16, PQ_COLOR_ACCENT);
    list_refresh();

    lv_obj_t *hint = pq_ui_text(s_page, PQ_TXT_HOME_HINT, 0, 292, &pq_font_16,
                                PQ_COLOR_MUTED);
    lv_obj_set_width(hint, LV_PCT(100));
    lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_CENTER, 0);
}

bool quest_is_active(void)
{
    return s_active;
}

static void enter_level(int index)
{
    s_current = index;
    page_close();
    LEVELS[index].enter();
}

static void leave_level(void)
{
    s_current = -1;
    page_close();
    quest_open();
}

void quest_open_last_sleep(void)
{
    quest_open();
    enter_level(L12_INDEX);
}

void quest_close(void)
{
    s_active = false;
    page_close();
}

// ------------------------------------------------------------------ L1 屏幕

#define L1_COLOR_COUNT 5
static const uint32_t L1_COLORS[L1_COLOR_COUNT] = {
    0xFFFFFF, 0x000000, 0xFF0000, 0x00C040, 0x0040FF,
};
static const char *const L1_NAMES[L1_COLOR_COUNT] = {
    PQ_TXT_WHITE, PQ_TXT_BLACK, PQ_TXT_RED, PQ_TXT_GREEN, PQ_TXT_BLUE,
};
#define L1_GRAY_STEP L1_COLOR_COUNT

static lv_obj_t *s_l1_title;
static lv_obj_t *s_l1_bars[16];
static int s_l1_step;

static void l1_apply(void)
{
    if (s_l1_step < L1_COLOR_COUNT) {
        if (s_l1_title) lv_label_set_text(s_l1_title, L1_NAMES[s_l1_step]);
        lv_obj_set_style_bg_color(s_page, lv_color_hex(L1_COLORS[s_l1_step]), 0);
        lv_obj_set_style_bg_opa(s_page, LV_OPA_COVER, 0);
        return;
    }

    // 灰阶：16 条 16 px 高的横带铺满整屏，检查灰阶台阶与偏色。
    if (s_l1_title) lv_label_set_text(s_l1_title, PQ_TXT_GRAYSCALE);
    lv_obj_set_style_bg_color(s_page, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(s_page, LV_OPA_COVER, 0);
    for (int i = 0; i < 16; i++) {
        if (s_l1_bars[i]) continue;
        lv_obj_t *bar = lv_obj_create(s_page);
        lv_obj_set_size(bar, LV_PCT(100), 16);
        lv_obj_set_pos(bar, 0, i * 16);
        lv_obj_set_style_border_width(bar, 0, 0);
        lv_obj_set_style_radius(bar, 0, 0);
        lv_obj_set_style_pad_all(bar, 0, 0);
        const uint32_t level = (uint32_t)(i * 255 / 15);
        lv_obj_set_style_bg_color(bar, lv_color_hex(level * 0x010101), 0);
        lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
        s_l1_bars[i] = bar;
    }
}

static void l1_enter(void)
{
    s_l1_step = 0;
    memset(s_l1_bars, 0, sizeof s_l1_bars);
    s_page = pq_ui_page_create(PQ_TXT_LEVEL_DISPLAY);
    s_l1_title = pq_ui_text(s_page, "", 0, 4, &pq_font_20, PQ_COLOR_MUTED);
    lv_obj_set_width(s_l1_title, LV_PCT(100));
    lv_obj_set_style_text_align(s_l1_title, LV_TEXT_ALIGN_CENTER, 0);
    pq_ui_hint(s_page, PQ_TXT_NEXT);
    l1_apply();
}

static void l1_key(bsp_btn_t btn, bsp_btn_ev_t ev)
{
    if (ev != BSP_BTN_CLICK || btn != BSP_BTN_OK) return;
    if (s_l1_step < L1_GRAY_STEP) {
        s_l1_step++;
        l1_apply();
        return;
    }
    finish(0, QS_PASS, "%s", PQ_TXT_MANUAL);
}

// ------------------------------------------------------------------ L2 背光

static lv_obj_t *s_l2_label;
static int s_l2_pct = 50;

static void l2_apply(void)
{
    bsp_display_backlight((uint8_t)s_l2_pct);
    if (s_l2_label) {
        char text[32];
        snprintf(text, sizeof text, "%s %d%%", PQ_TXT_BRIGHTNESS, s_l2_pct);
        lv_label_set_text(s_l2_label, text);
    }
}

static void l2_enter(void)
{
    s_l2_pct = 50;
    s_page = level_page(PQ_TXT_LEVEL_BACKLIGHT, &s_l2_label);
    l2_apply();
}

static void l2_key(bsp_btn_t btn, bsp_btn_ev_t ev)
{
    if (ev != BSP_BTN_CLICK) return;
    if (btn == BSP_BTN_UP) {
        s_l2_pct = s_l2_pct + 10 > 100 ? 100 : s_l2_pct + 10;
        l2_apply();
    } else if (btn == BSP_BTN_DOWN) {
        s_l2_pct = s_l2_pct - 10 < 10 ? 10 : s_l2_pct - 10;
        l2_apply();
    } else if (btn == BSP_BTN_OK) {
        bsp_display_backlight(100);
        finish(1, QS_PASS, "%s %d%%", PQ_TXT_MANUAL, s_l2_pct);
    }
}

// ------------------------------------------------------------------ L3 接口扫描

static bool s_l3_es8311;
static bool s_l3_cw2017;
static lv_obj_t *s_l3_label;

static void l3_enter(void)
{
    s_l3_es8311 = bsp_i2c_probe(BSP_I2C_ES8311_ADDR);
    s_l3_cw2017 = bsp_i2c_probe(BSP_I2C_CW2017_ADDR);

    s_page = level_page(PQ_TXT_LEVEL_I2C, &s_l3_label);
    char text[64];
    snprintf(text, sizeof text, "0x18 %s   0x63 %s",
             s_l3_es8311 ? PQ_TXT_PASS : PQ_TXT_FAIL,
             s_l3_cw2017 ? PQ_TXT_PASS : PQ_TXT_FAIL);
    lv_label_set_text(s_l3_label, text);
    lv_obj_set_style_text_color(s_l3_label,
        lv_color_hex(s_l3_es8311 && s_l3_cw2017 ? PQ_COLOR_OK : PQ_COLOR_WARN), 0);
}

static void l3_key(bsp_btn_t btn, bsp_btn_ev_t ev)
{
    if (ev != BSP_BTN_CLICK || btn != BSP_BTN_OK) return;
    if (s_l3_es8311 && s_l3_cw2017) {
        finish(2, QS_PASS, "0x18 0x63");
    } else if (!s_l3_cw2017) {
        finish(2, QS_FAIL, "-- 0x63");
    } else {
        finish(2, QS_FAIL, "0x18 --");
    }
}

// ------------------------------------------------------------------ L4 电量计

static bool s_l4_ok;
static lv_obj_t *s_l4_label;

static void l4_enter(void)
{
    const int soc1 = bsp_battery_soc();
    const int mv1 = bsp_battery_mv();
    vTaskDelay(pdMS_TO_TICKS(120));
    const int soc2 = bsp_battery_soc();
    const int mv2 = bsp_battery_mv();

    s_l4_ok = soc1 >= 0 && soc2 >= 0 && mv1 >= 3000 && mv1 <= 4300 &&
              mv2 >= 3000 && mv2 <= 4300 && (soc2 - soc1 <= 2) && (soc1 - soc2 <= 2);

    s_page = level_page(PQ_TXT_LEVEL_BATTERY, &s_l4_label);
    char text[64];
    if (soc1 < 0 || mv1 < 0) {
        snprintf(text, sizeof text, "%s", PQ_TXT_UNMEASURED_ERR);
    } else {
        snprintf(text, sizeof text, "%d%%   %d mV", soc1, mv1);
    }
    lv_label_set_text(s_l4_label, text);
    lv_obj_set_style_text_color(s_l4_label,
        lv_color_hex(s_l4_ok ? PQ_COLOR_OK : PQ_COLOR_WARN), 0);
}

static void l4_key(bsp_btn_t btn, bsp_btn_ev_t ev)
{
    if (ev != BSP_BTN_CLICK || btn != BSP_BTN_OK) return;
    finish(3, s_l4_ok ? QS_PASS : QS_FAIL, "%d%% %dmV",
           bsp_battery_soc(), bsp_battery_mv());
}

// ------------------------------------------------------------------ L5 按键

#define L5_STEP_COUNT 4

typedef struct {
    bsp_btn_t btn;
    bsp_btn_ev_t ev;
    const char *prompt;
} l5_step_t;

static const l5_step_t L5_STEPS[L5_STEP_COUNT] = {
    { BSP_BTN_UP, BSP_BTN_CLICK, PQ_TXT_CLICK_UP },
    { BSP_BTN_DOWN, BSP_BTN_CLICK, PQ_TXT_CLICK_DOWN },
    { BSP_BTN_OK, BSP_BTN_CLICK, PQ_TXT_CLICK_OK },
    { BSP_BTN_OK, BSP_BTN_LONG, PQ_TXT_LONG_OK },
};

static const int BTN_WINDOWS[BSP_BTN_COUNT][2] = BSP_BTN_MV_TABLE;

static lv_obj_t *s_l5_prompt;
static lv_obj_t *s_l5_mv;
static int s_l5_step;
static int s_l5_press_mv = -1;

static void l5_show(void)
{
    if (s_l5_prompt && s_l5_step < L5_STEP_COUNT) {
        lv_label_set_text(s_l5_prompt, L5_STEPS[s_l5_step].prompt);
    }
    if (s_l5_mv) {
        const int mv = s_l5_press_mv >= 0 ? s_l5_press_mv : bsp_button_read_mv();
        char text[32];
        snprintf(text, sizeof text, "%s %d mV", PQ_TXT_VOLTAGE, mv);
        lv_label_set_text(s_l5_mv, text);
    }
}

static void l5_enter(void)
{
    s_l5_step = 0;
    s_page = level_page(PQ_TXT_LEVEL_BUTTON, &s_l5_prompt);
    s_l5_mv = pq_ui_text(s_page, "", 14, 120, &pq_font_16, PQ_COLOR_MUTED);
    l5_show();
}

static void l5_key(bsp_btn_t btn, bsp_btn_ev_t ev)
{
    // 单击是“按下再抬起”，事件到达时键已经松开、ADC 回到松开电压。
    // 所以电压必须在 PRESS 事件里采样，单击/长按只负责比对事件类型。
    if (ev == BSP_BTN_PRESS) {
        s_l5_press_mv = bsp_button_read_mv();
        l5_show();
        return;
    }
    if (ev == BSP_BTN_RELEASE) {
        l5_show();
        return;
    }
    // 长按上键 = 放弃本关；长按确定在本关是被测输入，不能拿来返回。
    if (btn == BSP_BTN_UP && ev == BSP_BTN_LONG) {
        leave_level();
        return;
    }
    if (s_l5_step >= L5_STEP_COUNT) return;
    if (ev != BSP_BTN_CLICK && ev != BSP_BTN_LONG) return;

    l5_show();
    const l5_step_t *expected = &L5_STEPS[s_l5_step];
    if (btn != expected->btn || ev != expected->ev) {
        finish(L5_INDEX, QS_FAIL, "%s", PQ_TXT_FAIL);
        return;
    }
    const int mv = s_l5_press_mv;
    if (mv < 0 || mv < BTN_WINDOWS[btn][0] || mv > BTN_WINDOWS[btn][1]) {
        finish(L5_INDEX, QS_FAIL, "%d mV", mv);
        return;
    }
    s_l5_step++;
    if (s_l5_step >= L5_STEP_COUNT) {
        finish(L5_INDEX, QS_PASS, "%s", PQ_TXT_MANUAL);
        return;
    }
    l5_show();
}

// ------------------------------------------------------------------ L12 深睡

static const uint32_t L12_DURATIONS_S[3] = { 10, 15, 30 };
static lv_obj_t *s_l12_duration;
static lv_obj_t *s_l12_result;
static lv_obj_t *s_l12_detail;
// 档位也放 RTC 内存：深睡会复位芯片，若存在普通 RAM 里，醒来后必然掉回默认档。
// 0 = 10 秒，正好也是首次进入的默认值，所以不需要额外的“未初始化”哨兵。
static RTC_DATA_ATTR uint8_t s_l12_index_rtc;
static int s_l12_index;

static void l12_load_index(void)
{
    s_l12_index = s_l12_index_rtc < 3 ? (int)s_l12_index_rtc : 0;
}

static void l12_refresh_duration(void)
{
    if (!s_l12_duration) return;
    char text[32];
    snprintf(text, sizeof text, "%s %lu %s", PQ_TXT_DURATION,
             (unsigned long)L12_DURATIONS_S[s_l12_index], PQ_TXT_SECONDS);
    lv_label_set_text(s_l12_duration, text);
}

static const char *wake_name(pq_wake_t wake)
{
    switch (wake) {
        case PQ_WAKE_TIMER: return PQ_TXT_TIMER;
        case PQ_WAKE_KEY: return PQ_TXT_KEY;
        default: return "--";
    }
}

static void l12_refresh_result(void)
{
    if (!s_l12_result || !s_l12_detail) return;
    const pq_power_info_t *info = pq_power_info();

    if (!info->restored) {
        lv_label_set_text(s_l12_result, PQ_TXT_NO_PREVIOUS);
        lv_label_set_text(s_l12_detail, "");
        return;
    }
    char text[96];
    if (info->last.valid) {
        snprintf(text, sizeof text, "%s %.2fs / %.2fs", PQ_TXT_SLEPT,
                 (double)info->last.measured_us / 1000000.0,
                 (double)info->last.requested_us / 1000000.0);
        lv_label_set_text(s_l12_result, text);
        char percent[16];
        pq_ui_format_ppm(percent, sizeof percent, info->last.error_ppm);
        snprintf(text, sizeof text, "%s %s   %s   %lu", PQ_TXT_ERROR, percent,
                 wake_name(info->wake), (unsigned long)info->count);
        lv_label_set_text(s_l12_detail, text);
    } else {
        lv_label_set_text(s_l12_result, PQ_TXT_UNMEASURED);
        snprintf(text, sizeof text, "%s   %lu", wake_name(info->wake),
                 (unsigned long)info->count);
        lv_label_set_text(s_l12_detail, text);
    }
}

// L12 的判据是客观的：既然刚从这个关卡睡醒，就自动把这一轮记进报告，
// 不需要用户再按一次 OK（OK 在本关的语义是“再睡一次”）。
static void l12_record_if_fresh(void)
{
    const pq_power_info_t *info = pq_power_info();
    if (!info->restored || info->mode != 1) return;
    if (info->wake == PQ_WAKE_UNKNOWN) return;
    if (info->count <= s_l12_recorded_count) return;

    s_l12_recorded_count = info->count;
    if (info->last.valid) {
        char percent[16];
        pq_ui_format_ppm(percent, sizeof percent, info->last.error_ppm);
        s_status[L12_INDEX] = QS_PASS;
        snprintf(s_detail[L12_INDEX], DETAIL_MAX, "%.1fs %s",
                 (double)info->last.measured_us / 1000000.0, percent);
    } else {
        s_status[L12_INDEX] = QS_FAIL;
        snprintf(s_detail[L12_INDEX], DETAIL_MAX, "%s", PQ_TXT_UNMEASURED);
    }
    ESP_LOGI(TAG, "%s: %s %s（自动记录）", LEVELS[L12_INDEX].title,
             status_text(s_status[L12_INDEX]), s_detail[L12_INDEX]);
    report_log();
    results_save();
}

static void l12_enter(void)
{
    l12_load_index();
    l12_record_if_fresh();
    s_page = pq_ui_page_create(PQ_TXT_DEEP_SLEEP);
    pq_ui_text(s_page, "L12", 14, 40, &pq_font_16, PQ_COLOR_MUTED);

    lv_obj_t *panel = lv_obj_create(s_page);
    lv_obj_set_size(panel, 216, 62);
    lv_obj_set_pos(panel, 12, 68);
    lv_obj_set_style_bg_color(panel, lv_color_hex(PQ_COLOR_PANEL), 0);
    lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(panel, 10, 0);
    lv_obj_set_style_border_width(panel, 0, 0);
    lv_obj_set_style_pad_all(panel, 6, 0);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);

    s_l12_duration = pq_ui_text(panel, "", 4, 0, &pq_font_20, PQ_COLOR_ACCENT);
    lv_obj_set_height(s_l12_duration, 30);
    l12_refresh_duration();

    s_l12_result = pq_ui_text(s_page, "", 14, 144, &pq_font_16, PQ_COLOR_INK);
    s_l12_detail = pq_ui_text(s_page, "", 14, 170, &pq_font_16, PQ_COLOR_MUTED);
    l12_refresh_result();

    lv_obj_t *hint = pq_ui_text(s_page, PQ_TXT_QUEST_HINT, 0, 286, &pq_font_16,
                                PQ_COLOR_MUTED);
    lv_obj_set_width(hint, LV_PCT(100));
    lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_CENTER, 0);
}

static void l12_key(bsp_btn_t btn, bsp_btn_ev_t ev)
{
    if (ev != BSP_BTN_CLICK) return;
    if (btn == BSP_BTN_UP) {
        s_l12_index = (s_l12_index + 2) % 3;
        s_l12_index_rtc = (uint8_t)s_l12_index;
        l12_refresh_duration();
    } else if (btn == BSP_BTN_DOWN) {
        s_l12_index = (s_l12_index + 1) % 3;
        s_l12_index_rtc = (uint8_t)s_l12_index;
        l12_refresh_duration();
    } else if (btn == BSP_BTN_OK) {
        if (pq_power_key_held()) {
            lv_label_set_text(s_l12_result, PQ_TXT_RELEASE_KEY);
            return;
        }
        const uint64_t requested_us = (uint64_t)L12_DURATIONS_S[s_l12_index] * 1000000ULL;
        lv_label_set_text(s_l12_result, PQ_TXT_SLEEPING);
        lv_label_set_text(s_l12_detail, "");
        vTaskDelay(pdMS_TO_TICKS(400));
        esp_err_t err = pq_power_sleep(requested_us, 1, 0);
        ESP_LOGE(TAG, "入睡失败: %s", esp_err_to_name(err));
        l12_refresh_result();
    }
}

// ------------------------------------------------------------------ 按键分发

void quest_handle_key(bsp_btn_t btn, bsp_btn_ev_t ev)
{
    if (!s_active) return;

    if (s_current >= 0) {
        // 关卡内：长按确定统一返回列表（L5 自己接管长按语义）。
        if (s_current != L5_INDEX && ev == BSP_BTN_LONG && btn == BSP_BTN_OK) {
            if (!bsp_lvgl_lock(300)) return;
            leave_level();
            bsp_lvgl_unlock();
            return;
        }
        if (!bsp_lvgl_lock(300)) return;
        LEVELS[s_current].key(btn, ev);
        bsp_lvgl_unlock();
        return;
    }

    if (ev == BSP_BTN_LONG && btn == BSP_BTN_OK) {
        if (!bsp_lvgl_lock(300)) return;
        quest_close();
        app_home_open();
        bsp_lvgl_unlock();
        return;
    }
    if (ev == BSP_BTN_LONG && btn == BSP_BTN_UP) {
        if (!bsp_lvgl_lock(300)) return;
        results_clear();
        quest_open();
        bsp_lvgl_unlock();
        return;
    }
    if (ev != BSP_BTN_CLICK) return;
    if (!bsp_lvgl_lock(300)) return;
    if (btn == BSP_BTN_UP) {
        s_selected = (s_selected + LEVEL_COUNT - 1) % LEVEL_COUNT;
        quest_open();
    } else if (btn == BSP_BTN_DOWN) {
        s_selected = (s_selected + 1) % LEVEL_COUNT;
        quest_open();
    } else if (btn == BSP_BTN_OK) {
        enter_level(s_selected);
    }
    bsp_lvgl_unlock();
}

// ------------------------------------------------------------------ 结果访问器

int quest_results_count(void) { return LEVEL_COUNT; }

const char *quest_results_title(int index)
{
    if (index < 0 || index >= LEVEL_COUNT) return "";
    return LEVELS[index].title;
}

int quest_results_status(int index)
{
    if (index < 0 || index >= LEVEL_COUNT) return 0;
    return (int)s_status[index];
}

const char *quest_results_detail(int index)
{
    if (index < 0 || index >= LEVEL_COUNT) return "";
    return s_detail[index];
}

const char *quest_results_status_text(int index)
{
    if (index < 0 || index >= LEVEL_COUNT) return PQ_TXT_PENDING;
    return status_text(s_status[index]);
}

void quest_results_clear(void)
{
    results_clear();
}
