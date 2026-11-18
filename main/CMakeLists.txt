idf_component_register(
    SRCS "main.c"
         "app_home.c"
         "power.c"
         "power_state.c"
         "pq_ui.c"
    INCLUDE_DIRS "."
    REQUIRES bsp esp_event esp_hw_support esp_timer nvs_flash
)

# 生成的中文点阵字体（assets/fonts/，见 tools/gen_font.py）
target_sources(${COMPONENT_LIB} PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/../assets/fonts/pq_font_16.c"
    "${CMAKE_CURRENT_LIST_DIR}/../assets/fonts/pq_font_20.c"
)
