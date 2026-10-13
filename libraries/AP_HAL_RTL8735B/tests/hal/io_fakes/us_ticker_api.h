#pragma once
// 測試替身。出處：名稱與數值依 SDK d1b6426 的 component/mbed/hal/us_ticker_api.h（Apache-2.0）；只寫受測程式用到的部分，自己撰寫。
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
uint32_t us_ticker_read(void);
#ifdef __cplusplus
}
#endif
