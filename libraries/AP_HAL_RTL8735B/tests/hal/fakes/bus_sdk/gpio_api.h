#pragma once
// 測試替身。出處：名稱與數值依 SDK d1b6426 的 component/mbed/hal/gpio_api.h（Apache-2.0）；只寫受測程式用到的部分，自己撰寫。
#include <stdint.h>
typedef struct { uint32_t pin; } gpio_t;
typedef enum { PIN_INPUT = 0, PIN_OUTPUT = 1 } PinDirection;
#ifdef __cplusplus
extern "C" {
#endif
void gpio_init(gpio_t *, int pin);
void gpio_dir(gpio_t *, PinDirection);
void gpio_write(gpio_t *, int value);
int gpio_read(gpio_t *);
#ifdef __cplusplus
}
#endif
