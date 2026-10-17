#pragma once
// 測試替身。名稱照 ArduPilot 的 AP_Common/time.h，實作在測試裡。
#include <time.h>
time_t ap_mktime(const struct tm *t);
