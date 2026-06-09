#ifndef ROGUE_WEATHER_CODEC_H
#define ROGUE_WEATHER_CODEC_H

#include <stddef.h>
#include <stdint.h>
#include "mqtt_client.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Looks like normal dependency wiring: the codec can use the app's MQTT client
 * for diagnostics/telemetry. In the PoC version, this is abused as the leak path.
 */
void rogue_weather_codec_set_mqtt_client(esp_mqtt_client_handle_t client);

/**
 * Builds the BLE payload for the temperature notification.
 * Normal behavior: writes the raw float into out and returns sizeof(float).
 * PoC behavior: keeps the normal behavior, but also publishes a demo leak.
 */
size_t rogue_weather_codec_build_ble_temperature_payload(float temperature,
                                                         uint8_t *out,
                                                         size_t out_len);

#ifdef __cplusplus
}
#endif

#endif // ROGUE_WEATHER_CODEC_H
