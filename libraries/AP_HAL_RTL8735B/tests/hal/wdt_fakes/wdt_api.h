#pragma once
// 測試替身。出處：名稱與數值依 SDK d1b6426 的 component/mbed/hal_ext/wdt_api.h（Apache-2.0）；只寫受測程式用到的部分，自己撰寫。
#include <stdint.h>

void watchdog_init(uint32_t timeout_ms);
void watchdog_start(void);
void watchdog_stop(void);
void watchdog_refresh(void);
