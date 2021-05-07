/* Quick meshnet provides BLE presence detection from many ESP32 units to a MQTT
 * server like Mousquitto via the root node.
 * 
 */

#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include <stdio.h>
// MDF
#include "mdf_common.h"
#include "mesh_mqtt_handle.h"
#include "mwifi.h"
// NVS
#include "nvs.h"
#include "nvs_flash.h"
// BLE
#include "esp_bt.h"
#include "esp_bt_defs.h"
#include "esp_gap_ble_api.h"
#include "esp_bt_main.h"
// OSI
#include "osi/hash_map.h"
#include "osi/hash_functions.h"
#include "osi/mutex.h"
// LOG
#include "esp_log.h"

#define MEMORY_DEBUG
#define QUICK_TAG "QUICK_TAG"

#define BDA_RSSI_BUCKETS 128
#define DELTA_RSSI_MIN 15

static const char *TAG = "mqtt_examples";
esp_netif_t *sta_netif;

void root_write_task(void *arg)
{
    mdf_err_t ret = MDF_OK;
    char *data = NULL;
    size_t size = MWIFI_PAYLOAD_LEN;
    uint8_t src_addr[MWIFI_ADDR_LEN] = { 0x0 };
    mwifi_data_type_t data_type = { 0x0 };

    MDF_LOGI("Root write task is running");

    while (esp_mesh_is_root()) {
        if (!mwifi_get_root_status()) {
            vTaskDelay(500 / portTICK_RATE_MS);
            continue;
        }

        /**
         * @brief Recv data from node, and forward to mqtt server.
         */
        ret = mwifi_root_read(src_addr, &data_type, &data, &size, portMAX_DELAY);
        MDF_ERROR_GOTO(ret != MDF_OK, MEM_FREE, "<%s> mwifi_root_read", mdf_err_to_name(ret));

        ret = mesh_mqtt_write(src_addr, data, size, MESH_MQTT_DATA_JSON);

        MDF_ERROR_GOTO(ret != MDF_OK, MEM_FREE, "<%s> mesh_mqtt_publish", mdf_err_to_name(ret));

MEM_FREE:
        MDF_FREE(data);
    }

    MDF_LOGW("Root write task is exit");
    mesh_mqtt_stop();
    vTaskDelete(NULL);
}

void root_read_task(void *arg)
{
    mdf_err_t ret = MDF_OK;

    MDF_LOGI("Root read task is running");

    while (esp_mesh_is_root()) {
        if (!mwifi_get_root_status()) {
            vTaskDelay(500 / portTICK_RATE_MS);
            continue;
        }

        mesh_mqtt_data_t *request = NULL;
        mwifi_data_type_t data_type = { 0x0 };

        /**
         * @brief Recv data from mqtt data queue, and forward to special device.
         */
        ret = mesh_mqtt_read(&request, pdMS_TO_TICKS(500));

        if (ret != MDF_OK) {
            continue;
        }

        ret = mwifi_root_write(request->addrs_list, request->addrs_num, &data_type, request->data, request->size, true);
        MDF_ERROR_GOTO(ret != MDF_OK, MEM_FREE, "<%s> mwifi_root_write", mdf_err_to_name(ret));

MEM_FREE:
        MDF_FREE(request->addrs_list);
        MDF_FREE(request->data);
        MDF_FREE(request);
    }

    MDF_LOGW("Root read task is exit");
    mesh_mqtt_stop();
    vTaskDelete(NULL);
}

static void node_read_task(void *arg)
{
    mdf_err_t ret = MDF_OK;
    char *data = MDF_MALLOC(MWIFI_PAYLOAD_LEN);
    size_t size = MWIFI_PAYLOAD_LEN;
    mwifi_data_type_t data_type = { 0x0 };
    uint8_t src_addr[MWIFI_ADDR_LEN] = { 0x0 };

    MDF_LOGI("Node read task is running");

    for (;;) {
        if (!mwifi_is_connected()) {
            vTaskDelay(500 / portTICK_RATE_MS);
            continue;
        }

        size = MWIFI_PAYLOAD_LEN;
        memset(data, 0, MWIFI_PAYLOAD_LEN);
        ret = mwifi_read(src_addr, &data_type, data, &size, portMAX_DELAY);
        MDF_ERROR_CONTINUE(ret != MDF_OK, "<%s> mwifi_read", mdf_err_to_name(ret));
        MDF_LOGI("Node receive: " MACSTR ", size: %d, data: %s", MAC2STR(src_addr), size, data);
    }

    MDF_LOGW("Node read task is exit");
    MDF_FREE(data);
    vTaskDelete(NULL);
}

static mdf_err_t wifi_init()
{
    mdf_err_t ret = nvs_flash_init();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();

    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        MDF_ERROR_ASSERT(nvs_flash_erase());
        ret = nvs_flash_init();
    }

    MDF_ERROR_ASSERT(ret);

    MDF_ERROR_ASSERT(esp_netif_init());
    MDF_ERROR_ASSERT(esp_event_loop_create_default());
    ESP_ERROR_CHECK(esp_netif_create_default_wifi_mesh_netifs(&sta_netif, NULL));
    MDF_ERROR_ASSERT(esp_wifi_init(&cfg));
    MDF_ERROR_ASSERT(esp_wifi_set_storage(WIFI_STORAGE_FLASH));
    MDF_ERROR_ASSERT(esp_wifi_set_mode(WIFI_MODE_STA));
    MDF_ERROR_ASSERT(esp_wifi_set_ps(WIFI_PS_MIN_MODEM));
//    MDF_ERROR_ASSERT(esp_wifi_set_ps(WIFI_PS_NONE));
    MDF_ERROR_ASSERT(esp_mesh_set_6m_rate(false));
    MDF_ERROR_ASSERT(esp_wifi_start());

    return MDF_OK;
}

/**
 * @brief All module events will be sent to this task in esp-mdf
 *
 * @Note:
 *     1. Do not block or lengthy operations in the callback function.
 *     2. Do not consume a lot of memory in the callback function.
 *        The task memory of the callback function is only 4KB.
 */
static mdf_err_t event_loop_cb(mdf_event_loop_t event, void *ctx)
{
    MDF_LOGI("event_loop_cb, event: %d", event);

    switch (event) {
        case MDF_EVENT_MWIFI_STARTED:
            MDF_LOGI("MESH is started");
            break;

        case MDF_EVENT_MWIFI_PARENT_CONNECTED:
            MDF_LOGI("Parent is connected on station interface");

            if (esp_mesh_is_root()) {
                esp_netif_dhcpc_start(sta_netif);
            }

            break;

        case MDF_EVENT_MWIFI_PARENT_DISCONNECTED:
            MDF_LOGI("Parent is disconnected on station interface");

            if (esp_mesh_is_root() && mesh_mqtt_is_connect()) {
                mesh_mqtt_stop();
            }

            break;

        case MDF_EVENT_MWIFI_ROUTING_TABLE_ADD:
        case MDF_EVENT_MWIFI_ROUTING_TABLE_REMOVE:
            MDF_LOGI("MDF_EVENT_MWIFI_ROUTING_TABLE_REMOVE, total_num: %d", esp_mesh_get_total_node_num());

            if (esp_mesh_is_root() && mwifi_get_root_status()) {
                mdf_err_t err = mesh_mqtt_update_topo();

                if (err != MDF_OK) {
                    MDF_LOGE("Update topo failed");
                }
            }

            break;

        case MDF_EVENT_MWIFI_ROOT_GOT_IP: {
            MDF_LOGI("Root obtains the IP address. It is posted by LwIP stack automatically");

            mesh_mqtt_start(CONFIG_MQTT_URL);

            xTaskCreate(root_write_task, "root_write", 4 * 1024,
                        NULL, CONFIG_MDF_TASK_DEFAULT_PRIOTY, NULL);
            xTaskCreate(root_read_task, "root_read", 4 * 1024,
                        NULL, CONFIG_MDF_TASK_DEFAULT_PRIOTY, NULL);

            break;
        }

        case MDF_EVENT_CUSTOM_MQTT_CONNECTED:
            MDF_LOGI("MQTT connect");
            mdf_err_t err = mesh_mqtt_subscribe();
            if (err != MDF_OK) {
                MDF_LOGE("Subscribe failed");
            }
            err = mesh_mqtt_update_topo();
            if (err != MDF_OK) {
                MDF_LOGE("Update topo failed");
            }
            
            mwifi_post_root_status(true);
            break;

        case MDF_EVENT_CUSTOM_MQTT_DISCONNECTED:
            MDF_LOGI("MQTT disconnected");
            mwifi_post_root_status(false);
            break;

        default:
            break;
    }

    return MDF_OK;
}

static const char remote_device_name[] = "QUICK_PRESENCE_DETECTOR";

static esp_ble_scan_params_t ble_scan_params = {
    .scan_type              = BLE_SCAN_TYPE_ACTIVE,
    .own_addr_type          = BLE_ADDR_TYPE_PUBLIC,
    .scan_filter_policy     = BLE_SCAN_FILTER_ALLOW_ALL,
    .scan_interval          = 0x50,
    .scan_window            = 0x30,
    .scan_duplicate         = BLE_SCAN_DUPLICATE_DISABLE
};

static hash_index_t bda_hash_function(const void *key) {
    return *(hash_index_t*)key;
}

static void bda_key_fn(void *data) {
    free((esp_bd_addr_t*)data);
}

static bool bda_key_equality(const void *x, const void *y)
{
    return memcmp(x,y,sizeof(esp_bd_addr_t)) == 0;
}

hash_map_t *bda_rssi_hash_map;
osi_mutex_t bda_rssi_lock;

static void esp_gap_cb(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param)
{
    uint8_t *adv_name = NULL;
    uint8_t adv_name_len = 0;
    switch (event) {
        case ESP_GAP_BLE_SCAN_PARAM_SET_COMPLETE_EVT: {
            esp_ble_gap_start_scanning(0);
            break;
        }
        case ESP_GAP_BLE_SCAN_START_COMPLETE_EVT:
            //scan start complete event to indicate scan start successfully or failed
            if (param->scan_start_cmpl.status != ESP_BT_STATUS_SUCCESS) {
                ESP_LOGE(QUICK_TAG, "scan start failed, error status = %x", param->scan_start_cmpl.status);
                break;
            }
            ESP_LOGI(QUICK_TAG, "scan start success");

            break;
        case ESP_GAP_BLE_SCAN_RESULT_EVT: {
            esp_ble_gap_cb_param_t *scan_result = (esp_ble_gap_cb_param_t*)param;
            switch (scan_result->scan_rst.search_evt) {
                case ESP_GAP_SEARCH_INQ_RES_EVT: ;
                    adv_name = esp_ble_resolve_adv_data(scan_result->scan_rst.ble_adv, ESP_BLE_AD_TYPE_NAME_CMPL, &adv_name_len);                    

                    esp_bd_addr_t *bda = memcpy(
                        malloc(sizeof(esp_bd_addr_t)),
                        scan_result->scan_rst.bda,
                        sizeof(esp_bd_addr_t)
                    );

                    int old_rssi = (int)hash_map_get(bda_rssi_hash_map, bda), new_rssi = scan_result->scan_rst.rssi;

                    if(DELTA_RSSI_MIN <= abs(new_rssi - old_rssi)) {
                        #if CONFIG_EXAMPLE_DUMP_ADV_DATA_AND_SCAN_RESP
                        if (scan_result->scan_rst.adv_data_len > 0) {
                            ESP_LOGI(QUICK_TAG, "adv data:");
                            esp_log_buffer_hex(QUICK_TAG, &scan_result->scan_rst.ble_adv[0], scan_result->scan_rst.adv_data_len);
                        }
                        if (scan_result->scan_rst.scan_rsp_len > 0) {
                            ESP_LOGI(QUICK_TAG, "scan resp:");
                            esp_log_buffer_hex(QUICK_TAG, &scan_result->scan_rst.ble_adv[scan_result->scan_rst.adv_data_len], scan_result->scan_rst.scan_rsp_len);
                        }
                        #endif

                        if(!hash_map_set(bda_rssi_hash_map, bda, (void*)new_rssi)) {
                            hash_map_clear(bda_rssi_hash_map);
                            ESP_LOGI(QUICK_TAG, "%d buckets full, map cleared", BDA_RSSI_BUCKETS);
                        }

                        esp_log_buffer_hex(QUICK_TAG, bda, 6);
                        ESP_LOGI(QUICK_TAG, "searched Adv Data Len %d, Scan Response Len %d", scan_result->scan_rst.adv_data_len, scan_result->scan_rst.scan_rsp_len);
                        ESP_LOGI(QUICK_TAG, "searched Device Name Len %d", adv_name_len);
                        esp_log_buffer_char(QUICK_TAG, adv_name, adv_name_len);
                        ESP_LOGI(QUICK_TAG, "old rssi %d", old_rssi);
                        ESP_LOGI(QUICK_TAG, "new rssi %d", new_rssi);

                        if (adv_name != NULL) {
                            if (strlen(remote_device_name) == adv_name_len && strncmp((char *)adv_name, remote_device_name, adv_name_len) == 0) {
                                ESP_LOGI(QUICK_TAG, "searched device %s\n", remote_device_name);
                            }
                        }

                        ESP_LOGI(QUICK_TAG, "\n");

                        if (mwifi_is_connected() && mwifi_get_root_status()) {
                                mdf_err_t ret = MDF_OK;
                                size_t size = 0;
                                char *data = NULL;
                                mwifi_data_type_t data_type = { 0x0 };
                                uint8_t sta_mac[MWIFI_ADDR_LEN] = { 0 };
                                mesh_addr_t parent_mac = { 0 };

                                MDF_LOGI("Node task is running");

                                esp_wifi_get_mac(ESP_IF_WIFI_STA, sta_mac);

                                unsigned int bda_mac[] = { (*bda)[0], (*bda)[1], (*bda)[2], (*bda)[3], (*bda)[4], (*bda)[5] };
                                /**
                                 * @brief Send device information to mqtt server throught root node.
                                 */
                                esp_mesh_get_parent_bssid(&parent_mac);
                                size = asprintf(
                                    &data,
                                    "{\"type\":\"delta\", \"observer\": \"%02x%02x%02x%02x%02x%02x\", \"broadcaster\":\"%02x%02x%02x%02x%02x%02x\",\"rssi\":%d}",
                                    MAC2STR(sta_mac), MAC2STR(bda_mac), new_rssi
                                );

                                MDF_LOGD("Node send, size: %d, data: %s", size, data);
                                ret = mwifi_write(NULL, &data_type, data, size, true);
                                MDF_FREE(data);
                                if(ret != MDF_OK) {
                                    ESP_LOGI(QUICK_TAG, "<%s> mwifi_write", mdf_err_to_name(ret));
                                }
//                                MDF_ERROR_CONTINUE(ret != MDF_OK, "<%s> mwifi_write", mdf_err_to_name(ret));
                        }
                    }

                    break;
                case ESP_GAP_SEARCH_INQ_CMPL_EVT:
                    break;
                default:
                    break;
            }
            break;
        }

        case ESP_GAP_BLE_SCAN_STOP_COMPLETE_EVT:
            if (param->scan_stop_cmpl.status != ESP_BT_STATUS_SUCCESS){
                ESP_LOGE(QUICK_TAG, "scan stop failed, error status = %x", param->scan_stop_cmpl.status);
                break;
            }
            ESP_LOGI(QUICK_TAG, "stop scan successfully");
            break;

        case ESP_GAP_BLE_ADV_STOP_COMPLETE_EVT:
            if (param->adv_stop_cmpl.status != ESP_BT_STATUS_SUCCESS){
                ESP_LOGE(QUICK_TAG, "adv stop failed, error status = %x", param->adv_stop_cmpl.status);
                break;
            }
            ESP_LOGI(QUICK_TAG, "stop adv successfully");
            break;
        case ESP_GAP_BLE_UPDATE_CONN_PARAMS_EVT:
            ESP_LOGI(QUICK_TAG, "update connection params status = %d, min_int = %d, max_int = %d,conn_int = %d,latency = %d, timeout = %d",
                    param->update_conn_params.status,
                    param->update_conn_params.min_int,
                    param->update_conn_params.max_int,
                    param->update_conn_params.conn_int,
                    param->update_conn_params.latency,
                    param->update_conn_params.timeout);
            break;
        default:
            break;
    }
}

void app_main(void)
{
    /**
     * @brief Set the log level for serial port printing.
     */
    esp_log_level_set("*", ESP_LOG_INFO);
    esp_log_level_set(TAG, ESP_LOG_DEBUG);
    esp_log_level_set("mesh_mqtt", ESP_LOG_DEBUG);

    // Initialize NVS.
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK( ret );

    mwifi_init_config_t cfg = MWIFI_INIT_CONFIG_DEFAULT();
    mwifi_config_t config = {
        .router_ssid = CONFIG_ROUTER_SSID,
        .router_password = CONFIG_ROUTER_PASSWORD,
        .mesh_id = CONFIG_MESH_ID,
        .mesh_password = CONFIG_MESH_PASSWORD,
    };

    /**
     * @brief Initialize wifi mesh.
     */
    MDF_ERROR_ASSERT(mdf_event_loop_init(event_loop_cb));
    MDF_ERROR_ASSERT(wifi_init());
    MDF_ERROR_ASSERT(mwifi_init(&cfg));
    MDF_ERROR_ASSERT(mwifi_set_config(&config));
    MDF_ERROR_ASSERT(mwifi_start());

    ESP_ERROR_CHECK(esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT));

    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    ret = esp_bt_controller_init(&bt_cfg);
    if (ret) {
        ESP_LOGE(QUICK_TAG, "%s initialize controller failed: %s\n", __func__, esp_err_to_name(ret));
        return;
    }

    ret = esp_bt_controller_enable(ESP_BT_MODE_BLE);
    if (ret) {
        ESP_LOGE(QUICK_TAG, "%s enable controller failed: %s\n", __func__, esp_err_to_name(ret));
        return;
    }

    ret = esp_bluedroid_init();
    if (ret) {
        ESP_LOGE(QUICK_TAG, "%s init bluetooth failed: %s\n", __func__, esp_err_to_name(ret));
        return;
    }

    ret = esp_bluedroid_enable();
    if (ret) {
        ESP_LOGE(QUICK_TAG, "%s enable bluetooth failed: %s\n", __func__, esp_err_to_name(ret));
        return;
    }

    /**
     * @brief Create node handler
     */
    xTaskCreate(node_read_task, "node_read_task", 4 * 1024, NULL, CONFIG_MDF_TASK_DEFAULT_PRIOTY, NULL);

    //register the  callback function to the gap module
    ret = esp_ble_gap_register_callback(esp_gap_cb);
    if (ret){
        ESP_LOGE(QUICK_TAG, "%s gap register failed, error code = %x\n", __func__, ret);
        return;
    }

    bda_rssi_hash_map = hash_map_new(BDA_RSSI_BUCKETS, bda_hash_function, bda_key_fn, NULL, bda_key_equality);

    esp_err_t scan_ret = esp_ble_gap_set_scan_params(&ble_scan_params);
    if (scan_ret){
        ESP_LOGE(QUICK_TAG, "set scan params error, error code = %x", scan_ret);
    }
}
