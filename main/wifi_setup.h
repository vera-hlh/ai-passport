// main/wifi_setup.h —— Wi-Fi 配网页：设备开热点，手机浏览器填 SSID/密码。
#pragma once

#include "bsp_button.h"

#include <stdbool.h>

// 建立并载入配网页（会启动 SoftAP 与 HTTP 服务）。调用方必须持 LVGL 锁。
void wifi_setup_open(void);

bool wifi_setup_is_active(void);

void wifi_setup_handle_key(bsp_btn_t btn, bsp_btn_ev_t ev);
