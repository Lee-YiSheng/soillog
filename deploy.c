#include <stdio.h>
#include <string.h>
#include <inttypes.h> // FIX: For printing uint32_t correctly
#include <sys/unistd.h>
#include <sys/stat.h>
#include "esp_log.h"
#include "esp_err.h"
#include "esp_spiffs.h"
#include "esp_sleep.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// --- FIX: New ADC OneShot Driver ---
#include "esp_adc/adc_oneshot.h"

static const char *TAG = "CACAO_LOGGER";

// --- CONFIGURATION ---
#define SLEEP_SECONDS       10  // 1 Hour 3600, 
//#define BATCH_SIZE          24    // Flush after 24 readings
#define BATCH_SIZE 1    //for testing

#define SENSOR_POWER_PIN    GPIO_NUM_25
#define SENSOR_ADC_CHANNEL  ADC_CHANNEL_6 // GPIO 34 (ADC1 Channel 6)
#define SENSOR_SETTLE_MS        800     // tune: 200..1000ms
#define ADC_DISCARD_SAMPLES     8
#define ADC_AVG_SAMPLES         16
#define ADC_SAMPLE_DELAY_US     200     // small spacing between samples


// --- DATA STRUCTURES ---
typedef struct {
    uint32_t timestamp_hour;
    uint16_t moisture_raw;
} data_point_t;

RTC_DATA_ATTR data_point_t ram_buffer[BATCH_SIZE];
RTC_DATA_ATTR int buffer_index = 0;
RTC_DATA_ATTR uint32_t total_hours_run = 0;

// --- HELPER: SENSOR POWER ---
#include "rom/ets_sys.h"  // for ets_delay_us()

void power_sensor_on(void) {
    gpio_reset_pin(SENSOR_POWER_PIN);
    gpio_set_direction(SENSOR_POWER_PIN, GPIO_MODE_OUTPUT);
    gpio_set_level(SENSOR_POWER_PIN, 1);

    // Give the resistive divider/probe time to settle
    vTaskDelay(pdMS_TO_TICKS(SENSOR_SETTLE_MS));
}

void power_sensor_off(void) {
    // For resistive probes, keep it hard-off to reduce leakage/back-power paths
    gpio_set_direction(SENSOR_POWER_PIN, GPIO_MODE_OUTPUT);
    gpio_set_level(SENSOR_POWER_PIN, 0);
}

// --- HELPER: FILE SYSTEM ---
void dump_data_to_terminal() {
    ESP_LOGI(TAG, "--- CHECKING FOR SAVED DATA ---");
    
    esp_vfs_spiffs_conf_t conf = {
        .base_path = "/data",
        .partition_label = "storage",
        .max_files = 1,
        .format_if_mount_failed = false 
    };

    // Try to mount. If it fails, likely no data exists yet.
    if (esp_vfs_spiffs_register(&conf) != ESP_OK) {
        ESP_LOGW(TAG, "No storage found (first run?)");
        return;
    }

    FILE* f = fopen("/data/cacao_log.bin", "rb");
    if (f != NULL) {
        data_point_t record;
        int count = 0;
        // FIX: Use PRIu32 for uint32_t printing
        while (fread(&record, sizeof(data_point_t), 1, f) == 1) {
            printf("LOG #%d -> Hour: %" PRIu32 " | Moisture: %d\n", 
                   count++, record.timestamp_hour, record.moisture_raw);
        }
        fclose(f);
    }
    esp_vfs_spiffs_unregister("storage");
    ESP_LOGI(TAG, "--- END DATA CHECK ---");
}

void dump_last_record(void) {
    esp_vfs_spiffs_conf_t conf = {
        .base_path="/data",
        .partition_label="storage",
        .max_files=1,
        .format_if_mount_failed=false
    };
    if (esp_vfs_spiffs_register(&conf) != ESP_OK) return;

    FILE *f = fopen("/data/cacao_log.bin", "rb");
    if (!f) { esp_vfs_spiffs_unregister("storage"); return; }

    struct stat st;
    if (stat("/data/cacao_log.bin", &st) == 0 && st.st_size >= sizeof(data_point_t)) {
        fseek(f, st.st_size - sizeof(data_point_t), SEEK_SET);
        data_point_t r;
        if (fread(&r, sizeof(r), 1, f) == 1) {
            printf("LAST -> Hour: %" PRIu32 " Moisture: %u (bytes=%ld)\n",
                   r.timestamp_hour, r.moisture_raw, (long)st.st_size);
        }
    }
    fclose(f);
    esp_vfs_spiffs_unregister("storage");
}


void save_buffer_to_flash() {
    ESP_LOGI(TAG, "Saving daily batch to flash...");
    
    esp_vfs_spiffs_conf_t conf = {
        .base_path = "/data",
        .partition_label = "storage",
        .max_files = 1,
        .format_if_mount_failed = true
    };

    if (esp_vfs_spiffs_register(&conf) != ESP_OK) return;

    FILE* f = fopen("/data/cacao_log.bin", "ab");
    if (f != NULL) {
        fwrite(ram_buffer, sizeof(data_point_t), buffer_index, f);
        fclose(f);
    }
    esp_vfs_spiffs_unregister("storage");
    buffer_index = 0;
}

void dump_last_n_records(int n) {
    esp_vfs_spiffs_conf_t conf = {
        .base_path = "/data",
        .partition_label = "storage",
        .max_files = 1,
        .format_if_mount_failed = false
    };
    if (esp_vfs_spiffs_register(&conf) != ESP_OK) return;

    struct stat st;
    if (stat("/data/cacao_log.bin", &st) != 0 || st.st_size < (off_t)sizeof(data_point_t)) {
        printf("No log data yet\n");
        esp_vfs_spiffs_unregister("storage");
        return;
    }

    size_t rec_size = sizeof(data_point_t);
    size_t total = (size_t)(st.st_size / rec_size);
    size_t start = (n > (int)total) ? 0 : (total - (size_t)n);

    FILE *f = fopen("/data/cacao_log.bin", "rb");
    if (!f) { esp_vfs_spiffs_unregister("storage"); return; }

    fseek(f, (long)(start * rec_size), SEEK_SET);

    for (size_t i = start; i < total; i++) {
        data_point_t r;
        if (fread(&r, rec_size, 1, f) != 1) break;
        printf("LOG #%u -> Hour: %" PRIu32 " | Moisture: %u\n",
               (unsigned)i, r.timestamp_hour, r.moisture_raw);
    }

    fclose(f);
    esp_vfs_spiffs_unregister("storage");
}


int read_soil_moisture_new(void) {
    adc_oneshot_unit_handle_t adc1_handle;

    adc_oneshot_unit_init_cfg_t init_config = {
        .unit_id = ADC_UNIT_1,
        .ulp_mode = ADC_ULP_MODE_DISABLE,
    };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_config, &adc1_handle));

    adc_oneshot_chan_cfg_t config = {
        .bitwidth = ADC_BITWIDTH_DEFAULT,
        .atten = ADC_ATTEN_DB_12,
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc1_handle, SENSOR_ADC_CHANNEL, &config));

    int raw = 0;

    // 1) Throw away initial samples (stabilization)
    for (int i = 0; i < ADC_DISCARD_SAMPLES; i++) {
        ESP_ERROR_CHECK(adc_oneshot_read(adc1_handle, SENSOR_ADC_CHANNEL, &raw));
        ets_delay_us(ADC_SAMPLE_DELAY_US);
    }

    // 2) Average real samples
    int sum = 0;
    for (int i = 0; i < ADC_AVG_SAMPLES; i++) {
        ESP_ERROR_CHECK(adc_oneshot_read(adc1_handle, SENSOR_ADC_CHANNEL, &raw));
        sum += raw;
        ets_delay_us(ADC_SAMPLE_DELAY_US);
    }

    ESP_ERROR_CHECK(adc_oneshot_del_unit(adc1_handle));
    return sum / ADC_AVG_SAMPLES;
}

// --- MAIN ---
void app_main(void) {
    //dump_data_to_terminal();
    //dump_last_record();
    dump_last_n_records(10);

    total_hours_run++;

    // 1. Read Sensor
    power_sensor_on();
    int moisture = read_soil_moisture_new();
    power_sensor_off();

    // 2. Buffer Data
    ram_buffer[buffer_index].timestamp_hour = total_hours_run;
    ram_buffer[buffer_index].moisture_raw = (uint16_t)moisture;
    buffer_index++;

    // FIX: Updated printf for uint32_t
    ESP_LOGI(TAG, "Hour %" PRIu32 " | Moisture: %d | Buffer: %d/%d", 
             total_hours_run, moisture, buffer_index, BATCH_SIZE);

    // 3. Flush if Full
    if (buffer_index >= BATCH_SIZE) {
        save_buffer_to_flash();
    }

    // 4. Sleep
    gpio_hold_dis(SENSOR_POWER_PIN);
    esp_sleep_enable_timer_wakeup(SLEEP_SECONDS * 1000000ULL);
    esp_deep_sleep_start();
}