// main/power_state.c —— 深睡纯逻辑的实现，见 power_state.h。
#include "power_state.h"

#include <string.h>

#define FNV_OFFSET_BASIS 2166136261u
#define FNV_PRIME 16777619u

static uint32_t fnv_update(uint32_t hash, const void *data, size_t len)
{
    const uint8_t *bytes = (const uint8_t *)data;
    for (size_t i = 0; i < len; i++) {
        hash ^= bytes[i];
        hash *= FNV_PRIME;
    }
    return hash;
}

uint32_t pq_checksum_bytes(const void *data, size_t len)
{
    return fnv_update(FNV_OFFSET_BASIS, data, len);
}

// 只校验有意义的字段，不覆盖结构体内存里的对齐填充：
// 填充字节在 RTC 内存里没有定义值，把它们算进校验会让有效快照被误判。
static uint32_t pq_rtc_state_checksum(const pq_rtc_state_t *state)
{
    uint32_t hash = FNV_OFFSET_BASIS;
    hash = fnv_update(hash, &state->magic, sizeof state->magic);
    hash = fnv_update(hash, &state->seq, sizeof state->seq);
    hash = fnv_update(hash, &state->mode, sizeof state->mode);
    hash = fnv_update(hash, &state->arg, sizeof state->arg);
    hash = fnv_update(hash, &state->arm_us, sizeof state->arm_us);
    hash = fnv_update(hash, &state->requested_us, sizeof state->requested_us);
    return hash;
}

void pq_rtc_state_seal(pq_rtc_state_t *state)
{
    if (!state) return;
    state->magic = PQ_RTC_MAGIC;
    state->checksum = pq_rtc_state_checksum(state);
}

bool pq_rtc_state_valid(const pq_rtc_state_t *state)
{
    if (!state) return false;
    if (state->magic != PQ_RTC_MAGIC) return false;
    if (state->requested_us < PQ_DURATION_MIN_US) return false;
    if (state->requested_us > PQ_DURATION_MAX_US) return false;
    return state->checksum == pq_rtc_state_checksum(state);
}

uint64_t pq_duration_clamp(uint64_t requested_us)
{
    if (requested_us < PQ_DURATION_MIN_US) return PQ_DURATION_MIN_US;
    if (requested_us > PQ_DURATION_MAX_US) return PQ_DURATION_MAX_US;
    return requested_us;
}

void pq_drift_init(pq_drift_t *drift)
{
    if (!drift) return;
    drift->ppm = 0;
    drift->samples = 0;
}

bool pq_drift_update(pq_drift_t *drift, uint64_t expected_us, uint64_t measured_us)
{
    if (!drift) return false;
    if (expected_us < PQ_DRIFT_MIN_SAMPLE_US) return false;
    if (measured_us == 0) return false;

    const int64_t delta = (int64_t)measured_us - (int64_t)expected_us;
    const int64_t ppm = delta * 1000000LL / (int64_t)expected_us;
    if (ppm > PQ_SLEEP_TOLERANCE_PPM || ppm < -PQ_SLEEP_TOLERANCE_PPM) return false;

    int64_t clamped = ppm;
    if (clamped > PQ_DRIFT_LIMIT_PPM) clamped = PQ_DRIFT_LIMIT_PPM;
    if (clamped < -PQ_DRIFT_LIMIT_PPM) clamped = -PQ_DRIFT_LIMIT_PPM;

    if (drift->samples == 0) {
        drift->ppm = (int32_t)clamped;
    } else {
        // 指数平滑：新样本占 1/4，避免单次测量抖动把补偿带偏。
        drift->ppm = (int32_t)(((int64_t)drift->ppm * 3 + clamped) / 4);
    }
    if (drift->samples < UINT8_MAX) drift->samples++;
    return true;
}

uint64_t pq_drift_adjust(const pq_drift_t *drift, uint64_t target_us)
{
    const uint64_t clamped = pq_duration_clamp(target_us);
    if (!drift || drift->samples == 0 || drift->ppm == 0) return clamped;

    const int64_t scale = 1000000LL + drift->ppm;
    int64_t adjusted = (int64_t)clamped * scale / 1000000LL;

    // 再兜一层 ±20%：即便估计值异常，也不让一次补偿把时长推到离谱的范围。
    const int64_t lower = (int64_t)(clamped - clamped / 5);
    const int64_t upper = (int64_t)(clamped + clamped / 5);
    if (adjusted < lower) adjusted = lower;
    if (adjusted > upper) adjusted = upper;

    if (adjusted < (int64_t)PQ_DURATION_MIN_US) adjusted = (int64_t)PQ_DURATION_MIN_US;
    if (adjusted > (int64_t)PQ_DURATION_MAX_US) adjusted = (int64_t)PQ_DURATION_MAX_US;
    return (uint64_t)adjusted;
}

bool pq_sleep_result_from(const pq_rtc_state_t *state, uint64_t now_us,
                          pq_sleep_result_t *out)
{
    if (!out) return false;
    memset(out, 0, sizeof *out);
    if (!pq_rtc_state_valid(state)) return false;

    out->requested_us = state->requested_us;
    if (now_us <= state->arm_us) {
        // RTC 计时在唤醒后被复位：arm_us 来自上一次启动，无法相减。
        return false;
    }

    const uint64_t measured = now_us - state->arm_us;
    out->measured_us = measured;

    const int64_t delta = (int64_t)measured - (int64_t)state->requested_us;
    out->error_ppm = (int32_t)(delta * 1000000LL / (int64_t)state->requested_us);

    // 合理区间：至少 1 ms，且落在请求值的 ±5% 之内。超出即判为不可信，
    // 宁可报告“测不出来”，也不把错误数字写进漂移估计。
    if (measured < 1000ULL) return false;
    if (out->error_ppm > PQ_SLEEP_TOLERANCE_PPM) return false;
    if (out->error_ppm < -PQ_SLEEP_TOLERANCE_PPM) return false;

    out->valid = true;
    return true;
}
