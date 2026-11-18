// main/app_home.c —— 应用主屏（模板）：标题 + 状态区 + 一处占位入口。
//
// 新应用从这里开始改：换标题、加自己的入口、把按键交给自己的页面。
// 配色、页面、文字、选中行都在 pq_ui.h 里，不要另造一套。
#include "app_home.h"

#include "bsp_battery.h"
#include "bsp_display.h"
#include "pq_fonts.h"
#include "pq_ui.h"
#include "power.h"

#include "esp_log.h"
#include "lvgl.h"

#include <stdint.h>
#include <stdio.h>

static const char *TAG = "app_home";

#define ROW_Y0 126
#define ROW_STEP 32
#define ROW_H 32
#define LINE_H 24

static lv_obj_t *s_page;
static lv_obj_t *s_battery;
static lv_obj_t *s_sleep;
static bool s_active;

static void refresh_status(void)
{
    if (!s_battery || !s_sleep) return;
    char text[48];
    snprintf(text, sizeof text, "电量 %d%%  %d mV", bsp_battery_soc(), bsp_battery_mv());
    lv_label_set_text(s_battery, text);

    const pq_power_info_t *info = pq_power_info();
    snprintf(text, sizeof text, "唤醒次数 %lu", (unsigned long)info->count);
    lv_label_set_text(s_sleep, text);
}

void app_home_open(void)
{
    s_active = true;
    s_page = pq_ui_page_create("我的应用");

    lv_obj_t *panel = lv_obj_create(s_page);
    lv_obj_set_size(panel, 216, 56);
    lv_obj_set_pos(panel, 12, 44);
    lv_obj_set_style_bg_color(panel, lv_color_hex(PQ_COLOR_PANEL), 0);
    lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(panel, 10, 0);
    lv_obj_set_style_border_width(panel, 0, 0);
    lv_obj_set_style_pad_all(panel, 4, 0);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);

    s_battery = pq_ui_text(panel, "", 4, 0, &pq_font_16, PQ_COLOR_INK);
    lv_obj_set_height(s_battery, LINE_H);
    s_sleep = pq_ui_text(panel, "", 4, LINE_H, &pq_font_16, PQ_COLOR_ACCENT);
    lv_obj_set_height(s_sleep, LINE_H);
    refresh_status();

    lv_obj_t *row = pq_ui_row(s_page, 12, ROW_Y0, 216, ROW_H, true);
    lv_obj_t *label = lv_label_create(row);
    lv_obj_set_style_text_font(label, &pq_font_16, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(PQ_COLOR_INK), 0);
    lv_label_set_text(label, "把这里换成你的第一个页面");
    lv_obj_set_height(label, LINE_H);
    lv_obj_align(label, LV_ALIGN_LEFT_MID, 10, 2);

    lv_obj_t *hint = pq_ui_text(s_page, "长按确定 返回", 0, 288, &pq_font_16,
                                PQ_COLOR_MUTED);
    lv_obj_set_width(hint, LV_PCT(100));
    lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_CENTER, 0);

    ESP_LOGI(TAG, "主屏已建立");
}

bool app_home_is_active(void)
{
    return s_active;
}

void app_home_handle_key(bsp_btn_t btn, bsp_btn_ev_t ev)
{
    // 模板还没有二级页面：按键先留空，接线时再按页面分发。
    (void)btn;
    (void)ev;
}
