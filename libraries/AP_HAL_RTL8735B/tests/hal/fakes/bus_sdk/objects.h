#pragma once
// 測試替身。出處：名稱與數值依 SDK d1b6426 的 component/mbed/targets/hal/rtl8735b/objects.h、component/mbed/targets/hal/rtl8735b/PinNames.h（Apache-2.0）；只寫受測程式用到的部分，自己撰寫。
#include <stdint.h>
#include "hal_i2c.h"
#include "hal_ssi.h"
typedef enum {
    PIN_A0 = 0, PIN_A1 = 1,   // SDK 的 PIN_A0 是 0（rtl8735b_pin_name.h），是合法腳位
    PIN_E1 = 33, PIN_E2 = 34, PIN_E3 = 35, PIN_E4 = 36, PIN_E5 = 37, PIN_E6 = 38,
    PIN_F1 = 41, PIN_F2 = 42, PIN_F5 = 45, PIN_F6 = 46, PIN_F7 = 47, PIN_F8 = 48, NC = -1
} PinName;
typedef struct { hal_i2c_adapter_t i2c_adp; } i2c_t;
typedef struct {
    phal_ssi_adaptor_t hal_ssi_adaptor;
    volatile uint32_t state;
} spi_t;
