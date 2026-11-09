// main/focus.c —— 番茄钟：专注 / 休息都用深睡实现，时长可自设。
//
// 为什么用深睡：这块板子上“专注 25 分钟”最省电的形态就是让芯片睡着，屏幕自然熄。
// 唤醒源与自检关卡共用 power 模块，定时精度、按键中止、电量计恢复这些路径已经在
// M1/M2 真机验证过，这里只做业务状态机。
//
// 暂停语义：深睡不能暂停 RTC，所以“暂停”= 醒来后把剩余时长留在 RTC 里不再武装；
// 按“继续”时用剩余时长重新入睡。任何按键都会叫醒设备，正好用来暂停/中止。
//
// 时长设置：空闲页用上下键调“专注 / 休息”的分钟数（长按上键切换正在调的那一项），
// 设置存 RTC（跨深睡）并同步到 NVS（跨掉电）。
#include "focus.h"

#include "app_home.h"
#include "bsp_display.h"
#include "pq_fonts.h"
#include "pq_ui.h"
#include "power.h"
#include "ui_strings.h"

#include "esp_attr.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"
#include "nvs.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static const char *TAG = "pq_focus";

#define FOCUS_MAGIC 0x464f4331u   // 'FOC1'
#define FOCUS_NVS_NAMESPACE "pq_focus"
#define FOCUS_NVS_KEY "cfg"

#define POWER_MODE_FOCUS 2
#define POWER_MODE_BREAK 3

#define FOCUS_MIN_DEFAULT 25
#define BREAK_MIN_DEFAULT 5
#define FOCUS_MIN_LIMIT 120
#define BREAK_MIN_LIMIT 60

typedef enum {
    FOCUS_IDLE = 0,
    FOCUS_ACTIVE_FOCUS,
    FOCUS_PAUSED_FOCUS,
    FOCUS_DONE_FOCUS,
    FOCUS_ACTIVE_BREAK,
    FOCUS_PAUSED_BREAK,
    FOCUS_DONE_BREAK,
} focus_stage_t;

typedef struct {
    uint32_t magic;
    uint8_t stage;
    uint8_t editing_break;   // 空闲时正在调整哪一项：0 专注 / 1 休息
    uint16_t focus_min;
    uint16_t break_min;
    uint64_t remaining_us;
    uint32_t completed;
} focus_state_t;

typedef struct {
    uint32_t magic;
    uint32_t version;
    uint16_t focus_min;
    uint16_t break_min;
    uint32_t completed;
} focus_nvs_t;

#define FOCUS_NVS_MAGIC 0x464e5631u   // 'FNV1'

static RTC_DATA_ATTR focus_state_t s_state;
static lv_obj_t *s_page;
static lv_obj_t *s_stage_label;
static lv_obj_t *s_time_label;
static lv_obj_t *s_count_label;
static lv_obj_t *s_focus_setting;
static lv_obj_t *s_break_setting;
static lv_obj_t *s_hint_label;
static lv_obj_t *s_notice;
static bool s_active;

static void settings_save(void)
{
    nvs_handle_t handle;
    if (nvs_open(FOCUS_NVS_NAMESPACE, NVS_READWRITE, &handle) != ESP_OK) return;
    focus_nvs_t blob = {
        .magic = FOCUS_NVS_MAGIC,
        .version = 1,
        .focus_min = s_state.focus_min,
        .break_min = s_state.break_min,
        .completed = s_state.completed,
    };
    esp_err_t e = nvs_set_blob(handle, FOCUS_NVS_KEY, &blob, sizeof blob);
    if (e == ESP_OK) e = nvs_commit(handle);
    if (e != ESP_OK) ESP_LOGW(TAG, "番茄钟设置写入 NVS 失败: %s", esp_err_to_name(e));
    nvs_close(handle);
}

static void settings_load(void)
{
    nvs_handle_t handle;
    if (nvs_open(FOCUS_NVS_NAMESPACE, NVS_READONLY, &handle) != ESP_OK) return;
    focus_nvs_t blob;
    size_t len = sizeof blob;
    const bool ok = nvs_get_blob(handle, FOCUS_NVS_KEY, &blob, &len) == ESP_OK &&
                    len == sizeof blob && blob.magic == FOCUS_NVS_MAGIC;
    nvs_close(handle);
    if (!ok) return;
    s_state.focus_min = blob.focus_min ? blob.focus_min : FOCUS_MIN_DEFAULT;
    s_state.break_min = blob.break_min ? blob.break_min : BREAK_MIN_DEFAULT;
    s_state.completed = blob.completed;
    ESP_LOGI(TAG, "已从 NVS 恢复番茄钟设置: %u/%u 分钟, 已完成 %lu",
             (unsigned)s_state.focus_min, (unsigned)s_state.break_min,
             (unsigned long)s_state.completed);
}

static void ensure_state(void)
{
    if (s_state.magic == FOCUS_MAGIC) return;
    memset(&s_state, 0, sizeof s_state);
    s_state.magic = FOCUS_MAGIC;
    s_state.stage = FOCUS_IDLE;
    s_state.focus_min = FOCUS_MIN_DEFAULT;
    s_state.break_min = BREAK_MIN_DEFAULT;
    settings_load();
}

static uint64_t focus_length_us(void)
{
    return (uint64_t)s_state.focus_min * 60ULL * 1000000ULL;
}

static uint64_t break_length_us(void)
{
    return (uint64_t)s_state.break_min * 60ULL * 1000000ULL;
}

static void format_mmss(char *buf, size_t len, uint64_t remaining_us)
{
    const uint64_t seconds = (remaining_us + 999999ULL) / 1000000ULL;
    snprintf(buf, len, "%02lu:%02lu", (unsigned long)(seconds / 60),
             (unsigned long)(seconds % 60));
}

static bool stage_is_active(void)
{
    return s_state.stage == FOCUS_ACTIVE_FOCUS || s_state.stage == FOCUS_ACTIVE_BREAK;
}

static const char *stage_text(void)
{
    switch (s_state.stage) {
        case FOCUS_ACTIVE_FOCUS:
        case FOCUS_PAUSED_FOCUS: return PQ_TXT_FOCUS_STAGE;
        case FOCUS_DONE_FOCUS: return PQ_TXT_FOCUS_DONE;
        case FOCUS_ACTIVE_BREAK:
        case FOCUS_PAUSED_BREAK: return PQ_TXT_BREAK_STAGE;
        case FOCUS_DONE_BREAK: return PQ_TXT_BREAK_DONE;
        default: return PQ_TXT_START;
    }
}

static const char *hint_text(void)
{
    switch (s_state.stage) {
        case FOCUS_IDLE: return PQ_TXT_HINT_IDLE;
        case FOCUS_PAUSED_FOCUS:
        case FOCUS_PAUSED_BREAK: return PQ_TXT_HINT_PAUSE;
        case FOCUS_DONE_FOCUS: return PQ_TXT_HINT_BREAK;
        default: return PQ_TXT_HINT_DONE;
    }
}

static void refresh(void)
{
    if (!s_time_label) return;

    char text[64];
    if (s_state.stage == FOCUS_IDLE) {
        // 空闲页的大时间跟随“正在调整的那一项”，否则切到休息时数字不动，很费解。
        format_mmss(text, sizeof text,
                    s_state.editing_break ? break_length_us() : focus_length_us());
    } else if (s_state.stage == FOCUS_DONE_FOCUS ||
               s_state.stage == FOCUS_DONE_BREAK) {
        const uint64_t next = (s_state.stage == FOCUS_DONE_FOCUS)
                              ? break_length_us() : focus_length_us();
        format_mmss(text, sizeof text, next);
    } else {
        format_mmss(text, sizeof text, s_state.remaining_us);
    }
    lv_label_set_text(s_time_label, text);

    lv_label_set_text(s_stage_label, stage_text());
    lv_obj_set_style_text_color(s_stage_label, lv_color_hex(PQ_COLOR_ACCENT), 0);

    char count[48];
    snprintf(count, sizeof count, "%s %lu", PQ_TXT_COMPLETED,
             (unsigned long)s_state.completed);
    lv_label_set_text(s_count_label, count);

    // 空闲时显示两项设置，正在调整的那项用高亮色。
    if (s_state.stage == FOCUS_IDLE) {
        snprintf(text, sizeof text, "%s %u %s", PQ_TXT_FOCUS_STAGE,
                 (unsigned)s_state.focus_min, PQ_TXT_MINUTES);
        lv_label_set_text(s_focus_setting, text);
        snprintf(text, sizeof text, "%s %u %s", PQ_TXT_BREAK_STAGE,
                 (unsigned)s_state.break_min, PQ_TXT_MINUTES);
        lv_label_set_text(s_break_setting, text);
        lv_obj_set_style_text_color(s_focus_setting,
            lv_color_hex(s_state.editing_break ? PQ_COLOR_MUTED : PQ_COLOR_INK), 0);
        lv_obj_set_style_text_color(s_break_setting,
            lv_color_hex(s_state.editing_break ? PQ_COLOR_INK : PQ_COLOR_MUTED), 0);
    } else if (s_focus_setting && s_break_setting) {
        lv_label_set_text(s_focus_setting, "");
        lv_label_set_text(s_break_setting, "");
    }

    if (s_hint_label) lv_label_set_text(s_hint_label, hint_text());
}

static void set_notice(const char *text)
{
    if (s_notice) lv_label_set_text(s_notice, text ? text : "");
}

// 入睡。调用时不持 LVGL 锁；成功后不返回，失败时把错误显示在页面上。
static void start_sleep(int mode)
{
    if (bsp_lvgl_lock(300)) {
        if (s_time_label) lv_label_set_text(s_time_label, PQ_TXT_SLEEPING);
        if (s_notice) lv_label_set_text(s_notice, "");
        s_state.stage = (mode == POWER_MODE_FOCUS) ? FOCUS_ACTIVE_FOCUS
                                                   : FOCUS_ACTIVE_BREAK;
        bsp_lvgl_unlock();
    }

    vTaskDelay(pdMS_TO_TICKS(350));

    esp_err_t err = pq_power_sleep(s_state.remaining_us, (uint32_t)mode, 0);
    ESP_LOGE(TAG, "入睡失败: %s", esp_err_to_name(err));
    if (bsp_lvgl_lock(300)) {
        s_state.stage = (mode == POWER_MODE_FOCUS) ? FOCUS_PAUSED_FOCUS
                                                   : FOCUS_PAUSED_BREAK;
        set_notice(PQ_TXT_SLEEP_FAILED);
        refresh();
        bsp_lvgl_unlock();
    }
}

static void restore_from_wake(void)
{
    if (!stage_is_active()) return;

    const pq_power_info_t *info = pq_power_info();
    const bool is_focus = s_state.stage == FOCUS_ACTIVE_FOCUS;
    if (!info->restored ||
        (info->mode != POWER_MODE_FOCUS && info->mode != POWER_MODE_BREAK) ||
        info->wake == PQ_WAKE_UNKNOWN) {
        s_state.stage = is_focus ? FOCUS_PAUSED_FOCUS : FOCUS_PAUSED_BREAK;
        return;
    }

    const uint64_t armed = info->last.requested_us;
    const uint64_t slept = info->last.valid ? info->last.measured_us : 0;
    uint64_t remaining = armed > slept ? armed - slept : 0;

    if (remaining <= 1000000ULL) {
        remaining = 0;
        s_state.stage = is_focus ? FOCUS_DONE_FOCUS : FOCUS_DONE_BREAK;
        if (!is_focus) {
            // 一轮番茄 = 专注 + 休息，休息睡满就记账，不等用户按确定。
            s_state.completed++;
            settings_save();
        }
    } else {
        s_state.stage = is_focus ? FOCUS_PAUSED_FOCUS : FOCUS_PAUSED_BREAK;
    }
    s_state.remaining_us = remaining;
    ESP_LOGI(TAG, "唤醒恢复: mode=%lu 武装 %llu us 实测 %llu us 剩余 %llu us",
             (unsigned long)info->mode, (unsigned long long)armed,
             (unsigned long long)slept, (unsigned long long)remaining);
}

void focus_open(void)
{
    ensure_state();
    restore_from_wake();

    s_active = true;
    s_page = pq_ui_page_create(PQ_TXT_FOCUS);

    s_stage_label = pq_ui_text(s_page, "", 14, 56, &pq_font_16, PQ_COLOR_ACCENT);
    s_time_label = pq_ui_text(s_page, "", 14, 86, &pq_font_20, PQ_COLOR_INK);
    s_count_label = pq_ui_text(s_page, "", 14, 126, &pq_font_16, PQ_COLOR_MUTED);

    s_focus_setting = pq_ui_text(s_page, "", 14, 164, &pq_font_16, PQ_COLOR_INK);
    s_break_setting = pq_ui_text(s_page, "", 14, 192, &pq_font_16, PQ_COLOR_MUTED);
    s_notice = pq_ui_text(s_page, "", 14, 230, &pq_font_16, PQ_COLOR_WARN);

    s_hint_label = pq_ui_text(s_page, "", 12, 262, &pq_font_16, PQ_COLOR_MUTED);
    lv_obj_set_width(s_hint_label, 216);
    lv_obj_set_style_text_align(s_hint_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(s_hint_label, LV_LABEL_LONG_WRAP);

    refresh();
    ESP_LOGI(TAG, "番茄钟页已建立, stage=%d %u/%u 分钟", (int)s_state.stage,
             (unsigned)s_state.focus_min, (unsigned)s_state.break_min);
}

bool focus_is_active(void)
{
    return s_active;
}

static void close_to_home(void)
{
    s_active = false;
    pq_ui_page_delete(&s_page);
    s_stage_label = s_time_label = s_count_label = NULL;
    s_focus_setting = s_break_setting = s_hint_label = s_notice = NULL;
    app_home_open();
}

void focus_handle_key(bsp_btn_t btn, bsp_btn_ev_t ev)
{
    if (!s_active) return;
    if (!bsp_lvgl_lock(300)) return;

    // ---------- 空闲：上下调时长、长按上键切项、确定开始 ----------
    if (s_state.stage == FOCUS_IDLE) {
        if (ev == BSP_BTN_LONG && btn == BSP_BTN_UP) {
            s_state.editing_break = s_state.editing_break ? 0 : 1;
            set_notice("");
            refresh();
            bsp_lvgl_unlock();
            return;
        }
        if (ev == BSP_BTN_LONG && btn == BSP_BTN_OK) {
            settings_save();
            close_to_home();
            bsp_lvgl_unlock();
            return;
        }
        // 单击 ±1 分钟；双击 ±5 分钟，省得从 25 调到 1 要按 24 次。
        if ((ev == BSP_BTN_CLICK || ev == BSP_BTN_DOUBLE) &&
            (btn == BSP_BTN_UP || btn == BSP_BTN_DOWN)) {
            const int step = (ev == BSP_BTN_DOUBLE) ? 5 : 1;
            const int delta = (btn == BSP_BTN_UP) ? step : -step;
            if (s_state.editing_break) {
                int value = (int)s_state.break_min + delta;
                if (value < 1) value = 1;
                if (value > BREAK_MIN_LIMIT) value = BREAK_MIN_LIMIT;
                s_state.break_min = (uint16_t)value;
            } else {
                int value = (int)s_state.focus_min + delta;
                if (value < 1) value = 1;
                if (value > FOCUS_MIN_LIMIT) value = FOCUS_MIN_LIMIT;
                s_state.focus_min = (uint16_t)value;
            }
            set_notice("");
            refresh();
            bsp_lvgl_unlock();
            return;
        }
        if (ev == BSP_BTN_CLICK && btn == BSP_BTN_OK) {
            s_state.remaining_us = focus_length_us();
            settings_save();
            bsp_lvgl_unlock();
            start_sleep(POWER_MODE_FOCUS);
            return;
        }
        bsp_lvgl_unlock();
        return;
    }

    // ---------- 其它状态：长按确定返回/回空闲，长按上键放弃 ----------
    if (ev == BSP_BTN_LONG && btn == BSP_BTN_OK) {
        s_state.stage = FOCUS_IDLE;
        s_state.remaining_us = 0;
        set_notice("");
        refresh();
        bsp_lvgl_unlock();
        return;
    }
    if (ev == BSP_BTN_LONG && btn == BSP_BTN_UP) {
        s_state.stage = FOCUS_IDLE;
        s_state.remaining_us = 0;
        set_notice(PQ_TXT_GIVE_UP);
        refresh();
        bsp_lvgl_unlock();
        return;
    }
    if (ev != BSP_BTN_CLICK || btn != BSP_BTN_OK) {
        bsp_lvgl_unlock();
        return;
    }

    switch (s_state.stage) {
        case FOCUS_PAUSED_FOCUS:
            bsp_lvgl_unlock();
            start_sleep(POWER_MODE_FOCUS);
            return;
        case FOCUS_PAUSED_BREAK:
            bsp_lvgl_unlock();
            start_sleep(POWER_MODE_BREAK);
            return;
        case FOCUS_DONE_FOCUS:
            s_state.remaining_us = break_length_us();
            set_notice("");
            refresh();
            bsp_lvgl_unlock();
            start_sleep(POWER_MODE_BREAK);
            return;
        case FOCUS_DONE_BREAK:
            s_state.stage = FOCUS_IDLE;
            s_state.remaining_us = 0;
            refresh();
            break;
        default:
            break;
    }
    bsp_lvgl_unlock();
}
