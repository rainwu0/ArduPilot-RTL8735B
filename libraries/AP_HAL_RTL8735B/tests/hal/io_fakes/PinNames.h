#pragma once
// 測試替身。出處：名稱與數值依 SDK d1b6426 的 component/mbed/targets/hal/rtl8735b/PinNames.h（Apache-2.0）；只寫受測程式用到的部分，自己撰寫。
typedef enum {
    PIN_A2 = (0 << 5) | 2,
    PIN_A3 = (0 << 5) | 3,
    PIN_D17 = (3 << 5) | 17,
    PIN_E6 = (4 << 5) | 6,
    PIN_F0 = (5 << 5) | 0,
    PIN_F9 = (5 << 5) | 9,
    PIN_F11 = (5 << 5) | 11,
    PIN_F12 = (5 << 5) | 12,
    PIN_F13 = (5 << 5) | 13,
    PIN_F14 = (5 << 5) | 14,
    PIN_F3 = (5 << 5) | 3,
    PIN_F15 = (5 << 5) | 15,
} PinName;
