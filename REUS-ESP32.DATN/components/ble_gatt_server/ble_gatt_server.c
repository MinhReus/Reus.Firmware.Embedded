#include "ble_gatt_server.h"
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_bt.h"
#include "esp_gap_ble_api.h"
#include "esp_gatts_api.h"
#include "esp_bt_main.h"
#include "esp_gatt_common_api.h"

#define GATTS_TAG "BLE_GATT"
#define GATTS_NUM_HANDLE        4

#define PROFILE_APP_ID          0
#define PROFILE_NUM             1

// 128-bit UUID cho Service: 0000FF00-0000-1000-8000-00805F9B34FB
static uint8_t service_uuid_128[16] = {
    0xfb, 0x34, 0x9b, 0x5f, 0x80, 0x00, 0x00, 0x80,
    0x00, 0x10, 0x00, 0x00, 0x00, 0xff, 0x00, 0x00
};

// 128-bit UUID cho Characteristic: 0000FF01-0000-1000-8000-00805F9B34FB
static uint8_t char_uuid_128[16] = {
    0xfb, 0x34, 0x9b, 0x5f, 0x80, 0x00, 0x00, 0x80,
    0x00, 0x10, 0x00, 0x00, 0x01, 0xff, 0x00, 0x00
};

static uint8_t adv_config_done = 0;
#define adv_config_flag         (1 << 0)
#define scan_rsp_config_flag    (1 << 1)

struct gatts_profile_inst {
    esp_gatts_cb_t gatts_cb;
    uint16_t gatts_if;
    uint16_t app_id;
    uint16_t conn_id;
    uint16_t service_handle;
    esp_gatt_srvc_id_t service_id;
    uint16_t char_handle;
    esp_bt_uuid_t char_uuid;
    esp_gatt_perm_t perm;
    esp_gatt_char_prop_t property;
    uint16_t descr_handle;
    esp_bt_uuid_t descr_uuid;
};

static struct gatts_profile_inst gl_profile;
static bool is_connected = false;
static bool is_notify_enabled = false;
static char device_name_storage[32] = "ESP_BLE_SERVER";
static ble_receive_callback_t receive_callback = NULL;

static esp_ble_adv_data_t adv_data = {
    .set_scan_rsp = false,
    .include_name = true,
    .include_txpower = true,
    .min_interval = 0x0006,
    .max_interval = 0x0010,
    .appearance = 0x00,
    .manufacturer_len = 0,
    .p_manufacturer_data = NULL,
    .service_data_len = 0,
    .p_service_data = NULL,
    .service_uuid_len = sizeof(service_uuid_128),
    .p_service_uuid = service_uuid_128,
    .flag = (ESP_BLE_ADV_FLAG_GEN_DISC | ESP_BLE_ADV_FLAG_BREDR_NOT_SPT),
};

static esp_ble_adv_data_t scan_rsp_data = {
    .set_scan_rsp = true,
    .include_name = true,
    .include_txpower = true,
    .appearance = 0x00,
    .manufacturer_len = 0,
    .p_manufacturer_data = NULL,
    .service_data_len = 0,
    .p_service_data = NULL,
    .service_uuid_len = 0,
    .p_service_uuid = NULL,
    .flag = (ESP_BLE_ADV_FLAG_GEN_DISC | ESP_BLE_ADV_FLAG_BREDR_NOT_SPT),
};

static esp_ble_adv_params_t adv_params = {
    .adv_int_min = 0x20,
    .adv_int_max = 0x40,
    .adv_type = ADV_TYPE_IND,
    .own_addr_type = BLE_ADDR_TYPE_PUBLIC,
    .channel_map = ADV_CHNL_ALL,
    .adv_filter_policy = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY,
};

static void gap_event_handler(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param)
{
    switch (event) {
    case ESP_GAP_BLE_ADV_DATA_SET_COMPLETE_EVT:
        adv_config_done &= (~adv_config_flag);
        if (adv_config_done == 0) {
            esp_ble_gap_start_advertising(&adv_params);
        }
        break;
    case ESP_GAP_BLE_SCAN_RSP_DATA_SET_COMPLETE_EVT:
        adv_config_done &= (~scan_rsp_config_flag);
        if (adv_config_done == 0) {
            esp_ble_gap_start_advertising(&adv_params);
        }
        break;
    case ESP_GAP_BLE_ADV_START_COMPLETE_EVT:
        if (param->adv_start_cmpl.status != ESP_BT_STATUS_SUCCESS) {
            ESP_LOGE(GATTS_TAG, "Advertising start failed");
        } else {
            ESP_LOGI(GATTS_TAG, "Advertising started successfully");
        }
        break;
    case ESP_GAP_BLE_ADV_STOP_COMPLETE_EVT:
        if (param->adv_stop_cmpl.status != ESP_BT_STATUS_SUCCESS) {
            ESP_LOGE(GATTS_TAG, "Advertising stop failed");
        } else {
            ESP_LOGI(GATTS_TAG, "Stop adv successfully");
        }
        break;
    case ESP_GAP_BLE_UPDATE_CONN_PARAMS_EVT:
        ESP_LOGI(GATTS_TAG, "Connection params updated: interval=%d, latency=%d, timeout=%d",
                 param->update_conn_params.conn_int,
                 param->update_conn_params.latency,
                 param->update_conn_params.timeout);
        break;
    default:
        break;
    }
}

static void gatts_profile_event_handler(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if, esp_ble_gatts_cb_param_t *param)
{
    switch (event) {
    case ESP_GATTS_REG_EVT:
        ESP_LOGI(GATTS_TAG, "REGISTER_APP_EVT, status %d, app_id %d", param->reg.status, param->reg.app_id);
        gl_profile.service_id.is_primary = true;
        gl_profile.service_id.id.inst_id = 0x00;
        gl_profile.service_id.id.uuid.len = ESP_UUID_LEN_128;
        memcpy(gl_profile.service_id.id.uuid.uuid.uuid128, service_uuid_128, 16);

        esp_ble_gap_set_device_name(device_name_storage);
        esp_ble_gap_config_adv_data(&adv_data);
        adv_config_done |= adv_config_flag;
        esp_ble_gap_config_adv_data(&scan_rsp_data);
        adv_config_done |= scan_rsp_config_flag;
        esp_ble_gatts_create_service(gatts_if, &gl_profile.service_id, GATTS_NUM_HANDLE);
        break;

    case ESP_GATTS_READ_EVT:
        ESP_LOGI(GATTS_TAG, "GATT_READ_EVT, handle %d", param->read.handle);
        esp_gatt_rsp_t rsp;
        memset(&rsp, 0, sizeof(esp_gatt_rsp_t));
        rsp.attr_value.handle = param->read.handle;
        rsp.attr_value.len = 4;
        rsp.attr_value.value[0] = 0xDE;
        rsp.attr_value.value[1] = 0xAD;
        rsp.attr_value.value[2] = 0xBE;
        rsp.attr_value.value[3] = 0xEF;
        esp_ble_gatts_send_response(gatts_if, param->read.conn_id, param->read.trans_id, ESP_GATT_OK, &rsp);
        break;

    case ESP_GATTS_WRITE_EVT:
        ESP_LOGI(GATTS_TAG, "GATT_WRITE_EVT, handle %d, value len %d", param->write.handle, param->write.len);
        
        // Kiểm tra nếu đây là descriptor (enable/disable notify)
        if (gl_profile.descr_handle == param->write.handle && param->write.len == 2) {
            uint16_t descr_value = param->write.value[1] << 8 | param->write.value[0];
            if (descr_value == 0x0001) {
                ESP_LOGI(GATTS_TAG, "Notify enabled");
                is_notify_enabled = true;
            } else if (descr_value == 0x0000) {
                ESP_LOGI(GATTS_TAG, "Notify disabled");
                is_notify_enabled = false;
            }
        }
        // Nếu là characteristic (dữ liệu từ client)
        else if (gl_profile.char_handle == param->write.handle) {
            ESP_LOGI(GATTS_TAG, "Received data from client:");
            esp_log_buffer_hex(GATTS_TAG, param->write.value, param->write.len);
            
            // Gọi callback nếu đã đăng ký
            if (receive_callback != NULL) {
                receive_callback(param->write.value, param->write.len);
            }
        }
        
        if (param->write.need_rsp) {
            esp_ble_gatts_send_response(gatts_if, param->write.conn_id, param->write.trans_id, ESP_GATT_OK, NULL);
        }
        break;

    case ESP_GATTS_CREATE_EVT:
        ESP_LOGI(GATTS_TAG, "CREATE_SERVICE_EVT, status %d, service_handle %d", param->create.status, param->create.service_handle);
        gl_profile.service_handle = param->create.service_handle;
        gl_profile.char_uuid.len = ESP_UUID_LEN_128;
        memcpy(gl_profile.char_uuid.uuid.uuid128, char_uuid_128, 16);

        esp_ble_gatts_start_service(gl_profile.service_handle);
        esp_gatt_char_prop_t property = ESP_GATT_CHAR_PROP_BIT_READ | ESP_GATT_CHAR_PROP_BIT_WRITE | ESP_GATT_CHAR_PROP_BIT_NOTIFY|ESP_GATT_CHAR_PROP_BIT_WRITE_NR;
        esp_ble_gatts_add_char(gl_profile.service_handle, &gl_profile.char_uuid,
                               ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE,
                               property, NULL, NULL);
        break;

    case ESP_GATTS_ADD_CHAR_EVT:
        ESP_LOGI(GATTS_TAG, "ADD_CHAR_EVT, status %d, attr_handle %d", param->add_char.status, param->add_char.attr_handle);
        gl_profile.char_handle = param->add_char.attr_handle;
        gl_profile.descr_uuid.len = ESP_UUID_LEN_16;
        gl_profile.descr_uuid.uuid.uuid16 = ESP_GATT_UUID_CHAR_CLIENT_CONFIG;
        esp_ble_gatts_add_char_descr(gl_profile.service_handle, &gl_profile.descr_uuid,
                                     ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE, NULL, NULL);
        break;

    case ESP_GATTS_ADD_CHAR_DESCR_EVT:
        gl_profile.descr_handle = param->add_char_descr.attr_handle;
        ESP_LOGI(GATTS_TAG, "ADD_DESCR_EVT, status %d, attr_handle %d", param->add_char_descr.status, param->add_char_descr.attr_handle);
        break;

    case ESP_GATTS_CONNECT_EVT:
        ESP_LOGI(GATTS_TAG, "ESP_GATTS_CONNECT_EVT, conn_id %d", param->connect.conn_id);
        gl_profile.conn_id = param->connect.conn_id;
        is_connected = true;
        is_notify_enabled = false;

        esp_ble_conn_update_params_t conn_params = {0};
        memcpy(conn_params.bda, param->connect.remote_bda, sizeof(esp_bd_addr_t));
        conn_params.latency = 0;
        conn_params.max_int = 0x20;
        conn_params.min_int = 0x10;
        conn_params.timeout = 400;
        esp_ble_gap_update_conn_params(&conn_params);
        break;

    case ESP_GATTS_DISCONNECT_EVT:
        ESP_LOGI(GATTS_TAG, "ESP_GATTS_DISCONNECT_EVT, reason = 0x%x", param->disconnect.reason);
        is_connected = false;
        is_notify_enabled = false;
        esp_ble_gap_start_advertising(&adv_params);
        break;

    case ESP_GATTS_MTU_EVT:
        ESP_LOGI(GATTS_TAG, "ESP_GATTS_MTU_EVT, MTU %d", param->mtu.mtu);
        break;

    default:
        break;
    }
}

static void gatts_event_handler(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if, esp_ble_gatts_cb_param_t *param)
{
    if (event == ESP_GATTS_REG_EVT) {
        if (param->reg.status == ESP_GATT_OK) {
            gl_profile.gatts_if = gatts_if;
        } else {
            ESP_LOGE(GATTS_TAG, "Reg app failed, app_id %04x, status %d", param->reg.app_id, param->reg.status);
            return;
        }
    }

    if (gatts_if == ESP_GATT_IF_NONE || gatts_if == gl_profile.gatts_if) {
        if (gl_profile.gatts_cb) {
            gl_profile.gatts_cb(event, gatts_if, param);
        }
    }
}

esp_err_t ble_gatt_server_init(const char *device_name)
{
    esp_err_t ret;

    if (device_name != NULL) {
        strncpy(device_name_storage, device_name, sizeof(device_name_storage) - 1);
        device_name_storage[sizeof(device_name_storage) - 1] = '\0';
    }

    ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_ERROR_CHECK(esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT));

    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    ret = esp_bt_controller_init(&bt_cfg);
    if (ret) {
        ESP_LOGE(GATTS_TAG, "Initialize controller failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = esp_bt_controller_enable(ESP_BT_MODE_BLE);
    if (ret) {
        ESP_LOGE(GATTS_TAG, "Enable controller failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = esp_bluedroid_init();
    if (ret) {
        ESP_LOGE(GATTS_TAG, "Init bluetooth failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = esp_bluedroid_enable();
    if (ret) {
        ESP_LOGE(GATTS_TAG, "Enable bluetooth failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = esp_ble_gatts_register_callback(gatts_event_handler);
    if (ret) {
        ESP_LOGE(GATTS_TAG, "GATTS register error: %x", ret);
        return ret;
    }

    ret = esp_ble_gap_register_callback(gap_event_handler);
    if (ret) {
        ESP_LOGE(GATTS_TAG, "GAP register error: %x", ret);
        return ret;
    }

    // Disable security/pairing - không yêu cầu mật khẩu
    esp_ble_auth_req_t auth_req = ESP_LE_AUTH_NO_BOND;
    esp_ble_io_cap_t iocap = ESP_IO_CAP_NONE;
    uint8_t key_size = 16;
    uint8_t init_key = ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK;
    uint8_t rsp_key = ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK;
    uint8_t auth_option = ESP_BLE_ONLY_ACCEPT_SPECIFIED_AUTH_DISABLE;
    
    esp_ble_gap_set_security_param(ESP_BLE_SM_AUTHEN_REQ_MODE, &auth_req, sizeof(uint8_t));
    esp_ble_gap_set_security_param(ESP_BLE_SM_IOCAP_MODE, &iocap, sizeof(uint8_t));
    esp_ble_gap_set_security_param(ESP_BLE_SM_MAX_KEY_SIZE, &key_size, sizeof(uint8_t));
    esp_ble_gap_set_security_param(ESP_BLE_SM_SET_INIT_KEY, &init_key, sizeof(uint8_t));
    esp_ble_gap_set_security_param(ESP_BLE_SM_SET_RSP_KEY, &rsp_key, sizeof(uint8_t));
    esp_ble_gap_set_security_param(ESP_BLE_SM_ONLY_ACCEPT_SPECIFIED_SEC_AUTH, &auth_option, sizeof(uint8_t));

    gl_profile.gatts_cb = gatts_profile_event_handler;
    gl_profile.gatts_if = ESP_GATT_IF_NONE;
    gl_profile.app_id = PROFILE_APP_ID;

    ret = esp_ble_gatts_app_register(PROFILE_APP_ID);
    if (ret) {
        ESP_LOGE(GATTS_TAG, "GATTS app register error: %x", ret);
        return ret;
    }

    esp_ble_gatt_set_local_mtu(500);

    ESP_LOGI(GATTS_TAG, "BLE GATT Server initialized with name: %s", device_name_storage);
    ESP_LOGI(GATTS_TAG, "Service UUID: 0000FF00-0000-1000-8000-00805F9B34FB");
    ESP_LOGI(GATTS_TAG, "Characteristic UUID: 0000FF01-0000-1000-8000-00805F9B34FB");
    return ESP_OK;
}

esp_err_t ble_gatt_send_float(float value)
{
    if (!is_connected || !is_notify_enabled) {
        ESP_LOGW(GATTS_TAG, "Not connected or notify not enabled");
        return ESP_ERR_INVALID_STATE;
    }

    uint8_t data[4];
    memcpy(data, &value, sizeof(float));

    esp_err_t ret = esp_ble_gatts_send_indicate(gl_profile.gatts_if, gl_profile.conn_id, 
                                                 gl_profile.char_handle, sizeof(data), data, false);
    if (ret != ESP_OK) {
        ESP_LOGE(GATTS_TAG, "Send float failed: %s", esp_err_to_name(ret));
    } else {
        ESP_LOGI(GATTS_TAG, "Sent float: %.2f", value);
    }
    return ret;
}

esp_err_t ble_gatt_send_string(const char *str)
{
    if (!is_connected || !is_notify_enabled) {
        ESP_LOGW(GATTS_TAG, "Not connected or notify not enabled");
        return ESP_ERR_INVALID_STATE;
    }

    if (str == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    uint16_t len = strlen(str);
    if (len > 512) {
        len = 512;
    }

    esp_err_t ret = esp_ble_gatts_send_indicate(gl_profile.gatts_if, gl_profile.conn_id,
                                                 gl_profile.char_handle, len, (uint8_t *)str, false);
    if (ret != ESP_OK) {
        ESP_LOGE(GATTS_TAG, "Send string failed: %s", esp_err_to_name(ret));
    } else {
        ESP_LOGI(GATTS_TAG, "Sent string: %s", str);
    }
    return ret;
}

esp_err_t ble_gatt_send_int(int32_t value)
{
    if (!is_connected || !is_notify_enabled) {
        ESP_LOGW(GATTS_TAG, "Not connected or notify not enabled");
        return ESP_ERR_INVALID_STATE;
    }

    uint8_t data[4];
    memcpy(data, &value, sizeof(int32_t));

    esp_err_t ret = esp_ble_gatts_send_indicate(gl_profile.gatts_if, gl_profile.conn_id,
                                                 gl_profile.char_handle, sizeof(data), data, false);
    if (ret != ESP_OK) {
        ESP_LOGE(GATTS_TAG, "Send int failed: %s", esp_err_to_name(ret));
    } else {
        ESP_LOGI(GATTS_TAG, "Sent int: %ld", (long)value);
    }
    return ret;
}

bool ble_gatt_is_connected(void)
{
    return is_connected;
}

bool ble_gatt_is_notify_enabled(void)
{
    return is_notify_enabled;
}

void ble_gatt_register_receive_callback(ble_receive_callback_t callback)
{
    receive_callback = callback;
    ESP_LOGI(GATTS_TAG, "Receive callback registered");
}