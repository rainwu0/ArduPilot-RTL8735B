#pragma once
// 測試替身。出處：名稱與數值依 SDK d1b6426 的 component/soc/8735b/fwlib/rtl8735b/include/hal_adc.h（Apache-2.0）；只寫受測程式用到的部分，自己撰寫。
#include "objects.h"
#define HAL_OK 0
#define HP_ADC_INPUT_ALL_SINGLE 1
#ifdef __cplusplus
extern "C" {
#endif
void hal_adc_load_default(hal_adc_adapter_t *obj);
hal_status_t hal_adc_init(hal_adc_adapter_t *obj);
hal_status_t hal_adc_set_cvlist(hal_adc_adapter_t *obj, uint8_t *channels, uint8_t count);
void hal_adc_set_in_type_all(hal_adc_adapter_t *obj, uint32_t type);
hal_status_t hal_adc_deinit(hal_adc_adapter_t *obj);
uint16_t hal_adc_single_read(hal_adc_adapter_t *obj, uint8_t channel);
#ifdef __cplusplus
}
#endif
