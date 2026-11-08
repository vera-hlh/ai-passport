// tests/test_power_state.c —— 深睡纯逻辑的宿主测试（无需硬件）。
//
// 覆盖：RTC 快照校验、时长钳制、漂移样本的接受/拒绝与平滑、实测时长评估。
#include "power_state.h"

#include <stdio.h>
#include <string.h>

static int g_failures;

#define CHECK(cond)                                                          \
    do {                                                                     \
        if (!(cond)) {                                                       \
            printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);           \
            g_failures++;                                                    \
        }                                                                    \
    } while (0)

static pq_rtc_state_t make_state(uint64_t requested_us, uint64_t arm_us)
{
    pq_rtc_state_t state;
    memset(&state, 0, sizeof state);
    state.seq = 7;
    state.mode = 1;
    state.arg = 0;
    state.arm_us = arm_us;
    state.requested_us = requested_us;
    pq_rtc_state_seal(&state);
    return state;
}

static void test_seal_and_validate(void)
{
    pq_rtc_state_t state = make_state(15ULL * 1000 * 1000, 1000);
    CHECK(pq_rtc_state_valid(&state));

    // 任一字段被写坏都必须被校验和发现。
    pq_rtc_state_t broken = state;
    broken.seq += 1;
    CHECK(!pq_rtc_state_valid(&broken));

    broken = state;
    broken.requested_us += 1;
    CHECK(!pq_rtc_state_valid(&broken));

    // 上电初值（全零）不能被当成有效快照。
    pq_rtc_state_t zero;
    memset(&zero, 0, sizeof zero);
    CHECK(!pq_rtc_state_valid(&zero));

    // 请求时长短于下限的快照视为无效，避免把异常值带进评估。
    pq_rtc_state_t too_short = make_state(1000, 1000);
    CHECK(!pq_rtc_state_valid(&too_short));
}

static void test_duration_clamp(void)
{
    CHECK(pq_duration_clamp(0) == PQ_DURATION_MIN_US);
    CHECK(pq_duration_clamp(PQ_DURATION_MIN_US - 1) == PQ_DURATION_MIN_US);
    CHECK(pq_duration_clamp(PQ_DURATION_MIN_US) == PQ_DURATION_MIN_US);
    CHECK(pq_duration_clamp(15ULL * 1000 * 1000) == 15ULL * 1000 * 1000);
    CHECK(pq_duration_clamp(PQ_DURATION_MAX_US + 1) == PQ_DURATION_MAX_US);
}

static void test_drift(void)
{
    pq_drift_t drift;
    pq_drift_init(&drift);

    const uint64_t target = 15ULL * 1000 * 1000;
    // 没有样本时不做补偿。
    CHECK(pq_drift_adjust(&drift, target) == target);

    // +1% 的样本：15 s 实测 15.15 s。
    CHECK(pq_drift_update(&drift, target, target + target / 100));
    CHECK(drift.ppm == 10000);
    CHECK(drift.samples == 1);
    CHECK(pq_drift_adjust(&drift, target) == target + target / 100);

    // 超出 ±5% 容差的样本必须被拒绝，且不改变估计。
    CHECK(!pq_drift_update(&drift, target, target + target / 10));
    CHECK(drift.ppm == 10000);

    // 过短的样本不参与估计（按键抖动与启动开销占比太高）。
    CHECK(!pq_drift_update(&drift, 500ULL * 1000, 500ULL * 1000));

    // -1% 的样本与已有 +1% 做 3:1 平滑 → +0.5%。
    CHECK(pq_drift_update(&drift, target, target - target / 100));
    CHECK(drift.ppm == 5000);
    CHECK(drift.samples == 2);

    // 估计值被限幅在 ±5%，补偿后的时长不会超过 ±20% 的安全边界。
    pq_drift_t wide;
    pq_drift_init(&wide);
    wide.ppm = 80000;   // 手工制造越界估计
    wide.samples = 4;
    const uint64_t adjusted = pq_drift_adjust(&wide, target);
    CHECK(adjusted <= target + target / 5);
    CHECK(adjusted >= target - target / 5);

    // 补偿后仍不低于下限。
    pq_drift_t negative;
    pq_drift_init(&negative);
    negative.ppm = -50000;
    negative.samples = 2;
    const uint64_t short_target = PQ_DURATION_MIN_US;
    CHECK(pq_drift_adjust(&negative, short_target) >= PQ_DURATION_MIN_US);
}

static void test_sleep_result(void)
{
    const uint64_t target = 15ULL * 1000 * 1000;
    const uint64_t arm = 123456;
    pq_rtc_state_t state = make_state(target, arm);

    pq_sleep_result_t result;
    // 实测 15.03 s → +0.2%。
    CHECK(pq_sleep_result_from(&state, arm + target + 30000, &result));
    CHECK(result.valid);
    CHECK(result.measured_us == target + 30000);
    CHECK(result.error_ppm == 2000);

    // 唤醒后 RTC 计时被复位：无法相减，必须报“测不出来”。
    CHECK(!pq_sleep_result_from(&state, arm - 1, &result));
    CHECK(!result.valid);

    // 与请求值相差 33%：不可信，不能写进漂移估计。
    CHECK(!pq_sleep_result_from(&state, arm + 10ULL * 1000 * 1000, &result));
    CHECK(!result.valid);

    // 无效快照不产生结果。
    pq_rtc_state_t zero;
    memset(&zero, 0, sizeof zero);
    CHECK(!pq_sleep_result_from(&zero, arm + target, &result));
}

int main(void)
{
    test_seal_and_validate();
    test_duration_clamp();
    test_drift();
    test_sleep_result();

    if (g_failures != 0) {
        printf("test_power_state: FAIL (%d)\n", g_failures);
        return 1;
    }
    printf("test_power_state: PASS\n");
    return 0;
}
