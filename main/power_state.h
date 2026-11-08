// main/power_state.h —— 深睡模块的纯逻辑部分。
//
// 这里只做算术与校验:时长钳制、RTC 快照的封装/校验、睡眠时长评估、RC 时钟漂移补偿。
// 不依赖 ESP-IDF 与 LVGL，因此可以在宿主上编译并运行单元测试；
// 真正调用 esp_sleep_*、关外设的部分在 power.c。
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// RTC 快照的魔数:深睡唤醒是复位,RTC 内存内容靠它区分“上次睡过”与“随机上电”。
#define PQ_RTC_MAGIC 0x50515553u  // 'PQUS'

// 允许的睡眠时长:至少 1 秒,最长 24 小时。超出即拒绝,避免把“0 秒”或“永远”当成参数。
#define PQ_DURATION_MIN_US (1ULL * 1000ULL * 1000ULL)
#define PQ_DURATION_MAX_US (24ULL * 60ULL * 60ULL * 1000ULL * 1000ULL)

// 漂移补偿限幅:内部 RC 慢时钟的温漂再大也不该超过 ±5%,超过说明测错了。
#define PQ_DRIFT_LIMIT_PPM 50000

// 单次样本至少 1 秒才计入漂移,太短的睡眠里按键抖动与启动开销占比太高。
#define PQ_DRIFT_MIN_SAMPLE_US (1ULL * 1000ULL * 1000ULL)

// 实测时长与请求时长相差超过 ±5% 时判定为“不可信”,不参与补偿。
#define PQ_SLEEP_TOLERANCE_PPM 50000

// 深睡前后写入 RTC 内存的快照。arm_us / requested_us 用于唤醒后计算实测时长。
typedef struct {
    uint32_t magic;
    uint32_t seq;           // 累计入睡次数,唤醒后原样取回
    uint32_t mode;          // 1 = 自检关卡,2 = 番茄钟专注,3 = 番茄钟休息
    uint32_t arg;           // 关卡号或番次,由业务层解释
    uint64_t arm_us;        // 入睡瞬间的 RTC 计时
    uint64_t requested_us;  // 请求的睡眠时长(未经补偿)
    uint32_t checksum;      // 覆盖本结构其余字节的 FNV-1a
} pq_rtc_state_t;

// FNV-1a 32 位,足以发现未初始化/被写坏的 RTC 内容。
uint32_t pq_checksum_bytes(const void *data, size_t len);

// 写入魔数与校验和,使快照可被 pq_rtc_state_valid() 接受。
void pq_rtc_state_seal(pq_rtc_state_t *state);

// 魔数正确、校验和匹配才为真。
bool pq_rtc_state_valid(const pq_rtc_state_t *state);

// 把请求时长钳制到 [PQ_DURATION_MIN_US, PQ_DURATION_MAX_US]。
uint64_t pq_duration_clamp(uint64_t requested_us);

// RC 慢时钟的漂移估计:ppm 为“实测/预期”的偏差(正 = 实际睡得比预期久)。
typedef struct {
    int32_t ppm;
    uint32_t samples;
} pq_drift_t;

void pq_drift_init(pq_drift_t *drift);

// 记入一次样本。样本过短、预期为零或偏差超出容差时返回 false 且不改变估计。
bool pq_drift_update(pq_drift_t *drift, uint64_t expected_us, uint64_t measured_us);

// 按当前估计调整下一次请求时长;没有样本时原样返回(仅做钳制)。
uint64_t pq_drift_adjust(const pq_drift_t *drift, uint64_t target_us);

// 一次深睡的结果。valid=false 表示无法可信地测出时长(RTC 计时被复位或数值不合理)。
typedef struct {
    bool valid;
    uint64_t requested_us;
    uint64_t measured_us;
    int32_t error_ppm;
} pq_sleep_result_t;

// 用快照与唤醒后的 RTC 计时得出结果。时长不合理时 valid=false。
bool pq_sleep_result_from(const pq_rtc_state_t *state, uint64_t now_us,
                          pq_sleep_result_t *out);
