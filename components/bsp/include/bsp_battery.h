// components/bsp/include/bsp_battery.h
// CellWise CW2017 电量计:I2C 0x63,与 ES8311 共用总线。
// BSP 写入自定义电池 profile,芯片直接给 SOC%,无需外部分压电阻与查表。
#pragma once

#include "esp_err.h"

// 初始化。内部会调 bsp_i2c_init()(幂等)。
// 芯片不应答时返回 ESP_ERR_NOT_FOUND —— 上层可据此在 UI 上标记该项不可用。
esp_err_t bsp_battery_init(void);

// 将 CW2017 CONFIG 写为睡眠值并回读确认，失败时重试一次。这是 deep
// sleep 前的终端操作；芯片未初始化时视为无需处理并返回成功。
esp_err_t bsp_battery_sleep(void);

// 确保电量读数可信；读数本来就正常时立即返回，因此可以在初始化后无条件调用。
//
// 为什么需要单独一步：芯片在 bsp_battery_sleep() 之后会一直睡着，MCU 复位
// 并不会叫醒它；而 bsp_battery_init() 的就绪判据是“SOC ≤ 100”，睡眠态的
// 0% / 0 mV 恰好满足，于是初始化看起来成功、读数却全是 0。
// 实测还遇到第二种坏状态：芯片醒了、电压也对（4.16 V），但 SOC 仍是 0%
// （profile 不再参与计算）。因此本函数按“电压区间对应的 SOC 下限”判断自洽，
// 自洽检查失败时先 RESTART/ACTIVE 唤醒，仍不自洽就按首次上电流程重写 profile。
// 芯片未初始化时会先调用 bsp_battery_init()。
esp_err_t bsp_battery_recover(void);

// 剩余电量百分比 0..100;读失败返回 -1。
int bsp_battery_soc(void);

// 电池电压 mV;读失败返回 -1。
int bsp_battery_mv(void);
