// main/font_probe.h —— 中文字体探针：确认内置 CJK 子集能画中文，并列出计划文案的缺字。
#pragma once

#include "bsp_button.h"

#include <stdbool.h>

// 建立并载入探针页。调用方必须持 LVGL 锁。
void font_probe_open(void);

bool font_probe_is_active(void);

void font_probe_handle_key(bsp_btn_t btn, bsp_btn_ev_t ev);
