#pragma once
// 測試替身。出處：名稱與數值依 SDK d1b6426 的 component/mbed/hal_ext/gpio_ex_api.h（Apache-2.0）；只寫受測程式用到的部分，自己撰寫。
#include "gpio_api.h"
#ifdef __cplusplus
extern "C" {
#endif
void gpio_deinit(gpio_t *);
#ifdef __cplusplus
}
#endif
