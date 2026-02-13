#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_adc/adc_oneshot.h"

// Tag for logging to the monitor
//static const char *TAG = "SOIL_TEST";

// GPIO 34 corresponds to ADC1 Channel 6 on the ESP32
#define SENSOR_ADC_UNIT     ADC_UNIT_1
#define SENSOR_ADC_CHANNEL  ADC_CHANNEL_6

void app_main(void)
{
    // 1. Initialize the ADC Unit
    adc_oneshot_unit_handle_t adc1_handle;
    adc_oneshot_unit_init_cfg_t init_config1 = {
        .unit_id = SENSOR_ADC_UNIT,
    };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_config1, &adc1_handle));

    // 2. Configure the Channel (GPIO 34)
    // We use 11dB attenuation to allow reading voltages up to ~3.1V (safe for 3.3V sensors)
    adc_oneshot_chan_cfg_t config = {
        .bitwidth = ADC_BITWIDTH_DEFAULT,
        .atten = ADC_ATTEN_DB_12, 
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc1_handle, SENSOR_ADC_CHANNEL, &config));

    printf("--- ESP32 Soil Moisture Sensor Test (IDF) ---\n");

    while (1) {
        int raw_value = 0;
        
        // 3. Read the Raw Data
        ESP_ERROR_CHECK(adc_oneshot_read(adc1_handle, SENSOR_ADC_CHANNEL, &raw_value));

        // 4. Print to Monitor (Replaces Serial.print)
        //ESP_LOGI(TAG, "Raw Sensor Value: %d", raw_value);
printf("Raw Sensor Value: %d\n", raw_value);
        // 5. Delay for 500ms (Replaces delay(500))
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}