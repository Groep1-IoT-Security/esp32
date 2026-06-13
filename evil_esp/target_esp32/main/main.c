/**
 * ╔══════════════════════════════════════════════════════════════╗
 * ║  Target ESP32 — Victim Weather Station (WeMol)               ║
 * ║  Doel: Simuleert het doelwit weerstation apparaat            ║
 * ║  Functie: Verbindt met WiFi en doet automatisch OTA updates  ║
 * ╚══════════════════════════════════════════════════════════════╝
 *
 * Dit is een simulatie van de "WeMol weerstation" firmware die kwetsbaar is
 * voor de Evil Twin + Malicious OTA aanval.
 *
 * BELANGRIJK: Deze code moet geïmplementeerd worden in de daadwerkelijke
 * weerstation code om de aanval te kunnen demonstreren.
 *
 * Kwetsbaarheden:
 *   - Verbindt automatisch met open WiFi netwerk (geen verificatie)
 *   - Gebruikt onbeveiligde HTTP voor OTA updates (geen HTTPS)
 *   - Valideert firmware handtekeningen niet
 *   - Vertrouwt DNS responses zonder verificatie
 *
 * De aanval werkt als volgt:
 *   1. Evil Twin ESP32 bootst "WEMOL-network" na (rogue AP)
 *   2. Dit apparaat verbindt met de nep AP
 *   3. DNS sinkhole leidt firmware.update.weerstation.com naar 192.168.1.1
 *   4. OTA update downloadt malafide firmware van de Evil Twin
 *   5. Apparaat wordt gecompromitteerd
 */

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "esp_log.h"
#include "esp_http_client.h"
#include "esp_https_ota.h"

static const char *TAG = "WEMOL_VICTIM";

// De inloggegevens van het "echte" netwerk dat het apparaat verwacht
#define WIFI_SSID "WEMOL-network" // SSID van het te spoofen netwerk
#define WIFI_PASS "" // Open netwerk voor dit practicum

// Het domein waar het apparaat normaal zijn updates haalt (onbeveiligd HTTP!)
#define OTA_URL "http://firmware.update.weerstation.com/firmware.bin"

// Wi-Fi event handler
static void wifi_event_handler(void* arg, esp_event_base_t base, int32_t id, void* data) {
    if (id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (id == WIFI_EVENT_STA_DISCONNECTED) {
        ESP_LOGI(TAG, "Verbinding verbroken, probeer opnieuw...");
        vTaskDelay(pdMS_TO_TICKS(5000));
        esp_wifi_connect();
        } else if (id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) data;
        ESP_LOGI(TAG, "Verbonden! IP Adres: " IPSTR, IP2STR(&event->ip_info.ip));

        // FORCEER de DNS server naar de Gateway (Evil Twin) om LWIP DHCP bugs te omzeilen
        esp_netif_dns_info_t dns;
        dns.ip.u_addr.ip4.addr = event->ip_info.gw.addr;
        dns.ip.type = IPADDR_TYPE_V4;
        esp_netif_set_dns_info(event->esp_netif, ESP_NETIF_DNS_MAIN, &dns);
        ESP_LOGI(TAG, "DNS Geforceerd naar: " IPSTR, IP2STR(&event->ip_info.gw));
    }
}

// De functie die checkt of er een update is (De kwetsbaarheid!)
void check_for_updates() {
    ESP_LOGI(TAG, "Zoeken naar updates op: %s", OTA_URL);

        esp_http_client_config_t config = {
        .url = OTA_URL,
        // KWETSBAARHEID: We controleren geen certificaten en gebruiken HTTP.
        // Als een Evil Twin zich voordoet als deze server, accepteren we alles.
        .is_async = false, // Forceer synchrone verbinding (gebruikt onze Evil Twin DNS)
        .keep_alive_enable = false,
    };

    esp_https_ota_config_t ota_config = {
        .http_config = &config,
    };

    ESP_LOGI(TAG, "Update downloaden en flashen...");
    esp_err_t ret = esp_https_ota(&ota_config);
    
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "OTA Succesvol! Apparaat herstarten...");
        esp_restart();
    } else {
        ESP_LOGE(TAG, "OTA Mislukt. Code: %d", ret);
    }
}

void app_main(void) {
    // 1. Initialiseer NVS en Netwerk
    nvs_flash_init();
    esp_netif_init();
    esp_event_loop_create_default();
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);

    esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL);
    esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, NULL);

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASS,
        },
    };
    
    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    esp_wifi_start();

    // 2. Wacht even tot Wi-Fi verbonden is
    vTaskDelay(pdMS_TO_TICKS(10000));

    // 3. Simuleer een dagelijkse check voor firmware updates
    while(1) {
        check_for_updates();
        vTaskDelay(pdMS_TO_TICKS(30000)); // Probeer elke 30 seconden in dit practicum
    }
}