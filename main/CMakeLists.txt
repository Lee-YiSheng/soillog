idf_component_register(SRCS "main.c"
                       INCLUDE_DIRS "."
                       REQUIRES driver esp_hw_support bt esp_adc spiffs nvs_flash)