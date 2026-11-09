// main/focus.h —— 番茄钟（M4）：复用 power 的深睡路径。
#pragma once

#include "bsp_button.h"

#include <stdbool.h>

// 建立并载入番茄钟页面；若刚从这个功能的深睡里醒来，会自动恢复剩余时间。
// 调用方必须持 LVGL 锁。
void focus_open(void);

bool focus_is_active(void);

void focus_handle_key(bsp_btn_t btn, bsp_btn_ev_t ev);
