#ifndef ACS712_SIMPLE_H
#define ACS712_SIMPLE_H

#include "driver/adc.h"

typedef enum {
    ACS712_5A = 185,   // 185 mV/A
    ACS712_20A = 100,  // 100 mV/A
    ACS712_30A = 66    // 66 mV/A
} acs712_type_t;

typedef struct {
    adc_unit_t adc_unit;           // ADC1 hoặc ADC2
    adc1_channel_t channel;        // Channel ADC
    adc_atten_t attenuation;       // Attenuation (0dB, 2.5dB, 6dB, 11dB)
    float vcc_sensor;              // VCC nuôi ACS712 (5V hoặc 3.3V)
    float sensitivity;             // Sensitivity (mV/A)
    float ema_value;               // Giá trị EMA filter
    float alpha;                   // Hệ số EMA (0-1)
} acs712_t;

// Khởi tạo với ADC unit và attenuation tùy chọn
void acs712_init(acs712_t *acs, 
                 adc_unit_t adc_unit,
                 adc1_channel_t channel, 
                 adc_atten_t attenuation,
                 acs712_type_t type, 
                 float vcc_sensor, 
                 float alpha);

// Đọc raw ADC trung bình
int acs712_read_raw_avg(acs712_t *acs, int samples);

// Đọc điện áp (mV)
float acs712_get_voltage(acs712_t *acs);

// Đọc dòng DC (A)
float acs712_get_current_dc(acs712_t *acs);

// Set VCC sensor (cho calibration)
void acs712_set_vcc(acs712_t *acs, float vcc_sensor);

// Set attenuation (cho calibration)
void acs712_set_attenuation(acs712_t *acs, adc_atten_t attenuation);

int ema_filter(int new_value, float alpha);
#endif // ACS712_SIMPLE_H