/* 主機測試用的 SDK 替身：系統時鐘（微秒）。 */
#pragma once
#include <stdint.h>
uint64_t hal_read_systime_us(void);
