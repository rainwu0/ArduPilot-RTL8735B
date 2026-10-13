// wdt_shim.c 的主機測試：以替身 SDK 確認呼叫順序、啟動確認與 BOOT_REASON 的讀取與清除。
// 讀取結果在第一次呼叫後就固定，所以每個情境各跑一個行程：wdt-shim-test <情境>。
#include "wdt_shim.h"

#include <assert.h>
#include <stdint.h>
#include <string.h>

#include "cmsis.h"
#include "hal_wdt.h"
#include "wdt_api.h"

uint32_t wdt_fake_aon_regs[0x200 / 4];

#define BOOT_REASON (wdt_fake_aon_regs[0x104 / 4])
#define BIT(n) (1UL << (n))

static unsigned order;
static unsigned init_order;
static unsigned start_order;
static uint32_t init_timeout_ms;
static unsigned refresh_calls;
static unsigned check_calls;
static uint8_t check_obj;
static uint8_t check_result;

void watchdog_init(uint32_t timeout_ms)
{
    init_order = ++order;
    init_timeout_ms = timeout_ms;
}

void watchdog_start(void)
{
    start_order = ++order;
}

void watchdog_stop(void)
{
    assert(0);
}

void watchdog_refresh(void)
{
    refresh_calls++;
}

uint8_t hal_wdt_check_wdt_en(const uint8_t wdt_obj)
{
    check_calls++;
    check_obj = wdt_obj;
    return check_result;
}

// 先設逾時再啟動，啟動後讀 VNDR 看門狗的致能位元；讀回 0 時回報 -1。餵狗只呼叫 refresh。
static void scenario_start(void)
{
    check_result = 1;
    assert(rtl8735b_watchdog_start(2048) == 0);
    assert(init_timeout_ms == 2048);
    assert(init_order == 1 && start_order == 2);
    assert(check_calls == 1 && check_obj == WDT_VNDR_CTRL);
    assert(refresh_calls == 0);
    rtl8735b_watchdog_refresh();
    assert(refresh_calls == 1);

    check_result = 0;
    assert(rtl8735b_watchdog_start(2048) == -1);
}

// 上次是 vendor 看門狗重置：回報 1，只以寫 1 清掉 bit0；其他寫 1 清除的位元（bit1、BOD 的 bit4／5）
// 寫 0 不動，軟體控制的 bit2／3 與未定義的位元寫回原值。之後的呼叫回傳同一個值、不再寫入。
static void scenario_watchdog_reset(void)
{
    BOOT_REASON = BIT(0) | BIT(2) | BIT(4) | BIT(5) | BIT(9);
    assert(rtl8735b_reset_was_watchdog() == 1);
    assert(BOOT_REASON == (BIT(0) | BIT(2) | BIT(9)));
    BOOT_REASON = 0x5a5a0000UL;
    assert(rtl8735b_reset_was_watchdog() == 1);
    assert(BOOT_REASON == 0x5a5a0000UL);
}

// 其他原因的重置（bit0 為 0，AON 看門狗或 BOD 位元為 1）：回報 0，不寫暫存器；之後 bit0 變 1 也不改答案。
static void scenario_other_reset(void)
{
    BOOT_REASON = BIT(1) | BIT(3) | BIT(4);
    assert(rtl8735b_reset_was_watchdog() == 0);
    assert(BOOT_REASON == (BIT(1) | BIT(3) | BIT(4)));
    BOOT_REASON = BIT(0);
    assert(rtl8735b_reset_was_watchdog() == 0);
    assert(BOOT_REASON == BIT(0));
}

int main(int argc, char **argv)
{
    assert(argc == 2);
    if (strcmp(argv[1], "start") == 0) {
        scenario_start();
    } else if (strcmp(argv[1], "watchdog-reset") == 0) {
        scenario_watchdog_reset();
    } else if (strcmp(argv[1], "other-reset") == 0) {
        scenario_other_reset();
    } else {
        assert(0);
    }
    return 0;
}
