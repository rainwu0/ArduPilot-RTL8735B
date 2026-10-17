#pragma once
// 測試替身。名稱照 ArduPilot 的 AP_RTC，行為由測試定義。
#include <stdint.h>
class AP_RTC {
public:
    bool get_utc_usec(uint64_t &usec) const;
};
namespace AP {
AP_RTC &rtc();
}
