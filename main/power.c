// main/power.c —— 深睡模块的实现，契约见 power.h，算术见 power_state.c。
#include "power.h"

#include "bsp_audio.h"
#include "bsp_battery.h"
#include "bsp_button.h"
#include "bsp_display.h"
#include "bsp_i2c.h"
#include "bsp_pins.h"

#include "esp_attr.h"
#include "esp_log.h"
#include "esp_private/esp_clk.h"
#include "esp_sleep.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "pq_power";

// 跨深睡保留：快照、累计计数与漂移估计都必须活过复位。
static RTC_DATA_ATTR pq_rtc_state_t s_rtc;
static RTC_DATA_ATTR pq_drift_t s_drift;

static pq_power_info_t s_info;
static bool s_init_done;

static pq_wake_t wake_reason_from_causes(uint32_t causes)
{
    if (causes & (1u << ESP_SLEEP_WAKEUP_TIMER)) return PQ_WAKE_TIMER;
    if (causes & (1u << ESP_SLEEP_WAKEUP_GPIO)) return PQ_WAKE_KEY;
    return PQ_WAKE_UNKNOWN;
}

esp_err_t pq_power_init(void)
{
    if (s_init_done) return ESP_OK;
    s_init_done = true;

    s_info.restored = pq_rtc_state_valid(&s_rtc);
    s_info.wake = wake_reason_from_causes(esp_sleep_get_wakeup_causes());
    s_info.count = s_info.restored ? s_rtc.seq : 0;
    s_info.mode = s_info.restored ? s_rtc.mode : 0;
    s_info.arg = s_info.restored ? s_rtc.arg : 0;

    if (!s_info.restored) {
        pq_drift_init(&s_drift);
    } else if (s_drift.ppm > 10000 || s_drift.ppm < -10000) {
        // 深睡定时器与 RTC 计时用的是同一个 RC 慢时钟，真实漂移不可能超过 1%。
        // 超过就说明估计值把“醒来后的固定开销”当成了漂移（旧固件留下的脏数据），
        // 直接清零，否则补偿会越垫越高、短时长档必然超差。
        ESP_LOGW(TAG, "丢弃异常漂移估计 %ld ppm", (long)s_drift.ppm);
        pq_drift_init(&s_drift);
    } else {
        // esp_clk_rtc_time() 在上一次启动里被记入 arm_us。若 RTC 计时在深睡期间
        // 被复位，这个差值为负，pq_sleep_result_from() 会返回“测不出来”而不是负数。
        //
        // 必须扣掉“醒来之后到现在”这段清醒时间：RTC 一直走，而我们在 app_main
        // 里读到它时，引导程序与本模块初始化已经花掉几百毫秒。不扣的话这段固定开销
        // 会被漂移补偿当成“睡久了”，补偿值越垫越高，10 秒档很快就超出容差。
        const uint64_t now = esp_clk_rtc_time();
        const uint64_t awake = (uint64_t)esp_timer_get_time();
        uint64_t measured = now > s_rtc.arm_us ? now - s_rtc.arm_us : 0;
        measured = measured > awake ? measured - awake : 0;

        pq_sleep_result_from(&s_rtc, s_rtc.arm_us + measured, &s_info.last);
        ESP_LOGI(TAG, "时长校正: 原始 %llu us - 清醒 %llu us = %llu us",
                 (unsigned long long)(now > s_rtc.arm_us ? now - s_rtc.arm_us : 0),
                 (unsigned long long)awake, (unsigned long long)measured);
        // 同源时钟下真实漂移接近 0：只把 0.5% 以内的样本计入补偿，
        // 更大的偏差来自引导/初始化的固定开销，不能拿它调整下一次睡眠。
        if (s_info.last.valid && s_info.wake == PQ_WAKE_TIMER &&
            s_info.last.error_ppm <= 5000 && s_info.last.error_ppm >= -5000) {
            if (!pq_drift_update(&s_drift, s_info.last.requested_us, s_info.last.measured_us)) {
                ESP_LOGW(TAG, "漂移样本被拒: 请求 %llu us, 实测 %llu us",
                         (unsigned long long)s_info.last.requested_us,
                         (unsigned long long)s_info.last.measured_us);
            }
        }
    }

    s_info.drift_ppm = s_drift.ppm;
    s_info.drift_samples = s_drift.samples;

    ESP_LOGI(TAG, "唤醒来源=%d 累计=%lu 快照=%d 实测=%llu us 偏差=%ld ppm 样本=%lu",
             (int)s_info.wake, (unsigned long)s_info.count, (int)s_info.restored,
             (unsigned long long)s_info.last.measured_us, (long)s_info.drift_ppm,
             (unsigned long)s_info.drift_samples);
    return ESP_OK;
}

const pq_power_info_t *pq_power_info(void)
{
    return &s_info;
}

uint64_t pq_power_planned_us(uint64_t requested_us)
{
    return pq_drift_adjust(&s_drift, requested_us);
}

bool pq_power_key_held(void)
{
    const int mv = bsp_button_read_mv();
    // -1 表示 ADC 不可读（按键驱动尚未初始化或已拆掉）：此时不阻止入睡，
    // 让 bsp_button_prepare_deep_sleep() 的回读去把关。
    if (mv < 0) return false;
    return mv < BSP_BTN_MV_RELEASED_MIN;
}

static void log_if_failed(const char *step, esp_err_t error)
{
    if (error != ESP_OK) {
        ESP_LOGW(TAG, "入睡前 %s 失败，继续: %s", step, esp_err_to_name(error));
    }
}

esp_err_t pq_power_sleep(uint64_t requested_us, uint32_t mode, uint32_t arg)
{
    const uint64_t clamped = pq_duration_clamp(requested_us);
    const uint64_t planned = pq_drift_adjust(&s_drift, clamped);

    if (pq_power_key_held()) {
        ESP_LOGW(TAG, "按键被按住，拒绝入睡");
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t err = esp_sleep_enable_timer_wakeup(planned);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "定时器唤醒源武装失败: %s", esp_err_to_name(err));
        esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_TIMER);
        return err;
    }

    // 按键脚交回数字输入 + 上拉并回读电平。这一步是终端的：调用后按键驱动不再存在，
    // 失败只能重启恢复，否则会留下一个收不到输入的界面。
    int level = 0;
    err = bsp_button_prepare_deep_sleep(&level);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "按键交回失败，重启恢复外设: %s", esp_err_to_name(err));
        esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_TIMER);
        esp_restart();
        return err;
    }
    if (level != 1) {
        ESP_LOGW(TAG, "按键仍被按住(电平=%d)，这次深睡可能立即醒回", level);
    }

    // 任一键都把 GPIO0 拉到低电平，故按低电平武装。第一个参数是位掩码，不是引脚号。
    log_if_failed("GPIO 唤醒源",
                  esp_deep_sleep_enable_gpio_wakeup(
                      1ULL << BSP_BTN_GPIO, ESP_GPIO_WAKEUP_GPIO_LOW));

    // CW2017 与 ES8311 共用 I2C，先完成电量计写入与回读，再停其他。
    log_if_failed("CW2017 休眠", bsp_battery_sleep());
    log_if_failed("ES8311 休眠", bsp_audio_sleep());
    // 即使 codec 寄存器操作失败，也继续停时钟并释放引脚。
    log_if_failed("I2S 引脚释放", bsp_audio_prepare_deep_sleep());
    log_if_failed("共享 I2C 释放", bsp_i2c_prepare_deep_sleep());

    // 加锁等待当前 flush 完成，阻止 LVGL 在 LCD 关闭后继续刷屏。
    if (!bsp_lvgl_lock(1000)) {
        ESP_LOGE(TAG, "无法停止 LVGL 刷屏，重启恢复外设");
        esp_restart();
        return ESP_ERR_TIMEOUT;
    }
    bsp_display_backlight(0);
    log_if_failed("ST7789 休眠", bsp_display_prepare_deep_sleep());

    // 快照必须在所有外设停下之后写：arm_us 越接近真正的入睡时刻，实测越准。
    s_rtc.seq = (s_info.restored ? s_rtc.seq : 0) + 1;
    s_rtc.mode = mode;
    s_rtc.arg = arg;
    s_rtc.arm_us = esp_clk_rtc_time();
    s_rtc.requested_us = clamped;
    pq_rtc_state_seal(&s_rtc);

    ESP_LOGI(TAG, "进入深睡: 请求 %llu us，补偿后 %llu us，第 %lu 次",
             (unsigned long long)clamped, (unsigned long long)planned,
             (unsigned long)s_rtc.seq);

    esp_deep_sleep_start();

    // 深睡准备接口返回后总线已不可在本次运行中恢复。
    ESP_LOGE(TAG, "esp_deep_sleep_start 意外返回，重启恢复外设");
    esp_restart();
    return ESP_FAIL;
}
