#pragma once
// 測試替身。出處：名稱與數值依 SDK d1b6426 的 component/soc/8735b/cmsis/rtl8735b/include/cmsis.h、component/soc/8735b/fwlib/rtl8735b/include/rtl8735b_wdt.h（Apache-2.0）；只寫受測程式用到的部分，自己撰寫。
// wdt_shim.c 主機測試用的最小 cmsis.h：AON 暫存器區改成一般記憶體，WDT 物件代號與 SDK 相同。
#include <stdint.h>

extern uint32_t wdt_fake_aon_regs[0x200 / 4];
#define AON_BASE ((uintptr_t)wdt_fake_aon_regs)

enum {
    WDT_AON_CTRL = 0x11,
    WDT_VNDR_CTRL = 0x12
};
