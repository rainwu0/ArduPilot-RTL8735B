#pragma once
// 測試替身。出處：名稱與數值依 SDK d1b6426 的 component/mbed/hal/gpio_api.h（Apache-2.0）；只寫受測程式用到的部分，自己撰寫。
#include "objects.h"
#ifdef __cplusplus
extern "C" {
#endif
void gpio_init(gpio_t *obj, uint32_t pin);
void gpio_mode(gpio_t *obj, PinMode mode);
void gpio_dir(gpio_t *obj, PinDirection direction);
void gpio_write(gpio_t *obj, int value);
int gpio_read(gpio_t *obj);
#ifdef __cplusplus
}
#endif
