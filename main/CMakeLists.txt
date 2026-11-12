idf_component_register(
    SRCS "main.c"
         "demo_navigation.c"
         "ui_pixel.c"
         "ui_pixel_math.c"
         "demo_display.c"
         "demo_button.c"
         "demo_audio.c"
         "demo_battery.c"
         "demo_radio.c"
         "demo_wifi.c"
         "demo_ble.c"
         "demo_low_power.c"
         "power.c"
         "power_state.c"
         "pq_ui.c"
         "app_home.c"
         "quest.c"
         "font_probe.c"
         "report.c"
         "focus.c"
         "wifi_setup.c"
    INCLUDE_DIRS "."
    REQUIRES bsp bt esp_event esp_hw_support esp_netif esp_timer esp_wifi nvs_flash esp_http_server esp_http_client spiffs
)

# 生成的中文点阵字体（assets/fonts/，见 tools/gen_font.py）
target_sources(${COMPONENT_LIB} PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/../assets/fonts/pq_font_16.c"
    "${CMAKE_CURRENT_LIST_DIR}/../assets/fonts/pq_font_20.c"
)
