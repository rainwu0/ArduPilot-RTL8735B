/* SPDX-License-Identifier: GPL-3.0-or-later */

/*
 * 硬體看門狗與開機原因的 C 包裝，理由見 wdt_shim.h。
 *
 * 依據：
 *   - component/mbed/hal_ext/wdt_api.h:48–54：watchdog_init(timeout_ms) 的逾時單位為 ms，
 *     「default action of timeout is to reset the whole system」。
 *   - component/mbed/targets/hal/rtl8735b/wdt_api.c:42–82：watchdog_init 先 hal_wdt_reset(0x17)、再
 *     hal_wdt_init(timeout_ms * 1000)；watchdog_start／watchdog_refresh 直接呼叫 hal_wdt_enable／hal_wdt_refresh。
 *     實際逾時待板上量測。
 *   - hal_wdt_reset 的參數：bit0／1／2／4 為 1 時遮罩 WDOG_RST 對 AON／PON／WLON／SYSON 的重置，
 *     bit3 為 0 時讓 CPU 暖重置，因此 0x17 只重置 CPU。看門狗重置後 PWM 等周邊是否繼續輸出
 *     未查證，待板測。這裡沿用 mbed 的設定，不另改遮罩。
 *   - hal_wdt_check_wdt_en(WDT_VNDR_CTRL) 回報 vendor 看門狗是否啟用（WDT_EN_BYTE，暫存器定義在
 *     fwlib/rtl8735b/lib/include/pub/rtl8735b_vndr_s_type.h:14–31）。推論：mbed watchdog_start
 *     啟動的就是這個 vendor 看門狗；能推翻的證據是啟動後這個位元讀回 0。
 *   - AON_BASE 為 0x40009000（cmsis/rtl8735b/include/rtl8735b.h:6965）。開機原因暫存器 BOOT_REASON 在
 *     AON_BASE 偏移 0x104：bit0 表示上次重開機由 vendor 看門狗造成、bit1 表示由 AON 看門狗造成，
 *     bit4／5 為 BOD 狀態，這幾個位元都是寫 1 清除；bit2／3 由軟體控制，可讀寫。推論：bootloader 不清 bit0，
 *     應用程式讀得到；能推翻的證據是板上看門狗重置後 bit0 沒有被設起，或一般重置、斷電後 bit0 為 1。
 *   - sys_reset() 呼叫 hal_sys_set_system_reset()（mbed sys_api.c:40–43）。推論：地面站要求的
 *     重開機（Scheduler::reboot）不經 vendor 看門狗、不會設起 bit0；能推翻的證據是重開機後 bit0 為 1。
 */

#include "wdt_shim.h"

#include <cmsis.h>
#include <hal_wdt.h>
#include <wdt_api.h>

#define RTL8735B_AON_BOOT_REASON_OFFSET 0x104UL
#define RTL8735B_BOOT_REASON_VNDR_WDT (1UL << 0)
// 寫 1 清除的位元：bit0、bit1（看門狗）、bit4、bit5（BOD）
#define RTL8735B_BOOT_REASON_W1C_MASK ((1UL << 0) | (1UL << 1) | (1UL << 4) | (1UL << 5))

int rtl8735b_watchdog_start(uint32_t timeout_ms)
{
    watchdog_init(timeout_ms);
    watchdog_start();
    return hal_wdt_check_wdt_en(WDT_VNDR_CTRL) ? 0 : -1;
}

void rtl8735b_watchdog_refresh(void)
{
    watchdog_refresh();
}

uint8_t rtl8735b_reset_was_watchdog(void)
{
    static uint8_t latched;
    static uint8_t was_watchdog;
    if (!latched) {
        volatile uint32_t *const boot_reason =
            (volatile uint32_t *)(AON_BASE + RTL8735B_AON_BOOT_REASON_OFFSET);
        const uint32_t value = *boot_reason;
        was_watchdog = (value & RTL8735B_BOOT_REASON_VNDR_WDT) != 0U;
        if (was_watchdog) {
            // 清掉這次讀到的 vendor 看門狗位元，下一次一般重置才不會再讀到。其他 W1C 位元寫 0（不清 BOD
            // 狀態），軟體控制的 bit2／3 與未定義的位元寫回原值。
            *boot_reason = (value & ~RTL8735B_BOOT_REASON_W1C_MASK) | RTL8735B_BOOT_REASON_VNDR_WDT;
        }
        latched = 1;
    }
    return was_watchdog;
}
