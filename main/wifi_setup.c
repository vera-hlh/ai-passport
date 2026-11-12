// main/wifi_setup.c —— 手机网页配网：设备开 SoftAP，手机浏览器提交 SSID/密码。
//
// 这一版先做“手动打开 192.168.4.1”：强制门户（手机自动弹页）需要 DNS 劫持，
// 等这条链路跑通再加。流程：
//   进页面 → 扫描一次附近的 AP（给页面下拉列表）→ 启动 SoftAP "PQ-Setup"
//   → 手机连热点、浏览器打开 192.168.4.1 → 选 SSID、输密码 → 设备切 STA 连接
//   → 拿到 IP 后页面与屏幕都显示成功。
//
// 安全：密码只保存在 RAM 与 NVS（明文），不打日志；页面提供“忘记”入口。
#include "wifi_setup.h"

#include "app_home.h"
#include "bsp_display.h"
#include "demo_radio.h"
#include "pq_fonts.h"
#include "pq_ui.h"
#include "quest.h"
#include "ui_strings.h"

#include "esp_event.h"
#include "esp_http_client.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "esp_spiffs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"
#include "nvs.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <stdio.h>

static const char *TAG = "pq_wifisetup";

#define AP_SSID "PQ-Setup"
#define AP_PASSWORD ""            // 开放热点，方便手机直接连；页面里有提示
#define AP_MAX_STA 2
#define MAX_APS 10
#define SSID_MAX 33
#define PASS_MAX 65
#define CONNECT_TIMEOUT_MS 15000

#define WIFI_NVS_NAMESPACE "pq_wifi_cred"
#define WIFI_NVS_KEY "sta"

typedef enum {
    SETUP_IDLE = 0,      // 热点已开，等手机提交
    SETUP_CONNECTING,
    SETUP_CONNECTED,
    SETUP_FAILED,
} setup_state_t;

typedef struct {
    char ssid[SSID_MAX];
    char password[PASS_MAX];
} cred_t;

static lv_obj_t *s_page;
static lv_obj_t *s_status;
static lv_obj_t *s_detail;
static lv_obj_t *s_hint;
static bool s_active;

static esp_netif_t *s_ap_netif;
static esp_netif_t *s_sta_netif;
static httpd_handle_t s_server;
static bool s_wifi_inited;
static bool s_wifi_started;
static volatile setup_state_t s_state;
static char s_ip[16];
static cred_t s_cred;
static volatile bool s_connect_requested;
static wifi_ap_record_t s_aps[MAX_APS];
static uint16_t s_ap_count;
static TaskHandle_t s_task;

// ------------------------------------------------------------------ 页面

static void refresh(void)
{
    if (!s_status) return;
    switch (s_state) {
        case SETUP_CONNECTING:
            lv_label_set_text(s_status, PQ_TXT_CONNECTING);
            lv_obj_set_style_text_color(s_status, lv_color_hex(PQ_COLOR_ACCENT), 0);
            break;
        case SETUP_CONNECTED:
            lv_label_set_text(s_status, PQ_TXT_CONNECTED);
            lv_obj_set_style_text_color(s_status, lv_color_hex(PQ_COLOR_OK), 0);
            if (s_detail) lv_label_set_text_fmt(s_detail, "IP  %s", s_ip);
            break;
        case SETUP_FAILED:
            lv_label_set_text(s_status, PQ_TXT_CONNECT_FAILED);
            lv_obj_set_style_text_color(s_status, lv_color_hex(PQ_COLOR_WARN), 0);
            break;
        default:
            lv_label_set_text(s_status, PQ_TXT_WAIT_PHONE);
            lv_obj_set_style_text_color(s_status, lv_color_hex(PQ_COLOR_INK), 0);
            if (s_detail) {
                lv_label_set_text_fmt(s_detail, "%s\n%s\nhttp://192.168.4.1",
                                      PQ_TXT_AP_SSID, PQ_TXT_OPEN_BROWSER);
            }
            break;
    }
}

// ------------------------------------------------------------------ 凭据

static void cred_load(void)
{
    nvs_handle_t handle;
    if (nvs_open(WIFI_NVS_NAMESPACE, NVS_READONLY, &handle) != ESP_OK) return;
    size_t len = sizeof s_cred;
    if (nvs_get_blob(handle, WIFI_NVS_KEY, &s_cred, &len) != ESP_OK) {
        memset(&s_cred, 0, sizeof s_cred);
    }
    nvs_close(handle);
}

static void cred_save(void)
{
    nvs_handle_t handle;
    if (nvs_open(WIFI_NVS_NAMESPACE, NVS_READWRITE, &handle) != ESP_OK) return;
    nvs_erase_key(handle, WIFI_NVS_KEY);
    if (s_cred.ssid[0]) {
        nvs_set_blob(handle, WIFI_NVS_KEY, &s_cred, sizeof s_cred);
    }
    nvs_commit(handle);
    nvs_close(handle);
}

// ------------------------------------------------------------------ HTTP

static void html_escape(char *out, size_t len, const char *in)
{
    size_t used = 0;
    for (const char *p = in; *p && used + 6 < len; p++) {
        char c = *p;
        if (c == '<') used += (size_t)snprintf(out + used, len - used, "&lt;");
        else if (c == '>') used += (size_t)snprintf(out + used, len - used, "&gt;");
        else if (c == '&') used += (size_t)snprintf(out + used, len - used, "&amp;");
        else if (c == '"') used += (size_t)snprintf(out + used, len - used, "&quot;");
        else if (c >= 0x20) out[used++] = c;
    }
    out[used] = 0;
}

static esp_err_t root_get(httpd_req_t *req)
{
    static char page[4096];
    size_t used = 0;
    used += (size_t)snprintf(page + used, sizeof page - used,
        "<!doctype html><meta charset=utf-8>"
        "<meta name=viewport content='width=device-width,initial-scale=1'>"
        "<title>Passport 配网</title>"
        "<style>body{font-family:sans-serif;margin:24px;max-width:420px}"
        "select,input{width:100%%;padding:8px;margin:6px 0;font-size:16px}"
        "button{width:100%%;padding:12px;font-size:18px;margin-top:12px}</style>"
        "<h2>连接 Wi-Fi</h2><form method=post action=/connect>"
        "<label>网络</label><select name=ssid>");

    for (uint16_t i = 0; i < s_ap_count && i < MAX_APS; i++) {
        char ssid[96];
        html_escape(ssid, sizeof ssid, (const char *)s_aps[i].ssid);
        if (!ssid[0]) continue;
        used += (size_t)snprintf(page + used, sizeof page - used,
                                "<option value=\"%s\">%s (%d dBm)</option>",
                                ssid, ssid, (int)s_aps[i].rssi);
    }
    used += (size_t)snprintf(page + used, sizeof page - used,
        "</select><label>密码</label>"
        "<input type=password name=pass maxlength=64>"
        "<button type=submit>连接</button></form>"
        "<p style='color:#666'>设备热点：%s（开放，无需密码）</p>", AP_SSID);

    httpd_resp_set_type(req, "text/html; charset=utf-8");
    return httpd_resp_send(req, page, (ssize_t)strlen(page));
}

static void url_decode(char *dst, size_t len, const char *src, size_t src_len)
{
    size_t out = 0;
    for (size_t i = 0; i < src_len && out + 1 < len; i++) {
        if (src[i] == '+') {
            dst[out++] = ' ';
        } else if (src[i] == '%' && i + 2 < src_len) {
            unsigned value = 0;
            if (sscanf(src + i + 1, "%2x", &value) == 1) {
                dst[out++] = (char)value;
                i += 2;
            }
        } else {
            dst[out++] = src[i];
        }
    }
    dst[out] = 0;
}

static void form_field(const char *body, const char *key, char *out, size_t len)
{
    out[0] = 0;
    char pattern[24];
    snprintf(pattern, sizeof pattern, "%s=", key);
    const char *p = strstr(body, pattern);
    if (!p) return;
    p += strlen(pattern);
    const char *end = strchr(p, '&');
    const size_t n = end ? (size_t)(end - p) : strlen(p);
    url_decode(out, len, p, n);
}

static esp_err_t connect_post(httpd_req_t *req)
{
    char body[256];
    int received = httpd_req_recv(req, body, sizeof body - 1);
    if (received <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "empty");
        return ESP_FAIL;
    }
    body[received] = 0;

    char ssid[SSID_MAX];
    char pass[PASS_MAX];
    form_field(body, "ssid", ssid, sizeof ssid);
    form_field(body, "pass", pass, sizeof pass);
    if (!ssid[0]) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "missing ssid");
        return ESP_FAIL;
    }

    snprintf(s_cred.ssid, sizeof s_cred.ssid, "%s", ssid);
    snprintf(s_cred.password, sizeof s_cred.password, "%s", pass);
    s_connect_requested = true;   // 由主循环里的任务接手，避免在 HTTP 任务里阻塞
    ESP_LOGI(TAG, "收到配网请求: ssid=%s（密码不打印）", s_cred.ssid);

    httpd_resp_set_type(req, "text/html; charset=utf-8");
    return httpd_resp_send(req,
        "<meta charset=utf-8><body style='font-family:sans-serif;margin:24px'>"
        "<h3>已提交</h3><p>设备正在连接，请回看设备屏幕。</p></body>", -1);
}

static void http_start(void)
{
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.max_uri_handlers = 4;
    if (httpd_start(&s_server, &cfg) != ESP_OK) {
        ESP_LOGE(TAG, "HTTP 服务启动失败");
        s_server = NULL;
        return;
    }
    httpd_uri_t root = { .uri = "/", .method = HTTP_GET, .handler = root_get };
    httpd_uri_t connect = { .uri = "/connect", .method = HTTP_POST, .handler = connect_post };
    httpd_register_uri_handler(s_server, &root);
    httpd_register_uri_handler(s_server, &connect);
}

// ------------------------------------------------------------------ Wi-Fi 栈

static bool wifi_stack_up(void)
{
    if (demo_radio_nvs_prepare() != ESP_OK) return false;
    if (demo_radio_network_prepare() != ESP_OK) return false;

    // 先扫描一次，给配网页做 SSID 下拉列表。
    esp_netif_config_t sta_cfg = ESP_NETIF_DEFAULT_WIFI_STA();
    s_sta_netif = esp_netif_new(&sta_cfg);
    if (!s_sta_netif) return false;
    if (esp_netif_attach_wifi_station(s_sta_netif) != ESP_OK) return false;
    if (esp_wifi_set_default_wifi_sta_handlers() != ESP_OK) return false;

    wifi_init_config_t init_cfg = WIFI_INIT_CONFIG_DEFAULT();
    if (esp_wifi_init(&init_cfg) != ESP_OK) return false;
    s_wifi_inited = true;

    esp_netif_config_t ap_cfg = ESP_NETIF_DEFAULT_WIFI_AP();
    s_ap_netif = esp_netif_new(&ap_cfg);
    if (!s_ap_netif) return false;
    if (esp_netif_attach_wifi_ap(s_ap_netif) != ESP_OK) return false;

    if (esp_wifi_set_storage(WIFI_STORAGE_RAM) != ESP_OK) return false;
    if (esp_wifi_set_mode(WIFI_MODE_APSTA) != ESP_OK) return false;

    wifi_config_t ap = { 0 };
    snprintf((char *)ap.ap.ssid, sizeof ap.ap.ssid, "%s", AP_SSID);
    ap.ap.ssid_len = (uint8_t)strlen(AP_SSID);
    ap.ap.channel = 1;
    ap.ap.max_connection = AP_MAX_STA;
    ap.ap.authmode = WIFI_AUTH_OPEN;
    if (esp_wifi_set_config(WIFI_IF_AP, &ap) != ESP_OK) return false;

    if (esp_wifi_start() != ESP_OK) return false;
    s_wifi_started = true;

    // 扫描（APSTA 下可以边开热点边扫）
    uint16_t count = MAX_APS;
    if (esp_wifi_scan_start(NULL, true) == ESP_OK) {
        if (esp_wifi_scan_get_ap_records(&count, s_aps) == ESP_OK) s_ap_count = count;
    }
    ESP_LOGI(TAG, "SoftAP %s 已启动，扫描到 %u 个 AP", AP_SSID, (unsigned)s_ap_count);
    return true;
}

static void wifi_stack_down(void)
{
    if (s_server) {
        httpd_stop(s_server);
        s_server = NULL;
    }
    if (s_wifi_started) {
        esp_wifi_stop();
        s_wifi_started = false;
    }
    if (s_wifi_inited) {
        esp_wifi_deinit();
        s_wifi_inited = false;
    }
    if (s_sta_netif) {
        esp_netif_destroy_default_wifi(s_sta_netif);
        s_sta_netif = NULL;
    }
    if (s_ap_netif) {
        esp_netif_destroy_default_wifi(s_ap_netif);
        s_ap_netif = NULL;
    }
}

// 连接 STA 并等 IP。返回是否拿到地址。
static bool sta_connect(void)
{
    wifi_config_t sta = { 0 };
    // memcpy + strnlen：目标字段是定长 32/64 字节，snprintf 会被判可能截断。
    memcpy(sta.sta.ssid, s_cred.ssid, strnlen(s_cred.ssid, sizeof sta.sta.ssid - 1));
    memcpy(sta.sta.password, s_cred.password,
           strnlen(s_cred.password, sizeof sta.sta.password - 1));
    sta.sta.threshold.authmode = WIFI_AUTH_OPEN;
    if (esp_wifi_set_config(WIFI_IF_STA, &sta) != ESP_OK) return false;
    if (esp_wifi_connect() != ESP_OK) return false;

    for (int waited = 0; waited < CONNECT_TIMEOUT_MS; waited += 200) {
        vTaskDelay(pdMS_TO_TICKS(200));
        esp_netif_ip_info_t ip;
        if (esp_netif_get_ip_info(s_sta_netif, &ip) == ESP_OK && ip.ip.addr != 0) {
            snprintf(s_ip, sizeof s_ip, IPSTR, IP2STR(&ip.ip));
            return true;
        }
    }
    esp_wifi_disconnect();
    return false;
}


// ------------------------------------------------------------------ 联网与文件读写

#define STORE_PATH "/store"
#define STORE_FILE STORE_PATH "/wifi_test.txt"

static bool s_spiffs_mounted;

static bool spiffs_mount(void)
{
    if (s_spiffs_mounted) return true;
    esp_vfs_spiffs_conf_t conf = {
        .base_path = STORE_PATH,
        .partition_label = "storage",
        .max_files = 2,
        .format_if_mount_failed = true,
    };
    if (esp_vfs_spiffs_register(&conf) != ESP_OK) {
        ESP_LOGE(TAG, "SPIFFS 挂载失败");
        return false;
    }
    s_spiffs_mounted = true;
    return true;
}

static void spiffs_unmount(void)
{
    if (!s_spiffs_mounted) return;
    esp_vfs_spiffs_unregister("storage");
    s_spiffs_mounted = false;
}

// 一次真实 HTTP 请求：取 204 空响应，验证 DNS、路由与出站 TCP 都通。
static int http_probe(void)
{
    esp_http_client_config_t cfg = {
        .url = "http://connectivitycheck.gstatic.com/generate_204",
        .timeout_ms = 8000,
    };
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) return -1;
    const esp_err_t err = esp_http_client_perform(client);
    const int status = (err == ESP_OK) ? esp_http_client_get_status_code(client) : -1;
    esp_http_client_cleanup(client);
    ESP_LOGI(TAG, "HTTP 探测: err=%s status=%d", esp_err_to_name(err), status);
    return status;
}

// 写一个文件再读回来比对，验证 SPIFFS 真的能落盘。
static bool file_roundtrip(const char *ip, int http_status)
{
    if (!spiffs_mount()) return false;

    char content[128];
    snprintf(content, sizeof content, "ssid=%s ip=%s http=%d", s_cred.ssid, ip,
             http_status);

    FILE *f = fopen(STORE_FILE, "w");
    if (!f) {
        ESP_LOGE(TAG, "打开写入失败: %s", STORE_FILE);
        return false;
    }
    const size_t written = fwrite(content, 1, strlen(content), f);
    fclose(f);
    if (written != strlen(content)) {
        ESP_LOGE(TAG, "写入长度不符: %u/%u", (unsigned)written,
                 (unsigned)strlen(content));
        return false;
    }

    char readback[128] = { 0 };
    f = fopen(STORE_FILE, "r");
    if (!f) return false;
    const size_t got = fread(readback, 1, sizeof readback - 1, f);
    fclose(f);
    readback[got] = 0;

    const bool ok = got == written && strcmp(readback, content) == 0;
    ESP_LOGI(TAG, "文件回读: %s (%u 字节)", ok ? "一致" : "不一致", (unsigned)got);
    return ok;
}

// ------------------------------------------------------------------ 连接任务

static void connect_task(void *arg)
{
    (void)arg;
    for (;;) {
        if (s_connect_requested && s_active) {
            s_connect_requested = false;
            if (bsp_lvgl_lock(300)) {
                s_state = SETUP_CONNECTING;
                refresh();
                bsp_lvgl_unlock();
            }

            const bool ok = sta_connect();
            if (ok) {
                cred_save();
                const int http_status = http_probe();
                const bool file_ok = file_roundtrip(s_ip, http_status);

                char detail[40];
                snprintf(detail, sizeof detail, "%s %d %s", s_ip, http_status,
                         file_ok ? PQ_TXT_PASS : PQ_TXT_FAIL);
                quest_record_wifi_result(http_status > 0 && file_ok, detail);
                ESP_LOGI(TAG, "已连接 %s, IP=%s, HTTP=%d, 文件=%s", s_cred.ssid, s_ip,
                         http_status, file_ok ? "一致" : "失败");

                if (bsp_lvgl_lock(300)) {
                    if (s_detail) {
                        lv_label_set_text_fmt(s_detail, "IP %s\nHTTP %d\n%s %s",
                                              s_ip, http_status, PQ_TXT_FILE_STORE,
                                              file_ok ? PQ_TXT_PASS : PQ_TXT_FAIL);
                    }
                    bsp_lvgl_unlock();
                }
            } else {
                quest_record_wifi_result(false, PQ_TXT_CONNECT_FAILED);
                ESP_LOGW(TAG, "连接 %s 失败（或超时）", s_cred.ssid);
            }
            if (bsp_lvgl_lock(300)) {
                s_state = ok ? SETUP_CONNECTED : SETUP_FAILED;
                refresh();
                bsp_lvgl_unlock();
            }
        }
        if (!s_active) break;
        vTaskDelay(pdMS_TO_TICKS(200));
    }
    s_task = NULL;
    vTaskDelete(NULL);
}

// ------------------------------------------------------------------ 页面生命周期

void wifi_setup_open(void)
{
    s_active = true;
    s_state = SETUP_IDLE;
    s_connect_requested = false;
    s_ip[0] = 0;
    s_ap_count = 0;
    cred_load();

    s_page = pq_ui_page_create(PQ_TXT_WIFI_SETUP);

    s_status = pq_ui_text(s_page, "", 14, 70, &pq_font_20, PQ_COLOR_INK);
    lv_obj_set_width(s_status, 212);
    lv_label_set_long_mode(s_status, LV_LABEL_LONG_WRAP);

    s_detail = pq_ui_text(s_page, "", 14, 140, &pq_font_16, PQ_COLOR_MUTED);
    lv_obj_set_width(s_detail, 212);
    lv_label_set_long_mode(s_detail, LV_LABEL_LONG_WRAP);

    s_hint = pq_ui_text(s_page, PQ_TXT_BACK_HINT, 0, 286, &pq_font_16, PQ_COLOR_MUTED);
    lv_obj_set_width(s_hint, LV_PCT(100));
    lv_obj_set_style_text_align(s_hint, LV_TEXT_ALIGN_CENTER, 0);

    refresh();

    if (!wifi_stack_up()) {
        s_state = SETUP_FAILED;
        refresh();
        return;
    }
    http_start();
    if (!s_task) {
        xTaskCreate(connect_task, "wifi_setup", 4096, NULL, 4, &s_task);
    }
}

bool wifi_setup_is_active(void)
{
    return s_active;
}

static void close_to_home(void)
{
    s_active = false;
    vTaskDelay(pdMS_TO_TICKS(250));   // 让连接任务看到 s_active 并自己退出
    spiffs_unmount();
    wifi_stack_down();
    pq_ui_page_delete(&s_page);
    s_status = s_detail = s_hint = NULL;
    app_home_open();
}

void wifi_setup_handle_key(bsp_btn_t btn, bsp_btn_ev_t ev)
{
    if (!s_active) return;

    if (ev == BSP_BTN_LONG && btn == BSP_BTN_OK) {
        if (!bsp_lvgl_lock(300)) return;
        close_to_home();
        bsp_lvgl_unlock();
        return;
    }

}
