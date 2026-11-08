// main/power.h —— 深睡模块的 ESP-IDF 侧：武装唤醒源、按固定顺序停外设、读写 RTC 状态。
//
// 业务层（自检关卡、番茄钟）只调用这里的两个入口：
//   pq_power_init()  开机早期调用一次，取回上次睡眠的结果
//   pq_power_sleep() 睡一次，成功后不返回（醒来就是复位）
// 纯算术在 power_state.h，可在宿主上测试。
#pragma once

#include "esp_err.h"
#include "power_state.h"

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    PQ_WAKE_UNKNOWN = 0,
    PQ_WAKE_TIMER,   // RTC 定时器到点
    PQ_WAKE_KEY,     // 三个按键共用的 GPIO0 被拉低
} pq_wake_t;

typedef struct {
    bool restored;            // 上次确实是本模块睡的(RTC 快照有效)
    uint32_t count;           // 累计入睡次数
    pq_wake_t wake;           // 本次启动的唤醒来源
    uint32_t mode;            // 上次入睡时业务层给的模式号
    uint32_t arg;             // 上次入睡时业务层给的参数
    pq_sleep_result_t last;   // 上一次睡眠的实测结果
    int32_t drift_ppm;        // 当前 RC 时钟漂移估计
    uint32_t drift_samples;   // 已采纳的样本数
} pq_power_info_t;

// 开机早期调用一次：解码 RTC 快照、算实测时长、更新漂移估计。可重复调用（幂等）。
esp_err_t pq_power_init(void);

// 只在 pq_power_init() 之后有效，返回值不可修改。
const pq_power_info_t *pq_power_info(void);

// 经过漂移补偿后真正会请求的时长（用于界面显示）。
uint64_t pq_power_planned_us(uint64_t requested_us);

// 三个按键共用的 GPIO 当前是否被按住。深睡唤醒是低电平触发，
// 按住键入睡会让唤醒条件在入睡瞬间成立，因此必须先判断。
bool pq_power_key_held(void);

// 睡一次。mode/arg 由业务层解释（1 = 自检关卡，2/3 = 番茄钟专注/休息），
// 会原样写进 RTC 快照，唤醒后通过 pq_power_info() 取回。
// 成功后不返回（deep sleep 唤醒即复位）。失败时设备仍可用：
// 返回 ESP_ERR_INVALID_STATE 表示按键被按住，未做任何破坏性操作；
// 返回其他错误表示某个唤醒源武装失败，同样未被动过按键。
esp_err_t pq_power_sleep(uint64_t requested_us, uint32_t mode, uint32_t arg);
