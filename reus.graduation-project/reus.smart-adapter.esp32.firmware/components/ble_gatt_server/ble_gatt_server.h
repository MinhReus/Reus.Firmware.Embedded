#ifndef BLE_GATT_SERVER_H
#define BLE_GATT_SERVER_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Khởi tạo BLE GATT Server
 * 
 * @param device_name Tên thiết bị BLE (tối đa 29 ký tự)
 * @return esp_err_t ESP_OK nếu thành công
 */
esp_err_t ble_gatt_server_init(const char *device_name);

/**
 * @brief Gửi dữ liệu float qua BLE notify
 * 
 * @param value Giá trị float cần gửi
 * @return esp_err_t ESP_OK nếu thành công
 */
esp_err_t ble_gatt_send_float(float value);

/**
 * @brief Gửi dữ liệu string qua BLE notify
 * 
 * @param str Chuỗi cần gửi (tối đa 512 ký tự)
 * @return esp_err_t ESP_OK nếu thành công
 */
esp_err_t ble_gatt_send_string(const char *str);

/**
 * @brief Gửi dữ liệu int qua BLE notify
 * 
 * @param value Giá trị int cần gửi
 * @return esp_err_t ESP_OK nếu thành công
 */
esp_err_t ble_gatt_send_int(int32_t value);

/**
 * @brief Kiểm tra xem có client đang kết nối không
 * 
 * @return true nếu có client kết nối
 */
bool ble_gatt_is_connected(void);

/**
 * @brief Kiểm tra xem notification đã được enable chưa
 * 
 * @return true nếu notification đã được enable
 */
bool ble_gatt_is_notify_enabled(void);

/**
 * @brief Callback khi nhận dữ liệu từ client
 * 
 * @param data Con trỏ tới dữ liệu nhận được
 * @param len Độ dài dữ liệu
 */
typedef void (*ble_receive_callback_t)(uint8_t *data, uint16_t len);

/**
 * @brief Đăng ký callback để xử lý dữ liệu nhận từ client
 * 
 * @param callback Hàm callback sẽ được gọi khi nhận dữ liệu
 */
void ble_gatt_register_receive_callback(ble_receive_callback_t callback);

#ifdef __cplusplus
}
#endif

#endif // BLE_GATT_SERVER_H