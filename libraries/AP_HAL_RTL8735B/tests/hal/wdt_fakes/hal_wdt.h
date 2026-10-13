#pragma once
// 測試替身。出處：名稱與數值依 SDK d1b6426 的 component/soc/8735b/fwlib/rtl8735b/include/hal_wdt.h（Apache-2.0）；只寫受測程式用到的部分，自己撰寫。
#include <stdint.h>

uint8_t hal_wdt_check_wdt_en(const uint8_t wdt_obj);
