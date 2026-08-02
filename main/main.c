#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include <sys/unistd.h>
#include <sys/stat.h>
#include "esp_log.h"
#include "esp_err.h"
#include "esp_spiffs.h"
#include "esp_sleep.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_adc/adc_oneshot.h"
#include "rom/ets_sys.h"

// --- BLE NimBLE Headers ---
#include "esp_nimble_hci.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

static const char *TAG = "CACAO_LOGGER";

// --- CONFIGURATION ---
#define SLEEP_SECONDS          3600    // 1 Hour
#define BATCH_SIZE             24      // Flush after 24 readings
#define DEBUG_WINDOW_SECONDS   60      // BLE active window duration

#define SENSOR_POWER_PIN       GPIO_NUM_10
#define SENSOR_ADC_CHANNEL     ADC_CHANNEL_0 // GPIO 0
#define REED_SWITCH_PIN        GPIO_NUM_2    // NO Reed Switch to GND

#define SENSOR_SETTLE_MS       800
#define ADC_DISCARD_SAMPLES    8
#define ADC_AVG_SAMPLES        16
#define ADC_SAMPLE_DELAY_US    200

// --- DATA STRUCTURES ---
typedef struct {
    uint32_t timestamp_hour;
    uint16_t moisture_raw;
} __attribute__((packed)) data_point_t;

RTC_DATA_ATTR data_point_t ram_buffer[BATCH_SIZE];
RTC_DATA_ATTR int buffer_index = 0;
RTC_DATA_ATTR uint32_t total_hours_run = 0;

// ============================================================================
// 1. NIMBLE BLE GATT SERVER IMPLEMENTATION
// ============================================================================

static uint8_t ble_addr_type;
static int ble_gap_event(struct ble_gap_event *event, void *arg);

// Callback when you tap "Read" on Characteristic 0xFFF1 in nRF Connect / LightBlue
static int gatt_svr_chr_access(uint16_t conn_handle, uint16_t attr_handle,
                               struct ble_gatt_access_ctxt *ctxt, void *arg) {
    char status_str[128];
    uint16_t last_adc = (buffer_index > 0) ? ram_buffer[buffer_index - 1].moisture_raw : 0;
    
    snprintf(status_str, sizeof(status_str),
             "Node: IEx_Tree_1 | Hour: %" PRIu32 " | Last ADC: %u | Buffer: %d/%d",
             total_hours_run, last_adc, buffer_index, BATCH_SIZE);
             
    int rc = os_mbuf_append(ctxt->om, status_str, strlen(status_str));
    return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
}

static const struct ble_gatt_svc_def gatt_svr_svcs[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = BLE_UUID16_DECLARE(0xFFF0),
        .characteristics = (struct ble_gatt_chr_def[]) {
            {
                .uuid = BLE_UUID16_DECLARE(0xFFF1),
                .access_cb = gatt_svr_chr_access,
                .flags = BLE_GATT_CHR_F_READ,
            },
            { 0 },
        },
    },
    { 0 },
};

static void ble_app_advertise(void) {
    struct ble_gap_adv_params adv_params;
    struct ble_hs_adv_fields fields;
    const char *name = "IEx_Tree_1";

    memset(&fields, 0, sizeof fields);
    fields.name = (uint8_t *)name;
    fields.name_len = strlen(name);
    fields.name_is_complete = 1;
    ble_gap_adv_set_fields(&fields);

    memset(&adv_params, 0, sizeof adv_params);
    adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;
    ble_gap_adv_start(ble_addr_type, NULL, BLE_HS_FOREVER, &adv_params, ble_gap_event, NULL);
}

static int ble_gap_event(struct ble_gap_event *event, void *arg) {
    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        ESP_LOGI(TAG, "BLE Connected (Status: %d)", event->connect.status);
        if (event->connect.status != 0) {
            ble_app_advertise();
        }
        break;
    case BLE_GAP_EVENT_DISCONNECT:
        ESP_LOGI(TAG, "BLE Disconnected. Re-advertising...");
        ble_app_advertise();
        break;
    }
    return 0;
}

static void ble_on_reset(int reason) {
    ESP_LOGE(TAG, "BLE Reset; reason=%d", reason);
}

static void ble_on_sync(void) {
    ble_hs_id_infer_auto(0, &ble_addr_type);
    ble_app_advertise();
}

void ble_host_task(void *param) {
    nimble_port_run();
    nimble_port_freertos_deinit();
}

void run_ble_debug_window(void) {
    ESP_LOGW(TAG, "=== REED SWITCH TRIGGERED: Starting 60s BLE Debug Window ===");
    
    esp_nimble_hci_and_controller_init();
    nimble_port_init();
    
    ble_svc_gap_init();
    ble_svc_gatt_init();
    ble_gatts_count_cfg(gatt_svr_svcs);
    ble_gatts_add_svcs(gatt_svr_svcs);
    ble_svc_gap_device_name_set("IEx_Tree_1");
    
    ble_hs_cfg.reset_cb = ble_on_reset;
    ble_hs_cfg.sync_cb = ble_on_sync;
    
    nimble_port_freertos_init(ble_host_task);

    ESP_LOGI(TAG, "BLE Active ('IEx_Tree_1'). Connect via nRF Connect app. Sleeping in 60s...");
    vTaskDelay(pdMS_TO_TICKS(DEBUG_WINDOW_SECONDS * 1000));

    ESP_LOGW(TAG, "=== 60s window expired. Shutting down BLE... ===");
    nimble_port_stop();
    nimble_port_deinit();
    esp_nimble_hci_and_controller_deinit();
}

// ============================================================================
// 2. TIMELINE RECOVERY & STORAGE HELPERS
// ============================================================================

void recover_timeline(void) {
    if (esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_TIMER ||
        esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_GPIO) {
        return; 
    }

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
            fseek(f, 0, SEEK_END);
            long file_size = ftell(f);
            if (file_size >= sizeof(data_point_t)) {
                fseek(f, file_size - sizeof(data_point_t), SEEK_SET);
                data_point_t last_record;
                if (fread(&last_record, sizeof(data_point_t), 1, f) == 1) {
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

    printf("\n--- START CSV ---\n");
    printf("Hour_Timestamp,Raw_ADC\n");

    data_point_t record;
    while (fread(&record, sizeof(data_point_t), 1, f) == 1) {
        printf("%" PRIu32 ",%u\n", record.timestamp_hour, record.moisture_raw);
    }
    printf("--- END CSV ---\n\n");

    fclose(f);
    esp_vfs_spiffs_unregister("storage");
}

void save_buffer_to_flash(void) {
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

// ============================================================================
// 3. SENSOR READING
// ============================================================================

void power_sensor_on(void) {
    gpio_reset_pin(SENSOR_POWER_PIN);
    gpio_set_direction(SENSOR_POWER_PIN, GPIO_MODE_OUTPUT);
    gpio_set_level(SENSOR_POWER_PIN, 1);
    vTaskDelay(pdMS_TO_TICKS(SENSOR_SETTLE_MS));
}

void power_sensor_off(void) {
    gpio_set_direction(SENSOR_POWER_PIN, GPIO_MODE_OUTPUT);
    gpio_set_level(SENSOR_POWER_PIN, 0);
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
    for (int i = 0; i < ADC_DISCARD_SAMPLES; i++) {
        ESP_ERROR_CHECK(adc_oneshot_read(adc1_handle, SENSOR_ADC_CHANNEL, &raw));
        ets_delay_us(ADC_SAMPLE_DELAY_US);
    }

    int sum = 0;
    for (int i = 0; i < ADC_AVG_SAMPLES; i++) {
        ESP_ERROR_CHECK(adc_oneshot_read(adc1_handle, SENSOR_ADC_CHANNEL, &raw));
        sum += raw;
        ets_delay_us(ADC_SAMPLE_DELAY_US);
    }

    ESP_ERROR_CHECK(adc_oneshot_del_unit(adc1_handle));
    return sum / ADC_AVG_SAMPLES;
}

// ============================================================================
// 4. SLEEP HELPER
// ============================================================================

void enter_deep_sleep(void) {
    gpio_hold_dis(SENSOR_POWER_PIN);

    // 1. Configure NO Reed Switch wakeup on GPIO 2 (Pulled LOW when magnet swiped)
    gpio_reset_pin(REED_SWITCH_PIN);
    gpio_set_direction(REED_SWITCH_PIN, GPIO_MODE_INPUT);
    gpio_pullup_en(REED_SWITCH_PIN);
    gpio_pulldown_dis(REED_SWITCH_PIN);
    esp_deep_sleep_enable_gpio_wakeup(1ULL << REED_SWITCH_PIN, ESP_GPIO_WAKEUP_GPIO_LOW);

    // 2. Configure hourly timer wakeup
    esp_sleep_enable_timer_wakeup(SLEEP_SECONDS * 1000000ULL);

    ESP_LOGI(TAG, "Entering deep sleep...");
    esp_deep_sleep_start();
}

// ============================================================================
// 5. MAIN
// ============================================================================

void app_main(void) {
    recover_timeline();

    esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();

    // --- CASE A: Woken by Magnet Swipe (Reed Switch on GPIO 2) ---
    if (cause == ESP_SLEEP_WAKEUP_GPIO) {
        run_ble_debug_window();
        enter_deep_sleep();
        return; // Skip logging a new reading when only checking BLE status
    }

    // --- CASE B: Check for Wired Download Mode (BOOT Button / GPIO 9) ---
    gpio_reset_pin(GPIO_NUM_9);
    gpio_set_direction(GPIO_NUM_9, GPIO_MODE_INPUT);
    gpio_pullup_en(GPIO_NUM_9);
    
    ESP_LOGI(TAG, "Booting... 3 seconds to hold BOOT button for Wired CSV Download Mode.");
    vTaskDelay(pdMS_TO_TICKS(3000)); 

    if (gpio_get_level(GPIO_NUM_9) == 0) {
        ESP_LOGW(TAG, "*** DOWNLOAD MODE DETECTED ***");
        export_all_data_to_csv();
        ESP_LOGW(TAG, "Data export complete. Board is now paused.");
        while(1) {
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }

    // --- CASE C: Normal Logging Cycle (Hourly Timer or Power-On) ---
    total_hours_run++;

    power_sensor_on();
    int moisture = read_soil_moisture_new();
    power_sensor_off();

    ram_buffer[buffer_index].timestamp_hour = total_hours_run;
    ram_buffer[buffer_index].moisture_raw = (uint16_t)moisture;
    buffer_index++;

    ESP_LOGI(TAG, "Logged Hour %" PRIu32 " | Moisture: %d | Buffer: %d/%d", 
             total_hours_run, moisture, buffer_index, BATCH_SIZE);

    if (buffer_index >= BATCH_SIZE) {
        save_buffer_to_flash();
    }

    enter_deep_sleep();
}