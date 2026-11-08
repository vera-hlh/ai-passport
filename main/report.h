// main/report.h —— 体检报告页：读取各关卡的判定并展示汇总。
#pragma once

#include "bsp_button.h"

#include <stdbool.h>

// 建立并载入报告页。调用方必须持 LVGL 锁。
void report_open(void);

bool report_is_active(void);

void report_handle_key(bsp_btn_t btn, bsp_btn_ev_t ev);
