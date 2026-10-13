#pragma once
// 測試替身。出處：名稱與數值依 SDK d1b6426 的 component/soc/8735b/fwlib/rtl8735b/include/hal_pinmux.h、component/soc/8735b/fwlib/rtl8735b/include/rtl8735b_peri_id.h（Apache-2.0）；只寫受測程式用到的部分，自己撰寫。
#include <stdint.h>
#include "hal_i2c.h"

// 真實值在 SDK rtl8735b_peri_id.h；替身只需與 bus_shim.c 使用同一符號。
#define PID_SPI0 0x70000004U
#define PID_SPI1 0x70000005U

#ifdef __cplusplus
extern "C" {
#endif
hal_status_t hal_pinmux_unregister(uint32_t pin_name, uint32_t periphl_id);
#ifdef __cplusplus
}
#endif
