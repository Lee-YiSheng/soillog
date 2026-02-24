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
#define SLEEP_SECONDS       3600 // 1 Hour 3600, 
#define BATCH_SIZE          24    // Flush after 24 readings

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
} __attribute__((packed)) data_point_t; // Forces the compiler to skip padding

RTC_DATA_ATTR data_point_t ram_buffer[BATCH_SIZE];
RTC_DATA_ATTR int buffer_index = 0;
RTC_DATA_ATTR uint32_t total_hours_run = 0;

// --- HELPER: SENSOR POWER ---
#include "rom/ets_sys.h"  // for ets_delay_us()


void recover_timeline(void) {
    // If we woke up normally from deep sleep, the RTC memory is perfectly safe. Do nothing.
    if (esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_TIMER) {
        return; 
    }

    // Otherwise, we suffered a power cut or hard reset!
    ESP_LOGW(TAG, "Power cycle detected! Recovering timeline from flash...");
    
    esp_vfs_spiffs_conf_t conf = {
        .base_path="/data",
        .partition_label="storage",
        .max_files=1,
        .format_if_mount_failed=false
    };
    
    if (esp_vfs_spiffs_register(&conf) == ESP_OK) {
        FILE *f = fopen("/data/cacao_log.bin", "rb");
        if (f) {
            // Jump to the very end of the file
            fseek(f, 0, SEEK_END);
            long file_size = ftell(f);
            
            // If the file has at least one record (6 bytes)
            if (file_size >= sizeof(data_point_t)) {
                // Step back by exactly one record size
                fseek(f, file_size - sizeof(data_point_t), SEEK_SET);
                
                data_point_t last_record;
                if (fread(&last_record, sizeof(data_point_t), 1, f) == 1) {
                    // Overwrite the wiped 0 with our last known timestamp
                    total_hours_run = last_record.timestamp_hour;
                    ESP_LOGI(TAG, "Recovered Hour: %" PRIu32, total_hours_run);
                }
            }
            fclose(f);
        }
        esp_vfs_spiffs_unregister("storage");
    }
}

void export_all_data_to_csv(void) {
    ESP_LOGI(TAG, "Starting CSV Export...");
    
    esp_vfs_spiffs_conf_t conf = {
        .base_path = "/data",
        .partition_label = "storage",
        .max_files = 1,
        .format_if_mount_failed = false
    };

    if (esp_vfs_spiffs_register(&conf) != ESP_OK) {
        printf("Failed to mount SPIFFS. No data.\n");
        return;
    }

    FILE *f = fopen("/data/cacao_log.bin", "rb");
    if (!f) { 
        printf("No log file found.\n");
        esp_vfs_spiffs_unregister("storage"); 
        return; 
    }

    // Print the CSV Header
    printf("\n--- START CSV ---\n");
    printf("Hour_Timestamp,Raw_ADC\n");

    data_point_t record;
    // Let the ESP32 strip the metadata and read the pure file
    while (fread(&record, sizeof(data_point_t), 1, f) == 1) {
        printf("%" PRIu32 ",%u\n", record.timestamp_hour, record.moisture_raw);
    }
    
    printf("--- END CSV ---\n\n");

    fclose(f);
    esp_vfs_spiffs_unregister("storage");
}

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
    //dump_last_n_records(10);
    recover_timeline();
    //export_all_data_to_csv();

// 2. Initialize the built-in BOOT button (GPIO 0)
    gpio_reset_pin(GPIO_NUM_0);
    gpio_set_direction(GPIO_NUM_0, GPIO_MODE_INPUT);
    // Give it a tiny delay to stabilize the pin reading
    vTaskDelay(pdMS_TO_TICKS(10)); 

    // 3. THE "DOWNLOAD MODE" SWITCH
    // If you are holding the BOOT button when the ESP32 turns on:
    if (gpio_get_level(GPIO_NUM_0) == 0) {
        ESP_LOGW(TAG, "*** DOWNLOAD MODE DETECTED ***");
        
        // Print the data
        export_all_data_to_csv();
        
        ESP_LOGW(TAG, "Data export complete. You can safely unplug the USB.");
        
        // Trap the ESP32 in an infinite loop so it DOES NOT take a new 
        // sensor reading and ruin your data timeline while plugged into your Mac.
        while(1) {
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }

    // ==========================================
    // 4. NORMAL LOGGING MODE (Button NOT held)
    // ==========================================
    
    total_hours_run++;

    // Read the sensor
    power_sensor_on();
    int moisture = read_soil_moisture_new();
    power_sensor_off();

    // Buffer the data
    ram_buffer[buffer_index].timestamp_hour = total_hours_run;
    ram_buffer[buffer_index].moisture_raw = (uint16_t)moisture;
    buffer_index++;

    ESP_LOGI(TAG, "Logged Hour %" PRIu32 " | Moisture: %d | Buffer: %d/%d", 
             total_hours_run, moisture, buffer_index, BATCH_SIZE);

    // Save to flash if buffer is full
    if (buffer_index >= BATCH_SIZE) {
        save_buffer_to_flash();
    }

    // Go back to Deep Sleep
    gpio_hold_dis(SENSOR_POWER_PIN);
    esp_sleep_enable_timer_wakeup(SLEEP_SECONDS * 1000000ULL);
    esp_deep_sleep_start();
}