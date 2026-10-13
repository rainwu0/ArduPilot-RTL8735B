#pragma once
// 測試替身。出處：名稱與數值依 SDK d1b6426 的 component/mbed/hal/pwmout_api.h（Apache-2.0）；只寫受測程式用到的部分，自己撰寫。
#include "objects.h"
#ifdef __cplusplus
extern "C" {
#endif
void pwmout_init(pwmout_t *obj, uint32_t pin);
void pwmout_period_us(pwmout_t *obj, int us);
void pwmout_pulsewidth_us(pwmout_t *obj, int us);
#ifdef __cplusplus
}
#endif
