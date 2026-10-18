#pragma once
// 測試替身。出處：名稱依 SDK d1b6426 的 component/soc/8735b/fwlib/rtl8735b/include/hal_timer.h（Apache-2.0）；
// 只宣告受測程式用到的函式，自己撰寫。
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
uint32_t hal_read_curtime_us(void);
void hal_delay_us(uint32_t time_us);
#ifdef __cplusplus
}
#endif
