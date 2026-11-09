// main/focus.c —— 番茄钟：专注 / 休息都用深睡实现。
//
// 为什么用深睡：这块板子上“专注 25 分钟”最省电的形态就是让芯片睡着，屏幕自然熄。
// 唤醒源与自检关卡共用 power 模块，定时精度、按键中止、电量计恢复这些路径已经在
// M1/M2 真机验证过，这里只做业务状态机。
//
// 暂停语义：深睡不能暂停 RTC，所以“暂停”= 醒来后把剩余时长留在 RTC 里不再武装；
// 按“继续”时用剩余时长重新入睡。任何按键都会叫醒设备，正好用来暂停/中止。
//
// 长按上键：空闲时在 25/5 分钟与 1/1 分钟短档之间切换（短档只用于调试）。
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

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static const char *TAG = "pq_focus";

#define FOCUS_MAGIC 0x464f4331u   // 'FOC1'

#define FOCUS_DEFAULT_US (25ULL * 60ULL * 1000ULL * 1000ULL)
#define BREAK_DEFAULT_US (5ULL * 60ULL * 1000ULL * 1000ULL)

#define POWER_MODE_FOCUS 2
#define POWER_MODE_BREAK 3

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
    uint8_t short_mode;
    uint64_t remaining_us;
    uint32_t completed;
} focus_state_t;

static RTC_DATA_ATTR focus_state_t s_state;
static lv_obj_t *s_page;
static lv_obj_t *s_stage_label;
static lv_obj_t *s_time_label;
static lv_obj_t *s_count_label;
static lv_obj_t *s_hint_label;
static lv_obj_t *s_notice;
static bool s_active;

static void ensure_state(void)
{
    if (s_state.magic == FOCUS_MAGIC) return;
    memset(&s_state, 0, sizeof s_state);
    s_state.magic = FOCUS_MAGIC;
    s_state.stage = FOCUS_IDLE;
}

static uint64_t focus_length_us(void)
{
    return s_state.short_mode ? 60ULL * 1000ULL * 1000ULL : FOCUS_DEFAULT_US;
}

static uint64_t break_length_us(void)
{
    return s_state.short_mode ? 60ULL * 1000ULL * 1000ULL : BREAK_DEFAULT_US;
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
        case FOCUS_IDLE: return PQ_TXT_HINT_START;
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
    if (s_state.stage == FOCUS_IDLE || s_state.stage == FOCUS_DONE_FOCUS ||
        s_state.stage == FOCUS_DONE_BREAK) {
        // 空闲或已完成档：显示“下一步”会睡多久，避免屏幕上停在 00:00。
        const uint64_t next = (s_state.stage == FOCUS_DONE_FOCUS)
                              ? break_length_us() : focus_length_us();
        format_mmss(text, sizeof text, next);
    } else {
        format_mmss(text, sizeof text, s_state.remaining_us);
    }
    lv_label_set_text(s_time_label, text);

    lv_label_set_text(s_stage_label, stage_text());
    lv_obj_set_style_text_color(s_stage_label,
        lv_color_hex(PQ_COLOR_ACCENT), 0);

    char count[48];
    snprintf(count, sizeof count, "%s %lu", PQ_TXT_COMPLETED,
             (unsigned long)s_state.completed);
    lv_label_set_text(s_count_label, count);

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

    // 让“正在入睡”这一帧先画出来，再熄屏入睡。
    vTaskDelay(pdMS_TO_TICKS(350));

    esp_err_t err = pq_power_sleep(s_state.remaining_us, (uint32_t)mode, 0);
    // 只有失败才会回到这里。
    ESP_LOGE(TAG, "入睡失败: %s", esp_err_to_name(err));
    if (bsp_lvgl_lock(300)) {
        s_state.stage = (mode == POWER_MODE_FOCUS) ? FOCUS_PAUSED_FOCUS
                                                   : FOCUS_PAUSED_BREAK;
        set_notice(PQ_TXT_SLEEP_FAILED);
        refresh();
        bsp_lvgl_unlock();
    }
}

// 用 power 记下的“武装时长 − 实测睡眠”恢复剩余时间。
// 只在状态是 ACTIVE 时调用：那是“我们确实从这一页睡过去”的唯一证据，
// 否则从主屏再进本页会拿上一次的唤醒信息重复恢复。
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

    s_stage_label = pq_ui_text(s_page, "", 14, 62, &pq_font_16, PQ_COLOR_ACCENT);
    s_time_label = pq_ui_text(s_page, "", 14, 92, &pq_font_20, PQ_COLOR_INK);
    s_count_label = pq_ui_text(s_page, "", 14, 132, &pq_font_16, PQ_COLOR_MUTED);
    s_notice = pq_ui_text(s_page, "", 14, 200, &pq_font_16, PQ_COLOR_WARN);

    s_hint_label = pq_ui_text(s_page, "", 0, 286, &pq_font_16, PQ_COLOR_MUTED);
    lv_obj_set_width(s_hint_label, LV_PCT(100));
    lv_obj_set_style_text_align(s_hint_label, LV_TEXT_ALIGN_CENTER, 0);

    refresh();
    ESP_LOGI(TAG, "番茄钟页已建立, stage=%d", (int)s_state.stage);
}

bool focus_is_active(void)
{
    return s_active;
}

static void close_to_home(void)
{
    s_active = false;
    pq_ui_page_delete(&s_page);
    s_stage_label = s_time_label = s_count_label = s_hint_label = s_notice = NULL;
    app_home_open();
}

void focus_handle_key(bsp_btn_t btn, bsp_btn_ev_t ev)
{
    if (!s_active) return;

    // 长按确定：空闲时回主屏；其余状态放弃本轮回空闲。
    if (ev == BSP_BTN_LONG && btn == BSP_BTN_OK) {
        if (!bsp_lvgl_lock(300)) return;
        if (s_state.stage == FOCUS_IDLE) {
            close_to_home();
            bsp_lvgl_unlock();
            return;
        }
        s_state.stage = FOCUS_IDLE;
        s_state.remaining_us = 0;
        set_notice("");
        refresh();
        bsp_lvgl_unlock();
        return;
    }

    // 长按上键：空闲时切换长短档；运行/暂停/完成状态放弃本轮。
    if (ev == BSP_BTN_LONG && btn == BSP_BTN_UP) {
        if (!bsp_lvgl_lock(300)) return;
        if (s_state.stage == FOCUS_IDLE) {
            s_state.short_mode = s_state.short_mode ? 0 : 1;
            set_notice(s_state.short_mode ? PQ_TXT_SHORT_MODE : PQ_TXT_LONG_MODE);
        } else {
            s_state.stage = FOCUS_IDLE;
            s_state.remaining_us = 0;
            set_notice(PQ_TXT_GIVE_UP);
        }
        refresh();
        bsp_lvgl_unlock();
        return;
    }

    if (ev != BSP_BTN_CLICK || btn != BSP_BTN_OK) return;

    // 需要入睡的分支：先确认当前状态，再取一次锁把状态写进 RTC，然后交给 start_sleep。
    focus_stage_t stage = s_state.stage;
    if (stage == FOCUS_PAUSED_FOCUS || stage == FOCUS_PAUSED_BREAK) {
        start_sleep(stage == FOCUS_PAUSED_FOCUS ? POWER_MODE_FOCUS : POWER_MODE_BREAK);
        return;
    }
    if (stage == FOCUS_DONE_FOCUS) {
        if (!bsp_lvgl_lock(300)) return;
        s_state.remaining_us = break_length_us();
        bsp_lvgl_unlock();
        start_sleep(POWER_MODE_BREAK);
        return;
    }
    if (stage == FOCUS_IDLE) {
        if (!bsp_lvgl_lock(300)) return;
        s_state.remaining_us = focus_length_us();
        bsp_lvgl_unlock();
        start_sleep(POWER_MODE_FOCUS);
        return;
    }
    if (stage == FOCUS_DONE_BREAK) {
        if (!bsp_lvgl_lock(300)) return;
        s_state.completed++;
        s_state.stage = FOCUS_IDLE;
        s_state.remaining_us = 0;
        refresh();
        bsp_lvgl_unlock();
    }
}
