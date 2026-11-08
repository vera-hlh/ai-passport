// components/bsp/src/bsp_battery.c
// CW2017 电量计驱动。
#include "bsp_battery.h"
#include "bsp_i2c.h"
#include "bsp_pins.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdbool.h>

static const char *TAG = "bsp_batt";

#define CW_REG_VERSION   0x00   // 版本号,上电应答即代表芯片在位
#define CW_REG_VCELL_H   0x02   // 14bit 电压,V(uV) = raw * 312.5
#define CW_REG_SOC_H     0x04   // 高字节 = 整数百分比;低字节(0x05)= 1/256 %
#define CW_REG_CONFIG    0x08   // 0xF0=睡眠 / 0x30=复位态 / 0x00=正常
#define CW_REG_SOC_ALERT 0x0B   // bit7=profile UPDATE_FLAG;bit6:0=SOC 告警阈值
#define CW_REG_PROFILE   0x10   // 80 字节电池 profile 起始地址

#define CW_CONFIG_ACTIVE  0x00
#define CW_CONFIG_RESTART 0x30
#define CW_CONFIG_SLEEP   0xF0
#define CW_UPDATE_FLAG    0x80
#define CW_PROFILE_SIZE   80

// Wanli New Energy 500 mAh, profile2 / XIL40X / 2026-10-20.
// Exact vendor bytes: assets/battery/wanli-500mah-profile2-xil40x-20261020.txt.
// Infer the array length so the assertion also catches missing initializer bytes.
static const uint8_t s_battery_profile[] = {
    0x5A, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0xAF, 0xCA, 0xCA, 0xC0, 0xB4, 0xA8, 0xCB, 0xAA,
    0x9F, 0xCF, 0xB5, 0x8A, 0x70, 0x56, 0x4B, 0x40,
    0x34, 0x2B, 0x22, 0x55, 0x41, 0xD2, 0x30, 0xD7,
    0xC9, 0xBE, 0xAE, 0xC8, 0xAE, 0x97, 0x9D, 0x98,
    0x94, 0x9F, 0xB6, 0x89, 0x73, 0x65, 0x57, 0x49,
    0x3D, 0x50, 0x74, 0x87, 0xA3, 0x9B, 0x5E, 0x38,
    0x00, 0x00, 0xAB, 0x02, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x64, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xB7,
};

_Static_assert(sizeof(s_battery_profile) == CW_PROFILE_SIZE,
               "CW2017 battery profile must contain exactly 80 bytes");

static i2c_master_dev_handle_t s_dev;

static esp_err_t cw_read(uint8_t reg, uint8_t *buf, size_t n) {
    if (!s_dev) return ESP_ERR_INVALID_STATE;
    return i2c_master_transmit_receive(s_dev, &reg, 1, buf, n, 100);
}

static esp_err_t cw_write(uint8_t reg, uint8_t val) {
    if (!s_dev) return ESP_ERR_INVALID_STATE;
    uint8_t b[2] = { reg, val };
    return i2c_master_transmit(s_dev, b, 2, 100);
}

// 14 bit VCELL，单位 312.5 µV，换算成 mV。睡眠态的芯片会返回 0。
static int cw_voltage_mv(int *mv) {
    uint8_t b[2] = { 0 };
    if (cw_read(CW_REG_VCELL_H, b, 2) != 0) return -1;
    const uint32_t raw = (((uint32_t)b[0] << 8) | b[1]) & 0x3FFF;
    *mv = (int)((raw * 3125) / 10000);
    return 0;
}

// CONFIG 的低 4 bit 为保留位。复位、睡眠和激活均按芯片规定的时序切换。
static int cw_enter_sleep(void) {
    if (cw_write(CW_REG_CONFIG, CW_CONFIG_RESTART) != 0) return -1;
    vTaskDelay(pdMS_TO_TICKS(20));
    if (cw_write(CW_REG_CONFIG, CW_CONFIG_SLEEP) != 0) return -1;
    vTaskDelay(pdMS_TO_TICKS(10));
    return 0;
}

static int cw_enter_active(void) {
    if (cw_write(CW_REG_CONFIG, CW_CONFIG_RESTART) != 0) return -1;
    vTaskDelay(pdMS_TO_TICKS(20));
    if (cw_write(CW_REG_CONFIG, CW_CONFIG_ACTIVE) != 0) return -1;
    vTaskDelay(pdMS_TO_TICKS(10));
    return 0;
}

// 同时检查 UPDATE_FLAG 和 80 字节内容，避免只凭标志误用旧电芯参数。
static int cw_profile_matches(bool *matches) {
    uint8_t val = 0;
    *matches = false;

    if (cw_read(CW_REG_SOC_ALERT, &val, 1) != 0) return -1;
    if ((val & CW_UPDATE_FLAG) == 0) return 0;

    for (size_t i = 0; i < CW_PROFILE_SIZE; i++) {
        if (cw_read((uint8_t)(CW_REG_PROFILE + i), &val, 1) != 0) return -1;
        if (val != s_battery_profile[i]) return 0;
    }

    *matches = true;
    return 0;
}

// Profile 必须在睡眠态逐字节写入，读回校验后置 UPDATE_FLAG，再重启计算。
static int cw_update_profile(void) {
    uint8_t val = 0;
    if (cw_enter_sleep() != 0) return -1;

    for (size_t i = 0; i < CW_PROFILE_SIZE; i++) {
        if (cw_write((uint8_t)(CW_REG_PROFILE + i), s_battery_profile[i]) != 0) {
            ESP_LOGE(TAG, "写 profile 失败:index=%u", (unsigned)i);
            return -1;
        }
    }

    for (size_t i = 0; i < CW_PROFILE_SIZE; i++) {
        if (cw_read((uint8_t)(CW_REG_PROFILE + i), &val, 1) != 0) return -1;
        if (val != s_battery_profile[i]) {
            ESP_LOGE(TAG, "profile 校验失败:index=%u expected=0x%02X actual=0x%02X",
                     (unsigned)i, s_battery_profile[i], val);
            return -1;
        }
    }

    if (cw_read(CW_REG_SOC_ALERT, &val, 1) != 0) return -1;
    if (cw_write(CW_REG_SOC_ALERT, val | CW_UPDATE_FLAG) != 0) return -1;
    return cw_enter_active();
}

// 首次计算期间 SOC 可能暂时大于 100；最多等待 5 秒再判定初始化失败。
static int cw_wait_soc_ready(void) {
    for (int retry = 0; retry < 50; retry++) {
        uint8_t soc = 0;
        vTaskDelay(pdMS_TO_TICKS(100));
        if (cw_read(CW_REG_SOC_H, &soc, 1) == 0 && soc <= 100) return 0;
    }
    return -1;
}

esp_err_t bsp_battery_init(void) {
    if (s_dev) return ESP_OK;

    esp_err_t e = bsp_i2c_init();
    if (e != ESP_OK) return e;

    i2c_device_config_t dc = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address  = BSP_I2C_CW2017_ADDR,
        .scl_speed_hz    = 100000,
    };
    e = i2c_master_bus_add_device(bsp_i2c_bus(), &dc, &s_dev);
    if (e != ESP_OK) { ESP_LOGE(TAG, "添加 I2C 设备失败: %s", esp_err_to_name(e)); return e; }

    uint8_t ver = 0;
    if (cw_read(CW_REG_VERSION, &ver, 1) != 0) {
        ESP_LOGW(TAG, "CW2017 未应答 —— 用 bsp_i2c_scan() 确认 0x%02X 是否在线;"
                      "无电量计的板子可忽略本项", BSP_I2C_CW2017_ADDR);
        i2c_master_bus_rm_device(s_dev);
        s_dev = NULL;
        return ESP_ERR_NOT_FOUND;
    }
    ESP_LOGI(TAG, "检测到 CW2017 VERSION=0x%02X", ver);

    bool profile_matches = false;
    if (cw_profile_matches(&profile_matches) != 0) {
        ESP_LOGE(TAG, "读取电池 profile 失败");
        e = ESP_FAIL;
        goto fail;
    }

    if (!profile_matches) {
        ESP_LOGI(TAG, "写入万锂新能源 500mAh profile2 XIL40X 20261020");
        if (cw_update_profile() != 0) {
            ESP_LOGE(TAG, "自定义 profile 写入失败");
            e = ESP_FAIL;
            goto fail;
        }
    } else {
        uint8_t config = 0;
        if (cw_read(CW_REG_CONFIG, &config, 1) != 0) {
            e = ESP_FAIL;
            goto fail;
        }
        if (config != CW_CONFIG_ACTIVE && cw_enter_active() != 0) {
            e = ESP_FAIL;
            goto fail;
        }
        ESP_LOGI(TAG, "万锂新能源 500mAh profile2 XIL40X 20261020 已匹配");
    }

    if (cw_wait_soc_ready() != 0) {
        ESP_LOGE(TAG, "等待首次 SOC 计算超时");
        e = ESP_ERR_TIMEOUT;
        goto fail;
    }

    ESP_LOGI(TAG, "CW2017 ready: SOC=%d%%, voltage=%d mV",
             bsp_battery_soc(), bsp_battery_mv());
    return ESP_OK;

fail:
    i2c_master_bus_rm_device(s_dev);
    s_dev = NULL;
    return e;
}

esp_err_t bsp_battery_sleep(void) {
    if (!s_dev) return ESP_OK;

    for (unsigned attempt = 1; attempt <= 2; attempt++) {
        uint8_t actual = 0;
        esp_err_t write_error = cw_write(CW_REG_CONFIG, CW_CONFIG_SLEEP);
        vTaskDelay(pdMS_TO_TICKS(5));
        esp_err_t read_error = cw_read(CW_REG_CONFIG, &actual, 1);
        if (write_error == ESP_OK && read_error == ESP_OK &&
            actual == CW_CONFIG_SLEEP) {
            ESP_LOGI(TAG, "CW2017 已进入休眠并通过寄存器校验");
            return ESP_OK;
        }

        ESP_LOGW(TAG, "CW2017 休眠失败 attempt=%u write=%s(0x%x) "
                      "read=%s(0x%x) actual=0x%02X",
                 attempt, esp_err_to_name(write_error), (unsigned)write_error,
                 esp_err_to_name(read_error), (unsigned)read_error, actual);
        if (attempt < 2) vTaskDelay(pdMS_TO_TICKS(5));
    }

    ESP_LOGE(TAG, "CW2017 连续两次未能进入休眠");
    return ESP_FAIL;
}

int bsp_battery_soc(void) {
    uint8_t b[2] = { 0 };
    if (cw_read(CW_REG_SOC_H, b, 2) != 0) return -1;
    int soc = b[0];                       // 高字节即整数百分比
    if (soc > 100) return -1;             // 芯片未就绪时可能读到 0xFF
    return soc;
}

int bsp_battery_mv(void) {
    int mv = 0;
    if (cw_voltage_mv(&mv) != 0) return -1;
    return mv;
}

// 深睡唤醒后的补救路径，见 bsp_battery.h 的说明。
//
// 实测到的两种坏状态：
//   1. 芯片还停在睡眠态：CONFIG 读回像 0x00，但 VCELL/SOC 都是 0；
//   2. 芯片醒了、电压也对了，但 SOC 仍是 0（profile 丢失后不再参与计算）。
// 所以“可信”不能只看 SOC ≤ 100，还要看 SOC 与电压是否自洽：
// 4.16 V 的锂电不可能是 0%，遇到这种情况要按首次上电的完整流程重写 profile。
static bool cw_soc_plausible(int mv, int soc) {
    if (mv >= 4150) return soc >= 70;
    if (mv >= 4050) return soc >= 50;
    if (mv >= 3950) return soc >= 30;
    if (mv >= 3850) return soc >= 15;
    if (mv >= 3700) return soc >= 5;
    if (mv >= 3400) return soc >= 1;
    return soc >= 0;          // 3.4 V 以下任何非负 SOC 都接受
}

static bool cw_reading_plausible(int *mv_out, int *soc_out) {
    uint8_t soc = 0;
    int mv = 0;
    if (cw_read(CW_REG_SOC_H, &soc, 1) != 0) return false;
    if (cw_voltage_mv(&mv) != 0) return false;
    if (mv < 2500 || mv > 4500) return false;
    if (soc > 100) return false;
    if (!cw_soc_plausible(mv, (int)soc)) return false;
    if (mv_out) *mv_out = mv;
    if (soc_out) *soc_out = (int)soc;
    return true;
}

static bool cw_wait_plausible(int polls, int *mv_out, int *soc_out) {
    for (int poll = 0; poll < polls; poll++) {
        vTaskDelay(pdMS_TO_TICKS(100));
        if (cw_reading_plausible(mv_out, soc_out)) return true;
    }
    return false;
}

esp_err_t bsp_battery_recover(void) {
    if (!s_dev) {
        esp_err_t init_error = bsp_battery_init();
        if (init_error != ESP_OK) return init_error;
    }

    int mv = 0;
    int soc = 0;
    if (cw_reading_plausible(&mv, &soc)) return ESP_OK;   // 本来就正常，立刻返回

    for (int attempt = 1; attempt <= 2; attempt++) {
        esp_err_t e = cw_write(CW_REG_CONFIG, CW_CONFIG_RESTART);
        if (e != ESP_OK) return e;
        vTaskDelay(pdMS_TO_TICKS(100));
        e = cw_write(CW_REG_CONFIG, CW_CONFIG_ACTIVE);
        if (e != ESP_OK) return e;
        if (cw_wait_plausible(20, &mv, &soc)) {
            ESP_LOGI(TAG, "电量计已唤醒: SOC=%d%% %d mV", soc, mv);
            return ESP_OK;
        }

        // 纯唤醒不够（SOC 与电压不自洽）：按首次上电的流程重写 profile，
        // 让芯片用完整参数重新计算 SOC。
        ESP_LOGW(TAG, "电量计 SOC=%d%% @ %d mV 不自洽(第 %d 次)，重写 profile",
                 soc, mv, attempt);
        if (cw_update_profile() != 0) {
            ESP_LOGE(TAG, "重写电池 profile 失败");
            return ESP_FAIL;
        }
        if (cw_wait_plausible(30, &mv, &soc)) {
            ESP_LOGI(TAG, "电量计已恢复: SOC=%d%% %d mV", soc, mv);
            return ESP_OK;
        }
    }

    ESP_LOGE(TAG, "电量计连续两次未能恢复，电量读数不可信");
    return ESP_ERR_TIMEOUT;
}
