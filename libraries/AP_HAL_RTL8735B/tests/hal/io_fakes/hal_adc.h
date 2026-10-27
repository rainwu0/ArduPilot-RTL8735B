#pragma once
// 測試替身。出處：名稱與數值依 SDK d1b6426 的 component/soc/8735b/fwlib/rtl8735b/include/hal_adc.h（Apache-2.0）；只寫受測程式用到的部分，自己撰寫。
#include "objects.h"
#define HAL_OK 0
#define HP_ADC_INPUT_ALL_SINGLE 1
// 出廠校正參數的位址（SDK rtl8735b_adc.h 的 HP_ADC_GAIN_DENO_ADDR 等）
#define HP_ADC_GAIN_DENO_ADDR 0x4A8
#define HP_ADC_GAIN_MOLE_ADDR 0x4AA
#define HP_ADC_OFFSET_DENO_ADDR 0x4AC
#define HP_ADC_OFFSET_MOLE_ADDR 0x4AE
#ifdef __cplusplus
extern "C" {
#endif
void hal_adc_load_default(hal_adc_adapter_t *obj);
hal_status_t hal_adc_init(hal_adc_adapter_t *obj);
hal_status_t hal_adc_set_cvlist(hal_adc_adapter_t *obj, uint8_t *channels, uint8_t count);
void hal_adc_set_in_type_all(hal_adc_adapter_t *obj, uint32_t type);
hal_status_t hal_adc_deinit(hal_adc_adapter_t *obj);
uint16_t hal_adc_single_read(hal_adc_adapter_t *obj, uint8_t channel);
void hal_adc_read_cali_param(uint16_t addr, uint8_t *param_addr, uint8_t param_len);
uint32_t hal_adc_calc_cali_val(uint16_t adc_read_val, hal_adc_cali_para_t *phal_adc_cali_para);
#ifdef __cplusplus
}
#endif
