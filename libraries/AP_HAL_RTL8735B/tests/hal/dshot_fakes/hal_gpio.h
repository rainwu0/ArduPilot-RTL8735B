#pragma once
// 測試替身。出處：名稱與型別依 SDK d1b6426 的 component/soc/8735b/fwlib/rtl8735b/include/hal_gpio.h 與
// rtl8735b_gpio.h（Apache-2.0）；只寫受測程式用到的欄位與函式，自己撰寫。
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef enum {
    HAL_OK = 0x00,
    HAL_ERR_PARA = 0x03,
    HAL_ERR_HW = 0x05,
} hal_status_t;
typedef enum {
    GPIO_IN = 0,
    GPIO_OUT = 1,
} gpio_dir_t;
typedef struct hal_gpio_pin_adapter_s {
    uint8_t pin_name;
    uint8_t port_idx;
    uint8_t pin_idx;
    uint32_t bit_mask;
    volatile uint32_t *in_port;
    volatile uint32_t *out0_port;
    volatile uint32_t *out1_port;
    volatile uint32_t *outt_port;
} hal_gpio_adapter_t, *phal_gpio_adapter_t;
hal_status_t hal_gpio_init(phal_gpio_adapter_t pgpio_adapter, uint32_t pin_name);
void hal_gpio_deinit(phal_gpio_adapter_t pgpio_adapter);
void hal_gpio_set_dir(phal_gpio_adapter_t pgpio_adapter, gpio_dir_t dir);
void hal_gpio_write(phal_gpio_adapter_t pgpio_adapter, uint32_t io_data);
#ifdef __cplusplus
}
#endif
