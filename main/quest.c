// main/quest.c —— 自检关卡。M1 只有 L12：短档深睡 + 唤醒结果。
//
// L12 的判据全部是客观的：唤醒来源、实测时长与误差、RTC 快照是否完好、累计次数是否递增。
// 时长故意做成短档(10/15/30 秒)，这样回归测试不用真的等 25 分钟。
#include "quest.h"

#include "app_home.h"
#include "bsp_display.h"
#include "pq_fonts.h"
#include "pq_ui.h"
#include "power.h"
#include "ui_strings.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"

#include <stdint.h>
#include <stdio.h>

static const char *TAG = "pq_quest";

#define DURATION_STEPS 3
static const uint32_t DURATIONS_S[DURATION_STEPS] = { 10, 15, 30 };

static lv_obj_t *s_page;
static lv_obj_t *s_duration_label;
static lv_obj_t *s_result_label;
static lv_obj_t *s_detail_label;
static lv_obj_t *s_notice;
static int s_duration_index = 1;   // 默认 15 秒
static bool s_active;

static void set_notice(const char *text)
{
    if (s_notice) lv_label_set_text(s_notice, text ? text : "");
}

static void refresh_duration(void)
{
    if (!s_duration_label) return;
    char text[32];
    snprintf(text, sizeof text, "%s %lu %s", PQ_TXT_DURATION,
             (unsigned long)DURATIONS_S[s_duration_index], PQ_TXT_SECONDS);
    lv_label_set_text(s_duration_label, text);
}

static const char *wake_name(pq_wake_t wake)
{
    switch (wake) {
        case PQ_WAKE_TIMER: return PQ_TXT_TIMER;
        case PQ_WAKE_KEY: return PQ_TXT_KEY;
        default: return "--";
    }
}

// 把上一次睡眠的结果写进页面。这是 L12 的“判据展示”部分。
static void refresh_result(void)
{
    if (!s_result_label || !s_detail_label) return;
    const pq_power_info_t *info = pq_power_info();

    if (!info->restored) {
        lv_label_set_text(s_result_label, PQ_TXT_NO_PREVIOUS);
        lv_label_set_text(s_detail_label, "");
        return;
    }

    char text[96];
    if (info->last.valid) {
        snprintf(text, sizeof text, "%s %.2fs / %.2fs", PQ_TXT_SLEPT,
                 (double)info->last.measured_us / 1000000.0,
                 (double)info->last.requested_us / 1000000.0);
        lv_label_set_text(s_result_label, text);

        char percent[16];
        pq_ui_format_ppm(percent, sizeof percent, info->last.error_ppm);
        snprintf(text, sizeof text, "%s %s   %s   %lu", PQ_TXT_ERROR, percent,
                 wake_name(info->wake), (unsigned long)info->count);
        lv_label_set_text(s_detail_label, text);
    } else {
        lv_label_set_text(s_result_label, PQ_TXT_UNMEASURED);
        snprintf(text, sizeof text, "%s   %lu", wake_name(info->wake),
                 (unsigned long)info->count);
        lv_label_set_text(s_detail_label, text);
    }
}

void quest_open(void)
{
    s_active = true;
    s_page = pq_ui_page_create(PQ_TXT_DEEP_SLEEP);

    pq_ui_text(s_page, "L12", 14, 40, &pq_font_16, PQ_COLOR_MUTED);

    lv_obj_t *panel = lv_obj_create(s_page);
    lv_obj_set_size(panel, 216, 58);
    lv_obj_set_pos(panel, 12, 68);
    lv_obj_set_style_bg_color(panel, lv_color_hex(PQ_COLOR_PANEL), 0);
    lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(panel, 10, 0);
    lv_obj_set_style_border_width(panel, 0, 0);
    lv_obj_set_style_pad_all(panel, 4, 0);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);

    s_duration_label = pq_ui_text(panel, "", 4, 2, &pq_font_20, PQ_COLOR_ACCENT);
    refresh_duration();

    s_result_label = pq_ui_text(s_page, "", 14, 140, &pq_font_16, PQ_COLOR_INK);
    s_detail_label = pq_ui_text(s_page, "", 14, 166, &pq_font_16, PQ_COLOR_MUTED);
    refresh_result();

    s_notice = pq_ui_text(s_page, "", 14, 200, &pq_font_16, PQ_COLOR_WARN);

    lv_obj_t *hint = pq_ui_text(s_page, PQ_TXT_QUEST_HINT, 0, 288, &pq_font_16,
                                PQ_COLOR_MUTED);
    lv_obj_set_width(hint, LV_PCT(100));
    lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_CENTER, 0);

    ESP_LOGI(TAG, "自检页已建立");
}

bool quest_is_active(void)
{
    return s_active;
}

static void quest_exit(void)
{
    s_active = false;
    pq_ui_page_delete(&s_page);
    s_duration_label = s_result_label = s_detail_label = s_notice = NULL;
    app_home_open();
}

void quest_handle_key(bsp_btn_t btn, bsp_btn_ev_t ev)
{
    if (!s_active) return;

    if (ev == BSP_BTN_LONG && btn == BSP_BTN_OK) {
        if (!bsp_lvgl_lock(300)) return;
        quest_exit();
        bsp_lvgl_unlock();
        return;
    }
    if (ev != BSP_BTN_CLICK) return;
    if (!bsp_lvgl_lock(300)) return;

    if (btn == BSP_BTN_UP) {
        s_duration_index = (s_duration_index + DURATION_STEPS - 1) % DURATION_STEPS;
        refresh_duration();
        set_notice("");
    } else if (btn == BSP_BTN_DOWN) {
        s_duration_index = (s_duration_index + 1) % DURATION_STEPS;
        refresh_duration();
        set_notice("");
    } else if (btn == BSP_BTN_OK) {
        if (pq_power_key_held()) {
            set_notice(PQ_TXT_RELEASE_KEY);
            bsp_lvgl_unlock();
            return;
        }
        const uint64_t requested_us = (uint64_t)DURATIONS_S[s_duration_index] * 1000000ULL;
        if (s_result_label) lv_label_set_text(s_result_label, PQ_TXT_SLEEPING);
        if (s_detail_label) lv_label_set_text(s_detail_label, "");
        bsp_lvgl_unlock();

        // 先让这一帧画出来，再入睡；这段时间里按键仍由驱动持有。
        vTaskDelay(pdMS_TO_TICKS(400));

        esp_err_t err = pq_power_sleep(requested_us, 1, 0);
        // 只有失败才会回到这里：pq_power_sleep() 成功时不返回。
        ESP_LOGE(TAG, "入睡失败: %s", esp_err_to_name(err));
        if (bsp_lvgl_lock(300)) {
            refresh_result();
            set_notice(err == ESP_ERR_INVALID_STATE ? PQ_TXT_RELEASE_KEY
                                                    : PQ_TXT_SLEEP_FAILED);
            bsp_lvgl_unlock();
        }
        // 上面已经成对地解锁并自行重新加锁，不能再落到函数末尾的那次解锁。
        return;
    }
    bsp_lvgl_unlock();
}
