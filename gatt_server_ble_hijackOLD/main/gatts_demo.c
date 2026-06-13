/*
 * GATT server that advertises as "ESP_GATTS_DEMO",
 * exposes primary service 0x00FF with a NOTIFY characteristic 0xFF01,
 * and sends the 4-byte payload "ACS!" once per second when the client
 * has enabled notifications via the CCCD.
 *
 * Pairs with the gattc_demo client (REMOTE_SERVICE_UUID=0x00FF,
 * REMOTE_NOTIFY_CHAR_UUID=0xFF01).
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#include "nvs.h"
#include "nvs_flash.h"

#include "esp_bt.h"
#include "esp_gap_ble_api.h"
#include "esp_gatts_api.h"
#include "esp_bt_main.h"
#include "esp_gatt_common_api.h"
#include "esp_log.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

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

static void gatts_profile_event_handler(esp_gatts_cb_event_t event,
                                        esp_gatt_if_t gatts_if,
                                        esp_ble_gatts_cb_param_t *param);

static struct gatts_profile_inst gl_profile_tab[PROFILE_NUM] = {
    [PROFILE_APP_IDX] = {
        .gatts_cb = gatts_profile_event_handler,
        .gatts_if = ESP_GATT_IF_NONE,
    },
};

/* State */
static bool     is_connected     = false;
static bool     notify_enabled   = false;
static TaskHandle_t notify_task_handle = NULL;

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

void app_main(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_ERROR_CHECK(esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT));

    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_bt_controller_init(&bt_cfg));
    ESP_ERROR_CHECK(esp_bt_controller_enable(ESP_BT_MODE_BLE));

    esp_bluedroid_config_t cfg = BT_BLUEDROID_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_bluedroid_init_with_cfg(&cfg));
    ESP_ERROR_CHECK(esp_bluedroid_enable());

    ESP_ERROR_CHECK(esp_ble_gatts_register_callback(gatts_event_handler));
    ESP_ERROR_CHECK(esp_ble_gap_register_callback(gap_event_handler));
    ESP_ERROR_CHECK(esp_ble_gatts_app_register(APP_ID));

    ESP_ERROR_CHECK(esp_ble_gatt_set_local_mtu(500));

    xTaskCreate(notify_task, "notify_task", 4096, NULL, 5, &notify_task_handle);
}
