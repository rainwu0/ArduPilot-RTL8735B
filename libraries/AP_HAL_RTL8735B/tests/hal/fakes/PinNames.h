#pragma once
// 測試替身。出處：名稱與數值依 SDK d1b6426 的 component/soc/8735b/fwlib/rtl8735b/include/rtl8735b_pin_name.h（Apache-2.0）；只寫受測程式用到的部分，自己撰寫。

// SDK rtl8735b_pin_name.h：PIN_NAME(port, pin) = (port << 5) | pin。
enum {
    PIN_A2 = 2,
    PIN_A3 = 3,
    PIN_D15 = (3 << 5) | 15,
    PIN_D16 = (3 << 5) | 16,
    PIN_E1 = (4 << 5) | 1,
    PIN_E2 = (4 << 5) | 2,
    PIN_F3 = (5 << 5) | 3,
    PIN_F4 = (5 << 5) | 4,
};
