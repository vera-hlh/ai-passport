// main/app_home.h —— 应用主屏：自检、番茄钟、报告、字体探针与旧演示的入口。
#pragma once

#include "bsp_button.h"

#include <stdbool.h>

// 建立并载入主屏。调用方必须持 LVGL 锁。
void app_home_open(void);

// 主屏是否处于活动状态（按键路由用）。
bool app_home_is_active(void);

// 主屏按键处理，由 main.c 的输入任务调用（内部自行取/放 LVGL 锁）。
void app_home_handle_key(bsp_btn_t btn, bsp_btn_ev_t ev);
