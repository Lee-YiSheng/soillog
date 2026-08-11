idf_component_register(SRCS "main.c"
                       INCLUDE_DIRS "."
                       REQUIRES driver esp_hw_support bt esp_adc spiffs nvs_flash)

# idf_component_register(
#     # --- UNCOMMENT ONLY ONE TARGET SOURCE FILE ---
#     # SRCS "main.c"
#     SRCS "calibration_main.c"
    
#     INCLUDE_DIRS "."
#     REQUIRES esp_adc esp_timer driver
# )