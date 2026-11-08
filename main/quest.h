// main/quest.h —— 硬件自检的关卡宿主。M1 只实现 L12(深睡/唤醒)。
#pragma once

#include "bsp_button.h"

#include <stdbool.h>

// 建立并载入自检页。调用方必须持 LVGL 锁。
void quest_open(void);

// 直接进入 L12 深睡关卡（从深睡唤醒后回到这里看结果）。
void quest_open_last_sleep(void);

// 离开自检页并释放页面（不切到主屏）。
void quest_close(void);

// 自检页是否处于活动状态（按键路由用）。
bool quest_is_active(void);

// 自检页按键处理，由 main.c 的输入任务调用（内部自行取/放 LVGL 锁）。
void quest_handle_key(bsp_btn_t btn, bsp_btn_ev_t ev);
