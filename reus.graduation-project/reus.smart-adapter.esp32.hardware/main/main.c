#include "acs712.h"
#include "driver/adc.h"
#include "driver/adc_types_legacy.h"
#include "esp_task_wdt.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "hal/adc_types.h"
#include "nvs_flash.h"
#include "ble_gatt_server.h"
#include "driver/gpio.h"
#include "stdint.h"
#include "string.h"
#include "esp_timer.h"

#define LED_GPIO 2
#define RELAY 23
#define TAG "ACS712"

#define R1 51.0f
#define R2 74.5f
#define VREF 5.0f        

#define FILTER_SAMPLES 10

acs712_t sensor;


static float dc = 0;
static float volt = 0;
static float vset = 0;
static float power = 0;
static float percent = 0;
static volatile float ble_value = 0.5f;
// Flag để đảm bảo BLE đã init xong
static volatile bool ble_ready = false;
static bool relay_on = false;
static int64_t safe_start_time = 0;
static bool ble_connected_prev = false;

// Callback xử lý dữ liệu nhận từ BLE client
void on_ble_data_received(uint8_t *data, uint16_t len)
{
    ESP_LOGI(TAG, "=== Data received from client ===");
    ESP_LOGI(TAG, "Length: %d bytes", len);

    if (len == 0) return;

    char str[64];
    if (len >= sizeof(str)) len = sizeof(str) - 1;

    memcpy(str, data, len);
    str[len] = '\0';

    ESP_LOGI(TAG, "Data as string: %s", str);

    // Convert string -> float
    char *endptr;
    float value = strtof(str, &endptr);

    if (endptr == str) {
        ESP_LOGE(TAG, "Convert to float FAILED");
        return;
    }

    ble_value = value;   // lưu giá trị hợp lệ
    ESP_LOGI(TAG, "Float value = %.3f", ble_value);
}

static void relay_init(void)
{
    gpio_reset_pin(RELAY);
    gpio_set_direction(RELAY, GPIO_MODE_OUTPUT);
    gpio_set_level(RELAY, 0); // NC: trạng thái an toàn ban đầu
}


static void led_init(void)
{
    gpio_reset_pin(LED_GPIO);
    gpio_set_direction(LED_GPIO, GPIO_MODE_OUTPUT);
    gpio_set_level(LED_GPIO, 0); // tắt LED ban đầu
}

void led_blink_task(void *pvParameters)
{
    while (1)
    {
        if (!ble_gatt_is_connected()) {
            // Nhấp nháy nhanh khi chưa kết nối
            gpio_set_level(LED_GPIO, 1);
            vTaskDelay(pdMS_TO_TICKS(250));
            gpio_set_level(LED_GPIO, 0);
            vTaskDelay(pdMS_TO_TICKS(250));
        } else {
            // Sáng liên tục khi đã kết nối
            gpio_set_level(LED_GPIO, 1);
            vTaskDelay(pdMS_TO_TICKS(500));
        }
    }
}

void adc_task(void *pv)
{

    char message[64];
    
    ESP_LOGI(TAG, "ADC Task started, waiting for BLE ready...");
    
    // Chờ BLE init xong
    while (!ble_ready) {
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    
    ESP_LOGI(TAG, "BLE ready, starting ADC measurements");

    while(1)
    {
		bool ble_connected_now = ble_gatt_is_connected();

		/* Vừa bị disconnect */
		if (!ble_connected_now && ble_connected_prev) {
    	ble_value = 0.5f;   // reset về mặc định
    	ESP_LOGW(TAG, "BLE disconnected -> reset threshold to 0.5A");
	}

		/* Vừa connect */
		if (ble_connected_now && !ble_connected_prev) {
    	ESP_LOGI(TAG, "BLE connected");
	}

		ble_connected_prev = ble_connected_now;

        // Đọc raw ADC với samples ít hơn để tránh watchdog
        int raw_dc = acs712_read_raw_avg(&sensor, 1500);    // Giảm từ 200 xuống 50
        vTaskDelay(pdMS_TO_TICKS(10));  // Thêm delay nhỏ để tránh watchdog
        
        int raw = adc1_get_raw(ADC1_CHANNEL_6);
        int raw_volt = ema_filter(raw,0.2f);
        vTaskDelay(pdMS_TO_TICKS(10));  // Thêm delay nhỏ
        
        // Đọc giá trị dòng điện và điện áp
        dc = acs712_get_current_dc(&sensor);
        volt = ((raw_volt*3.3)/4095.0)*(R1+R2)/R2;
        vset = volt - 0.41f;     
        acs712_set_vcc(&sensor,vset);  
		//Xử lý relay
		int64_t now = esp_timer_get_time(); // microsecond

		if (dc > ble_value) {
    		// Quá dòng → relay ON ngay
    		gpio_set_level(RELAY, 1);
    		relay_on = true;
    		safe_start_time = 0; // reset trạng thái an toàn
    		ESP_LOGW(TAG, "RELAY IS ON");
		}
		else {
    		// Đang an toàn
    		if (relay_on) {
        	if (safe_start_time == 0) {
			ESP_LOGW(TAG, "RELAY IS OFF");
            // bắt đầu đếm thời gian an toàn
            safe_start_time = now;
        	}
        else if ((now - safe_start_time) >= 3000000) {
            // an toàn liên tục ≥ 3s → OFF relay
            gpio_set_level(RELAY, 0);
            relay_on = false;
            safe_start_time = 0;
        }
    }
}

        // Tính công suất
        power = dc * volt;
        if(volt > 5.00 && volt <5.50)
        {
			percent = 100.0;
		}
		else percent = (volt/5)*100;
        // In raw ADC trước, rồi mới in các giá trị đã tính
        ESP_LOGI(TAG, "RAW_DC=%d | RAW_VOLT=%d | DC: %.2f A | Volt: %.2f V | Power: %.2f W | Percent: %.2f", 
                 raw_dc, raw_volt, dc, volt, power, percent);

        // Kiểm tra kết nối BLE và notification đã được enable
        if (ble_gatt_is_connected() && ble_gatt_is_notify_enabled())
        {
            // Tạo string chỉ gồm số, cách nhau bằng dấu phẩy
            snprintf(message, sizeof(message), "%.2f,%.2f,%.2f,%.2f", dc, volt, power, percent);

            esp_err_t ret = ble_gatt_send_string(message);
            if(ret == ESP_OK) {
                ESP_LOGI(TAG, "Sent data: %s", message);
            } else {
                ESP_LOGW(TAG, "Failed to send data: %d", ret);
            }
        }
        else
        {
            ESP_LOGW(TAG, "BLE not connected or notify not enabled");
        }

        vTaskDelay(pdMS_TO_TICKS(500)); // Gửi mỗi 500ms
    }
}

// Task BLE init trước
void ble_task(void *pvParameters)
{
    ESP_LOGI(TAG, "BLE Task started, initializing...");
    
    esp_err_t ret = ble_gatt_server_init("ESP32 GATTS");
    if(ret != ESP_OK) {
        ESP_LOGE(TAG, "BLE GATT Server init failed!");
        vTaskDelete(NULL);
        return;
    }
    ESP_LOGI(TAG, "BLE GATT Server initialized - Device name: ESP32 GATTS");

    // Đăng ký callback
    ble_gatt_register_receive_callback(on_ble_data_received);
    ESP_LOGI(TAG, "Receive callback registered");
    
    // Đánh dấu BLE đã sẵn sàng
    ble_ready = true;
    ESP_LOGI(TAG, "BLE ready flag set");

    while(1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}


void app_main(void)
{
    esp_err_t ret;

    ESP_LOGI(TAG, "=== ACS712 BLE Monitor Starting ===");
    
	relay_init();
	ESP_LOGI(TAG, "Relay initialized on GPIO %d", RELAY);

    // Init NVS
    ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // Khởi tạo cảm biến ACS712
    ESP_LOGI(TAG, "Initializing ACS712 sensors...");
    acs712_init(&sensor,ADC_UNIT_1, ADC1_CHANNEL_7,ADC_ATTEN_DB_11,ACS712_5A,vset,0.4f);
    //acs712_init(&voltage,ADC_UNIT_1, ADC1_CHANNEL_6,ADC_ATTEN_DB_11,ACS712_5A,5.3f,0.2f);
    ESP_LOGI(TAG, "ACS712 sensors initialized");

    // Khởi tạo LED
    led_init();
    ESP_LOGI(TAG, "LED initialized on GPIO %d", LED_GPIO);

    // Tạo BLE task TRƯỚC để init xong mới cho ADC task chạy
    ESP_LOGI(TAG, "Creating tasks...");
   
    xTaskCreate(ble_task, "BLE_Task", 8192, NULL, 6, NULL);      // Priority cao hơn
    
    xTaskCreate(led_blink_task, "LED_Task", 2048, NULL, 5, NULL);
    
    xTaskCreate(adc_task, "ADC_Task", 4096, NULL, 4, NULL);      // Priority thấp hơn BLE

    ESP_LOGI(TAG, "All tasks created successfully");
    ESP_LOGI(TAG, "Waiting for BLE connection...");

    while(1) {
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}