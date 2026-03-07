#include "acs712.h"
#include "esp_log.h"
#include "esp_rom_sys.h"

static const char *TAG = "ACS712";

// Hàm get VREF theo attenuation
static float get_vref_by_atten(adc_atten_t atten) {
    switch(atten) {
        case ADC_ATTEN_DB_0:   return 1100.0f;  // ~1.1V
        case ADC_ATTEN_DB_2_5: return 1500.0f;  // ~1.5V
        case ADC_ATTEN_DB_6:   return 2200.0f;  // ~2.2V
        case ADC_ATTEN_DB_11:  return 3300.0f;  // ~3.3V
        default:               return 3300.0f;
    }
}

// ------------------ INIT ------------------
void acs712_init(acs712_t *acs, 
                 adc_unit_t adc_unit,
                 adc1_channel_t channel,
                 adc_atten_t attenuation,
                 acs712_type_t type, 
                 float vcc_sensor, 
                 float alpha)
{
    acs->adc_unit = adc_unit;
    acs->channel = channel;
    acs->attenuation = attenuation;
    acs->vcc_sensor = vcc_sensor;
    acs->sensitivity = (float)type;
    acs->alpha = alpha;
    
    // Cấu hình ADC
    if (adc_unit == ADC_UNIT_1) {
        adc1_config_width(ADC_WIDTH_BIT_12);
        adc1_config_channel_atten(channel, attenuation);
    } else if (adc_unit == ADC_UNIT_2) {
        adc2_config_channel_atten((adc2_channel_t)channel, attenuation);
    }
    
    // Khởi tạo EMA
    if (adc_unit == ADC_UNIT_1) {
        acs->ema_value = (float)adc1_get_raw(channel);
    } else {
        int raw;
        adc2_get_raw((adc2_channel_t)channel, ADC_WIDTH_BIT_12, &raw);
        acs->ema_value = (float)raw;
    }
    
    const char* atten_str[] = {"0dB", "2.5dB", "6dB", "11dB"};
    float vref = get_vref_by_atten(attenuation);
    
    ESP_LOGI(TAG, "ACS712 ADC%d_CH%d initialized", 
             (adc_unit == ADC_UNIT_1) ? 1 : 2, channel);
    ESP_LOGI(TAG, "  Attenuation: %s (VREF=%.1fmV)", 
             atten_str[attenuation], vref);
    ESP_LOGI(TAG, "  VCC_Sensor: %.2fV | Sensitivity: %dmV/A | Alpha: %.2f",
             vcc_sensor, (int)acs->sensitivity, alpha);
}

// ------------------ ĐỌC RAW ADC TRUNG BÌNH ------------------
int acs712_read_raw_avg(acs712_t *acs, int samples)
{
    long sum = 0;
    
    if (acs->adc_unit == ADC_UNIT_1) {
        for(int i = 0; i < samples; i++) {
            sum += adc1_get_raw(acs->channel);
            esp_rom_delay_us(100);
        }
    } else {
        int raw;
        for(int i = 0; i < samples; i++) {
            adc2_get_raw((adc2_channel_t)acs->channel, ADC_WIDTH_BIT_12, &raw);
            sum += raw;
            esp_rom_delay_us(100);
        }
    }
    
    return sum / samples;
}

// ------------------ ĐIỆN ÁP (mV) ------------------
float acs712_get_voltage(acs712_t *acs)
{
    // Đọc trung bình
    int adc_raw = acs712_read_raw_avg(acs, 100);
    
    // Lọc EMA
    acs->ema_value = acs->alpha * adc_raw + (1.0f - acs->alpha) * acs->ema_value;
    
    // Lấy VREF theo attenuation
    float vref_mv = get_vref_by_atten(acs->attenuation);
    
    // Chuyển sang mV
    float voltage_mv = (acs->ema_value * vref_mv) / 4095.0f;
    
    return voltage_mv;
}

// ------------------ DÒNG DC (A) ------------------
float acs712_get_current_dc(acs712_t *acs)
{
    float voltage_mv = acs712_get_voltage(acs);
    
    // Midpoint = VCC_sensor / 2 
    float midpoint_mv = (acs->vcc_sensor * 1000.0f) / 2.0f;
    float delta_mv = voltage_mv - midpoint_mv;
    
    // Dòng điện (A) = delta_mV / sensitivity
    float current_a = delta_mv / acs->sensitivity;
    
    return current_a;
}

// ------------------ SET VCC SENSOR (CHO CALIBRATION) ------------------
void acs712_set_vcc(acs712_t *acs, float vcc_sensor)
{
    acs->vcc_sensor = vcc_sensor;
    ESP_LOGI(TAG, "VCC_Sensor updated to %.2fV (Midpoint now %.0fmV)",
             vcc_sensor, (vcc_sensor * 1000.0f) / 2.0f);
}

// ------------------ SET ATTENUATION (CHO CALIBRATION) ------------------
void acs712_set_attenuation(acs712_t *acs, adc_atten_t attenuation)
{
    acs->attenuation = attenuation;
    
    if (acs->adc_unit == ADC_UNIT_1) {
        adc1_config_channel_atten(acs->channel, attenuation);
    } else {
        adc2_config_channel_atten((adc2_channel_t)acs->channel, attenuation);
    }
    
    const char* atten_str[] = {"0dB", "2.5dB", "6dB", "11dB"};
    float vref = get_vref_by_atten(attenuation);
    
    ESP_LOGI(TAG, "Attenuation updated to %s (VREF=%.1fmV)", 
             atten_str[attenuation], vref);
}
//-----------EMA filter---------------//
int ema_filter(int new_value, float alpha)
{
    static float ema = 0;
    static bool initialized = false;
    
    if (!initialized) {
        ema = new_value;
        initialized = true;
    }
    
    ema = alpha * new_value + (1 - alpha) * ema;
    return ema;
}