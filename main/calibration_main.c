/*
 * calibration_main.c
 * Dedicated hardware profiling & sensor calibration target for ESP32-C3 SuperMini.
 *
 * - Outputs 48-bit IEEE eFuse MAC Address for unit registration.
 * - Powers SENSOR_POWER_PIN (GPIO 10) continuously HIGH.
 * - Samples ADC_CHANNEL_0 (GPIO 0) every 1000ms.
 */

#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_mac.h"
#include "esp_log.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"

// --- HARDWARE PIN & CHANNEL MAPPING ---
#define SENSOR_POWER_PIN    GPIO_NUM_10
#define ADC_CHANNEL         ADC_CHANNEL_0     // GPIO 0 on ESP32-C3 (ADC1 Channel 0)
#define ADC_ATTENUATION     ADC_ATTEN_DB_12   // 12 dB atten for full 0-3300mV range (IDF v5.1+)
#define ADC_BITWIDTH        ADC_BITWIDTH_DEFAULT
#define SAMPLE_INTERVAL_MS  1000

static const char *TAG = "CALIBRATION";

/**
 * @brief Reads and prints the factory Wi-Fi Station MAC address from eFuse.
 */
static void print_device_mac_address(void)
{
    uint8_t mac[6] = {0};
    esp_err_t err = esp_read_mac(mac, ESP_MAC_WIFI_STA);
    
    if (err == ESP_OK) {
        printf("==================================================\n");
        printf(" [CACAO_LOGGER] CALIBRATION & HARDWARE PROFILING\n");
        printf(" Target MAC Address : %02x:%02x:%02x:%02x:%02x:%02x\n",
               mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
        printf("==================================================\n\n");
    } else {
        ESP_LOGE(TAG, "Failed to read eFuse MAC address: %s", esp_err_to_name(err));
    }
}

/**
 * @brief Configures GPIO 10 as a push-pull output and drives it HIGH.
 */
static void enable_sensor_power(void)
{
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << SENSOR_POWER_PIN),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&io_conf);
    gpio_set_level(SENSOR_POWER_PIN, 1);
    
    ESP_LOGI(TAG, "Sensor power enabled on GPIO %d", SENSOR_POWER_PIN);
}

/**
 * @brief Initializes ADC Oneshot Unit and attempts curve-fitting calibration.
 */
static bool init_adc_calibration(adc_unit_t unit, adc_channel_t channel, 
                                 adc_atten_t atten, adc_cali_handle_t *out_handle)
{
    adc_cali_handle_t handle = NULL;
    esp_err_t ret = ESP_FAIL;
    bool calibrated = false;

#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
    adc_cali_curve_fitting_config_t cali_config = {
        .unit_id = unit,
        .chan = channel,
        .atten = atten,
        .bitwidth = ADC_BITWIDTH,
    };
    ret = adc_cali_create_scheme_curve_fitting(&cali_config, &handle);
    if (ret == ESP_OK) {
        calibrated = true;
    }
#endif

    *out_handle = handle;
    if (!calibrated) {
        ESP_LOGW(TAG, "Hardware ADC calibration scheme not supported on this unit. Using linear fallback.");
    }
    return calibrated;
}

void app_main(void)
{
    // 1. Output hardware identifier for manifest registration
    print_device_mac_address();

    // 2. Drive probe power rail HIGH continuously during bench testing
    enable_sensor_power();

    // 3. Initialize ADC1 Oneshot Driver
    adc_oneshot_unit_handle_t adc_handle;
    adc_oneshot_unit_init_cfg_t init_config1 = {
        .unit_id = ADC_UNIT_1,
    };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_config1, &adc_handle));

    // 4. Configure ADC channel attenuation and bitwidth
    adc_oneshot_chan_cfg_t config = {
        .bitwidth = ADC_BITWIDTH,
        .atten = ADC_ATTENUATION,
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc_handle, ADC_CHANNEL, &config));

    // 5. Initialize voltage conversion calibration handle
    adc_cali_handle_t adc_cali_handle = NULL;
    bool do_calibration = init_adc_calibration(ADC_UNIT_1, ADC_CHANNEL, ADC_ATTENUATION, &adc_cali_handle);

    ESP_LOGI(TAG, "Starting 1-second continuous sampling loop...");
    printf("--------------------------------------------------\n");

    uint32_t sample_index = 1;
    int raw_adc = 0;
    int voltage_mv = 0;

    // 6. Continuous polling loop
    // 6. Continuous polling loop with 16-sample averaging
    while (1) {
        int raw_adc_sum = 0;
        int single_sample = 0;

        // Take 16 rapid bursts to average out capacitive oscillator noise
        for (int i = 0; i < 16; i++) {
            ESP_ERROR_CHECK(adc_oneshot_read(adc_handle, ADC_CHANNEL, &single_sample));
            raw_adc_sum += single_sample;
        }
        raw_adc = raw_adc_sum / 16;

        if (do_calibration) {
            ESP_ERROR_CHECK(adc_cali_raw_to_voltage(adc_cali_handle, raw_adc, &voltage_mv));
        } else {
            voltage_mv = (raw_adc * 3300) / 4095;
        }

        printf("[CALIBRATION] Sample #%-4" PRIu32 " | Raw ADC: %-4d | Voltage: %-4d mV\n",
               sample_index, raw_adc, voltage_mv);

        sample_index++;
        vTaskDelay(pdMS_TO_TICKS(SAMPLE_INTERVAL_MS));
    }
}