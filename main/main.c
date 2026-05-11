#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "esp_wifi.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_log.h"
#include "mqtt_client.h"
#include "esp_http_server.h" // Required for the API

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "bmp280.h"
#include <i2cdev.h>

static const char *TAG = "WEATHER_STATION";

/* --- CONFIGURATION --- */
#define WIFI_SSID      "niet voor jou"
#define WIFI_PASS      "7zh0wnqa2m"
#define BROKER_URL     "mqtt://192.168.1.181" 

#define SDA_GPIO 21
#define SCL_GPIO 22
#define I2C_MASTER_NUM I2C_NUM_0
#define AHT20_ADDR 0x38

/* --- GLOBALS --- */
static esp_mqtt_client_handle_t mqtt_client = NULL;
static float global_humidity = 0.0; 
bmp280_t bmp_dev;
i2c_dev_t aht_dev;

/* --- API HANDLER --- */
esp_err_t get_humidity_handler(httpd_req_t *req) {
    char json_response[64];
    snprintf(json_response, sizeof(json_response), "{\"humidity\": %.2f}", global_humidity);
    
    // Allow the browser to access this from a different origin (CORS)
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*"); 
    
    return httpd_resp_send(req, json_response, strlen(json_response));
}

void __attribute__((used)) __attribute__((noinline)) secret_debug_mode() {
    ESP_LOGW("DEBUG", "MODE ACTIVATED: PW BELOW!");
    ESP_LOGW("DEBUG", "ESP32{BUFF3R0V3RFL0W}");
    while(1);
}

void* exploit_target = &secret_debug_mode;

/* --- WEB SERVER SETUP --- */
void start_api_server() {
    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();

    if (httpd_start(&server, &config) == ESP_OK) {
        httpd_uri_t humidity_uri = {
            .uri      = "/api/humidity",
            .method   = HTTP_GET,
            .handler  = get_humidity_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &humidity_uri);
        ESP_LOGI(TAG, "HTTP Server started at /api/humidity");
    }
}

void update_device_tag(char *new_tag, uint16_t length) {
    char tag_buffer[16]; // Very small buffer for the lab
    ESP_LOGW(TAG, "Attempting to copy %u bytes into 16-byte buffer...", length);

    // The Culprit: memcpy doesn't check if length > 16
    memcpy(tag_buffer, new_tag, length); 

    ESP_LOGI(TAG, "Device tag updated to: %s", tag_buffer);
}

/* --- MQTT EVENT HANDLER --- */
static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data) {
    esp_mqtt_event_handle_t event = event_data;
    switch ((esp_mqtt_event_id_t)event_id) {
        case MQTT_EVENT_CONNECTED:
            // The device must subscribe to the "trap" topic
            esp_mqtt_client_subscribe(mqtt_client, "device/rename", 0);
            ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED");
            ESP_LOGI(TAG, "Subscribed to device/rename");
            break;

        case MQTT_EVENT_DATA:
            // When data arrives on our trap topic, call the vulnerable function
            if (strncmp(event->topic, "device/rename", event->topic_len) == 0) {
                update_device_tag(event->data, event->data_len);
            }
            break;
        case MQTT_EVENT_DISCONNECTED:
            ESP_LOGI(TAG, "MQTT_EVENT_DISCONNECTED");
            break;
        default:
            break;
    }
}

/* --- WIFI EVENT HANDLER --- */
static void wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        esp_wifi_connect();
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "Got IP:" IPSTR, IP2STR(&event->ip_info.ip));
    }
}

/* --- INITIALIZATION FUNCTIONS --- */
void wifi_init_sta(void) {
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, NULL));
    wifi_config_t wifi_config = { .sta = { .ssid = WIFI_SSID, .password = WIFI_PASS } };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
}

void init_sensors() {
    ESP_ERROR_CHECK(i2cdev_init());
    memset(&aht_dev, 0, sizeof(i2c_dev_t));
    aht_dev.port = I2C_MASTER_NUM;
    aht_dev.addr = AHT20_ADDR;
    aht_dev.cfg.sda_io_num = SDA_GPIO;
    aht_dev.cfg.scl_io_num = SCL_GPIO;
    aht_dev.cfg.master.clk_speed = 100000;
    ESP_ERROR_CHECK(bmp280_init_desc(&bmp_dev, BMP280_I2C_ADDRESS_1, I2C_MASTER_NUM, SDA_GPIO, SCL_GPIO));
    bmp280_params_t params;
    bmp280_init_default_params(&params);
    ESP_ERROR_CHECK(bmp280_init(&bmp_dev, &params));
}

static void mqtt_app_start(void) {
    esp_mqtt_client_config_t mqtt_cfg = { .broker.address.uri = BROKER_URL, .broker.address.port = 1883 };
    mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
    esp_mqtt_client_register_event(mqtt_client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    esp_mqtt_client_start(mqtt_client);
}

/* --- MAIN APP --- */
void app_main(void) {
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      ESP_ERROR_CHECK(nvs_flash_erase());
      ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    wifi_init_sta();
    vTaskDelay(pdMS_TO_TICKS(5000)); 
    
    mqtt_app_start();
    start_api_server(); // Start the API server after WiFi is up
    init_sensors();

    volatile int force_include = 0;
    if (force_include == 1) {
        secret_debug_mode(); 
    }

    while (1) {
        float humidity = 0.0f;
        float temperature = 0.0f;
        float p_ignore, h_ignore; 

        // Read Humidity (AHT20)
        uint8_t aht_data[6];
        uint8_t aht_cmd[] = {0xAC, 0x33, 0x00};
        if (i2c_dev_write(&aht_dev, NULL, 0, aht_cmd, 3) == ESP_OK) {
            vTaskDelay(pdMS_TO_TICKS(80));
            if (i2c_dev_read(&aht_dev, NULL, 0, aht_data, 6) == ESP_OK) {
                uint32_t hum_raw = ((uint32_t)aht_data[1] << 12) | ((uint32_t)aht_data[2] << 4) | (aht_data[3] >> 4);
                humidity = (float)hum_raw * 100 / 1048576.0;
                global_humidity = humidity;
            }
        }

        // Read Temperature (BMP280)
        if (bmp280_read_float(&bmp_dev, &temperature, &p_ignore, &h_ignore) == ESP_OK) {
            ESP_LOGI(TAG, "Readings: Temp: %.1f°C | Hum: %.1f%%", temperature, humidity);
            
            if (mqtt_client != NULL) {
                char temp_payload[16];
                snprintf(temp_payload, sizeof(temp_payload), "%.2f", temperature);
                esp_mqtt_client_publish(mqtt_client, "sensors/weather/temperature", temp_payload, 0, 1, 0);
            }
        }

        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}