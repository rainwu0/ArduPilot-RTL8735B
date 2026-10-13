#pragma once
// 測試替身。出處：名稱與數值依 SDK d1b6426 的 component/mbed/hal/gpio_irq_api.h（Apache-2.0）；只寫受測程式用到的部分，自己撰寫。
#include "objects.h"
#ifdef __cplusplus
extern "C" {
#endif
int gpio_irq_init(gpio_irq_t *obj, uint32_t pin, gpio_irq_handler handler, uint32_t id);
void gpio_irq_set(gpio_irq_t *obj, gpio_irq_event event, uint32_t enable);
void gpio_irq_disable(gpio_irq_t *obj);
#ifdef __cplusplus
}
#endif
